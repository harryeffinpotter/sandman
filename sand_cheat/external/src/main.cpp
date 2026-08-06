// sand_external.exe — external ban-avoidance overlay for sand.exe.
//
// KWARE-style architecture:
//   - We do NOT inject any DLL into the game.
//   - We do NOT set HWBP registers on any game thread.
//   - We do NOT patch any game code or vtables.
//   - We DO talk to the launcher's already-loaded kernel driver via
//     cmdchannel (HAL-hijacked syscall). The driver reads/writes game
//     memory on our behalf using MmCopyVirtualMemory internally.
//   - We render our own overlay window with WDA_EXCLUDEFROMCAPTURE
//     (stream-proof).
//
// Prereq: `sand_launcher.exe` must have run at least once this boot so
// the vulnerable driver + our kernel driver are loaded and the
// syscall-hijack dispatcher is active. If launcher isn't running or
// wasn't run yet, cmdchannel::init/heartbeat will fail and we print
// an actionable message.

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "cmdchannel.h"

// -----------------------------------------------------------------------
// Console + log helpers
// -----------------------------------------------------------------------

static void ext_log(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    char buf[2048];
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a);
    va_end(a);

    // Console echo
    printf("%s", buf);
    fflush(stdout);

    // Persistent file trace so we can eyeball post-run
    FILE* f = nullptr;
    fopen_s(&f, "C:\\Users\\ysg\\projects\\sand_cheat\\external_trace.txt", "a");
    if (f) {
        fprintf(f, "[%lu] %s", GetTickCount(), buf);
        fclose(f);
    }
}

// -----------------------------------------------------------------------
// Process attach — find sand.exe by name
// -----------------------------------------------------------------------

static uint32_t find_pid_by_name(const wchar_t* target) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    uint32_t pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, target) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// -----------------------------------------------------------------------
// External memory reader wrappers
// -----------------------------------------------------------------------

// Read `size` bytes from game process VA `src` into local buffer `dst`.
static bool ext_read(uint32_t pid, uint64_t src, void* dst, uint32_t size) {
    return cmdchannel::read_memory(pid, src, (uint64_t)dst, size);
}

template <typename T>
static bool ext_read_val(uint32_t pid, uint64_t src, T& out) {
    return ext_read(pid, src, &out, sizeof(T));
}

// Read an Il2CppString (Unity boxed System.String) at `strObj` into UTF-8
// buffer. Il2CppString layout: [+0x10] int length, [+0x14] wchar_t data[length].
static bool ext_read_il2cpp_string(uint32_t pid, uint64_t strObj, std::string& out) {
    out.clear();
    if (!strObj) return false;
    int32_t len = 0;
    if (!ext_read_val(pid, strObj + 0x10, len)) return false;
    if (len <= 0 || len > 512) return false;
    std::vector<uint16_t> wchars(len);
    if (!ext_read(pid, strObj + 0x14, wchars.data(), (uint32_t)(len * 2))) return false;
    out.reserve(len);
    for (int i = 0; i < len; i++) out.push_back((char)(wchars[i] & 0xFF));
    return true;
}

// -----------------------------------------------------------------------
// Bootstrap — verify driver channel is alive, find game, map module
// -----------------------------------------------------------------------

struct GameContext {
    uint32_t pid = 0;
    uint64_t game_assembly_base = 0;
    uint32_t game_assembly_size = 0;
    uint64_t sand_exe_base = 0;   // for future use (main module base)
    uint32_t sand_exe_size = 0;
};

static bool ext_bootstrap(GameContext& ctx) {
    ext_log("[boot] cmdchannel::init...\n");
    if (!cmdchannel::init()) {
        ext_log("[boot] cmdchannel::init FAILED. Is sand_launcher.exe loaded?\n");
        return false;
    }

    ext_log("[boot] scanning for sand.exe...\n");
    ctx.pid = find_pid_by_name(L"sand.exe");
    if (!ctx.pid) {
        ext_log("[boot] sand.exe not running.\n");
        return false;
    }
    ext_log("[boot] found sand.exe pid=%u\n", ctx.pid);

    // Heartbeat — write a byte to our own address space via the driver as a
    // liveness ping. Uses our own process memory as target (safe, reversible).
    volatile uint8_t heartbeat_slot = 0;
    if (!cmdchannel::heartbeat((uint64_t)&heartbeat_slot)) {
        ext_log("[boot] cmdchannel::heartbeat FAILED — driver not responding\n");
        return false;
    }
    ext_log("[boot] driver heartbeat OK (slot wrote %u)\n", (unsigned)heartbeat_slot);

    ext_log("[boot] find_module GameAssembly.dll in pid %u...\n", ctx.pid);
    if (!cmdchannel::find_module(ctx.pid, L"GameAssembly.dll",
                                 &ctx.game_assembly_base, &ctx.game_assembly_size)) {
        ext_log("[boot] find_module GameAssembly.dll FAILED\n");
        return false;
    }
    ext_log("[boot] GameAssembly.dll base=%llx size=0x%x\n",
            (unsigned long long)ctx.game_assembly_base, ctx.game_assembly_size);

    cmdchannel::find_module(ctx.pid, L"sand.exe", &ctx.sand_exe_base, &ctx.sand_exe_size);
    ext_log("[boot] sand.exe base=%llx size=0x%x\n",
            (unsigned long long)ctx.sand_exe_base, ctx.sand_exe_size);

    return true;
}

// -----------------------------------------------------------------------
// Sanity dump — proves we can walk PE headers of a mapped module in the
// target process purely via external reads.
// -----------------------------------------------------------------------

static void ext_dump_module_probe(const GameContext& ctx) {
    uint16_t dos_mz = 0;
    if (!ext_read_val(ctx.pid, ctx.game_assembly_base, dos_mz)) {
        ext_log("[probe] failed to read DOS header\n");
        return;
    }
    ext_log("[probe] GameAssembly.dll[0..2] = %04x (expected 5A4D 'MZ')\n", dos_mz);

    uint32_t pe_offset = 0;
    ext_read_val(ctx.pid, ctx.game_assembly_base + 0x3C, pe_offset);
    ext_log("[probe] e_lfanew = %x\n", pe_offset);

    uint32_t nt_sig = 0;
    ext_read_val(ctx.pid, ctx.game_assembly_base + pe_offset, nt_sig);
    ext_log("[probe] NT sig = %08x (expected 4550 'PE\\0\\0')\n", nt_sig);
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    ext_log("========================================\n");
    ext_log("  sand_external.exe (KWARE-style)\n");
    ext_log("  Zero injection, kernel-driver R/W only\n");
    ext_log("========================================\n\n");

    GameContext ctx;
    if (!ext_bootstrap(ctx)) {
        ext_log("[main] bootstrap FAILED — exiting.\n");
        ext_log("[main] Prerequisites:\n");
        ext_log("       1. sand_launcher.exe was run this boot session\n");
        ext_log("       2. driver mapped + syscall hijacked (cmdchannel active)\n");
        ext_log("       3. sand.exe is running\n");
        return 1;
    }

    ext_log("\n[main] bootstrap OK — attached to sand.exe pid=%u\n", ctx.pid);
    ext_dump_module_probe(ctx);

    ext_log("\n[main] scaffolding complete. Overlay + entity scan pending phase 2.\n");
    ext_log("[main] Press Ctrl+C to exit.\n");

    // Keep-alive loop — later this is where the overlay message pump goes.
    for (;;) {
        Sleep(1000);
        // Later: pump ImGui, run scan tick, update overlay.
    }
    return 0;
}

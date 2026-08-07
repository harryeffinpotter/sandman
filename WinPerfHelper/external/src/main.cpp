// PerfMonSvc.exe — external ban-avoidance overlay for sand.exe.
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
// Prereq: `RTSSDriverSvc.exe` must have run at least once this boot so
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
#include "overlay.h"
#include "state.h"
#include "ui.h"
#include "scan.h"
#include "config.h"
#include "opsec.h"
#include "imgui.h"

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
    // Trace file lives in %APPDATA%\Microsoft\PerfCache\ so it blends in
    // with legit Windows caches. Directory is created on first write.
    static bool s_path_ready = false;
    static char s_trace_path[MAX_PATH] = {};
    if (!s_path_ready) {
        char appdata[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
        if (n && n < MAX_PATH) {
            char dir[MAX_PATH];
            snprintf(dir, sizeof(dir), "%s\\Microsoft\\PerfCache", appdata);
            CreateDirectoryA(dir, nullptr);
            snprintf(s_trace_path, sizeof(s_trace_path), "%s\\perfmon.log", dir);
            s_path_ready = true;
        }
    }
    if (s_path_ready) {
        FILE* f = opsec::silent_fopen(s_trace_path, "a");
        if (f) {
            fprintf(f, "[%lu] %s", GetTickCount(), buf);
            fclose(f);
        }
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

static bool ext_bootstrap(state::GameCtx& ctx) {
    ext_log("[boot] cmdchannel::init...\n");
    if (!cmdchannel::init()) {
        ext_log("[boot] cmdchannel::init FAILED. Is RTSSDriverSvc.exe loaded?\n");
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

    cmdchannel::find_module(ctx.pid, L"sand.exe", &ctx.game_exe_base, &ctx.game_exe_size);
    ext_log("[boot] sand.exe base=%llx size=0x%x\n",
            (unsigned long long)ctx.game_exe_base, ctx.game_exe_size);

    return true;
}

// -----------------------------------------------------------------------
// Sanity dump — proves we can walk PE headers of a mapped module in the
// target process purely via external reads.
// -----------------------------------------------------------------------

static void ext_dump_module_probe(const state::GameCtx& ctx) {
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
    ext_log("  PerfMonSvc.exe (KWARE-style)\n");
    ext_log("  Zero injection, kernel-driver R/W only\n");
    ext_log("========================================\n\n");

    config::load();

    if (!opsec::preflight_ok()) {
        ext_log("[main] OpSec preflight FAILED — refusing to attach.\n");
        ext_log("[main] Disable BE / kill offending drivers, then re-run.\n");
        return 4;
    }
    opsec::arm_settle_timer();

    if (!ext_bootstrap(state::g)) {
        ext_log("[main] bootstrap FAILED — exiting.\n");
        ext_log("[main] Prerequisites:\n");
        ext_log("       1. RTSSDriverSvc.exe was run this boot session\n");
        ext_log("       2. driver mapped + syscall hijacked (cmdchannel active)\n");
        ext_log("       3. sand.exe is running\n");
        return 1;
    }

    ext_log("\n[main] bootstrap OK — attached to sand.exe pid=%u\n", state::g.pid);
    ext_dump_module_probe(state::g);

    // Pre-populate memory viewer with GameAssembly.dll base for a first read.
    snprintf(state::g.mem_viewer_addr, sizeof(state::g.mem_viewer_addr),
             "%llX", (unsigned long long)state::g.game_assembly_base);

    ext_log("\n[main] initializing stream-proof overlay...\n");
    if (!overlay::init(state::g.pid)) {
        ext_log("[main] overlay init FAILED\n");
        return 2;
    }
    ext_log("[main] overlay up: game=%p overlay=%p\n",
            overlay::game_hwnd(), overlay::overlay_hwnd());
    ext_log("[main] HOTKEYS: INSERT = click-through toggle, HOME = menu toggle\n");

    // Render loop
    ULONGLONG start_tick = GetTickCount64();
    ULONGLONG last_scan_tick = 0;
    while (overlay::alive()) {
        ui::poll_hotkeys();
        overlay::pump_messages();

        // Scan tick — jittered interval + startup settle window. Avoids
        // regular syscall rhythm that could fingerprint us over minutes.
        ULONGLONG now = GetTickCount64();
        static uint32_t next_delay_ms = 200;
        if (state::g.scan_enabled && opsec::settled()
            && (now - last_scan_tick) >= next_delay_ms) {
            last_scan_tick = now;
            scan::tick();
            next_delay_ms = opsec::next_scan_delay_ms();
        }

        overlay::frame(ui::draw_all);
        state::g.frame_count++;
        if ((state::g.frame_count % 600) == 0) {
            ULONGLONG dt = now - start_tick;
            ext_log("[main] %llu frames / %llu ms  (avg %.1f fps)  entities=%llu  scanMs=%llu\n",
                    (unsigned long long)state::g.frame_count,
                    (unsigned long long)dt,
                    (double)state::g.frame_count * 1000.0 / (double)(dt ? dt : 1),
                    (unsigned long long)state::g.entity_count,
                    (unsigned long long)state::g.last_scan_ms);
        }
        Sleep(1);
    }

    ext_log("[main] overlay window closed — shutting down\n");
    config::save();
    overlay::shutdown();
    return 0;
}

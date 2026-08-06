#include "invoke_stage2.h"
#include "cmdchannel.h"

#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

static void llog(const char* fmt, ...) {
    FILE* f = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\launcher_trace.txt", "a");
    if (!f) return;
    fprintf(f, "[%lu] ", GetTickCount());
    va_list a; va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fflush(f); fclose(f);
}

namespace invoke_stage2 {

namespace {

// ---------------------------------------------------------------------------
// DllMain-invoker shellcode.
//
// Plant target: qword_18355F8C8 — RTSS Path A Present-body dispatch slot. RTSS
// calls this function pointer directly (no args — sub_180071E10 does `call
// cs:qword_18355F8C8` with whatever RCX/RDX/R8 happen to hold; we ignore them).
// Ditto return value — caller ignores RAX. Our stub registers .pdata via
// RtlAddFunctionTable, then calls the Stage-2 entry, writes the marker,
// returns 0.
//
// Choice of F8C8 over EA90 slot redirect (the prior design):
//   - F8C8 is an RTSS-owned .data slot, zero on disk, written by RTSS during
//     its own Present detour install. Disk-diff vs image: invisible.
//   - No fake vtable needed — direct function pointer call.
//   - Same architectural class as the LarpDLL persistent plant (which uses
//     this slot for per-frame dispatch post-arrival).
//
// Layout (102 bytes total):
//    0x00  55                    push rbp
//    0x01  48 89 E5              mov  rbp, rsp
//    0x04  48 83 E4 F0           and  rsp, -16
//    0x08  48 83 EC 20           sub  rsp, 32
//    0x0C  48 B9 <imm64>         mov  rcx, PDATA_VA
//    0x16  BA <imm32>            mov  edx, PDATA_COUNT
//    0x1B  49 B8 <imm64>         mov  r8,  STAGE2_BASE
//    0x25  48 B8 <imm64>         mov  rax, RtlAddFunctionTable
//    0x2F  FF D0                 call rax
//    0x31  48 B9 <imm64>         mov  rcx, STAGE2_BASE
//    0x3B  BA 01 00 00 00        mov  edx, 1          ; DLL_PROCESS_ATTACH
//    0x40  45 33 C0              xor  r8d, r8d        ; lpReserved = NULL
//    0x43  48 B8 <imm64>         mov  rax, ENTRY_VA
//    0x4D  FF D0                 call rax
//    0x4F  48 B8 <imm64>         mov  rax, MARKER_VA
//    0x59  C7 00 BE BA FE CA     mov  dword [rax], 0xCAFEBABE
//    0x5F  33 C0                 xor  eax, eax
//    0x61  48 89 EC              mov  rsp, rbp
//    0x64  5D                    pop  rbp
//    0x65  C3                    ret
//
// Patch offsets into the template (rel to shellcode start):
constexpr uint32_t SHELLCODE_SIZE        = 0x66;  // 102 bytes
constexpr uint32_t PATCH_PDATA_VA_OFF    = 0x0E;
constexpr uint32_t PATCH_PDATA_COUNT_OFF = 0x17;
constexpr uint32_t PATCH_BASE_FT_OFF     = 0x1D;  // stage2_base for RtlAddFunctionTable
constexpr uint32_t PATCH_RTLADDFT_OFF    = 0x27;
constexpr uint32_t PATCH_STAGE2_BASE_OFF = 0x33;  // stage2_base for DllMain
constexpr uint32_t PATCH_ENTRY_VA_OFF    = 0x45;
constexpr uint32_t PATCH_MARKER_VA_OFF   = 0x51;

static const uint8_t SHELLCODE_TEMPLATE[SHELLCODE_SIZE] = {
    0x55,                                              // push rbp
    0x48, 0x89, 0xE5,                                  // mov  rbp, rsp
    0x48, 0x83, 0xE4, 0xF0,                            // and  rsp, -16
    0x48, 0x83, 0xEC, 0x20,                            // sub  rsp, 32
    // RtlAddFunctionTable(pdata_va, pdata_count, stage2_base)
    0x48, 0xB9, 0,0,0,0,0,0,0,0,                       // mov  rcx, imm64 (pdata_va)
    0xBA, 0,0,0,0,                                      // mov  edx, imm32 (pdata_count)
    0x49, 0xB8, 0,0,0,0,0,0,0,0,                       // mov  r8,  imm64 (stage2_base)
    0x48, 0xB8, 0,0,0,0,0,0,0,0,                       // mov  rax, imm64 (RtlAddFunctionTable)
    0xFF, 0xD0,                                        // call rax
    // DllMain(stage2_base, DLL_PROCESS_ATTACH, NULL)
    0x48, 0xB9, 0,0,0,0,0,0,0,0,                       // mov  rcx, imm64 (stage2_base)
    0xBA, 0x01, 0x00, 0x00, 0x00,                      // mov  edx, 1
    0x45, 0x33, 0xC0,                                  // xor  r8d, r8d
    0x48, 0xB8, 0,0,0,0,0,0,0,0,                       // mov  rax, imm64 (entry_va)
    0xFF, 0xD0,                                        // call rax
    // Write marker
    0x48, 0xB8, 0,0,0,0,0,0,0,0,                       // mov  rax, imm64 (marker_va)
    0xC7, 0x00, 0xBE, 0xBA, 0xFE, 0xCA,                // mov  dword [rax], 0xCAFEBABE
    0x33, 0xC0,                                        // xor  eax, eax
    0x48, 0x89, 0xEC,                                  // mov  rsp, rbp
    0x5D,                                              // pop  rbp
    0xC3,                                              // ret
};

// ---------------------------------------------------------------------------
// Invoker parking layout (0x200 total):
//    +0x000  shellcode (102 B, patched at load)
//    +0x080  marker dword (4 B)
// ---------------------------------------------------------------------------
constexpr uint32_t INVOKER_SHELLCODE_OFF = 0x000;
constexpr uint32_t INVOKER_MARKER_OFF    = 0x080;
constexpr uint32_t INVOKER_TOTAL         = 0x200;

constexpr uint32_t MARKER_DONE = 0xCAFEBABEu;

// Path A Present body-dispatch slot. Zero on disk, RTSS's install writes
// sub_180071F60 here. Our plant overwrites with stub_va for one render tick.
//
// Mode-discriminator: F8C8 is populated only in STANDARD-mode RTSS install.
// In MS-Detours-mode (UseDetours=1 profile) F8C8 stays NULL and the
// equivalent body-dispatch lives at stub_base+0x50, where stub_base is
// read from F8C0 (Present cluster's saved-fn slot) and points to RTSS's
// per-process trampoline buffer.
constexpr uint32_t F8C8_RVA      = 0x355F8C8;   // standard-mode plant slot
constexpr uint32_t F8C0_RVA      = 0x355F8C0;   // also = stub_base in Detours mode
constexpr uint32_t STUB_HOOK_OFF = 0x50;        // hook-fn ptr offset within each stub

uint64_t xorshift_next(uint64_t& s) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return s;
}
uint64_t seed_rng() {
    uint64_t s = __rdtsc() ^ GetTickCount64() ^
                 (static_cast<uint64_t>(GetCurrentThreadId()) << 32);
    return s ? s : 0xBADCAFE5DEADBEEFull;
}

// Pick a parking slot that (a) reads as zero from target, and (b) does not
// overlap Stage 2's [stage2_base, stage2_base + stage2_size) region.
bool pick_invoker_parking(uint32_t game_pid,
                          const rtss_inject::Ctx& ctx,
                          uint64_t stage2_base, uint32_t stage2_size,
                          uint64_t& out_va, uint32_t& out_rva) {
    constexpr uint32_t PAGE = 0x1000;
    const uint64_t rtss_base     = ctx.rtss_base_in_game;
    const uint32_t zone_size     = ctx.parking_max_rva - ctx.parking_min_rva;
    const uint32_t addressable   = (zone_size - INVOKER_TOTAL) & ~(PAGE - 1);

    uint64_t rng = seed_rng();
    for (uint32_t attempt = 0; attempt < 8; ++attempt) {
        const uint32_t offset = static_cast<uint32_t>(xorshift_next(rng) % addressable) & ~(PAGE - 1);
        const uint32_t rva    = ctx.parking_min_rva + offset;
        const uint64_t va     = rtss_base + rva;

        // Overlap check against Stage 2 region.
        const uint64_t s2_end    = stage2_base + stage2_size;
        const uint64_t inv_end   = va + INVOKER_TOTAL;
        const bool overlaps = !(inv_end <= stage2_base || va >= s2_end);
        if (overlaps) continue;

        uint8_t probe[INVOKER_TOTAL] = {};
        if (!cmdchannel::read_memory(game_pid, va,
                                     reinterpret_cast<uint64_t>(probe),
                                     INVOKER_TOTAL)) continue;
        bool clean = true;
        for (uint32_t i = 0; i < INVOKER_TOTAL; ++i) {
            if (probe[i] != 0) { clean = false; break; }
        }
        if (!clean) continue;

        out_va  = va;
        out_rva = rva;
        return true;
    }
    return false;
}

} // namespace

bool invoke_dllmain(uint32_t game_pid,
                    const rtss_inject::Ctx& ctx,
                    uint64_t stage2_base,
                    uint32_t stage2_size,
                    uint32_t entry_rva,
                    uint32_t exception_rva,
                    uint32_t exception_size) {
    llog("invoke_dllmain ENTER: pid=%u stage2_base=%016llX stage2_size=0x%X entry_rva=%08X\n",
        game_pid, (unsigned long long)stage2_base, stage2_size, entry_rva);
    llog("  ctx: rtss_base=%016llX parking_min=%08X parking_max=%08X\n",
        (unsigned long long)ctx.rtss_base_in_game, ctx.parking_min_rva, ctx.parking_max_rva);

    if (!ctx.rtss_base_in_game || !ctx.parking_min_rva) {
        llog("FAIL: ctx missing fields\n");
        std::printf("[!] invoke_stage2: ctx missing required fields\n");
        return false;
    }
    if (!stage2_base || !stage2_size || !entry_rva) {
        llog("FAIL: bad stage2 params\n");
        std::printf("[!] invoke_stage2: bad stage2 params "
                    "(base=%016llX size=%u entry=%08X)\n",
                    static_cast<unsigned long long>(stage2_base),
                    stage2_size, entry_rva);
        return false;
    }

    const uint64_t rtss_base = ctx.rtss_base_in_game;

    // (1) Mode detection + plant slot resolution.
    //
    // STANDARD mode: F8C8 populated with sub_180071F60 — plant directly there.
    // DETOURS mode:  F8C8 zero. F8C0 holds stub_base ∈ ~0x7FFD… (per-process
    //                trampoline buffer outside RTSS image). Plant at
    //                stub_base+0x50 where RTSS dispatches the per-Present
    //                hook fn via `jmp [stub+0x50]`.
    const uint64_t f8c8_va = rtss_base + F8C8_RVA;
    const uint64_t f8c0_va = rtss_base + F8C0_RVA;

    uint64_t f8c8_val = 0;
    uint64_t f8c0_val = 0;
    llog("reading F8C8 at %016llX and F8C0 at %016llX\n",
        (unsigned long long)f8c8_va, (unsigned long long)f8c0_va);
    if (!cmdchannel::read_memory(game_pid, f8c8_va,
                                 reinterpret_cast<uint64_t>(&f8c8_val), 8) ||
        !cmdchannel::read_memory(game_pid, f8c0_va,
                                 reinterpret_cast<uint64_t>(&f8c0_val), 8)) {
        llog("FAIL: F8C8/F8C0 read\n");
        std::printf("[!] invoke_stage2: F8C8/F8C0 READ failed\n");
        return false;
    }
    llog("F8C8=%016llX F8C0=%016llX\n", (unsigned long long)f8c8_val, (unsigned long long)f8c0_val);

    uint64_t plant_slot_va = 0;
    uint64_t saved_body    = 0;
    const char* mode_tag   = "?";

    if (f8c8_val != 0) {
        // STANDARD mode — plant directly on F8C8.
        plant_slot_va = f8c8_va;
        saved_body    = f8c8_val;
        mode_tag      = "STANDARD";
    } else if (f8c0_val != 0) {
        // DETOURS mode — F8C0 must point outside RTSS .text (stub buffer).
        const uint64_t rtss_lo = rtss_base;
        const uint64_t rtss_hi = rtss_base + 0x4000000;     // RTSS image ~56 MB
        if (f8c0_val < rtss_lo || f8c0_val >= rtss_hi) {
            plant_slot_va = f8c0_val + STUB_HOOK_OFF;
            mode_tag      = "DETOURS";
            // Read existing hook fn ptr (the saved value we'll restore on cleanup).
            if (!cmdchannel::read_memory(game_pid, plant_slot_va,
                                         reinterpret_cast<uint64_t>(&saved_body), 8)) {
                std::printf("[!] invoke_stage2: stub+0x50 READ failed\n");
                return false;
            }
            if (!saved_body) {
                std::printf("[!] invoke_stage2: stub+0x50 is NULL — Detours stub not yet armed\n");
                return false;
            }
        } else {
            std::printf("[!] invoke_stage2: F8C8=0, F8C0=%016llX in RTSS range "
                        "— RTSS install path not finished yet\n",
                        static_cast<unsigned long long>(f8c0_val));
            return false;
        }
    } else {
        std::printf("[!] invoke_stage2: F8C8 and F8C0 both NULL — RTSS Present install hasn't run\n");
        return false;
    }

    std::printf("[+] invoke_stage2: mode=%s plant_slot=%016llX saved=%016llX\n",
                mode_tag,
                static_cast<unsigned long long>(plant_slot_va),
                static_cast<unsigned long long>(saved_body));
    llog("mode=%s plant_slot_va=%016llX saved_body=%016llX\n",
        mode_tag, (unsigned long long)plant_slot_va, (unsigned long long)saved_body);

    uint64_t inv_va = 0;
    uint32_t inv_rva = 0;
    llog("picking invoker parking...\n");
    if (!pick_invoker_parking(game_pid, ctx, stage2_base, stage2_size,
                              inv_va, inv_rva)) {
        llog("FAIL: no clean invoker parking slot\n");
        std::printf("[!] invoke_stage2: no clean invoker parking slot found\n");
        return false;
    }
    const uint64_t stub_va   = inv_va + INVOKER_SHELLCODE_OFF;
    const uint64_t marker_va = inv_va + INVOKER_MARKER_OFF;
    const uint64_t entry_va  = stage2_base + entry_rva;
    std::printf("[*] invoke_stage2: invoker_parking=%016llX (rva=0x%X) "
                "stub=%016llX marker=%016llX entry=%016llX\n",
                static_cast<unsigned long long>(inv_va), inv_rva,
                static_cast<unsigned long long>(stub_va),
                static_cast<unsigned long long>(marker_va),
                static_cast<unsigned long long>(entry_va));
    llog("invoker: inv_va=%016llX inv_rva=0x%X stub_va=%016llX marker_va=%016llX entry_va=%016llX\n",
        (unsigned long long)inv_va, inv_rva,
        (unsigned long long)stub_va, (unsigned long long)marker_va, (unsigned long long)entry_va);

    // (3) Build invoker parking buffer: patched shellcode only. No fake
    // vtable needed — F8C8 is a direct function pointer, not a vtable slot.
    uint8_t buf[INVOKER_TOTAL] = {};
    uint8_t* sc = buf + INVOKER_SHELLCODE_OFF;
    std::memcpy(sc, SHELLCODE_TEMPLATE, SHELLCODE_SIZE);

    uint64_t pdata_va = stage2_base + exception_rva;
    uint32_t pdata_count = exception_size / 12;  // sizeof(RUNTIME_FUNCTION) = 12

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    uint64_t rtlAddFT = (uint64_t)GetProcAddress(ntdll, "RtlAddFunctionTable");

    std::memcpy(sc + PATCH_PDATA_VA_OFF,    &pdata_va,      8);
    std::memcpy(sc + PATCH_PDATA_COUNT_OFF, &pdata_count,   4);
    std::memcpy(sc + PATCH_BASE_FT_OFF,     &stage2_base,   8);
    std::memcpy(sc + PATCH_RTLADDFT_OFF,    &rtlAddFT,      8);
    std::memcpy(sc + PATCH_STAGE2_BASE_OFF, &stage2_base,   8);
    std::memcpy(sc + PATCH_ENTRY_VA_OFF,    &entry_va,      8);
    std::memcpy(sc + PATCH_MARKER_VA_OFF,   &marker_va,     8);

    llog("shellcode patched: pdata_va=%016llX count=%u rtlAddFT=%016llX stage2_base=%016llX entry_va=%016llX marker_va=%016llX\n",
        (unsigned long long)pdata_va, pdata_count, (unsigned long long)rtlAddFT,
        (unsigned long long)stage2_base, (unsigned long long)entry_va, (unsigned long long)marker_va);
    llog("shellcode bytes[0..16]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
        sc[0],sc[1],sc[2],sc[3],sc[4],sc[5],sc[6],sc[7],sc[8],sc[9],sc[10],sc[11],sc[12],sc[13],sc[14],sc[15]);

    if (!cmdchannel::write_memory(game_pid, inv_va,
                                  reinterpret_cast<uint64_t>(buf),
                                  INVOKER_TOTAL)) {
        llog("FAIL: invoker parking write\n");
        std::printf("[!] invoke_stage2: invoker parking WRITE failed\n");
        return false;
    }
    llog("invoker parking written OK\n");
    std::printf("[+] invoke_stage2: invoker parking staged (shellcode@+0x%X, marker@+0x%X)\n",
                INVOKER_SHELLCODE_OFF, INVOKER_MARKER_OFF);

    // Make invoker page executable via PTE NX clear. VAD stays PAGE_READWRITE,
    // no ZwProtectVirtualMemory syscall for MEM_PRIVATE+EXECUTE (BEDaisy hook).
    const uint32_t inv_prot_size = (INVOKER_TOTAL + 0xFFFu) & ~0xFFFu;
    if (!cmdchannel::set_pte_nx(game_pid, inv_va, inv_prot_size,
                                cmdchannel::PTE_FLAG_CLEAR_NX)) {
        llog("FAIL: invoker PTE NX clear\n");
        std::printf("[!] invoke_stage2: invoker PTE NX clear failed\n");
        return false;
    }
    const uint32_t inv_old_protect = 0x04;
    llog("invoker PTE NX cleared va=%016llX size=0x%X\n",
        (unsigned long long)inv_va, inv_prot_size);
    std::printf("[+] invoke_stage2: invoker PTE NX cleared (VAD stays RW)\n");

    // (6) Plant target = stub_va. Next Present tick, RTSS calls our shellcode
    // directly via the function-pointer dispatch:
    //   - STANDARD mode: sub_180071E10 → call cs:qword_18355F8C8 → stub_va
    //   - DETOURS  mode: sub_180071CA0 (or via stub+0x58 indirect) → stub_va
    //
    // Detours-mode wrinkle: the stub buffer page is RX after RTSS's
    // trampoline-install — direct WriteVM via MmCopyVirtualMemory returns
    // STATUS_ACCESS_VIOLATION on RX pages. Flip protection for the 8-byte
    // plant, restore after.
    //
    // CRITICAL: protection bit selection differs per mode. The page boundary
    // wraps multiple stubs (Present/Present1/ResBuf/CreateSC/CSCHwnd/CSCCw/ExecCL
    // are all packed in one 4KB page at stub_buf+0x1E0..0x480). If we strip
    // execute, any in-flight game→stub call during our window hits DEP.
    //   STANDARD mode: F8C8 in RTSS .data — never executable; PAGE_READWRITE
    //                  is the natural protection.
    //   DETOURS  mode: stub+0x50 in RWX-allocated page — must keep execute
    //                  permission set; use PAGE_EXECUTE_READWRITE.
    constexpr uint32_t PAGE_READWRITE_VAL          = 0x04;
    constexpr uint32_t PAGE_EXECUTE_READWRITE_VAL2 = 0x40;
    const uint32_t plant_new_protect =
        (f8c8_val != 0) ? PAGE_READWRITE_VAL : PAGE_EXECUTE_READWRITE_VAL2;
    uint32_t plant_old_protect = 0;
    bool slot_protected = cmdchannel::protect_memory(
        game_pid, plant_slot_va, 8, plant_new_protect, &plant_old_protect);
    if (!slot_protected) {
        std::printf("[!] invoke_stage2: plant slot PROTECT -> RW failed (%s mode)\n", mode_tag);
        uint32_t discard = 0;
        (void)cmdchannel::protect_memory(game_pid, inv_va, INVOKER_TOTAL,
                                         inv_old_protect, &discard);
        return false;
    }
    std::printf("[*] invoke_stage2: plant slot PROTECT 0x%X -> 0x%X (%s)\n",
                plant_old_protect, plant_new_protect, mode_tag);

    if (!cmdchannel::write_memory(game_pid, plant_slot_va,
                                  reinterpret_cast<uint64_t>(&stub_va), 8)) {
        std::printf("[!] invoke_stage2: plant slot WRITE failed (%s mode)\n", mode_tag);
        uint32_t discard = 0;
        (void)cmdchannel::protect_memory(game_pid, plant_slot_va, 8,
                                         plant_old_protect, &discard);
        (void)cmdchannel::protect_memory(game_pid, inv_va, INVOKER_TOTAL,
                                         inv_old_protect, &discard);
        return false;
    }
    llog("PLANTED stub_va=%016llX at slot %016llX\n", (unsigned long long)stub_va, (unsigned long long)plant_slot_va);
    std::printf("[+] invoke_stage2: planted at %016llX — waiting for DllMain marker\n",
                static_cast<unsigned long long>(plant_slot_va));

    // (7) Poll for marker. DllMain likely returns on the first render tick
    // after redirect, so 5 s timeout is generous. If ExecuteCommandLists
    // hook is firing at 60+ FPS, marker lands within ~16-200 ms.
    constexpr uint32_t POLL_INTERVAL_MS = 50;
    constexpr uint32_t POLL_TIMEOUT_MS  = 5000;
    constexpr uint32_t MAX_POLLS        = POLL_TIMEOUT_MS / POLL_INTERVAL_MS;

    bool fired = false;
    uint32_t elapsed_ms = 0;
    for (uint32_t i = 0; i < MAX_POLLS; ++i) {
        Sleep(POLL_INTERVAL_MS);
        elapsed_ms += POLL_INTERVAL_MS;
        uint32_t mark = 0;
        if (!cmdchannel::read_memory(game_pid, marker_va,
                                     reinterpret_cast<uint64_t>(&mark), 4)) {
            std::printf("[!] invoke_stage2: marker READ failed during poll\n");
            break;
        }
        if (mark == MARKER_DONE) {
            llog("MARKER 0xCAFEBABE seen after %u ms\n", elapsed_ms);
            std::printf("[+] invoke_stage2: MARKER 0xCAFEBABE seen after %u ms — DllMain returned\n",
                        elapsed_ms);
            fired = true;
            break;
        }
        if (i < 5 || (i % 20 == 0)) {
            llog("poll %u/%u: marker=0x%08X (waiting...)\n", i, MAX_POLLS, mark);
        }
    }
    if (!fired) {
        llog("TIMEOUT: DllMain did NOT complete in %u ms\n", POLL_TIMEOUT_MS);
        std::printf("[!] invoke_stage2: timeout — DllMain did NOT complete within %u ms\n",
                    POLL_TIMEOUT_MS);
    }

    // (8) Restore plant slot = saved_body so no NEW render ticks enter our
    // stub. LarpDLL's RealThreadProc replants on its own (mode-aware) once
    // DllMain returns — race-safe since the saved_body we read matches what
    // LarpDLL's plant logic expects for its mode.
    llog("STEP 8: restoring plant slot va=%016llX -> saved_body=%016llX\n",
         (unsigned long long)plant_slot_va, (unsigned long long)saved_body);
    bool restore_ok = cmdchannel::write_memory(game_pid, plant_slot_va,
                             reinterpret_cast<uint64_t>(&saved_body), 8);
    llog("STEP 8: plant slot write %s\n", restore_ok ? "OK" : "FAIL");

    uint64_t slot_readback = 0;
    if (cmdchannel::read_memory(game_pid, plant_slot_va,
                                reinterpret_cast<uint64_t>(&slot_readback), 8)) {
        llog("STEP 8: plant slot readback=%016llX (expected=%016llX) match=%d\n",
             (unsigned long long)slot_readback, (unsigned long long)saved_body,
             (slot_readback == saved_body) ? 1 : 0);
    } else {
        llog("STEP 8: plant slot readback FAILED\n");
    }

    // Restore plant slot's original page protection (RX in DETOURS mode, no-op
    // in STANDARD mode where it was already RW).
    {
        uint32_t discard = 0;
        bool prot_ok = cmdchannel::protect_memory(game_pid, plant_slot_va, 8,
                                   plant_old_protect, &discard);
        llog("STEP 8b: plant slot protect restore prot=0x%X %s\n",
             plant_old_protect, prot_ok ? "OK" : "FAIL");
    }
    std::printf("[+] invoke_stage2: %s plant slot restored (val=saved_body, prot=0x%X)\n",
                mode_tag, plant_old_protect);

    // (8b) DRAIN: F8C8 fires once per Present. Marker fires on the first
    // call, but another shellcode invocation may be mid-flight (inside
    // DllMain) when the launcher enters cleanup. If we zero the shellcode
    // or flip the invoker page from RWX → RW while that call is running,
    // its return from DllMain AVs at stub+0x4F (post `call rax`) → Theia
    // killswitch kills the game. Sleep 500 ms bounds the worst case.
    llog("STEP 8c: draining in-flight shellcode Sleep(500)\n");
    std::printf("[*] invoke_stage2: draining in-flight shellcode (500 ms)...\n");
    Sleep(500);
    llog("STEP 8c: drain complete\n");

    // (9) Scrub invoker parking only. Stage 2 region stays resident —
    // DllMain is done but hook_e900_asm + shadow_onpaint_asm live in
    // Stage 2 .text and must remain executable.
    llog("STEP 9: zeroing invoker parking va=%016llX size=0x%X\n",
         (unsigned long long)inv_va, (unsigned)INVOKER_TOTAL);
    uint8_t zero_buf[INVOKER_TOTAL] = {};
    bool zero_ok = cmdchannel::write_memory(game_pid, inv_va,
                             reinterpret_cast<uint64_t>(zero_buf),
                             INVOKER_TOTAL);
    llog("STEP 9: invoker zero write %s\n", zero_ok ? "OK" : "FAIL");
    uint32_t discard = 0;
    bool inv_prot_ok = cmdchannel::protect_memory(game_pid, inv_va, INVOKER_TOTAL,
                               inv_old_protect, &discard);
    llog("STEP 9: invoker protect restore prot=0x%X %s\n",
         inv_old_protect, inv_prot_ok ? "OK" : "FAIL");

    uint64_t s2_first_qw = 0xAAAAAAAAAAAAAAAAull;
    uint64_t s2_last_qw  = 0xBBBBBBBBBBBBBBBBull;
    bool s2_rd1 = cmdchannel::read_memory(game_pid, stage2_base,
                                  reinterpret_cast<uint64_t>(&s2_first_qw), 8);
    bool s2_rd2 = cmdchannel::read_memory(game_pid, stage2_base + stage2_size - 8,
                                  reinterpret_cast<uint64_t>(&s2_last_qw), 8);
    llog("STEP 10: stage2 read1=%d(%016llX) read2=%d(%016llX) @%016llX+0x%X\n",
         s2_rd1 ? 1 : 0, (unsigned long long)s2_first_qw,
         s2_rd2 ? 1 : 0, (unsigned long long)s2_last_qw,
         (unsigned long long)stage2_base, stage2_size);

    uint64_t s2_entry_qw = 0xCCCCCCCCCCCCCCCCull;
    bool s2_rd3 = cmdchannel::read_memory(game_pid, stage2_base + entry_rva,
                                  reinterpret_cast<uint64_t>(&s2_entry_qw), 8);
    llog("STEP 10b: stage2 entry read=%d val=%016llX @%016llX\n",
         s2_rd3 ? 1 : 0, (unsigned long long)s2_entry_qw,
         (unsigned long long)(stage2_base + entry_rva));

    llog("invoker parking scrubbed, returning fired=%d\n", fired?1:0);
    std::printf("[+] invoke_stage2: invoker parking scrubbed\n");

    return fired;
}

} // namespace invoke_stage2

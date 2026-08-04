#include "rtss_inject.h"

#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>

#include "cmdchannel.h"

namespace rtss_inject {

// ---------------------------------------------------------------------------
// Known-version table.
//
// `parking_{min,max}_rva` is the dormant-virtual parking zone verified by IDA
// static analysis of RIP-relative refs from .text into .data (see §2.9.g in
// injection-info.txt). A 1 MB buffer is applied against the nearest touched
// page on each side so minor build-to-build drift still leaves headroom.
// Phase 16.b.beta picks a random 4 KB-aligned offset within this range per
// run — no static "park at X" signature survives in the launcher binary.
// ---------------------------------------------------------------------------
struct KnownBuild {
    uint32_t tds;
    uint32_t soi;
    const char* label;
    // Hijack target + parking
    uint32_t d3d12_obj_rva;         // A50F0 — per-slot command-list array base
    uint32_t parking_min_rva;
    uint32_t parking_max_rva;
    // Readiness probes (per-slot arrays at slot 0)
    uint32_t slot_fence_a5050_rva;
    uint32_t slot_fence_a50b0_rva;
    uint32_t slot_prereq_a4fd0_rva;
    uint32_t slot_prereq_a5290_rva;
    uint32_t slot_frame_counter_rva;
    // Master render-enable 6-way OR-gate (sub_18004B090)
    uint32_t master_gate_rvas[6];
    // Ancillary diagnostic probes
    uint32_t render_mode_rva;
    uint32_t render_state_rva;
    uint32_t swapchain_ptr_rva;
    uint32_t render_duration_rva;
};

static const KnownBuild KNOWN[] = {
    // RTSS 7.3.5/7.3.6 — all RVAs from IDA reversal 2026-04-18/19 against
    // image base 0x180000000. Cross-reference: cerebrum Key Learnings
    // "DX12 render-tick anatomy" entry for the sources.
    //
    // Full vtable offset set the stub must tolerate on A50F0[N]:
    //   {0x48, 0x50, 0x60, 0x78, 0x80, 0xA0, 0xA8, 0xB0,
    //    0xC8, 0xD0, 0xE0, 0xF0, 0x160, 0x170, 0x180}
    // (15 distinct offsets — command-list semantics).
    //
    // Parking zone: 51.15 MB dormant gap between 0x1F0000 and 0x3516000,
    // 1 MB buffer each side => safe range 0x2F0000..0x3416000 (~49 MB).
    {
        /* tds                */ 0x65FE9A62,
        /* soi                */ 0x3586000,
        /* label              */ "RTSS 7.3.5/7.3.6 (sample target — D3D12)",
        /* d3d12_obj_rva      */ 0x1A50F0,
        /* parking_min_rva    */ 0x2F0000,
        /* parking_max_rva    */ 0x3416000,
        /* slot_fence_a5050   */ 0x1A5050,
        /* slot_fence_a50b0   */ 0x1A50B0,
        /* slot_prereq_a4fd0  */ 0x1A4FD0,
        /* slot_prereq_a5290  */ 0x1A5290,
        /* slot_frame_counter */ 0x1A5090,
        /* master_gate_rvas   */ { 0x3519860, 0x3519888, 0x35198B0,
                                   0x35198D8, 0x3519900, 0x3561420 },
        /* render_mode_rva    */ 0x3561B8C,
        /* render_state_rva   */ 0x3561BA8,
        /* swapchain_ptr_rva  */ 0x355A9A8,
        /* render_duration_rva*/ 0x355A968,
    },
};

static const KnownBuild* find_known_build(uint32_t tds, uint32_t soi) {
    for (const auto& kb : KNOWN) {
        if (kb.tds == tds && kb.soi == soi) return &kb;
    }
    return nullptr;
}

// rdtsc-seeded xorshift64 — throwaway RNG for parking-offset selection.
// Not crypto; just enough entropy so offset differs per run.
static uint64_t xorshift_next(uint64_t& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}
static uint64_t seed_rng() {
    uint64_t s = __rdtsc();
    s ^= GetTickCount64();
    s ^= static_cast<uint64_t>(GetCurrentThreadId()) << 32;
    if (s == 0) s = 0xDEADBEEFCAFEBABEull;  // xorshift degenerates at zero
    return s;
}

bool wait_for_rtss_in_game(uint32_t game_pid, Ctx& ctx, uint32_t timeout_ms) {
    ctx = {};

    constexpr uint32_t POLL_INTERVAL_MS = 200;
    const uint32_t     max_iters        = timeout_ms / POLL_INTERVAL_MS;

    std::printf("[*] rtss_inject: looking for RTSSHooks64.dll in PID %u "
                "(timeout=%u ms)\n", game_pid, timeout_ms);

    for (uint32_t i = 0; i < max_iters; ++i) {
        uint64_t base = 0;
        uint32_t size = 0;
        if (cmdchannel::find_module(game_pid, L"RTSSHooks64.dll", &base, &size)
            && base) {
            ctx.rtss_base_in_game = base;
            ctx.rtss_size_in_game = size;
            std::printf("[+] rtss_inject: RTSS in game @ %016llX (size=0x%X) "
                        "after %u ms\n",
                        (unsigned long long)base, size, i * POLL_INTERVAL_MS);
            return true;
        }
        if ((i * POLL_INTERVAL_MS) % 1000 == 0 && i != 0) {
            std::printf("    still waiting... (%u ms elapsed)\n",
                        i * POLL_INTERVAL_MS);
        }
        Sleep(POLL_INTERVAL_MS);
    }

    std::printf("[!] rtss_inject: timeout — RTSS not in PID %u\n", game_pid);
    std::printf("[!] check tester-side setup:\n");
    std::printf("[!]   1. RTSS is installed at C:\\Program Files (x86)\\RivaTuner Statistics Server\\\n");
    std::printf("[!]   2. RTSS is running (RTSS.exe + possibly MSIAfterburner.exe in Task Manager)\n");
    std::printf("[!]   3. Target game is listed in RTSS's application detection with level=HIGH\n");
    std::printf("[!]   4. Game is a GUI process pumping messages (console apps won't trigger the global hook)\n");
    return false;
}

bool verify_in_game_rtss(uint32_t game_pid, Ctx& ctx) {
    if (!ctx.rtss_base_in_game) return false;

    uint8_t hdr[0x400] = {};
    if (!cmdchannel::read_memory(
            game_pid,
            ctx.rtss_base_in_game,
            reinterpret_cast<uint64_t>(hdr),
            sizeof(hdr))) {
        std::printf("[!] rtss_inject: cmd 0 READ on RTSS header failed\n");
        return false;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hdr);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        std::printf("[!] rtss_inject: RTSS header MZ missing (%04X)\n", dos->e_magic);
        return false;
    }
    if (dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > sizeof(hdr)) {
        std::printf("[!] rtss_inject: PE header beyond 0x400 (e_lfanew=%X)\n",
                    dos->e_lfanew);
        return false;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(hdr + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        std::printf("[!] rtss_inject: PE signature missing (%08X)\n", nt->Signature);
        return false;
    }

    ctx.rtss_time_date_stamp = nt->FileHeader.TimeDateStamp;
    ctx.rtss_size_of_image   = nt->OptionalHeader.SizeOfImage;

    std::printf("[+] rtss_inject: in-game RTSS build:\n");
    std::printf("    TimeDateStamp = 0x%08X\n", ctx.rtss_time_date_stamp);
    std::printf("    SizeOfImage   = 0x%X\n", ctx.rtss_size_of_image);

    const KnownBuild* kb = find_known_build(ctx.rtss_time_date_stamp,
                                            ctx.rtss_size_of_image);
    if (!kb) {
        std::printf("[!] rtss_inject: UNKNOWN RTSS build — descriptor + parking RVAs not in table\n");
        std::printf("[!]   Phase 16.b.gamma will need to resolve descriptor via runtime scan,\n");
        std::printf("[!]   or add this build to the KNOWN table after reversing its RTSSHooks64.dll\n");
        return true;  // not fatal — we just haven't seen this build before
    }

    ctx.d3d12_obj_rva         = kb->d3d12_obj_rva;
    ctx.parking_min_rva       = kb->parking_min_rva;
    ctx.parking_max_rva       = kb->parking_max_rva;
    ctx.slot_fence_a5050_rva  = kb->slot_fence_a5050_rva;
    ctx.slot_fence_a50b0_rva  = kb->slot_fence_a50b0_rva;
    ctx.slot_prereq_a4fd0_rva = kb->slot_prereq_a4fd0_rva;
    ctx.slot_prereq_a5290_rva = kb->slot_prereq_a5290_rva;
    ctx.slot_frame_counter_rva = kb->slot_frame_counter_rva;
    for (int i = 0; i < 6; ++i)
        ctx.master_gate_rvas[i] = kb->master_gate_rvas[i];
    ctx.render_mode_rva       = kb->render_mode_rva;
    ctx.render_state_rva      = kb->render_state_rva;
    ctx.swapchain_ptr_rva     = kb->swapchain_ptr_rva;
    ctx.render_duration_rva   = kb->render_duration_rva;

    std::printf("[+] rtss_inject: matched known build: %s\n", kb->label);
    std::printf("    d3d12_obj_rva  = 0x%X  (A50F0 — command-list array base, slot 0)\n",
                kb->d3d12_obj_rva);
    std::printf("    parking_zone   = [0x%X .. 0x%X)  (%.2f MB)\n",
                kb->parking_min_rva, kb->parking_max_rva,
                (kb->parking_max_rva - kb->parking_min_rva) / (1024.0 * 1024.0));
    return true;
}

// ---------------------------------------------------------------------------
// snapshot_d3d12_state — fat diagnostic read of all probe RVAs.
// Purpose: every call reads ~20 separate state points in a single function,
// so one failed redirect run gives us the full picture without re-testing.
// ---------------------------------------------------------------------------
bool snapshot_d3d12_state(uint32_t game_pid, const Ctx& ctx, const char* label) {
    if (!ctx.rtss_base_in_game || !ctx.d3d12_obj_rva) {
        std::printf("[!] snapshot: ctx not populated (run verify_in_game_rtss first)\n");
        return false;
    }
    const uint64_t base = ctx.rtss_base_in_game;

    // Wrapper — keep going on per-read failures so we print as much as possible.
    struct Rd {
        uint32_t pid;
        bool operator()(uint64_t va, void* dst, uint32_t size) const {
            return cmdchannel::read_memory(pid, va,
                                           reinterpret_cast<uint64_t>(dst), size);
        }
    };
    Rd rd{game_pid};

    // --- Target + prereq quad (slot 0) ------------------------------------
    uint64_t slot0_cmdlist = 0, slot0_fence_a5050 = 0, slot0_fence_a50b0 = 0;
    uint64_t slot0_a4fd0 = 0,   slot0_a5290     = 0, swapchain_ptr   = 0;
    uint32_t frame_counter_slot0 = 0, render_mode = 0, render_state = 0;
    uint32_t render_duration = 0;
    uint32_t master_gates[6] = {0};

    rd(base + ctx.d3d12_obj_rva,          &slot0_cmdlist,      8);
    rd(base + ctx.slot_fence_a5050_rva,   &slot0_fence_a5050,  8);
    rd(base + ctx.slot_fence_a50b0_rva,   &slot0_fence_a50b0,  8);
    rd(base + ctx.slot_prereq_a4fd0_rva,  &slot0_a4fd0,        8);
    rd(base + ctx.slot_prereq_a5290_rva,  &slot0_a5290,        8);
    rd(base + ctx.slot_frame_counter_rva, &frame_counter_slot0, 4);
    rd(base + ctx.swapchain_ptr_rva,      &swapchain_ptr,      8);
    rd(base + ctx.render_mode_rva,        &render_mode,        4);
    rd(base + ctx.render_state_rva,       &render_state,       4);
    rd(base + ctx.render_duration_rva,    &render_duration,    4);
    for (int i = 0; i < 6; ++i) {
        rd(base + ctx.master_gate_rvas[i], &master_gates[i], 4);
    }

    // --- Report ----------------------------------------------------------
    std::printf("\n=== D3D12 state snapshot [%s] (rtss_base=%016llX) ===\n",
                label, (unsigned long long)base);

    std::printf("  slot-0 command list (A50F0[0])   = %016llX  %s\n",
                (unsigned long long)slot0_cmdlist,
                slot0_cmdlist ? "(populated)" : "(NULL — RTSS has no DX12 swapchain yet)");
    std::printf("  slot-0 fence A5050[0]            = %016llX  %s\n",
                (unsigned long long)slot0_fence_a5050,
                slot0_fence_a5050 ? "(populated)" : "(NULL)");
    std::printf("  slot-0 fence A50B0[0]            = %016llX  %s  [DO NOT HIJACK]\n",
                (unsigned long long)slot0_fence_a50b0,
                slot0_fence_a50b0 ? "(populated)" : "(NULL — head gate FAILS)");
    std::printf("  slot-0 heap A4FD0[0]             = %016llX  %s\n",
                (unsigned long long)slot0_a4fd0,
                slot0_a4fd0 ? "(populated)" : "(NULL — head gate FAILS)");
    std::printf("  slot-0 topology A5290[0]         = %016llX  %s\n",
                (unsigned long long)slot0_a5290,
                slot0_a5290 ? "(populated)" : "(NULL — head gate FAILS)");
    std::printf("  slot-0 frame counter A5090[0]    = %u        %s\n",
                frame_counter_slot0,
                frame_counter_slot0 ? "(render active)" :
                                      "(no frames rendered yet — redirect will silent-miss)");
    std::printf("  swapchain backing (18355A9A8)    = %016llX\n",
                (unsigned long long)swapchain_ptr);
    std::printf("  render duration µs (18355A968)   = %u\n", render_duration);
    std::printf("  render_mode  (183561B8C)         = %u  (0=immediate 1=line 2=tri-strip)\n",
                render_mode);
    std::printf("  render_state (183561BA8)         = 0x%08X  %s\n", render_state,
                render_state ? "(extra +0xD0 calls active)" : "(three +0xD0 calls disabled)");

    std::printf("  master-gate (6-way OR, body-gate of sub_18004B090):\n");
    bool any_gate_open = false;
    for (int i = 0; i < 6; ++i) {
        any_gate_open |= (master_gates[i] != 0);
        std::printf("    gate[%d] @ RVA 0x%X = 0x%08X  %s\n",
                    i, ctx.master_gate_rvas[i], master_gates[i],
                    master_gates[i] ? "(OPEN)" : "(closed)");
    }
    std::printf("  master-gate aggregate            = %s\n",
                any_gate_open ? "RENDER BLOCK ENTERED"
                              : "BLOCKED — render block skipped, A50F0 never touched");

    // Head-gate precondition summary.
    const bool head_gate_ok = (slot0_cmdlist != 0) && (slot0_fence_a50b0 != 0) &&
                              (slot0_a4fd0 != 0)   && (slot0_a5290 != 0);
    std::printf("  head-gate summary                = %s\n",
                head_gate_ok ? "all 4 prereqs OK — A50F0 reachable via sub_180048300"
                             : "prereq NULL — A50F0 not reachable from _UnwindNestedFrames");

    // --- Dispatch-level state (IDA 2026-04-19: Present detour sub_18007B970
    // + OSD render entry sub_18004B8C0). These decide whether the DX12
    // branch is taken at all — downstream of all 6 master gates. ---
    uint32_t backend_sel      = 0;  // dword_18355F8B8  — 6=D3D9/10, 7=D3D11, 8/9=D3D12, 10=interop
    uint32_t hook_installed   = 0;  // dword_18357D004  — Present detour early-exits if 0
    uint32_t render_bits      = 0;  // dword_18357D000  — bit0 main OSD, bit2 extra render
    uint32_t per_proc_enable  = 0;  // dword_183560FE0  — per-process render switch
    uint32_t dx12_enable      = 0;  // dword_183561BA4  — DX12-specific render enable
    uint32_t osd_early_guard  = 0;  // dword_183561BA0  — sub_18004B8C0 early-exit guard
    uint32_t osd_interop_sel  = 0;  // dword_183561B88  — selects interop vs direct-device branch
    uint64_t captured_queue   = 0;  // qword_1801A4E10  — set by OnExecuteD3D12CommandLists
    uint64_t active_swapchain = 0;  // qword_1801A62C0  — swapchain guard (multi-swapchain reject)

    rd(base + 0x355F8B8, &backend_sel,      4);
    rd(base + 0x357D004, &hook_installed,   4);
    rd(base + 0x357D000, &render_bits,      4);
    rd(base + 0x3560FE0, &per_proc_enable,  4);
    rd(base + 0x3561BA4, &dx12_enable,      4);
    rd(base + 0x3561BA0, &osd_early_guard,  4);
    rd(base + 0x3561B88, &osd_interop_sel,  4);
    rd(base + 0x1A4E10,  &captured_queue,   8);
    rd(base + 0x1A62C0,  &active_swapchain, 8);

    std::printf("  --- dispatch-level (Present→OSD entry) ---\n");
    const char* backend_str = "UNKNOWN";
    switch (backend_sel) {
        case 6:  backend_str = "D3D9/10"; break;
        case 7:  backend_str = "D3D11";    break;
        case 8:  backend_str = "D3D12";    break;
        case 9:  backend_str = "D3D12+";   break;
        case 10: backend_str = "INTEROP";  break;
        case 1:  backend_str = "OpenGL?";  break;
        case 0:  backend_str = "(unset)";  break;
    }
    std::printf("  backend_sel (18355F8B8)          = %u     (%s)  %s\n",
                backend_sel, backend_str,
                (backend_sel == 8 || backend_sel == 9)
                    ? "DX12 path ENGAGED"
                    : "DX12 path NOT ENGAGED — A50F0 will stay NULL");
    std::printf("  hook_installed (18357D004)       = 0x%08X  %s\n", hook_installed,
                hook_installed ? "(Present detour active)" :
                                 "(Present detour NO-OP — init incomplete)");
    std::printf("  render_bits (18357D000)          = 0x%08X  bit0=%s bit2=%s\n",
                render_bits,
                (render_bits & 1) ? "ON" : "OFF",
                (render_bits & 4) ? "ON" : "OFF");
    std::printf("  per_proc_enable (183560FE0)      = 0x%08X  %s\n", per_proc_enable,
                per_proc_enable ? "(enabled)" : "(disabled — skip main OSD)");
    std::printf("  dx12_enable (183561BA4)          = 0x%08X  %s\n", dx12_enable,
                dx12_enable ? "(DX12 render enabled)" :
                              "(DX12 render DISABLED in Present detour)");
    std::printf("  osd_early_guard (183561BA0)      = 0x%08X\n", osd_early_guard);
    std::printf("  osd_interop_sel (183561B88)      = 0x%08X  %s\n", osd_interop_sel,
                osd_interop_sel ? "(INTEROP multi-device path)" :
                                  "(DIRECT device-from-swapchain path)");
    std::printf("  captured_queue (1801A4E10)       = %016llX  %s\n",
                (unsigned long long)captured_queue,
                captured_queue ? "(ExecuteCommandLists hook FIRED — queue captured)" :
                                 "(NO CAPTURE — ExecuteCommandLists hook not firing)");
    std::printf("  active_swapchain (1801A62C0)     = %016llX\n",
                (unsigned long long)active_swapchain);

    std::printf("=== end snapshot ===\n\n");
    return true;
}

// ---------------------------------------------------------------------------
// sweep_d3d12_slots — find which slot (if any) RTSS populated.
//
// Per-slot arrays are strided by 0x40 (A50F0[N] = 0x1A50F0 + N*8 for the
// 8-byte ptr array, NOT 0x40 — arrays are dense qwords). Verified via IDA:
// code does `qword_1801A50F0[rcx]` with rcx as slot index.
// ---------------------------------------------------------------------------
bool sweep_d3d12_slots(uint32_t game_pid, const Ctx& ctx, int* out_live_slot) {
    if (out_live_slot) *out_live_slot = -1;
    if (!ctx.rtss_base_in_game || !ctx.d3d12_obj_rva) {
        std::printf("[!] sweep: ctx not populated\n");
        return false;
    }
    const uint64_t base = ctx.rtss_base_in_game;
    constexpr int MAX_SLOTS = 16;

    std::printf("\n=== slot sweep A50F0/A50B0/A4FD0/A5290/A5090 [0..%d] ===\n",
                MAX_SLOTS - 1);
    std::printf("  slot | A50F0 (cmdlist)   | A50B0 (fence)     | A4FD0 (heap)      | A5290 (topo)      | fcnt\n");
    std::printf("  -----+-------------------+-------------------+-------------------+-------------------+------\n");

    int live = -1;
    for (int n = 0; n < MAX_SLOTS; ++n) {
        uint64_t cmdlist = 0, a50b0 = 0, a4fd0 = 0, a5290 = 0;
        uint32_t fcnt = 0;
        cmdchannel::read_memory(game_pid, base + ctx.d3d12_obj_rva         + n * 8,
                                reinterpret_cast<uint64_t>(&cmdlist), 8);
        cmdchannel::read_memory(game_pid, base + ctx.slot_fence_a50b0_rva  + n * 8,
                                reinterpret_cast<uint64_t>(&a50b0),   8);
        cmdchannel::read_memory(game_pid, base + ctx.slot_prereq_a4fd0_rva + n * 8,
                                reinterpret_cast<uint64_t>(&a4fd0),   8);
        cmdchannel::read_memory(game_pid, base + ctx.slot_prereq_a5290_rva + n * 8,
                                reinterpret_cast<uint64_t>(&a5290),   8);
        cmdchannel::read_memory(game_pid, base + ctx.slot_frame_counter_rva + n * 4,
                                reinterpret_cast<uint64_t>(&fcnt),    4);

        const bool any_populated = (cmdlist | a50b0 | a4fd0 | a5290 | fcnt) != 0;
        if (any_populated) {
            std::printf("  [%2d] | %016llX  | %016llX  | %016llX  | %016llX  | %u\n",
                        n, (unsigned long long)cmdlist, (unsigned long long)a50b0,
                        (unsigned long long)a4fd0,     (unsigned long long)a5290,
                        fcnt);
        }
        if (live < 0 && fcnt > 0 && cmdlist != 0) live = n;
    }

    // D3D9-11 fallback check: slot_7 descriptor at 0x1A5F68. If populated,
    // RTSS is using the old method-chain path, not the DX12 command-list
    // path — possible for D3D11On12 games or games where RTSS picks the
    // 11-compat overlay route.
    uint64_t slot7_desc = 0;
    cmdchannel::read_memory(game_pid, base + 0x1A5F68,
                            reinterpret_cast<uint64_t>(&slot7_desc), 8);
    std::printf("\n  D3D9-11 slot_7 @ 0x1A5F68  = %016llX  %s\n",
                (unsigned long long)slot7_desc,
                slot7_desc ? "(POPULATED — RTSS using D3D9-11 path, not DX12)"
                           : "(empty)");

    // Interop-mode cluster sweep (IDA 2026-04-19, sub_18004B8C0 branch on
    // dword_183561B88 != 0). Parallel arrays of 8-byte per-slot ptrs:
    //   qword_18018EA50[N] — per-swapchain resource (arg to interop render)
    //   qword_18018EA90[N] — interop render target, vtable +0x20 / +0x28 <<<
    //   qword_18018EAD0[N] — secondary (vtable +0x378)
    //   qword_18018EB10[N] — helper state
    // If ANY row is populated here, it's the live interop slot and is the
    // correct redirect target for this game.
    std::printf("\n  === interop cluster (qword_18018EA50..B10) ===\n");
    std::printf("  slot | EA50 (perswap)    | EA90 (render TGT) | EAD0 (vtbl+378)   | EB10 (helper)\n");
    std::printf("  -----+-------------------+-------------------+-------------------+-------------------\n");
    int live_interop = -1;
    for (int n = 0; n < MAX_SLOTS; ++n) {
        uint64_t ea50 = 0, ea90 = 0, ead0 = 0, eb10 = 0;
        cmdchannel::read_memory(game_pid, base + 0x18EA50 + n * 8,
                                reinterpret_cast<uint64_t>(&ea50), 8);
        cmdchannel::read_memory(game_pid, base + 0x18EA90 + n * 8,
                                reinterpret_cast<uint64_t>(&ea90), 8);
        cmdchannel::read_memory(game_pid, base + 0x18EAD0 + n * 8,
                                reinterpret_cast<uint64_t>(&ead0), 8);
        cmdchannel::read_memory(game_pid, base + 0x18EB10 + n * 8,
                                reinterpret_cast<uint64_t>(&eb10), 8);

        const bool any_populated = (ea50 | ea90 | ead0 | eb10) != 0;
        if (any_populated) {
            std::printf("  [%2d] | %016llX  | %016llX  | %016llX  | %016llX\n",
                        n, (unsigned long long)ea50, (unsigned long long)ea90,
                        (unsigned long long)ead0,    (unsigned long long)eb10);
        }
        if (live_interop < 0 && ea90 != 0) live_interop = n;
    }
    if (live_interop >= 0) {
        std::printf("\n[+] sweep: INTEROP slot %d is LIVE (EA90 render target populated)\n",
                    live_interop);
        std::printf("    redirect target VA = %016llX (RVA 0x%X)\n",
                    (unsigned long long)(base + 0x18EA90 + live_interop * 8),
                    0x18EA90 + live_interop * 8);
    } else {
        std::printf("\n[!] sweep: no INTEROP slot has EA90 populated\n");
    }

    if (live >= 0) {
        std::printf("\n[+] sweep: slot %d is LIVE (fcnt>0, cmdlist non-NULL)\n", live);
    } else {
        std::printf("\n[!] sweep: no DX12 slot has a live command list + fcnt > 0\n");
    }
    std::printf("=== end sweep ===\n\n");

    if (out_live_slot) *out_live_slot = live;
    return live >= 0;
}

bool probe_parking_zone(uint32_t game_pid, const Ctx& ctx) {
    if (!ctx.rtss_base_in_game) {
        std::printf("[!] probe_parking_zone: ctx.rtss_base_in_game is zero\n");
        return false;
    }
    if (ctx.parking_min_rva == 0 || ctx.parking_max_rva <= ctx.parking_min_rva) {
        std::printf("[!] probe_parking_zone: unknown build — no parking zone defined\n");
        return false;
    }

    constexpr uint32_t PAGE = 0x1000;
    constexpr uint32_t MAX_RETRIES = 4;

    const uint32_t zone_size   = ctx.parking_max_rva - ctx.parking_min_rva;
    const uint32_t addressable = (zone_size - PAGE) & ~(PAGE - 1);  // last valid 4 KB-aligned slot

    uint64_t rng = seed_rng();
    uint8_t  read_buf[PAGE];
    uint32_t parking_rva = 0;
    uint64_t parking_va  = 0;

    // (1) Pick random offset + zero-probe. Retry if nonzero.
    for (uint32_t attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        const uint32_t offset = static_cast<uint32_t>(xorshift_next(rng) % addressable) & ~(PAGE - 1);
        parking_rva = ctx.parking_min_rva + offset;
        parking_va  = ctx.rtss_base_in_game + parking_rva;

        std::printf("[*] probe_parking_zone: attempt %u — candidate parking RVA 0x%X (VA %016llX)\n",
                    attempt + 1, parking_rva, (unsigned long long)parking_va);

        std::memset(read_buf, 0xFF, sizeof(read_buf));
        if (!cmdchannel::read_memory(game_pid, parking_va,
                                     reinterpret_cast<uint64_t>(read_buf), sizeof(read_buf))) {
            std::printf("[!] probe_parking_zone: cmd 0 READ at candidate failed\n");
            return false;
        }

        bool all_zero = true;
        for (uint32_t i = 0; i < sizeof(read_buf); ++i) {
            if (read_buf[i] != 0) {
                all_zero = false;
                std::printf("[!] probe_parking_zone: candidate NOT zero (byte %u = 0x%02X) — retrying\n",
                            i, read_buf[i]);
                break;
            }
        }
        if (all_zero) break;
        if (attempt + 1 == MAX_RETRIES) {
            std::printf("[!] probe_parking_zone: exhausted %u attempts — all candidates non-zero\n",
                        MAX_RETRIES);
            std::printf("[!]   IDA static analysis may have missed an indirect ref into this zone.\n");
            std::printf("[!]   Revisit injection-info.txt §2.9.g — parking zone may need narrowing.\n");
            return false;
        }
        parking_va = 0;
    }
    std::printf("[+] probe_parking_zone: zone at RVA 0x%X is clean (4 KB all-zero)\n", parking_rva);

    // (2) Write sentinel, read back, verify.
    const uint32_t sentinel = 0xDEADBEEFu;
    if (!cmdchannel::write_memory(game_pid, parking_va,
                                  reinterpret_cast<uint64_t>(&sentinel), sizeof(sentinel))) {
        std::printf("[!] probe_parking_zone: cmd 1 WRITE sentinel failed\n");
        return false;
    }
    uint32_t readback = 0;
    if (!cmdchannel::read_memory(game_pid, parking_va,
                                 reinterpret_cast<uint64_t>(&readback), sizeof(readback))) {
        std::printf("[!] probe_parking_zone: cmd 0 READ sentinel failed\n");
        return false;
    }
    if (readback != sentinel) {
        std::printf("[!] probe_parking_zone: readback mismatch — wrote 0x%08X, read 0x%08X\n",
                    sentinel, readback);
        return false;
    }
    std::printf("[+] probe_parking_zone: sentinel 0x%08X written + verified at RVA 0x%X\n",
                sentinel, parking_rva);

    // (3) Zero the sentinel, verify zero again (leave no residue for AC scanners).
    const uint32_t zero = 0;
    if (!cmdchannel::write_memory(game_pid, parking_va,
                                  reinterpret_cast<uint64_t>(&zero), sizeof(zero))) {
        std::printf("[!] probe_parking_zone: cmd 1 WRITE zero (cleanup) failed\n");
        return false;
    }
    readback = 0xFFFFFFFFu;
    if (!cmdchannel::read_memory(game_pid, parking_va,
                                 reinterpret_cast<uint64_t>(&readback), sizeof(readback))) {
        std::printf("[!] probe_parking_zone: cmd 0 READ post-cleanup failed\n");
        return false;
    }
    if (readback != 0) {
        std::printf("[!] probe_parking_zone: post-cleanup non-zero (0x%08X)\n", readback);
        return false;
    }
    std::printf("[+] probe_parking_zone: parking RVA 0x%X scrubbed clean — full R/W primitive validated\n",
                parking_rva);
    return true;
}

// ---------------------------------------------------------------------------
// Phase 16.b.gamma — DX12 A50F0[0] redirect smoke.
//
// Target is an ID3D12GraphicsCommandList ptr in the per-slot A50F0 array.
// RTSS calls vtable methods on A50F0[0] at 15 distinct offsets per frame:
//   {0x48, 0x50, 0x60, 0x78, 0x80, 0xA0, 0xA8, 0xB0,
//    0xC8, 0xD0, 0xE0, 0xF0, 0x160, 0x170, 0x180}
// Stub must land cleanly on any of them and return 0 — RTSS treats return
// as a handle / status and OR's several together; non-zero can divert flow
// into error paths that freeze the overlay.
//
// Parking layout (total 0x400 = 1 KB):
//   +0x000  vtable (49 qwords = 0x188 B) — needed slots point at stub body
//   +0x200  stub body (23 B)
//   +0x280  sentinel dword (4 B)
//
// Stub body (23 bytes):
//   +0x00  mov rax, imm64           ; sentinel VA (patched at +0x02)
//   +0x0A  mov dword [rax], 0xDEADBEEF
//   +0x10  xor eax, eax             ; return 0
//   +0x12  ret
//   (total 0x13 = 19 bytes, rounded to 0x40 reserved)
// ---------------------------------------------------------------------------
static constexpr uint32_t STUB_SIZE = 0x13;
static constexpr uint32_t PATCH_SENTINEL_VA = 0x02;  // imm64 byte offset in stub

static const uint8_t STUB_TEMPLATE[STUB_SIZE] = {
    0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,   // mov rax, <sentinel_VA> (patched)
    0xC7, 0x00, 0xEF, 0xBE, 0xAD, 0xDE,   // mov dword [rax], 0xDEADBEEF
    0x33, 0xC0,                           // xor eax, eax
    0xC3,                                 // ret
};

// Parking layout.
static constexpr uint32_t PARKING_VTABLE_OFF   = 0x000;  // 49 qwords
static constexpr uint32_t PARKING_VTABLE_SIZE  = 0x188;  // covers slot[48] (offset 0x180)
static constexpr uint32_t PARKING_STUB_OFF     = 0x200;
static constexpr uint32_t PARKING_SENTINEL_OFF = 0x280;
static constexpr uint32_t PARKING_TOTAL_SIZE   = 0x400;

// The 15 distinct vtable offsets the stub must catch.
static const uint32_t VTABLE_HOT_OFFSETS[] = {
    0x48, 0x50, 0x60, 0x78, 0x80, 0xA0, 0xA8, 0xB0,
    0xC8, 0xD0, 0xE0, 0xF0, 0x160, 0x170, 0x180
};
static constexpr uint32_t VTABLE_HOT_COUNT =
    sizeof(VTABLE_HOT_OFFSETS) / sizeof(VTABLE_HOT_OFFSETS[0]);

bool redirect_d3d12_smoke(uint32_t game_pid, const Ctx& ctx) {
    // --- Precondition validation ---
    if (!ctx.rtss_base_in_game || !ctx.d3d12_obj_rva || !ctx.parking_min_rva ||
        !ctx.slot_frame_counter_rva || ctx.master_gate_rvas[0] == 0) {
        std::printf("[!] redirect_d3d12_smoke: ctx missing required fields "
                    "(d3d12_obj/parking/frame_counter/master_gate[0])\n");
        return false;
    }

    const uint64_t rtss_base = ctx.rtss_base_in_game;
    const uint64_t a50f0_va  = rtss_base + ctx.d3d12_obj_rva;           // target slot
    const uint64_t a50b0_va  = rtss_base + ctx.slot_fence_a50b0_rva;    // prereq only
    const uint64_t a4fd0_va  = rtss_base + ctx.slot_prereq_a4fd0_rva;   // prereq only
    const uint64_t a5290_va  = rtss_base + ctx.slot_prereq_a5290_rva;   // prereq only
    const uint64_t fcnt_va   = rtss_base + ctx.slot_frame_counter_rva;  // A5090[0]

    // --- (1) Pre-redirect snapshot ---
    snapshot_d3d12_state(game_pid, ctx, "pre-redirect");

    // --- (2) Readiness gate: prereq quad non-NULL + frame counter > 0.
    // If any prereq is NULL the head-gate in sub_180048300 skips A50F0
    // entirely → stub never fires even with a perfect fake vtable.
    uint64_t pre_cmdlist = 0, pre_a50b0 = 0, pre_a4fd0 = 0, pre_a5290 = 0;
    uint32_t pre_fcnt = 0;
    if (!cmdchannel::read_memory(game_pid, a50f0_va,
                                 reinterpret_cast<uint64_t>(&pre_cmdlist), 8) ||
        !cmdchannel::read_memory(game_pid, a50b0_va,
                                 reinterpret_cast<uint64_t>(&pre_a50b0), 8) ||
        !cmdchannel::read_memory(game_pid, a4fd0_va,
                                 reinterpret_cast<uint64_t>(&pre_a4fd0), 8) ||
        !cmdchannel::read_memory(game_pid, a5290_va,
                                 reinterpret_cast<uint64_t>(&pre_a5290), 8) ||
        !cmdchannel::read_memory(game_pid, fcnt_va,
                                 reinterpret_cast<uint64_t>(&pre_fcnt), 4)) {
        std::printf("[!] redirect_d3d12_smoke: readiness READ failed\n");
        return false;
    }
    if (!pre_cmdlist || !pre_a50b0 || !pre_a4fd0 || !pre_a5290 || !pre_fcnt) {
        std::printf("[!] redirect_d3d12_smoke: NOT READY — RTSS has not yet "
                    "rendered a DX12 frame to slot 0\n");
        std::printf("[!]   cmdlist=%016llX a50b0=%016llX a4fd0=%016llX "
                    "a5290=%016llX fcnt=%u\n",
                    (unsigned long long)pre_cmdlist, (unsigned long long)pre_a50b0,
                    (unsigned long long)pre_a4fd0,   (unsigned long long)pre_a5290,
                    pre_fcnt);
        std::printf("[!]   bring overlay up in the target (move mouse, resize, etc)\n");
        return false;
    }

    // --- (3) Pick parking region (zero-probed) ---
    constexpr uint32_t PAGE = 0x1000;
    const uint32_t zone_size   = ctx.parking_max_rva - ctx.parking_min_rva;
    const uint32_t addressable = (zone_size - PARKING_TOTAL_SIZE) & ~(PAGE - 1);

    uint64_t rng = seed_rng();
    uint64_t parking_va  = 0;
    uint32_t parking_rva = 0;
    for (uint32_t attempt = 0; attempt < 4; ++attempt) {
        const uint32_t offset = static_cast<uint32_t>(xorshift_next(rng) % addressable) & ~(PAGE - 1);
        parking_rva = ctx.parking_min_rva + offset;
        parking_va  = rtss_base + parking_rva;

        uint8_t check_buf[PARKING_TOTAL_SIZE] = {};
        if (!cmdchannel::read_memory(game_pid, parking_va,
                                     reinterpret_cast<uint64_t>(check_buf),
                                     PARKING_TOTAL_SIZE)) {
            std::printf("[!] redirect_d3d12_smoke: zero-probe READ failed\n");
            return false;
        }
        bool all_zero = true;
        for (uint32_t i = 0; i < PARKING_TOTAL_SIZE; ++i) {
            if (check_buf[i] != 0) { all_zero = false; break; }
        }
        if (all_zero) break;
        parking_va = 0;
        if (attempt == 3) {
            std::printf("[!] redirect_d3d12_smoke: no clean parking region found in 4 attempts\n");
            return false;
        }
    }
    const uint64_t stub_va     = parking_va + PARKING_STUB_OFF;
    const uint64_t sentinel_va = parking_va + PARKING_SENTINEL_OFF;
    std::printf("[*] redirect_d3d12_smoke: parking_va=%016llX stub_va=%016llX "
                "sentinel_va=%016llX (rva=0x%X)\n",
                (unsigned long long)parking_va, (unsigned long long)stub_va,
                (unsigned long long)sentinel_va, parking_rva);

    // --- (4) Save originals: A50F0[0] + 6 master gates ---
    uint64_t saved_cmdlist = pre_cmdlist;
    uint32_t saved_gates[6] = {0};
    for (int i = 0; i < 6; ++i) {
        const uint64_t gate_va = rtss_base + ctx.master_gate_rvas[i];
        if (!cmdchannel::read_memory(game_pid, gate_va,
                                     reinterpret_cast<uint64_t>(&saved_gates[i]), 4)) {
            std::printf("[!] redirect_d3d12_smoke: master-gate READ[%d] failed\n", i);
            return false;
        }
    }
    std::printf("[*] redirect_d3d12_smoke: saved A50F0[0]=%016llX  gates=[%u %u %u %u %u %u]\n",
                (unsigned long long)saved_cmdlist,
                saved_gates[0], saved_gates[1], saved_gates[2],
                saved_gates[3], saved_gates[4], saved_gates[5]);

    // --- (5) Build parking buffer: 49-qword fake vtable + stub + sentinel ---
    uint8_t parking_buf[PARKING_TOTAL_SIZE] = {};

    // Fill vtable first-0x188 with self-ref (harmless if RTSS picks an
    // unexpected offset — crash inside our parking, not on the real game
    // data page).
    uint64_t* vtable = reinterpret_cast<uint64_t*>(parking_buf + PARKING_VTABLE_OFF);
    const uint32_t VTABLE_QWORDS = PARKING_VTABLE_SIZE / 8;  // 49
    for (uint32_t i = 0; i < VTABLE_QWORDS; ++i) vtable[i] = parking_va;

    // Point each of the 15 hot offsets at stub_va. Single shared stub —
    // returns 0 for every caller regardless of the method the vtable
    // entry claimed to be.
    for (uint32_t i = 0; i < VTABLE_HOT_COUNT; ++i) {
        const uint32_t off = VTABLE_HOT_OFFSETS[i];
        vtable[off / 8] = stub_va;
    }

    // Copy stub template, patch sentinel VA into the mov rax, imm64.
    uint8_t* stub = parking_buf + PARKING_STUB_OFF;
    std::memcpy(stub, STUB_TEMPLATE, STUB_SIZE);
    std::memcpy(stub + PATCH_SENTINEL_VA, &sentinel_va, sizeof(sentinel_va));

    // --- (6) Write parking buffer (single cross-proc WRITE) ---
    if (!cmdchannel::write_memory(game_pid, parking_va,
                                  reinterpret_cast<uint64_t>(parking_buf),
                                  PARKING_TOTAL_SIZE)) {
        std::printf("[!] redirect_d3d12_smoke: parking region WRITE failed\n");
        return false;
    }
    std::printf("[+] redirect_d3d12_smoke: parking staged (49-qword vtable, 15 hot offsets -> stub)\n");

    // --- (7) Flip parking page to RWX. RTSS .data is NX; CPU will fault
    // on stub_va execution otherwise. Save old protect for scrub.
    constexpr uint32_t PAGE_EXECUTE_READWRITE_VAL = 0x40;
    uint32_t old_protect = 0;
    if (!cmdchannel::protect_memory(game_pid, parking_va, PARKING_TOTAL_SIZE,
                                    PAGE_EXECUTE_READWRITE_VAL, &old_protect)) {
        std::printf("[!] redirect_d3d12_smoke: cmd 5 PROTECT (-> RWX) failed\n");
        return false;
    }
    std::printf("[+] redirect_d3d12_smoke: parking page -> PAGE_EXECUTE_READWRITE (old=0x%X)\n",
                old_protect);

    // --- (8) Flip master gate [0] = 1 to force render-block entry in
    // sub_18004B090. If all 6 gates were zero, RTSS skips the entire
    // render path before touching A50F0.
    const uint32_t one = 1;
    const uint64_t gate0_va = rtss_base + ctx.master_gate_rvas[0];
    if (saved_gates[0] == 0) {
        if (!cmdchannel::write_memory(game_pid, gate0_va,
                                      reinterpret_cast<uint64_t>(&one), 4)) {
            std::printf("[!] redirect_d3d12_smoke: master_gate[0] WRITE failed\n");
            return false;
        }
        std::printf("[+] redirect_d3d12_smoke: master_gate[0] opened (0 -> 1)\n");
    } else {
        std::printf("[*] redirect_d3d12_smoke: master_gate[0] already open (0x%X) — no flip\n",
                    saved_gates[0]);
    }

    // --- (9) Overwrite A50F0[0] = parking_va. THIS is the redirect —
    // next render tick, sub_180048300 derefs our parking instead of the
    // real command list; fake vtable routes method calls to stub.
    if (!cmdchannel::write_memory(game_pid, a50f0_va,
                                  reinterpret_cast<uint64_t>(&parking_va), 8)) {
        std::printf("[!] redirect_d3d12_smoke: A50F0[0] redirect WRITE failed\n");
        // still try to restore master gate
        cmdchannel::write_memory(game_pid, gate0_va,
                                 reinterpret_cast<uint64_t>(&saved_gates[0]), 4);
        uint32_t discard = 0;
        (void)cmdchannel::protect_memory(game_pid, parking_va, PARKING_TOTAL_SIZE,
                                         old_protect, &discard);
        return false;
    }
    std::printf("[+] redirect_d3d12_smoke: A50F0[0] redirected -> parking_va (waiting for stub fire)\n");

    // --- (10) Poll sentinel (up to 5 s, 200 ms cadence) ---
    constexpr uint32_t POLL_INTERVAL_MS = 200;
    constexpr uint32_t POLL_TIMEOUT_MS  = 5000;
    constexpr uint32_t MAX_POLLS        = POLL_TIMEOUT_MS / POLL_INTERVAL_MS;

    bool fired = false;
    for (uint32_t i = 0; i < MAX_POLLS; ++i) {
        Sleep(POLL_INTERVAL_MS);
        uint32_t sent = 0;
        if (!cmdchannel::read_memory(game_pid, sentinel_va,
                                     reinterpret_cast<uint64_t>(&sent), 4)) {
            std::printf("[!] redirect_d3d12_smoke: sentinel READ failed during poll\n");
            break;
        }
        if (sent == 0xDEADBEEFu) {
            std::printf("[+] redirect_d3d12_smoke: STUB FIRED (sentinel 0xDEADBEEF seen after %u ms)\n",
                        (i + 1) * POLL_INTERVAL_MS);
            fired = true;
            break;
        }
    }
    if (!fired) {
        std::printf("[!] redirect_d3d12_smoke: timeout — stub did NOT fire within %u ms\n",
                    POLL_TIMEOUT_MS);
    }

    // --- (11) Post-redirect snapshot (diagnostic, before restore) ---
    snapshot_d3d12_state(game_pid, ctx, fired ? "post-redirect (success)"
                                              : "post-redirect (timeout)");

    // --- (12) Restore: A50F0[0] first (so no in-flight tick lands on
    // parking after gates are restored), then master gate, then scrub,
    // then restore original page protection.
    cmdchannel::write_memory(game_pid, a50f0_va,
                             reinterpret_cast<uint64_t>(&saved_cmdlist), 8);
    if (saved_gates[0] == 0) {
        cmdchannel::write_memory(game_pid, gate0_va,
                                 reinterpret_cast<uint64_t>(&saved_gates[0]), 4);
    }

    uint8_t zero_buf[PARKING_TOTAL_SIZE] = {};
    if (!cmdchannel::write_memory(game_pid, parking_va,
                                  reinterpret_cast<uint64_t>(zero_buf),
                                  PARKING_TOTAL_SIZE)) {
        std::printf("[!] redirect_d3d12_smoke: parking scrub WRITE failed (residue left in target)\n");
    } else {
        std::printf("[+] redirect_d3d12_smoke: parking region scrubbed\n");
    }
    uint32_t discard = 0;
    (void)cmdchannel::protect_memory(game_pid, parking_va, PARKING_TOTAL_SIZE,
                                     old_protect, &discard);

    return fired;
}

// ---------------------------------------------------------------------------
// Phase 16.b.gamma (interop variant) — redirect qword_18018EA90[N].
// See header for path/rationale. Reuses STUB_TEMPLATE + PARKING_* layout
// from the direct-mode redirect above.
// ---------------------------------------------------------------------------
static constexpr uint32_t EA90_RVA = 0x18EA90;
static constexpr uint32_t INTEROP_MAX_SLOTS = 8;

bool redirect_d3d12_interop_smoke(uint32_t game_pid, const Ctx& ctx) {
    if (!ctx.rtss_base_in_game || !ctx.parking_min_rva) {
        std::printf("[!] redirect_d3d12_interop: ctx missing required fields\n");
        return false;
    }
    const uint64_t rtss_base = ctx.rtss_base_in_game;

    // --- (1) Pre-redirect snapshot ---
    snapshot_d3d12_state(game_pid, ctx, "pre-interop-redirect");

    // --- (2) Find populated slot: EA90[0..7] — first non-NULL wins.
    int live_slot = -1;
    uint64_t saved_ea90 = 0;
    for (int n = 0; n < INTEROP_MAX_SLOTS; ++n) {
        uint64_t val = 0;
        if (!cmdchannel::read_memory(game_pid, rtss_base + EA90_RVA + n * 8,
                                     reinterpret_cast<uint64_t>(&val), 8)) {
            std::printf("[!] redirect_d3d12_interop: EA90[%d] READ failed\n", n);
            return false;
        }
        if (val != 0) { live_slot = n; saved_ea90 = val; break; }
    }
    if (live_slot < 0) {
        std::printf("[!] redirect_d3d12_interop: no EA90[0..%d] slot populated — "
                    "interop target not ready\n", INTEROP_MAX_SLOTS - 1);
        return false;
    }
    const uint64_t ea90_va = rtss_base + EA90_RVA + live_slot * 8;
    std::printf("[+] redirect_d3d12_interop: live interop slot = %d, EA90 VA = %016llX (orig = %016llX)\n",
                live_slot, (unsigned long long)ea90_va, (unsigned long long)saved_ea90);

    // --- (3) Pick parking region (zero-probed) ---
    constexpr uint32_t PAGE = 0x1000;
    const uint32_t zone_size   = ctx.parking_max_rva - ctx.parking_min_rva;
    const uint32_t addressable = (zone_size - PARKING_TOTAL_SIZE) & ~(PAGE - 1);

    uint64_t rng = seed_rng();
    uint64_t parking_va  = 0;
    uint32_t parking_rva = 0;
    for (uint32_t attempt = 0; attempt < 4; ++attempt) {
        const uint32_t offset = static_cast<uint32_t>(xorshift_next(rng) % addressable) & ~(PAGE - 1);
        parking_rva = ctx.parking_min_rva + offset;
        parking_va  = rtss_base + parking_rva;

        uint8_t check_buf[PARKING_TOTAL_SIZE] = {};
        if (!cmdchannel::read_memory(game_pid, parking_va,
                                     reinterpret_cast<uint64_t>(check_buf),
                                     PARKING_TOTAL_SIZE)) {
            std::printf("[!] redirect_d3d12_interop: zero-probe READ failed\n");
            return false;
        }
        bool all_zero = true;
        for (uint32_t i = 0; i < PARKING_TOTAL_SIZE; ++i) {
            if (check_buf[i] != 0) { all_zero = false; break; }
        }
        if (all_zero) break;
        parking_va = 0;
        if (attempt == 3) {
            std::printf("[!] redirect_d3d12_interop: no clean parking region found\n");
            return false;
        }
    }
    const uint64_t stub_va     = parking_va + PARKING_STUB_OFF;
    const uint64_t sentinel_va = parking_va + PARKING_SENTINEL_OFF;
    std::printf("[*] redirect_d3d12_interop: parking_va=%016llX stub_va=%016llX sentinel_va=%016llX\n",
                (unsigned long long)parking_va, (unsigned long long)stub_va,
                (unsigned long long)sentinel_va);

    // --- (4) Build parking buffer ---
    uint8_t parking_buf[PARKING_TOTAL_SIZE] = {};

    uint64_t* vtable = reinterpret_cast<uint64_t*>(parking_buf + PARKING_VTABLE_OFF);
    const uint32_t VTABLE_QWORDS = PARKING_VTABLE_SIZE / 8;
    for (uint32_t i = 0; i < VTABLE_QWORDS; ++i) vtable[i] = parking_va;
    // Only 2 hot offsets in interop mode: +0x20, +0x28
    vtable[0x20 / 8] = stub_va;
    vtable[0x28 / 8] = stub_va;

    uint8_t* stub = parking_buf + PARKING_STUB_OFF;
    std::memcpy(stub, STUB_TEMPLATE, STUB_SIZE);
    std::memcpy(stub + PATCH_SENTINEL_VA, &sentinel_va, sizeof(sentinel_va));

    // --- (5) Write parking buffer ---
    if (!cmdchannel::write_memory(game_pid, parking_va,
                                  reinterpret_cast<uint64_t>(parking_buf),
                                  PARKING_TOTAL_SIZE)) {
        std::printf("[!] redirect_d3d12_interop: parking WRITE failed\n");
        return false;
    }
    std::printf("[+] redirect_d3d12_interop: parking staged (2 hot offsets +0x20, +0x28 -> stub)\n");

    // --- (6) RWX the parking page ---
    constexpr uint32_t PAGE_EXECUTE_READWRITE_VAL = 0x40;
    uint32_t old_protect = 0;
    if (!cmdchannel::protect_memory(game_pid, parking_va, PARKING_TOTAL_SIZE,
                                    PAGE_EXECUTE_READWRITE_VAL, &old_protect)) {
        std::printf("[!] redirect_d3d12_interop: PROTECT (-> RWX) failed\n");
        return false;
    }

    // --- (7) Overwrite EA90[N] = parking_va ---
    if (!cmdchannel::write_memory(game_pid, ea90_va,
                                  reinterpret_cast<uint64_t>(&parking_va), 8)) {
        std::printf("[!] redirect_d3d12_interop: EA90[%d] redirect WRITE failed\n", live_slot);
        uint32_t discard = 0;
        (void)cmdchannel::protect_memory(game_pid, parking_va, PARKING_TOTAL_SIZE,
                                         old_protect, &discard);
        return false;
    }
    std::printf("[+] redirect_d3d12_interop: EA90[%d] redirected -> parking_va (waiting for stub fire)\n",
                live_slot);

    // --- (8) Poll sentinel (up to 5 s, 200 ms cadence) ---
    constexpr uint32_t POLL_INTERVAL_MS = 200;
    constexpr uint32_t POLL_TIMEOUT_MS  = 5000;
    constexpr uint32_t MAX_POLLS        = POLL_TIMEOUT_MS / POLL_INTERVAL_MS;

    bool fired = false;
    for (uint32_t i = 0; i < MAX_POLLS; ++i) {
        Sleep(POLL_INTERVAL_MS);
        uint32_t sent = 0;
        if (!cmdchannel::read_memory(game_pid, sentinel_va,
                                     reinterpret_cast<uint64_t>(&sent), 4)) {
            std::printf("[!] redirect_d3d12_interop: sentinel READ failed during poll\n");
            break;
        }
        if (sent == 0xDEADBEEFu) {
            std::printf("[+] redirect_d3d12_interop: STUB FIRED (sentinel 0xDEADBEEF seen after %u ms)\n",
                        (i + 1) * POLL_INTERVAL_MS);
            fired = true;
            break;
        }
    }
    if (!fired) {
        std::printf("[!] redirect_d3d12_interop: timeout — stub did NOT fire within %u ms\n",
                    POLL_TIMEOUT_MS);
    }

    // --- (9) Post-redirect snapshot ---
    snapshot_d3d12_state(game_pid, ctx, fired ? "post-interop-redirect (success)"
                                              : "post-interop-redirect (timeout)");

    // --- (10) Restore EA90[N], scrub parking, restore protect ---
    cmdchannel::write_memory(game_pid, ea90_va,
                             reinterpret_cast<uint64_t>(&saved_ea90), 8);

    uint8_t zero_buf[PARKING_TOTAL_SIZE] = {};
    if (!cmdchannel::write_memory(game_pid, parking_va,
                                  reinterpret_cast<uint64_t>(zero_buf),
                                  PARKING_TOTAL_SIZE)) {
        std::printf("[!] redirect_d3d12_interop: parking scrub WRITE failed\n");
    } else {
        std::printf("[+] redirect_d3d12_interop: parking region scrubbed\n");
    }
    uint32_t discard = 0;
    (void)cmdchannel::protect_memory(game_pid, parking_va, PARKING_TOTAL_SIZE,
                                     old_protect, &discard);

    return fired;
}

// ---------------------------------------------------------------------------
// Phase 16.b.gamma (DX11 fallback) — F8C8/F8C0 Present body-dispatch smoke.
//
// F8C8 is a direct function pointer call (`call cs:qword_18355F8C8`), not a
// vtable dereference — no fake vtable needed. Two modes:
//
//   STANDARD: F8C8 != 0 — plant_slot = F8C8 VA, saved = F8C8 value.
//   DETOURS:  F8C8 == 0 && F8C0 != 0 — plant_slot = F8C0_val + 0x50,
//             saved = value at that address.
//
// Parking layout (0x100 total — stub + sentinel only):
//   +0x000  stub body (0x13 B)
//   +0x080  sentinel dword (4 B)
// ---------------------------------------------------------------------------
static constexpr uint32_t DX11_PARKING_STUB_OFF     = 0x000;
static constexpr uint32_t DX11_PARKING_SENTINEL_OFF = 0x080;
static constexpr uint32_t DX11_PARKING_TOTAL        = 0x100;

static constexpr uint32_t BACKEND_SEL_RVA = 0x355F8B8;
static constexpr uint32_t F8C8_RVA        = 0x355F8C8;
static constexpr uint32_t F8C0_RVA        = 0x355F8C0;
static constexpr uint32_t DETOURS_HOOK_OFF = 0x50;

bool redirect_d3d11_present_smoke(uint32_t game_pid, const Ctx& ctx) {
    if (!ctx.rtss_base_in_game || !ctx.parking_min_rva) {
        std::printf("[!] redirect_d3d11_present: ctx missing required fields\n");
        return false;
    }
    const uint64_t rtss_base = ctx.rtss_base_in_game;

    // --- (1) Check backend_sel — must be 7 (D3D11) ---
    uint32_t backend_sel = 0;
    if (!cmdchannel::read_memory(game_pid, rtss_base + BACKEND_SEL_RVA,
                                 reinterpret_cast<uint64_t>(&backend_sel), 4)) {
        std::printf("[!] redirect_d3d11_present: backend_sel READ failed\n");
        return false;
    }
    if (backend_sel != 7) {
        std::printf("[!] redirect_d3d11_present: backend_sel = %u (expected 7 = D3D11)\n",
                    backend_sel);
        return false;
    }
    std::printf("[+] redirect_d3d11_present: backend_sel = 7 (D3D11)\n");

    // --- (2) Mode detection: F8C8 / F8C0 ---
    uint64_t f8c8_val = 0;
    uint64_t f8c0_val = 0;
    if (!cmdchannel::read_memory(game_pid, rtss_base + F8C8_RVA,
                                 reinterpret_cast<uint64_t>(&f8c8_val), 8) ||
        !cmdchannel::read_memory(game_pid, rtss_base + F8C0_RVA,
                                 reinterpret_cast<uint64_t>(&f8c0_val), 8)) {
        std::printf("[!] redirect_d3d11_present: F8C8/F8C0 READ failed\n");
        return false;
    }

    uint64_t plant_slot_va = 0;
    uint64_t saved_body    = 0;
    const char* mode_tag   = "?";

    if (f8c8_val != 0) {
        plant_slot_va = rtss_base + F8C8_RVA;
        saved_body    = f8c8_val;
        mode_tag      = "STANDARD";
    } else if (f8c0_val != 0) {
        const uint64_t rtss_lo = rtss_base;
        const uint64_t rtss_hi = rtss_base + 0x4000000;
        if (f8c0_val < rtss_lo || f8c0_val >= rtss_hi) {
            plant_slot_va = f8c0_val + DETOURS_HOOK_OFF;
            mode_tag      = "DETOURS";
            if (!cmdchannel::read_memory(game_pid, plant_slot_va,
                                         reinterpret_cast<uint64_t>(&saved_body), 8)) {
                std::printf("[!] redirect_d3d11_present: stub+0x50 READ failed\n");
                return false;
            }
            if (!saved_body) {
                std::printf("[!] redirect_d3d11_present: stub+0x50 is NULL — "
                            "Detours stub not yet armed\n");
                return false;
            }
        } else {
            std::printf("[!] redirect_d3d11_present: F8C8=0, F8C0=%016llX in RTSS range "
                        "— Present install not finished\n",
                        static_cast<unsigned long long>(f8c0_val));
            return false;
        }
    } else {
        std::printf("[!] redirect_d3d11_present: F8C8 and F8C0 both NULL — "
                    "Present install hasn't run\n");
        return false;
    }
    std::printf("[+] redirect_d3d11_present: mode=%s plant_slot=%016llX saved=%016llX\n",
                mode_tag,
                static_cast<unsigned long long>(plant_slot_va),
                static_cast<unsigned long long>(saved_body));

    // --- (3) Pick parking region (zero-probed) ---
    constexpr uint32_t PAGE = 0x1000;
    const uint32_t zone_size   = ctx.parking_max_rva - ctx.parking_min_rva;
    const uint32_t addressable = (zone_size - DX11_PARKING_TOTAL) & ~(PAGE - 1);

    uint64_t rng = seed_rng();
    uint64_t parking_va  = 0;
    uint32_t parking_rva = 0;
    for (uint32_t attempt = 0; attempt < 4; ++attempt) {
        const uint32_t offset = static_cast<uint32_t>(xorshift_next(rng) % addressable) & ~(PAGE - 1);
        parking_rva = ctx.parking_min_rva + offset;
        parking_va  = rtss_base + parking_rva;

        uint8_t check_buf[DX11_PARKING_TOTAL] = {};
        if (!cmdchannel::read_memory(game_pid, parking_va,
                                     reinterpret_cast<uint64_t>(check_buf),
                                     DX11_PARKING_TOTAL)) {
            std::printf("[!] redirect_d3d11_present: zero-probe READ failed\n");
            return false;
        }
        bool all_zero = true;
        for (uint32_t i = 0; i < DX11_PARKING_TOTAL; ++i) {
            if (check_buf[i] != 0) { all_zero = false; break; }
        }
        if (all_zero) break;
        parking_va = 0;
        if (attempt == 3) {
            std::printf("[!] redirect_d3d11_present: no clean parking region found in 4 attempts\n");
            return false;
        }
    }
    const uint64_t stub_va     = parking_va + DX11_PARKING_STUB_OFF;
    const uint64_t sentinel_va = parking_va + DX11_PARKING_SENTINEL_OFF;
    std::printf("[*] redirect_d3d11_present: parking_va=%016llX stub_va=%016llX "
                "sentinel_va=%016llX (rva=0x%X)\n",
                (unsigned long long)parking_va, (unsigned long long)stub_va,
                (unsigned long long)sentinel_va, parking_rva);

    // --- (4) Build parking buffer: stub + sentinel ---
    uint8_t parking_buf[DX11_PARKING_TOTAL] = {};
    uint8_t* stub = parking_buf + DX11_PARKING_STUB_OFF;
    std::memcpy(stub, STUB_TEMPLATE, STUB_SIZE);
    std::memcpy(stub + PATCH_SENTINEL_VA, &sentinel_va, sizeof(sentinel_va));

    // --- (5) Write parking buffer ---
    if (!cmdchannel::write_memory(game_pid, parking_va,
                                  reinterpret_cast<uint64_t>(parking_buf),
                                  DX11_PARKING_TOTAL)) {
        std::printf("[!] redirect_d3d11_present: parking WRITE failed\n");
        return false;
    }
    std::printf("[+] redirect_d3d11_present: parking staged (stub only, no vtable)\n");

    // --- (6) Flip parking page to RWX ---
    constexpr uint32_t PAGE_EXECUTE_READWRITE_VAL = 0x40;
    uint32_t parking_old_protect = 0;
    if (!cmdchannel::protect_memory(game_pid, parking_va, DX11_PARKING_TOTAL,
                                    PAGE_EXECUTE_READWRITE_VAL, &parking_old_protect)) {
        std::printf("[!] redirect_d3d11_present: parking PROTECT (-> RWX) failed\n");
        return false;
    }
    std::printf("[+] redirect_d3d11_present: parking page -> RWX (old=0x%X)\n",
                parking_old_protect);

    // --- (7) DETOURS mode: flip plant_slot page to RWX ---
    uint32_t plant_old_protect = 0;
    bool plant_prot_flipped = false;
    const bool is_detours = (f8c8_val == 0);
    if (is_detours) {
        if (!cmdchannel::protect_memory(game_pid, plant_slot_va, 8,
                                        PAGE_EXECUTE_READWRITE_VAL,
                                        &plant_old_protect)) {
            std::printf("[!] redirect_d3d11_present: plant slot PROTECT -> RWX failed\n");
            uint32_t discard = 0;
            (void)cmdchannel::protect_memory(game_pid, parking_va, DX11_PARKING_TOTAL,
                                             parking_old_protect, &discard);
            return false;
        }
        plant_prot_flipped = true;
        std::printf("[*] redirect_d3d11_present: plant slot PROTECT 0x%X -> RWX (DETOURS)\n",
                    plant_old_protect);
    }

    // --- (8) Overwrite plant_slot with stub_va ---
    if (!cmdchannel::write_memory(game_pid, plant_slot_va,
                                  reinterpret_cast<uint64_t>(&stub_va), 8)) {
        std::printf("[!] redirect_d3d11_present: plant slot WRITE failed\n");
        if (plant_prot_flipped) {
            uint32_t discard = 0;
            (void)cmdchannel::protect_memory(game_pid, plant_slot_va, 8,
                                             plant_old_protect, &discard);
        }
        uint32_t discard = 0;
        (void)cmdchannel::protect_memory(game_pid, parking_va, DX11_PARKING_TOTAL,
                                         parking_old_protect, &discard);
        return false;
    }
    std::printf("[+] redirect_d3d11_present: plant slot redirected -> stub_va "
                "(waiting for stub fire)\n");

    // --- (9) Poll sentinel (up to 5 s, 200 ms cadence) ---
    constexpr uint32_t POLL_INTERVAL_MS = 200;
    constexpr uint32_t POLL_TIMEOUT_MS  = 5000;
    constexpr uint32_t MAX_POLLS        = POLL_TIMEOUT_MS / POLL_INTERVAL_MS;

    bool fired = false;
    for (uint32_t i = 0; i < MAX_POLLS; ++i) {
        Sleep(POLL_INTERVAL_MS);
        uint32_t sent = 0;
        if (!cmdchannel::read_memory(game_pid, sentinel_va,
                                     reinterpret_cast<uint64_t>(&sent), 4)) {
            std::printf("[!] redirect_d3d11_present: sentinel READ failed during poll\n");
            break;
        }
        if (sent == 0xDEADBEEFu) {
            std::printf("[+] redirect_d3d11_present: STUB FIRED "
                        "(sentinel 0xDEADBEEF seen after %u ms)\n",
                        (i + 1) * POLL_INTERVAL_MS);
            fired = true;
            break;
        }
    }
    if (!fired) {
        std::printf("[!] redirect_d3d11_present: timeout — stub did NOT fire within %u ms\n",
                    POLL_TIMEOUT_MS);
    }

    // --- (10) Restore: plant_slot first, then scrub, then protections ---
    cmdchannel::write_memory(game_pid, plant_slot_va,
                             reinterpret_cast<uint64_t>(&saved_body), 8);

    if (is_detours) {
        std::printf("[*] redirect_d3d11_present: draining in-flight dispatch (200 ms)\n");
        Sleep(200);
    }

    uint8_t zero_buf[DX11_PARKING_TOTAL] = {};
    if (!cmdchannel::write_memory(game_pid, parking_va,
                                  reinterpret_cast<uint64_t>(zero_buf),
                                  DX11_PARKING_TOTAL)) {
        std::printf("[!] redirect_d3d11_present: parking scrub WRITE failed\n");
    } else {
        std::printf("[+] redirect_d3d11_present: parking region scrubbed\n");
    }

    if (plant_prot_flipped) {
        uint32_t discard = 0;
        (void)cmdchannel::protect_memory(game_pid, plant_slot_va, 8,
                                         plant_old_protect, &discard);
    }
    {
        uint32_t discard = 0;
        (void)cmdchannel::protect_memory(game_pid, parking_va, DX11_PARKING_TOTAL,
                                         parking_old_protect, &discard);
    }

    return fired;
}

} // namespace rtss_inject

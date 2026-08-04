// rtss_inject.h — Phase 16.b: detect + verify RTSS in game, then patch cheat.
//
// DESIGN NOTE (revised 2026-04-18):
//   We NO LONGER drop RTSSHooks64.dll or call InstallRTSSHook from the loader.
//   Tester pre-installs RTSS via the official Guru3D installer, configures
//   it to start with Windows + auto-inject the target game. Our loader's job
//   is reduced to:
//       (1) Detect RTSS is in game (driver cmd 2 FIND_MODULE)
//       (2) Identify the RTSS build (PE TimeDateStamp + SizeOfImage)
//       (3) Resolve version-specific offsets (callback descriptor)
//       (4) Patch cheat into RTSS via driver cmd 1 WRITE + cmd 5 PROTECT
//
//   Strictly-exceeds-sample per Replica Policy §1:
//     - No FileCreate from loader for RTSS files
//     - No LoadLibraryW of RTSS in loader
//     - No SetWindowsHookExW from loader
//     - No hook-ownership attribution pointing at loader
//     - RTSS lands via the user's own signed Afterburner/RTSS service
//   See injection-info.txt §2.8 for full attribution analysis.

#pragma once

#include <cstdint>

namespace rtss_inject {

struct Ctx {
    uint64_t rtss_base_in_game    = 0;  // filled by wait_for_rtss_in_game
    uint32_t rtss_size_in_game    = 0;
    uint32_t rtss_time_date_stamp = 0;  // filled by verify_in_game_rtss
    uint32_t rtss_size_of_image   = 0;

    // All RVAs below are populated by verify_in_game_rtss when the build is
    // recognised. Zero when the build is unknown — downstream phases must
    // refuse to proceed.

    // --- Redirect target + parking zone -----------------------------------
    uint32_t d3d12_obj_rva        = 0;  // A50F0 — per-slot D3D12 command list array base
    uint32_t parking_min_rva      = 0;  // inclusive lower bound of dormant zone
    uint32_t parking_max_rva      = 0;  // exclusive upper bound

    // --- Readiness / prerequisite-gate probes -----------------------------
    // Parallel per-slot arrays. RTSS only derefs A50F0[N] when ALL of
    // A50B0[N], A4FD0[N], A5290[N] are non-NULL (head gate in
    // sub_180048300). If any is zero at redirect time, we silently no-op.
    uint32_t slot_fence_a5050_rva = 0;  // fence object (A5050) — +0x40 called first
    uint32_t slot_fence_a50b0_rva = 0;  // second fence (A50B0) — do NOT redirect
    uint32_t slot_prereq_a4fd0_rva = 0; // upload heap (A4FD0)
    uint32_t slot_prereq_a5290_rva = 0; // primitive topology / format (A5290)
    uint32_t slot_frame_counter_rva = 0; // dword A5090[N] — per-slot tick counter,
                                         // best readiness + redirect-success probe

    // --- Master render-enable gate (sub_18004B090 body) -------------------
    // Six dwords OR'd together; if ALL zero, the whole render block is
    // skipped before A50F0 is touched. We flip [0] to 1 to force entry,
    // restore after. Remaining five probed as diagnostic only.
    uint32_t master_gate_rvas[6]  = {0, 0, 0, 0, 0, 0};

    // --- Ancillary state (diagnostic-only) --------------------------------
    uint32_t render_mode_rva      = 0;  // dword 183561B8C — 0=immediate 1=line 2=tri
    uint32_t render_state_rva     = 0;  // dword 183561BA8 — v11/v64, controls 3
                                        // extra A50F0[N].vtable[+0xD0] calls
    uint32_t swapchain_ptr_rva    = 0;  // qword 18355A9A8 — current swapchain backing
    uint32_t render_duration_rva  = 0;  // dword 18355A968 — last tick µs, nonzero
                                        // means RTSS actively rendering
};

// Poll driver cmd 2 (FIND_MODULE) against target game PID until
// RTSSHooks64.dll appears in its PEB.Ldr, or timeout.
// If RTSS is not there, tester's RTSS autostart/auto-inject is not working;
// helpful error messages printed.
bool wait_for_rtss_in_game(uint32_t game_pid, Ctx& ctx, uint32_t timeout_ms = 10000);

// After wait succeeds: read in-game RTSS PE headers via driver cmd 0 (READ).
// Fills ctx.rtss_time_date_stamp + ctx.rtss_size_of_image, plus descriptor
// and parking fields when build is known. Used downstream to dispatch to
// the correct version-specific descriptor RVA and parking zone.
bool verify_in_game_rtss(uint32_t game_pid, Ctx& ctx);

// Phase 16.b.beta: parking-zone probe. Validates the cross-process R/W path
// into RTSS's dormant-virtual tail before we attempt any redirect.
//   (1) pick random 4 KB-aligned RVA in [parking_min_rva, parking_max_rva),
//   (2) zero-probe 4 KB at candidate VA (retries on non-zero collision),
//   (3) write sentinel, read-back verify, scrub.
// Preconditions: wait_for_rtss_in_game succeeded, verify_in_game_rtss
// matched a known build (parking_min_rva != 0).
bool probe_parking_zone(uint32_t game_pid, const Ctx& ctx);

// Phase 16.b.gamma.pre: diagnostic snapshot of the DX12 render state.
// Reads every probe RVA in ctx via cross-process cmd 0 READ and prints a
// labeled block. Safe to call at any time; no writes. Useful before the
// redirect (confirm readiness) and after (diagnose failure path).
// `label` shows up in the header — pass "pre-redirect" / "post-redirect" etc.
bool snapshot_d3d12_state(uint32_t game_pid, const Ctx& ctx, const char* label);

// Slot sweep: dumps A50F0[0..15], A50B0[0..15], A4FD0[0..15], A5290[0..15],
// A5090[0..15] to find which slot (if any) RTSS populated. Also reads the
// D3D9-11 slot_7 descriptor @ 0x1A5F68 to check whether RTSS fell back to
// the old path (e.g. D3D11On12 compat). Populates `out_live_slot` with the
// first slot found where frame counter > 0; returns true if any slot live.
bool sweep_d3d12_slots(uint32_t game_pid, const Ctx& ctx, int* out_live_slot);

// Phase 16.b.gamma: D3D12 object-array redirect smoke test.
//
// The RTSS D3D12 render tick (sub_180048300, mis-labeled _UnwindNestedFrames
// by IDA — it is NOT an unwind handler) derefs a per-slot object array at
// qword_1801A50F0[N]. N is chosen at runtime via a vtable method on an
// outer obj (one slot per RTSS-tracked D3D12 swapchain).
//
// Across one render tick the object at A50F0[N] is called at 15 distinct
// vtable offsets total (command-list semantics):
//   {0x48, 0x50, 0x60, 0x78, 0x80, 0xA0, 0xA8, 0xB0,
//    0xC8, 0xD0, 0xE0, 0xF0, 0x160, 0x170, 0x180}
// Fake vtable must populate all 15 to point at a stub that returns 0.
//
// Prereq gate: A50F0 is only derefed when A50B0[N], A4FD0[N], A5290[N] are
// ALL non-NULL (parallel per-slot arrays in the 0x1A4E50..0x1A5300 state
// cluster). Before redirect, wait for these to populate — easiest proxy is
// watching dword_1801A5090[N] (per-slot frame counter) advance monotonically.
// Do NOT redirect A50B0[N] — its vtable[+0x40] is a GetRefCount-like that
// RTSS compares against the frame counter in a SwitchToThread spin loop;
// a bogus return can wedge the host.
//
// D3D9/10/11 (slot_7 at 0x1A5F68) is NOT used on DX12 targets — slot_7 lives
// on the sub_180053100 chain which DX12 games skip entirely.
//
// Sequence:
//   (1) pre-redirect state snapshot (diagnostic),
//   (2) readiness check: A50F0[0], A50B0[0], A4FD0[0], A5290[0] all non-NULL
//       AND frame counter A5090[0] > 0 (RTSS has rendered ≥1 DX12 frame),
//   (3) pick random parking base + zero-probe,
//   (4) save original A50F0[0] + all 6 master-gate dwords,
//   (5) stage parking: fake vtable (49 qwords = 0x188 B, all 15 needed
//       offsets pointing at stub body) + stub body at +0x200 +
//       sentinel at +0x280; patch stub template with sentinel VA,
//   (6) change parking page protection to PAGE_EXECUTE_READWRITE,
//   (7) flip master gate: master_gate[0] = 1 (forces render block entry),
//   (8) overwrite A50F0[0] = parking_va (the redirect),
//   (9) poll sentinel for 0xDEADBEEF (stub fired) up to 5 s,
//  (10) post-redirect state snapshot (success or timeout path),
//  (11) restore A50F0[0] + master gates, scrub parking, restore protect.
//
// Preconditions: probe_parking_zone succeeded. Target must be a D3D12 process
// with RTSS actively rendering its overlay.
bool redirect_d3d12_smoke(uint32_t game_pid, const Ctx& ctx);

// Phase 16.b.gamma (interop variant): redirect the interop-mode render
// target qword_18018EA90[0] when RTSS picked INTEROP over DIRECT mode.
//
// Reached when dword_183561B88 != 0 (selects sub_18004B8C0's interop
// branch). RTSS uses its OWN ID3D12Device to build overlay geometry and
// composites with the game swapchain via DXGI interop. Per-frame it calls
// vtable methods on qword_18018EA90[N] at offsets:
//   +0x20   (first call — pushes a per-swap resource ptr into some buffer)
//   +0x28   (second call — finalizes that push)
// plus vtable +0x378 on qword_18018EAD0[N] (we DO NOT redirect that — it's
// a real fence/sync object; spoofed return can wedge the host).
//
// Slot index N = same as direct (method +0x120 on swapchain). On this game
// the populated slot is 0; probe EA90[N] non-NULL before use.
//
// Stub is identical to the direct-mode one (mov rax,sentinel; write dword;
// xor eax,eax; ret). Fake vtable only needs +0x20 and +0x28 populated —
// rest self-ref to parking to crash-inside-parking if RTSS picks a
// surprise offset.
//
// No master-gate flip needed — the interop path doesn't consult those.
// No head-gate quad to check — EA90 population IS the gate.
bool redirect_d3d12_interop_smoke(uint32_t game_pid, const Ctx& ctx);

// Phase 16.b.gamma (DX11 fallback): smoke test via the F8C8/F8C0
// Present body-dispatch slot (the same slot invoke_dllmain uses).
// F8C8 is a direct function pointer — no fake vtable needed.
// backend_sel (RVA 0x355F8B8) must read 7 (D3D11).
bool redirect_d3d11_present_smoke(uint32_t game_pid, const Ctx& ctx);

} // namespace rtss_inject

// state.h — shared runtime state between main + overlay + scan.
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace state {

struct GameCtx {
    uint32_t pid                 = 0;
    uint64_t game_assembly_base  = 0;
    uint32_t game_assembly_size  = 0;
    uint64_t game_exe_base       = 0;
    uint32_t game_exe_size       = 0;

    // Bootstrap-time known singletons. Populated by discovery, or by
    // reading from a shared file the DLL emits (transition phase).
    uint64_t game_context_module = 0;

    // Frame counters + timing
    uint64_t frame_count         = 0;
    uint64_t last_scan_ms        = 0;
    uint64_t entity_count        = 0;

    // UI state (main-thread only)
    bool     click_through       = true;
    bool     menu_visible        = true;
    char     mem_viewer_addr[32] = "0";
    int      mem_viewer_bytes    = 128;

    // GCM entry — LO pastes address from crash_info.txt.
    // Auto-discovery lives in Phase 6.
    char     gcm_input[32]       = "0";
    bool     scan_enabled        = false;

    // Manually designated "self" entity id. When 0, scan uses heuristic
    // (first PlayerAvatar with position). When set, scan uses this
    // entity as the reference point for distance sorting + radar centering.
    int32_t  self_entity_id      = 0;

    // -----------------------------------------------------------
    // OpSec knobs (KWARE-style track covering during runtime)
    // -----------------------------------------------------------

    // Silent mode: no disk writes at all during operation (no perfmon.log,
    // no perfmon.ini writes). All state is memory-only. Config LOAD still
    // works so an existing perfmon.ini boots correctly, but nothing is
    // saved back. Use when we've verified stable settings and want a
    // clean disk footprint per session.
    bool     silent_mode         = false;

    // Preflight: refuse to attach if BEDaisy.sys is loaded. Prevents any
    // syscall/handle activity in the presence of the AC kernel driver.
    // Since we already run without OpenProcess handles this is a
    // belt-and-suspenders check — matches kware §3.16 WdFilter refusal.
    bool     preflight_bedaisy   = true;

    // Delay first scan by N seconds after attach — lets any BE
    // initial-scan window pass before we start touching game memory.
    int      first_scan_delay_s  = 5;

    // Scan tick base interval + jitter. Instead of a fixed 200ms tick
    // we vary between [base, base+jitter] each iteration so the syscall
    // rhythm isn't a clean sine wave.
    int      scan_tick_base_ms   = 180;
    int      scan_tick_jitter_ms = 120;
};

extern GameCtx g;

} // namespace state

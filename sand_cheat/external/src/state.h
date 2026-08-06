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
    uint64_t sand_exe_base       = 0;
    uint32_t sand_exe_size       = 0;

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
};

extern GameCtx g;

} // namespace state

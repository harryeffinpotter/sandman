// writeops.cpp — external game-state writes via cmdchannel.
//
// One pass per scan tick. Reads scan::g_snap indirectly via component
// lookups on entity ptrs already captured. For features that require
// scanning ALL entities (turret rapid fire), we iterate the snapshot
// once and per-entity check if the target component exists.

#include "writeops.h"
#include "scan.h"
#include "state.h"
#include "cmdchannel.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace writeops {

Config g_cfg;

namespace {

bool ext_write(uint64_t dst, const void* src, uint32_t size) {
    return cmdchannel::write_memory(state::g.pid, dst, (uint64_t)src, size);
}

void apply_force_interact() {
    if (!g_cfg.force_interact_lock || g_cfg.locked_target_id <= 0) return;
    if (!scan::g_player.entity_ptr || scan::g_indices.interact_target < 0) return;
    uint64_t it_comp = scan::get_component(scan::g_player.entity_ptr,
                                           scan::g_indices.interact_target);
    if (!it_comp) return;
    int32_t val = g_cfg.locked_target_id;
    ext_write(it_comp + 0x10, &val, sizeof(val));
}

void apply_turret_and_recoil() {
    bool rapid = g_cfg.turret_rapid_fire;
    bool norec = g_cfg.turret_no_recoil;
    if (!rapid && !norec) return;
    if (scan::g_indices.stationary_auto < 0 && scan::g_indices.recoil_look < 0) return;

    static std::vector<scan::Entity> snap;
    scan::copy_snapshot(snap);

    for (const auto& e : snap) {
        if (rapid && scan::g_indices.stationary_auto >= 0) {
            uint64_t sa = scan::get_component(e.ptr, scan::g_indices.stationary_auto);
            if (sa) {
                float val = 0.01f;
                ext_write(sa + 0x24, &val, sizeof(val));
            }
        }
        if (norec && scan::g_indices.recoil_look >= 0) {
            uint64_t rl = scan::get_component(e.ptr, scan::g_indices.recoil_look);
            if (rl) {
                uint8_t zeros[48] = {};
                ext_write(rl + 0x10, zeros, sizeof(zeros));
            }
        }
    }
}

} // namespace

void apply_all() {
    apply_force_interact();
    apply_turret_and_recoil();
}

} // namespace writeops

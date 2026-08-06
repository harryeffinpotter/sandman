// writeops.cpp — external game-state writes via cmdchannel.

#include "writeops.h"
#include "scan.h"
#include "state.h"
#include "cmdchannel.h"

#include <windows.h>
#include <cstdio>

namespace writeops {

Config g_cfg;

namespace {

bool ext_write(uint64_t dst, const void* src, uint32_t size) {
    return cmdchannel::write_memory(state::g.pid, dst, (uint64_t)src, size);
}

} // namespace

void apply_all() {
    // Force interact target — write chosen entity ID to the local player's
    // InteractTarget component. Any interact action then hits our pick
    // regardless of what the player is actually pointing at.
    if (g_cfg.force_interact_lock && g_cfg.locked_target_id > 0
        && scan::g_player.entity_ptr && scan::g_indices.interact_target >= 0) {
        uint64_t it_comp = scan::get_component(scan::g_player.entity_ptr,
                                               scan::g_indices.interact_target);
        if (it_comp) {
            int32_t val = g_cfg.locked_target_id;
            ext_write(it_comp + 0x10, &val, sizeof(val));
        }
    }
}

} // namespace writeops

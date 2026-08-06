// writeops.h — game-state mutations via cmdchannel::write_memory.
//
// Any feature that changes game behaviour lives here. All writes go
// through the kernel driver, so we can flip bits in any process from
// external without touching game code. Runs on the scan thread — one
// pass per tick.
#pragma once

#include <cstdint>

namespace writeops {

// Configuration flipped by UI checkboxes / hotkeys.
struct Config {
    bool     force_interact_lock = false;   // continuously write target_id
    int32_t  locked_target_id    = -1;
    // Turret / weapon writes (require entity classification we don't have
    // yet externally — placeholder for phase 10)
    bool     turret_rapid_fire   = false;
    bool     turret_no_recoil    = false;
};
extern Config g_cfg;

// Called every scan tick. Applies whatever writes are enabled.
void apply_all();

} // namespace writeops

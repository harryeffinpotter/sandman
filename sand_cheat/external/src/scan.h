// scan.h — external entity scanner.
//
// Reads sand.exe game state via cmdchannel and populates state::g with
// entity list snapshot. All memory reads are external — no code touches
// the game process.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace scan {

struct Entity {
    uint64_t ptr    = 0;   // remote VA of the entity struct in game process
    int32_t  id     = 0;
    bool     enabled= false;
    // Populated once name resolution + position resolution wired
    std::string name;
    float    x = 0, y = 0, z = 0;
    bool     has_pos = false;
};

// One scan tick — cheap when nothing changed, does a full walk when
// state::g.game_context_module is set. Populates the shared entity
// snapshot buffer.
// Returns true if entity_count was read successfully this tick.
bool tick();

// Snapshot access — copies latest entity list to caller's buffer.
// UI thread pulls this every frame to render.
size_t copy_snapshot(std::vector<Entity>& out);

} // namespace scan

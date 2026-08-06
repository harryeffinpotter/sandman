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
    std::string name;
    // World position (game coords are chunked: absolute = cx*CHUNK+x, cy*CHUNK+z)
    float    x = 0, y = 0, z = 0;
    int32_t  cx = 0, cy = 0;
    bool     has_pos = false;
};

// One-time (or on-GCM-change) discovery of component indices from
// GameContextModule + 0x20. Reads the componentNames array and matches
// well-known names (Position, BlueprintData, View, etc.).
bool discover_component_indices();

// Introspected indices — negative if not found in the current session.
struct ComponentIndices {
    int position   = -1;
    int blueprint  = -1;
    int view       = -1;
    int view_data  = -1;
    int parent     = -1;
    int nice_name  = -1;
    int account_id = -1;
    int user_name  = -1;
    int mob_state  = -1;
    int ai_agent   = -1;
    int large_item = -1;
    int item_type  = -1;
    int id         = -1;
};
extern ComponentIndices g_indices;

// One scan tick — cheap when nothing changed, does a full walk when
// state::g.game_context_module is set. Populates the shared entity
// snapshot buffer.
// Returns true if entity_count was read successfully this tick.
bool tick();

// Snapshot access — copies latest entity list to caller's buffer.
// UI thread pulls this every frame to render.
size_t copy_snapshot(std::vector<Entity>& out);

} // namespace scan

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
    // Computed each tick when player is known
    float    distance = -1.0f;
    // Classification (set by scan post-processing)
    bool     is_player = false;
    bool     is_mob    = false;
    bool     is_walker = false;
    bool     is_item   = false;
    bool     is_self   = false;
};

struct PlayerInfo {
    bool     found = false;
    uint64_t entity_ptr = 0;
    int32_t  id = 0;
    float    ax = 0, ay = 0, az = 0;  // absolute world position
};
extern PlayerInfo g_player;

// Chunk size for chunked coords (Unity floating-origin trick).
constexpr float CHUNK_SIZE = 100.0f;

// One-time (or on-GCM-change) discovery of component indices from
// GameContextModule + 0x20. Reads the componentNames array and matches
// well-known names (Position, BlueprintData, View, etc.).
bool discover_component_indices();

// Introspected indices — negative if not found in the current session.
struct ComponentIndices {
    int position         = -1;
    int blueprint        = -1;
    int view             = -1;
    int view_data        = -1;
    int parent           = -1;
    int nice_name        = -1;
    int account_id       = -1;
    int user_name        = -1;
    int mob_state        = -1;
    int ai_agent         = -1;
    int large_item       = -1;
    int item_type        = -1;
    int id               = -1;
    int interact_target  = -1;   // write target here to force interact
    int interactible     = -1;   // marks entities the player can interact with
    int invincible       = -1;   // add this component to make invincible
    int recoil_look      = -1;   // memset+0x10 for no-recoil
    int stationary_auto  = -1;   // +0x24 float = fire cooldown
    int weapon_overheat  = -1;
    int cheat_walker_fly = -1;
    int cheat_walker_spd = -1;
};
extern ComponentIndices g_indices;

// Public component lookup for write path.
uint64_t get_component(uint64_t entity, int idx);

// One scan tick — cheap when nothing changed, does a full walk when
// state::g.game_context_module is set. Populates the shared entity
// snapshot buffer.
// Returns true if entity_count was read successfully this tick.
bool tick();

// Snapshot access — copies latest entity list to caller's buffer.
// UI thread pulls this every frame to render.
size_t copy_snapshot(std::vector<Entity>& out);

} // namespace scan

// scan.cpp — external entity scanner over cmdchannel.
//
// Mirrors the DLL's scan_entities layout but does all reads via the
// kernel driver. Offsets are the same as the internal version (stable
// per IL2CPP game version).

#include "scan.h"
#include "state.h"
#include "cmdchannel.h"
#include "writeops.h"

#include <windows.h>
#include <mutex>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace scan {

ComponentIndices g_indices;
PlayerInfo       g_player;

namespace {

std::mutex          g_snap_lock;
std::vector<Entity> g_snap;
uint64_t            g_last_gcm_discovered = 0;

bool ext_read(uint64_t src, void* dst, uint32_t size) {
    return cmdchannel::read_memory(state::g.pid, src, (uint64_t)dst, size);
}

template <typename T>
bool ext_read_val(uint64_t src, T& out) {
    return ext_read(src, &out, sizeof(T));
}

// ---------- Il2CppString read ----------
bool read_il2cpp_string(uint64_t str_obj, std::string& out) {
    out.clear();
    if (!str_obj) return false;
    int32_t len = 0;
    if (!ext_read_val(str_obj + 0x10, len)) return false;
    if (len <= 0 || len > 300) return false;
    std::vector<uint16_t> wc(len);
    if (!ext_read(str_obj + 0x14, wc.data(), (uint32_t)(len * 2))) return false;
    out.reserve(len);
    for (int i = 0; i < len; i++) {
        uint16_t w = wc[i];
        out.push_back((char)(w & 0xFF));
    }
    return true;
}

// ---------- SlimDict lookup (mirror of DLL) ----------
// dict + 0x10 = buckets Il2CppArray (int32[])
// dict + 0x18 = entries Il2CppArray (24-byte entries)
//   entry: [hash:i32][key:i32][value:u64][next:i32][pad:i32]
// buckets are 1-based indices into entries
uint64_t dict_slim_lookup(uint64_t dict, int32_t key) {
    if (!dict) return 0;
    uint64_t buckets_arr = 0, entries_arr = 0;
    if (!ext_read_val(dict + 0x10, buckets_arr)) return 0;
    if (!ext_read_val(dict + 0x18, entries_arr)) return 0;
    if (!buckets_arr || !entries_arr) return 0;

    uint64_t bucket_count = 0, entry_count = 0;
    if (!ext_read_val(buckets_arr + 0x18, bucket_count)) return 0;
    if (!ext_read_val(entries_arr + 0x18, entry_count))  return 0;
    if (bucket_count == 0 || bucket_count > 500000) return 0;
    if (entry_count > 500000) return 0;

    // Read just the one bucket we care about
    int32_t bucket_idx = (int)((uint32_t)(key & 0x7FFFFFFF) % (uint32_t)bucket_count);
    int32_t entry_1based = 0;
    if (!ext_read_val(buckets_arr + 0x20 + bucket_idx * sizeof(int32_t), entry_1based)) return 0;
    int32_t i = entry_1based - 1;

    int safety = 0;
    while (i >= 0 && i < (int)entry_count && safety < 1000) {
        uint8_t entry[24];
        if (!ext_read(entries_arr + 0x20 + i * 24, entry, sizeof(entry))) return 0;
        int32_t  e_key  = *(int32_t*)(entry + 4);
        uint64_t e_val  = *(uint64_t*)(entry + 8);
        int32_t  e_next = *(int32_t*)(entry + 16);
        if (e_key == key) return e_val;
        i = e_next;
        safety++;
    }
    return 0;
}

// (definition moved to public API below)

// ---------- Component discovery ----------
bool discover_indices_internal(uint64_t gcm) {
    ComponentIndices out{};
    uint64_t componentNames = 0;
    if (!ext_read_val(gcm + 0x20, componentNames) || !componentNames) return false;

    uint64_t items_arr = 0;
    int32_t  size = 0;
    if (!ext_read_val(componentNames + 0x10, items_arr)) return false;
    if (!ext_read_val(componentNames + 0x18, size))      return false;
    if (!items_arr || size <= 0 || size > 10000) return false;

    // Grab all string pointers in one shot
    std::vector<uint64_t> str_ptrs(size);
    if (!ext_read(items_arr + 0x20, str_ptrs.data(),
                  (uint32_t)(size * sizeof(uint64_t)))) return false;

    struct Want { const char* name; int ComponentIndices::* slot; };
    static const Want wants[] = {
        { "Position",          &ComponentIndices::position   },
        { "BlueprintData",     &ComponentIndices::blueprint  },
        { "View",              &ComponentIndices::view       },
        { "ViewData",          &ComponentIndices::view_data  },
        { "Parent",            &ComponentIndices::parent     },
        { "NiceNameData",      &ComponentIndices::nice_name  },
        { "AccountId",         &ComponentIndices::account_id },
        { "UserNameComponent", &ComponentIndices::user_name  },
        { "MobState",          &ComponentIndices::mob_state  },
        { "AiAgentData",       &ComponentIndices::ai_agent   },
        { "LargeItemData",     &ComponentIndices::large_item },
        { "ItemTypeData",      &ComponentIndices::item_type  },
        { "Id",                &ComponentIndices::id         },
        { "InteractTarget",    &ComponentIndices::interact_target },
        { "InteractibleActive",&ComponentIndices::interactible },
        { "Invincible",        &ComponentIndices::invincible },
        { "RecoilLookOffset",  &ComponentIndices::recoil_look },
        { "StationaryAutoWeapon", &ComponentIndices::stationary_auto },
        { "WeaponOverheat",    &ComponentIndices::weapon_overheat },
        { "CheatWalkerFly",    &ComponentIndices::cheat_walker_fly },
        { "CheatWalkerSpeedMultiplier", &ComponentIndices::cheat_walker_spd },
        { "HealthData",        &ComponentIndices::health_data },
    };

    for (int i = 0; i < size; i++) {
        std::string name;
        if (!read_il2cpp_string(str_ptrs[i], name)) continue;
        for (const auto& w : wants) {
            if (name == w.name) out.*(w.slot) = i;
        }
    }
    g_indices = out;
    return true;
}

// ---------- Bulk pointer read ----------
bool ext_read_ptr_array(uint64_t src, size_t n, std::vector<uint64_t>& out) {
    out.clear();
    if (n == 0) return true;
    const size_t MAX_ITEMS = (1024 * 1024) / sizeof(uint64_t);
    out.reserve(n);
    size_t remaining = n;
    uint64_t cursor = src;
    while (remaining) {
        size_t chunk = remaining < MAX_ITEMS ? remaining : MAX_ITEMS;
        size_t old_sz = out.size();
        out.resize(old_sz + chunk);
        if (!ext_read(cursor, out.data() + old_sz, (uint32_t)(chunk * sizeof(uint64_t))))
            return false;
        cursor    += chunk * sizeof(uint64_t);
        remaining -= chunk;
    }
    return true;
}

} // namespace

// ---------- Public API ----------

uint64_t get_component(uint64_t entity, int idx) {
    if (idx < 0 || !entity) return 0;
    uint64_t dict = 0;
    if (!ext_read_val(entity + 0x50, dict)) return 0;
    return dict_slim_lookup(dict, idx);
}

bool discover_component_indices() {
    if (!state::g.game_context_module) return false;
    if (!discover_indices_internal(state::g.game_context_module)) return false;
    g_last_gcm_discovered = state::g.game_context_module;
    return true;
}

bool tick() {
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    uint64_t gcm = state::g.game_context_module;
    if (!gcm) return false;

    // Rediscover component indices if GCM changed
    if (gcm != g_last_gcm_discovered) discover_component_indices();

    uint64_t context = 0;
    if (!ext_read_val(gcm + 0x10, context) || !context) return false;

    uint64_t entity_arr_va = 0;
    uint64_t entity_count  = 0;

    uint64_t entities_cache = 0;
    ext_read_val(context + 0x98, entities_cache);
    if (entities_cache) {
        uint64_t sz = 0;
        if (ext_read_val(entities_cache + 0x18, sz) && sz < 200000) {
            entity_count  = sz;
            entity_arr_va = entities_cache + 0x20;
        }
    }

    std::vector<uint64_t> ptrs;
    if (entity_arr_va) {
        if (!ext_read_ptr_array(entity_arr_va, (size_t)entity_count, ptrs)) return false;
    } else {
        // HashSet fallback
        uint64_t hs = 0;
        if (!ext_read_val(context + 0x58, hs) || !hs) return false;
        uint64_t slots_arr = 0;
        int32_t  last_index = 0;
        if (!ext_read_val(hs + 0x18, slots_arr))  return false;
        if (!ext_read_val(hs + 0x24, last_index)) return false;
        if (!slots_arr || last_index <= 0 || last_index > 500000) return false;
        std::vector<uint8_t> slot_buf(size_t(last_index) * 16);
        if (!ext_read(slots_arr + 0x20, slot_buf.data(), (uint32_t)slot_buf.size()))
            return false;
        for (int i = 0; i < last_index; i++) {
            int32_t  hc = *(int32_t*)(slot_buf.data() + i * 16);
            uint64_t ep = *(uint64_t*)(slot_buf.data() + i * 16 + 8);
            if (hc < 0 || !ep) continue;
            ptrs.push_back(ep);
        }
        entity_count = ptrs.size();
    }

    // Build snapshot: entity + id + enabled + name + position
    std::vector<Entity> temp;
    temp.reserve(ptrs.size());

    for (uint64_t p : ptrs) {
        if (!p) continue;
        Entity e;
        e.ptr = p;
        int32_t id = 0; uint8_t enabled = 0;
        if (ext_read_val(p + 0x48, id))      e.id = id;
        if (ext_read_val(p + 0x4C, enabled)) e.enabled = (enabled != 0);

        // Blueprint name (if index resolved)
        if (g_indices.blueprint >= 0) {
            uint64_t bp = get_component(p, g_indices.blueprint);
            if (bp) {
                uint64_t name_str = 0;
                if (ext_read_val(bp + 0x10, name_str)) {
                    read_il2cpp_string(name_str, e.name);
                }
            }
        }

        // Position (WorldVector at +0x10 of Position component)
        if (g_indices.position >= 0) {
            uint64_t pos = get_component(p, g_indices.position);
            if (pos) {
                struct WV { float x, y, z; int32_t cx, cy; } wv{};
                if (ext_read_val(pos + 0x10, wv)) {
                    e.x = wv.x; e.y = wv.y; e.z = wv.z;
                    e.cx = wv.cx; e.cy = wv.cy;
                    e.has_pos = true;
                }
            }
        }

        // Health data (HealthData +0x10 = current, +0x14 = max — game-common layout)
        if (g_indices.health_data >= 0) {
            uint64_t hd = get_component(p, g_indices.health_data);
            if (hd) {
                float cur = 0, mx = 0;
                if (ext_read_val(hd + 0x10, cur) && ext_read_val(hd + 0x14, mx)) {
                    e.hp = cur;
                    e.hp_max = mx;
                }
            }
        }

        // Classify entity
        if (!e.name.empty()) {
            if (e.name.rfind("PlayerAvatar", 0) == 0)         e.is_player = true;
            else if (e.name.rfind("EXPEDITION_WALKER", 0) == 0) e.is_walker = true;
            else if (e.name.rfind("mob_", 0) == 0 ||
                     e.name.rfind("MobLivingSand", 0) == 0 ||
                     e.name.rfind("MobGhoul", 0) == 0)          e.is_mob = true;
            else e.is_item = true;
        }
        temp.push_back(std::move(e));
    }

    // Player detection —
    //   1. if operator manually set state::g.self_entity_id, use that
    //   2. else pick first PlayerAvatar with a position (heuristic)
    PlayerInfo pi{};
    if (state::g.self_entity_id > 0) {
        for (auto& e : temp) {
            if (e.id == state::g.self_entity_id && e.has_pos) {
                pi.found = true;
                pi.entity_ptr = e.ptr;
                pi.id = e.id;
                pi.ax = e.cx * CHUNK_SIZE + e.x;
                pi.ay = e.y;
                pi.az = e.cy * CHUNK_SIZE + e.z;
                e.is_self = true;
                break;
            }
        }
    }
    if (!pi.found) {
        for (auto& e : temp) {
            if (e.is_player && e.has_pos) {
                pi.found = true;
                pi.entity_ptr = e.ptr;
                pi.id = e.id;
                pi.ax = e.cx * CHUNK_SIZE + e.x;
                pi.ay = e.y;
                pi.az = e.cy * CHUNK_SIZE + e.z;
                e.is_self = true;
                break;
            }
        }
    }
    g_player = pi;

    // Distance from player
    if (pi.found) {
        for (auto& e : temp) {
            if (!e.has_pos) continue;
            float ax = e.cx * CHUNK_SIZE + e.x;
            float ay = e.y;
            float az = e.cy * CHUNK_SIZE + e.z;
            float dx = ax - pi.ax, dy = ay - pi.ay, dz = az - pi.az;
            e.distance = sqrtf(dx*dx + dy*dy + dz*dz);
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_snap_lock);
        g_snap = std::move(temp);
    }
    state::g.entity_count = entity_count;

    // Apply any enabled writes after the scan populated player + indices.
    writeops::apply_all();

    QueryPerformanceCounter(&t1);
    state::g.last_scan_ms = (uint64_t)((t1.QuadPart - t0.QuadPart) * 1000 / freq.QuadPart);
    return true;
}

size_t copy_snapshot(std::vector<Entity>& out) {
    std::lock_guard<std::mutex> lk(g_snap_lock);
    out = g_snap;
    return out.size();
}

} // namespace scan

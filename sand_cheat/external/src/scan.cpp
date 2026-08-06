// scan.cpp — external entity scanner over cmdchannel.
//
// Mirrors the DLL's scan_entities layout but does all reads via the
// kernel driver. Offsets are the same as the internal version (they
// come from IL2CPP class layout, stable per game version):
//
//   GameContextModule + 0x10   -> context ptr
//   context           + 0x98   -> entitiesCache ptr  (if present)
//   entitiesCache     + 0x18   -> size (int64)
//   entitiesCache     + 0x20   -> void*[size] entity pointer array
//   fallback (no cache): context + 0x58 -> HashSet<Entity>
//     hashSet + 0x18 -> slots array,  hashSet + 0x24 -> lastIndex
//     each slot is 16 bytes: [hash:i32 unused:i32 entity:i64]
//   entity + 0x48 -> entityId (int32)
//   entity + 0x4C -> enabled (bool)
//
// Chunked reads — we grab the entire pointer array in one kernel call
// rather than N tiny reads, then process locally. Same for slots.

#include "scan.h"
#include "state.h"
#include "cmdchannel.h"

#include <windows.h>
#include <mutex>
#include <vector>
#include <cstring>
#include <cstdio>

namespace scan {

namespace {

std::mutex          g_snap_lock;
std::vector<Entity> g_snap;

bool ext_read(uint64_t src, void* dst, uint32_t size) {
    return cmdchannel::read_memory(state::g.pid, src, (uint64_t)dst, size);
}

template <typename T>
bool ext_read_val(uint64_t src, T& out) {
    return ext_read(src, &out, sizeof(T));
}

// Grab N pointers starting at src into local vector.
bool ext_read_ptr_array(uint64_t src, size_t n, std::vector<uint64_t>& out) {
    out.clear();
    if (n == 0) return true;
    // cmdchannel caps a single read at 1MB — batch if huge.
    const size_t MAX_ITEMS_PER_READ = (1024 * 1024) / sizeof(uint64_t);
    out.reserve(n);
    size_t remaining = n;
    uint64_t cursor = src;
    while (remaining) {
        size_t chunk = remaining < MAX_ITEMS_PER_READ ? remaining : MAX_ITEMS_PER_READ;
        size_t old_sz = out.size();
        out.resize(old_sz + chunk);
        if (!ext_read(cursor, out.data() + old_sz, (uint32_t)(chunk * sizeof(uint64_t)))) {
            return false;
        }
        cursor += chunk * sizeof(uint64_t);
        remaining -= chunk;
    }
    return true;
}

} // namespace

bool tick() {
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    uint64_t gcm = state::g.game_context_module;
    if (!gcm) return false;

    uint64_t context = 0;
    if (!ext_read_val(gcm + 0x10, context) || !context) return false;

    // Try entities cache first (context + 0x98)
    uint64_t entities_cache = 0;
    ext_read_val(context + 0x98, entities_cache);

    uint64_t entity_arr_va = 0;
    uint64_t entity_count  = 0;

    if (entities_cache) {
        uint64_t sz = 0;
        if (ext_read_val(entities_cache + 0x18, sz) && sz < 200000) {
            entity_count = sz;
            entity_arr_va = entities_cache + 0x20;
        }
    }

    if (!entity_arr_va) {
        // Fallback: HashSet path
        uint64_t hs = 0;
        if (!ext_read_val(context + 0x58, hs) || !hs) return false;
        uint64_t slots_arr = 0;
        int32_t  last_index = 0;
        if (!ext_read_val(hs + 0x18, slots_arr)) return false;
        if (!ext_read_val(hs + 0x24, last_index)) return false;
        if (!slots_arr || last_index <= 0 || last_index > 500000) return false;

        // slots_arr is an Il2CppArray of 16-byte slots.
        // Each slot: [hashCode:i32][unused:i32][entity:u64]
        std::vector<uint8_t> slot_buf(size_t(last_index) * 16);
        if (!ext_read(slots_arr + 0x20, slot_buf.data(),
                      (uint32_t)slot_buf.size())) return false;

        std::vector<Entity> temp;
        temp.reserve(last_index);
        for (int i = 0; i < last_index; i++) {
            int32_t hc  = *(int32_t*)(slot_buf.data() + i * 16);
            uint64_t ep = *(uint64_t*)(slot_buf.data() + i * 16 + 8);
            if (hc < 0 || !ep) continue;
            Entity e;
            e.ptr = ep;
            temp.push_back(e);
        }
        entity_count = temp.size();

        // Read entityId + enabled per entity (small)
        for (auto& e : temp) {
            int32_t id = 0; uint8_t enabled = 0;
            if (ext_read_val(e.ptr + 0x48, id))     e.id = id;
            if (ext_read_val(e.ptr + 0x4C, enabled)) e.enabled = enabled != 0;
        }

        std::lock_guard<std::mutex> lk(g_snap_lock);
        g_snap = std::move(temp);
    } else {
        std::vector<uint64_t> ptrs;
        if (!ext_read_ptr_array(entity_arr_va, (size_t)entity_count, ptrs)) return false;

        std::vector<Entity> temp;
        temp.reserve(ptrs.size());
        for (uint64_t p : ptrs) {
            if (!p) continue;
            Entity e; e.ptr = p;
            int32_t id = 0; uint8_t enabled = 0;
            if (ext_read_val(p + 0x48, id))       e.id = id;
            if (ext_read_val(p + 0x4C, enabled))  e.enabled = enabled != 0;
            temp.push_back(e);
        }

        std::lock_guard<std::mutex> lk(g_snap_lock);
        g_snap = std::move(temp);
    }

    state::g.entity_count = entity_count;

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

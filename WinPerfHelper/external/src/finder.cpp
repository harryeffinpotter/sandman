// finder.cpp — external memory search primitives + GCM auto-discovery.

#include "finder.h"
#include "state.h"
#include "cmdchannel.h"

#include <windows.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>

namespace finder {

namespace {

bool ext_read(uint64_t src, void* dst, uint32_t size) {
    return cmdchannel::read_memory(state::g.pid, src, (uint64_t)dst, size);
}

template <typename T>
bool ext_read_val(uint64_t src, T& out) {
    return ext_read(src, &out, sizeof(T));
}

bool region_is_scannable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    DWORD p = mbi.Protect & 0xFF;
    if (p == PAGE_NOACCESS) return false;
    // MEM_IMAGE also scannable — GameAssembly.dll pages are MEM_IMAGE
    return true;
}

} // namespace

// -----------------------------------------------------------------------
// enumerate_regions
// -----------------------------------------------------------------------
size_t enumerate_regions(std::vector<Region>& out, size_t max_regions) {
    out.clear();
    uint64_t addr = 0;
    const uint64_t max_addr = 0x00007FFFFFFFFFFFULL;
    uint8_t mbi_buf[48] = {}; // driver returns 48-byte MEMORY_BASIC_INFORMATION
    while (addr < max_addr && out.size() < max_regions) {
        if (!cmdchannel::query_memory(state::g.pid, addr, mbi_buf)) break;
        MEMORY_BASIC_INFORMATION mbi;
        memcpy(&mbi, mbi_buf, sizeof(mbi));
        uint64_t base = (uint64_t)mbi.BaseAddress;
        uint64_t sz   = (uint64_t)mbi.RegionSize;
        if (sz == 0) break;
        if (region_is_scannable(mbi) && sz <= 0x10000000ULL) {
            Region r{ base, sz, mbi.Protect, mbi.Type };
            out.push_back(r);
        }
        addr = base + sz;
    }
    return out.size();
}

// -----------------------------------------------------------------------
// Internal: page-chunk read + memmem search in one region
// -----------------------------------------------------------------------
static size_t scan_region_for_bytes(const Region& r,
                                    const uint8_t* needle, size_t nlen,
                                    std::vector<uint64_t>& out, size_t max_hits) {
    if (nlen == 0 || nlen > 4096) return 0;
    if (r.size < nlen) return 0;

    const size_t CHUNK = 256 * 1024; // 256 KB per driver read
    std::vector<uint8_t> buf(CHUNK + nlen);
    size_t hits_before = out.size();

    uint64_t cursor = r.base;
    uint64_t end    = r.base + r.size;
    size_t   carry  = 0;   // leftover bytes from previous chunk to allow crossing boundary

    while (cursor < end && out.size() < max_hits) {
        uint64_t chunk_size = std::min<uint64_t>(CHUNK, end - cursor);
        if (!ext_read(cursor, buf.data() + carry, (uint32_t)chunk_size)) return out.size() - hits_before;
        size_t total = carry + (size_t)chunk_size;
        if (total >= nlen) {
            for (size_t i = 0; i + nlen <= total; i++) {
                if (buf[i] != needle[0]) continue;
                if (memcmp(buf.data() + i, needle, nlen) == 0) {
                    uint64_t hit_va = cursor - carry + i;
                    out.push_back(hit_va);
                    if (out.size() >= max_hits) break;
                }
            }
        }
        // carry last (nlen-1) bytes so matches spanning chunks aren't missed
        if (total >= nlen - 1) {
            carry = nlen - 1;
            memmove(buf.data(), buf.data() + total - carry, carry);
        } else {
            carry = 0;
        }
        cursor += chunk_size;
    }
    return out.size() - hits_before;
}

size_t find_bytes(const uint8_t* needle, size_t nlen,
                  std::vector<uint64_t>& out, size_t max_hits) {
    std::vector<Region> regions;
    enumerate_regions(regions);
    out.clear();
    for (const auto& r : regions) {
        scan_region_for_bytes(r, needle, nlen, out, max_hits);
        if (out.size() >= max_hits) break;
    }
    return out.size();
}

size_t find_wide_string(const wchar_t* str,
                        std::vector<uint64_t>& out, size_t max_hits) {
    size_t wlen = 0;
    while (str[wlen]) wlen++;
    std::vector<uint8_t> needle(wlen * 2);
    for (size_t i = 0; i < wlen; i++) {
        needle[i * 2 + 0] = (uint8_t)(str[i] & 0xFF);
        needle[i * 2 + 1] = (uint8_t)((str[i] >> 8) & 0xFF);
    }
    return find_bytes(needle.data(), needle.size(), out, max_hits);
}

size_t find_qword(uint64_t needle, std::vector<uint64_t>& out, size_t max_hits) {
    uint8_t bytes[8];
    memcpy(bytes, &needle, 8);
    return find_bytes(bytes, 8, out, max_hits);
}

// -----------------------------------------------------------------------
// GameContextModule auto-discovery
// -----------------------------------------------------------------------
uint64_t auto_discover_gcm() {
    // Step 1: find the wide string "GameContextModule\0"
    std::vector<uint64_t> str_hits;
    find_wide_string(L"GameContextModule", str_hits, 16);
    if (str_hits.empty()) {
        fprintf(stderr, "[finder] GameContextModule string not found\n");
        return 0;
    }

    // The class NAME is actually stored as a raw C-string, not an Il2CppString,
    // in IL2CPP metadata — so try the ASCII-null-terminated form separately.
    // Use the first wide hit (usually the Il2CppString instance).
    uint64_t name_str_va = str_hits[0];

    // Step 2: find pointers TO the string. Class::name is stored at a stable
    // offset from klass — we don't need to know that offset; instead we
    // enumerate all pointers to the string and treat each hit as candidate.
    // Then find pointers to each candidate — one of those is Il2CppObject::klass
    // on the runtime singleton, isolated from metadata clusters.
    std::vector<uint64_t> refs_to_str;
    find_qword(name_str_va, refs_to_str, 128);

    // Step 3: for each candidate name-field VA, guess klass = ref - N where
    // N is a small offset. Try 0x10 (common Il2CppClass::name offset).
    // For each guess, scan for pointers to it that AREN'T in the metadata
    // cluster (tightly-packed hits within 0x2000 = class method table).
    std::vector<std::pair<uint64_t, int>> singleton_candidates; // {addr, cluster_size}
    for (uint64_t ref : refs_to_str) {
        for (int guess_off : { 0x10, 0x18, 0x8, 0x20 }) {
            uint64_t klass_candidate = ref - guess_off;
            std::vector<uint64_t> klass_refs;
            find_qword(klass_candidate, klass_refs, 512);
            if (klass_refs.empty()) continue;

            // Find isolated hits (not part of the metadata cluster).
            std::sort(klass_refs.begin(), klass_refs.end());
            for (size_t i = 0; i < klass_refs.size(); i++) {
                bool isolated = true;
                for (size_t j = 0; j < klass_refs.size(); j++) {
                    if (i == j) continue;
                    uint64_t diff = klass_refs[i] > klass_refs[j]
                                    ? klass_refs[i] - klass_refs[j]
                                    : klass_refs[j] - klass_refs[i];
                    if (diff < 0x2000) { isolated = false; break; }
                }
                if (isolated) {
                    singleton_candidates.emplace_back(klass_refs[i], (int)klass_refs.size());
                }
            }
        }
    }

    if (singleton_candidates.empty()) {
        fprintf(stderr, "[finder] no isolated singleton candidates\n");
        return 0;
    }

    // The first isolated hit is our best guess for the singleton instance
    // (that's the address of the first qword of the Il2CppObject).
    uint64_t chosen = singleton_candidates.front().first;
    fprintf(stderr, "[finder] GCM auto-discovered: %llx (from %zu candidates)\n",
            (unsigned long long)chosen, singleton_candidates.size());
    return chosen;
}

} // namespace finder

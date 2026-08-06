// finder.h — external process memory scanner + IL2CPP klass/instance
// discovery. All reads flow through cmdchannel — zero touch on game.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace finder {

struct Region {
    uint64_t base;
    uint64_t size;
    uint32_t protect;   // Windows PAGE_* flags
    uint32_t type;      // MEM_PRIVATE / MEM_MAPPED / MEM_IMAGE
};

// Enumerate committed R/W regions in game process — used as scan target
// set. Skips reserved / no-access / guard / oversized regions. Bounded
// to `max_regions` entries to keep the vector manageable (e.g. 4096).
size_t enumerate_regions(std::vector<Region>& out, size_t max_regions = 4096);

// Search a byte pattern across all committed R/W regions. Returns list
// of absolute VAs where the pattern was found. Stops after `max_hits`.
size_t find_bytes(const uint8_t* needle, size_t nlen,
                  std::vector<uint64_t>& out, size_t max_hits = 32);

// Convenience: search a wide (UTF-16 LE) NUL-terminated string.
size_t find_wide_string(const wchar_t* str,
                        std::vector<uint64_t>& out, size_t max_hits = 32);

// Search for qword-aligned pointer values in R/W regions.
size_t find_qword(uint64_t needle,
                  std::vector<uint64_t>& out, size_t max_hits = 256);

// -----------------------------------------------------------------------
// High-level: auto-discover GameContextModule instance in target process.
// Two-stage scan:
//   1. locate the wide string "GameContextModule\0" in game memory
//   2. find pointers to it (that's Il2CppClass::name storage)
//   3. subtract standard il2cpp class-name field offset to get klass ptr
//   4. find pointers to klass (those are Il2CppObject::klass fields on
//      instances)
//   5. filter out metadata/class-descriptor clusters — the runtime
//      singleton lives alone in the small-object heap
// Returns 0 if not resolved.
// -----------------------------------------------------------------------
uint64_t auto_discover_gcm();

} // namespace finder

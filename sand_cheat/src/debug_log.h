#pragma once
#include <cstddef>

namespace ringlog {
constexpr size_t RING_CAP = 512;
constexpr size_t LINE_MAX = 256;

void push(const char* fmt, ...);
size_t count();
const char* line(size_t index_from_oldest);
void clear();

// Disk mirror: every push() also appends to this file with fflush after each
// line. Survives external TerminateProcess (e.g. BattlEye kill) — the OS
// closes handles but buffered stdio has already been flushed to the kernel
// write cache. File is truncated on first push after set_disk_mirror().
void set_disk_mirror(const wchar_t* path);
void force_flush();
}

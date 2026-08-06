#pragma once
#include <cstddef>

namespace ringlog {
constexpr size_t RING_CAP = 512;
constexpr size_t LINE_MAX = 256;

void push(const char* fmt, ...);
size_t count();
const char* line(size_t index_from_oldest);
void clear();
}

#include "debug_log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace {
struct Line { char text[ringlog::LINE_MAX + 1]; };
Line g_ring[ringlog::RING_CAP];
size_t g_head = 0;
size_t g_size = 0;
CRITICAL_SECTION g_lock;
bool g_lockInit = false;

void ensure_init() {
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
    }
}
}

namespace ringlog {
void push(const char* fmt, ...) {
    ensure_init();
    char tmp[LINE_MAX + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_lock);
    memcpy(g_ring[g_head].text, tmp, sizeof(tmp));
    g_head = (g_head + 1) % RING_CAP;
    if (g_size < RING_CAP) ++g_size;
    LeaveCriticalSection(&g_lock);
}
size_t count() { return g_size; }
const char* line(size_t idx) {
    if (idx >= g_size) return "";
    size_t start = (g_head + RING_CAP - g_size) % RING_CAP;
    return g_ring[(start + idx) % RING_CAP].text;
}
void clear() {
    ensure_init();
    EnterCriticalSection(&g_lock);
    g_head = 0; g_size = 0;
    LeaveCriticalSection(&g_lock);
}
}

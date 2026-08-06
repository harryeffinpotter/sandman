#include "debug_log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>

namespace {
struct Line { char text[ringlog::LINE_MAX + 1]; };
Line g_ring[ringlog::RING_CAP];
size_t g_head = 0;
size_t g_size = 0;
CRITICAL_SECTION g_lock;
bool g_lockInit = false;

FILE* g_diskFile = nullptr;
wchar_t g_diskPath[MAX_PATH] = {};
bool g_diskArmed = false;
bool g_diskTruncated = false;

void ensure_init() {
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
    }
}

// Caller holds g_lock.
void ensure_disk_open() {
    if (!g_diskArmed) return;
    if (g_diskFile) return;
    if (g_diskPath[0] == 0) return;
    const wchar_t* mode = g_diskTruncated ? L"a" : L"w";
    if (_wfopen_s(&g_diskFile, g_diskPath, mode) != 0) {
        g_diskFile = nullptr;
        return;
    }
    g_diskTruncated = true;
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

    ensure_disk_open();
    if (g_diskFile) {
        fputs(tmp, g_diskFile);
        size_t len = strlen(tmp);
        if (len == 0 || tmp[len - 1] != '\n') fputc('\n', g_diskFile);
        fflush(g_diskFile);
    }
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
void set_disk_mirror(const wchar_t* path) {
    ensure_init();
    EnterCriticalSection(&g_lock);
    if (g_diskFile) { fclose(g_diskFile); g_diskFile = nullptr; }
    g_diskPath[0] = 0;
    g_diskTruncated = false;
    if (path) {
        wcsncpy_s(g_diskPath, path, _TRUNCATE);
        g_diskArmed = true;
    } else {
        g_diskArmed = false;
    }
    LeaveCriticalSection(&g_lock);
}
void force_flush() {
    ensure_init();
    EnterCriticalSection(&g_lock);
    if (g_diskFile) fflush(g_diskFile);
    LeaveCriticalSection(&g_lock);
}
}

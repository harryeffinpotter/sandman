#include "debug_log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <share.h>

namespace {
struct Line {
    char text[ringlog::LINE_MAX + 1];
    int  count;
};
Line g_ring[ringlog::RING_CAP];
size_t g_head = 0;
size_t g_size = 0;
CRITICAL_SECTION g_lock;
bool g_lockInit = false;
bool g_paused = false;

FILE* g_diskFile = nullptr;
wchar_t g_diskPath[MAX_PATH] = {};
bool g_diskArmed = false;
bool g_diskTruncated = false;

// Shared display buffer for line() readers (formatted "text (Nx)" if count>1).
char g_displayBuf[ringlog::LINE_MAX + 32];

// Disk-side dedup state (line() uses ring's in-place count; disk is streaming
// so still needs its own pending-line collapse).
char g_pendLine[ringlog::LINE_MAX + 2];
int  g_pendCount = 0;

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
    g_diskFile = _wfsopen(g_diskPath, mode, _SH_DENYNO);
    if (!g_diskFile) return;
    g_diskTruncated = true;
}

// Caller holds g_lock. Emit pending disk-dedup line if any.
void commit_disk_pending_locked() {
    if (g_pendCount == 0) return;
    if (!g_diskFile) { g_pendCount = 0; return; }
    if (g_pendCount == 1) {
        fputs(g_pendLine, g_diskFile);
        fputc('\n', g_diskFile);
    } else {
        char suffix[32];
        snprintf(suffix, sizeof(suffix), " (%dx)", g_pendCount);
        fputs(g_pendLine, g_diskFile);
        fputs(suffix, g_diskFile);
        fputc('\n', g_diskFile);
    }
    g_pendCount = 0;
}
} // namespace

namespace ringlog {
void push(const char* fmt, ...) {
    ensure_init();
    if (g_paused) return;
    char tmp[LINE_MAX + 2];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    size_t tlen = strlen(tmp);
    while (tlen > 0 && (tmp[tlen - 1] == '\n' || tmp[tlen - 1] == '\r')) tmp[--tlen] = 0;

    EnterCriticalSection(&g_lock);

    // Ring dedup: if same as the most recent ring entry, just bump its count.
    if (g_size > 0) {
        size_t lastIdx = (g_head + RING_CAP - 1) % RING_CAP;
        if (strcmp(g_ring[lastIdx].text, tmp) == 0) {
            g_ring[lastIdx].count++;
        } else {
            memcpy(g_ring[g_head].text, tmp, tlen + 1);
            g_ring[g_head].count = 1;
            g_head = (g_head + 1) % RING_CAP;
            if (g_size < RING_CAP) ++g_size;
        }
    } else {
        memcpy(g_ring[g_head].text, tmp, tlen + 1);
        g_ring[g_head].count = 1;
        g_head = (g_head + 1) % RING_CAP;
        g_size = 1;
    }

    ensure_disk_open();
    if (g_diskFile) {
        if (g_pendCount > 0 && strcmp(g_pendLine, tmp) == 0) {
            g_pendCount++;
        } else {
            commit_disk_pending_locked();
            strncpy_s(g_pendLine, sizeof(g_pendLine), tmp, _TRUNCATE);
            g_pendCount = 1;
        }
    }
    LeaveCriticalSection(&g_lock);
}
size_t count() { return g_size; }
const char* line(size_t idx) {
    if (idx >= g_size) return "";
    size_t start = (g_head + RING_CAP - g_size) % RING_CAP;
    const Line& L = g_ring[(start + idx) % RING_CAP];
    if (L.count <= 1) return L.text;
    snprintf(g_displayBuf, sizeof(g_displayBuf), "%s (%dx)", L.text, L.count);
    return g_displayBuf;
}
void clear() {
    ensure_init();
    EnterCriticalSection(&g_lock);
    g_head = 0; g_size = 0;
    g_pendCount = 0;
    LeaveCriticalSection(&g_lock);
}
void set_disk_mirror(const wchar_t* path) {
    ensure_init();
    EnterCriticalSection(&g_lock);
    commit_disk_pending_locked();
    if (g_diskFile) { fflush(g_diskFile); fclose(g_diskFile); g_diskFile = nullptr; }
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
    commit_disk_pending_locked();
    if (g_diskFile) fflush(g_diskFile);
    LeaveCriticalSection(&g_lock);
}
void set_paused(bool paused) {
    ensure_init();
    EnterCriticalSection(&g_lock);
    g_paused = paused;
    if (paused) commit_disk_pending_locked();
    LeaveCriticalSection(&g_lock);
}
bool is_paused() {
    ensure_init();
    EnterCriticalSection(&g_lock);
    bool p = g_paused;
    LeaveCriticalSection(&g_lock);
    return p;
}
size_t dump_ring_to_file(const wchar_t* path) {
    ensure_init();
    if (!path) return 0;
    FILE* out = nullptr;
    if (_wfopen_s(&out, path, L"w") != 0 || !out) return 0;
    size_t written = 0;
    EnterCriticalSection(&g_lock);
    size_t start = (g_head + RING_CAP - g_size) % RING_CAP;
    for (size_t i = 0; i < g_size; ++i) {
        const Line& L = g_ring[(start + i) % RING_CAP];
        if (L.count > 1) fprintf(out, "%s (%dx)\n", L.text, L.count);
        else             fprintf(out, "%s\n", L.text);
        ++written;
    }
    LeaveCriticalSection(&g_lock);
    fflush(out);
    fclose(out);
    return written;
}
}

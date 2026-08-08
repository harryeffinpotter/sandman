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
    char normKey[ringlog::LINE_MAX + 1];  // dedup key with variable data masked
    int  count;
};
Line g_ring[ringlog::RING_CAP];

// Normalize a log line for fuzzy dedupe: mask sequences of hex/digit chars.
// "AV at 0x1234 eid=999 (skipped 47)" -> "AV at 0x# eid=# (skipped #)"
// So different addresses / entity IDs / counts collapse to the same key.
void normalize_line(const char* src, char* dst, size_t dstCap) {
    if (!src || !dst || dstCap == 0) { if (dst && dstCap) dst[0] = 0; return; }
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dstCap; ++si) {
        char c = src[si];
        bool isNum = (c >= '0' && c <= '9');
        // Treat 0x-prefixed runs as one hex blob
        if (c == '0' && src[si+1] == 'x') {
            if (di + 3 >= dstCap) break;
            dst[di++] = '0'; dst[di++] = 'x'; dst[di++] = '#';
            si += 2;
            while (src[si] && ((src[si] >= '0' && src[si] <= '9') ||
                               (src[si] >= 'a' && src[si] <= 'f') ||
                               (src[si] >= 'A' && src[si] <= 'F'))) si++;
            si--;
            continue;
        }
        if (isNum) {
            if (di == 0 || dst[di-1] != '#') dst[di++] = '#';
            while (src[si] && src[si] >= '0' && src[si] <= '9') si++;
            si--;
            continue;
        }
        dst[di++] = c;
    }
    dst[di] = 0;
}
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

    // Fuzzy dedup key — masks addresses/eids/counts so
    // "AV at 0x1234 eid=999" and "AV at 0x5678 eid=42" collapse.
    char normKey[LINE_MAX + 1];
    normalize_line(tmp, normKey, sizeof(normKey));

    EnterCriticalSection(&g_lock);

    // Ring dedup: check last 16 entries (not just most recent) for a
    // normalized-key match. Human text stays the same when only variable
    // data (addresses, IDs) changes -> collapse into a single entry with
    // a running count. Keeps the live log readable during stall storms.
    bool merged = false;
    if (g_size > 0) {
        size_t back = g_size < 16 ? g_size : 16;
        for (size_t off = 1; off <= back; ++off) {
            size_t idx = (g_head + RING_CAP - off) % RING_CAP;
            if (strcmp(g_ring[idx].normKey, normKey) == 0) {
                g_ring[idx].count++;
                // Refresh the visible text to the latest concrete instance
                // so LO always sees a real example, not a stale one.
                memcpy(g_ring[idx].text, tmp, tlen + 1);
                merged = true;
                break;
            }
        }
    }
    if (!merged) {
        memcpy(g_ring[g_head].text, tmp, tlen + 1);
        memcpy(g_ring[g_head].normKey, normKey, strlen(normKey) + 1);
        g_ring[g_head].count = 1;
        g_head = (g_head + 1) % RING_CAP;
        if (g_size < RING_CAP) ++g_size;
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

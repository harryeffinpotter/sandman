// opsec.cpp

#include "opsec.h"
#include "state.h"

#include <windows.h>
#include <psapi.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "psapi.lib")

namespace opsec {

namespace {

ULONGLONG g_settle_target_tick = 0;

// Check if a kernel driver is loaded by base name.
bool driver_loaded(const char* basename) {
    LPVOID drivers[512];
    DWORD  cbNeeded = 0;
    if (!EnumDeviceDrivers(drivers, sizeof(drivers), &cbNeeded)) return false;
    int count = cbNeeded / sizeof(LPVOID);
    for (int i = 0; i < count; i++) {
        char name[MAX_PATH] = {};
        if (GetDeviceDriverBaseNameA(drivers[i], name, MAX_PATH)) {
            if (_stricmp(name, basename) == 0) return true;
        }
    }
    return false;
}

} // namespace

bool preflight_ok() {
    if (!state::g.preflight_bedaisy) return true;
    if (driver_loaded("BEDaisy.sys")) {
        fprintf(stderr, "[opsec] BEDaisy.sys is loaded — refusing to attach.\n");
        return false;
    }
    if (driver_loaded("BEClient_x64.dll")) {
        fprintf(stderr, "[opsec] BEClient_x64 present — refusing.\n");
        return false;
    }
    return true;
}

uint32_t next_scan_delay_ms() {
    int base   = state::g.scan_tick_base_ms;
    int jitter = state::g.scan_tick_jitter_ms;
    if (base   < 50)  base = 50;
    if (jitter < 0)   jitter = 0;
    if (jitter > 500) jitter = 500;
    return (uint32_t)(base + (rand() % (jitter + 1)));
}

FILE* silent_fopen(const char* path, const char* mode) {
    if (state::g.silent_mode) return nullptr;
    FILE* f = nullptr;
    if (fopen_s(&f, path, mode) != 0) return nullptr;
    return f;
}

bool settled() {
    return GetTickCount64() >= g_settle_target_tick;
}

void arm_settle_timer() {
    int delay = state::g.first_scan_delay_s;
    if (delay < 0)  delay = 0;
    if (delay > 60) delay = 60;
    g_settle_target_tick = GetTickCount64() + (ULONGLONG)delay * 1000ULL;
    // Seed RNG for scan jitter — process-cycle based so each launch is different.
    srand((unsigned)(GetTickCount64() & 0xFFFFFFFF));
}

} // namespace opsec

// config.cpp — INI-style key=value settings persistence.

#include "config.h"
#include "state.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace config {

// Config lives alongside the trace file in %APPDATA%\Microsoft\PerfCache\.
// If APPDATA isn't set we fall back to the project directory for dev.
static const char* config_path() {
    static char path[MAX_PATH] = {};
    if (path[0]) return path;
    char appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (n && n < MAX_PATH) {
        char dir[MAX_PATH];
        snprintf(dir, sizeof(dir), "%s\\Microsoft\\PerfCache", appdata);
        CreateDirectoryA(dir, nullptr);
        snprintf(path, sizeof(path), "%s\\perfmon.ini", dir);
    } else {
        strncpy_s(path, sizeof(path),
                  "C:\\Users\\ysg\\projects\\WinPerfHelper\\external\\external_config.ini",
                  _TRUNCATE);
    }
    return path;
}

void load() {
    FILE* f = nullptr;
    if (fopen_s(&f, config_path(), "r") != 0 || !f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        // strip newline
        char* nl = strchr(val, '\n'); if (nl) *nl = 0;
        nl = strchr(val, '\r');       if (nl) *nl = 0;

        if (strcmp(key, "gcm") == 0) {
            uint64_t v = _strtoui64(val, nullptr, 16);
            if (v) {
                state::g.game_context_module = v;
                snprintf(state::g.gcm_input, sizeof(state::g.gcm_input), "%llX",
                         (unsigned long long)v);
                state::g.scan_enabled = true;
            }
        } else if (strcmp(key, "click_through") == 0) {
            state::g.click_through = (atoi(val) != 0);
        } else if (strcmp(key, "menu_visible") == 0) {
            state::g.menu_visible = (atoi(val) != 0);
        } else if (strcmp(key, "self_entity_id") == 0) {
            state::g.self_entity_id = atoi(val);
        } else if (strcmp(key, "silent_mode") == 0) {
            state::g.silent_mode = (atoi(val) != 0);
        } else if (strcmp(key, "preflight_bedaisy") == 0) {
            state::g.preflight_bedaisy = (atoi(val) != 0);
        } else if (strcmp(key, "first_scan_delay_s") == 0) {
            state::g.first_scan_delay_s = atoi(val);
        } else if (strcmp(key, "scan_tick_base_ms") == 0) {
            state::g.scan_tick_base_ms = atoi(val);
        } else if (strcmp(key, "scan_tick_jitter_ms") == 0) {
            state::g.scan_tick_jitter_ms = atoi(val);
        }
    }
    fclose(f);
}

void save() {
    FILE* f = nullptr;
    // Honour silent mode — don't persist state to disk when opsec silent.
    if (state::g.silent_mode) return;
    if (fopen_s(&f, config_path(), "w") != 0 || !f) return;
    fprintf(f, "gcm=%llX\n", (unsigned long long)state::g.game_context_module);
    fprintf(f, "click_through=%d\n", state::g.click_through ? 1 : 0);
    fprintf(f, "menu_visible=%d\n",  state::g.menu_visible ? 1 : 0);
    fprintf(f, "self_entity_id=%d\n", state::g.self_entity_id);
    fprintf(f, "silent_mode=%d\n", state::g.silent_mode ? 1 : 0);
    fprintf(f, "preflight_bedaisy=%d\n", state::g.preflight_bedaisy ? 1 : 0);
    fprintf(f, "first_scan_delay_s=%d\n", state::g.first_scan_delay_s);
    fprintf(f, "scan_tick_base_ms=%d\n", state::g.scan_tick_base_ms);
    fprintf(f, "scan_tick_jitter_ms=%d\n", state::g.scan_tick_jitter_ms);
    fclose(f);
}

} // namespace config

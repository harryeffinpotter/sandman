// config.cpp — INI-style key=value settings persistence.

#include "config.h"
#include "state.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace config {

static const char* CONFIG_PATH = "C:\\Users\\ysg\\projects\\sand_cheat\\external\\external_config.ini";

void load() {
    FILE* f = nullptr;
    if (fopen_s(&f, CONFIG_PATH, "r") != 0 || !f) return;
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
        }
    }
    fclose(f);
}

void save() {
    FILE* f = nullptr;
    if (fopen_s(&f, CONFIG_PATH, "w") != 0 || !f) return;
    fprintf(f, "gcm=%llX\n", (unsigned long long)state::g.game_context_module);
    fprintf(f, "click_through=%d\n", state::g.click_through ? 1 : 0);
    fprintf(f, "menu_visible=%d\n",  state::g.menu_visible ? 1 : 0);
    fprintf(f, "self_entity_id=%d\n", state::g.self_entity_id);
    fclose(f);
}

} // namespace config

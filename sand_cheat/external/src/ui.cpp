// ui.cpp — external overlay widgets.

#include "ui.h"
#include "state.h"
#include "overlay.h"
#include "cmdchannel.h"
#include "scan.h"
#include "finder.h"
#include "writeops.h"

#include "imgui.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace ui {

namespace {

// One-shot memory read for the memory viewer widget.
bool ext_read(uint32_t pid, uint64_t src, void* dst, uint32_t size) {
    return cmdchannel::read_memory(pid, src, (uint64_t)dst, size);
}

uint64_t parse_hex_addr(const char* s) {
    uint64_t v = 0;
    if (!s) return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | (uint64_t)d;
    }
    return v;
}

// -----------------------------------------------------------------------
// Widget: overview card
// -----------------------------------------------------------------------
void draw_overview() {
    auto& g = state::g;
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 260), ImGuiCond_FirstUseEver);
    ImGui::Begin("sand_external — overview");
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "KWARE-style external overlay");
    ImGui::Separator();
    ImGui::Text("Zero DLL injection.");
    ImGui::Text("Zero HWBP / vtable patches.");
    ImGui::Text("R/W via kernel driver cmdchannel.");
    ImGui::Separator();
    ImGui::Text("game pid:              %u", g.pid);
    ImGui::Text("GameAssembly.dll base: 0x%llX  size 0x%X",
                (unsigned long long)g.game_assembly_base, g.game_assembly_size);
    ImGui::Text("sand.exe base:         0x%llX  size 0x%X",
                (unsigned long long)g.sand_exe_base, g.sand_exe_size);
    if (g.game_context_module) {
        ImGui::Text("GameContextModule:     0x%llX", (unsigned long long)g.game_context_module);
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
                           "GameContextModule not yet resolved");
    }
    ImGui::Separator();
    ImGui::Text("frames rendered:  %llu", (unsigned long long)g.frame_count);
    ImGui::Text("last scan ms:     %llu", (unsigned long long)g.last_scan_ms);
    ImGui::Text("entity count:     %llu", (unsigned long long)g.entity_count);
    ImGui::Separator();
    ImGui::TextDisabled("INSERT: toggle click-through   |   HOME: toggle menu");
    ImGui::End();
}

// -----------------------------------------------------------------------
// Widget: memory viewer
// -----------------------------------------------------------------------
void draw_memory_viewer() {
    auto& g = state::g;
    ImGui::SetNextWindowPos(ImVec2(500, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 380), ImGuiCond_FirstUseEver);
    ImGui::Begin("mem viewer");
    ImGui::InputText("addr (hex)", g.mem_viewer_addr, sizeof(g.mem_viewer_addr));
    ImGui::SliderInt("bytes", &g.mem_viewer_bytes, 16, 512);
    ImGui::SameLine();
    static uint8_t buf[512] = {};
    static bool have_read = false;
    static bool last_ok = false;
    static uint64_t last_addr = 0;
    if (ImGui::Button("Read")) {
        uint64_t addr = parse_hex_addr(g.mem_viewer_addr);
        int n = g.mem_viewer_bytes;
        if (n < 16) n = 16;
        if (n > 512) n = 512;
        memset(buf, 0, sizeof(buf));
        last_ok = ext_read(g.pid, addr, buf, (uint32_t)n);
        last_addr = addr;
        have_read = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(last_ok ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.5f, 0.5f, 1),
                       have_read ? (last_ok ? "OK" : "read FAILED") : "");
    ImGui::Separator();
    if (have_read && last_ok) {
        int n = g.mem_viewer_bytes;
        if (n > 512) n = 512;
        for (int row = 0; row < n; row += 16) {
            char line[192];
            int off = snprintf(line, sizeof(line), "%016llX  ",
                               (unsigned long long)(last_addr + row));
            for (int b = 0; b < 16 && row + b < n; b++) {
                off += snprintf(line + off, sizeof(line) - off, "%02X ", buf[row + b]);
            }
            off += snprintf(line + off, sizeof(line) - off, " ");
            for (int b = 0; b < 16 && row + b < n; b++) {
                uint8_t c = buf[row + b];
                line[off++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
            }
            line[off] = 0;
            ImGui::TextUnformatted(line);
        }
    }
    ImGui::End();
}

// -----------------------------------------------------------------------
// Widget: entity list (placeholder until scan.cpp populates state)
// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// Widget: radar — top-down mini-map of entities relative to player
// -----------------------------------------------------------------------
void draw_radar() {
    ImGui::SetNextWindowPos(ImVec2(1080, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("radar");
    static float range = 100.0f;
    ImGui::SliderFloat("range (m)", &range, 20.0f, 500.0f, "%.0f");
    static bool show_players = true, show_mobs = true, show_walkers = true, show_items = false;
    ImGui::Checkbox("players", &show_players); ImGui::SameLine();
    ImGui::Checkbox("mobs",    &show_mobs);    ImGui::SameLine();
    ImGui::Checkbox("walkers", &show_walkers); ImGui::SameLine();
    ImGui::Checkbox("items",   &show_items);

    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
    ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
    if (canvas_sz.x < 100) canvas_sz.x = 100;
    if (canvas_sz.y < 100) canvas_sz.y = 100;
    float radius = (canvas_sz.x < canvas_sz.y ? canvas_sz.x : canvas_sz.y) * 0.5f - 4;
    ImVec2 center(canvas_p0.x + canvas_sz.x * 0.5f, canvas_p0.y + canvas_sz.y * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Background
    dl->AddCircleFilled(center, radius, IM_COL32(15, 15, 25, 220), 64);
    dl->AddCircle(center, radius, IM_COL32(80, 80, 120, 255), 64, 1.5f);
    // Range rings
    for (int i = 1; i <= 4; i++) {
        float r = radius * ((float)i / 4.0f);
        dl->AddCircle(center, r, IM_COL32(50, 50, 80, 180), 48, 1.0f);
    }
    // Crosshair
    dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y),
                IM_COL32(80, 80, 100, 160), 1.0f);
    dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius),
                IM_COL32(80, 80, 100, 160), 1.0f);
    // Self
    dl->AddCircleFilled(center, 4.0f, IM_COL32(80, 220, 80, 255));

    if (!scan::g_player.found) {
        ImGui::Dummy(canvas_sz);
        ImGui::TextDisabled("waiting for player position...");
        ImGui::End();
        return;
    }

    static std::vector<scan::Entity> snap;
    scan::copy_snapshot(snap);
    for (const auto& e : snap) {
        if (!e.has_pos || e.is_self) continue;
        if (e.is_player  && !show_players) continue;
        if (e.is_mob     && !show_mobs)    continue;
        if (e.is_walker  && !show_walkers) continue;
        if (e.is_item    && !show_items)   continue;

        float ax = e.cx * scan::CHUNK_SIZE + e.x;
        float az = e.cy * scan::CHUNK_SIZE + e.z;
        float dx = ax - scan::g_player.ax;
        float dz = az - scan::g_player.az;
        float dist2 = dx*dx + dz*dz;
        if (dist2 > range * range) continue;

        // Scale into radar space (top-down: x → right, z → up=north)
        float sx = center.x + (dx / range) * radius;
        float sy = center.y - (dz / range) * radius;

        ImU32 color;
        if (e.is_player)      color = IM_COL32(255, 60, 60, 255);
        else if (e.is_walker) color = IM_COL32(200, 200, 60, 255);
        else if (e.is_mob)    color = IM_COL32(230, 130, 40, 255);
        else                  color = IM_COL32(120, 180, 255, 220);

        dl->AddCircleFilled(ImVec2(sx, sy), e.is_player ? 4.0f : 3.0f, color);
    }
    ImGui::Dummy(canvas_sz);
    ImGui::End();
}

void draw_entity_list() {
    auto& g = state::g;
    ImGui::SetNextWindowPos(ImVec2(20, 300), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 340), ImGuiCond_FirstUseEver);
    ImGui::Begin("entities");

    ImGui::InputText("GameContextModule (hex)", g.gcm_input, sizeof(g.gcm_input));
    if (ImGui::Button("Apply GCM")) {
        g.game_context_module = parse_hex_addr(g.gcm_input);
        g.scan_enabled = (g.game_context_module != 0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Auto-discover")) {
        uint64_t discovered = finder::auto_discover_gcm();
        if (discovered) {
            g.game_context_module = discovered;
            snprintf(g.gcm_input, sizeof(g.gcm_input), "%llX",
                     (unsigned long long)discovered);
            g.scan_enabled = true;
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("scan enabled", &g.scan_enabled);
    ImGui::Separator();
    ImGui::Text("last scan:      %llu ms", (unsigned long long)g.last_scan_ms);
    ImGui::Text("entity count:   %llu",   (unsigned long long)g.entity_count);
    ImGui::Separator();

    ImGui::Text("indices: pos=%d bp=%d view=%d parent=%d",
                scan::g_indices.position, scan::g_indices.blueprint,
                scan::g_indices.view, scan::g_indices.parent);
    ImGui::Separator();

    // Filter
    static char filter[64] = "";
    ImGui::InputText("filter (name contains)", filter, sizeof(filter));
    static bool sort_by_dist = true;
    static bool only_players = false;
    ImGui::Checkbox("sort by distance", &sort_by_dist);
    ImGui::SameLine();
    ImGui::Checkbox("players only", &only_players);
    ImGui::Separator();

    if (scan::g_player.found) {
        ImGui::Text("SELF: id=%d abs=(%.1f, %.1f, %.1f)",
                    scan::g_player.id, scan::g_player.ax, scan::g_player.ay, scan::g_player.az);
    } else {
        ImGui::TextDisabled("player not found");
    }
    ImGui::Separator();

    // Snapshot + sort by distance
    static std::vector<scan::Entity> snap;
    scan::copy_snapshot(snap);
    if (sort_by_dist) {
        std::sort(snap.begin(), snap.end(),
                  [](const scan::Entity& a, const scan::Entity& b) {
                      if (a.distance < 0) return false;
                      if (b.distance < 0) return true;
                      return a.distance < b.distance;
                  });
    }
    ImGui::BeginChild("entlist", ImVec2(0, 200), true);
    int shown = 0;
    for (size_t i = 0; i < snap.size(); i++) {
        const auto& e = snap[i];
        if (only_players && !e.is_player) continue;
        if (filter[0]) {
            if (e.name.find(filter) == std::string::npos) continue;
        }
        if (shown >= 200) break;
        // Colour by category
        ImVec4 c(0.75f, 0.75f, 0.75f, 1.0f);
        if (e.is_self)        c = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
        else if (e.is_player) c = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        else if (e.is_walker) c = ImVec4(1.0f, 0.9f, 0.4f, 1.0f);
        else if (e.is_mob)    c = ImVec4(1.0f, 0.6f, 0.3f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        if (e.has_pos) {
            ImGui::Text("[%5d] %-30s dist=%6.1f pos=(%.0f,%.0f,%.0f)",
                        e.id, e.name.empty() ? "(no name)" : e.name.c_str(),
                        e.distance, e.x, e.y, e.z);
        } else {
            ImGui::Text("[%5d] %-30s (no pos)",
                        e.id, e.name.empty() ? "(no name)" : e.name.c_str());
        }
        ImGui::PopStyleColor();
        shown++;
    }
    if (shown == 0) ImGui::TextDisabled("(no entities match filter)");
    ImGui::EndChild();
    ImGui::End();
}

} // namespace

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

void set_click_through(bool ct) {
    HWND h = overlay::overlay_hwnd();
    if (!h) return;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ct) ex |= WS_EX_TRANSPARENT;
    else    ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
    state::g.click_through = ct;
}

void poll_hotkeys() {
    static bool ins_prev = false, home_prev = false;
    bool ins_now  = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    bool home_now = (GetAsyncKeyState(VK_HOME)   & 0x8000) != 0;
    if (ins_now && !ins_prev)   set_click_through(!state::g.click_through);
    if (home_now && !home_prev) state::g.menu_visible = !state::g.menu_visible;
    ins_prev  = ins_now;
    home_prev = home_now;
}

void draw_writeops() {
    ImGui::SetNextWindowPos(ImVec2(500, 420), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 240), ImGuiCond_FirstUseEver);
    ImGui::Begin("write ops");
    ImGui::Text("Live mutations to game state via kernel driver writes.");
    ImGui::Separator();

    ImGui::Checkbox("Force interact target lock", &writeops::g_cfg.force_interact_lock);
    ImGui::TextDisabled("Overrides player's InteractTarget every tick.");
    ImGui::InputInt("locked target entity id", &writeops::g_cfg.locked_target_id);

    if (scan::g_player.found && ImGui::Button("Lock nearest player")) {
        static std::vector<scan::Entity> snap;
        scan::copy_snapshot(snap);
        float best_d = 1e9f;
        int best_id = -1;
        for (const auto& e : snap) {
            if (!e.is_player || e.is_self || !e.has_pos) continue;
            if (e.distance >= 0 && e.distance < best_d) {
                best_d = e.distance;
                best_id = e.id;
            }
        }
        if (best_id > 0) writeops::g_cfg.locked_target_id = best_id;
    }
    ImGui::SameLine();
    if (scan::g_player.found && ImGui::Button("Lock nearest mob")) {
        static std::vector<scan::Entity> snap;
        scan::copy_snapshot(snap);
        float best_d = 1e9f;
        int best_id = -1;
        for (const auto& e : snap) {
            if (!e.is_mob || !e.has_pos) continue;
            if (e.distance >= 0 && e.distance < best_d) {
                best_d = e.distance;
                best_id = e.id;
            }
        }
        if (best_id > 0) writeops::g_cfg.locked_target_id = best_id;
    }
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Turret / weapon writes (any owned turret)");
    ImGui::Checkbox("Turret rapid fire",  &writeops::g_cfg.turret_rapid_fire);
    ImGui::TextDisabled("StationaryAutoWeapon +0x24 = 0.01f");
    ImGui::Checkbox("Turret no recoil",   &writeops::g_cfg.turret_no_recoil);
    ImGui::TextDisabled("RecoilLookOffset +0x10 = 48 zeros");
    ImGui::Separator();
    ImGui::Text("indices  interact=%d recoil=%d stationary=%d overheat=%d walker_fly=%d",
                scan::g_indices.interact_target, scan::g_indices.recoil_look,
                scan::g_indices.stationary_auto, scan::g_indices.weapon_overheat,
                scan::g_indices.cheat_walker_fly);
    ImGui::End();
}

void draw_all() {
    if (!state::g.menu_visible) return;
    draw_overview();
    draw_memory_viewer();
    draw_entity_list();
    draw_radar();
    draw_writeops();
}

} // namespace ui

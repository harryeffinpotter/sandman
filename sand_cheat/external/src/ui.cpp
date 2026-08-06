// ui.cpp — external overlay widgets.

#include "ui.h"
#include "state.h"
#include "overlay.h"
#include "cmdchannel.h"
#include "scan.h"

#include "imgui.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
    ImGui::Separator();

    // Snapshot
    static std::vector<scan::Entity> snap;
    scan::copy_snapshot(snap);
    ImGui::BeginChild("entlist", ImVec2(0, 200), true);
    int shown = 0;
    for (size_t i = 0; i < snap.size(); i++) {
        const auto& e = snap[i];
        if (filter[0]) {
            if (e.name.find(filter) == std::string::npos) continue;
        }
        if (shown >= 200) break;
        if (e.has_pos) {
            ImGui::Text("[%4d] id=%5d %-32s  pos=(%.1f, %.1f, %.1f) chunk=(%d,%d)",
                        e.id, e.id, e.name.empty() ? "(no name)" : e.name.c_str(),
                        e.x, e.y, e.z, e.cx, e.cy);
        } else {
            ImGui::Text("[%4d] id=%5d %-32s  (no pos)",
                        e.id, e.id, e.name.empty() ? "(no name)" : e.name.c_str());
        }
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

void draw_all() {
    if (!state::g.menu_visible) return;
    draw_overview();
    draw_memory_viewer();
    draw_entity_list();
}

} // namespace ui

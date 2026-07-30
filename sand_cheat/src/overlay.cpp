#include "overlay.h"
#include "cheat.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>

bool g_menuVisible = true;
static HWND g_overlayHwnd = nullptr;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static WNDCLASSEXW g_wc = {};

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL featureLevelArray[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (hr != S_OK) return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
    return true;
}

bool overlay_init() {
    g_wc = { sizeof(g_wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr),
              nullptr, nullptr, nullptr, nullptr, L"SandOverlay", nullptr };
    RegisterClassExW(&g_wc);

    g_overlayHwnd = CreateWindowExW(
        0, g_wc.lpszClassName, L"Item Manager",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 700, 550,
        nullptr, nullptr, g_wc.hInstance, nullptr);

    if (!g_overlayHwnd) return false;

    ShowWindow(g_overlayHwnd, SW_SHOW);
    UpdateWindow(g_overlayHwnd);

    if (!CreateDeviceD3D(g_overlayHwnd)) {
        DestroyWindow(g_overlayHwnd);
        UnregisterClassW(g_wc.lpszClassName, g_wc.hInstance);
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(g_overlayHwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    return true;
}

void overlay_render() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) {
            g_running.store(false);
            return;
        }
    }

    if (key_pressed('D')) {
        bool was = g_dupeMode.load();
        g_dupeMode.store(!was);
        g_stickyLock.store(false);
        if (was) {
            g_permaLockActive.store(false);
            g_permaLockName.clear();
            g_lockedEntityId.store(-1);
            g_lockedEntityPtr.store(0);
        }
    }
    if (key_pressed('S')) {
        EnterCriticalSection(&g_itemsLock);
        if (!g_items.empty()) {
            auto& top = g_items[0];
            g_permaLockName = top.name;
            g_permaLockActive.store(true);
            int lockId = (top.serverId > 0) ? top.serverId : top.entityId;
            g_lockedEntityId.store(lockId);
            g_lockedEntityPtr.store((uintptr_t)top.entityPtr);
            g_stickyLock.store(true);
            g_dupeMode.store(false);
        }
        LeaveCriticalSection(&g_itemsLock);
    }
    if (key_pressed('U')) {
        g_permaLockActive.store(false);
        g_permaLockName.clear();
        g_lockedEntityId.store(-1);
        g_lockedEntityPtr.store(0);
        g_dupeMode.store(false);
        g_stickyLock.store(false);
    }
    if (key_pressed('H')) {
        g_heavyBypass.store(!g_heavyBypass.load());
    }
    if (key_pressed('W')) {
        g_weaponFilter.store(!g_weaponFilter.load());
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    {
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(displaySize);
        ImGui::Begin("##Main", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        ImGui::Text("Status: ");
        ImGui::SameLine();
        if (g_stickyLock.load()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "STICKY: %s", g_permaLockName.c_str());
        } else if (g_dupeMode.load()) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "DUPE MODE");
        } else if (g_permaLockActive.load()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "LOCKED: %s", g_permaLockName.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "SCANNING");
        }

        ImGui::Text("Player: (%.1f, %.1f, %.1f) [C:%d,%d]",
            g_playerPos.x, g_playerPos.y, g_playerPos.z, g_playerPos.cx, g_playerPos.cy);
        ImGui::Text("Entities: %d", g_entityCount.load());

        if (g_permaLockActive.load())
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Locked: %s", g_permaLockName.c_str());

        ImGui::Separator();

        ImGui::Text("Controls");
        {
            bool dupe = g_dupeMode.load();
            if (ImGui::Checkbox("Dupe Mode", &dupe)) {
                g_dupeMode.store(dupe);
                if (!dupe) {
                    g_stickyLock.store(false);
                    g_permaLockActive.store(false);
                    g_permaLockName.clear();
                    g_lockedEntityId.store(-1);
                    g_lockedEntityPtr.store(0);
                }
            }
        }
        {
            bool sticky = g_stickyLock.load();
            ImGui::Checkbox("Sticky Lock", &sticky);
            if (sticky && !g_stickyLock.load()) {
                EnterCriticalSection(&g_itemsLock);
                if (!g_items.empty()) {
                    auto& top = g_items[0];
                    g_permaLockName = top.name;
                    g_permaLockActive.store(true);
                    int lockId = (top.serverId > 0) ? top.serverId : top.entityId;
                    g_lockedEntityId.store(lockId);
                    g_lockedEntityPtr.store((uintptr_t)top.entityPtr);
                    g_stickyLock.store(true);
                    g_dupeMode.store(false);
                }
                LeaveCriticalSection(&g_itemsLock);
            } else if (!sticky && g_stickyLock.load()) {
                g_stickyLock.store(false);
            }
        }
        {
            bool heavy = g_heavyBypass.load();
            if (ImGui::Checkbox("Heavy Bypass", &heavy))
                g_heavyBypass.store(heavy);
        }
        {
            bool wfilt = g_weaponFilter.load();
            if (ImGui::Checkbox("Weapon Filter", &wfilt))
                g_weaponFilter.store(wfilt);
        }

        if (ImGui::Button("Unlock All")) {
            g_permaLockActive.store(false);
            g_permaLockName.clear();
            g_lockedEntityId.store(-1);
            g_lockedEntityPtr.store(0);
            g_dupeMode.store(false);
            g_stickyLock.store(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Dump Entities")) {
            g_dumpEntities.store(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Probe Context")) {
            g_probeContext.store(true);
        }

        ImGui::Separator();

        ImGui::Text("Items");
        ImGui::BeginChild("ItemList", ImVec2(0, 0), true);

        if (ImGui::BeginTable("Items", 6,
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Src", ImGuiTableColumnFlags_WidthFixed, 45.0f);
            ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            EnterCriticalSection(&g_itemsLock);

            int displayIdx = 0;
            for (size_t i = 0; i < g_items.size(); i++) {
                const ItemInfo& item = g_items[i];
                if (g_weaponFilter.load() && !item.isWeapon) continue;

                bool isLocked = g_permaLockActive.load() && (item.name == g_permaLockName);

                ImVec4 color;
                if (isLocked) color = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
                else if (item.isWeapon) color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                else if (item.isHeldByPlayer) color = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
                else color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                ImGui::TableNextRow();
                ImGui::PushStyleColor(ImGuiCol_Text, color);

                char label[32];
                snprintf(label, sizeof(label), "%d", displayIdx + 1);

                ImGui::TableSetColumnIndex(0);
                bool clicked = ImGui::Selectable(label, isLocked,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);

                if (clicked) {
                    g_permaLockName = item.name;
                    g_permaLockActive.store(true);
                    int lockId = (item.serverId > 0) ? item.serverId : item.entityId;
                    g_lockedEntityId.store(lockId);
                    g_lockedEntityPtr.store((uintptr_t)item.entityPtr);
                    g_dupeMode.store(false);
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(item.name.c_str());

                ImGui::TableSetColumnIndex(2);
                if (item.distance >= 0)
                    ImGui::Text("%.1fm", item.distance);
                else
                    ImGui::TextUnformatted("---");

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(item.isHeldByPlayer ? "PAR" : "WRLD");

                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(item.isHeavy ? "HVY" : (item.isWeapon ? "WPN" : ""));

                ImGui::TableSetColumnIndex(5);
                ImGui::Text("%d", item.entityId);

                ImGui::PopStyleColor();
                displayIdx++;
            }

            LeaveCriticalSection(&g_itemsLock);
            ImGui::EndTable();
        }

        ImGui::EndChild();
        ImGui::End();
    }

    ImGui::Render();
    const float clear_color[4] = { 0.06f, 0.06f, 0.06f, 1.0f };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);
}

void overlay_shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

    if (g_overlayHwnd) { DestroyWindow(g_overlayHwnd); g_overlayHwnd = nullptr; }
    UnregisterClassW(g_wc.lpszClassName, g_wc.hInstance);
}

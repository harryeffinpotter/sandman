// overlay.cpp — external stream-proof ImGui overlay for sand.exe.
//
// Design:
//   - Enumerate windows owned by the game PID, pick the largest visible
//     top-level window (usually Unity's main render window).
//   - Create our own top-level window sized to that game window, no
//     activation, no taskbar entry, click-through by default.
//   - Attach a DXGI composition swap chain via DirectComposition so the
//     window can be truly transparent (no black bg, no client-area frame).
//   - SetWindowDisplayAffinity(WDA_MONITOR = 0x1) so screen capture APIs
//     see black in our region — stream-proof at the compositor level.
//   - Repositions itself every frame to follow the game window (handles
//     move / resize / fullscreen transitions).

#include "overlay.h"

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dcomp.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace overlay {

namespace {

uint32_t                g_game_pid       = 0;
HWND                    g_game_hwnd      = nullptr;
HWND                    g_overlay_hwnd   = nullptr;
UINT                    g_last_w         = 0;
UINT                    g_last_h         = 0;
RECT                    g_last_game_rect = {};

ID3D11Device*           g_dev            = nullptr;
ID3D11DeviceContext*    g_ctx            = nullptr;
IDXGISwapChain1*        g_swap           = nullptr;
ID3D11RenderTargetView* g_rtv            = nullptr;

IDCompositionDevice*    g_dcomp_dev      = nullptr;
IDCompositionTarget*    g_dcomp_target   = nullptr;
IDCompositionVisual*    g_dcomp_visual   = nullptr;

bool                    g_imgui_up       = false;

// ---------------------------------------------------------------------
// Game window discovery
// ---------------------------------------------------------------------

struct EnumCtx { uint32_t pid; HWND best; LONG best_area; };

static BOOL CALLBACK enum_windows_cb(HWND h, LPARAM lparam) {
    if (!IsWindowVisible(h)) return TRUE;
    DWORD owner_pid = 0;
    GetWindowThreadProcessId(h, &owner_pid);
    auto* ctx = (EnumCtx*)lparam;
    if (owner_pid != ctx->pid) return TRUE;
    RECT r;
    if (!GetWindowRect(h, &r)) return TRUE;
    LONG area = (r.right - r.left) * (r.bottom - r.top);
    if (area > ctx->best_area) {
        ctx->best_area = area;
        ctx->best = h;
    }
    return TRUE;
}

static HWND find_game_window(uint32_t pid) {
    EnumCtx ctx{ pid, nullptr, 0 };
    EnumWindows(enum_windows_cb, (LPARAM)&ctx);
    return ctx.best;
}

// ---------------------------------------------------------------------
// Window proc
// ---------------------------------------------------------------------

static LRESULT CALLBACK overlay_wnd_proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return 0;
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// ---------------------------------------------------------------------
// RTV rebuild — call after swapchain resize
// ---------------------------------------------------------------------

static bool build_rtv() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    HRESULT hr = g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return SUCCEEDED(hr);
}

} // namespace

// ---------------------------------------------------------------------
// init
// ---------------------------------------------------------------------

bool init(uint32_t game_pid) {
    g_game_pid = game_pid;
    g_game_hwnd = find_game_window(game_pid);
    if (!g_game_hwnd) {
        fprintf(stderr, "[overlay] no game window for pid %u\n", game_pid);
        return false;
    }

    RECT gr;
    GetWindowRect(g_game_hwnd, &gr);
    g_last_game_rect = gr;
    g_last_w = gr.right - gr.left;
    g_last_h = gr.bottom - gr.top;
    if (g_last_w < 32) g_last_w = 32;
    if (g_last_h < 32) g_last_h = 32;

    static wchar_t className[32];
    wsprintfW(className, L"SANDX_%u", GetTickCount());
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = overlay_wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // WS_EX_NOACTIVATE + TOOLWINDOW = no taskbar + doesn't steal focus.
    // WS_EX_TRANSPARENT starts as click-through; toggled off when menu open.
    // WS_EX_LAYERED not needed — DirectComposition handles alpha.
    g_overlay_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        className, L"PerfMonSvc",
        WS_POPUP,
        gr.left, gr.top, g_last_w, g_last_h,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_overlay_hwnd) {
        fprintf(stderr, "[overlay] CreateWindowEx failed: %lu\n", GetLastError());
        return false;
    }

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_overlay_hwnd, &margins);
    // WDA_MONITOR (0x1) = stream-proof; capture APIs see black.
    SetWindowDisplayAffinity(g_overlay_hwnd, 0x1);

    ShowWindow(g_overlay_hwnd, SW_SHOWNOACTIVATE);

    // D3D11 device + immediate context
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &g_dev, nullptr, &g_ctx);
    if (FAILED(hr)) {
        fprintf(stderr, "[overlay] D3D11CreateDevice failed 0x%lX\n", hr);
        return false;
    }

    IDXGIDevice*   dxgi_dev  = nullptr;
    IDXGIAdapter*  adapter   = nullptr;
    IDXGIFactory2* factory2  = nullptr;
    g_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev);
    dxgi_dev->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory2));

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = g_last_w;
    scd.Height = g_last_h;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = factory2->CreateSwapChainForComposition(g_dev, &scd, nullptr, &g_swap);
    if (FAILED(hr)) {
        fprintf(stderr, "[overlay] CreateSwapChainForComposition failed 0x%lX\n", hr);
        factory2->Release(); adapter->Release(); dxgi_dev->Release();
        return false;
    }

    hr = DCompositionCreateDevice(dxgi_dev, IID_PPV_ARGS(&g_dcomp_dev));
    if (FAILED(hr)) { fprintf(stderr, "[overlay] DCompositionCreateDevice 0x%lX\n", hr); return false; }
    hr = g_dcomp_dev->CreateTargetForHwnd(g_overlay_hwnd, TRUE, &g_dcomp_target);
    if (FAILED(hr)) { fprintf(stderr, "[overlay] CreateTargetForHwnd 0x%lX\n", hr); return false; }
    hr = g_dcomp_dev->CreateVisual(&g_dcomp_visual);
    if (FAILED(hr)) { fprintf(stderr, "[overlay] CreateVisual 0x%lX\n", hr); return false; }
    g_dcomp_visual->SetContent(g_swap);
    g_dcomp_target->SetRoot(g_dcomp_visual);
    g_dcomp_dev->Commit();

    factory2->Release(); adapter->Release(); dxgi_dev->Release();

    if (!build_rtv()) {
        fprintf(stderr, "[overlay] build_rtv failed\n");
        return false;
    }

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_overlay_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    g_imgui_up = true;

    return true;
}

// ---------------------------------------------------------------------
// alive
// ---------------------------------------------------------------------

bool alive() {
    return g_overlay_hwnd != nullptr && IsWindow(g_overlay_hwnd);
}

// ---------------------------------------------------------------------
// pump_messages
// ---------------------------------------------------------------------

void pump_messages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ---------------------------------------------------------------------
// Reposition + resize to follow game window
// ---------------------------------------------------------------------

static void follow_game_window() {
    if (!IsWindow(g_game_hwnd)) return;
    RECT gr;
    if (!GetWindowRect(g_game_hwnd, &gr)) return;
    if (memcmp(&gr, &g_last_game_rect, sizeof(gr)) == 0) return;
    g_last_game_rect = gr;
    UINT w = gr.right - gr.left;
    UINT h = gr.bottom - gr.top;
    if (w < 32) w = 32;
    if (h < 32) h = 32;
    SetWindowPos(g_overlay_hwnd, HWND_TOPMOST, gr.left, gr.top, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (w != g_last_w || h != g_last_h) {
        g_last_w = w; g_last_h = h;
        if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
        g_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        build_rtv();
    }
}

// ---------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------

void frame(void (*paint)()) {
    if (!g_imgui_up) return;
    follow_game_window();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (paint) paint();

    ImGui::Render();

    float clear[4] = { 0.f, 0.f, 0.f, 0.f };  // transparent
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    g_swap->Present(1, 0);
    if (g_dcomp_dev) g_dcomp_dev->Commit();
}

// ---------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------

void shutdown() {
    if (g_imgui_up) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imgui_up = false;
    }
    if (g_rtv)           { g_rtv->Release();           g_rtv = nullptr; }
    if (g_dcomp_visual)  { g_dcomp_visual->Release();  g_dcomp_visual = nullptr; }
    if (g_dcomp_target)  { g_dcomp_target->Release();  g_dcomp_target = nullptr; }
    if (g_dcomp_dev)     { g_dcomp_dev->Release();     g_dcomp_dev = nullptr; }
    if (g_swap)          { g_swap->Release();          g_swap = nullptr; }
    if (g_ctx)           { g_ctx->Release();           g_ctx = nullptr; }
    if (g_dev)           { g_dev->Release();           g_dev = nullptr; }
    if (g_overlay_hwnd)  { DestroyWindow(g_overlay_hwnd); g_overlay_hwnd = nullptr; }
}

HWND game_hwnd()    { return g_game_hwnd; }
HWND overlay_hwnd() { return g_overlay_hwnd; }

} // namespace overlay

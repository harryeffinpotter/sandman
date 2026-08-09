#include "overlay.h"
#include "win.h"
#include "debug_log.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <dcomp.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <unordered_set>
#include <cmath>

extern std::atomic<int> g_executeHookCalls;
extern std::atomic<int> g_forceInteractWrites;
extern std::atomic<int> g_turretEntitiesFound;
extern std::atomic<int> g_turretModsApplied;
extern std::atomic<int> g_dbgHasWeaponHeat;
extern std::atomic<int> g_dbgHasStationaryAuto;
extern std::atomic<int> g_dbgHasRecoilLook;
extern std::atomic<int> g_dbgHasOverheated;

static void dbglog(const char* fmt, ...) {
    char tmp[256];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    ringlog::push("[d] %s", tmp);
}
static void tlog(const char* fmt, ...) {
    char tmp[256];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    ringlog::push("[t] %s", tmp);
}

static void save_settings() { /* stealth: no I/O from injected DLL */ }
#if 0
static void save_settings_disabled_() {
    FILE* f = nullptr;
    if (!f) return;
    fprintf(f, "esp3d=%d\n", g_esp3DEnabled.load() ? 1 : 0);
    fprintf(f, "espMaxDist=%.0f\n", g_espMaxDist);
    fprintf(f, "radar=%d\n", g_espEnabled.load() ? 1 : 0);
    fprintf(f, "radarRange=%.0f\n", g_radarRange);
    fprintf(f, "showSelf=%d\n", g_espShowSelf.load() ? 1 : 0);
    fprintf(f, "showPlayers=%d\n", g_espShowPlayers.load() ? 1 : 0);
    fprintf(f, "showMobs=%d\n", g_espShowMobs.load() ? 1 : 0);
    fprintf(f, "showWalkers=%d\n", g_espShowWalkers.load() ? 1 : 0);
    fprintf(f, "showItems=%d\n", g_espShowItems.load() ? 1 : 0);
    fprintf(f, "showSkeleton=%d\n", g_espShowSkeleton.load() ? 1 : 0);
    fprintf(f, "playerDist=%.0f\n", g_espPlayerDist);
    fprintf(f, "mobDist=%.0f\n", g_espMobDist);
    fprintf(f, "walkerDist=%.0f\n", g_espWalkerDist);
    fprintf(f, "itemDist=%.0f\n", g_espItemDist);
    fprintf(f, "showLootT1=%d\n", g_espShowLootT1.load() ? 1 : 0);
    fprintf(f, "showLootT2=%d\n", g_espShowLootT2.load() ? 1 : 0);
    fprintf(f, "showLootT3=%d\n", g_espShowLootT3.load() ? 1 : 0);
    fprintf(f, "lootT1Dist=%.0f\n", g_espLootT1Dist);
    fprintf(f, "lootT2Dist=%.0f\n", g_espLootT2Dist);
    fprintf(f, "lootT3Dist=%.0f\n", g_espLootT3Dist);
    fprintf(f, "rapidFire=%d\n", g_turretRapidFire.load() ? 1 : 0);
    fprintf(f, "noRecoil=%d\n", g_turretNoRecoil.load() ? 1 : 0);
    fprintf(f, "menuVisible=%d\n", g_menuVisible ? 1 : 0);
    fprintf(f, "aimbotEnabled=%d\n", g_aimbotEnabled.load() ? 1 : 0);
    fprintf(f, "aimbotFOV=%.0f\n", g_aimbotFOV);
    fprintf(f, "aimbotMaxDist=%.0f\n", g_aimbotMaxDist);
    fprintf(f, "aimbotDrawFOV=%d\n", g_aimbotDrawFOV.load() ? 1 : 0);
    fprintf(f, "aimbotTargetPlayers=%d\n", g_aimbotTargetPlayers.load() ? 1 : 0);
    fprintf(f, "aimbotTargetMobs=%d\n", g_aimbotTargetMobs.load() ? 1 : 0);
    fprintf(f, "aimbotActivationKey=%d\n", g_aimbotActivationKey);
    fprintf(f, "mobAimbotSame=%d\n", g_mobAimbotSame ? 1 : 0);
    auto saveProfile = [&](const char* prefix, const AimbotProfile& p) {
        fprintf(f, "%s.realityAim=%d\n", prefix, p.realityAim ? 1 : 0);
        fprintf(f, "%s.magnetism=%.2f\n", prefix, p.magnetism);
        fprintf(f, "%s.magnetismR=%d\n", prefix, p.magnetismRandomize ? 1 : 0);
        fprintf(f, "%s.magnetismRAmt=%.2f\n", prefix, p.magnetismRandomAmt);
        fprintf(f, "%s.boneHead=%.0f\n", prefix, p.boneWeightHead);
        fprintf(f, "%s.boneTorso=%.0f\n", prefix, p.boneWeightTorso);
        fprintf(f, "%s.boneR=%d\n", prefix, p.boneWeightRandomize ? 1 : 0);
        fprintf(f, "%s.boneRAmt=%.0f\n", prefix, p.boneWeightRandomAmt);
        fprintf(f, "%s.feather=%.0f\n", prefix, p.feather);
        fprintf(f, "%s.featherR=%d\n", prefix, p.featherRandomize ? 1 : 0);
        fprintf(f, "%s.featherRAmt=%.0f\n", prefix, p.featherRandomAmt);
        fprintf(f, "%s.prediction=%d\n", prefix, p.prediction ? 1 : 0);
        fprintf(f, "%s.bulletVelocity=%.1f\n", prefix, p.bulletVelocity);
        fprintf(f, "%s.closestBone=%d\n", prefix, p.closestBone ? 1 : 0);
        fprintf(f, "%s.cbStrength=%.2f\n", prefix, p.closestBoneStrength);
        fprintf(f, "%s.cbStrengthR=%d\n", prefix, p.closestBoneStrengthRandomize ? 1 : 0);
        fprintf(f, "%s.cbStrengthRAmt=%.2f\n", prefix, p.closestBoneStrengthRandomAmt);
        fprintf(f, "%s.centerPull=%.2f\n", prefix, p.centerPull);
        fprintf(f, "%s.centerPullR=%d\n", prefix, p.centerPullRandomize ? 1 : 0);
        fprintf(f, "%s.centerPullRAmt=%.2f\n", prefix, p.centerPullRandomAmt);
        fprintf(f, "%s.smooth=%.1f\n", prefix, p.smooth);
    };
    saveProfile("p", g_aimPlayer);
    saveProfile("m", g_aimMob);
    fprintf(f, "weaponModsEnabled=%d\n", g_weaponModsEnabled.load() ? 1 : 0);
    fprintf(f, "weaponNoDrop=%d\n", g_weaponNoDrop.load() ? 1 : 0);
    fprintf(f, "weaponNoBloom=%d\n", g_weaponNoBloom.load() ? 1 : 0);
    fprintf(f, "weaponVelocityMult=%.2f\n", g_weaponVelocityMult.load());
    fprintf(f, "streamProof=%d\n", g_streamProof.load() ? 1 : 0);
    fclose(f);
}

static void load_settings() {
    FILE* f = fopen(SETTINGS_PATH, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        float val;
        if (sscanf(line, "%63[^=]=%f", key, &val) == 2) {
            int iv = (int)val;
            if (strcmp(key, "esp3d") == 0) g_esp3DEnabled.store(iv != 0);
            else if (strcmp(key, "espMaxDist") == 0) g_espMaxDist = val;
            else if (strcmp(key, "radar") == 0) g_espEnabled.store(iv != 0);
            else if (strcmp(key, "radarRange") == 0) g_radarRange = val;
            else if (strcmp(key, "showSelf") == 0) g_espShowSelf.store(iv != 0);
            else if (strcmp(key, "showPlayers") == 0) g_espShowPlayers.store(iv != 0);
            else if (strcmp(key, "showMobs") == 0) g_espShowMobs.store(iv != 0);
            else if (strcmp(key, "showWalkers") == 0) g_espShowWalkers.store(iv != 0);
            else if (strcmp(key, "showItems") == 0) g_espShowItems.store(iv != 0);
            else if (strcmp(key, "showSkeleton") == 0) g_espShowSkeleton.store(iv != 0);
            else if (strcmp(key, "playerDist") == 0) g_espPlayerDist = val;
            else if (strcmp(key, "mobDist") == 0) g_espMobDist = val;
            else if (strcmp(key, "walkerDist") == 0) g_espWalkerDist = val;
            else if (strcmp(key, "itemDist") == 0) g_espItemDist = val;
            else if (strcmp(key, "showLootT1") == 0) g_espShowLootT1.store(iv != 0);
            else if (strcmp(key, "showLootT2") == 0) g_espShowLootT2.store(iv != 0);
            else if (strcmp(key, "showLootT3") == 0) g_espShowLootT3.store(iv != 0);
            else if (strcmp(key, "lootT1Dist") == 0) g_espLootT1Dist = val;
            else if (strcmp(key, "lootT2Dist") == 0) g_espLootT2Dist = val;
            else if (strcmp(key, "lootT3Dist") == 0) g_espLootT3Dist = val;
            else if (strcmp(key, "rapidFire") == 0) g_turretRapidFire.store(iv != 0);
            else if (strcmp(key, "noRecoil") == 0) g_turretNoRecoil.store(iv != 0);
            else if (strcmp(key, "menuVisible") == 0) g_menuVisible = (iv != 0);
            else if (strcmp(key, "aimbotEnabled") == 0) g_aimbotEnabled.store(iv != 0);
            else if (strcmp(key, "aimbotFOV") == 0) g_aimbotFOV = val;
            else if (strcmp(key, "aimbotMaxDist") == 0) g_aimbotMaxDist = val;
            else if (strcmp(key, "aimbotDrawFOV") == 0) g_aimbotDrawFOV.store(iv != 0);
            else if (strcmp(key, "aimbotTargetPlayers") == 0) g_aimbotTargetPlayers.store(iv != 0);
            else if (strcmp(key, "aimbotTargetMobs") == 0) g_aimbotTargetMobs.store(iv != 0);
            else if (strcmp(key, "aimbotActivationKey") == 0) g_aimbotActivationKey = iv;
            else if (strcmp(key, "mobAimbotSame") == 0) g_mobAimbotSame = (iv != 0);
            else if (strcmp(key, "weaponModsEnabled") == 0) g_weaponModsEnabled.store(iv != 0);
            else if (strcmp(key, "weaponNoDrop") == 0) g_weaponNoDrop.store(iv != 0);
            else if (strcmp(key, "weaponNoBloom") == 0) g_weaponNoBloom.store(iv != 0);
            else if (strcmp(key, "weaponVelocityMult") == 0) g_weaponVelocityMult.store(val);
            else if (strcmp(key, "streamProof") == 0) g_streamProof.store(iv != 0);
            else {
                AimbotProfile* prof = nullptr;
                const char* field = nullptr;
                if (strncmp(key, "p.", 2) == 0) { prof = &g_aimPlayer; field = key + 2; }
                else if (strncmp(key, "m.", 2) == 0) { prof = &g_aimMob; field = key + 2; }
                if (prof && field) {
                    if (strcmp(field, "realityAim") == 0) prof->realityAim = (iv != 0);
                    else if (strcmp(field, "magnetism") == 0) prof->magnetism = val;
                    else if (strcmp(field, "magnetismR") == 0) prof->magnetismRandomize = (iv != 0);
                    else if (strcmp(field, "magnetismRAmt") == 0) prof->magnetismRandomAmt = val;
                    else if (strcmp(field, "boneHead") == 0) prof->boneWeightHead = val;
                    else if (strcmp(field, "boneTorso") == 0) prof->boneWeightTorso = val;
                    else if (strcmp(field, "boneR") == 0) prof->boneWeightRandomize = (iv != 0);
                    else if (strcmp(field, "boneRAmt") == 0) prof->boneWeightRandomAmt = val;
                    else if (strcmp(field, "feather") == 0) prof->feather = val;
                    else if (strcmp(field, "featherR") == 0) prof->featherRandomize = (iv != 0);
                    else if (strcmp(field, "featherRAmt") == 0) prof->featherRandomAmt = val;
                    else if (strcmp(field, "prediction") == 0) prof->prediction = (iv != 0);
                    else if (strcmp(field, "bulletVelocity") == 0) prof->bulletVelocity = val;
                    else if (strcmp(field, "closestBone") == 0) prof->closestBone = (iv != 0);
                    else if (strcmp(field, "cbStrength") == 0) prof->closestBoneStrength = val;
                    else if (strcmp(field, "cbStrengthR") == 0) prof->closestBoneStrengthRandomize = (iv != 0);
                    else if (strcmp(field, "cbStrengthRAmt") == 0) prof->closestBoneStrengthRandomAmt = val;
                    else if (strcmp(field, "centerPull") == 0) prof->centerPull = val;
                    else if (strcmp(field, "centerPullR") == 0) prof->centerPullRandomize = (iv != 0);
                    else if (strcmp(field, "centerPullRAmt") == 0) prof->centerPullRandomAmt = val;
                    else if (strcmp(field, "smooth") == 0) prof->smooth = val;
                }
            }
        }
    }
    fclose(f);
}
#endif
static void load_settings() { /* stealth: no I/O from injected DLL */ }

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND g_gameHwnd = nullptr;
static WNDPROC g_originalWndProc = nullptr;
static bool g_imguiInitialized = false;
std::atomic<bool> g_streamProof{false};
volatile bool g_overlayDisabled = false;
static HWND g_overlayHwnd = nullptr;
static ID3D11Device* g_overlayDevice = nullptr;
static ID3D11DeviceContext* g_overlayContext = nullptr;
static IDXGISwapChain1* g_overlaySwapChain = nullptr;
static ID3D11RenderTargetView* g_overlayRTV = nullptr;
static bool g_overlayImguiInit = false;
// Set from UI, consumed at top of hooked_present before NewFrame. Toggling
// mid-frame tore down ImGui_ImplDX11 while the current draw was in flight and
// AV'd inside d3d11.dll on RenderDrawData.
static std::atomic<int> g_streamProofSwapRequest{0}; // 0=none, 1=enable, 2=disable
static IDCompositionDevice* g_dcompDevice = nullptr;
static IDCompositionTarget* g_dcompTarget = nullptr;
static IDCompositionVisual* g_dcompVisual = nullptr;
static IDXGISwapChain* g_initSwapChain = nullptr;

bool g_menuVisible = true;
std::atomic<bool> g_forceWindowed{false};

// Auto re-equip for dupe mode. Burst pattern: N quick scroll-bounces in a
// row (with intra-burst gap), then a longer pause between bursts. Mimics
// human input pattern better than pure interval spam.
std::atomic<bool> g_autoReequip{false};
int g_reequipKey1 = 0x31;   // '1' (legacy)
int g_reequipKey2 = 0x32;   // '2' (legacy)
int g_reequipIntervalMs = 500;   // (legacy — pause between rounds)
int g_reequipBurstCount = 2;     // how many rapid selects per round
int g_reequipBurstGapMs = 50;    // gap between the rapid selects
int g_reequipRoundPauseMs = 500; // gap between rounds

static void send_key(int vk, bool down) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = (WORD)vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

static void send_scroll(int wheelDelta) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = (DWORD)wheelDelta;  // WHEEL_DELTA = 120
    SendInput(1, &in, sizeof(in));
}

static void auto_reequip_tick() {
    // Throttled diagnostic — every ~2 sec log the gate state so LO can see
    // exactly what's blocking. Removed once feature is confirmed working.
    static ULONGLONG s_lastDiag = 0;
    ULONGLONG nowT = GetTickCount64();
    if (nowT - s_lastDiag > 2000) {
        s_lastDiag = nowT;
        bool ar = g_autoReequip.load();
        bool dm = g_dupeMode.load();
        bool su = g_dupeSuspended.load();
        bool fg = (GetForegroundWindow() == g_gameHwnd);
        ringlog::push("[reequip-diag] autoRe=%d dupe=%d suspend=%d foreground=%d hwnd=%p",
                      ar?1:0, dm?1:0, su?1:0, fg?1:0, g_gameHwnd);
    }
    if (!g_autoReequip.load() || !g_dupeMode.load()) return;
    // Suspend hotkey (F9) — temporarily pauses re-equip so LO can interact
    // with the world normally without losing the locked item. Toggle again
    // to resume.
    if (g_dupeSuspended.load()) return;
    // Only fire when the game window is foreground so we don't spam keys
    // into other apps if LO Alt-Tabs.
    if (GetForegroundWindow() != g_gameHwnd) return;
    // Burst pattern: fire N rapid selects with small gap between them,
    // then a longer pause before the next burst. Mimics human input
    // pattern better than pure interval spam.
    static ULONGLONG s_burstStart = 0;
    static ULONGLONG s_lastFire = 0;
    static int s_firedThisBurst = 0;
    ULONGLONG now = GetTickCount64();
    int burstCount = g_reequipBurstCount > 0 ? g_reequipBurstCount : 1;
    ULONGLONG burstGap = (ULONGLONG)(g_reequipBurstGapMs > 0 ? g_reequipBurstGapMs : 30);
    ULONGLONG roundPause = (ULONGLONG)(g_reequipRoundPauseMs > 0 ? g_reequipRoundPauseMs : 500);

    if (s_firedThisBurst >= burstCount) {
        // Waiting for next round
        if (now - s_burstStart < roundPause) return;
        s_burstStart = now;
        s_firedThisBurst = 0;
        s_lastFire = 0;
    }
    if (s_lastFire != 0 && now - s_lastFire < burstGap) return;
    if (s_firedThisBurst == 0) s_burstStart = now;

    // LO 2026-08-09: SendInput scroll and number keys don't work in this
    // game (radial TAB menu). Dispatch the equip message internally via
    // il2cpp_object_new + Publish — same path the game uses when you
    // radial-select a slot. Alternates between two configurable slots so
    // the equip fires as a swap (dupe race window).
    static int s_toggle = 0;
    int slotA = g_reequipSlotA.load();
    int slotB = g_reequipSlotB.load();
    dupelab_dispatch_equip_slot(s_toggle ? slotB : slotA);
    s_toggle ^= 1;
    s_firedThisBurst++;
    s_lastFire = now;
}


typedef HRESULT(STDMETHODCALLTYPE* fn_Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(STDMETHODCALLTYPE* fn_ResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

static fn_Present g_originalPresent = nullptr;
static fn_ResizeBuffers g_originalResize = nullptr;
static void** g_vtable = nullptr;
static void* g_vtableOrigPresent = nullptr;
static void* g_vtableOrigResize = nullptr;
static void* g_relayPage = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void create_render_target(IDXGISwapChain* pSwapChain) {
    if (!g_pd3dDevice) return;
    ID3D11Texture2D* pBackBuffer = nullptr;
    pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void release_render_target() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

static HRESULT STDMETHODCALLTYPE hooked_present(IDXGISwapChain*, UINT, UINT);
static HRESULT STDMETHODCALLTYPE hooked_resize_buffers(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// SetFullscreenState vtable hook — when g_forceWindowed is on, we return
// S_OK without actually going fullscreen. Game thinks its call succeeded
// and stops re-trying, we stay windowed. Vtable slot 10 on IDXGISwapChain.
typedef HRESULT (STDMETHODCALLTYPE* fn_SetFullscreenState)(
    IDXGISwapChain* pSwapChain, BOOL Fullscreen, IDXGIOutput* pTarget);
static fn_SetFullscreenState g_originalSetFullscreenState = nullptr;
static void** g_swapChainVtable = nullptr;
static uintptr_t g_setFullscreenSlot_oldProt = 0;

static HRESULT STDMETHODCALLTYPE hooked_set_fullscreen_state(
    IDXGISwapChain* pSwapChain, BOOL Fullscreen, IDXGIOutput* pTarget)
{
    if (g_forceWindowed.load() && Fullscreen == TRUE) {
        // Silently pretend it worked. Game moves on, stays windowed.
        return S_OK;
    }
    if (g_originalSetFullscreenState) {
        return g_originalSetFullscreenState(pSwapChain, Fullscreen, pTarget);
    }
    return E_FAIL;
}

static void install_fullscreen_hook(IDXGISwapChain* pSwapChain) {
    if (!pSwapChain || g_originalSetFullscreenState) return;  // already hooked
    void** vtbl = *(void***)pSwapChain;
    if (!vtbl) return;
    // IDXGISwapChain vtable layout:
    //   [0-2]  IUnknown  (QueryInterface, AddRef, Release)
    //   [3-6]  IDXGIObject (SetPrivateData, ...)
    //   [7]    GetDevice
    //   [8]    Present
    //   [9]    GetBuffer
    //   [10]   SetFullscreenState  <-- HERE
    //   [11]   GetFullscreenState
    g_swapChainVtable = vtbl;
    g_originalSetFullscreenState = (fn_SetFullscreenState)vtbl[10];
    DWORD oldProt;
    if (VirtualProtect(&vtbl[10], sizeof(void*), PAGE_READWRITE, &oldProt)) {
        vtbl[10] = (void*)&hooked_set_fullscreen_state;
        DWORD tmp;
        VirtualProtect(&vtbl[10], sizeof(void*), oldProt, &tmp);
        g_setFullscreenSlot_oldProt = oldProt;
        dbglog("[fullscreen-hook] installed — original=%p\n", g_originalSetFullscreenState);
    } else {
        g_originalSetFullscreenState = nullptr;
        dbglog("[fullscreen-hook] VirtualProtect FAILED\n");
    }
}

static LRESULT WINAPI hooked_wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_INSERT)
        g_menuVisible = !g_menuVisible;

    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam) && g_menuVisible)
        return 0;

    return CallWindowProcW(g_originalWndProc, hWnd, msg, wParam, lParam);
}

static bool seh_get_camera_yaw(float* outCos, float* outSin) {
    __try {
        void* cam = g_cameraGetMain(nullptr);
        if (!cam) return false;
        void* tf = g_getTransform(cam, nullptr);
        if (!tf) return false;
        Vec3 fwd;
        g_getForward(&fwd, tf, nullptr);
        float yaw = atan2f(fwd.x, fwd.z);
        *outCos = cosf(-yaw);
        *outSin = sinf(-yaw);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { dbglog("[seh_get_camera_yaw] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

struct BoneScreenPos {
    float x, y;
    bool valid;
};

struct ESP3DEntry {
    float sx, sy, headSX, headSY;
    char label[96];
    int type;
    int lootTier;
    float dist;
    bool hasHead;
    bool hasSkeleton;
    bool isExtraction;
    bool isReactor;
    bool isFinalExtract;
    BoneScreenPos bones[55];
    float healthNorm;   // 0..1 or -1 if unknown
    bool  isAlly;
    bool  hasCustomColor;
    ImU32 customColor;
    bool  isSentinel;
    Vec3  sentinelWorld;   // world pos for in-world ring
    DWORD posTimestamp;    // when the position was sampled (velocity extrapolation)
    float velX, velY, velZ;
};

// Canonical forum BoneId (0..21) → our internal Unity HumanBodyBones slot (0..54).
// Our e.bones[] uses Unity HumanBodyBones layout (Head=10, UpperChest=54, etc).
static const int kBoneIdToSlot[22] = {
    10,  9,  8, 54,  7,  0,   // Head, Neck, Chest, Spine2(UpperChest), Spine1(Spine), Hips
    11, 13, 15, 17,           // L_Clavicle, L_Shoulder, L_Elbow, L_Hand
    12, 14, 16, 18,           // R_Clavicle, R_Shoulder, R_Elbow, R_Hand
     1,  3,  5, 19,           // L_Femur, L_Knee, L_Ankle, L_Toe
     2,  4,  6, 20            // R_Femur, R_Knee, R_Ankle, R_Toe
};
static const char* kBoneIdNames[22] = {
    "Head","Neck","Chest","Spine2","Spine1","Hips",
    "L Clavicle","L Shoulder","L Elbow","L Hand",
    "R Clavicle","R Shoulder","R Elbow","R Hand",
    "L Femur","L Knee","L Ankle","L Toe",
    "R Femur","R Knee","R Ankle","R Toe"
};

static const int SKELETON_CONNECTIONS[][2] = {
    {10, 9}, {9, 54}, {54, 8}, {8, 7}, {7, 0},
    {54, 11}, {11, 13}, {13, 15}, {15, 17},
    {54, 12}, {12, 14}, {14, 16}, {16, 18},
    {0, 1}, {1, 3}, {3, 5},
    {0, 2}, {2, 4}, {4, 6},
};
static const int SKELETON_CONNECTION_COUNT = sizeof(SKELETON_CONNECTIONS) / sizeof(SKELETON_CONNECTIONS[0]);

struct ESPSnapshot {
    WorldVector pos;
    Vec3 transformWorldPos;
    float distance;
    float velX, velY, velZ;
    DWORD lastPosTime;
    char displayName[96];
    BoneWorldPos bonePositions[55];
    bool hasTransformPos;
    bool hasBones;
    bool isCreature;
    bool isExtraction;
    bool isReactor;
    bool isFinalExtract;
    int type;
    int lootTier;
    float healthNorm;   // 0..1 or -1 if unknown
    bool  isAlly;
    bool  hasCustomColor;
    ImU32 customColor;
    bool  isSentinel;
    WorldVector sentinelWorldVec;   // for in-world circle projection
};
static void seh_project_entities_impl(std::vector<ESP3DEntry>& out, float maxDist, volatile bool& csHeld) {
    if (!g_cameraGetMain || !g_cameraW2S || !g_getTransform || !g_getPosition) return;

    void* camera = g_cameraGetMain(nullptr);
    if (!camera) return;
    void* camTf = g_getTransform(camera, nullptr);
    if (!camTf) return;
    Vec3 camPos;
    g_getPosition(&camPos, camTf, nullptr);

    Vec3 refPos = camPos;
    if (g_getParent) {
        void* parentTf = g_getParent(camTf, nullptr);
        if (parentTf) {
            g_getPosition(&refPos, parentTf, nullptr);
        }
    }

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    static std::vector<ESPSnapshot> snapshots;
    snapshots.clear();

    float playerAbsX, playerAbsY, playerAbsZ;
    bool aimbotActive = g_aimbotEnabled.load();
    bool showSkeleton = g_espShowSkeleton.load();

    EnterCriticalSection(&g_itemsLock);
    csHeld = true;

    playerAbsX = g_playerPos.cx * CHUNK_SIZE + g_playerPos.x;
    playerAbsY = g_playerPos.y;
    playerAbsZ = g_playerPos.cy * CHUNK_SIZE + g_playerPos.z;

    for (auto& item : g_items) {
        // MUTUALLY EXCLUSIVE — PlayerAvatar never counted as mob/walker even
        // if it carries AiAgentData or similar mob-signaling components. This
        // was the cause of "players not showing" after switching isCreature
        // to component-based detection: an AiAgent-carrying PlayerAvatar was
        // isPlayer=T AND isMob=T, then hidden by the isMob filter when
        // ShowMobs was off (even with ShowPlayers on).
        bool isPlayer = (item.name.rfind("PlayerAvatar", 0) == 0);
        bool isWalker = !isPlayer && (item.name.rfind("EXPEDITION_WALKER", 0) == 0);
        bool isMob    = !isPlayer && !isWalker && item.isCreature;
        bool isItem   = (!isPlayer && !isMob && !isWalker);
        bool isLoot = (item.lootTier > 0);

        if (item.isSentinel) {
            // Sentinels bypass the isItem filter — they get their own toggle.
            if (!g_espShowSentinels.load()) continue;
        } else {
            if (isPlayer && !g_espShowPlayers.load()) continue;
            if (isMob && !g_espShowMobs.load()) continue;
            if (isWalker && !g_espShowWalkers.load()) continue;
        }
        // Hide walker_* child entities (legs, compartments, engines, etc)
        // that belong to YOUR OWN trampler. Enemy walker_* still shows.
        if (g_espHideOwnWalkerParts.load() && item.isAlly
            && item.name.rfind("walker_", 0) == 0) continue;
        // Case-insensitive filter check. Lowercase both sides so user can
        // add "mob" and it matches "MobGhoul", "mob_upior", etc.
        {
            std::string _lname = item.name;
            for (auto& c : _lname) if (c >= 'A' && c <= 'Z') c = c + 32;
            bool hidden = false;
            for (auto& n : g_hiddenNames) {
                std::string _ln = n;
                for (auto& c : _ln) if (c >= 'A' && c <= 'Z') c = c + 32;
                if (_ln == _lname) { hidden = true; break; }
            }
            if (!hidden) {
                for (auto& p : g_hiddenPrefixes) {
                    std::string _lp = p;
                    for (auto& c : _lp) if (c >= 'A' && c <= 'Z') c = c + 32;
                    if (_lname.rfind(_lp, 0) == 0) { hidden = true; break; }
                }
            }
            if (hidden) continue;
        }
        if (isLoot) {
            if (item.lootTier == 1 && !g_espShowLootT1.load()) continue;
            if (item.lootTier == 2 && !g_espShowLootT2.load()) continue;
            if (item.lootTier == 3 && !g_espShowLootT3.load()) continue;
        } else if (isItem && !item.isSentinel && !g_espShowItems.load()) continue;

        float catDist;
        if (isPlayer) catDist = g_espPlayerDist;
        else if (isMob) catDist = g_espMobDist;
        else if (isWalker) catDist = g_espWalkerDist;
        else if (isLoot) {
            if (item.lootTier == 3) catDist = g_espLootT3Dist;
            else if (item.lootTier == 2) catDist = g_espLootT2Dist;
            else catDist = g_espLootT1Dist;
        } else catDist = g_espItemDist;
        if (item.distance >= 0 && item.distance > catDist) continue;

        ESPSnapshot snap;
        snap.pos = item.pos;
        snap.transformWorldPos = item.transformWorldPos;
        snap.hasTransformPos = item.hasTransformPos;
        snap.distance = item.distance;
        snap.velX = item.velX;
        snap.velY = item.velY;
        snap.velZ = item.velZ;
        snap.lastPosTime = item.lastPosTime;
        snap.isCreature = item.isCreature;
        snap.isExtraction = item.isExtraction;
        snap.isReactor = item.isReactor;
        snap.isFinalExtract = item.isFinalExtract;
        snap.healthNorm = item.healthNorm;
        snap.isAlly = item.isAlly;
        snap.hasCustomColor = false;
        auto ccit = g_customEspColors.find(item.name);
        if (ccit != g_customEspColors.end()) {
            snap.hasCustomColor = true;
            snap.customColor = (ImU32)ccit->second;
        }
        snap.isSentinel = item.isSentinel;
        snap.sentinelWorldVec = item.pos;
        // Extraction / Reactor filters — hide if their toggle is off, but
        // let them always render if the toggle is on regardless of category.
        if (item.isExtraction && !g_espShowExtraction.load() && !isPlayer && !isMob) continue;
        if (item.isReactor && !g_espShowReactors.load() && !isPlayer && !isMob && !isWalker) continue;
        snap.lootTier = item.lootTier;
        snap.type = isWalker ? 2 : (isPlayer ? 0 : (isMob ? 1 : 3));

        const char* dn = item.displayName.empty() ? item.name.c_str() : item.displayName.c_str();
        if (snap.type == 0 && item.displayName.empty()) dn = "PLAYER";
        else if (snap.type == 2 && item.displayName.empty()) dn = "WALKER";
        snprintf(snap.displayName, sizeof(snap.displayName), "%s", dn);

        snap.hasBones = false;
        if (!isWalker && !isItem && showSkeleton && item.hasBones) {
            memcpy(snap.bonePositions, item.bonePositions, sizeof(snap.bonePositions));
            snap.hasBones = true;
        }

        snapshots.push_back(snap);
    }

    LeaveCriticalSection(&g_itemsLock);
    csHeld = false;

    DWORD nowTickMs = GetTickCount();
    for (auto& snap : snapshots) {
        Vec3 worldPos;
        if (snap.hasTransformPos) {
            worldPos = snap.transformWorldPos;
        } else {
            float entityAbsX = snap.pos.cx * CHUNK_SIZE + snap.pos.x;
            float entityAbsY = snap.pos.y;
            float entityAbsZ = snap.pos.cy * CHUNK_SIZE + snap.pos.z;
            worldPos.x = refPos.x + (entityAbsX - playerAbsX);
            worldPos.y = refPos.y + (entityAbsY - playerAbsY);
            worldPos.z = refPos.z + (entityAbsZ - playerAbsZ);
        }

        // Velocity extrapolation — labels track the entity between scans
        // instead of teleporting in scan-interval jumps. Render thread runs
        // 60+ Hz; scan runs at whatever speed the worker manages. Between
        // scans, project where the entity should BE now given its last known
        // velocity. Capped at 500ms extrapolation to avoid wild predictions
        // when scan stalls or entity abruptly stops.
        if (snap.lastPosTime != 0) {
            DWORD ageMs = nowTickMs - snap.lastPosTime;
            if (ageMs > 500) ageMs = 500;
            float ageSec = ageMs / 1000.0f;
            worldPos.x += snap.velX * ageSec;
            worldPos.y += snap.velY * ageSec;
            worldPos.z += snap.velZ * ageSec;
        }

        if (aimbotActive) {
            bool isMob = (snap.type == 1);
            const AimbotProfile& pp = (isMob && !g_mobAimbotSame) ? g_aimMob : g_aimPlayer;
            if (pp.prediction && pp.bulletVelocity > 1.0f && snap.distance > 0.1f) {
                // Bullet lead on top of the base extrapolation — total offset
                // is (age-since-scan + bullet-travel-time) * velocity.
                float predTime = snap.distance / pp.bulletVelocity;
                worldPos.x += snap.velX * predTime;
                worldPos.y += snap.velY * predTime;
                worldPos.z += snap.velZ * predTime;
            }
        }

        Vec3 screenPos;
        g_cameraW2S(&screenPos, camera, &worldPos, nullptr);
        if (screenPos.z <= 0) continue;
        if (std::isnan(screenPos.x) || std::isnan(screenPos.y)) continue;

        float sx = screenPos.x;
        float sy = displaySize.y - screenPos.y;
        if (sx < -200 || sx > displaySize.x + 200) continue;
        if (sy < -200 || sy > displaySize.y + 200) continue;

        ESP3DEntry e;
        e.sx = sx; e.sy = sy;
        e.hasHead = false;
        e.hasSkeleton = false;

        if (snap.type != 2 && snap.type != 3) {
            Vec3 headWorldPos = worldPos;
            headWorldPos.y += 1.8f;
            Vec3 headScreenPos;
            g_cameraW2S(&headScreenPos, camera, &headWorldPos, nullptr);
            if (headScreenPos.z > 0 && !std::isnan(headScreenPos.x) && !std::isnan(headScreenPos.y)) {
                e.headSX = headScreenPos.x;
                e.headSY = displaySize.y - headScreenPos.y;
                e.hasHead = true;
            }
        }

        e.type = snap.type;
        e.lootTier = snap.lootTier;
        e.dist = snap.distance >= 0 ? snap.distance : 0.0f;
        e.isExtraction = snap.isExtraction;
        e.isReactor = snap.isReactor;
        e.isFinalExtract = snap.isFinalExtract;
        e.healthNorm = snap.healthNorm;
        e.isAlly = snap.isAlly;
        e.hasCustomColor = snap.hasCustomColor;
        e.customColor = snap.customColor;
        e.isSentinel = snap.isSentinel;
        e.posTimestamp = snap.lastPosTime;
        e.velX = snap.velX; e.velY = snap.velY; e.velZ = snap.velZ;
        // Extract world XYZ (chunk-adjusted) for in-world ring drawing.
        e.sentinelWorld.x = snap.sentinelWorldVec.cx * CHUNK_SIZE + snap.sentinelWorldVec.x;
        e.sentinelWorld.y = snap.sentinelWorldVec.y;
        e.sentinelWorld.z = snap.sentinelWorldVec.cy * CHUNK_SIZE + snap.sentinelWorldVec.z;
        const char* prefix = "";
        if (snap.isFinalExtract) prefix = "[FINAL EXTRACT] ";
        else if (snap.isExtraction) prefix = "[EXTRACT] ";
        else if (snap.isReactor) prefix = snap.isAlly ? "[OUR REACTOR] " : "[REACTOR] ";
        char hpTag[24] = "";
        if (g_espShowHealth.load() && e.healthNorm >= 0.0f && e.healthNorm <= 1.5f) {
            int pct = (int)(e.healthNorm * 100.0f + 0.5f);
            snprintf(hpTag, sizeof(hpTag), " [HP %d%%]", pct);
        }
        // Distance is optional per LO — toggle in ESP Render style.
        if (g_espShowDistance.load()) {
            snprintf(e.label, sizeof(e.label), "%s%s [%.0fm]%s", prefix, snap.displayName, e.dist, hpTag);
        } else {
            snprintf(e.label, sizeof(e.label), "%s%s%s", prefix, snap.displayName, hpTag);
        }

        e.hasSkeleton = false;
        if (snap.type != 2 && snap.type != 3 && showSkeleton && snap.hasBones) {
            // Project EVERY valid bone slot 0-54, not just the 20 named
            // indices — otherwise raw-fallback point-cloud mode is invisible
            // because slots 19-53 never get screen-projected. Named-match
            // fills specific indices; raw fills sequentially. Cover both.
            bool anyBone = false;
            int projectedCount = 0;
            for (int bi = 0; bi < 55; bi++) {
                e.bones[bi].valid = false;
                if (!snap.bonePositions[bi].valid) continue;
                Vec3 boneWorldPos = snap.bonePositions[bi].pos;
                Vec3 boneScreen;
                g_cameraW2S(&boneScreen, camera, &boneWorldPos, nullptr);
                if (boneScreen.z > 0 && !std::isnan(boneScreen.x) && !std::isnan(boneScreen.y)) {
                    e.bones[bi].x = boneScreen.x;
                    e.bones[bi].y = displaySize.y - boneScreen.y;
                    e.bones[bi].valid = true;
                    anyBone = true;
                    projectedCount++;
                }
            }
            e.hasSkeleton = anyBone;
            if (anyBone && e.bones[10].valid) {
                e.headSX = e.bones[10].x;
                e.headSY = e.bones[10].y;
                e.hasHead = true;
            }
            // Throttled diag — every 2s log projection stats so LO can see
            // whether bones are being lost at world->screen or actually drawn.
            static ULONGLONG s_boneProjLog = 0;
            ULONGLONG nt = GetTickCount64();
            if (nt - s_boneProjLog > 2000) {
                s_boneProjLog = nt;
                int inSlots = 0;
                for (int b = 0; b < 55; b++) if (snap.bonePositions[b].valid) inSlots++;
                ringlog::push("[bone-render] type=%d snap=%d/55 projected=%d/55 hasSkeleton=%d",
                              snap.type, inSlots, projectedCount, anyBone?1:0);
            }
        }

        out.push_back(e);
    }
}

static void seh_project_entities(std::vector<ESP3DEntry>& out, float maxDist) {
    volatile bool csHeld = false;
    __try {
        seh_project_entities_impl(out, maxDist, csHeld);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (csHeld) LeaveCriticalSection(&g_itemsLock);
        dbglog("[seh_project] SEH exception: 0x%08lX\n", GetExceptionCode());
    }
}

static float rand_offset(float& offset, DWORD& lastT, DWORD now, DWORD interval, float amt, int seed) {
    if (now - lastT > interval) {
        offset = ((float)((now + seed) % 997) / 997.0f - 0.5f) * 2.0f * amt;
        lastT = now;
    }
    return offset;
}

static void apply_aimbot(const std::vector<ESP3DEntry>& entries) {
    if (!g_aimbotEnabled.load() || !g_aimbotActive.load()) return;
    __try {

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    float centerX = displaySize.x * 0.5f;
    float centerY = displaySize.y * 0.5f;
    float fovRadius = g_aimbotFOV;
    float maxDist = g_aimbotMaxDist;
    bool targetPlayers = g_aimbotTargetPlayers.load();
    bool targetMobs = g_aimbotTargetMobs.load();

    float bestScreenDist = fovRadius;
    const ESP3DEntry* bestTarget = nullptr;

    // Reactor priority pass — if enabled and an enemy reactor is in FOV,
    // it wins over players/mobs.
    bool reactorPri = g_aimbotReactorPriority.load();
    bool targetReactors = g_aimbotTargetReactors.load();
    if (reactorPri || targetReactors) {
        float bestR = fovRadius;
        const ESP3DEntry* bestReactor = nullptr;
        for (auto& e : entries) {
            if (!e.isReactor || e.isAlly) continue;
            if (e.dist > maxDist) continue;
            float dx = e.sx - centerX, dy = e.sy - centerY;
            float sd = sqrtf(dx*dx + dy*dy);
            if (sd < bestR) { bestR = sd; bestReactor = &e; }
        }
        if (bestReactor) { bestTarget = bestReactor; bestScreenDist = bestR; }
    }
    if (!bestTarget) {
        for (auto& e : entries) {
            if (e.type == 0 && !targetPlayers) continue;
            if (e.type == 1 && !targetMobs) continue;
            if (e.type == 2) continue;
            if (e.dist > maxDist) continue;
            float dx = e.sx - centerX;
            float dy = e.sy - centerY;
            float screenDist = sqrtf(dx * dx + dy * dy);
            if (screenDist < bestScreenDist) {
                bestScreenDist = screenDist;
                bestTarget = &e;
            }
        }
    }
    if (!bestTarget) return;

    AimbotProfile& prof = (bestTarget->type == 1 && !g_mobAimbotSame) ? g_aimMob : g_aimPlayer;

    float torsoSX = bestTarget->sx;
    float torsoSY = bestTarget->sy;
    // Per-bone selection: BoneId (0..21) → internal Unity slot via kBoneIdToSlot.
    // The selected bone replaces "head" attractor in reality-aim/weights so the
    // rest of the pipeline (feather, center-pull, closest-bone) keeps working.
    int _bid = (prof.targetBoneId >= 0 && prof.targetBoneId < 22) ? prof.targetBoneId : 0;
    int _slot = kBoneIdToSlot[_bid];
    bool _boneOk = (_slot >= 0 && _slot < 55 && bestTarget->bones[_slot].valid);
    float headSX, headSY;
    if (_boneOk) {
        headSX = bestTarget->bones[_slot].x;
        headSY = bestTarget->bones[_slot].y;
    } else {
        headSX = bestTarget->hasHead ? bestTarget->headSX : torsoSX;
        headSY = bestTarget->hasHead ? bestTarget->headSY : (torsoSY - 30.0f);
    }

    DWORD now = GetTickCount();

    float magnetism = prof.magnetism;
    if (prof.magnetismRandomize) {
        magnetism += rand_offset(prof.rt_magOff, prof.rt_magT, now, 600, prof.magnetismRandomAmt, 0);
        if (magnetism < 0.05f) magnetism = 0.05f;
        if (magnetism > 1.0f) magnetism = 1.0f;
    }

    float headW = prof.boneWeightHead;
    float torsoW = prof.boneWeightTorso;
    if (prof.boneWeightRandomize && !prof.closestBone) {
        float bwOff = rand_offset(prof.rt_bwOff, prof.rt_bwT, now, 900, prof.boneWeightRandomAmt, 100);
        headW += bwOff;
        torsoW -= bwOff;
        if (headW < 0) headW = 0;
        if (torsoW < 0) torsoW = 0;
    }
    headW /= 100.0f; torsoW /= 100.0f;
    float wSum = headW + torsoW;
    if (wSum > 0.001f) { headW /= wSum; torsoW /= wSum; }
    else { headW = 0.5f; torsoW = 0.5f; }

    float centerPull = prof.centerPull;
    if (prof.centerPullRandomize) {
        centerPull += rand_offset(prof.rt_cpOff, prof.rt_cpT, now, 850, prof.centerPullRandomAmt, 200);
        if (centerPull < 0) centerPull = 0;
        if (centerPull > 1.0f) centerPull = 1.0f;
    }

    float cbStrength = prof.closestBoneStrength;
    if (prof.closestBoneStrengthRandomize) {
        cbStrength += rand_offset(prof.rt_cbsOff, prof.rt_cbsT, now, 750, prof.closestBoneStrengthRandomAmt, 300);
        if (cbStrength < 0) cbStrength = 0;
        if (cbStrength > 1.0f) cbStrength = 1.0f;
    }

    if (prof.realityAim) {
        float boneSepY = fabsf(headSY - torsoSY);
        float boneSepX = fabsf(headSX - torsoSX);
        float boneSep = sqrtf(boneSepX * boneSepX + boneSepY * boneSepY);
        if (boneSep < 15.0f) boneSep = 15.0f;

        float headRadius = boneSep * 0.45f;
        float torsoRadius = boneSep * 0.55f;

        float featherPct = prof.feather / 100.0f;
        if (prof.featherRandomize) {
            featherPct += rand_offset(prof.rt_featherOff, prof.rt_featherT, now, 700, prof.featherRandomAmt / 100.0f, 400);
            if (featherPct < 0.0f) featherPct = 0.0f;
        }
        headRadius *= (1.0f + featherPct);
        torsoRadius *= (1.0f + featherPct);

        float dxHead = centerX - headSX;
        float dyHead = centerY - headSY;
        float distToHead = sqrtf(dxHead * dxHead + dyHead * dyHead);

        float dxTorso = centerX - torsoSX;
        float dyTorso = centerY - torsoSY;
        float distToTorso = sqrtf(dxTorso * dxTorso + dyTorso * dyTorso);

        bool inHeadZone = distToHead < headRadius;
        bool inTorsoZone = distToTorso < torsoRadius;
        bool inAnyZone = inHeadZone || inTorsoZone;

        if (prof.closestBone) {
            if (prof.rt_currentBone == -1) {
                prof.rt_currentBone = (distToHead <= distToTorso) ? 0 : 1;
            } else {
                float currentDist = (prof.rt_currentBone == 0) ? distToHead : distToTorso;
                float otherDist = (prof.rt_currentBone == 0) ? distToTorso : distToHead;
                float threshold = currentDist * (1.0f - cbStrength * 0.7f);
                if (otherDist < threshold) {
                    prof.rt_currentBone = 1 - prof.rt_currentBone;
                }
            }
        }

        float moveX = 0.0f, moveY = 0.0f;

        if (inAnyZone) {
            if (prof.closestBone) {
                float boneCX = (prof.rt_currentBone == 0) ? headSX : torsoSX;
                float boneCY = (prof.rt_currentBone == 0) ? headSY : torsoSY;
                if (centerPull > 0.01f) {
                    moveX = (boneCX - centerX) * centerPull * 0.015f;
                    moveY = (boneCY - centerY) * centerPull * 0.015f;
                }
            } else {
                float imbalance = fabsf(headW - torsoW);
                float wcx = headSX * headW + torsoSX * torsoW;
                float wcy = headSY * headW + torsoSY * torsoW;

                if (imbalance > 0.01f) {
                    float driftX = wcx - centerX;
                    float driftY = wcy - centerY;
                    moveX = driftX * imbalance * 0.008f;
                    moveY = driftY * imbalance * 0.008f;
                }

                if (centerPull > 0.01f) {
                    float boneCX, boneCY;
                    if (inHeadZone && (!inTorsoZone || distToHead < distToTorso)) {
                        boneCX = headSX; boneCY = headSY;
                    } else {
                        boneCX = torsoSX; boneCY = torsoSY;
                    }
                    moveX += (boneCX - centerX) * centerPull * 0.015f;
                    moveY += (boneCY - centerY) * centerPull * 0.015f;
                }
            }
        } else {
            if (prof.closestBone) {
                float boneCX = (prof.rt_currentBone == 0) ? headSX : torsoSX;
                float boneCY = (prof.rt_currentBone == 0) ? headSY : torsoSY;
                float boneR = (prof.rt_currentBone == 0) ? headRadius : torsoRadius;
                float dxB = centerX - boneCX;
                float dyB = centerY - boneCY;
                float distB = sqrtf(dxB * dxB + dyB * dyB);
                if (distB > 0.01f) {
                    float angle = atan2f(dyB, dxB);
                    float edgeX = boneCX + cosf(angle) * boneR;
                    float edgeY = boneCY + sinf(angle) * boneR;
                    moveX = (edgeX - centerX) * magnetism * 0.35f;
                    moveY = (edgeY - centerY) * magnetism * 0.35f;
                }
            } else {
                float wcx = headSX * headW + torsoSX * torsoW;
                float wcy = headSY * headW + torsoSY * torsoW;

                float nearestX, nearestY;
                if (distToHead * (1.0f + torsoW * 0.5f) < distToTorso * (1.0f + headW * 0.5f)) {
                    float angle = atan2f(dyHead, dxHead);
                    nearestX = headSX + cosf(angle) * headRadius;
                    nearestY = headSY + sinf(angle) * headRadius;
                } else {
                    float angle = atan2f(dyTorso, dxTorso);
                    nearestX = torsoSX + cosf(angle) * torsoRadius;
                    nearestY = torsoSY + sinf(angle) * torsoRadius;
                }

                moveX = (nearestX - centerX) * magnetism * 0.35f;
                moveY = (nearestY - centerY) * magnetism * 0.35f;
                moveX += (wcx - nearestX) * magnetism * 0.05f;
                moveY += (wcy - nearestY) * magnetism * 0.05f;
            }
        }

        int mx = (int)moveX;
        int my = (int)moveY;
        if (mx == 0 && my == 0 && (fabsf(moveX) > 0.3f || fabsf(moveY) > 0.3f)) {
            if (fabsf(moveX) > 0.3f) mx = (moveX > 0) ? 1 : -1;
            if (fabsf(moveY) > 0.3f) my = (moveY > 0) ? 1 : -1;
        }
        if (mx != 0 || my != 0) {
            mouse_event(MOUSEEVENTF_MOVE, mx, my, 0, 0);
        }
    } else {
        float targetX = headSX * headW + torsoSX * torsoW;
        float targetY = headSY * headW + torsoSY * torsoW;
        float deltaX = targetX - centerX;
        float deltaY = targetY - centerY;
        float factor = 1.0f / prof.smooth;
        int moveX = (int)(deltaX * factor);
        int moveY = (int)(deltaY * factor);
        if (moveX == 0 && moveY == 0 && (fabsf(deltaX) > 0.5f || fabsf(deltaY) > 0.5f)) {
            moveX = (deltaX > 0) ? 1 : -1;
            moveY = (deltaY > 0) ? 1 : -1;
        }
        if (moveX != 0 || moveY != 0) {
            mouse_event(MOUSEEVENTF_MOVE, moveX, moveY, 0, 0);
        }
    }

    } __except (EXCEPTION_EXECUTE_HANDLER) { dbglog("[apply_aimbot] SEH: 0x%08lX\n", GetExceptionCode()); }
}

static void render_aimbot_profile(AimbotProfile& p, const char* suffix) {
    ImGui::PushID(suffix);

    {
        if (ImGui::Checkbox("Reality Aim", &p.realityAim))
            ;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), p.realityAim ? "(zone-based)" : "(traditional)");
    }
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Aim Target Bone");
    ImGui::PushItemWidth(140);
    ImGui::Combo("##tbone", &p.targetBoneId, kBoneIdNames, 22);
    ImGui::PopItemWidth();
    ImGui::TextDisabled("Selected bone replaces head attractor. Missing bone → torso fallback.");

    ImGui::Separator();
    if (p.realityAim) {
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Magnetism");
        ImGui::SliderFloat("Strength", &p.magnetism, 0.05f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::Checkbox("R##mag", &p.magnetismRandomize);
        if (p.magnetismRandomize) {
            ImGui::SliderFloat("+/- Mag", &p.magnetismRandomAmt, 0.01f, 0.3f, "%.2f");
        }
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Behavior");
        ImGui::SliderFloat("Smoothing", &p.smooth, 1.0f, 20.0f, "%.1f");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "(1=snap, 20=gentle)");
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Targeting Mode");
    ImGui::Checkbox("Closest Bone", &p.closestBone);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), p.closestBone ? "(auto-target nearest)" : "(weighted distribution)");

    if (p.closestBone) {
        ImGui::SliderFloat("Bone Stickiness", &p.closestBoneStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::Checkbox("R##cbs", &p.closestBoneStrengthRandomize);
        if (p.closestBoneStrengthRandomize) {
            ImGui::SliderFloat("+/- Stick", &p.closestBoneStrengthRandomAmt, 0.01f, 0.3f, "%.2f");
        }
    } else {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Bone Weights");
        ImGui::SliderFloat("Head", &p.boneWeightHead, 0.0f, 100.0f, "%.0f%%");
        ImGui::SliderFloat("Torso", &p.boneWeightTorso, 0.0f, 100.0f, "%.0f%%");

        float totalWeight = p.boneWeightHead + p.boneWeightTorso;
        if (totalWeight > 0.01f) {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "Effective: Head %.0f%% / Torso %.0f%%",
                (p.boneWeightHead / totalWeight) * 100.0f,
                (p.boneWeightTorso / totalWeight) * 100.0f);
        }

        ImGui::Checkbox("R##bw", &p.boneWeightRandomize);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "Randomize Weights");
        if (p.boneWeightRandomize) {
            ImGui::SliderFloat("+/- Weight", &p.boneWeightRandomAmt, 1.0f, 25.0f, "%.0f%%");
        }

        if (ImGui::Button("Neutral")) { p.boneWeightHead = 50; p.boneWeightTorso = 50; }
        ImGui::SameLine();
        if (ImGui::Button("Headhunter")) { p.boneWeightHead = 90; p.boneWeightTorso = 10; }
        ImGui::SameLine();
        if (ImGui::Button("Body Shot")) { p.boneWeightHead = 20; p.boneWeightTorso = 80; }
    }

    if (p.realityAim) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Center Pull");
        ImGui::SliderFloat("Pull Strength", &p.centerPull, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::Checkbox("R##cp", &p.centerPullRandomize);
        if (p.centerPullRandomize) {
            ImGui::SliderFloat("+/- Pull", &p.centerPullRandomAmt, 0.01f, 0.3f, "%.2f");
        }
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "(0=free float, 1=center lock)");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Feathering");
        ImGui::SliderFloat("Feather %%", &p.feather, 0.0f, 50.0f, "%.0f%%%%");
        ImGui::Checkbox("Randomize Feather", &p.featherRandomize);
        if (p.featherRandomize) {
            ImGui::SliderFloat("+/- Feather", &p.featherRandomAmt, 1.0f, 15.0f, "%.0f%%");
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Prediction");
    ImGui::Checkbox("Lead Target", &p.prediction);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "(auto-calculated from distance)");
    if (p.prediction) {
        ImGui::SliderFloat("Bullet Velocity", &p.bulletVelocity, 50.0f, 1000.0f, "%.0f m/s");
    }

    ImGui::PopID();
}

static void create_stream_proof_overlay() {
    dbglog("[stream-proof] attempting creation, g_gameHwnd=%p\n", g_gameHwnd);
    RECT gameRect;
    GetWindowRect(g_gameHwnd, &gameRect);

    wchar_t className[32];
    wsprintfW(className, L"SP_%u", GetTickCount());
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    UINT w = gameRect.right - gameRect.left;
    UINT h = gameRect.bottom - gameRect.top;

    g_overlayHwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
        className, nullptr,
        WS_POPUP,
        gameRect.left, gameRect.top, w, h,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!g_overlayHwnd) {
        dbglog("[stream-proof] CreateWindowExW failed: %lu\n", GetLastError());
        return;
    }

    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(g_overlayHwnd, &margins);

    // WDA_EXCLUDEFROMCAPTURE (0x11) — modern flag (Win 10 2004+) that hides
    // the window from ALL screen-capture APIs including Windows.Graphics.
    // Capture (what modern OBS Game Capture uses). WDA_MONITOR (0x1) is
    // the older flag that only blocks DXGI Desktop Duplication — doesn't
    // block modern OBS. This was the root of every 'stream-proof half-
    // works' state — the API we were using literally doesn't cover the
    // capture path OBS actually takes.
    SetWindowDisplayAffinity(g_overlayHwnd, 0x00000011);

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        &g_overlayDevice, nullptr, &g_overlayContext);
    if (FAILED(hr)) {
        dbglog("[stream-proof] D3D11CreateDevice failed: 0x%lX\n", hr);
        DestroyWindow(g_overlayHwnd);
        g_overlayHwnd = nullptr;
        return;
    }

    IDXGIDevice* dxgiDevice = nullptr;
    g_overlayDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    IDXGIAdapter* adapter = nullptr;
    dxgiDevice->GetAdapter(&adapter);
    IDXGIFactory2* factory2 = nullptr;
    adapter->GetParent(IID_PPV_ARGS(&factory2));

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = w;
    scd.Height = h;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = factory2->CreateSwapChainForComposition(g_overlayDevice, &scd, nullptr, &g_overlaySwapChain);
    if (FAILED(hr)) {
        dbglog("[stream-proof] CreateSwapChainForComposition failed: 0x%lX\n", hr);
        factory2->Release(); adapter->Release(); dxgiDevice->Release();
        g_overlayDevice->Release(); g_overlayDevice = nullptr;
        g_overlayContext->Release(); g_overlayContext = nullptr;
        DestroyWindow(g_overlayHwnd); g_overlayHwnd = nullptr;
        return;
    }

    hr = DCompositionCreateDevice(dxgiDevice, IID_PPV_ARGS(&g_dcompDevice));
    if (FAILED(hr)) {
        dbglog("[stream-proof] DCompositionCreateDevice failed: 0x%lX\n", hr);
        g_overlaySwapChain->Release(); g_overlaySwapChain = nullptr;
        factory2->Release(); adapter->Release(); dxgiDevice->Release();
        g_overlayDevice->Release(); g_overlayDevice = nullptr;
        g_overlayContext->Release(); g_overlayContext = nullptr;
        DestroyWindow(g_overlayHwnd); g_overlayHwnd = nullptr;
        return;
    }

    hr = g_dcompDevice->CreateTargetForHwnd(g_overlayHwnd, TRUE, &g_dcompTarget);
    if (FAILED(hr)) {
        dbglog("[stream-proof] CreateTargetForHwnd failed: 0x%lX\n", hr);
        g_dcompDevice->Release(); g_dcompDevice = nullptr;
        g_overlaySwapChain->Release(); g_overlaySwapChain = nullptr;
        factory2->Release(); adapter->Release(); dxgiDevice->Release();
        g_overlayDevice->Release(); g_overlayDevice = nullptr;
        g_overlayContext->Release(); g_overlayContext = nullptr;
        DestroyWindow(g_overlayHwnd); g_overlayHwnd = nullptr;
        return;
    }

    hr = g_dcompDevice->CreateVisual(&g_dcompVisual);
    if (FAILED(hr)) {
        dbglog("[stream-proof] CreateVisual failed: 0x%lX\n", hr);
        g_dcompTarget->Release(); g_dcompTarget = nullptr;
        g_dcompDevice->Release(); g_dcompDevice = nullptr;
        g_overlaySwapChain->Release(); g_overlaySwapChain = nullptr;
        factory2->Release(); adapter->Release(); dxgiDevice->Release();
        g_overlayDevice->Release(); g_overlayDevice = nullptr;
        g_overlayContext->Release(); g_overlayContext = nullptr;
        DestroyWindow(g_overlayHwnd); g_overlayHwnd = nullptr;
        return;
    }

    g_dcompVisual->SetContent(g_overlaySwapChain);
    g_dcompTarget->SetRoot(g_dcompVisual);
    g_dcompDevice->Commit();

    ID3D11Texture2D* backBuf = nullptr;
    HRESULT gbHr = g_overlaySwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuf);
    if (FAILED(gbHr) || !backBuf) {
        dbglog("[stream-proof] GetBuffer FAILED hr=0x%lX — tearing down\n", gbHr);
        g_dcompVisual->Release();  g_dcompVisual = nullptr;
        g_dcompTarget->Release();  g_dcompTarget = nullptr;
        g_dcompDevice->Release();  g_dcompDevice = nullptr;
        g_overlaySwapChain->Release(); g_overlaySwapChain = nullptr;
        factory2->Release(); adapter->Release(); dxgiDevice->Release();
        g_overlayDevice->Release(); g_overlayDevice = nullptr;
        g_overlayContext->Release(); g_overlayContext = nullptr;
        DestroyWindow(g_overlayHwnd); g_overlayHwnd = nullptr;
        return;
    }
    HRESULT rtvHr = g_overlayDevice->CreateRenderTargetView(backBuf, nullptr, &g_overlayRTV);
    backBuf->Release();
    if (FAILED(rtvHr) || !g_overlayRTV) {
        dbglog("[stream-proof] CreateRenderTargetView FAILED hr=0x%lX — tearing down\n", rtvHr);
        g_dcompVisual->Release();  g_dcompVisual = nullptr;
        g_dcompTarget->Release();  g_dcompTarget = nullptr;
        g_dcompDevice->Release();  g_dcompDevice = nullptr;
        g_overlaySwapChain->Release(); g_overlaySwapChain = nullptr;
        factory2->Release(); adapter->Release(); dxgiDevice->Release();
        g_overlayDevice->Release(); g_overlayDevice = nullptr;
        g_overlayContext->Release(); g_overlayContext = nullptr;
        DestroyWindow(g_overlayHwnd); g_overlayHwnd = nullptr;
        return;
    }

    // Pre-clear the swap chain to fully-transparent + Present ONCE before
    // showing the window. Without this the backbuffer contains undefined
    // pixels (usually white on Nvidia/AMD default alloc) and DirectComposition
    // shows a solid white film over the game until the first real render tick.
    {
        float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };  // premultiplied alpha 0 = fully transparent
        g_overlayContext->ClearRenderTargetView(g_overlayRTV, clear);
        g_overlaySwapChain->Present(0, 0);
    }

    ShowWindow(g_overlayHwnd, SW_SHOWNOACTIVATE);

    factory2->Release();
    adapter->Release();
    dxgiDevice->Release();

    dbglog("[stream-proof] overlay created at %p rtv=%p\n", g_overlayHwnd, g_overlayRTV);
}

static void destroy_stream_proof_overlay() {
    if (g_dcompVisual) { g_dcompVisual->Release(); g_dcompVisual = nullptr; }
    if (g_dcompTarget) { g_dcompTarget->Release(); g_dcompTarget = nullptr; }
    if (g_dcompDevice) { g_dcompDevice->Release(); g_dcompDevice = nullptr; }
    if (g_overlayRTV) { g_overlayRTV->Release(); g_overlayRTV = nullptr; }
    if (g_overlaySwapChain) { g_overlaySwapChain->Release(); g_overlaySwapChain = nullptr; }
    if (g_overlayContext) { g_overlayContext->Release(); g_overlayContext = nullptr; }
    if (g_overlayDevice) { g_overlayDevice->Release(); g_overlayDevice = nullptr; }
    if (g_overlayHwnd) { DestroyWindow(g_overlayHwnd); g_overlayHwnd = nullptr; }
    g_overlayImguiInit = false;
}

static void safe_imgui_render_and_present_overlay() {
    __try {
        ImGui::Render();

        if (g_streamProof.load() && g_overlayImguiInit
            && g_overlayContext && g_overlayRTV && g_overlaySwapChain
            && g_overlayHwnd && g_gameHwnd) {
            RECT gr = {};
            if (!GetWindowRect(g_gameHwnd, &gr)) {
                // Game window gone. Silently fall through to game-device path.
                if (g_mainRenderTargetView) {
                    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
                }
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                return;
            }
            MoveWindow(g_overlayHwnd, gr.left, gr.top, gr.right - gr.left, gr.bottom - gr.top, FALSE);

            LONG_PTR exStyle = GetWindowLongPtrW(g_overlayHwnd, GWL_EXSTYLE);
            if (g_menuVisible) {
                exStyle &= ~WS_EX_TRANSPARENT;
            } else {
                exStyle |= WS_EX_TRANSPARENT;
            }
            SetWindowLongPtrW(g_overlayHwnd, GWL_EXSTYLE, exStyle);

            float clear[4] = {0, 0, 0, 0};
            g_overlayContext->ClearRenderTargetView(g_overlayRTV, clear);
            g_overlayContext->OMSetRenderTargets(1, &g_overlayRTV, nullptr);

            D3D11_VIEWPORT vp = {};
            vp.Width = (float)(gr.right - gr.left);
            vp.Height = (float)(gr.bottom - gr.top);
            vp.MaxDepth = 1.0f;
            g_overlayContext->RSSetViewports(1, &vp);

            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_overlaySwapChain->Present(1, 0);
        } else {
            if (g_mainRenderTargetView) {
                g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            }
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        dbglog("[present] SEH in render: 0x%08lX\n", GetExceptionCode());
    }
}


static bool seh_present_init(IDXGISwapChain* pSwapChain) {
    __try {
        if (g_imguiInitialized && pSwapChain != g_initSwapChain) {
            dbglog("[hooked_present] SWAPCHAIN CHANGED old=%p new=%p — reinitializing\n", g_initSwapChain, pSwapChain);
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            release_render_target();
            if (g_overlayImguiInit) {
                destroy_stream_proof_overlay();
                g_overlayImguiInit = false;
            }
            if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
            if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
            g_imguiInitialized = false;
            g_initSwapChain = nullptr;
        }

        if (!g_imguiInitialized) {
            if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
                dbglog("[hooked_present] GetDevice FAILED\n");
                return true;
            }
            dbglog("[hooked_present] got device %p\n", g_pd3dDevice);

            g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
            create_render_target(pSwapChain);

            DXGI_SWAP_CHAIN_DESC desc;
            pSwapChain->GetDesc(&desc);
            HWND newHwnd = desc.OutputWindow;
            dbglog("[hooked_present] game hwnd: %p\n", newHwnd);

            if (newHwnd != g_gameHwnd) {
                if (g_gameHwnd && g_originalWndProc) {
                    SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
                }
                g_gameHwnd = newHwnd;
                g_originalWndProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)hooked_wndproc);
                dbglog("[hooked_present] wndproc hooked, original: %p\n", g_originalWndProc);
            } else if (!g_originalWndProc) {
                g_gameHwnd = newHwnd;
                g_originalWndProc = (WNDPROC)SetWindowLongPtrW(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)hooked_wndproc);
                dbglog("[hooked_present] wndproc hooked, original: %p\n", g_originalWndProc);
            }

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            // Disable ImGui's keyboard nav (Tab/arrows/etc) so game keys
            // don't hijack menu focus. LO's Tab-hold-radial-menu was
            // scrolling our UI wildly.
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
            ImGui::StyleColorsDark();

            load_settings();

            if (g_streamProof.load()) {
                create_stream_proof_overlay();
                if (g_overlayHwnd && g_overlayDevice && g_overlayContext) {
                    ImGui_ImplWin32_Init(g_gameHwnd);
                    ImGui_ImplDX11_Init(g_overlayDevice, g_overlayContext);
                    g_overlayImguiInit = true;
                    dbglog("[hooked_present] stream-proof ImGui init done\n");
                } else {
                    dbglog("[hooked_present] stream-proof creation failed, falling back\n");
                    g_streamProof.store(false);
                    ImGui_ImplWin32_Init(g_gameHwnd);
                    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
                }
            } else {
                ImGui_ImplWin32_Init(g_gameHwnd);
                ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
            }

            g_initSwapChain = pSwapChain;
            g_imguiInitialized = true;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dbglog("[hooked_present] SEH exception during init: 0x%08lX\n", GetExceptionCode());
        return false;
    }
}

// Stream-proof swap-request handler — extracted from hooked_present because
// __try can't sit in a function with C++ objects that unwind (C2712).
// Wrapped in __try/__except so a mid-swap crash reverts cleanly instead of
// killing the game (LO's 'click stream-proof = instant crash' bug, task 15).
static void seh_handle_streamproof_swap() {
    __try {
        int req = g_streamProofSwapRequest.exchange(0);
        if (req == 1 && !g_overlayImguiInit) {
            create_stream_proof_overlay();
            if (g_overlayHwnd && g_overlayDevice && g_overlayContext
                && g_overlaySwapChain && g_overlayRTV) {
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
                bool win32ok = ImGui_ImplWin32_Init(g_overlayHwnd);
                bool dx11ok  = ImGui_ImplDX11_Init(g_overlayDevice, g_overlayContext);
                if (!win32ok || !dx11ok) {
                    dbglog("[stream-proof] backend init FAILED (win32=%d dx11=%d) -- reverting\n", win32ok, dx11ok);
                    ImGui_ImplDX11_Shutdown();
                    ImGui_ImplWin32_Shutdown();
                    ImGui_ImplWin32_Init(g_gameHwnd);
                    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
                    destroy_stream_proof_overlay();
                    g_streamProof.store(false);
                } else {
                    g_overlayImguiInit = true;
                    g_streamProof.store(true);
                    dbglog("[stream-proof] enabled -- overlay hwnd=%p rtv=%p\n", g_overlayHwnd, g_overlayRTV);
                }
            } else {
                dbglog("[stream-proof] enable failed (hwnd=%p dev=%p ctx=%p sc=%p rtv=%p)\n",
                       g_overlayHwnd, g_overlayDevice, g_overlayContext, g_overlaySwapChain, g_overlayRTV);
                g_streamProof.store(false);
            }
        } else if (req == 2 && g_overlayImguiInit) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui_ImplWin32_Init(g_gameHwnd);
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
            g_overlayImguiInit = false;
            destroy_stream_proof_overlay();
            g_streamProof.store(false);
            dbglog("[stream-proof] disabled\n");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // Instant-crash guard: on swap failure fall back to game backend + kill stream-proof
        dbglog("[stream-proof] SEH 0x%08lX during swap — reverting\n", GetExceptionCode());
        __try { ImGui_ImplDX11_Shutdown(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try { ImGui_ImplWin32_Shutdown(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try {
            if (g_gameHwnd) ImGui_ImplWin32_Init(g_gameHwnd);
            if (g_pd3dDevice && g_pd3dDeviceContext) ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try { destroy_stream_proof_overlay(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        g_overlayImguiInit = false;
        g_streamProof.store(false);
        g_streamProofSwapRequest.store(0);
    }
}

// Wrapped ImGui NewFrame calls — catch the C-runtime assert that fires
// when the backend was init'd with a null hwnd (stream-proof partial init
// failure). Assert = MessageBox + abort; we can't stop the MessageBox from
// imgui itself, but we CAN catch the abort SEH so the game keeps running.
// Returns true if NewFrames succeeded, false if we should abort this frame.
static bool seh_imgui_newframes() {
    __try {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        dbglog("[imgui] SEH 0x%08lX in NewFrame — reverting backend to game hwnd\n", GetExceptionCode());
        // Attempt to salvage: reinit backends against game hwnd/device
        __try { ImGui_ImplDX11_Shutdown(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try { ImGui_ImplWin32_Shutdown(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
        __try {
            if (g_gameHwnd && g_pd3dDevice && g_pd3dDeviceContext) {
                ImGui_ImplWin32_Init(g_gameHwnd);
                ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
                g_overlayImguiInit = false;
                g_streamProof.store(false);
                destroy_stream_proof_overlay();
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        // Ensure menu re-appears on next frame — otherwise assert cascade
        // leaves g_menuVisible false and LO thinks it's fully gone.
        g_menuVisible = true;
        return false;
    }
}

static HRESULT STDMETHODCALLTYPE hooked_present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // Update render-thread heartbeat FIRST — worker checks gap for freeze detect.
    g_lastPresentTick.store(GetTickCount64());
    static bool s_logged = false;
    if (!s_logged) { dbglog("[hooked_present] FIRST CALL swapchain=%p\n", pSwapChain); s_logged = true; }

    // Render-thread heartbeat — pairs with worker heartbeat. If BOTH stop during
    // a freeze, external actor (BE / Unity GC) is suspending everything. If only
    // this one stops but worker keeps beating, main thread is stalled (mutex,
    // some game-side wait). Log the gap since last present.
    {
        static DWORD s_lastPresentTick = 0;
        static int s_presentCounter = 0;
        DWORD nowT = GetTickCount();
        s_presentCounter++;
        DWORD gap = s_lastPresentTick ? (nowT - s_lastPresentTick) : 0;
        // Only log big gaps (>500ms = stutter/hitch) or every ~5s of normal frames
        if (gap > 500 || (s_presentCounter % 300 == 0)) {
            dbglog("[rhb] present #%d tick=%lu gapMs=%lu%s\n",
                   s_presentCounter, nowT, gap, gap > 5000 ? " <<< RENDER STALL" : "");
        }
        s_lastPresentTick = nowT;
    }

    if (g_overlayDisabled)
        return g_originalPresent(pSwapChain, SyncInterval, Flags);

    if (!seh_present_init(pSwapChain)) {
        g_overlayDisabled = true;
        dbglog("[hooked_present] init crashed — overlay disabled permanently\n");
        return g_originalPresent(pSwapChain, SyncInterval, Flags);
    }

    if (!g_pd3dDeviceContext || !g_pd3dDevice) {
        return g_originalPresent(pSwapChain, SyncInterval, Flags);
    }

    static int s_frameCount = 0;
    if (s_frameCount < 5) { dbglog("[frame %d] start\n", s_frameCount); }

    // Auto re-equip tick for dupe mode — SendInput spam slot swap so
    // non-weapon items get re-spawned as fresh entities.
    auto_reequip_tick();

    // Global emergency-reset hotkey (F1). GetAsyncKeyState is polled here
    // instead of via WndProc because WndProc-based INSERT stops working if
    // the game loses focus or our wndproc hook got clobbered by a freeze.
    // Effect: force menu visible, kill stream-proof (menu-hiding fullscreen
    // case), bring overlay window back to topmost.
    {
        static bool s_f1_prev = false;
        bool s_f1_now = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        if (s_f1_now && !s_f1_prev) {
            g_menuVisible = true;
            g_forceWindowed.store(false);
            // SYNCHRONOUS stream-proof teardown — we're already on the render
            // thread inside Present. Deferring via g_streamProofSwapRequest
            // caused a race where the swap chain state was inconsistent for
            // 1+ frames and the overlay window kept its last-drawn frame
            // stuck on screen.
            if (g_overlayImguiInit) {
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
                if (g_gameHwnd) ImGui_ImplWin32_Init(g_gameHwnd);
                if (g_pd3dDevice && g_pd3dDeviceContext) {
                    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
                }
                g_overlayImguiInit = false;
                destroy_stream_proof_overlay();
                g_streamProof.store(false);
                g_streamProofSwapRequest.store(0);
                dbglog("[F1] stream-proof torn down synchronously\n");
            }
            if (g_gameHwnd) {
                // Bring game to front so its window pumps messages again
                ShowWindow(g_gameHwnd, SW_SHOW);
                SetForegroundWindow(g_gameHwnd);
            }
            dbglog("[F1] emergency reset — menu on, stream-proof off, force-windowed off\n");
        }
        s_f1_prev = s_f1_now;
    }

    // Persistent force-windowed. Kick the swap chain out of exclusive
    // fullscreen every frame if the flag is set. Also blocks Alt+Enter.
    if (g_forceWindowed.load() && g_initSwapChain) {
        BOOL fs = FALSE;
        IDXGIOutput* out = nullptr;
        g_initSwapChain->GetFullscreenState(&fs, &out);
        if (out) out->Release();
        if (fs) {
            g_initSwapChain->SetFullscreenState(FALSE, nullptr);
        }
        // MWA_NO_ALT_ENTER blocks Alt+Enter fullscreen toggling.
        static bool s_mwa_set = false;
        if (!s_mwa_set) {
            IDXGIDevice* dev = nullptr;
            IDXGIAdapter* adp = nullptr;
            IDXGIFactory* fac = nullptr;
            if (g_pd3dDevice && SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dev)) && dev) {
                if (SUCCEEDED(dev->GetAdapter(&adp)) && adp) {
                    if (SUCCEEDED(adp->GetParent(__uuidof(IDXGIFactory), (void**)&fac)) && fac) {
                        fac->MakeWindowAssociation(g_gameHwnd, DXGI_MWA_NO_ALT_ENTER);
                        fac->Release();
                        s_mwa_set = true;
                    }
                    adp->Release();
                }
                dev->Release();
            }
        }
    }

    seh_handle_streamproof_swap();

    if (!seh_imgui_newframes()) {
        // NewFrame failed (e.g. imgui assert during stream-proof partial init).
        // Skip rendering this frame; backend was salvaged inside the helper.
        HRESULT hr2 = g_originalPresent(pSwapChain, SyncInterval, Flags);
        return hr2;
    }
    if (s_frameCount < 5) { dbglog("[frame %d] all newframes ok\n", s_frameCount); }

    if (g_menuVisible) {
        std::string lockedName;
        EnterCriticalSection(&g_itemsLock);
        lockedName = g_permaLockName;
        LeaveCriticalSection(&g_itemsLock);

        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 200), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::Begin("Sand Raider", &g_menuVisible);

        ImGui::Text("Status: ");
        ImGui::SameLine();
        if (g_stickyLock.load()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "STICKY: %s", lockedName.c_str());
        } else if (g_dupeMode.load()) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "DUPE MODE");
        } else if (g_permaLockActive.load()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "LOCKED: %s", lockedName.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "SCANNING");
        }

        ImGui::Text("Player: (%.1f, %.1f, %.1f) [C:%d,%d]",
            g_playerPos.x, g_playerPos.y, g_playerPos.z, g_playerPos.cx, g_playerPos.cy);
        ImGui::Text("Entities: %d", g_entityCount.load());
        ImGui::Text("Hook calls: %d | Interact writes: %d",
            g_executeHookCalls.load(), g_forceInteractWrites.load());

        if (g_permaLockActive.load())
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Locked: %s", lockedName.c_str());

        ImGui::Separator();

        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("Items")) {
                ImGui::Text("Controls");
                {
                    bool dupe = g_dupeMode.load();
                    if (ImGui::Checkbox("Dupe Mode", &dupe)) {
                        g_dupeMode.store(dupe);
                        if (!dupe) {
                            g_stickyLock.store(false);
                            g_permaLockActive.store(false);
                            EnterCriticalSection(&g_itemsLock);
                            g_permaLockName.clear();
                            LeaveCriticalSection(&g_itemsLock);
                            g_lockedEntityId.store(-1);
                            g_lockedEntityPtr.store(0);
                        }
                    }
                }
                {
                    // Auto re-equip via SendInput — forces the game to re-spawn
                    // the held item entity so dupe-mode picker sees it fresh
                    // every tick. Fixes 'gun dupes fast because of hand-swap
                    // animation, but medkits/rods need manual swap-and-back'.
                    bool are = g_autoReequip.load();
                    if (ImGui::Checkbox("Auto re-equip (spam slot swap)", &are))
                        g_autoReequip.store(are);
                    if (are) {
                        // Slot targets — dispatched via EquipItemInInventorySlotHoloMessage
                        // internally, no SendInput. Game uses radial menu so scroll/keys don't work.
                        int a = g_reequipSlotA.load();
                        int b = g_reequipSlotB.load();
                        ImGui::PushItemWidth(80);
                        if (ImGui::InputInt("Slot A##rea", &a)) g_reequipSlotA.store(a);
                        ImGui::SameLine();
                        if (ImGui::InputInt("Slot B##reb", &b)) g_reequipSlotB.store(b);
                        ImGui::PopItemWidth();
                        ImGui::TextDisabled("Alternates equip between A and B via internal message dispatch.");
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(dupe non-weapons w/o manual swap)");
                    if (are) {
                        ImGui::Indent();
                        {
                            bool ar = g_autoRelockDupe.load();
                            if (ImGui::Checkbox("Auto-relock same item when it reappears (top-of-list)", &ar))
                                g_autoRelockDupe.store(ar);
                            ImGui::TextDisabled("Remembers last-locked item name. When it disappears + reappears mid-dupe-cycle, re-locks automatically. No need to click again.");
                        }
                        // Always-visible: how long between full rounds
                        ImGui::SliderInt("Time between rounds (ms)", &g_reequipRoundPauseMs, 100, 5000, "%d ms");
                        // Double-pump: 2 quick re-equips per round instead of 1
                        static bool s_doublePump = (g_reequipBurstCount == 2);
                        // Keep the checkbox in sync if code somewhere else changed burst count
                        if (g_reequipBurstCount == 2) s_doublePump = true;
                        else if (g_reequipBurstCount == 1) s_doublePump = false;
                        if (ImGui::Checkbox("Equip 2x per round", &s_doublePump)) {
                            g_reequipBurstCount = s_doublePump ? 2 : 1;
                        }
                        if (s_doublePump) {
                            ImGui::Indent();
                            ImGui::SliderInt("Time between the 2 equips (ms)", &g_reequipBurstGapMs, 10, 500, "%d ms");
                            ImGui::Unindent();
                        }
                        ImGui::TextDisabled("Requires 'Dupe Mode' also enabled. Scroll bounce re-equips whatever's in hand.");
                        ImGui::Unindent();
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
                {
                    bool sc = g_showContainerContents.load();
                    if (ImGui::Checkbox("Show container contents (items with Parent)", &sc))
                        g_showContainerContents.store(sc);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(dupe research: click any child to lock+F-force it)");
                }

                {
                    std::string sel;
                    EnterCriticalSection(&g_itemsLock);
                    sel = g_permaLockName;
                    LeaveCriticalSection(&g_itemsLock);

                    if (!sel.empty()) {
                        if (ImGui::Button("Hide Selected")) {
                            g_hiddenNames.insert(sel);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Hide Prefix")) {
                            size_t upos = sel.find('_');
                            if (upos != std::string::npos) {
                                std::string prefix = sel.substr(0, upos + 1);
                                bool already = false;
                                for (auto& p : g_hiddenPrefixes) {
                                    if (p == prefix) { already = true; break; }
                                }
                                if (!already) g_hiddenPrefixes.push_back(prefix);
                            } else {
                                g_hiddenNames.insert(sel);
                            }
                        }
                        ImGui::SameLine();
                    }
                    if (!g_hiddenNames.empty() || !g_hiddenPrefixes.empty()) {
                        if (ImGui::Button("Clear Hidden")) {
                            g_hiddenNames.clear();
                            g_hiddenPrefixes.clear();
                        }
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            "(%d names, %d prefixes hidden)",
                            (int)g_hiddenNames.size(), (int)g_hiddenPrefixes.size());
                    }
                }

                if (ImGui::Button("Unlock All")) {
                    g_permaLockActive.store(false);
                    EnterCriticalSection(&g_itemsLock);
                    g_permaLockName.clear();
                    LeaveCriticalSection(&g_itemsLock);
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

                static char searchBuf[256] = {};
                ImGui::InputText("Search", searchBuf, sizeof(searchBuf));
                g_nameFilter = searchBuf;

                ImGui::Text("Items");
                ImGui::BeginChild("ItemList", ImVec2(0, 0), true);

                struct ItemRowSnap {
                    std::string name;
                    float distance;
                    int entityId;
                    int serverId;
                    void* entityPtr;
                    bool isWeapon;
                    bool isHeavy;
                    bool isHeldByPlayer;
                    bool isInOthersInv;
                    int  parentEntityId;
                    float healthNorm;   // -1 = unknown
                };
                static std::vector<ItemRowSnap> rowSnaps;
                rowSnaps.clear();

                bool wf = g_weaponFilter.load();
                size_t nlen = strlen(searchBuf);

                EnterCriticalSection(&g_itemsLock);
                rowSnaps.reserve(g_items.size());
                bool showChildren = g_showContainerContents.load();
                for (size_t i = 0; i < g_items.size(); i++) {
                    const ItemInfo& item = g_items[i];
                    if (wf && !item.isWeapon) continue;
                    // Hide container-INVENTORY-children by default. PlayerAvatars
                    // + mobs have Parent set too (for session/spawn tracking)
                    // so pure parent!=0 check was too broad and made players
                    // vanish from ESP list (repeat regression, task 6-ish).
                    // Now: only hide items whose NAME starts with "item_" —
                    // that's actual inventory clutter. PlayerAvatars/mobs/
                    // walkers stay visible regardless.
                    bool looksLikeInvChild = item.parentEntityId != 0 && !item.isHeldByPlayer
                                             && item.name.rfind("item_", 0) == 0;
                    if (!showChildren && looksLikeInvChild) continue;
                    // Items-panel-only hides — kills phantom-item interference
                    // with auto-dupe without hiding the item type from world ESP.
                    if (g_itemListHiddenNames.count(item.name)) continue;
                    if (g_itemListHiddenEntityIds.count(item.entityId)) continue;
                    if (nlen > 0) {
                        bool found = false;
                        // Match against BOTH raw name AND displayName so searches like
                        // "canned food" hit the nice-name, and "PlayerAvatar" still works.
                        const std::string* haystacks[2] = { &item.name, &item.displayName };
                        for (int h = 0; h < 2 && !found; h++) {
                            const char* hay = haystacks[h]->c_str();
                            size_t hlen = haystacks[h]->size();
                            if (nlen > hlen) continue;
                            for (size_t s = 0; s <= hlen - nlen; s++) {
                                bool match = true;
                                for (size_t c = 0; c < nlen; c++) {
                                    if (tolower((unsigned char)hay[s+c]) != tolower((unsigned char)searchBuf[c])) {
                                        match = false;
                                        break;
                                    }
                                }
                                if (match) { found = true; break; }
                            }
                        }
                        if (!found) continue;
                    }

                    // Case-insensitive filter check.
                    {
                        std::string _lname = item.name;
                        for (auto& c : _lname) if (c >= 'A' && c <= 'Z') c = c + 32;
                        bool hidden = false;
                        for (auto& n : g_hiddenNames) {
                            std::string _ln = n;
                            for (auto& c : _ln) if (c >= 'A' && c <= 'Z') c = c + 32;
                            if (_ln == _lname) { hidden = true; break; }
                        }
                        if (!hidden) {
                            for (auto& p : g_hiddenPrefixes) {
                                std::string _lp = p;
                                for (auto& c : _lp) if (c >= 'A' && c <= 'Z') c = c + 32;
                                if (_lname.rfind(_lp, 0) == 0) { hidden = true; break; }
                            }
                        }
                        if (hidden) continue;
                    }

                    ItemRowSnap rs;
                    rs.name = item.name;
                    rs.distance = item.distance;
                    rs.entityId = item.entityId;
                    rs.serverId = item.serverId;
                    rs.entityPtr = item.entityPtr;
                    rs.isWeapon = item.isWeapon;
                    rs.isHeavy = item.isHeavy;
                    rs.isHeldByPlayer = item.isHeldByPlayer;
                    rs.isInOthersInv = item.isInOthersInv;
                    rs.parentEntityId = item.parentEntityId;
                    rs.healthNorm = item.healthNorm;
                    rowSnaps.push_back(std::move(rs));
                }
                LeaveCriticalSection(&g_itemsLock);

                if (ImGui::BeginTable("Items", 7,
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                    ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {

                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("HP",   ImGuiTableColumnFlags_WidthFixed, 50.0f);
                    ImGui::TableSetupColumn("Src", ImGuiTableColumnFlags_WidthFixed, 45.0f);
                    ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableHeadersRow();

                    int lockedId = g_lockedEntityId.load();
                    bool permaActive = g_permaLockActive.load();

                    ImGuiListClipper clipper;
                    clipper.Begin((int)rowSnaps.size());
                    while (clipper.Step()) {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                            const ItemRowSnap& item = rowSnaps[i];
                            int itemLockId = (item.serverId > 0) ? item.serverId : item.entityId;
                            bool isLocked = permaActive && (itemLockId == lockedId);

                            ImVec4 color;
                            if (isLocked) color = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
                            else if (item.isWeapon) color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                            else if (item.isHeldByPlayer) color = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
                            else color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                            ImGui::TableNextRow();
                            ImGui::PushStyleColor(ImGuiCol_Text, color);

                            char label[32];
                            snprintf(label, sizeof(label), "%d", i + 1);

                            ImGui::TableSetColumnIndex(0);
                            bool clicked = ImGui::Selectable(label, isLocked,
                                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);

                            if (clicked) {
                                EnterCriticalSection(&g_itemsLock);
                                g_permaLockName = item.name;
                                LeaveCriticalSection(&g_itemsLock);
                                g_permaLockActive.store(true);
                                int lockId = (item.serverId > 0) ? item.serverId : item.entityId;
                                g_lockedEntityId.store(lockId);
                                g_lockedEntityPtr.store((uintptr_t)item.entityPtr);
                                // Remember for auto-relock: when this item name
                                // re-appears in items after dupe cycles, we
                                // auto-relock without needing another click.
                                ensure_last_duped_name_cs();
                                EnterCriticalSection(&g_lastDupedNameCS);
                                g_lastDupedName = item.name;
                                LeaveCriticalSection(&g_lastDupedNameCS);
                                // Kill focus on ANY currently-focused text input
                                // (e.g. the Search box) so subsequent keystrokes
                                // don't get typed into it. Was LO's #1 UI gripe.
                                ImGui::SetWindowFocus(nullptr);
                                ImGuiContext& gc = *ImGui::GetCurrentContext();
                                if (gc.ActiveId != 0) ImGui::ClearActiveID();
                                // NOTE: don't auto-disable g_dupeMode here — it
                                // would kill the auto-scroll-reequip since that
                                // gates on (autoReequip && dupeMode). Let LO
                                // toggle dupe explicitly.
                            }

                            // Right-click context menu — quick blacklist actions
                            char popupId[64];
                            snprintf(popupId, sizeof(popupId), "rowctx_%d", i);
                            if (ImGui::BeginPopupContextItem(popupId)) {
                                ImGui::Text("%s", item.name.c_str());
                                ImGui::Separator();
                                if (ImGui::MenuItem("Hide this name from ESP")) {
                                    g_hiddenNames.insert(item.name);
                                }
                                // Compute a sensible prefix — first underscore token or first 8 chars
                                std::string pfx = item.name;
                                size_t sp = pfx.find('_');
                                if (sp != std::string::npos && sp < 24) pfx = pfx.substr(0, sp);
                                if (pfx.size() > 24) pfx = pfx.substr(0, 24);
                                char label2[128];
                                snprintf(label2, sizeof(label2), "Hide ALL '%s*' from ESP", pfx.c_str());
                                if (ImGui::MenuItem(label2)) {
                                    bool already = false;
                                    for (auto& p : g_hiddenPrefixes) if (p == pfx) { already = true; break; }
                                    if (!already) g_hiddenPrefixes.push_back(pfx);
                                }
                                if (ImGui::MenuItem("Copy name to clipboard")) {
                                    ImGui::SetClipboardText(item.name.c_str());
                                }
                                ImGui::Separator();
                                // List-only hides — for phantom items cluttering
                                // your auto-dupe workflow. World ESP unaffected.
                                if (ImGui::MenuItem("Hide THIS entity from list (id-specific)")) {
                                    g_itemListHiddenEntityIds.insert(item.entityId);
                                }
                                if (ImGui::MenuItem("Hide all with this name from list only")) {
                                    g_itemListHiddenNames.insert(item.name);
                                }
                                // Custom ESP color — inline ColorEdit4 in the popup.
                                ImGui::Separator();
                                {
                                    ImU32 cur = IM_COL32(255,255,255,255);
                                    auto cit = g_customEspColors.find(item.name);
                                    if (cit != g_customEspColors.end()) cur = (ImU32)cit->second;
                                    float col[4] = {
                                        ((cur >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
                                        ((cur >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
                                        ((cur >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
                                        ((cur >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f,
                                    };
                                    if (ImGui::ColorEdit4("ESP color", col, ImGuiColorEditFlags_NoInputs)) {
                                        g_customEspColors[item.name] = (uint32_t)IM_COL32(
                                            (int)(col[0]*255), (int)(col[1]*255),
                                            (int)(col[2]*255), (int)(col[3]*255));
                                    }
                                    if (cit != g_customEspColors.end() && ImGui::MenuItem("Remove custom color")) {
                                        g_customEspColors.erase(item.name);
                                    }
                                }
                                ImGui::EndPopup();
                            }

                            ImGui::TableSetColumnIndex(1);
                            if (item.isHeldByPlayer)
                                ImGui::Text("%s (held)", item.name.c_str());
                            else if (item.isInOthersInv)
                                ImGui::Text("[inv %d] %s", item.parentEntityId, item.name.c_str());
                            else
                                ImGui::TextUnformatted(item.name.c_str());

                            ImGui::TableSetColumnIndex(2);
                            if (item.distance >= 0)
                                ImGui::Text("%.1fm", item.distance);
                            else
                                ImGui::TextUnformatted("---");

                            ImGui::TableSetColumnIndex(3);
                            if (item.healthNorm >= 0.0f && item.healthNorm <= 1.5f) {
                                int pct = (int)(item.healthNorm * 100.0f + 0.5f);
                                // Color-code: green >=50, orange 20-50, red <20
                                ImVec4 hpCol = (item.healthNorm >= 0.5f)
                                    ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                    : (item.healthNorm >= 0.2f)
                                    ? ImVec4(1.0f, 0.65f, 0.15f, 1.0f)
                                    : ImVec4(0.95f, 0.25f, 0.25f, 1.0f);
                                ImGui::TextColored(hpCol, "%d%%", pct);
                            } else {
                                ImGui::TextUnformatted("---");
                            }

                            ImGui::TableSetColumnIndex(4);
                            ImGui::TextUnformatted(item.isHeldByPlayer ? "PAR" : "WRLD");

                            ImGui::TableSetColumnIndex(5);
                            ImGui::TextUnformatted(item.isHeavy ? "HVY" : (item.isWeapon ? "WPN" : ""));

                            ImGui::TableSetColumnIndex(6);
                            ImGui::Text("%d", item.entityId);

                            ImGui::PopStyleColor();
                        }
                    }
                    clipper.End();

                    ImGui::EndTable();
                }

                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Turret")) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Turret / Weapon Mods");
                ImGui::Separator();

                {
                    bool rapid = g_turretRapidFire.load();
                    if (ImGui::Checkbox("Rapid Fire", &rapid))
                        g_turretRapidFire.store(rapid);
                }
                {
                    bool noRecoil = g_turretNoRecoil.load();
                    if (ImGui::Checkbox("No Recoil (full kill)", &noRecoil))
                        g_turretNoRecoil.store(noRecoil);
                }
                {
                    ImGui::PushItemWidth(140);
                    float m = g_recoilMult.load();
                    if (ImGui::SliderFloat("Recoil strength##rec", &m, 0.0f, 1.0f, "%.2fx")) {
                        g_recoilMult.store(m);
                    }
                    ImGui::PopItemWidth();
                    ImGui::TextDisabled("Cosmetic only right now — checkbox above does the actual work.");
                    ImGui::TextDisabled("Scaling branch corrupted RecoilLookOffset pointer fields => CTD.");
                    ImGui::TextDisabled("Field layout dump goes to log on first hit ([recoil-layout]).");
                }

                ImGui::Separator();

                bool anyActive = g_turretRapidFire.load() || g_turretNoRecoil.load();
                if (anyActive) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "ACTIVE:");
                    if (g_turretRapidFire.load()) ImGui::BulletText("Rapid fire (shotDuration = 0.01)");
                    if (g_turretNoRecoil.load()) ImGui::BulletText("No recoil (spring zeroed)");
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "DISABLED");
                }

                ImGui::Separator();
                ImGui::Text("Weapon entities: %d | Mods applied: %d",
                    g_turretEntitiesFound.load(), g_turretModsApplied.load());

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Component Debug:");
                ImGui::Text("  WeaponOverheatData: %d", g_dbgHasWeaponHeat.load());
                ImGui::Text("  StationaryAutoWeapon: %d", g_dbgHasStationaryAuto.load());
                ImGui::Text("  RecoilLookOffset: %d", g_dbgHasRecoilLook.load());
                ImGui::Text("  Overheated: %d", g_dbgHasOverheated.load());

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Component Indices:");
                ImGui::Text("  Overheated: %d", g_idx_overheated);
                ImGui::Text("  RecoilLookOffset: %d", g_idx_recoil_look);
                ImGui::Text("  StationaryAutoWeapon: %d", g_idx_stationary_auto);
                ImGui::Text("  StationaryWeaponData: %d", g_idx_stationary_data);
                ImGui::Text("  WeaponOverheat: %d", g_idx_weapon_overheat);
                ImGui::Text("  WeaponOverheatData: %d", g_idx_weapon_overheat_data);
                ImGui::Text("  AutoTurretData: %d", g_idx_auto_turret);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Player")) {
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Player mods (strip-component approach — Entitas systems short-circuit when target component is missing)");
                ImGui::Separator();
                {
                    bool v = g_noFallDamage.load();
                    if (ImGui::Checkbox("No fall damage", &v)) g_noFallDamage.store(v);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(strips FallDamageData per tick)");
                }
                {
                    bool v = g_noJumpDelay.load();
                    if (ImGui::Checkbox("No jump delay (spam jump / fatigue-free)", &v)) g_noJumpDelay.store(v);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(strips JumpDelay)");
                }
                {
                    bool v = g_infiniteAmmo.load();
                    if (ImGui::Checkbox("Infinite ammo (experimental)", &v)) g_infiniteAmmo.store(v);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(strips AmmoId/InventoryItemAmmoId)");
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "GRAVITY MODES (write to Jump component — likely gravity field, negative = anti-grav)");
                {
                    bool v = g_flyMode.load();
                    if (ImGui::Checkbox("Fly mode (gravity = -1x, floats up)", &v)) g_flyMode.store(v);
                }
                {
                    bool v = g_lowGravMode.load();
                    if (ImGui::Checkbox("Low gravity (0.3x, floaty jumps)", &v)) g_lowGravMode.store(v);
                }
                {
                    float m = g_jumpForceMult.load();
                    if (ImGui::SliderFloat("Manual gravity multiplier", &m, -3.0f, 5.0f, "%.2fx"))
                        g_jumpForceMult.store(m);
                    ImGui::TextDisabled("1.0 = default, 0.5 = double jump height, 0 = no fall damage, -1 = anti-grav. Fly/Low-grav checkboxes override.");
                }
                {
                    float m = g_speedMult.load();
                    // Slider capped at 3.5x — above ~3x game silently soft-kicks
                    // (session marked as speedhack, no popup, interact locks,
                    // "cursed" state until reconnect). 3.5 = little buffer.
                    if (ImGui::SliderFloat("Speed multiplier", &m, 1.0f, 3.5f, "%.2fx"))
                        g_speedMult.store(m);
                    ImGui::TextDisabled("Capped at 3.5x — above ~3x game silently soft-kicks you (LO 2026-08-07 observed).");
                }
                {
                    bool v = g_walkerFly.load();
                    if (ImGui::Checkbox("Walker fly (in-vehicle)", &v)) g_walkerFly.store(v);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(writes CheatWalkerFly+0x10 = true)");
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "NOCLIP (Position writes — server-observed, capped at 30 m/s)");
                {
                    bool e = g_noClipEnabled.load();
                    if (ImGui::Checkbox("Enable noclip (W = up, S = down)", &e)) g_noClipEnabled.store(e);
                }
                {
                    float s = g_noClipSpeed.load();
                    ImGui::PushItemWidth(160);
                    if (ImGui::SliderFloat("Speed##noclip", &s, 3.0f, 30.0f, "%.1f m/s"))
                        g_noClipSpeed.store(s);
                    ImGui::PopItemWidth();
                }
                {
                    // Activation-key selector (hold/toggle)
                    const char* nmodes[] = { "Hold RMB", "Hold Mouse5", "Hold V", "Toggle Mouse5", "Toggle V", "Toggle F" };
                    static int nmodeIdx = 0;
                    // Restore selector from current binding
                    int cur = 0;
                    if (g_hotkeyNoClipToggle.load() == VK_XBUTTON2) cur = 3;
                    else if (g_hotkeyNoClipToggle.load() == 'V')    cur = 4;
                    else if (g_hotkeyNoClipToggle.load() == 'F')    cur = 5;
                    else if (g_hotkeyNoClipHold.load() == VK_XBUTTON2) cur = 1;
                    else if (g_hotkeyNoClipHold.load() == 'V')       cur = 2;
                    else cur = 0;
                    nmodeIdx = cur;
                    if (ImGui::Combo("Activation##noclip", &nmodeIdx, nmodes, 6)) {
                        g_hotkeyNoClipHold.store(0);
                        g_hotkeyNoClipToggle.store(0);
                        g_noClipActive.store(false);
                        switch (nmodeIdx) {
                            case 0: g_hotkeyNoClipHold.store(VK_RBUTTON); break;
                            case 1: g_hotkeyNoClipHold.store(VK_XBUTTON2); break;
                            case 2: g_hotkeyNoClipHold.store('V'); break;
                            case 3: g_hotkeyNoClipToggle.store(VK_XBUTTON2); break;
                            case 4: g_hotkeyNoClipToggle.store('V'); break;
                            case 5: g_hotkeyNoClipToggle.store('F'); break;
                        }
                    }
                    ImGui::TextDisabled("RMB doubles as ADS — game still fires ADS. Toggle keys don't conflict.");
                    // Diagnostic — reveals silently-failing noclip. Called
                    // ticks up always; Active only when key held/toggled;
                    // NoEntity if g_playerEntityPtr is 0 (scan hasn't cached
                    // player yet); Wrote counts successful Position writes.
                    ImGui::Text("noclip diag: called=%d active=%d noEnt=%d wrote=%d entPtr=%p",
                        g_noclipDbgCalled.load(), g_noclipDbgActive.load(),
                        g_noclipDbgNoEntity.load(), g_noclipDbgWrote.load(),
                        (void*)g_playerEntityPtr.load());
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "World mods");
                {
                    bool v = g_alwaysDay.load();
                    if (ImGui::Checkbox("Always day", &v)) g_alwaysDay.store(v);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(writes TimeOfDayManager.currentTime)");
                }
                {
                    float t = g_dayTime.load();
                    if (ImGui::SliderFloat("Time of day", &t, 0.0f, 1.0f, "%.2f (0=night, 0.5=noon, 1=night)"))
                        g_dayTime.store(t);
                }
                ImGui::Text("TimeOfDayManager singleton: %p", (void*)g_todInstance);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 1.0f, 1.0f), "SHIP / STORM / WALKER MODS");
                {
                    bool v = g_stormImmunity.load();
                    if (ImGui::Checkbox("Storm immunity (strip InEyeOfStorm from us)", &v)) g_stormImmunity.store(v);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(idx %d)", g_idx_in_eye_of_storm);
                }
                {
                    bool v = g_shipResilience.load();
                    if (ImGui::Checkbox("Ship resilience (force max HP on our reactor)", &v)) g_shipResilience.store(v);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(reactor idx %d)", g_idx_reactor_data);
                }
                {
                    float m = g_walkerSpeedMult.load();
                    if (ImGui::SliderFloat("Walker speed multiplier", &m, 1.0f, 5.0f, "%.2fx"))
                        g_walkerSpeedMult.store(m);
                    ImGui::TextDisabled("Writes WalkerEngineData +0x10 *= mult (offset guess). Trampler nitrous.");
                }
                {
                    bool ev = g_espShowExtraction.load();
                    if (ImGui::Checkbox("ESP: Extraction points (yellow marker)", &ev)) g_espShowExtraction.store(ev);
                }
                {
                    bool rv = g_espShowReactors.load();
                    if (ImGui::Checkbox("ESP: Ships / reactors (cyan marker)", &rv)) g_espShowReactors.store(rv);
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "ANTI-CHEAT COMPONENT STRIPS (client-side — server may still validate)");
                {
                    bool v = g_stripAntiCheat.load();
                    if (ImGui::Checkbox("Strip AntiCheat component from player", &v)) g_stripAntiCheat.store(v);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(idx %d)", g_idx_anticheat);
                }
                {
                    bool v = g_stripSpeedCap.load();
                    if (ImGui::Checkbox("Strip AntiCheatSpeedCapData (bypass speed cap)", &v)) g_stripSpeedCap.store(v);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(idx %d)", g_idx_anticheat_speedcap);
                }
                ImGui::TextDisabled("AntiCheatNoClipIgnore=%d, DontDestroyInStorm=%d (add-component primitive needed)",
                                    g_idx_anticheat_noclip_ignore, g_idx_dont_destroy_in_storm);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "HEAVY DUPE OPTIONS (test which one works)");
                {
                    bool v = g_heavyBypass.load();
                    if (ImGui::Checkbox("HeavyFix1: strip LargeItemData on locked entity", &v)) g_heavyBypass.store(v);
                }
                {
                    bool v = g_heavyFix2.load();
                    if (ImGui::Checkbox("HeavyFix2: flip ItemTypeData to weapon (=1) on locked entity", &v)) g_heavyFix2.store(v);
                    ImGui::TextDisabled("Both can be on simultaneously — 'HeavyFix3'.");
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Loot ESP colors");
                ImGui::ColorEdit4("Tier 1 color", g_lootT1Color, ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Tier 2 color", g_lootT2Color, ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Tier 3 color", g_lootT3Color, ImGuiColorEditFlags_NoInputs);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Utility");
                if (ImGui::Button("Hoover — re-dump every entity NOW (perf_l.dat)")) {
                    g_hooverRequest.store(true);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Force fresh full-state snapshot.");
                if (ImGui::Button("HARD KILL (or press F12) — instant TerminateProcess")) {
                    g_hardKillRequested.store(true);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Bypasses game's 7-step shutdown hell.");
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 1.0f, 1.0f, 1.0f), "REBINDABLE HOTKEYS:");
                {
                    // Per-feature rebind button. Click -> set capture request
                    // -> worker thread watches next key press -> assigns to feature.
                    struct HK { const char* label; std::atomic<int>* vk; int reqId; };
                    HK kBinds[] = {
                        {"Hard Kill",        &g_hotkeyHardKill,       1},
                        {"Dupe Suspend",     &g_hotkeyDupeSuspend,    2},
                        {"Dupe Master Off",  &g_hotkeyDupeMaster,     3},
                        {"Playback First Recording", &g_hotkeyPlaybackFirst, 4},
                    };
                    int active = g_hotkeyCaptureRequest.load();
                    for (auto& hk : kBinds) {
                        int cur = hk.vk->load();
                        ImGui::Text("%s: VK 0x%02X", hk.label, cur);
                        ImGui::SameLine();
                        char btn[64];
                        snprintf(btn, sizeof(btn), "%s##rb%d",
                                 active == hk.reqId ? "PRESS ANY KEY..." : "Rebind", hk.reqId);
                        if (ImGui::Button(btn)) {
                            g_hotkeyCaptureRequest.store(active == hk.reqId ? 0 : hk.reqId);
                        }
                    }
                    ImGui::TextDisabled("Click Rebind then press any key. Escape to cancel.");
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Component Indices:");
                ImGui::Text("  FallDamageData:      %d", g_idx_fall_damage);
                ImGui::Text("  JumpDelay:           %d", g_idx_jump_delay);
                ImGui::Text("  Jump:                %d", g_idx_jump);
                ImGui::Text("  AmmoId:              %d", g_idx_ammo);
                ImGui::Text("  InventoryItemAmmoId: %d", g_idx_inv_ammo);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Weapons")) {
                {
                    bool enabled = g_weaponModsEnabled.load();
                    if (ImGui::Checkbox("Enable Weapon Mods", &enabled))
                        g_weaponModsEnabled.store(enabled);
                }

                if (g_weaponModsEnabled.load()) {
                    ImGui::Separator();
                    {
                        bool noDrop = g_weaponNoDrop.load();
                        if (ImGui::Checkbox("No Bullet Drop", &noDrop))
                            g_weaponNoDrop.store(noDrop);
                    }
                    {
                        bool noBloom = g_weaponNoBloom.load();
                        if (ImGui::Checkbox("No Bloom", &noBloom))
                            g_weaponNoBloom.store(noBloom);
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(removes projectile drag)");
                    }
                    {
                        float velMult = g_weaponVelocityMult.load();
                        if (ImGui::SliderFloat("Bullet Velocity", &velMult, 1.0f, 100.0f, "x%.1f"))
                            g_weaponVelocityMult.store(velMult);
                    }

                    ImGui::Separator();
                    bool anyActive = g_weaponNoDrop.load() || g_weaponNoBloom.load() || g_weaponVelocityMult.load() > 1.0f;
                    if (anyActive) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "ACTIVE:");
                        if (g_weaponNoDrop.load()) ImGui::BulletText("No bullet drop (gravity = 0)");
                        if (g_weaponNoBloom.load()) ImGui::BulletText("No bloom (drag = 0)");
                        if (g_weaponVelocityMult.load() > 1.0f) ImGui::BulletText("Velocity x%.1f", g_weaponVelocityMult.load());
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No modifications active");
                    }
                }

                ImGui::Separator();
                ImGui::Text("BulletProjectileData index: %d", g_idx_bullet_projectile_data);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("ESP")) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "3D World ESP");
                ImGui::Separator();
                {
                    bool esp3d = g_esp3DEnabled.load();
                    if (ImGui::Checkbox("Enable 3D ESP", &esp3d))
                        g_esp3DEnabled.store(esp3d);
                }
                {
                    bool showPlayers = g_espShowPlayers.load();
                    if (ImGui::Checkbox("Players##3d", &showPlayers))
                        g_espShowPlayers.store(showPlayers);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!showPlayers);
                    ImGui::SliderFloat("Dist##player", &g_espPlayerDist, 100.0f, 20000.0f, "%.0f m");
                    ImGui::EndDisabled();
                }
                {
                    bool showMobs = g_espShowMobs.load();
                    if (ImGui::Checkbox("Mobs##3d", &showMobs))
                        g_espShowMobs.store(showMobs);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!showMobs);
                    ImGui::SliderFloat("Dist##mob", &g_espMobDist, 100.0f, 10000.0f, "%.0f m");
                    ImGui::EndDisabled();
                }
                {
                    bool showWalkers = g_espShowWalkers.load();
                    if (ImGui::Checkbox("Walkers##3d", &showWalkers))
                        g_espShowWalkers.store(showWalkers);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!showWalkers);
                    ImGui::SliderFloat("Dist##walker", &g_espWalkerDist, 100.0f, 20000.0f, "%.0f m");
                    ImGui::EndDisabled();
                }
                {
                    bool showItems = g_espShowItems.load();
                    if (ImGui::Checkbox("Items##3d", &showItems))
                        g_espShowItems.store(showItems);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!showItems);
                    ImGui::SliderFloat("Dist##item", &g_espItemDist, 50.0f, 2000.0f, "%.0f m");
                    ImGui::EndDisabled();
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Loot Containers");
                {
                    bool t1 = g_espShowLootT1.load();
                    if (ImGui::Checkbox("T1 Loot##3d", &t1))
                        g_espShowLootT1.store(t1);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!t1);
                    ImGui::SliderFloat("Dist##lootT1", &g_espLootT1Dist, 50.0f, 2000.0f, "%.0f m");
                    ImGui::EndDisabled();
                }
                {
                    bool t2 = g_espShowLootT2.load();
                    if (ImGui::Checkbox("T2 Loot##3d", &t2))
                        g_espShowLootT2.store(t2);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!t2);
                    ImGui::SliderFloat("Dist##lootT2", &g_espLootT2Dist, 50.0f, 2000.0f, "%.0f m");
                    ImGui::EndDisabled();
                }
                {
                    bool t3 = g_espShowLootT3.load();
                    if (ImGui::Checkbox("T3 Loot##3d", &t3))
                        g_espShowLootT3.store(t3);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!t3);
                    ImGui::SliderFloat("Dist##lootT3", &g_espLootT3Dist, 50.0f, 2000.0f, "%.0f m");
                    ImGui::EndDisabled();
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "Render style");
                {
                    bool sb = g_espShowBox.load();
                    if (ImGui::Checkbox("Box", &sb)) g_espShowBox.store(sb);
                    ImGui::SameLine();
                    bool ss = g_espShowSkeleton.load();
                    if (ImGui::Checkbox("Skeleton", &ss)) g_espShowSkeleton.store(ss);
                    ImGui::SameLine();
                    bool sh = g_espShowHealth.load();
                    if (ImGui::Checkbox("HP%", &sh)) g_espShowHealth.store(sh);
                    ImGui::SameLine();
                    bool shb = g_espShowHealthBar.load();
                    if (ImGui::Checkbox("HP Bar", &shb)) g_espShowHealthBar.store(shb);
                    ImGui::SameLine();
                    bool sd = g_espShowDistance.load();
                    if (ImGui::Checkbox("Distance", &sd)) g_espShowDistance.store(sd);
                    ImGui::TextDisabled("HP appends [HP N%%] text. HP Bar draws a colored bar above the box.");
                }
                {
                    ImGui::PushItemWidth(160);
                    float ls = g_espLabelScale.load();
                    if (ImGui::SliderFloat("Label size##ls", &ls, 0.5f, 2.5f, "%.2fx"))
                        g_espLabelScale.store(ls);
                    ImGui::PopItemWidth();
                    const char* posNames[] = { "On entity", "Above", "Below" };
                    int lp = g_espLabelPos.load();
                    ImGui::SetNextItemWidth(160);
                    if (ImGui::Combo("Label position##lp", &lp, posNames, 3))
                        g_espLabelPos.store(lp);
                    ImGui::TextDisabled("Above/Below draws a small tether line to the entity so crowds stay readable.");
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.7f, 1.0f), "Filters (hide from ESP)");
                {
                    ImGui::TextDisabled("%d names, %d prefixes currently hidden. Right-click any item in the Items list to add.",
                        (int)g_hiddenNames.size(), (int)g_hiddenPrefixes.size());
                    // Direct-input add name
                    static char s_addName[128] = "";
                    ImGui::SetNextItemWidth(200);
                    ImGui::InputText("##addName", s_addName, sizeof(s_addName));
                    ImGui::SameLine();
                    if (ImGui::Button("Hide Name") && s_addName[0]) { g_hiddenNames.insert(s_addName); s_addName[0] = 0; }
                    ImGui::SameLine();
                    if (ImGui::Button("Hide Prefix") && s_addName[0]) {
                        bool already = false;
                        for (auto& p : g_hiddenPrefixes) if (p == s_addName) { already = true; break; }
                        if (!already) g_hiddenPrefixes.push_back(s_addName);
                        s_addName[0] = 0;
                    }
                    // Scrollable list of active hides w/ per-item remove
                    if (ImGui::CollapsingHeader("Manage hidden list##filterMgr")) {
                        ImGui::BeginChild("##hidelist", ImVec2(0, 140), true);
                        std::vector<std::string> toRemoveNames;
                        for (auto& n : g_hiddenNames) {
                            ImGui::PushID(n.c_str());
                            if (ImGui::SmallButton("X")) toRemoveNames.push_back(n);
                            ImGui::SameLine();
                            ImGui::Text("name: %s", n.c_str());
                            ImGui::PopID();
                        }
                        for (auto& n : toRemoveNames) g_hiddenNames.erase(n);
                        std::vector<size_t> toRemoveIdx;
                        for (size_t i = 0; i < g_hiddenPrefixes.size(); i++) {
                            ImGui::PushID((int)(1000 + i));
                            if (ImGui::SmallButton("X")) toRemoveIdx.push_back(i);
                            ImGui::SameLine();
                            ImGui::Text("prefix: %s*", g_hiddenPrefixes[i].c_str());
                            ImGui::PopID();
                        }
                        for (auto it = toRemoveIdx.rbegin(); it != toRemoveIdx.rend(); ++it)
                            g_hiddenPrefixes.erase(g_hiddenPrefixes.begin() + *it);
                        ImGui::EndChild();
                        if (!g_hiddenNames.empty() || !g_hiddenPrefixes.empty()) {
                            if (ImGui::Button("Clear ALL hidden##filterMgrClear")) {
                                g_hiddenNames.clear();
                                g_hiddenPrefixes.clear();
                            }
                        }
                    }
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Stream Protection");
                {
                    bool sp = g_streamProof.load();
                    if (ImGui::Checkbox("Stream Proof", &sp)) {
                        if (sp && !g_overlayImguiInit) {
                            g_streamProofSwapRequest.store(1);
                        } else if (!sp && g_overlayImguiInit) {
                            g_streamProofSwapRequest.store(2);
                        } else {
                            g_streamProof.store(sp);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), g_overlayImguiInit ? "(overlay hidden from capture)" : "(visible in recordings)");

                    // Diagnostic — is the game actually windowed or lying?
                    BOOL fs = FALSE;
                    IDXGIOutput* out = nullptr;
                    if (g_initSwapChain) g_initSwapChain->GetFullscreenState(&fs, &out);
                    if (out) out->Release();
                    if (fs) {
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                            "SWAP CHAIN: EXCLUSIVE FULLSCREEN -- stream-proof menu WILL be hidden.");
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Force windowed")) {
                            // One-shot flip -- but the game re-forces fullscreen
                            // next frame if we don't also enable the every-frame
                            // toggle. So do BOTH.
                            if (g_initSwapChain)
                                g_initSwapChain->SetFullscreenState(FALSE, nullptr);
                            g_forceWindowed.store(true);
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                            "SWAP CHAIN: windowed / borderless -- stream-proof should show menu.");
                    }
                    // Persistent auto-force. When on, every Present frame checks
                    // fullscreen state + kicks it back to windowed. Also blocks
                    // Alt+Enter fullscreen toggle via DXGI_MWA_NO_ALT_ENTER.
                    // Guarded by confirmation modal because if the game fights
                    // back it can cause flicker/lockup that makes the UI hard
                    // to reach to turn OFF. F1 (global) also disables it.
                    {
                        static bool s_show_confirm = false;
                        bool afw = g_forceWindowed.load();
                        bool checked = afw;
                        if (ImGui::Checkbox("Force windowed EVERY FRAME (kills any fullscreen switch)", &checked)) {
                            if (checked && !afw) {
                                // Attempting to turn ON — confirm
                                s_show_confirm = true;
                            } else if (!checked && afw) {
                                // Off is always safe
                                g_forceWindowed.store(false);
                            }
                        }
                        if (s_show_confirm) {
                            ImGui::OpenPopup("force_windowed_confirm");
                            s_show_confirm = false;
                        }
                        if (ImGui::BeginPopupModal("force_windowed_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "Enable Force Windowed?");
                            ImGui::Separator();
                            ImGui::TextWrapped(
                                "This calls SetFullscreenState(FALSE) on the game's swap chain\n"
                                "EVERY FRAME. If the game re-forces fullscreen just as often,\n"
                                "you'll get flicker or a fight between the two. In the worst\n"
                                "case the UI becomes unusable.\n"
                                "\n"
                                "Safety nets:\n"
                                "  - press F1 anywhere to instantly disable this + reset menu\n"
                                "  - toggle the checkbox again to turn off if you can see it\n"
                                "\n"
                                "Recommend using the one-shot 'Force windowed' button first\n"
                                "and only enabling THIS toggle if the game keeps snapping\n"
                                "back to fullscreen."
                            );
                            ImGui::Separator();
                            if (ImGui::Button("Enable it")) {
                                g_forceWindowed.store(true);
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel")) {
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }
                    }
                    ImGui::TextDisabled("F1 anywhere = emergency reset (menu on, stream-proof off, focus overlay)");
                    ImGui::TextDisabled("If game keeps snapping to fullscreen: right-click sand.exe -> Properties");
                    ImGui::TextDisabled("-> Compatibility -> check 'Disable fullscreen optimizations'.");
                }

                if (g_cameraGetMain && g_cameraW2S) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Camera: OK");
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Camera: NOT FOUND");
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "2D Radar");
                ImGui::Separator();
                {
                    bool esp = g_espEnabled.load();
                    if (ImGui::Checkbox("Enable Radar", &esp))
                        g_espEnabled.store(esp);
                }
                ImGui::PushItemWidth(160);
                ImGui::SliderFloat("Range##radar", &g_radarRange, 500.0f, 20000.0f, "%.0f m");
                {
                    float rot = g_radarRotationOffsetDeg.load();
                    if (ImGui::SliderFloat("Rot offset##radar", &rot, -180.0f, 180.0f, "%.1f deg"))
                        g_radarRotationOffsetDeg.store(rot);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("0##rotreset")) g_radarRotationOffsetDeg.store(0.0f);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-30##rot30")) g_radarRotationOffsetDeg.store(-30.0f);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+30##rotp30")) g_radarRotationOffsetDeg.store(30.0f);
                    ImGui::TextDisabled("Tune until things you look at appear at 12 o'clock on radar.");
                }
                ImGui::PopItemWidth();
                {
                    bool sc = g_espShowStormCircles.load();
                    if (ImGui::Checkbox("Storm Circles (amber=current, red=future)", &sc))
                        g_espShowStormCircles.store(sc);
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d found)", g_stormCirclesFound.load());
                }
                {
                    bool ss = g_espShowSentinels.load();
                    if (ImGui::Checkbox("Sentinel detection rings", &ss))
                        g_espShowSentinels.store(ss);
                    ImGui::PushItemWidth(160);
                    float sr = g_sentinelRadius.load();
                    if (ImGui::SliderFloat("Sentinel radius##sen", &sr, 100.0f, 800.0f, "%.0f m"))
                        g_sentinelRadius.store(sr);
                    float smh = g_sentinelMountHeight.load();
                    if (ImGui::SliderFloat("Mount height##sen", &smh, 0.0f, 60.0f, "%.0f m"))
                        g_sentinelMountHeight.store(smh);
                    ImGui::PopItemWidth();
                    ImGui::TextDisabled("Mount height: how tall the pillar is. Ring anchors at (sentinel Y - mount height).");
                }
                {
                    bool how = g_espHideOwnWalkerParts.load();
                    if (ImGui::Checkbox("Hide own walker parts (legs / compartments / engines)", &how))
                        g_espHideOwnWalkerParts.store(how);
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Visibility");
                {
                    bool showSelf = g_espShowSelf.load();
                    if (ImGui::Checkbox("Show Self (green)", &showSelf))
                        g_espShowSelf.store(showSelf);
                }

                ImGui::Separator();
                int playerCount = 0;
                int mobCount = 0;
                int walkerCount = 0;
                int itemCount = 0;
                EnterCriticalSection(&g_itemsLock);
                for (auto& item : g_items) {
                    if (item.name.rfind("PlayerAvatar", 0) == 0) playerCount++;
                    else if (item.isCreature) mobCount++;
                    else if (item.name.rfind("EXPEDITION_WALKER", 0) == 0) walkerCount++;
                    else itemCount++;
                }
                LeaveCriticalSection(&g_itemsLock);
                ImGui::Text("Players: %d | Mobs: %d | Walkers: %d | Items: %d", playerCount, mobCount, walkerCount, itemCount);

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Aimbot")) {
                {
                    bool enabled = g_aimbotEnabled.load();
                    if (ImGui::Checkbox("Enable Aimbot", &enabled))
                        g_aimbotEnabled.store(enabled);
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "General");
                ImGui::SliderFloat("FOV Radius", &g_aimbotFOV, 30.0f, 500.0f, "%.0f px");
                ImGui::SliderFloat("Max Distance", &g_aimbotMaxDist, 50.0f, 2000.0f, "%.0f m");

                {
                    bool tp = g_aimbotTargetPlayers.load();
                    if (ImGui::Checkbox("Target Players", &tp))
                        g_aimbotTargetPlayers.store(tp);
                }
                ImGui::SameLine();
                {
                    bool tm = g_aimbotTargetMobs.load();
                    if (ImGui::Checkbox("Target Mobs", &tm))
                        g_aimbotTargetMobs.store(tm);
                }
                ImGui::SameLine();
                {
                    bool tr = g_aimbotTargetReactors.load();
                    if (ImGui::Checkbox("Target Reactors", &tr))
                        g_aimbotTargetReactors.store(tr);
                }
                {
                    bool rp = g_aimbotReactorPriority.load();
                    if (ImGui::Checkbox("Reactor Priority (override best target + bullseye)", &rp))
                        g_aimbotReactorPriority.store(rp);
                }

                const char* keyNames[] = { "Mouse 5 (Side)", "Mouse 4 (Side)", "Right Click", "Left Alt", "Left Shift" };
                int keyValues[] = { VK_XBUTTON2, VK_XBUTTON1, VK_RBUTTON, VK_LMENU, VK_LSHIFT };
                int currentKeyIdx = 0;
                for (int k = 0; k < 5; k++) {
                    if (g_aimbotActivationKey == keyValues[k]) { currentKeyIdx = k; break; }
                }
                if (ImGui::Combo("Activation Key", &currentKeyIdx, keyNames, 5)) {
                    g_aimbotActivationKey = keyValues[currentKeyIdx];
                }

                {
                    bool drawFov = g_aimbotDrawFOV.load();
                    if (ImGui::Checkbox("Draw FOV Circle", &drawFov))
                        g_aimbotDrawFOV.store(drawFov);
                }

                ImGui::Separator();
                ImGui::Checkbox("Monster Aimbot same as Player", &g_mobAimbotSame);

                if (g_mobAimbotSame) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Aim Profile (All Targets)");
                    render_aimbot_profile(g_aimPlayer, "player");
                } else {
                    if (ImGui::BeginTabBar("AimbotProfiles")) {
                        if (ImGui::BeginTabItem("Players")) {
                            render_aimbot_profile(g_aimPlayer, "player");
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Monsters")) {
                            render_aimbot_profile(g_aimMob, "mob");
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                }

                ImGui::Separator();
                if (g_aimbotEnabled.load()) {
                    ImGui::TextColored(
                        g_aimbotActive.load() ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(0.8f, 0.8f, 0.0f, 1.0f),
                        g_aimbotActive.load() ? "ACTIVE — aiming" : "ARMED — hold activation key");
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "DISABLED");
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Dupe Lab")) {
                ImGui::PushItemWidth(140);   // cap slider/input widget widths
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "MESSAGE CAPTURE + REPLAY");
                ImGui::Separator();
                // Countdown: LO clicks button in menu (Esc pauses game),
                // waits N seconds, un-Escapes, action fires with game live.
                {
                    int d = g_actionDelaySec.load();
                    if (ImGui::SliderInt("Action delay##delay", &d, 0, 15, "%d sec"))
                        g_actionDelaySec.store(d);
                    ImGui::TextDisabled("Arms every button below to fire N sec after click.");
                    int pendId = g_pendingActionId.load();
                    if (pendId != 0) {
                        unsigned long long dl = g_pendingActionDeadline.load();
                        unsigned long long now = GetTickCount64();
                        int remainMs = (dl > now) ? (int)(dl - now) : 0;
                        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1),
                            "PENDING action id=%d — fires in %.1f sec", pendId, remainMs / 1000.0f);
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel Pending")) g_pendingActionId.store(0);
                    }
                }
                ImGui::Separator();
                {
                    bool cap = g_captureMessages.load();
                    if (ImGui::Checkbox("Capture-to-file (perf_capture.dat)", &cap)) g_captureMessages.store(cap);
                    ImGui::TextDisabled("Every Publish call logged. Default OFF (amplifies AVs during instability). Turn on right before action, off after.");
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "RECORD (in-memory msg buffer, per named action):");
                bool recording = g_dupeLabRecording.load();
                static const char* kRecordings[] = {
                    "place-on-shelf", "place-in-box", "swap-box",
                    "pickup-from-shelf", "pickup-from-box", "split-stack", "equip-item"
                };
                for (auto* nm : kRecordings) {
                    char btn[128]; snprintf(btn, sizeof(btn), "Arm: %s", nm);
                    if (ImGui::Button(btn)) { dupelab_arm_record(nm); }
                    ImGui::SameLine();
                    size_t c = dupelab_recording_count(nm);
                    ImGui::TextDisabled("(%zu msgs)", c);
                    ImGui::SameLine();
                    char pb[128]; snprintf(pb, sizeof(pb), "Playback##%s", nm);
                    if (ImGui::Button(pb)) { dupelab_schedule(100, nm); }
                }
                if (recording) {
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "RECORDING — perform the action now, then click STOP.");
                    if (ImGui::Button("STOP RECORDING")) dupelab_record_stop();
                }
                ImGui::TextDisabled("Publish addr: %p | HoloMessengerModule instance: %p",
                                    g_holoPublishAddr, g_holoMessengerInstance);
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "COMPONENT SPOOFS (writes to currently-locked entity):");
                ImGui::TextDisabled("Click an item in the Items list first to lock. All buttons target that lock.");
                {
                    int t = g_dupeSpoofType.load();
                    ImGui::SliderInt("Spoof type##st", &t, 0, 20);
                    g_dupeSpoofType.store(t);
                    if (ImGui::Button("Type-Spoof")) {
                        dupelab_spoof_type_on_locked(g_dupeSpoofType.load());
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("Write ItemTypeData.type (2 = consumable).");
                }
                {
                    int s = g_dupeForceHandSlot.load();
                    ImGui::SliderInt("Force slot##fs", &s, 0, 9);
                    g_dupeForceHandSlot.store(s);
                    if (ImGui::Button("Force-Into-Hand")) {
                        dupelab_force_slot_on_locked(g_dupeForceHandSlot.load());
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("InventoryItemSlotIndex (0 = usually hand).");
                }
                {
                    if (ImGui::Button("TROJAN COMBO (type=2 + slot=0)")) {
                        dupelab_spoof_type_on_locked(2);
                        dupelab_force_slot_on_locked(0);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("Spoof to consumable + force into hand slot. Disguise + brandish.");
                }
                {
                    if (ImGui::Button("Strip InteractibleNotActive (make interact-eligible)")) {
                        dupelab_strip_interactible_not_active_on_locked();
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("Removes the 'not interactible' flag so game treats target as grabbable.");
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "COMBO BUTTONS:");
                if (ImGui::Button("ONE-CLICK DUPE ATTEMPT (spoof+slot+strip)")) {
                    dupelab_spoof_type_on_locked(g_dupeSpoofType.load());
                    dupelab_force_slot_on_locked(g_dupeForceHandSlot.load());
                    dupelab_strip_interactible_not_active_on_locked();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Fires all three spoofs. Then F-force-interact in-game.");
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "DIRECT MESSAGE DISPATCH (server-side experiments, ANY result is data):");
                ImGui::TextDisabled("Field offsets are il2cpp packing GUESSES. Wrong ones = SEH-caught no-op, safe.");
                static int s_slot = 0, s_srcSlot = 0, s_dstSlot = 1, s_srcParent = 0, s_dstParent = 0, s_count = 1;
                ImGui::SliderInt("Generic slot arg", &s_slot, 0, 9);
                if (ImGui::Button("Equip Slot N (dispatch)")) dupelab_dispatch_equip_slot(s_slot);
                ImGui::SameLine();
                if (ImGui::Button("Drop from Slot N (dispatch)")) dupelab_dispatch_drop_slot(s_slot);
                ImGui::Separator();
                ImGui::TextDisabled("Move / Split — need slot indices + parent entity IDs:");
                ImGui::InputInt("from Slot", &s_srcSlot); ImGui::SameLine();
                ImGui::InputInt("from Parent (entity id)", &s_srcParent);
                ImGui::InputInt("to Slot", &s_dstSlot); ImGui::SameLine();
                ImGui::InputInt("to Parent (entity id)", &s_dstParent);
                ImGui::InputInt("count (for split)", &s_count);
                if (ImGui::Button("Move Slot (dispatch MoveInventorySlot)")) {
                    dupelab_dispatch_move(s_srcSlot, s_srcParent, s_dstSlot, s_dstParent);
                }
                ImGui::SameLine();
                if (ImGui::Button("Split Stack (dispatch SplitInventorySlot)")) {
                    dupelab_dispatch_split(s_srcSlot, s_srcParent, s_dstSlot, s_dstParent, s_count);
                }
                ImGui::TextDisabled("Split with count=1 targeting materials/silver/food = material dupe test.");
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f), "MESSAGE STORM (dispatch N times rapid):");
                static int s_stormCount = 5;
                ImGui::SliderInt("Storm count", &s_stormCount, 1, 20);
                if (ImGui::Button("Storm: Equip Slot N x count")) {
                    for (int i = 0; i < s_stormCount; ++i) dupelab_dispatch_equip_slot(s_slot);
                }
                ImGui::SameLine();
                if (ImGui::Button("Storm: Split x count")) {
                    for (int i = 0; i < s_stormCount; ++i)
                        dupelab_dispatch_split(s_srcSlot, s_srcParent, s_dstSlot, s_dstParent, s_count);
                }
                if (ImGui::Button("Storm: Move x count")) {
                    for (int i = 0; i < s_stormCount; ++i)
                        dupelab_dispatch_move(s_srcSlot, s_srcParent, s_dstSlot, s_dstParent);
                }
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "EDGE-CASE COMBOS (racing / weird):");
                if (ImGui::Button("Move-to-Same-Slot (from==to, expect ghost)")) {
                    dupelab_dispatch_move(s_srcSlot, s_srcParent, s_srcSlot, s_srcParent);
                }
                if (ImGui::Button("Move-to-Occupied (dst already has item)")) {
                    dupelab_dispatch_move(s_srcSlot, s_srcParent, s_dstSlot, s_dstParent);
                    // dispatch a second identical move to try to fight the race
                    dupelab_dispatch_move(s_srcSlot, s_srcParent, s_dstSlot, s_dstParent);
                }
                if (ImGui::Button("Move + Equip Race (same frame)")) {
                    dupelab_dispatch_move(s_srcSlot, s_srcParent, s_dstSlot, s_dstParent);
                    dupelab_dispatch_equip_slot(s_dstSlot);
                }
                if (ImGui::Button("Drop + Equip Race (drop then equip immediately)")) {
                    dupelab_dispatch_drop_slot(s_slot);
                    dupelab_dispatch_equip_slot(s_slot);
                }
                if (ImGui::Button("Split x5 same target (rapid replicate)")) {
                    for (int i = 0; i < 5; ++i)
                        dupelab_dispatch_split(s_srcSlot, s_srcParent, s_dstSlot + i, s_dstParent, s_count);
                }
                if (ImGui::Button("Move x5 to 5 different dst slots")) {
                    for (int i = 0; i < 5; ++i)
                        dupelab_dispatch_move(s_srcSlot, s_srcParent, s_dstSlot + i, s_dstParent);
                }
                ImGui::TextDisabled("Each button = one hypothesis. Try each, watch inventory + debug log for behavior.");
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.9f, 1.0f), "NOTES ON DUPE MECHANISM (from LO's testing):");
                ImGui::BulletText("Small items dupe: hand-brandish + interactables-list duplex + F-grab creates copy");
                ImGui::BulletText("Big items don't fit the window: snatch animation exceeds check-in-hand timeout");
                ImGui::BulletText("Materials (silver/food) never brandish -> never enter duplex -> current dupe misses them");
                ImGui::BulletText("Real fix: capture the outbound HoloMessage the game sends when a place/pickup succeeds, replay it with different args");
                ImGui::BulletText("Type-spoof to canned-food type = no 3D model = no render crash during brandish");
                ImGui::PopItemWidth();       // match the PushItemWidth at Dupe Lab tab start
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Debug")) {
                if (ImGui::Button("Clear")) ringlog::clear();
                ImGui::SameLine();
                bool paused = ringlog::is_paused();
                if (ImGui::Checkbox("Pause", &paused)) ringlog::set_paused(paused);
                ImGui::SameLine();
                ImGui::TextColored(paused ? ImVec4(1, 0.6f, 0.3f, 1) : ImVec4(0.6f, 1, 0.6f, 1),
                    paused ? "[PAUSED]" : "[live]");
                ImGui::SameLine();
                static char s_dumpStatus[128] = "";
                static ULONGLONG s_dumpStatusExpiry = 0;
                if (ImGui::Button("Save Log")) {
                    wchar_t rp[MAX_PATH];
                    wchar_t adw[MAX_PATH];
                    DWORD nw = GetEnvironmentVariableW(L"APPDATA", adw, MAX_PATH);
                    if (nw && nw < MAX_PATH)
                        _snwprintf_s(rp, MAX_PATH, _TRUNCATE, L"%s\\Microsoft\\PerfCache\\perf_snap.dat", adw);
                    else
                        wcsncpy_s(rp, MAX_PATH, L"C:\\ProgramData\\Microsoft\\PerfCache\\ring_snapshot.txt", _TRUNCATE);
                    size_t n = ringlog::dump_ring_to_file(rp);
                    snprintf(s_dumpStatus, sizeof(s_dumpStatus),
                        "wrote %zu lines -> perf_snap.dat", n);
                    s_dumpStatusExpiry = GetTickCount64() + 4000;
                }
                ImGui::SameLine();
                ImGui::Text("Lines: %zu / %zu", ringlog::count(), ringlog::RING_CAP);
                if (s_dumpStatus[0] && GetTickCount64() < s_dumpStatusExpiry) {
                    ImGui::TextColored(ImVec4(0.4f, 1, 0.7f, 1), "%s", s_dumpStatus);
                }
                ImGui::Separator();

                ImGui::BeginChild("DebugLogScroll", ImVec2(0, 0), true,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGuiListClipper clipper;
                clipper.Begin((int)ringlog::count());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        ImGui::TextUnformatted(ringlog::line((size_t)i));
                    }
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    static std::vector<ESP3DEntry> espEntries;
    espEntries.clear();
    if ((g_esp3DEnabled.load() || g_aimbotEnabled.load()) && g_cameraGetMain && g_cameraW2S) {
        static bool s_renderThreadAttached = false;
        if (!s_renderThreadAttached) {
            HMODULE ga = GetModuleHandleA("GameAssembly.dll");
            if (ga) {
                auto threadAttach = (fn_il2cpp_thread_attach)GetProcAddress(ga, "il2cpp_thread_attach");
                auto domainGet = (fn_il2cpp_domain_get)GetProcAddress(ga, "il2cpp_domain_get");
                if (threadAttach && domainGet) {
                    void* dom = domainGet();
                    if (dom) threadAttach(dom);
                }
            }
            s_renderThreadAttached = true;
            g_renderThreadId = GetCurrentThreadId();
        }

        float projDist = g_espPlayerDist;
        if (g_espMobDist > projDist) projDist = g_espMobDist;
        if (g_espWalkerDist > projDist) projDist = g_espWalkerDist;
        if (g_espItemDist > projDist) projDist = g_espItemDist;
        if (g_espLootT1Dist > projDist) projDist = g_espLootT1Dist;
        if (g_espLootT2Dist > projDist) projDist = g_espLootT2Dist;
        if (g_espLootT3Dist > projDist) projDist = g_espLootT3Dist;
        if (g_aimbotEnabled.load() && g_aimbotMaxDist > projDist) projDist = g_aimbotMaxDist;
        seh_project_entities(espEntries, projDist);

        if (g_esp3DEnabled.load() && !espEntries.empty()) {
            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            // For in-world circle projection (sentinel radius, future features).
            void* camera = g_cameraGetMain ? g_cameraGetMain(nullptr) : nullptr;
            ImVec2 displaySize = ImGui::GetIO().DisplaySize;
            float labelScale = g_espLabelScale.load();
            if (labelScale < 0.4f) labelScale = 0.4f;
            if (labelScale > 3.0f) labelScale = 3.0f;
            int labelPos = g_espLabelPos.load();   // 0=center, 1=above, 2=below
            ImFont* labelFont = ImGui::GetFont();
            // GetFontSize() returns current scaled size; scaling on top is fine
            // for label-level scaling because we pass a computed size to AddText.
            float labelFontSize = ImGui::GetFontSize() * labelScale;
            for (auto& e : espEntries) {
                ImVec2 textSize = ImGui::CalcTextSize(e.label);
                textSize.x *= labelScale; textSize.y *= labelScale;
                float boxW = textSize.x + 10;
                float boxH = textSize.y + 6;
                // Anchor: center (default), above, or below the entity dot.
                float labelDy = 0;
                if (labelPos == 1) labelDy = -boxH - 6;   // above
                else if (labelPos == 2) labelDy = boxH + 6; // below

                ImU32 bgColor, borderColor, textColor;
                if (e.hasCustomColor) {
                    int r = (e.customColor >> IM_COL32_R_SHIFT) & 0xFF;
                    int g = (e.customColor >> IM_COL32_G_SHIFT) & 0xFF;
                    int b = (e.customColor >> IM_COL32_B_SHIFT) & 0xFF;
                    bgColor     = IM_COL32(r/3, g/3, b/3, 180);
                    borderColor = IM_COL32(r, g, b, 240);
                    textColor   = IM_COL32(r, g, b, 255);
                } else if (e.isFinalExtract) {
                    // Bright magenta — final extraction is the most valuable location
                    bgColor = IM_COL32(80, 0, 60, 200);
                    borderColor = IM_COL32(255, 50, 200, 240);
                    textColor = IM_COL32(255, 100, 220, 255);
                } else if (e.isExtraction) {
                    // Bright yellow — regular extraction / landing / ship
                    bgColor = IM_COL32(60, 60, 0, 200);
                    borderColor = IM_COL32(255, 230, 40, 240);
                    textColor = IM_COL32(255, 240, 100, 255);
                } else if (e.isReactor && e.type != 0 && e.type != 1) {
                    // Cyan — enemy ship (has ReactorData but not a player or mob)
                    bgColor = IM_COL32(0, 60, 60, 200);
                    borderColor = IM_COL32(40, 230, 230, 240);
                    textColor = IM_COL32(100, 240, 240, 255);
                } else if (e.type == 2) {
                    bgColor = IM_COL32(0, 40, 80, 180);
                    borderColor = IM_COL32(0, 200, 255, 220);
                    textColor = IM_COL32(100, 220, 255, 255);
                } else if (e.type == 0) {
                    bgColor = IM_COL32(80, 0, 0, 180);
                    borderColor = IM_COL32(255, 50, 50, 220);
                    textColor = IM_COL32(255, 80, 80, 255);
                } else if (e.type == 1) {
                    bgColor = IM_COL32(80, 40, 0, 180);
                    borderColor = IM_COL32(255, 165, 0, 220);
                    textColor = IM_COL32(255, 200, 50, 255);
                } else if (e.lootTier == 3) {
                    ImU32 c = IM_COL32((int)(g_lootT3Color[0]*255), (int)(g_lootT3Color[1]*255), (int)(g_lootT3Color[2]*255), 255);
                    bgColor = IM_COL32((int)(g_lootT3Color[0]*80), (int)(g_lootT3Color[1]*80), (int)(g_lootT3Color[2]*80), 180);
                    borderColor = c; textColor = c;
                } else if (e.lootTier == 2) {
                    ImU32 c = IM_COL32((int)(g_lootT2Color[0]*255), (int)(g_lootT2Color[1]*255), (int)(g_lootT2Color[2]*255), 255);
                    bgColor = IM_COL32((int)(g_lootT2Color[0]*80), (int)(g_lootT2Color[1]*80), (int)(g_lootT2Color[2]*80), 180);
                    borderColor = c; textColor = c;
                } else if (e.lootTier == 1) {
                    ImU32 c = IM_COL32((int)(g_lootT1Color[0]*255), (int)(g_lootT1Color[1]*255), (int)(g_lootT1Color[2]*255), 255);
                    bgColor = IM_COL32((int)(g_lootT1Color[0]*80), (int)(g_lootT1Color[1]*80), (int)(g_lootT1Color[2]*80), 160);
                    borderColor = c; textColor = c;
                } else {
                    bgColor = IM_COL32(40, 60, 20, 160);
                    borderColor = IM_COL32(140, 200, 60, 200);
                    textColor = IM_COL32(180, 230, 100, 255);
                }

                float labelCY = e.sy + labelDy;
                if (g_espShowBox.load() && !e.isSentinel) {
                    // Sentinels: no bounding box (per LO — label + ground ring only).
                    drawList->AddRectFilled(
                        ImVec2(e.sx - boxW / 2, labelCY - boxH / 2),
                        ImVec2(e.sx + boxW / 2, labelCY + boxH / 2),
                        bgColor, 3.0f);
                    drawList->AddRect(
                        ImVec2(e.sx - boxW / 2, labelCY - boxH / 2),
                        ImVec2(e.sx + boxW / 2, labelCY + boxH / 2),
                        borderColor, 3.0f);
                }
                // Health bar — draw ABOVE the box (or above the entity center
                // if box is disabled) so it's always visible. Green >=50%,
                // orange 20-50%, red <20%.
                if (g_espShowHealthBar.load() && e.healthNorm >= 0.0f && e.healthNorm <= 1.5f) {
                    float hn = e.healthNorm;
                    if (hn > 1.0f) hn = 1.0f;
                    float bx0 = e.sx - boxW / 2;
                    float bx1 = e.sx + boxW / 2;
                    float by  = e.sy - boxH / 2 - 8.0f;   // 8px above box top
                    float bh  = 4.0f;
                    ImU32 hpCol;
                    if (hn >= 0.5f)      hpCol = IM_COL32(60, 220, 60, 240);   // green
                    else if (hn >= 0.2f) hpCol = IM_COL32(255, 165, 40, 240);  // orange
                    else                 hpCol = IM_COL32(230, 40, 40, 240);   // red
                    drawList->AddRectFilled(
                        ImVec2(bx0, by),
                        ImVec2(bx1, by + bh),
                        IM_COL32(20, 20, 20, 200));                        // dark bg
                    drawList->AddRectFilled(
                        ImVec2(bx0, by),
                        ImVec2(bx0 + (bx1 - bx0) * hn, by + bh),
                        hpCol);                                            // fill
                    drawList->AddRect(
                        ImVec2(bx0, by),
                        ImVec2(bx1, by + bh),
                        IM_COL32(0, 0, 0, 200));                           // outline
                }
                drawList->AddText(
                    labelFont, labelFontSize,
                    ImVec2(e.sx - textSize.x / 2, labelCY - textSize.y / 2),
                    textColor, e.label);
                // Tether line — only when label is offset from the entity dot
                // (below or above), so the user can tell which label goes to
                // which enemy in a crowd.
                if (labelPos == 2) {
                    drawList->AddLine(
                        ImVec2(e.sx, e.sy),
                        ImVec2(e.sx, labelCY - boxH / 2),
                        borderColor, 1.0f);
                } else if (labelPos == 1) {
                    drawList->AddLine(
                        ImVec2(e.sx, labelCY + boxH / 2),
                        ImVec2(e.sx, e.sy),
                        borderColor, 1.0f);
                } else {
                    drawList->AddLine(
                        ImVec2(e.sx, e.sy + boxH / 2),
                        ImVec2(e.sx, e.sy + boxH / 2 + 12),
                        borderColor, 1.5f);
                }
                // Sentinel detection radius — draw a red ring on the sand
                // (world-space, projected). Skip entirely if you're INSIDE
                // the ring (already inside danger zone, ring is huge & useless
                // AND was producing giant lines from off-screen projections
                // stalling the render thread → rubberband). Also cap segments
                // to 24 and viewport-clip each vertex so no line spans the
                // whole screen when the ring wraps behind the camera.
                if (e.isSentinel && g_espShowSentinels.load() && camera) {
                    float r = g_sentinelRadius.load();
                    // Ground anchoring: sentinels are mounted on tall pillars,
                    // so e.sentinelWorld.y is the TOWER TOP. Subtracting the
                    // mount-height slider gives an approximation of the base
                    // ground level under the sentinel — anchored to the
                    // sentinel's own terrain, not the player's (fixes the
                    // "ring floats in air when I'm on a mountain" bug).
                    // Real fix (physics raycast per point) is queued.
                    float groundY = e.sentinelWorld.y - g_sentinelMountHeight.load();
                    if (e.dist > 0.0f && e.dist < r) {
                        // Inside — draw a small marker at the entity pos.
                        drawList->AddText(
                            ImVec2(e.sx - 8, e.sy - 20),
                            IM_COL32(255, 40, 40, 240), "!IN RANGE!");
                    } else {
                        const int SEGMENTS = 24;
                        float clipMinX = -displaySize.x, clipMaxX = displaySize.x * 2;
                        float clipMinY = -displaySize.y, clipMaxY = displaySize.y * 2;
                        Vec3 prevScreen{0,0,0}; bool havePrev = false;
                        for (int seg = 0; seg <= SEGMENTS; seg++) {
                            float ang = (float)seg / (float)SEGMENTS * 6.28318531f;
                            Vec3 wp;
                            wp.x = e.sentinelWorld.x + cosf(ang) * r;
                            wp.y = groundY;
                            wp.z = e.sentinelWorld.z + sinf(ang) * r;
                            Vec3 sp;
                            g_cameraW2S(&sp, camera, &wp, nullptr);
                            if (sp.z <= 0 || std::isnan(sp.x) || std::isnan(sp.y)) {
                                havePrev = false; continue;
                            }
                            float sx = sp.x;
                            float sy = displaySize.y - sp.y;
                            bool onscreen = (sx > clipMinX && sx < clipMaxX &&
                                             sy > clipMinY && sy < clipMaxY);
                            if (!onscreen) { havePrev = false; continue; }
                            if (havePrev) {
                                drawList->AddLine(
                                    ImVec2(prevScreen.x, prevScreen.y),
                                    ImVec2(sx, sy),
                                    IM_COL32(255, 40, 40, 220), 2.0f);
                            }
                            prevScreen = Vec3{sx, sy, 0};
                            havePrev = true;
                        }
                    }
                }
                // Reactor bullseye when priority mode is on and target is enemy.
                if (e.isReactor && !e.isAlly && g_aimbotReactorPriority.load()) {
                    ImU32 bull = IM_COL32(255, 40, 40, 240);
                    drawList->AddCircle(ImVec2(e.sx, e.sy), 14.0f, bull, 24, 2.0f);
                    drawList->AddCircle(ImVec2(e.sx, e.sy),  6.0f, bull, 12, 1.5f);
                    drawList->AddLine(ImVec2(e.sx-18, e.sy), ImVec2(e.sx+18, e.sy), bull, 1.0f);
                    drawList->AddLine(ImVec2(e.sx, e.sy-18), ImVec2(e.sx, e.sy+18), bull, 1.0f);
                }
            }
            for (auto& e : espEntries) {
                if (!e.hasSkeleton) continue;
                for (int ci = 0; ci < SKELETON_CONNECTION_COUNT; ci++) {
                    int from = SKELETON_CONNECTIONS[ci][0];
                    int to = SKELETON_CONNECTIONS[ci][1];
                    if (!e.bones[from].valid || !e.bones[to].valid) continue;
                    // Sanity: skip lines between bones whose screen positions
                    // are absurdly far apart (weird non-humanoid rigs like
                    // sentinel spawners produce >1000px lines across the whole
                    // screen). Adjacent humanoid bones are always <200px apart.
                    float _dx = e.bones[from].x - e.bones[to].x;
                    float _dy = e.bones[from].y - e.bones[to].y;
                    if (_dx*_dx + _dy*_dy > 250.0f*250.0f) continue;
                    ImU32 boneColor = (e.type == 0)
                        ? IM_COL32(255, 255, 255, 200)
                        : IM_COL32(255, 165, 0, 220);       // orange for mobs
                    drawList->AddLine(
                        ImVec2(e.bones[from].x, e.bones[from].y),
                        ImVec2(e.bones[to].x, e.bones[to].y),
                        boneColor, 1.5f);
                }
                // Count populated slots to decide if we're in raw-fallback mode.
                // Raw fill starts at slot 0 sequentially, so if slots 22+ are
                // set that means the walker didn't match named bones and we
                // dumped every transform. In that case: draw the point cloud
                // capped at 22 to avoid dot-storm, and connect adjacent slots
                // as a chain so it reads as a skeleton not a random cluster.
                int _validTotal = 0, _validHigh = 0;
                for (int bi = 0; bi < 55; bi++)
                    if (e.bones[bi].valid) { _validTotal++; if (bi >= 22) _validHigh++; }
                bool _rawMode = (_validHigh > 3);   // heuristic

                ImU32 jointColor = (e.type == 0)
                    ? IM_COL32(255, 255, 255, 200)
                    : IM_COL32(255, 165, 0, 240);           // orange for mobs
                ImU32 headColor = IM_COL32(255, 50, 50, 255);
                // Cap joint circles to slots 0-21 (canonical bone range).
                // Skip higher slots even in raw mode — dot-storm was choking ESP.
                for (int bi = 0; bi < 22; bi++) {
                    if (!e.bones[bi].valid) continue;
                    ImU32 jc = (bi == 10) ? headColor : jointColor;
                    float jr = (bi == 10) ? 3.0f : 1.8f;
                    drawList->AddCircleFilled(ImVec2(e.bones[bi].x, e.bones[bi].y), jr, jc);
                }
                // Raw-mode: connect adjacent slots as a chain so ghouls with
                // non-humanoid rigs still read as a stick figure. Same 250px
                // sanity cap. Cheap — one line per adjacent-valid pair.
                if (_rawMode) {
                    int _prev = -1;
                    for (int bi = 0; bi < 22; bi++) {
                        if (!e.bones[bi].valid) continue;
                        if (_prev >= 0) {
                            float ddx = e.bones[_prev].x - e.bones[bi].x;
                            float ddy = e.bones[_prev].y - e.bones[bi].y;
                            if (ddx*ddx + ddy*ddy <= 250.0f*250.0f) {
                                drawList->AddLine(
                                    ImVec2(e.bones[_prev].x, e.bones[_prev].y),
                                    ImVec2(e.bones[bi].x,    e.bones[bi].y),
                                    jointColor, 1.0f);
                            }
                        }
                        _prev = bi;
                    }
                }
            }
        }
    }

    g_aimbotActive.store((GetAsyncKeyState(g_aimbotActivationKey) & 0x8000) != 0);
    apply_aimbot(espEntries);

    if (g_aimbotEnabled.load() && g_aimbotDrawFOV.load()) {
        ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        ImGui::GetBackgroundDrawList()->AddCircle(center, g_aimbotFOV, IM_COL32(255, 255, 255, 80), 64);
    }

    if (g_espEnabled.load()) {
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(displaySize.x - 250, displaySize.y - 250), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(230, 230), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(120, 120), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags radarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav;
        if (!g_menuVisible)
            radarFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

        ImGui::Begin("##Radar", nullptr, radarFlags);

        ImVec2 wPos = ImGui::GetWindowPos();
        ImVec2 wSize = ImGui::GetWindowSize();
        float side = (wSize.x < wSize.y) ? wSize.x : wSize.y;
        float radarRadius = side * 0.48f;
        ImVec2 radarCenter(wPos.x + wSize.x * 0.5f, wPos.y + wSize.y * 0.5f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddCircleFilled(radarCenter, radarRadius, IM_COL32(0, 0, 0, 140), 64);
        drawList->AddCircle(radarCenter, radarRadius, IM_COL32(100, 100, 100, 180), 64);
        drawList->AddCircle(radarCenter, radarRadius * 0.5f, IM_COL32(60, 60, 60, 120), 32);
        drawList->AddLine(
            ImVec2(radarCenter.x - radarRadius, radarCenter.y),
            ImVec2(radarCenter.x + radarRadius, radarCenter.y),
            IM_COL32(60, 60, 60, 120));
        drawList->AddLine(
            ImVec2(radarCenter.x, radarCenter.y - radarRadius),
            ImVec2(radarCenter.x, radarCenter.y + radarRadius),
            IM_COL32(60, 60, 60, 120));

        if (g_espShowSelf.load()) {
            drawList->AddCircleFilled(radarCenter, 4.0f, IM_COL32(0, 255, 0, 255));
            // Facing arrow — radar is view-locked so player forward = screen up.
            ImVec2 apex (radarCenter.x,        radarCenter.y - 18.0f);
            ImVec2 baseL(radarCenter.x - 3.0f, radarCenter.y -  8.0f);
            ImVec2 baseR(radarCenter.x + 3.0f, radarCenter.y -  8.0f);
            drawList->AddTriangleFilled(apex, baseR, baseL, IM_COL32(120, 255, 120, 255));
        }

        float playerAbsX = g_playerPos.cx * CHUNK_SIZE + g_playerPos.x;
        float playerAbsZ = g_playerPos.cy * CHUNK_SIZE + g_playerPos.z;
        float range = g_radarRange;
        float scale = radarRadius / range;
        bool showPlayers = g_espShowPlayers.load();
        bool showMobs = g_espShowMobs.load();
        bool showWalkers = g_espShowWalkers.load();

        float cosYaw = 1.0f, sinYaw = 0.0f;
        if (g_cameraGetMain && g_getTransform && g_getForward) {
            seh_get_camera_yaw(&cosYaw, &sinYaw);
        }
        // User-tunable additional rotation offset (compose atop yaw)
        {
            float extraRad = g_radarRotationOffsetDeg.load() * 3.14159265f / 180.0f;
            float c2 = cosf(extraRad), s2 = sinf(extraRad);
            float nc = cosYaw * c2 - sinYaw * s2;
            float ns = cosYaw * s2 + sinYaw * c2;
            cosYaw = nc; sinYaw = ns;
        }

        struct RadarSnap {
            WorldVector pos;
            std::string displayName;
            bool isPlayer;
            bool isMob;
            bool isWalker;
            bool isSentinel;
        };
        static std::vector<RadarSnap> radarSnaps;
        radarSnaps.clear();
        bool showItems = g_espShowItems.load();
        bool showSentinels = g_espShowSentinels.load();

        EnterCriticalSection(&g_itemsLock);
        radarSnaps.reserve(g_items.size());
        for (auto& item : g_items) {
            // Sentinels bypass all other category filters — dedicated toggle.
            if (item.isSentinel) {
                if (!showSentinels) continue;
                RadarSnap rs;
                rs.pos = item.pos;
                rs.displayName = item.displayName;
                rs.isPlayer = false; rs.isMob = false; rs.isWalker = false;
                rs.isSentinel = true;
                radarSnaps.push_back(std::move(rs));
                continue;
            }
            bool isPlayer = (item.name.rfind("PlayerAvatar", 0) == 0);
            bool isWalker = !isPlayer && (item.name.rfind("EXPEDITION_WALKER", 0) == 0);
            bool isMob = !isPlayer && !isWalker && item.isCreature;
            bool isItem = (!isPlayer && !isMob && !isWalker);
            if (isPlayer && !showPlayers) continue;
            if (isMob && !showMobs) continue;
            if (isWalker && !showWalkers) continue;
            if (isItem && !showItems) continue;

            RadarSnap rs;
            rs.pos = item.pos;
            if (isPlayer) rs.displayName = item.displayName;
            rs.isPlayer = isPlayer;
            rs.isMob = isMob;
            rs.isWalker = isWalker;
            rs.isSentinel = false;
            radarSnaps.push_back(std::move(rs));
        }
        LeaveCriticalSection(&g_itemsLock);

        for (auto& item : radarSnaps) {
            float absX = item.pos.cx * CHUNK_SIZE + item.pos.x;
            float absZ = item.pos.cy * CHUNK_SIZE + item.pos.z;

            float dx = absX - playerAbsX;
            float dz = absZ - playerAbsZ;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > range) continue;

            float rotX = dx * cosYaw - dz * sinYaw;
            float rotZ = dx * sinYaw + dz * cosYaw;
            float rx = radarCenter.x + rotX * scale;
            float ry = radarCenter.y - rotZ * scale;

            float clampDx = rx - radarCenter.x;
            float clampDy = ry - radarCenter.y;
            float clampDist = sqrtf(clampDx * clampDx + clampDy * clampDy);
            if (clampDist > radarRadius - 4.0f) {
                float cs = (radarRadius - 4.0f) / clampDist;
                rx = radarCenter.x + clampDx * cs;
                ry = radarCenter.y + clampDy * cs;
            }

            ImU32 dotColor;
            float dotSize;
            if (item.isSentinel) { dotColor = IM_COL32(255, 40, 40, 255); dotSize = 4.0f; }
            else if (item.isWalker) { dotColor = IM_COL32(0, 200, 255, 255); dotSize = 5.0f; }
            else if (item.isPlayer) { dotColor = IM_COL32(255, 50, 50, 255); dotSize = 4.0f; }
            else if (item.isMob) { dotColor = IM_COL32(255, 165, 0, 255); dotSize = 3.0f; }
            else { dotColor = IM_COL32(140, 200, 60, 200); dotSize = 2.5f; }
            drawList->AddCircleFilled(ImVec2(rx, ry), dotSize, dotColor);

            // Sentinel detection ring on the radar — scale detection radius
            // to radar pixels the same way entity distances are scaled.
            if (item.isSentinel) {
                float sensR = g_sentinelRadius.load() * scale;
                drawList->AddCircle(ImVec2(rx, ry), sensR, IM_COL32(255, 40, 40, 220), 48, 1.5f);
            }

            char distLabel[64];
            if (item.isSentinel)
                snprintf(distLabel, sizeof(distLabel), "Sentinel %.0fm", dist);
            else if (item.isPlayer && !item.displayName.empty())
                snprintf(distLabel, sizeof(distLabel), "%s %.0fm", item.displayName.c_str(), dist);
            else
                snprintf(distLabel, sizeof(distLabel), "%.0fm", dist);
            drawList->AddText(ImVec2(rx + 6, ry - 6), IM_COL32(255, 255, 255, 200), distLabel);
        }

        // Storm circles overlay — draw AFTER dots so they layer on top.
        if (g_espShowStormCircles.load()) {
            EnterCriticalSection(&g_stormLock);
            std::vector<StormCircle> stormCopy(g_stormCircles);
            LeaveCriticalSection(&g_stormLock);
            for (auto& sc : stormCopy) {
                float sdx = sc.absX - playerAbsX;
                float sdz = sc.absZ - playerAbsZ;
                float srotX = sdx * cosYaw - sdz * sinYaw;
                float srotZ = sdx * sinYaw + sdz * cosYaw;
                float scx = radarCenter.x + srotX * scale;
                float scy = radarCenter.y - srotZ * scale;
                float sr  = sc.radius * scale;
                ImU32 col = sc.isDestination
                    ? IM_COL32(255, 100, 100, 220)   // red = destination (future)
                    : IM_COL32(255, 200, 0, 200);    // amber = current
                drawList->AddCircle(ImVec2(scx, scy), sr, col, 64, 2.0f);
                drawList->AddCircleFilled(ImVec2(scx, scy), 3.0f, col);
                char lbl[32];
                snprintf(lbl, sizeof(lbl), sc.isDestination ? "->Storm#%d" : "Storm#%d", sc.phaseIdx);
                drawList->AddText(ImVec2(scx + 4, scy + 4), col, lbl);
            }
        }

        if (g_menuVisible) {
            char rangeLabel[64];
            snprintf(rangeLabel, sizeof(rangeLabel), "%.0fm", range);
            drawList->AddText(
                ImVec2(radarCenter.x - radarRadius, radarCenter.y - radarRadius - 14),
                IM_COL32(200, 200, 200, 200), "ESP Radar");
            drawList->AddText(
                ImVec2(radarCenter.x + radarRadius - 40, radarCenter.y + radarRadius + 2),
                IM_COL32(150, 150, 150, 180), rangeLabel);
        }

        ImGui::End();
    }

    {
        static int s_saveCounter = 0;
        if (++s_saveCounter >= 120) {
            s_saveCounter = 0;
            save_settings();
        }
    }

    safe_imgui_render_and_present_overlay();

    if (s_frameCount < 5) { dbglog("[frame %d] calling original present\n", s_frameCount); }
    HRESULT hr = g_originalPresent(pSwapChain, SyncInterval, Flags);
    if (s_frameCount < 5) { dbglog("[frame %d] original present returned 0x%lX\n", s_frameCount, hr); s_frameCount++; }
    return hr;
}

static HRESULT STDMETHODCALLTYPE hooked_resize_buffers(IDXGISwapChain* pSwapChain, UINT BufferCount,
    UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    __try { release_render_target(); } __except(EXCEPTION_EXECUTE_HANDLER) {
        dbglog("[resize] SEH in release_render_target: 0x%08lX\n", GetExceptionCode());
    }
    HRESULT hr = E_FAIL;
    __try {
        hr = g_originalResize(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        dbglog("[resize] SEH in original resize: 0x%08lX\n", GetExceptionCode());
    }
    __try { create_render_target(pSwapChain); } __except(EXCEPTION_EXECUTE_HANDLER) {
        dbglog("[resize] SEH in create_render_target: 0x%08lX\n", GetExceptionCode());
    }

    __try {
        if (g_streamProof.load() && g_overlaySwapChain && g_overlayHwnd && g_gameHwnd) {
            if (g_overlayRTV) { g_overlayRTV->Release(); g_overlayRTV = nullptr; }
            RECT gr;
            GetWindowRect(g_gameHwnd, &gr);
            UINT w = gr.right - gr.left;
            UINT h = gr.bottom - gr.top;
            if (w > 0 && h > 0) {
                g_overlaySwapChain->ResizeBuffers(2, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
                ID3D11Texture2D* buf = nullptr;
                g_overlaySwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&buf);
                if (buf) {
                    g_overlayDevice->CreateRenderTargetView(buf, nullptr, &g_overlayRTV);
                    buf->Release();
                }
                MoveWindow(g_overlayHwnd, gr.left, gr.top, w, h, FALSE);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        dbglog("[resize] SEH in stream proof: 0x%08lX\n", GetExceptionCode());
    }

    return hr;
}

static void* alloc_near(void* target, size_t size) {
    uintptr_t addr = (uintptr_t)target;
    uintptr_t lo = (addr > 0x7FFF0000ULL) ? addr - 0x7FFF0000ULL : 0;
    uintptr_t hi = addr + 0x7FFF0000ULL;
    for (uintptr_t try_addr = (addr & ~0xFFFFULL) - 0x10000; try_addr >= lo; try_addr -= 0x10000) {
        void* p = VirtualAlloc((void*)try_addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    for (uintptr_t try_addr = (addr & ~0xFFFFULL) + 0x10000; try_addr <= hi; try_addr += 0x10000) {
        void* p = VirtualAlloc((void*)try_addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return nullptr;
}

static bool find_swapchain_vtable(void** outPresent, void** outResize, void*** outVtable) {
    HMODULE dxgi = GetModuleHandleA("dxgi.dll");
    if (!dxgi) {
        dbglog("[vtable_scan] dxgi.dll not loaded\n");
        return false;
    }

    uintptr_t base = (uintptr_t)dxgi;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    DWORD numSections = nt->FileHeader.NumberOfSections;
    uintptr_t imageEnd = base + nt->OptionalHeader.SizeOfImage;

    dbglog("[vtable_scan] dxgi base: %p, end: %p (SizeOfImage: 0x%X)\n",
        (void*)base, (void*)imageEnd, nt->OptionalHeader.SizeOfImage);

    uintptr_t textStart = 0, textEnd = 0;
    uintptr_t rdataStart = 0, rdataEnd = 0;

    for (DWORD i = 0; i < numSections; i++) {
        if (memcmp(sec[i].Name, ".text", 5) == 0) {
            textStart = base + sec[i].VirtualAddress;
            textEnd = textStart + sec[i].Misc.VirtualSize;
        }
        if (memcmp(sec[i].Name, ".rdata", 6) == 0) {
            rdataStart = base + sec[i].VirtualAddress;
            rdataEnd = rdataStart + sec[i].Misc.VirtualSize;
        }
    }

    if (!textStart || !rdataStart) {
        dbglog("[vtable_scan] failed to find .text or .rdata section\n");
        return false;
    }

    dbglog("[vtable_scan] .text: %p - %p (%zu bytes)\n",
        (void*)textStart, (void*)textEnd, (size_t)(textEnd - textStart));
    dbglog("[vtable_scan] .rdata: %p - %p (%zu bytes)\n",
        (void*)rdataStart, (void*)rdataEnd, (size_t)(rdataEnd - rdataStart));

    const int MAX_HOOKED = 32;
    uintptr_t hookedFuncs[MAX_HOOKED];
    int numHooked = 0;

    for (uintptr_t addr = textStart; addr < textEnd - 5; addr++) {
        uint8_t* p = (uint8_t*)addr;
        if (p[0] != 0xE9) continue;

        if (addr > textStart) {
            uint8_t prev = *(p - 1);
            if (prev != 0xCC && prev != 0xC3 && prev != 0xC2) continue;
        }

        int32_t disp = *(int32_t*)(p + 1);
        uintptr_t target = addr + 5 + (intptr_t)disp;

        if (target >= base && target < imageEnd) continue;

        if (numHooked < MAX_HOOKED) {
            hookedFuncs[numHooked] = addr;
            dbglog("[vtable_scan] phase1: hooked func #%d at %p (RVA 0x%X) -> %p\n",
                numHooked, (void*)addr, (DWORD)(addr - base), (void*)target);
            numHooked++;
        }
    }

    dbglog("[vtable_scan] phase1 complete: %d BE-hooked functions found\n", numHooked);

    if (numHooked == 0) {
        dbglog("[vtable_scan] no BE-hooked functions found\n");
        return false;
    }

    const int VTABLE_SIZE = 18;
    void** bestVtable = nullptr;
    void** fallbackVtable = nullptr;

    for (int h = 0; h < numHooked; h++) {
        uintptr_t hookedAddr = hookedFuncs[h];

        for (uintptr_t scan = rdataStart; scan + sizeof(void*) <= rdataEnd; scan += sizeof(void*)) {
            uintptr_t val = *(uintptr_t*)scan;
            if (val != hookedAddr) continue;

            uintptr_t vtableBase = scan - 8 * sizeof(void*);
            if (vtableBase < rdataStart) continue;
            if (vtableBase + VTABLE_SIZE * sizeof(void*) > rdataEnd) continue;

            void** vtable = (void**)vtableBase;
            bool allValid = true;
            for (int j = 0; j < VTABLE_SIZE; j++) {
                uintptr_t entry = (uintptr_t)vtable[j];
                if (entry < textStart || entry >= textEnd) {
                    allValid = false;
                    break;
                }
            }
            if (!allValid) continue;

            bool slot13hooked = false;
            uintptr_t slot13addr = (uintptr_t)vtable[13];
            for (int k = 0; k < numHooked; k++) {
                if (hookedFuncs[k] == slot13addr) {
                    slot13hooked = true;
                    break;
                }
            }

            dbglog("[vtable_scan] phase2: vtable at %p (from hooked#%d at [8]=%p), [13]=%p hooked:%s\n",
                (void*)vtable, h, (void*)hookedAddr, (void*)vtable[13], slot13hooked ? "Y" : "N");

            if (slot13hooked && !bestVtable) {
                bestVtable = vtable;
            }
            if (!fallbackVtable) {
                fallbackVtable = vtable;
            }
        }
    }

    if (bestVtable) {
        dbglog("[vtable_scan] winner (both [8]+[13] hooked): vtable at %p\n", (void*)bestVtable);
        *outPresent = bestVtable[8];
        *outResize = bestVtable[13];
        *outVtable = bestVtable;
        return true;
    }

    if (fallbackVtable) {
        dbglog("[vtable_scan] WARNING: no vtable with both [8]+[13] hooked, using fallback\n");
        dbglog("[vtable_scan] fallback vtable at %p, [8]=%p [13]=%p\n",
            (void*)fallbackVtable, (void*)fallbackVtable[8], (void*)fallbackVtable[13]);
        *outPresent = fallbackVtable[8];
        *outResize = fallbackVtable[13];
        *outVtable = fallbackVtable;
        return true;
    }

    dbglog("[vtable_scan] no vtable found with any hooked function at index [8]\n");
    return false;
}

static int x64_insn_len(const uint8_t* code) {
    const uint8_t* p = code;

    for (;;) {
        uint8_t b = *p;
        if ((b >= 0x40 && b <= 0x4F) || b == 0x66 || b == 0x67 ||
            b == 0xF0 || b == 0xF2 || b == 0xF3 ||
            b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26 ||
            b == 0x64 || b == 0x65) { p++; continue; }
        break;
    }

    bool hasREXW = false;
    for (const uint8_t* r = code; r < p; r++) {
        if ((*r & 0xF8) == 0x48) hasREXW = true;
    }

    uint8_t op = *p++;

    if ((op & 0xF0) == 0x50) return (int)(p - code);
    if (op == 0x90 || op == 0xC3 || op == 0xCC || op == 0xC9) return (int)(p - code);
    if (op == 0xE8 || op == 0xE9) return (int)(p - code) + 4;
    if (op == 0xEB) return (int)(p - code) + 1;
    if ((op >= 0x70 && op <= 0x7F)) return (int)(p - code) + 1;
    if (op == 0x68) return (int)(p - code) + 4;
    if (op == 0x6A) return (int)(p - code) + 1;
    if ((op & 0xF8) == 0xB8) return (int)(p - code) + (hasREXW ? 8 : 4);

    bool twoByteOp = false;
    if (op == 0x0F) {
        op = *p++;
        twoByteOp = true;
        if (op >= 0x80 && op <= 0x8F) return (int)(p - code) + 4;
    }

    uint8_t modrm = *p++;
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;

    if (mod != 3 && rm == 4) p++;

    if (mod == 0 && rm == 5) p += 4;
    else if (mod == 1) p += 1;
    else if (mod == 2) p += 4;

    if (!twoByteOp) {
        if (op == 0x80 || op == 0x83 || op == 0xC0 || op == 0xC1 || op == 0xC6) p += 1;
        else if (op == 0x81 || op == 0xC7 || op == 0x69) p += 4;
        else if (op == 0xF7 && ((modrm >> 3) & 7) <= 1) p += 4;
    }

    return (int)(p - code);
}

static void* build_disk_trampoline(uint8_t* hookedFunc, uint8_t* relayPage, int relayOffset) {
    tlog("build_disk_trampoline: hookedFunc=%p relayOffset=%d\n", hookedFunc, relayOffset);
    HMODULE dxgi = GetModuleHandleA("dxgi.dll");
    if (!dxgi) return nullptr;

    uintptr_t rva = (uintptr_t)hookedFunc - (uintptr_t)dxgi;

    char dllPath[MAX_PATH];
    GetModuleFileNameA(dxgi, dllPath, MAX_PATH);

    HANDLE hFile = CreateFileA(dllPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        dbglog("[trampoline] failed to open %s\n", dllPath);
        return nullptr;
    }

    IMAGE_DOS_HEADER dos;
    DWORD br;
    ReadFile(hFile, &dos, sizeof(dos), &br, nullptr);

    SetFilePointer(hFile, dos.e_lfanew, nullptr, FILE_BEGIN);
    IMAGE_NT_HEADERS nt;
    ReadFile(hFile, &nt, sizeof(nt), &br, nullptr);

    DWORD fileOffset = 0;
    for (DWORD i = 0; i < nt.FileHeader.NumberOfSections; i++) {
        IMAGE_SECTION_HEADER sec;
        ReadFile(hFile, &sec, sizeof(sec), &br, nullptr);
        if (rva >= sec.VirtualAddress && rva < sec.VirtualAddress + sec.Misc.VirtualSize) {
            fileOffset = sec.PointerToRawData + (DWORD)(rva - sec.VirtualAddress);
            break;
        }
    }

    if (!fileOffset) {
        dbglog("[trampoline] RVA 0x%X not found in any section\n", (DWORD)rva);
        CloseHandle(hFile);
        return nullptr;
    }

    uint8_t origBytes[32];
    SetFilePointer(hFile, fileOffset, nullptr, FILE_BEGIN);
    ReadFile(hFile, origBytes, sizeof(origBytes), &br, nullptr);
    tlog("build_disk_trampoline: fileOffset=0x%lX, read %lu bytes from disk\n", (unsigned long)fileOffset, (unsigned long)br);
    CloseHandle(hFile);
    tlog("build_disk_trampoline: file closed, origBytes[0..7]=%02X %02X %02X %02X %02X %02X %02X %02X\n", origBytes[0],origBytes[1],origBytes[2],origBytes[3],origBytes[4],origBytes[5],origBytes[6],origBytes[7]);

    dbglog("[trampoline] RVA 0x%X original bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
        (DWORD)rva,
        origBytes[0], origBytes[1], origBytes[2], origBytes[3],
        origBytes[4], origBytes[5], origBytes[6], origBytes[7],
        origBytes[8], origBytes[9], origBytes[10], origBytes[11],
        origBytes[12], origBytes[13], origBytes[14], origBytes[15]);
    tlog("build_disk_trampoline: dbglog done\n");

    tlog("build_disk_trampoline: starting insn decode loop\n");
    int stolen = 0;
    while (stolen < 5) {
        int len = x64_insn_len(origBytes + stolen);
        if (len <= 0 || len > 15) {
            dbglog("[trampoline] LDE failed at offset %d\n", stolen);
            return nullptr;
        }
        stolen += len;
    }
    tlog("build_disk_trampoline: stolen=%d bytes\n", stolen);

    dbglog("[trampoline] stealing %d bytes\n", stolen);

    uint8_t* tramp = relayPage + relayOffset;
    memcpy(tramp, origBytes, stolen);
    tlog("build_disk_trampoline: memcpy to tramp=%p done\n", tramp);

    tlog("build_disk_trampoline: starting fixup loop\n");
    int pos = 0;
    while (pos < stolen) {
        const uint8_t* insn = origBytes + pos;
        int len = x64_insn_len(insn);

        const uint8_t* q = insn;
        while ((*q >= 0x40 && *q <= 0x4F) || *q == 0x66 || *q == 0x67 ||
               *q == 0xF0 || *q == 0xF2 || *q == 0xF3 ||
               *q == 0x2E || *q == 0x36 || *q == 0x3E || *q == 0x26 ||
               *q == 0x64 || *q == 0x65) q++;

        uint8_t opc = *q++;
        if (opc == 0x0F) opc = *q++;

        bool hasModRM = !((opc & 0xF0) == 0x50 || opc == 0x90 || opc == 0xC3 || opc == 0xCC || opc == 0xC9 ||
                          opc == 0xE8 || opc == 0xE9 || opc == 0xEB || (opc >= 0x70 && opc <= 0x7F) ||
                          opc == 0x68 || opc == 0x6A || (opc & 0xF8) == 0xB8);

        if (hasModRM && (q - insn) < len) {
            uint8_t modrm = *q;
            uint8_t mod = (modrm >> 6) & 3;
            uint8_t rm = modrm & 7;

            if (mod == 0 && rm == 5) {
                int dispOff = (int)(q + 1 - insn);
                if (rm == 4) dispOff++;

                int32_t origDisp = *(int32_t*)(insn + dispOff);
                uintptr_t origAddr = (uintptr_t)hookedFunc + pos + len + origDisp;
                uintptr_t newAddr = (uintptr_t)(tramp + pos + len);
                int32_t newDisp = (int32_t)(origAddr - newAddr);
                *(int32_t*)(tramp + pos + dispOff) = newDisp;

                dbglog("[trampoline] fixed RIP-relative at stolen offset %d: disp %d -> %d\n",
                    pos, origDisp, newDisp);
            }
        }

        if (opc == 0xE8 || opc == 0xE9) {
            int dispOff = (int)(q - insn);
            int32_t origDisp = *(int32_t*)(insn + dispOff);
            uintptr_t origTarget = (uintptr_t)hookedFunc + pos + len + origDisp;
            uintptr_t newIP = (uintptr_t)(tramp + pos + len);
            int64_t newDisp64 = (int64_t)origTarget - (int64_t)newIP;
            if (newDisp64 > INT32_MAX || newDisp64 < INT32_MIN) {
                dbglog("[trampoline] relative JMP/CALL out of range at offset %d\n", pos);
                return nullptr;
            }
            *(int32_t*)(tramp + pos + dispOff) = (int32_t)newDisp64;
            dbglog("[trampoline] fixed relative branch at stolen offset %d\n", pos);
        }

        pos += len;
    }
    tlog("build_disk_trampoline: fixup loop done\n");

    uintptr_t jmpTarget = (uintptr_t)hookedFunc + stolen;
    tramp[stolen] = 0xFF;
    tramp[stolen + 1] = 0x25;
    *(uint32_t*)(tramp + stolen + 2) = 0;
    *(uintptr_t*)(tramp + stolen + 6) = jmpTarget;
    tlog("build_disk_trampoline: tramp complete at %p, jmpTarget=%p\n", tramp, (void*)jmpTarget);

    dbglog("[trampoline] built at %p, stolen=%d, jumps to %p\n",
        tramp, stolen, (void*)jmpTarget);

    return tramp;
}

bool overlay_init() {
    tlog("overlay_init() ENTER\n");
    HMODULE dxgi = GetModuleHandleA("dxgi.dll");
    if (!dxgi) return false;
    tlog("overlay_init: dxgi=%p\n", dxgi);

    uintptr_t dxgiBase = (uintptr_t)dxgi;
    uint8_t* fnPresent = (uint8_t*)(dxgiBase + 0xDAD0);
    uint8_t* fnResize  = (uint8_t*)(dxgiBase + 0x388C0);
    tlog("overlay_init: fnPresent=%p byte=%02X fnResize=%p byte=%02X\n", fnPresent, fnPresent[0], fnResize, fnResize[0]);

    if (fnPresent[0] != 0xE9 || fnResize[0] != 0xE9) {
        dbglog("[overlay_init] fast-path RVA check failed, falling back to scan\n");
        void* pAddr = nullptr;
        void* rAddr = nullptr;
        void** scanVtable = nullptr;
        if (!find_swapchain_vtable(&pAddr, &rAddr, &scanVtable)) {
            dbglog("[overlay_init] vtable scan also FAILED\n");
            return false;
        }
        fnPresent = (uint8_t*)pAddr;
        fnResize  = (uint8_t*)rAddr;
        if (fnPresent[0] != 0xE9 || fnResize[0] != 0xE9) {
            dbglog("[overlay_init] scan result not E9-hooked\n");
            return false;
        }
    }

    g_relayPage = alloc_near(fnPresent, 4096);
    if (!g_relayPage) return false;
    tlog("overlay_init: relayPage=%p\n", g_relayPage);

    uint8_t* relay = (uint8_t*)g_relayPage;
    relay[0] = 0xFF;
    relay[1] = 0x25;
    *(uint32_t*)(relay + 2) = 0;
    *(uintptr_t*)(relay + 6) = (uintptr_t)hooked_present;
    tlog("overlay_init: relay present stub written, hooked_present=%p\n", (void*)hooked_present);

    relay[14] = 0xFF;
    relay[15] = 0x25;
    *(uint32_t*)(relay + 16) = 0;
    *(uintptr_t*)(relay + 20) = (uintptr_t)hooked_resize_buffers;
    tlog("overlay_init: relay resize stub written, hooked_resize=%p\n", (void*)hooked_resize_buffers);

    tlog("overlay_init: calling build_disk_trampoline for Present...\n");
    void* presentTramp = build_disk_trampoline(fnPresent, relay, 32);
    if (!presentTramp) {
        dbglog("[overlay_init] Present trampoline build FAILED\n");
        return false;
    }
    g_originalPresent = (fn_Present)presentTramp;
    tlog("overlay_init: presentTramp=%p\n", presentTramp);

    tlog("overlay_init: calling build_disk_trampoline for Resize...\n");
    void* resizeTramp = build_disk_trampoline(fnResize, relay, 128);
    if (!resizeTramp) {
        dbglog("[overlay_init] Resize trampoline build FAILED\n");
        return false;
    }
    g_originalResize = (fn_ResizeBuffers)resizeTramp;
    tlog("overlay_init: resizeTramp=%p\n", resizeTramp);

    void** vtable = nullptr;
    {
        auto dos = (IMAGE_DOS_HEADER*)dxgiBase;
        auto nt = (IMAGE_NT_HEADERS*)(dxgiBase + dos->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        uintptr_t rdataStart = 0, rdataEnd = 0;
        for (DWORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            if (memcmp(sec[i].Name, ".rdata", 6) == 0) {
                rdataStart = dxgiBase + sec[i].VirtualAddress;
                rdataEnd = rdataStart + sec[i].Misc.VirtualSize;
                break;
            }
        }
        if (!rdataStart) return false;
        for (uintptr_t scan = rdataStart; scan + 18 * sizeof(void*) <= rdataEnd; scan += sizeof(void*)) {
            if (*(uintptr_t*)(scan + 8 * sizeof(void*)) == (uintptr_t)fnPresent) {
                void** candidate = (void**)scan;
                bool allText = true;
                for (int j = 0; j < 18; j++) {
                    uintptr_t e = (uintptr_t)candidate[j];
                    if (e < dxgiBase + 0x1000 || e >= dxgiBase + nt->OptionalHeader.SizeOfImage) {
                        allText = false;
                        break;
                    }
                }
                if (allText) { vtable = candidate; break; }
            }
        }
    }
    if (!vtable) return false;
    tlog("overlay_init: vtable=%p\n", vtable);

    g_vtable = vtable;
    g_vtableOrigPresent = vtable[8];
    g_vtableOrigResize = vtable[13];

    DWORD old;
    VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &old);
    vtable[8] = g_relayPage;
    VirtualProtect(&vtable[8], sizeof(void*), old, &old);

    VirtualProtect(&vtable[13], sizeof(void*), PAGE_READWRITE, &old);
    vtable[13] = (void*)((uint8_t*)g_relayPage + 14);
    VirtualProtect(&vtable[13], sizeof(void*), old, &old);

    dbglog("[overlay_init] vtable-hooked Present=%p Resize=%p relay=%p vtable=%p\n",
        (void*)fnPresent, (void*)fnResize, g_relayPage, (void*)vtable);

    tlog("overlay_init: SUCCESS vtable[8]=%p vtable[13]=%p\n", vtable[8], vtable[13]);
    return true;
}

void overlay_shutdown() {
    if (g_vtable) {
        DWORD old;
        VirtualProtect(&g_vtable[8], sizeof(void*), PAGE_READWRITE, &old);
        g_vtable[8] = g_vtableOrigPresent;
        VirtualProtect(&g_vtable[8], sizeof(void*), old, &old);

        VirtualProtect(&g_vtable[13], sizeof(void*), PAGE_READWRITE, &old);
        g_vtable[13] = g_vtableOrigResize;
        VirtualProtect(&g_vtable[13], sizeof(void*), old, &old);
    }

    Sleep(500);

    destroy_stream_proof_overlay();

    if (g_imguiInitialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }

    if (g_originalWndProc && g_gameHwnd) {
        SetWindowLongPtrA(g_gameHwnd, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
    }

    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

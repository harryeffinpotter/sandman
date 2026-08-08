#include "win.h"
#include "pe_resolve.h"
#include "debug_log.h"

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <tlhelp32.h>

static void wlog(const char* fmt, ...) {
    char tmp[256];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    ringlog::push("[w] %s", tmp);
}

// ---------------------------------------------------------------------------
// RESOLVE macro
// ---------------------------------------------------------------------------
#define RESOLVE(api, mod, name) \
    api.name = (fn_##name)pe_resolve::get_proc(mod, #name);

// ---------------------------------------------------------------------------
// Global variable definitions
// ---------------------------------------------------------------------------
volatile void* g_gameContextModule = nullptr;
volatile void* g_findInteractSystem = nullptr;
volatile bool g_hooked = false;
std::atomic<bool> g_hwbpActive{false};

static IL2CPP_API* g_apiPtr = nullptr;
static IL2CPP_API* get_il2cpp_api() { return g_apiPtr; }
void set_il2cpp_api_ptr(IL2CPP_API* p) { g_apiPtr = p; }

int g_idx_blueprint = -1;
int g_idx_position = -1;
int g_idx_interactible = -1;
int g_idx_interact_target = -1;
int g_idx_parent = -1;
int g_idx_item_type = -1;
int g_idx_interact_not_active = -1;
int g_idx_interactions = -1;
int g_idx_id = -1;
int g_idx_large_item = -1;
int g_idx_overheated = -1;
int g_idx_recoil_look = -1;
int g_idx_stationary_auto = -1;
int g_idx_stationary_data = -1;
int g_idx_weapon_overheat = -1;
int g_idx_weapon_overheat_data = -1;
int g_idx_auto_turret = -1;
int g_idx_bullet_projectile_data = -1;
int g_idx_health_data = -1;
int g_idx_invincible = -1;
int g_idx_speed_data = -1;
int g_idx_jump = -1;
int g_idx_jump_delay = -1;
int g_idx_fall_damage = -1;
int g_idx_ammo = -1;
int g_idx_inv_ammo = -1;
int g_idx_cheat_walker_fly = -1;
int g_idx_anticheat = -1;                // AntiCheat component (idx 27 per LO's game)
int g_idx_anticheat_noclip_ignore = -1;  // AntiCheatNoClipIgnore (idx 28)
int g_idx_anticheat_speedcap = -1;       // AntiCheatSpeedCapData (idx 29)
int g_idx_dont_destroy_in_storm = -1;    // DontDestroyInStorm (idx 134)
int g_idx_sandstorm_data = -1;           // SandStormData (359)
int g_idx_sandstorm_destination = -1;    // SandStormDestination (360)
int g_idx_extraction_point = -1;         // ExtractionPointData (166)
int g_idx_final_extraction = -1;         // FinalExtractionPointData (172)
int g_idx_extraction_box = -1;           // ExtractionBox (163)
int g_idx_extraction_ship = -1;          // ExtractionShipData (167)
int g_idx_extraction_progress = -1;      // ExtractionInProgress (164)
int g_idx_extraction_landing = -1;       // ExtractionLandingPoint (165)
int g_idx_contract_info = -1;            // ContractInfoData (81)
int g_idx_walker_engine = -1;            // WalkerEngineData (447)
int g_idx_reactor_data = -1;             // ReactorData (337)
int g_idx_reactor_state = -1;            // ReactorState (342)
int g_idx_reactor_turbo = -1;            // ReactorTurboState (343)
int g_idx_health_normalized = -1;        // HealthNormalizedComponent
int g_idx_in_eye_of_storm = -1;          // InEyeOfStorm
int g_idx_switchable_radial = -1;        // SwitchableRadialViewBehaviour (569)
int g_idx_current_slot_id = -1;          // CurrentSlotId (99) — what slot player has selected
int g_idx_previous_slot_id = -1;         // PreviousSlotId (317)
int g_idx_inventory_data = -1;           // InventoryData (219)
int g_idx_inventory_entity_id = -1;      // InventoryEntityId (220)
int g_idx_inventory_slot_data = -1;      // InventorySlotData (225)
int g_idx_inventory_item_id = -1;        // InventoryItemId (223)
int g_idx_inventory_item_count = -1;     // InventoryItemCount (222)
int g_idx_inventory_item_slot_index = -1;// InventoryItemSlotIndex (224)
int g_idx_recently_updated_slot = -1;    // RecentlyUpdatedInventorySlot (345)
int g_idx_reactor_slot = -1;             // ReactorSlot (340)
std::atomic<bool> g_autoRelockDupe{true};// re-lock same item name when it re-appears
std::string g_lastDupedName;             // remembered across relocks
CRITICAL_SECTION g_lastDupedNameCS;
static bool g_lastDupedNameInit = false;
void ensure_last_duped_name_cs() {
    if (!g_lastDupedNameInit) { InitializeCriticalSection(&g_lastDupedNameCS); g_lastDupedNameInit = true; }
}
std::atomic<bool> g_dupeSuspended{false};   // F9 hotkey (rebindable) toggles this
// User-rebindable hotkey bindings. Defaults match previous hardcoded F-keys.
// Overlay UI has a "Bind hotkey" button per action; next key press assigns.
std::atomic<int> g_hotkeyHardKill{VK_F12};
std::atomic<int> g_hotkeyDupeSuspend{VK_F9};
std::atomic<int> g_hotkeyDupeMaster{VK_F10};
std::atomic<int> g_hotkeyPlaybackFirst{VK_F7};
std::atomic<int> g_hotkeyCaptureRequest{0};  // set to feature index when UI asks user to press a key

// Countdown scheduler — every Dupe Lab button can be "armed" for delayed
// fire, so LO can click in menu then Esc-close and let the countdown fire
// the action in-game with server live (Esc pauses game = server ignores).
std::atomic<int>          g_actionDelaySec{3};      // default 3 seconds
std::atomic<int>          g_recordDurationSec{5};   // (unused now)
std::atomic<unsigned long long> g_recordAutoStopDeadline{0}; // (unused now)
std::atomic<int>          g_hotkeyRecordToggle{VK_DELETE}; // toggle armed recording
static std::string        g_armedRecordName;               // set by UI arm button
std::atomic<float>        g_radarRotationOffsetDeg{0.0f};  // user-tunable radar alignment
std::atomic<int>          g_pendingActionId{0};     // 0 = no pending action
std::atomic<unsigned long long> g_pendingActionDeadline{0};
// Game-render-thread heartbeat. Updated inside hooked_present. Worker
// thread checks it — if gap > 5s = game render thread frozen. Old
// heartbeat was on OUR worker thread so it stayed 'alive' during freezes.
std::atomic<unsigned long long> g_lastPresentTick{0};
// Recorded name for pending playback (only for id == 100 = playback-by-name)
static char g_pendingActionArg[64] = {0};
static CRITICAL_SECTION g_pendingActionCS;
static bool g_pendingActionCSInit = false;
static void ensure_pending_cs() {
    if (!g_pendingActionCSInit) { InitializeCriticalSection(&g_pendingActionCS); g_pendingActionCSInit = true; }
}

// Schedule action id to fire after g_actionDelaySec seconds. arg is optional
// (only used for playback which needs the recording name). Called from UI.
void dupelab_schedule(int actionId, const char* arg) {
    ensure_pending_cs();
    EnterCriticalSection(&g_pendingActionCS);
    if (arg) strncpy_s(g_pendingActionArg, sizeof(g_pendingActionArg), arg, _TRUNCATE);
    else g_pendingActionArg[0] = 0;
    LeaveCriticalSection(&g_pendingActionCS);
    int delayMs = g_actionDelaySec.load() * 1000;
    g_pendingActionDeadline.store(GetTickCount64() + (unsigned long long)delayMs);
    g_pendingActionId.store(actionId);
    wlog("[dupelab] scheduled action id=%d arg='%s' fire in %d sec\n",
         actionId, arg ? arg : "", g_actionDelaySec.load());
}

// Called from worker thread every scan. Fires the pending action if deadline
// reached. IDs map to specific dupelab_* functions.
void dupelab_check_pending() {
    int id = g_pendingActionId.load();
    if (id == 0) return;
    unsigned long long now = GetTickCount64();
    unsigned long long deadline = g_pendingActionDeadline.load();
    if (now < deadline) return;
    g_pendingActionId.store(0);
    char arg[64] = {0};
    ensure_pending_cs();
    EnterCriticalSection(&g_pendingActionCS);
    strncpy_s(arg, sizeof(arg), g_pendingActionArg, _TRUNCATE);
    LeaveCriticalSection(&g_pendingActionCS);
    wlog("[dupelab] firing scheduled action id=%d arg='%s'\n", id, arg);
    switch (id) {
        case 1:  dupelab_spoof_type_on_locked(g_dupeSpoofType.load()); break;
        case 2:  dupelab_force_slot_on_locked(g_dupeForceHandSlot.load()); break;
        case 3:  dupelab_strip_interactible_not_active_on_locked(); break;
        case 4:  dupelab_spoof_type_on_locked(g_dupeSpoofType.load());
                 dupelab_force_slot_on_locked(g_dupeForceHandSlot.load()); break;
        case 5:  dupelab_spoof_type_on_locked(g_dupeSpoofType.load());
                 dupelab_force_slot_on_locked(g_dupeForceHandSlot.load());
                 dupelab_strip_interactible_not_active_on_locked(); break;
        case 100: if (arg[0]) dupelab_playback_cstr(arg); break;
        case 200: if (arg[0]) { dupelab_record_start(std::string(arg)); } break;  // delayed record start
        // more can be added as needed
    }
}

// ---------------------------------------------------------------------------
// Steam API cache — GetFriendPersonaName resolves a SteamID64 to the real
// platform display name ("JimmyBob"). Loaded at boot from steam_api64.dll.
// ---------------------------------------------------------------------------
typedef void* (*fn_steam_friends_get)();
typedef const char* (*fn_get_friend_name)(void* iface, unsigned long long steamId);
typedef bool  (*fn_request_user_info)(void* iface, unsigned long long steamId, bool nameOnly);
static fn_steam_friends_get g_pSteamFriendsGet = nullptr;
static fn_get_friend_name   g_pGetFriendName   = nullptr;
static fn_request_user_info g_pRequestUserInfo = nullptr;
static void*                g_steamFriendsIface = nullptr;
static std::unordered_map<unsigned long long, std::string> g_steamNameCache;
static CRITICAL_SECTION g_steamNameCacheCS;
static bool g_steamCacheInit = false;

void steam_names_init() {
    if (g_steamCacheInit) return;
    g_steamCacheInit = true;
    InitializeCriticalSection(&g_steamNameCacheCS);
    HMODULE hm = GetModuleHandleA("steam_api64.dll");
    if (!hm) {
        wlog("[steam-names] steam_api64.dll not loaded — skipping\n");
        return;
    }
    // Try common Steam API interface accessor versions (SDK bumps sometimes)
    static const char* friendsAccessors[] = {
        "SteamAPI_SteamFriends_v017", "SteamAPI_SteamFriends_v018",
        "SteamAPI_SteamFriends_v016", "SteamAPI_SteamFriends_v015",
    };
    for (auto* n : friendsAccessors) {
        g_pSteamFriendsGet = (fn_steam_friends_get)GetProcAddress(hm, n);
        if (g_pSteamFriendsGet) { wlog("[steam-names] found accessor: %s\n", n); break; }
    }
    g_pGetFriendName = (fn_get_friend_name)GetProcAddress(hm, "SteamAPI_ISteamFriends_GetFriendPersonaName");
    if (!g_pGetFriendName) {
        wlog("[steam-names] SteamAPI_ISteamFriends_GetFriendPersonaName not exported\n");
        return;
    }
    // RequestUserInformation kicks off a network fetch of a non-friend's
    // persona. Without this, GetFriendPersonaName returns empty for any
    // player who isn't already in Steam's cache (i.e. most lobby randoms).
    g_pRequestUserInfo = (fn_request_user_info)GetProcAddress(hm, "SteamAPI_ISteamFriends_RequestUserInformation");
    wlog("[steam-names] RequestUserInformation=%p\n", (void*)g_pRequestUserInfo);
    if (g_pSteamFriendsGet) {
        __try { g_steamFriendsIface = g_pSteamFriendsGet(); }
        __except(EXCEPTION_EXECUTE_HANDLER) { g_steamFriendsIface = nullptr; }
    }
    wlog("[steam-names] iface=%p getName=%p\n", g_steamFriendsIface, (void*)g_pGetFriendName);
}

// Isolated RequestUserInformation caller — plain C, no C++ unwinding.
static void seh_steam_request_user_info(unsigned long long steamId) {
    if (!g_pRequestUserInfo || !g_steamFriendsIface) return;
    __try { g_pRequestUserInfo(g_steamFriendsIface, steamId, true); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// __try can't sit in a function that has std::string/unordered_map locals
// (C2712), so the raw Steam API call is isolated in a plain C-style helper
// that only touches primitives + fixed-size char buffer.
static bool seh_steam_call_get_name(unsigned long long steamId, char* outBuf, int outCap) {
    if (!outBuf || outCap <= 0) return false;
    outBuf[0] = 0;
    if (!g_pGetFriendName || !g_steamFriendsIface) return false;
    __try {
        const char* name = g_pGetFriendName(g_steamFriendsIface, steamId);
        if (name && name[0]) {
            strncpy_s(outBuf, outCap, name, _TRUNCATE);
            return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

std::string get_steam_name(unsigned long long steamId) {
    if (steamId == 0) return {};
    EnterCriticalSection(&g_steamNameCacheCS);
    auto it = g_steamNameCache.find(steamId);
    if (it != g_steamNameCache.end()) {
        std::string r = it->second;
        LeaveCriticalSection(&g_steamNameCacheCS);
        return r;
    }
    LeaveCriticalSection(&g_steamNameCacheCS);
    char nameBuf[128];
    std::string result;
    if (seh_steam_call_get_name(steamId, nameBuf, sizeof(nameBuf))) {
        result = nameBuf;
    }
    // If Steam returned empty, kick a RequestUserInformation for this ID
    // so Steam fetches the persona from network. The name will be
    // available on a subsequent scan. Without this, non-friends never
    // resolve. Track requested IDs so we don't spam per-scan.
    if (result.empty() && g_pRequestUserInfo && g_steamFriendsIface) {
        static std::unordered_map<unsigned long long, DWORD> s_requested;
        DWORD now = GetTickCount();
        auto rit = s_requested.find(steamId);
        // Retry only every 5 seconds if a prior request didn't yield a name
        if (rit == s_requested.end() || (now - rit->second) > 5000) {
            s_requested[steamId] = now;
            seh_steam_request_user_info(steamId);
        }
    }
    // Cache HITS only. Empty results retry until Steam has the name.
    if (!result.empty()) {
        EnterCriticalSection(&g_steamNameCacheCS);
        g_steamNameCache[steamId] = result;
        LeaveCriticalSection(&g_steamNameCacheCS);
    }
    return result;
}
int g_idx_cheat_walker_speed = -1;
int g_idx_shot_info = -1;
int g_idx_nice_name = -1;
int g_idx_account_id = -1;
int g_idx_view = -1;
int g_idx_view_position = -1;
int g_idx_view_data = -1;
int g_idx_char_ctrl_vb = -1;
int g_idx_fps_ctrl_vb = -1;
int g_idx_mob_vb = -1;
int g_idx_simple_anim_vb = -1;
int g_idx_mob_state = -1;
int g_idx_mob_ghoul = -1;
int g_idx_mob_ls = -1;
int g_idx_mob_ls_jr = -1;
int g_idx_ai_agent = -1;
int g_idx_user_name = -1;

std::vector<ItemInfo> g_items;
CRITICAL_SECTION g_itemsLock;

std::atomic<bool> g_permaLockActive{false};
std::string g_permaLockName;
std::atomic<int> g_lockedEntityId{-1};
std::atomic<bool> g_dupeMode{false};
std::atomic<bool> g_stickyLock{false};
std::atomic<bool> g_weaponFilter{false};
std::atomic<bool> g_heavyBypass{false};
std::atomic<bool> g_turretNoOverheat{false};
std::atomic<bool> g_turretRapidFire{false};
std::atomic<bool>  g_turretNoRecoil{false};
// Recoil strength multiplier — 0.0 = zero (same as no-recoil-checkbox),
// 1.0 = normal, values between = reduced but visible. Only applied when
// != 1.0. When ==1.0, we skip writes entirely so the game accumulates
// normally on next shot (fixes the "recoil never comes back after
// toggling no-recoil off" issue: previously we memset 48 zero bytes
// per tick which permanently pinned the vector at zero).
std::atomic<float> g_recoilMult{1.0f};
std::atomic<bool> g_weaponModsEnabled{false};
std::atomic<bool> g_weaponNoDrop{false};
std::atomic<bool> g_weaponNoBloom{false};
std::atomic<float> g_weaponVelocityMult{1.0f};
// Player cheat toggles — apply per scan tick via strip_component:
//   Entitas systems that require these components short-circuit when
//   the component is missing, so stripping them = the effect is bypassed.
std::atomic<bool> g_noFallDamage{false};
std::atomic<bool> g_flyMode{false};
std::atomic<bool> g_lowGravMode{false};
std::atomic<bool> g_heavyFix2{false};        // flip ItemType to weapon on locked large item
std::atomic<bool> g_hooverRequest{false};    // one-shot re-dump trigger
std::atomic<bool> g_hardKillRequested{false};// hotkey → TerminateProcess
std::atomic<int>  g_playerEntityId{-1};      // updated by scan_entities each tick
std::atomic<bool> g_stripAntiCheat{false};   // strip AntiCheat component from our player
std::atomic<bool> g_espShowExtraction{true}; // highlight extraction points in ESP
std::atomic<bool> g_espShowReactors{false};  // highlight ships (entities with ReactorData)
std::atomic<bool> g_stormImmunity{false};    // strip InEyeOfStorm from us so we take no storm dps
std::atomic<bool> g_shipResilience{false};   // force max HP on our ship's ReactorData
std::atomic<float> g_walkerSpeedMult{1.0f};  // WalkerEngineData speed multiplier
std::atomic<bool> g_addNoClipIgnore{false};  // (unused for now — component ADD requires more than strip)
std::atomic<bool> g_stripSpeedCap{false};    // strip AntiCheatSpeedCapData from our player
float g_lootT1Color[4] = {0.4f, 0.9f, 0.4f, 1.0f}; // green
float g_lootT2Color[4] = {0.4f, 0.6f, 1.0f, 1.0f}; // blue
float g_lootT3Color[4] = {1.0f, 0.6f, 0.2f, 1.0f}; // orange
std::atomic<bool> g_noJumpDelay{false};
std::atomic<bool> g_infiniteAmmo{false};
// Jump-force multiplier — 1.0 = default, 2.0 = 2x jump height, etc.
// Applied on the Jump component. Field offset is a probe target: Entitas
// components in this codebase typically store the primary float at +0x10,
// so we start there. If the offset is wrong the multiplier will just
// have no visible effect; adjust JUMP_FORCE_FIELD_OFFSET and rebuild.
std::atomic<float> g_jumpForceMult{1.0f};
static constexpr int JUMP_FORCE_FIELD_OFFSET = 0x10;
// Always-day: writes TimeOfDayManager.currentTime to g_dayTime every tick.
// Range depends on the game — try 0.0-1.0 (normalized) or 0-24 (hour).
// Default 0.5 works if it's normalized (== noon).
std::atomic<bool>  g_alwaysDay{false};
std::atomic<float> g_dayTime{0.5f};
// Speed multiplier — writes to SpeedData component. Field offset guess 0x10.
std::atomic<float> g_speedMult{1.0f};
static constexpr int SPEED_MULT_FIELD_OFFSET = 0x10;
// Walker fly (in-vehicle) — writes bool to CheatWalkerFly component.
std::atomic<bool>  g_walkerFly{false};
static constexpr int WALKER_FLY_FIELD_OFFSET = 0x10;
// Populated at worker init — pointer to TimeOfDayManager singleton.
volatile uintptr_t g_todInstance = 0;
static constexpr int TOD_CURRENTTIME_OFFSET = 0x88;
static constexpr int TOD_PROGRESS_OFFSET    = 0x80;
std::atomic<uintptr_t> g_lockedEntityPtr{0};
std::atomic<uintptr_t> g_cachedRecoilEntity{0};
std::atomic<bool> g_running{true};

WorldVector g_playerPos = {};
void* g_userNameKlass = nullptr;
void* g_userNameHUDKlass = nullptr;
void* g_userNameHUDType  = nullptr;
void* g_userContextModuleInstance = nullptr;
fn_getEntByAcctId g_getUserEntityByAcctId = nullptr;
void* g_userContextModuleKlass = nullptr;
int   g_userNameFieldOffset = -1;
std::atomic<int> g_userNameRescanRequest{0};
std::atomic<int> g_entityCount{0};

std::string g_nameFilter;
int g_scrollOffset = 0;
std::unordered_set<std::string> g_hiddenNames;
std::vector<std::string> g_hiddenPrefixes = { "Mob", "walker_", "EXPEDITION_WALKER" };

Hook g_executeHook;
fn_execute g_original_execute = nullptr;
Hook g_farHook;
Hook g_publishHook;
void* g_holoPublishAddr = nullptr;
void* g_holoMessengerInstance = nullptr;  // set on first Publish call — 'this' ptr from any hook fire
// SendMoveSlot / SendSplitSlot hooks — inventory-slot operations use
// ClientNetworkControllerModule extension methods instead of HoloMessenger.
// Hooking these captures place-on-shelf, swap-slots, split-stack etc.
Hook g_sendMoveSlotHook;
Hook g_sendSplitSlotHook;
Hook g_sendEquipHook;
Hook g_sendDropHook;
void* g_sendMoveSlotAddr  = nullptr;
void* g_sendSplitSlotAddr = nullptr;
void* g_sendEquipAddr     = nullptr;
void* g_sendDropAddr      = nullptr;

// SendMoveSlot(this, byte fromSlot, int fromParent, byte toSlot, int toParent, DateTime timestamp)
typedef void (*fn_send_move_slot)(void* thisPtr, uint8_t fromSlot, int32_t fromParent, uint8_t toSlot, int32_t toParent, uint64_t timestamp);
typedef void (*fn_send_split_slot)(void* thisPtr, uint8_t fromSlot, int32_t fromParent, uint8_t toSlot, int32_t toParent, uint16_t count, uint64_t timestamp);
typedef void (*fn_send_equip)(void* thisPtr, uint8_t slotId, uint64_t timestamp);
typedef void (*fn_send_drop)(void* thisPtr, int32_t entityId, uint8_t slotId, uint64_t timestamp, uint16_t count);

void __fastcall hooked_send_move_slot(void* thisPtr, uint8_t fromSlot, int32_t fromParent, uint8_t toSlot, int32_t toParent, uint64_t timestamp) {
    ringlog::push("[net-send] MoveSlot fs=%u fp=%d ts=%u tp=%d", fromSlot, fromParent, toSlot, toParent);
    __try {
        ((fn_send_move_slot)g_sendMoveSlotHook.trampoline_exec)(thisPtr, fromSlot, fromParent, toSlot, toParent, timestamp);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void __fastcall hooked_send_split_slot(void* thisPtr, uint8_t fromSlot, int32_t fromParent, uint8_t toSlot, int32_t toParent, uint16_t count, uint64_t timestamp) {
    ringlog::push("[net-send] SplitSlot fs=%u fp=%d ts=%u tp=%d cnt=%u", fromSlot, fromParent, toSlot, toParent, count);
    __try {
        ((fn_send_split_slot)g_sendSplitSlotHook.trampoline_exec)(thisPtr, fromSlot, fromParent, toSlot, toParent, count, timestamp);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void __fastcall hooked_send_equip(void* thisPtr, uint8_t slotId, uint64_t timestamp) {
    ringlog::push("[net-send] Equip slot=%u", slotId);
    __try {
        ((fn_send_equip)g_sendEquipHook.trampoline_exec)(thisPtr, slotId, timestamp);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void __fastcall hooked_send_drop(void* thisPtr, int32_t entityId, uint8_t slotId, uint64_t timestamp, uint16_t count) {
    ringlog::push("[net-send] Drop eid=%d slot=%u cnt=%u", entityId, slotId, count);
    __try {
        ((fn_send_drop)g_sendDropHook.trampoline_exec)(thisPtr, entityId, slotId, timestamp, count);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
std::atomic<bool> g_captureMessages{false};  // DEFAULT OFF. LO toggles from UI when actively capturing. On = every Publish call reads msg + writes to perf_capture.dat which can amplify AVs during instability.
static FILE* g_captureFile = nullptr;
static CRITICAL_SECTION g_captureCS;
static bool g_captureCSInit = false;
static void ensure_capture_cs() {
    if (!g_captureCSInit) { InitializeCriticalSection(&g_captureCS); g_captureCSInit = true; }
}

typedef void (*fn_publish)(void* thisPtr, void* msg);
static bool is_readable(const void* ptr, size_t len);  // fwd decl
struct CapturedMsg;
static void seh_dispatch_captured(const CapturedMsg* cm);

// ---------------------------------------------------------------------------
// Dupe Lab recording buffer — snapshot HoloMessages so we can replay them.
// Each capture stores the msg's klass ptr + up to 0x80 bytes of instance data.
// Replay allocates a fresh il2cpp object of the same klass, memcpy's the
// snapshot bytes over the instance, dispatches via Publish.
// ---------------------------------------------------------------------------
struct CapturedMsg {
    void* klass;         // klass pointer at the time of capture
    unsigned char bytes[0x80];
    size_t byteLen;
    DWORD tick;
};
struct Recording {
    std::string name;
    std::vector<CapturedMsg> msgs;
};
static std::unordered_map<std::string, Recording> g_recordings;
static CRITICAL_SECTION g_recCS;
static bool g_recCSInit = false;
static void ensure_rec_cs() {
    if (!g_recCSInit) { InitializeCriticalSection(&g_recCS); g_recCSInit = true; }
}
std::atomic<bool>  g_dupeLabRecording{false};
std::atomic<bool>  g_showContainerContents{false};  // Items panel: show child-entities (Parent!=0)
std::atomic<int>   g_dupeSpoofType{2};              // ItemTypeData.type value for spoof (guess: 2=consumable)
std::atomic<int>   g_dupeForceHandSlot{0};          // InventoryItemSlotIndex.value target
static std::string g_activeRecordingName;  // guarded by g_recCS

void dupelab_record_start(const std::string& name) {
    ensure_rec_cs();
    EnterCriticalSection(&g_recCS);
    g_activeRecordingName = name;
    g_recordings[name].name = name;
    g_recordings[name].msgs.clear();
    LeaveCriticalSection(&g_recCS);
    g_dupeLabRecording.store(true);
}
void dupelab_record_stop() {
    g_dupeLabRecording.store(false);
    ensure_rec_cs();
    EnterCriticalSection(&g_recCS);
    g_activeRecordingName.clear();
    LeaveCriticalSection(&g_recCS);
}

// Arm a recording name so LO can press hotkey (Del) to start it later.
void dupelab_arm_record(const char* name) {
    ensure_rec_cs();
    EnterCriticalSection(&g_recCS);
    g_armedRecordName = name ? std::string(name) : std::string();
    LeaveCriticalSection(&g_recCS);
    ringlog::push("[dupelab] armed record='%s' — press Del to start", name ? name : "");
}

// Hotkey handler: if not recording + armed = start. If recording = stop.
void dupelab_record_hotkey_toggle() {
    if (g_dupeLabRecording.load()) {
        std::string wasName;
        ensure_rec_cs();
        EnterCriticalSection(&g_recCS);
        wasName = g_activeRecordingName;
        LeaveCriticalSection(&g_recCS);
        dupelab_record_stop();
        ringlog::push("[dupelab] STOP record '%s' (via hotkey)", wasName.c_str());
    } else {
        std::string arm;
        ensure_rec_cs();
        EnterCriticalSection(&g_recCS);
        arm = g_armedRecordName;
        LeaveCriticalSection(&g_recCS);
        if (arm.empty()) {
            ringlog::push("[dupelab] hotkey hit but no recording armed");
            return;
        }
        dupelab_record_start(arm);
        ringlog::push("[dupelab] START record '%s' (via hotkey)", arm.c_str());
    }
}
size_t dupelab_recording_count(const std::string& name) {
    ensure_rec_cs();
    EnterCriticalSection(&g_recCS);
    size_t r = g_recordings.count(name) ? g_recordings[name].msgs.size() : 0;
    LeaveCriticalSection(&g_recCS);
    return r;
}
// C-string wrappers — delegate to std::string overloads. Callable from
// hotkey handlers in main.cpp without C2712 (can't construct std::string
// inside a function with __try scope).
size_t dupelab_recording_count_cstr(const char* name) {
    return dupelab_recording_count(std::string(name ? name : ""));
}
void dupelab_playback_cstr(const char* name) {
    dupelab_playback(std::string(name ? name : ""));
}

// Replay: for each captured msg, allocate a new il2cpp object of the same
// klass, memcpy the snapshot bytes over the instance data, dispatch via Publish.
void dupelab_playback(const std::string& name) {
    if (!g_holoPublishAddr) { wlog("[dupelab] playback %s FAILED: Publish addr null\n", name.c_str()); return; }
    IL2CPP_API* api = get_il2cpp_api();
    if (!api || !api->il2cpp_object_new) { wlog("[dupelab] playback %s FAILED: object_new unavailable\n", name.c_str()); return; }
    ensure_rec_cs();
    // Snapshot the list so we don't hold CS across il2cpp calls
    std::vector<CapturedMsg> local;
    EnterCriticalSection(&g_recCS);
    if (g_recordings.count(name)) local = g_recordings[name].msgs;
    LeaveCriticalSection(&g_recCS);
    wlog("[dupelab] playback '%s' — dispatching %zu messages\n", name.c_str(), local.size());
    ringlog::push("[playback:%s] dispatching %zu messages", name.c_str(), local.size());
    IL2CPP_API* api2 = get_il2cpp_api();
    int idx = 0;
    for (auto& cm : local) {
        char kn[128] = "?";
        if (api2 && api2->il2cpp_class_get_name && cm.klass) {
            const char* n = api2->il2cpp_class_get_name(cm.klass);
            if (n) strncpy_s(kn, sizeof(kn), n, _TRUNCATE);
        }
        ringlog::push("[playback:%s] [%d/%zu] dispatching %s", name.c_str(), idx + 1, local.size(), kn);
        seh_dispatch_captured(&cm);
        idx++;
    }
    ringlog::push("[playback:%s] done", name.c_str());
}

// Dupe Lab scenario actions — one-shot writes to the currently-locked
// entity's components. Reuse the g_lockedEntityPtr LO already sets by
// clicking an item in the list. All SEH-safe.
void dupelab_spoof_type_on_locked(int typeValue) {
    uintptr_t ent = g_lockedEntityPtr.load();
    if (!ent || g_idx_item_type < 0) return;
    __try {
        void* c = get_component((void*)ent, g_idx_item_type);
        if (is_readable(c, 0x18)) {
            *(int*)((uintptr_t)c + 0x10) = typeValue;
            wlog("[dupelab] type-spoofed locked entity to type=%d\n", typeValue);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void dupelab_force_slot_on_locked(int slotValue) {
    uintptr_t ent = g_lockedEntityPtr.load();
    if (!ent || g_idx_inventory_item_slot_index < 0) return;
    __try {
        void* c = get_component((void*)ent, g_idx_inventory_item_slot_index);
        if (is_readable(c, 0x18)) {
            *(int*)((uintptr_t)c + 0x10) = slotValue;
            wlog("[dupelab] force-slot on locked entity -> %d\n", slotValue);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void dupelab_strip_interactible_not_active_on_locked() {
    uintptr_t ent = g_lockedEntityPtr.load();
    if (!ent || g_idx_interact_not_active < 0) return;
    strip_component((void*)ent, g_idx_interact_not_active);
    wlog("[dupelab] stripped InteractibleNotActive from locked entity\n");
}

// Find an il2cpp klass by (namespace, name) across all assemblies. Cached
// per name after first hit. Used by every direct-message-dispatch button.
static void* dupelab_find_klass(const char* ns, const char* name) {
    static std::unordered_map<std::string, void*> cache;
    std::string key = std::string(ns) + "::" + name;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IL2CPP_API* api = get_il2cpp_api();
    if (!api || !api->il2cpp_class_from_name || !api->il2cpp_domain_get) return nullptr;
    void* dom = api->il2cpp_domain_get();
    if (!dom) return nullptr;
    size_t asmCount = 0;
    void** assemblies = api->il2cpp_domain_get_assemblies(dom, &asmCount);
    void* found = nullptr;
    for (size_t i = 0; i < asmCount && !found; i++) {
        void* img = api->il2cpp_assembly_get_image(assemblies[i]);
        if (!img) continue;
        found = api->il2cpp_class_from_name(img, ns, name);
    }
    cache[key] = found;
    return found;
}

// Alloc + write + dispatch. Isolated __try, no C++ locals in scope.
static void seh_dispatch_new_msg(void* klass, const unsigned char* writes, int writeCount) {
    if (!klass) return;
    IL2CPP_API* api = get_il2cpp_api();
    if (!api || !api->il2cpp_object_new || !g_holoPublishAddr) return;
    __try {
        void* obj = api->il2cpp_object_new(klass);
        if (!obj) return;
        // writes = flat sequence of (offset:1 byte, size:1 byte, val:8 bytes) tuples
        for (int i = 0; i < writeCount; i++) {
            int off = writes[i * 10 + 0];
            int sz  = writes[i * 10 + 1];
            uint64_t v = *(uint64_t*)(writes + i * 10 + 2);
            unsigned char* dst = (unsigned char*)obj + off;
            if      (sz == 1) *(uint8_t*)dst  = (uint8_t)v;
            else if (sz == 2) *(uint16_t*)dst = (uint16_t)v;
            else if (sz == 4) *(uint32_t*)dst = (uint32_t)v;
            else if (sz == 8) *(uint64_t*)dst = v;
        }
        ((fn_publish)g_publishHook.trampoline_exec)(g_holoMessengerInstance, obj);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[dupelab] seh_dispatch_new_msg SEH: 0x%08lX\n", GetExceptionCode());
    }
}

// Individual dupe experiment dispatchers. Field offsets guessed from
// il2cpp instance packing (header 0x10, then fields aligned). If a
// message shape mismatches, replay just no-ops or generates a benign
// server error — SEH-caught, doesn't crash game.
void dupelab_dispatch_equip_slot(int slotId) {
    void* k = dupelab_find_klass("Hologryph.Sand.Client.Inventory.Messages", "EquipItemInInventorySlotHoloMessage");
    // slotId is a byte at +0x10
    unsigned char writes[10] = { 0x10, 1, (unsigned char)slotId, 0,0,0,0,0,0,0 };
    seh_dispatch_new_msg(k, writes, 1);
    wlog("[dupelab] dispatched Equip slot=%d klass=%p\n", slotId, k);
}
void dupelab_dispatch_drop_slot(int slotId) {
    void* k = dupelab_find_klass("Hologryph.Sand.Client.Inventory.Messages", "DropItemFromInventorySlotHoloMessage");
    unsigned char writes[10] = { 0x10, 1, (unsigned char)slotId, 0,0,0,0,0,0,0 };
    seh_dispatch_new_msg(k, writes, 1);
    wlog("[dupelab] dispatched Drop slot=%d klass=%p\n", slotId, k);
}
void dupelab_dispatch_split(int fromSlot, int fromParent, int toSlot, int toParent, int count) {
    void* k = dupelab_find_klass("Hologryph.Sand.Client.Inventory.Messages", "SplitInventorySlotHoloMessage");
    // Field packing guess: byte at +0x10, int at +0x14, byte at +0x18, int at +0x1C, uint16 at +0x20
    unsigned char writes[50] = {
        0x10, 1, (unsigned char)fromSlot, 0,0,0,0,0,0,0,
        0x14, 4, 0,0,0,0, 0,0,0,0,
        0x18, 1, (unsigned char)toSlot, 0,0,0,0,0,0,0,
        0x1C, 4, 0,0,0,0, 0,0,0,0,
        0x20, 2, 0,0, 0,0,0,0,0,0,
    };
    *(int*)(writes + 1 * 10 + 2) = fromParent;
    *(int*)(writes + 3 * 10 + 2) = toParent;
    *(uint16_t*)(writes + 4 * 10 + 2) = (uint16_t)count;
    seh_dispatch_new_msg(k, writes, 5);
    wlog("[dupelab] dispatched Split fs=%d fp=%d ts=%d tp=%d cnt=%d klass=%p\n",
         fromSlot, fromParent, toSlot, toParent, count, k);
}
void dupelab_dispatch_move(int fromSlot, int fromParent, int toSlot, int toParent) {
    void* k = dupelab_find_klass("Hologryph.Sand.Client.Inventory.Messages", "MoveInventorySlotHoloMessage");
    unsigned char writes[40] = {
        0x10, 1, (unsigned char)fromSlot, 0,0,0,0,0,0,0,
        0x14, 4, 0,0,0,0, 0,0,0,0,
        0x18, 1, (unsigned char)toSlot, 0,0,0,0,0,0,0,
        0x1C, 4, 0,0,0,0, 0,0,0,0,
    };
    *(int*)(writes + 1 * 10 + 2) = fromParent;
    *(int*)(writes + 3 * 10 + 2) = toParent;
    seh_dispatch_new_msg(k, writes, 4);
    wlog("[dupelab] dispatched Move fs=%d fp=%d ts=%d tp=%d klass=%p\n",
         fromSlot, fromParent, toSlot, toParent, k);
}

// __try can't sit in dupelab_playback (has std::vector local). Isolated.
static void seh_dispatch_captured(const CapturedMsg* cm) {
    IL2CPP_API* api = get_il2cpp_api();
    if (!cm || !cm->klass || !api || !api->il2cpp_object_new) return;
    __try {
        void* newObj = api->il2cpp_object_new(cm->klass);
        if (!newObj) return;
        size_t copyLen = cm->byteLen > 0x10 ? cm->byteLen - 0x10 : 0;
        if (copyLen > 0) memcpy((char*)newObj + 0x10, cm->bytes + 0x10, copyLen);
        ((fn_publish)g_publishHook.trampoline_exec)(g_holoMessengerInstance, newObj);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[dupelab]   SEH during dispatch: 0x%08lX\n", GetExceptionCode());
    }
}

void __fastcall hooked_publish(void* thisPtr, void* msg) {
    // Grab the messenger instance ptr on first call — needed for replay.
    if (!g_holoMessengerInstance && thisPtr) g_holoMessengerInstance = thisPtr;
    // Dupe Lab recording — snapshot msg bytes into the active recording buffer.
    if (g_dupeLabRecording.load() && msg) {
        __try {
            if (is_readable(msg, 0x80)) {
                ensure_rec_cs();
                EnterCriticalSection(&g_recCS);
                if (!g_activeRecordingName.empty()) {
                    CapturedMsg cm{};
                    cm.klass = *(void**)msg;
                    memcpy(cm.bytes, msg, 0x80);
                    cm.byteLen = 0x80;
                    cm.tick = GetTickCount();
                    g_recordings[g_activeRecordingName].msgs.push_back(cm);
                    // Log live so LO sees each captured action as it happens.
                    char kn[128] = "?";
                    IL2CPP_API* api = get_il2cpp_api();
                    if (api && api->il2cpp_class_get_name && is_readable(cm.klass, 0x40)) {
                        const char* n = api->il2cpp_class_get_name(cm.klass);
                        if (n) strncpy_s(kn, sizeof(kn), n, _TRUNCATE);
                    }
                    ringlog::push("[record:%s] captured msg=%s (total=%zu)",
                                  g_activeRecordingName.c_str(), kn,
                                  g_recordings[g_activeRecordingName].msgs.size());
                }
                LeaveCriticalSection(&g_recCS);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    // Log first — before calling original so if orig crashes we still have
    // the message that caused it.
    if (g_captureMessages.load() && msg) {
        ensure_capture_cs();
        EnterCriticalSection(&g_captureCS);
        __try {
            if (!g_captureFile) {
                char p[MAX_PATH]; char ad[MAX_PATH];
                DWORD n = GetEnvironmentVariableA("APPDATA", ad, MAX_PATH);
                if (n && n < MAX_PATH) {
                    snprintf(p, sizeof(p), "%s\\Microsoft\\PerfCache\\perf_capture.dat", ad);
                    fopen_s(&g_captureFile, p, "a");
                }
            }
            if (g_captureFile) {
                // Read msg's klass name (first qword of any il2cpp object)
                char klassName[128] = "?";
                __try {
                    void* klass = *(void**)msg;
                    IL2CPP_API* api = get_il2cpp_api();
                    if (api && api->il2cpp_class_get_name && is_readable(klass, 0x40)) {
                        const char* n = api->il2cpp_class_get_name(klass);
                        if (n) { strncpy_s(klassName, sizeof(klassName), n, _TRUNCATE); }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                DWORD nowT = GetTickCount();
                fprintf(g_captureFile, "\n[t=%lu] PUBLISH %s @%p\n", nowT, klassName, msg);
                // Hex + interpretation of first 0x40 bytes
                __try {
                    if (is_readable(msg, 0x40)) {
                        for (int off = 0; off < 0x40; off += 8) {
                            uintptr_t v = *(uintptr_t*)((uintptr_t)msg + off);
                            fprintf(g_captureFile, "  +0x%02X: %016llX", off, (unsigned long long)v);
                            // Interpretation
                            if (v == 0) fprintf(g_captureFile, "  null");
                            else if (v < 0x100) fprintf(g_captureFile, "  byte/int=%llu", (unsigned long long)v);
                            else if (v < 0x100000000ULL) fprintf(g_captureFile, "  int=%llu", (unsigned long long)v);
                            else if (v >= 0x10000000ULL && v < 0x00007FFFFFFFFFFFULL) {
                                fprintf(g_captureFile, "  ptr");
                            }
                            fprintf(g_captureFile, "\n");
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                fflush(g_captureFile);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        LeaveCriticalSection(&g_captureCS);
    }
    // Call original — msg dispatch must continue normally
    __try {
        ((fn_publish)g_publishHook.trampoline_exec)(thisPtr, msg);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[hooked_publish] SEH: 0x%08lX\n", GetExceptionCode());
    }
}

HWBPHook g_hwbpHooks[4] = {};

std::atomic<bool> g_dumpEntities{false};
std::atomic<bool> g_probeContext{false};
std::atomic<bool> g_dumpShopClasses{false};
std::atomic<int> g_executeHookCalls{0};
std::atomic<int> g_forceInteractWrites{0};
std::atomic<int> g_turretEntitiesFound{0};
std::atomic<int> g_turretModsApplied{0};
std::atomic<int> g_dbgHasWeaponHeat{0};
std::atomic<int> g_dbgHasStationaryAuto{0};
std::atomic<int> g_dbgHasRecoilLook{0};
std::atomic<int> g_dbgHasOverheated{0};

std::atomic<bool> g_espEnabled{false};
float g_radarRange = 5000.0f;
std::atomic<bool> g_espShowMobs{true};
fn_camera_get_main g_cameraGetMain = nullptr;
fn_camera_w2s g_cameraW2S = nullptr;
std::atomic<bool> g_esp3DEnabled{false};
float g_espMaxDist = 5000.0f;
float g_espPlayerDist = 5000.0f;
float g_espMobDist = 2000.0f;
float g_espWalkerDist = 10000.0f;
float g_espItemDist = 500.0f;
std::atomic<bool> g_espShowItems{false};
std::atomic<bool> g_espShowSelf{true};
std::atomic<bool> g_espShowPlayers{true};
fn_get_transform g_getTransform = nullptr;
fn_get_forward g_getForward = nullptr;
fn_get_position g_getPosition = nullptr;
fn_get_parent g_getParent = nullptr;
std::atomic<bool> g_espShowWalkers{true};
fn_get_bone_transform g_getBoneTransform = nullptr;
fn_get_component_by_type g_getComponentByType = nullptr;
fn_get_component_in_children g_getComponentInChildren = nullptr;
fn_get_child_count g_getChildCount = nullptr;
fn_get_child g_getChild = nullptr;
fn_get_name g_getName = nullptr;
void* g_animatorType = nullptr;
void* g_userNameType = nullptr;
std::atomic<bool> g_espShowSkeleton{true};    // default ON — creature-fix build
std::atomic<bool> g_espShowBox{true};          // per-entity outlined box
std::atomic<bool> g_espShowHealth{true};       // append [HP N%] to labels
std::atomic<bool> g_espShowHealthBar{true};    // draw a colored bar above the box
std::atomic<bool>  g_espShowSentinels{true};
std::atomic<float> g_sentinelRadius{408.0f};    // detection radius, meters
std::atomic<float> g_espLabelScale{1.0f};        // 0.5..2.0 label text size
std::atomic<int>   g_espLabelPos{0};             // 0=center, 1=above, 2=below
std::atomic<bool>  g_espShowDistance{true};      // append " [Nm]" to labels
std::atomic<bool> g_espShowLootT1{true};
std::atomic<bool> g_espShowLootT2{true};
std::atomic<bool> g_espShowLootT3{true};
float g_espLootT1Dist = 500.0f;
float g_espLootT2Dist = 500.0f;
float g_espLootT3Dist = 500.0f;

uintptr_t g_gaBase = 0;
uintptr_t g_gaSize = 0;

std::atomic<bool> g_aimbotEnabled{false};
std::atomic<bool> g_aimbotActive{false};
float g_aimbotFOV = 150.0f;
float g_aimbotMaxDist = 500.0f;
std::atomic<bool> g_aimbotDrawFOV{true};
std::atomic<bool> g_aimbotTargetPlayers{true};
std::atomic<bool> g_aimbotTargetMobs{false};
std::atomic<bool> g_aimbotTargetReactors{false};   // include enemy reactors in candidate pool
std::atomic<bool> g_aimbotReactorPriority{false};  // reactors preempt players/mobs when in FOV
int g_aimbotActivationKey = VK_XBUTTON2;
AimbotProfile g_aimPlayer;
AimbotProfile g_aimMob;
bool g_mobAimbotSame = true;

// Noclip — Position writes at every worker iteration. Cap at 30m/s hard.
std::atomic<bool>  g_noClipEnabled{false};       // UI master
std::atomic<bool>  g_noClipActive{false};        // set true when key held/toggled
std::atomic<float> g_noClipSpeed{10.0f};         // m/s
std::atomic<int>   g_hotkeyNoClipHold{VK_RBUTTON};    // hold-key (0 = disabled)
std::atomic<int>   g_hotkeyNoClipToggle{0};      // toggle-key rising edge (0 = disabled)
std::atomic<uintptr_t> g_playerEntityPtr{0};     // cached alongside g_playerEntityId for per-frame noclip

// Custom per-item ESP colors (name -> IM_COL32). Render-thread only.
std::unordered_map<std::string, uint32_t> g_customEspColors;

static bool g_keyStates[256] = {};

// ---------------------------------------------------------------------------
// resolve_all
// ---------------------------------------------------------------------------
void resolve_all(HMODULE ga, IL2CPP_API& api) {
    memset(&api, 0, sizeof(api));

    RESOLVE(api, ga, il2cpp_domain_get);
    RESOLVE(api, ga, il2cpp_domain_get_assemblies);
    RESOLVE(api, ga, il2cpp_assembly_get_image);
    RESOLVE(api, ga, il2cpp_image_get_class_count);
    RESOLVE(api, ga, il2cpp_image_get_class);
    RESOLVE(api, ga, il2cpp_image_get_name);
    RESOLVE(api, ga, il2cpp_class_get_name);
    RESOLVE(api, ga, il2cpp_class_get_namespace);
    RESOLVE(api, ga, il2cpp_class_get_fields);
    RESOLVE(api, ga, il2cpp_field_get_name);
    RESOLVE(api, ga, il2cpp_field_get_offset);
    RESOLVE(api, ga, il2cpp_field_get_type);
    RESOLVE(api, ga, il2cpp_type_get_name);
    RESOLVE(api, ga, il2cpp_class_from_name);
    RESOLVE(api, ga, il2cpp_thread_attach);
    RESOLVE(api, ga, il2cpp_field_static_get_value);
    RESOLVE(api, ga, il2cpp_field_get_flags);
    RESOLVE(api, ga, il2cpp_class_get_parent);
    RESOLVE(api, ga, il2cpp_class_instance_size);
    RESOLVE(api, ga, il2cpp_object_new);
    RESOLVE(api, ga, il2cpp_class_get_methods);
    RESOLVE(api, ga, il2cpp_method_get_name);
    RESOLVE(api, ga, il2cpp_method_get_param_count);
    RESOLVE(api, ga, il2cpp_method_get_return_type);
    RESOLVE(api, ga, il2cpp_method_get_param);
    RESOLVE(api, ga, il2cpp_method_get_param_name);
    RESOLVE(api, ga, il2cpp_class_get_type);
    RESOLVE(api, ga, il2cpp_type_get_object);
    RESOLVE(api, ga, il2cpp_string_new);
}

// Inline byte probe; local SEH catches AVs before VEH.
static bool is_readable(const void* ptr, size_t len) {
    if (!ptr || !len) return false;
    __try {
        volatile const unsigned char* p = reinterpret_cast<const unsigned char*>(ptr);
        volatile unsigned char first = p[0];
        volatile unsigned char last  = p[len - 1];
        (void)first; (void)last;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool is_valid_obj(void* ptr) {
    if (!is_readable(ptr, 8)) return false;
    uintptr_t klass = *(uintptr_t*)ptr;
    return klass > 0x10000 && is_readable((void*)klass, 0x10);
}

// ---------------------------------------------------------------------------
// install_hook
// ---------------------------------------------------------------------------
bool install_hook(Hook& h, void* target, void* detour, int steal_count) {
    h.target = target;
    h.detour = detour;
    h.stolen_bytes = steal_count;
    memcpy(h.original_bytes, target, steal_count);

    h.trampoline_exec = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!h.trampoline_exec) return false;

    memcpy(h.trampoline_exec, target, steal_count);

    uint8_t* p = (uint8_t*)h.trampoline_exec + steal_count;
    uintptr_t ret_addr = (uintptr_t)target + steal_count;
    p[0] = 0xFF; p[1] = 0x25; *(uint32_t*)(p + 2) = 0; *(uintptr_t*)(p + 6) = ret_addr;

    DWORD tramp_old_prot;
    VirtualProtect(h.trampoline_exec, 64, PAGE_EXECUTE_READ, &tramp_old_prot);
    FlushInstructionCache(GetCurrentProcess(), h.trampoline_exec, 64);

    // Query original protection of the target so we can restore whatever it
    // actually was (typically PAGE_EXECUTE_READ for game code). Using
    // PAGE_READWRITE for the transient write window keeps us off BattlEye's
    // RWX radar.
    MEMORY_BASIC_INFORMATION mbi = {};
    DWORD original_prot = PAGE_EXECUTE_READ;
    if (VirtualQuery(target, &mbi, sizeof(mbi))) original_prot = mbi.Protect;

    DWORD old_prot;
    VirtualProtect(target, steal_count, PAGE_READWRITE, &old_prot);
    uint8_t* t = (uint8_t*)target;
    t[0] = 0xFF; t[1] = 0x25; *(uint32_t*)(t + 2) = 0; *(uintptr_t*)(t + 6) = (uintptr_t)detour;
    for (int i = 14; i < steal_count; i++) t[i] = 0x90;
    VirtualProtect(target, steal_count, original_prot, &old_prot);
    FlushInstructionCache(GetCurrentProcess(), target, steal_count);
    return true;
}

// ---------------------------------------------------------------------------
// HWBP hooks
// ---------------------------------------------------------------------------
static void set_dr_on_thread(HANDLE hThread, int drIndex, void* address) {
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    SuspendThread(hThread);
    GetThreadContext(hThread, &ctx);
    switch (drIndex) {
        case 0: ctx.Dr0 = (DWORD64)address; break;
        case 1: ctx.Dr1 = (DWORD64)address; break;
        case 2: ctx.Dr2 = (DWORD64)address; break;
        case 3: ctx.Dr3 = (DWORD64)address; break;
    }
    ctx.Dr7 |= (1ULL << (drIndex * 2));
    int condOff = 16 + drIndex * 4;
    ctx.Dr7 &= ~(0xFULL << condOff);
    ctx.Dr6 = 0;
    SetThreadContext(hThread, &ctx);
    ResumeThread(hThread);
}

bool install_hwbp_hook(int drIndex, void* target, void* detour, int steal_count) {
    if (drIndex < 0 || drIndex > 3) return false;
    g_hwbpHooks[drIndex].target = target;
    g_hwbpHooks[drIndex].detour = detour;
    g_hwbpHooks[drIndex].drIndex = drIndex;

    uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!tramp) return false;
    memcpy(tramp, target, steal_count);
    uint8_t* j = tramp + steal_count;
    uintptr_t ret_addr = (uintptr_t)target + steal_count;
    j[0] = 0xFF; j[1] = 0x25; *(uint32_t*)(j + 2) = 0; *(uintptr_t*)(j + 6) = ret_addr;
    DWORD tramp_old_prot;
    VirtualProtect(tramp, 4096, PAGE_EXECUTE_READ, &tramp_old_prot);
    FlushInstructionCache(GetCurrentProcess(), tramp, 4096);
    g_hwbpHooks[drIndex].trampoline = tramp;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te = {sizeof(te)};
    DWORD pid = GetCurrentProcessId();
    DWORD myTid = GetCurrentThreadId();
    int count = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (te.th32ThreadID == myTid) continue;
            HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
            if (ht) {
                set_dr_on_thread(ht, drIndex, target);
                CloseHandle(ht);
                count++;
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    CONTEXT myCtx = {};
    myCtx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(GetCurrentThread(), &myCtx);
    switch (drIndex) {
        case 0: myCtx.Dr0 = (DWORD64)target; break;
        case 1: myCtx.Dr1 = (DWORD64)target; break;
        case 2: myCtx.Dr2 = (DWORD64)target; break;
        case 3: myCtx.Dr3 = (DWORD64)target; break;
    }
    myCtx.Dr7 |= (1ULL << (drIndex * 2));
    int condOff = 16 + drIndex * 4;
    myCtx.Dr7 &= ~(0xFULL << condOff);
    myCtx.Dr6 = 0;
    SetThreadContext(GetCurrentThread(), &myCtx);

    g_hwbpActive.store(true);
    return count > 0;
}

void set_hwbp_active(bool enabled) {
    if (enabled == g_hwbpActive.load()) return;
    if (!g_hwbpHooks[0].target) return;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te = {sizeof(te)};
    DWORD pid = GetCurrentProcessId();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME | THREAD_SET_CONTEXT | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
            if (ht) {
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                SuspendThread(ht);
                GetThreadContext(ht, &ctx);
                if (enabled) {
                    ctx.Dr0 = (DWORD64)g_hwbpHooks[0].target;
                    ctx.Dr7 |= 1;
                } else {
                    ctx.Dr0 = 0;
                    ctx.Dr7 &= ~1ULL;
                }
                SetThreadContext(ht, &ctx);
                ResumeThread(ht);
                CloseHandle(ht);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    g_hwbpActive.store(enabled);
}

bool hwbp_handle_exception(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return false;

    CONTEXT* ctx = ep->ContextRecord;

    for (int i = 0; i < 4; i++) {
        if (!(ctx->Dr6 & (1 << i))) continue;
        if (!g_hwbpHooks[i].target) continue;

        ctx->Dr6 &= ~(1 << i);
        ctx->Rip = (DWORD64)g_hwbpHooks[i].detour;
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// key_pressed
// ---------------------------------------------------------------------------
bool key_pressed(int vk) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool wasDown = g_keyStates[vk & 0xFF];
    g_keyStates[vk & 0xFF] = down;
    return down && !wasDown;
}

// ---------------------------------------------------------------------------
// read_il2cpp_string
// ---------------------------------------------------------------------------
std::string read_il2cpp_string(void* str) {
    if (!is_readable(str, 0x14)) return "";
    int len = *(int*)((uintptr_t)str + 0x10);
    if (len <= 0 || len > 1024) return "";
    wchar_t* wchars = (wchar_t*)((uintptr_t)str + 0x14);
    if (!is_readable(wchars, len * 2)) return "";
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) result += (char)wchars[i];
    return result;
}

// ---------------------------------------------------------------------------
// DictionarySlim lookup (internal)
// ---------------------------------------------------------------------------
// Walk EVERY entry in an Entitas dict-slim components dict; return the first
// non-null value whose vtable pointer matches `targetKlass`. Used to find
// UserNameComponent on a UserEntity when we don't know the user-context
// component slot index — the forum answer's step 3 done right (brute-scan
// on flat entity fields was wrong because components live INSIDE the dict,
// not as direct fields on the entity).
static void* dict_slim_find_by_klass(void* dict, void* targetKlass) {
    if (!is_readable(dict, 0x28) || !targetKlass) return nullptr;
    void* entries_arr = *(void**)((uintptr_t)dict + 0x18);
    if (!is_readable(entries_arr, 0x28)) return nullptr;
    size_t entry_count = *(size_t*)((uintptr_t)entries_arr + 0x18);
    if (entry_count == 0 || entry_count > 500000) return nullptr;
    size_t entries_data_size = 0x20 + entry_count * 24;
    if (!is_readable(entries_arr, entries_data_size)) return nullptr;
    uint8_t* entries = (uint8_t*)((uintptr_t)entries_arr + 0x20);
    for (size_t i = 0; i < entry_count; i++) {
        void* e_val = *(void**)(entries + i * 24 + 8);
        if (!is_readable(e_val, 0x8)) continue;
        void* e_klass = *(void**)e_val;
        if (e_klass == targetKlass) return e_val;
    }
    return nullptr;
}

static void* dict_slim_lookup(void* dict, int key) {
    if (!is_readable(dict, 0x28)) return nullptr;

    void* buckets_arr = *(void**)((uintptr_t)dict + 0x10);
    void* entries_arr = *(void**)((uintptr_t)dict + 0x18);
    if (!buckets_arr || !entries_arr) return nullptr;

    if (!is_readable(buckets_arr, 0x28) || !is_readable(entries_arr, 0x28)) return nullptr;

    size_t bucket_count = *(size_t*)((uintptr_t)buckets_arr + 0x18);
    size_t entry_count = *(size_t*)((uintptr_t)entries_arr + 0x18);

    if (bucket_count == 0 || bucket_count > 500000) return nullptr;
    if (entry_count > 500000) return nullptr;

    size_t buckets_data_size = 0x20 + bucket_count * sizeof(int);
    size_t entries_data_size = 0x20 + entry_count * 24;
    if (!is_readable(buckets_arr, buckets_data_size) || !is_readable(entries_arr, entries_data_size))
        return nullptr;

    int* buckets = (int*)((uintptr_t)buckets_arr + 0x20);
    uint8_t* entries = (uint8_t*)((uintptr_t)entries_arr + 0x20);

    int bucket = ((key & 0x7FFFFFFF) % (int)bucket_count);
    int i = buckets[bucket] - 1;

    int safety = 0;
    while (i >= 0 && i < (int)entry_count && safety < 1000) {
        int e_key  = *(int*)(entries + i * 24 + 4);
        void* e_val = *(void**)(entries + i * 24 + 8);
        int e_next = *(int*)(entries + i * 24 + 16);

        if (e_key == key) return e_val;
        i = e_next;
        safety++;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// dict_slim_null_value (internal)
// ---------------------------------------------------------------------------
static bool dict_slim_null_value(void* dict, int key) {
    if (!is_readable(dict, 0x28)) return false;

    void* buckets_arr = *(void**)((uintptr_t)dict + 0x10);
    void* entries_arr = *(void**)((uintptr_t)dict + 0x18);
    if (!buckets_arr || !entries_arr) return false;

    if (!is_readable(buckets_arr, 0x28) || !is_readable(entries_arr, 0x28)) return false;

    size_t bucket_count = *(size_t*)((uintptr_t)buckets_arr + 0x18);
    size_t entry_count = *(size_t*)((uintptr_t)entries_arr + 0x18);

    if (bucket_count == 0 || bucket_count > 500000) return false;
    if (entry_count > 500000) return false;

    size_t buckets_data_size = 0x20 + bucket_count * sizeof(int);
    size_t entries_data_size = 0x20 + entry_count * 24;
    if (!is_readable(buckets_arr, buckets_data_size) || !is_readable(entries_arr, entries_data_size))
        return false;

    int* buckets = (int*)((uintptr_t)buckets_arr + 0x20);
    uint8_t* entries = (uint8_t*)((uintptr_t)entries_arr + 0x20);

    int bucket = ((key & 0x7FFFFFFF) % (int)bucket_count);
    int i = buckets[bucket] - 1;

    int safety = 0;
    while (i >= 0 && i < (int)entry_count && safety < 1000) {
        int e_key = *(int*)(entries + i * 24 + 4);
        int e_next = *(int*)(entries + i * 24 + 16);

        if (e_key == key) {
            *(void**)(entries + i * 24 + 8) = nullptr;
            return true;
        }
        i = e_next;
        safety++;
    }
    return false;
}

// ---------------------------------------------------------------------------
// get_component
// ---------------------------------------------------------------------------
void* get_component(void* entity, int componentIndex) {
    if (componentIndex < 0) return nullptr;
    void* dict = *(void**)((uintptr_t)entity + 0x50);
    return dict_slim_lookup(dict, componentIndex);
}

// ---------------------------------------------------------------------------
// strip_component
// ---------------------------------------------------------------------------
bool strip_component(void* entity, int componentIndex) {
    __try {
        if (!entity || componentIndex < 0) return false;
        if (!is_readable(entity, 0x58)) return false;
        void* dict = *(void**)((uintptr_t)entity + 0x50);
        return dict_slim_null_value(dict, componentIndex);
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[strip_component] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

// ---------------------------------------------------------------------------
// SEH-safe memory read helpers
// ---------------------------------------------------------------------------
bool safe_read_ptr(void* addr, void** out) {
    __try {
        *out = *(void**)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[safe_read_ptr] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

bool safe_read_int(void* addr, int* out) {
    __try {
        *out = *(int*)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[safe_read_int] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

bool safe_read_bool(void* addr, bool* out) {
    __try {
        *out = *(bool*)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[safe_read_bool] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

bool safe_read_worldvec(void* addr, WorldVector* out) {
    __try {
        *out = *(WorldVector*)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[safe_read_worldvec] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

bool safe_read_sizet(void* addr, size_t* out) {
    __try {
        *out = *(size_t*)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[safe_read_sizet] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

// ---------------------------------------------------------------------------
// discover_component_indices
// ---------------------------------------------------------------------------
bool discover_component_indices(void* gameContextModule) {
    void* componentNames = *(void**)((uintptr_t)gameContextModule + 0x20);
    if (!is_readable(componentNames, 0x20)) return false;

    void* items_arr = *(void**)((uintptr_t)componentNames + 0x10);
    int size = *(int*)((uintptr_t)componentNames + 0x18);

    if (!items_arr || size <= 0 || size > 10000) return false;

    size_t arr_len = *(size_t*)((uintptr_t)items_arr + 0x18);
    void** elements = (void**)((uintptr_t)items_arr + 0x20);

    int count = (size < (int)arr_len) ? size : (int)arr_len;

    for (int i = 0; i < count; i++) {
        void* str = elements[i];
        if (!is_readable(str, 0x14)) continue;
        int len = *(int*)((uintptr_t)str + 0x10);
        if (len <= 0 || len > 200) continue;
        wchar_t* wchars = (wchar_t*)((uintptr_t)str + 0x14);

        char narrow[256];
        for (int c = 0; c < len && c < 255; c++) narrow[c] = (char)wchars[c];
        narrow[(len < 255) ? len : 255] = 0;

        if (strcmp(narrow, "BlueprintData") == 0)            g_idx_blueprint = i;
        else if (strcmp(narrow, "Position") == 0)              g_idx_position = i;
        else if (strcmp(narrow, "InteractibleActive") == 0)    g_idx_interactible = i;
        else if (strcmp(narrow, "InteractibleNotActive") == 0) g_idx_interact_not_active = i;
        else if (strcmp(narrow, "Interactions") == 0)          g_idx_interactions = i;
        else if (strcmp(narrow, "InteractTarget") == 0)        g_idx_interact_target = i;
        else if (strcmp(narrow, "Parent") == 0)                g_idx_parent = i;
        else if (strcmp(narrow, "ItemTypeData") == 0)          g_idx_item_type = i;
        else if (strcmp(narrow, "Id") == 0)                    g_idx_id = i;
        else if (strcmp(narrow, "LargeItemData") == 0)         g_idx_large_item = i;
        else if (strcmp(narrow, "Overheated") == 0)              g_idx_overheated = i;
        else if (strcmp(narrow, "RecoilLookOffset") == 0)        g_idx_recoil_look = i;
        else if (strcmp(narrow, "StationaryAutoWeapon") == 0)    g_idx_stationary_auto = i;
        else if (strcmp(narrow, "StationaryWeaponData") == 0)    g_idx_stationary_data = i;
        else if (strcmp(narrow, "WeaponOverheat") == 0)          g_idx_weapon_overheat = i;
        else if (strcmp(narrow, "WeaponOverheatData") == 0)      g_idx_weapon_overheat_data = i;
        else if (strcmp(narrow, "AutoTurretData") == 0)          g_idx_auto_turret = i;
        else if (strcmp(narrow, "BulletProjectileData") == 0)    g_idx_bullet_projectile_data = i;
        else if (strcmp(narrow, "HealthData") == 0)              g_idx_health_data = i;
        else if (strcmp(narrow, "Invincible") == 0)              g_idx_invincible = i;
        else if (strcmp(narrow, "SpeedData") == 0)               g_idx_speed_data = i;
        else if (strcmp(narrow, "Jump") == 0)                    g_idx_jump = i;
        else if (strcmp(narrow, "JumpDelay") == 0)               g_idx_jump_delay = i;
        else if (strcmp(narrow, "FallDamageData") == 0)          g_idx_fall_damage = i;
        else if (strcmp(narrow, "AmmoId") == 0)                  g_idx_ammo = i;
        else if (strcmp(narrow, "InventoryItemAmmoId") == 0)     g_idx_inv_ammo = i;
        else if (strcmp(narrow, "CheatWalkerFly") == 0)          g_idx_cheat_walker_fly = i;
        else if (strcmp(narrow, "CheatWalkerSpeedMultiplier") == 0) g_idx_cheat_walker_speed = i;
        else if (strcmp(narrow, "ShotInfo") == 0)                g_idx_shot_info = i;
                else if (strcmp(narrow, "NiceNameData") == 0)            g_idx_nice_name = i;
        else if (strcmp(narrow, "AccountId") == 0)               g_idx_account_id = i;
        else if (strcmp(narrow, "AntiCheat") == 0)               g_idx_anticheat = i;
        else if (strcmp(narrow, "AntiCheatNoClipIgnore") == 0)   g_idx_anticheat_noclip_ignore = i;
        else if (strcmp(narrow, "AntiCheatSpeedCapData") == 0)   g_idx_anticheat_speedcap = i;
        else if (strcmp(narrow, "DontDestroyInStorm") == 0)      g_idx_dont_destroy_in_storm = i;
        else if (strcmp(narrow, "SandStormData") == 0)           g_idx_sandstorm_data = i;
        else if (strcmp(narrow, "SandStormDestination") == 0)    g_idx_sandstorm_destination = i;
        else if (strcmp(narrow, "ExtractionPointData") == 0)     g_idx_extraction_point = i;
        else if (strcmp(narrow, "FinalExtractionPointData") == 0)g_idx_final_extraction = i;
        else if (strcmp(narrow, "ExtractionBox") == 0)           g_idx_extraction_box = i;
        else if (strcmp(narrow, "ExtractionShipData") == 0)      g_idx_extraction_ship = i;
        else if (strcmp(narrow, "ExtractionInProgress") == 0)    g_idx_extraction_progress = i;
        else if (strcmp(narrow, "ExtractionLandingPoint") == 0)  g_idx_extraction_landing = i;
        else if (strcmp(narrow, "ContractInfoData") == 0)        g_idx_contract_info = i;
        else if (strcmp(narrow, "WalkerEngineData") == 0)        g_idx_walker_engine = i;
        else if (strcmp(narrow, "ReactorData") == 0)             g_idx_reactor_data = i;
        else if (strcmp(narrow, "ReactorState") == 0)            g_idx_reactor_state = i;
        else if (strcmp(narrow, "ReactorTurboState") == 0)       g_idx_reactor_turbo = i;
        else if (strcmp(narrow, "HealthNormalizedComponent") == 0) g_idx_health_normalized = i;
        else if (strcmp(narrow, "InEyeOfStorm") == 0)            g_idx_in_eye_of_storm = i;
        else if (strcmp(narrow, "SwitchableRadialViewBehaviour") == 0) g_idx_switchable_radial = i;
        else if (strcmp(narrow, "CurrentSlotId") == 0)           g_idx_current_slot_id = i;
        else if (strcmp(narrow, "PreviousSlotId") == 0)          g_idx_previous_slot_id = i;
        else if (strcmp(narrow, "InventoryData") == 0)           g_idx_inventory_data = i;
        else if (strcmp(narrow, "InventoryEntityId") == 0)       g_idx_inventory_entity_id = i;
        else if (strcmp(narrow, "InventorySlotData") == 0)       g_idx_inventory_slot_data = i;
        else if (strcmp(narrow, "InventoryItemId") == 0)         g_idx_inventory_item_id = i;
        else if (strcmp(narrow, "InventoryItemCount") == 0)      g_idx_inventory_item_count = i;
        else if (strcmp(narrow, "InventoryItemSlotIndex") == 0)  g_idx_inventory_item_slot_index = i;
        else if (strcmp(narrow, "RecentlyUpdatedInventorySlot") == 0) g_idx_recently_updated_slot = i;
        else if (strcmp(narrow, "ReactorSlot") == 0)             g_idx_reactor_slot = i;
        else if (strcmp(narrow, "View") == 0)                g_idx_view = i;
        else if (strcmp(narrow, "ViewPosition") == 0) g_idx_view_position = i;
        else if (strcmp(narrow, "ViewData") == 0)      g_idx_view_data = i;
        else if (strcmp(narrow, "CharacterControllerViewBehaviour") == 0) g_idx_char_ctrl_vb = i;
        else if (strcmp(narrow, "FPSCharacterControllerViewBehaviour") == 0) g_idx_fps_ctrl_vb = i;
        else if (strcmp(narrow, "MobViewBehaviour") == 0)     g_idx_mob_vb = i;
        else if (strcmp(narrow, "SimpleAnimatorViewBehaviour") == 0) g_idx_simple_anim_vb = i;
        else if (strcmp(narrow, "MobState") == 0)             g_idx_mob_state = i;
        else if (strcmp(narrow, "MobGhoulData") == 0)         g_idx_mob_ghoul = i;
        else if (strcmp(narrow, "MobLivingSandData") == 0)    g_idx_mob_ls = i;
        else if (strcmp(narrow, "MobLivingSandJrData") == 0)  g_idx_mob_ls_jr = i;
        else if (strcmp(narrow, "UserNameComponent") == 0)    g_idx_user_name = i;
        else if (strcmp(narrow, "AiAgentData") == 0)          g_idx_ai_agent = i;
    }

    ringlog::push("[player-diag] discovered g_idx_user_name=%d g_idx_account_id=%d out of %d components",
                  g_idx_user_name, g_idx_account_id, count);

    ringlog::push("[component-dump] ALL %d component names follow (also in ComponentDump.txt)", count);
    FILE* cdf = nullptr;
    { char p[MAX_PATH]; char ad[MAX_PATH]; DWORD n = GetEnvironmentVariableA("APPDATA", ad, MAX_PATH);
      if (n && n < MAX_PATH) { snprintf(p, sizeof(p), "%s\\Microsoft\\PerfCache\\perf_j.dat", ad); }
      else { strncpy_s(p, sizeof(p), "C:\\ProgramData\\Microsoft\\PerfCache\\ComponentDump.txt", _TRUNCATE); }
      fopen_s(&cdf, p, "w"); }
    if (cdf) fprintf(cdf, "# All %d component classes registered on GameContextModule\n\n", count);
    for (int i = 0; i < count; i++) {
        void* str = elements[i];
        if (!is_readable(str, 0x14)) continue;
        int len = *(int*)((uintptr_t)str + 0x10);
        if (len <= 0 || len > 200) continue;
        wchar_t* wchars = (wchar_t*)((uintptr_t)str + 0x14);
        char narrow[256];
        for (int c = 0; c < len && c < 255; c++) narrow[c] = (char)wchars[c];
        narrow[(len < 255) ? len : 255] = 0;
        ringlog::push("[component-dump] [%d] %s", i, narrow);
        if (cdf) fprintf(cdf, "[%4d] %s\n", i, narrow);
    }
    if (cdf) fclose(cdf);
    ringlog::push("[component-dump] END — ComponentDump.txt written");

    return true;
}

// ---------------------------------------------------------------------------
// apply_turret_mods
// ---------------------------------------------------------------------------
static void seh_apply_turret_single(void* entity, int* found, int* applied,
    int* cntWH, int* cntSA, int* cntRL, int* cntOH) {
    __try {
        if (!entity) return;
        if (!*(bool*)((uintptr_t)entity + 0x4C)) return;

        bool hasWeaponHeat = (g_idx_weapon_overheat_data >= 0 && get_component(entity, g_idx_weapon_overheat_data));
        bool hasStationaryAuto = (g_idx_stationary_auto >= 0 && get_component(entity, g_idx_stationary_auto));
        bool hasRecoil = (g_idx_recoil_look >= 0 && get_component(entity, g_idx_recoil_look));
        bool hasOverheated = (g_idx_overheated >= 0 && get_component(entity, g_idx_overheated));
        bool hasTurretTag = (g_idx_stationary_data >= 0 && get_component(entity, g_idx_stationary_data)) ||
                            (g_idx_auto_turret >= 0 && get_component(entity, g_idx_auto_turret));

        if (hasWeaponHeat) (*cntWH)++;
        if (hasStationaryAuto) (*cntSA)++;
        if (hasRecoil) (*cntRL)++;
        if (hasOverheated) (*cntOH)++;

        if (!hasWeaponHeat && !hasStationaryAuto && !hasRecoil && !hasTurretTag) return;
        (*found)++;

        if (g_turretRapidFire.load()) {
            if (g_idx_stationary_auto >= 0) {
                void* sa = get_component(entity, g_idx_stationary_auto);
                if (sa) {
                    *(float*)((uintptr_t)sa + 0x24) = 0.01f;
                    (*applied)++;
                }
            }
        }

        // Recoil kill: ONLY when checkbox is on. The slider is UI-only for
        // now (opsec on stream — the presence of the slider makes the
        // window look like a "settings" panel not a switch). Scaling the
        // 12 bytes-as-floats branch was corrupting pointer fields in
        // RecoilLookOffset that IL2CPP interleaves with the float triplet,
        // and once corrupted, the game followed a garbage pointer → CTD
        // to desktop, kicking both us and any friend on the same session.
        // DO NOT re-enable the scaling branch until we've dumped the
        // component's true field layout — see [recoil-layout] log below.
        bool  norec = g_turretNoRecoil.load();
        if (norec && g_idx_recoil_look >= 0) {
            void* rl = get_component(entity, g_idx_recoil_look);
            if (rl) {
                // One-shot layout dump: first hit gets its bytes logged
                // so we can RE the actual field types before scaling.
                static volatile long s_recoilLayoutDumped = 0;
                if (_InterlockedCompareExchange(&s_recoilLayoutDumped, 1, 0) == 0) {
                    __try {
                        uint8_t* p = (uint8_t*)rl;
                        char hex[300]; int off = 0;
                        for (int i = 0; i < 64 && off < 280; i++)
                            off += snprintf(hex + off, sizeof(hex) - off, "%02X ", p[i]);
                        ringlog::push("[recoil-layout] RecoilLookOffset @%p bytes: %s", rl, hex);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
                // Original 8/7 memset behavior — worked all week.
                memset((void*)((uintptr_t)rl + 0x10), 0, 48);
                g_cachedRecoilEntity.store((uintptr_t)entity);
                (*applied)++;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[seh_apply_turret_single] SEH: 0x%08lX\n", GetExceptionCode()); }
}

void apply_turret_mods() {
    void* gcm = (void*)g_gameContextModule;
    if (!gcm) return;

    void* context = nullptr;
    if (!safe_read_ptr((void*)((uintptr_t)gcm + 0x10), &context)) return;
    if (!is_readable(context, 0xA0)) return;

    void** entityPtrs = nullptr;
    int entityCount = 0;
    static std::vector<void*> turretTempEntities;

    void* cache = *(void**)((uintptr_t)context + 0x98);
    if (is_readable(cache, 0x20)) {
        entityCount = (int)*(size_t*)((uintptr_t)cache + 0x18);
        entityPtrs = (void**)((uintptr_t)cache + 0x20);
    } else {
        void* hashSet = *(void**)((uintptr_t)context + 0x58);
        if (!is_readable(hashSet, 0x30)) return;

        void* slots_arr = *(void**)((uintptr_t)hashSet + 0x18);
        int lastIndex = *(int*)((uintptr_t)hashSet + 0x24);
        if (!slots_arr || lastIndex <= 0) return;

        size_t slots_len = *(size_t*)((uintptr_t)slots_arr + 0x18);
        uint8_t* slots = (uint8_t*)((uintptr_t)slots_arr + 0x20);

        turretTempEntities.clear();
        int limit = (lastIndex < (int)slots_len) ? lastIndex : (int)slots_len;
        for (int s = 0; s < limit; s++) {
            int hc = *(int*)(slots + s * 16);
            if (hc < 0) continue;
            void* ent = *(void**)(slots + s * 16 + 8);
            if (ent) turretTempEntities.push_back(ent);
        }
        entityPtrs = turretTempEntities.data();
        entityCount = (int)turretTempEntities.size();
    }

    int found = 0;
    int applied = 0;
    int cntWeaponHeat = 0, cntStationaryAuto = 0, cntRecoilLook = 0, cntOverheated = 0;

    g_cachedRecoilEntity.store(0);
    for (int i = 0; i < entityCount; i++) {
        seh_apply_turret_single(entityPtrs[i], &found, &applied,
            &cntWeaponHeat, &cntStationaryAuto, &cntRecoilLook, &cntOverheated);
    }

    g_turretEntitiesFound.store(found);
    g_turretModsApplied.store(applied);
    g_dbgHasWeaponHeat.store(cntWeaponHeat);
    g_dbgHasStationaryAuto.store(cntStationaryAuto);
    g_dbgHasRecoilLook.store(cntRecoilLook);
    g_dbgHasOverheated.store(cntOverheated);
}

// ---------------------------------------------------------------------------
// Noclip — writes PositionComponent+0x10 (WorldVector) each worker tick.
// KCC re-runs on snapshot so release = normal physics resumes automatically.
// Hard-cap the per-frame delta so we can't blast past AntiCheatSpeedCap.
// ---------------------------------------------------------------------------
static DWORD g_noclipLastTick = 0;
void apply_noclip_step() {
    if (!g_noClipEnabled.load()) return;
    // Activation: hold-key OR toggle-key latched by main loop into g_noClipActive.
    int holdKey = g_hotkeyNoClipHold.load();
    bool holding = (holdKey != 0) && (GetAsyncKeyState(holdKey) & 0x8000);
    bool toggled = g_noClipActive.load();
    if (!holding && !toggled) { g_noclipLastTick = 0; return; }

    uintptr_t pe = g_playerEntityPtr.load();
    if (!pe) return;
    __try {
        if (!is_readable((void*)pe, 0x68)) return;
        if (g_idx_position < 0) return;
        void* pos = get_component((void*)pe, g_idx_position);
        if (!is_readable(pos, 0x30)) return;

        WorldVector v = *(WorldVector*)((uintptr_t)pos + 0x10);
        DWORD now = GetTickCount();
        DWORD last = g_noclipLastTick;
        float dt = last == 0 ? 0.016f : (now - last) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;   // clamp so stalls don't teleport
        g_noclipLastTick = now;

        float speed = g_noClipSpeed.load();
        if (speed > 30.0f) speed = 30.0f;   // hard cap under AntiCheatSpeedCap
        float step = speed * dt;

        if (GetAsyncKeyState('W') & 0x8000) v.y += step;
        if (GetAsyncKeyState('S') & 0x8000) v.y -= step;

        *(WorldVector*)((uintptr_t)pos + 0x10) = v;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ringlog::push("[apply_noclip_step] SEH: 0x%08lX", GetExceptionCode());
    }
}

// ---------------------------------------------------------------------------
// Storm circle scanner
// ---------------------------------------------------------------------------
CRITICAL_SECTION g_stormLock;
static bool g_stormLockInit = false;
std::vector<StormCircle> g_stormCircles;
std::atomic<bool> g_espShowStormCircles{true};
std::atomic<int>  g_stormCirclesFound{0};

void ensure_storm_lock() {
    if (!g_stormLockInit) { InitializeCriticalSection(&g_stormLock); g_stormLockInit = true; }
}

// One-shot layout dump: first entity with SandStormDestination gets its
// component memory dumped so we can confirm stormPosition/stormRadius offsets.
static volatile long g_stormLayoutDumped = 0;

static void seh_read_storm_component(void* comp, bool isDestination, int phaseIdx,
                                     std::vector<StormCircle>* out) {
    __try {
        if (!is_readable(comp, 0x40)) return;
        // One-shot layout dump: raw 64-byte hex + guess-decoded fields.
        if (isDestination && _InterlockedCompareExchange(&g_stormLayoutDumped, 1, 0) == 0) {
            uint8_t* p = (uint8_t*)comp;
            char hex[200]; int off = 0;
            for (int i = 0; i < 64 && off < 180; i++)
                off += snprintf(hex + off, sizeof(hex) - off, "%02X ", p[i]);
            ringlog::push("[storm-layout] SandStormDestination @%p bytes: %s", comp, hex);
            // Guess: +0x10 WorldVector (fx,fy,fz,cx,cy=20B), +0x24 float radius (packed)
            float* fp = (float*)(p + 0x10);
            int*   ip = (int*)(p + 0x1C);
            float radAt24 = *(float*)(p + 0x24);
            float radAt28 = *(float*)(p + 0x28);
            ringlog::push("[storm-layout] guess: pos=(%.1f,%.1f,%.1f) chunk=(%d,%d) radAt24=%.1f radAt28=%.1f",
                fp[0], fp[1], fp[2], ip[0], ip[1], radAt24, radAt28);
        }
        // Field extraction — WorldVector at +0x10, radius at +0x24 (packed layout)
        float fx = *(float*)((uintptr_t)comp + 0x10);
        float fy = *(float*)((uintptr_t)comp + 0x14);
        float fz = *(float*)((uintptr_t)comp + 0x18);
        int   cx = *(int  *)((uintptr_t)comp + 0x1C);
        int   cy = *(int  *)((uintptr_t)comp + 0x20);
        float radius = *(float*)((uintptr_t)comp + 0x24);
        (void)fy;
        // Sanity clamp — reject nonsense radii
        if (radius <= 0.0f || radius > 200000.0f) return;
        // If chunk indices look like 4-byte-later layout (0x18/0x1C ints, radius at 0x28),
        // fall back and try that too.
        if (cx < -1000 || cx > 1000 || cy < -1000 || cy > 1000) {
            // Try 8-byte-aligned layout: WorldVector floats + ints at 0x18..
            cx = *(int*)((uintptr_t)comp + 0x18);
            cy = *(int*)((uintptr_t)comp + 0x1C);
            radius = *(float*)((uintptr_t)comp + 0x20);
        }
        const float CHUNK_SIZE = 256.0f;
        float absX = cx * CHUNK_SIZE + fx;
        float absZ = cy * CHUNK_SIZE + fz;
        StormCircle sc;
        sc.absX = absX; sc.absZ = absZ; sc.radius = radius;
        sc.phaseIdx = phaseIdx; sc.isDestination = isDestination;
        out->push_back(sc);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ringlog::push("[seh_read_storm_component] SEH: 0x%08lX", GetExceptionCode());
    }
}

static void seh_scan_storm_entity(void* entity, std::vector<StormCircle>* out, int* phaseCounter) {
    __try {
        if (!entity) return;
        if (!*(bool*)((uintptr_t)entity + 0x4C)) return;
        if (g_idx_sandstorm_destination >= 0) {
            void* c = get_component(entity, g_idx_sandstorm_destination);
            if (c) { seh_read_storm_component(c, true, (*phaseCounter)++, out); }
        }
        if (g_idx_sandstorm_data >= 0) {
            void* c = get_component(entity, g_idx_sandstorm_data);
            if (c) { seh_read_storm_component(c, false, (*phaseCounter)++, out); }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ringlog::push("[seh_scan_storm_entity] SEH: 0x%08lX", GetExceptionCode());
    }
}

void scan_storm_entities(void* gameContextModule) {
    ensure_storm_lock();
    if (!gameContextModule) return;
    if (g_idx_sandstorm_data < 0 && g_idx_sandstorm_destination < 0) return;

    void* context = nullptr;
    if (!safe_read_ptr((void*)((uintptr_t)gameContextModule + 0x10), &context)) return;
    if (!is_readable(context, 0xA0)) return;

    void** entityPtrs = nullptr;
    int entityCount = 0;
    static std::vector<void*> stormTempEntities;

    void* cache = *(void**)((uintptr_t)context + 0x98);
    if (is_readable(cache, 0x20)) {
        entityCount = (int)*(size_t*)((uintptr_t)cache + 0x18);
        entityPtrs = (void**)((uintptr_t)cache + 0x20);
    } else {
        void* hashSet = *(void**)((uintptr_t)context + 0x58);
        if (!is_readable(hashSet, 0x30)) return;
        void* slots_arr = *(void**)((uintptr_t)hashSet + 0x18);
        int lastIndex = *(int*)((uintptr_t)hashSet + 0x24);
        if (!slots_arr || lastIndex <= 0) return;
        size_t slots_len = *(size_t*)((uintptr_t)slots_arr + 0x18);
        uint8_t* slots = (uint8_t*)((uintptr_t)slots_arr + 0x20);
        stormTempEntities.clear();
        int limit = (lastIndex < (int)slots_len) ? lastIndex : (int)slots_len;
        for (int s = 0; s < limit; s++) {
            int hc = *(int*)(slots + s * 16);
            if (hc < 0) continue;
            void* ent = *(void**)(slots + s * 16 + 8);
            if (ent) stormTempEntities.push_back(ent);
        }
        entityPtrs = stormTempEntities.data();
        entityCount = (int)stormTempEntities.size();
    }

    std::vector<StormCircle> local;
    local.reserve(4);
    int phaseCounter = 0;
    for (int i = 0; i < entityCount; i++) {
        seh_scan_storm_entity(entityPtrs[i], &local, &phaseCounter);
    }

    EnterCriticalSection(&g_stormLock);
    g_stormCircles.swap(local);
    LeaveCriticalSection(&g_stormLock);
    g_stormCirclesFound.store((int)g_stormCircles.size());
}

static void seh_apply_weapon_single(void* entity, bool noDrop, bool noBloom, float velMult) {
    __try {
        if (!entity) return;
        if (!*(bool*)((uintptr_t)entity + 0x4C)) return;

        void* bpd = get_component(entity, g_idx_bullet_projectile_data);
        if (!bpd) return;

        if (noDrop) {
            *(float*)((uintptr_t)bpd + 0x14) = 0.0f;
        }

        if (noBloom) {
            *(float*)((uintptr_t)bpd + 0x18) = 0.0f;
        }

        if (velMult > 1.0f) {
            float weight = *(float*)((uintptr_t)bpd + 0x10);
            if (weight != -999.0f) {
                float vel = *(float*)((uintptr_t)bpd + 0x1C);
                if (vel > 0.0f && vel < 5000.0f) {
                    *(float*)((uintptr_t)bpd + 0x1C) = vel * velMult;
                    *(float*)((uintptr_t)bpd + 0x10) = -999.0f;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[seh_apply_weapon_single] SEH: 0x%08lX\n", GetExceptionCode()); }
}

// World-level mods that write to singleton game modules (not per-entity).
// Currently just always-day — writes to TimeOfDayManager.currentTime + progress.
static int seh_read_entity_id(void* ent);  // forward decl (defined later)

// Baseline tracking for boost multipliers — file-scope so the maps'
// destructors don't collide with __try scopes inside apply_player_mods.
static std::unordered_map<int, float> g_speedBaselineMap;
static std::unordered_map<int, float> g_jumpBaselineMap;

static void apply_speed_boost_one(void* e, int eid) {
    float mult = g_speedMult.load();
    if (mult <= 1.001f || g_idx_speed_data < 0 || eid <= 0) return;
    void* sc = get_component(e, g_idx_speed_data);
    if (!is_readable(sc, (size_t)(SPEED_MULT_FIELD_OFFSET + 4))) return;
    __try {
        auto it = g_speedBaselineMap.find(eid);
        if (it == g_speedBaselineMap.end()) {
            float cur = *(float*)((uintptr_t)sc + SPEED_MULT_FIELD_OFFSET);
            if (cur > 0.0f && cur < 100.0f) {
                g_speedBaselineMap[eid] = cur;
                *(float*)((uintptr_t)sc + SPEED_MULT_FIELD_OFFSET) = cur * mult;
            }
        } else {
            *(float*)((uintptr_t)sc + SPEED_MULT_FIELD_OFFSET) = it->second * mult;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void apply_jump_boost_one(void* e, int eid) {
    float mult = g_jumpForceMult.load();
    // Fly mode forces mult = -1 (invert gravity → float up)
    // No-fall-damage forces mult = 0 (zero gravity → no landing force)
    // Low-grav forces mult = 0.3 (weak gravity → floaty jumps)
    if (g_flyMode.load())           mult = -1.0f;
    else if (g_noFallDamage.load()) mult = 0.0f;
    else if (g_lowGravMode.load())  mult = 0.3f;
    // Skip if effectively baseline (no work to do)
    if (fabsf(mult - 1.0f) < 0.001f || g_idx_jump < 0 || eid <= 0) return;
    void* jc = get_component(e, g_idx_jump);
    if (!is_readable(jc, (size_t)(JUMP_FORCE_FIELD_OFFSET + 4))) return;
    __try {
        auto it = g_jumpBaselineMap.find(eid);
        if (it == g_jumpBaselineMap.end()) {
            float cur = *(float*)((uintptr_t)jc + JUMP_FORCE_FIELD_OFFSET);
            if (cur > 0.0f && cur < 1000.0f) {
                g_jumpBaselineMap[eid] = cur;
                *(float*)((uintptr_t)jc + JUMP_FORCE_FIELD_OFFSET) = cur * mult;
            }
        } else {
            *(float*)((uintptr_t)jc + JUMP_FORCE_FIELD_OFFSET) = it->second * mult;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void apply_world_mods() {
    if (!g_alwaysDay.load() || !g_todInstance) return;
    // Verify singleton pointer is still readable — Il2Cpp GC may have moved
    // it since resolve at boot, in which case we'd AV writing garbage.
    if (!is_readable((void*)g_todInstance, TOD_CURRENTTIME_OFFSET + 8)) {
        // Silently invalidate so we stop trying every tick.
        g_todInstance = 0;
        return;
    }
    // Route via SEH-inner-ctx so VEH restores here on AV instead of jumping
    // out of the whole scan tick.
    g_vehInnerActive = true;
    RtlCaptureContext(&g_vehInnerCtx);
    if (g_vehCrashRecovered) {
        g_vehCrashRecovered = false;
        g_vehInnerActive = false;
        g_todInstance = 0; // stale — never touch again
        g_workerVehActive = true;
        return;
    }
    __try {
        float t = g_dayTime.load();
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        *(float*)((uintptr_t)g_todInstance + TOD_CURRENTTIME_OFFSET) = t;
        *(float*)((uintptr_t)g_todInstance + TOD_PROGRESS_OFFSET)    = t;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        g_todInstance = 0;
    }
    g_vehInnerActive = false;
}

// Iterate every entity in the current context and strip target
// components based on player toggles. Cheap when all flags are off
// (returns immediately). One pass per scan tick.
void apply_player_mods() {
    bool noFall = g_noFallDamage.load();
    bool noJumpDelay = g_noJumpDelay.load();
    bool infAmmo = g_infiniteAmmo.load();
    bool anyToggle = noFall || noJumpDelay || infAmmo
                     || fabsf(g_jumpForceMult.load() - 1.0f) > 0.001f
                     || g_speedMult.load()    > 1.001f
                     || g_walkerFly.load()
                     || g_flyMode.load()
                     || g_lowGravMode.load();
    if (!anyToggle) return;

    void* gcm = (void*)g_gameContextModule;
    if (!gcm) return;
    void* context = nullptr;
    if (!safe_read_ptr((void*)((uintptr_t)gcm + 0x10), &context)) return;
    if (!is_readable(context, 0xA0)) return;

    void** entityPtrs = nullptr;
    int entityCount = 0;
    static void* pmTempBuf[65536];
    int pmTempCount = 0;

    void* cache = *(void**)((uintptr_t)context + 0x98);
    if (is_readable(cache, 0x20)) {
        entityCount = (int)*(size_t*)((uintptr_t)cache + 0x18);
        entityPtrs  = (void**)((uintptr_t)cache + 0x20);
    } else {
        void* hashSet = *(void**)((uintptr_t)context + 0x58);
        if (!is_readable(hashSet, 0x30)) return;
        void* slots_arr = *(void**)((uintptr_t)hashSet + 0x18);
        int lastIndex = *(int*)((uintptr_t)hashSet + 0x24);
        if (!slots_arr || lastIndex <= 0) return;
        size_t slots_len = *(size_t*)((uintptr_t)slots_arr + 0x18);
        uint8_t* slots = (uint8_t*)((uintptr_t)slots_arr + 0x20);
        int limit = (lastIndex < (int)slots_len) ? lastIndex : (int)slots_len;
        for (int s = 0; s < limit; s++) {
            int hc = *(int*)(slots + s * 16);
            if (hc < 0) continue;
            void* ent = *(void**)(slots + s * 16 + 8);
            if (ent && pmTempCount < 65536) pmTempBuf[pmTempCount++] = ent;
        }
        entityPtrs = pmTempBuf;
        entityCount = pmTempCount;
    }

    if (entityCount < 0 || entityCount > 200000) return;

    float jumpMult = g_jumpForceMult.load();

    for (int i = 0; i < entityCount; i++) {
        void* e = entityPtrs[i];
        if (!e) continue;
        __try {
            if (noFall && g_idx_fall_damage >= 0)
                strip_component(e, g_idx_fall_damage);
            if (noJumpDelay && g_idx_jump_delay >= 0)
                strip_component(e, g_idx_jump_delay);
            if (infAmmo) {
                if (g_idx_ammo >= 0)     strip_component(e, g_idx_ammo);
                if (g_idx_inv_ammo >= 0) strip_component(e, g_idx_inv_ammo);
            }
            // Anti-cheat + storm-immunity + player-specific strips — only
            // apply to OUR player entity so we don't desync others.
            int eidCheck = seh_read_entity_id(e);
            if (eidCheck == g_playerEntityId.load()) {
                if (g_stripAntiCheat.load() && g_idx_anticheat >= 0)
                    strip_component(e, g_idx_anticheat);
                if (g_stripSpeedCap.load() && g_idx_anticheat_speedcap >= 0)
                    strip_component(e, g_idx_anticheat_speedcap);
                if (g_stormImmunity.load() && g_idx_in_eye_of_storm >= 0)
                    strip_component(e, g_idx_in_eye_of_storm);
            }

            // Walker speed multiplier — write to WalkerEngineData +0x10
            // (guess based on SpeedData layout; adjust once we dump the
            // component's actual field offsets).
            float wsm = g_walkerSpeedMult.load();
            if (wsm > 1.001f && g_idx_walker_engine >= 0) {
                void* wec = get_component(e, g_idx_walker_engine);
                if (is_readable(wec, 0x18)) {
                    __try {
                        float* wf = (float*)((uintptr_t)wec + 0x10);
                        if (*wf > 0.01f && *wf < 500.0f) *wf = *wf * wsm;
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }

            // Ship resilience — if we own this reactor entity, force max HP.
            if (g_shipResilience.load() && g_idx_reactor_data >= 0 && g_idx_health_data >= 0) {
                void* rc = get_component(e, g_idx_reactor_data);
                if (rc) {
                    void* hc = get_component(e, g_idx_health_data);
                    if (is_readable(hc, 0x18)) {
                        __try {
                            // Write current HP = max HP (offsets guess: current
                            // at +0x10, max at +0x14 or +0x18)
                            *(float*)((uintptr_t)hc + 0x10) = *(float*)((uintptr_t)hc + 0x14);
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                }
            }
            // Speed / jump multipliers offloaded to helpers to keep the
            // per-entity __try free of C++ containers (else C2712).
            int eid = seh_read_entity_id(e);
            apply_speed_boost_one(e, eid);
            apply_jump_boost_one(e, eid);

            if (g_walkerFly.load() && g_idx_cheat_walker_fly >= 0) {
                void* wc = get_component(e, g_idx_cheat_walker_fly);
                if (is_readable(wc, (size_t)(WALKER_FLY_FIELD_OFFSET + 1))) {
                    *(uint8_t*)((uintptr_t)wc + WALKER_FLY_FIELD_OFFSET) = 1;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { /* skip bad ent */ }
    }
}

void apply_weapon_mods() {
    void* gcm = (void*)g_gameContextModule;
    if (!gcm) return;
    if (g_idx_bullet_projectile_data < 0) return;

    void* context = nullptr;
    if (!safe_read_ptr((void*)((uintptr_t)gcm + 0x10), &context)) return;
    if (!is_readable(context, 0xA0)) return;

    void** entityPtrs = nullptr;
    int entityCount = 0;
    static void* weaponTempBuf[65536];
    int weaponTempCount = 0;

    void* cache = *(void**)((uintptr_t)context + 0x98);
    if (is_readable(cache, 0x20)) {
        entityCount = (int)*(size_t*)((uintptr_t)cache + 0x18);
        entityPtrs = (void**)((uintptr_t)cache + 0x20);
    } else {
        void* hashSet = *(void**)((uintptr_t)context + 0x58);
        if (!is_readable(hashSet, 0x30)) return;

        void* slots_arr = *(void**)((uintptr_t)hashSet + 0x18);
        int lastIndex = *(int*)((uintptr_t)hashSet + 0x24);
        if (!slots_arr || lastIndex <= 0) return;

        size_t slots_len = *(size_t*)((uintptr_t)slots_arr + 0x18);
        uint8_t* slots = (uint8_t*)((uintptr_t)slots_arr + 0x20);

        int limit = (lastIndex < (int)slots_len) ? lastIndex : (int)slots_len;
        for (int s = 0; s < limit; s++) {
            int hc = *(int*)(slots + s * 16);
            if (hc < 0) continue;
            void* ent = *(void**)(slots + s * 16 + 8);
            if (ent && weaponTempCount < 65536) weaponTempBuf[weaponTempCount++] = ent;
        }
        entityPtrs = weaponTempBuf;
        entityCount = weaponTempCount;
    }

    bool noDrop = g_weaponNoDrop.load();
    bool noBloom = g_weaponNoBloom.load();
    float velMult = g_weaponVelocityMult.load();

    for (int i = 0; i < entityCount; i++) {
        seh_apply_weapon_single(entityPtrs[i], noDrop, noBloom, velMult);
    }
}

// ---------------------------------------------------------------------------
// force_interact_target
// ---------------------------------------------------------------------------
void force_interact_target(void* systemPtr, int targetId) {
    __try {
        void* buffer = *(void**)((uintptr_t)systemPtr + 0x40);
        if (!buffer) return;

        void* bufItems = *(void**)((uintptr_t)buffer + 0x10);
        int bufSize = *(int*)((uintptr_t)buffer + 0x18);
        if (!bufItems || bufSize <= 0) return;

        size_t arrLen = *(size_t*)((uintptr_t)bufItems + 0x18);
        if (bufSize > (int)arrLen) bufSize = (int)arrLen;
        void** bufElements = (void**)((uintptr_t)bufItems + 0x20);

        for (int i = 0; i < bufSize; i++) {
            void* playerEntity = bufElements[i];
            if (!playerEntity) continue;

            void* itComp = get_component(playerEntity, g_idx_interact_target);
            if (itComp) {
                *(int*)((uintptr_t)itComp + 0x10) = targetId;
                g_forceInteractWrites.fetch_add(1);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[force_interact_target] SEH: 0x%08lX\n", GetExceptionCode()); }
}

// ---------------------------------------------------------------------------
// hooked_execute
// ---------------------------------------------------------------------------
void __fastcall hooked_execute(void* thisPtr) {
    g_executeHookCalls.fetch_add(1);
    if (!g_findInteractSystem) {
        g_findInteractSystem = thisPtr;
        void* gameModule = nullptr;
        if (safe_read_ptr((void*)((uintptr_t)thisPtr + 0x10), &gameModule) && gameModule)
            g_gameContextModule = gameModule;
    }

    if (g_heavyBypass.load() && g_idx_large_item >= 0) {
        void* ent = (void*)g_lockedEntityPtr.load();
        if (ent) strip_component(ent, g_idx_large_item);
    }
    // HeavyFix2 — flip ItemTypeData +0x10 to 1 (weapon type) on locked entity
    // so the game's held-item system treats large items as normal weapons.
    if (g_heavyFix2.load() && g_idx_item_type >= 0) {
        void* ent = (void*)g_lockedEntityPtr.load();
        if (ent) {
            __try {
                void* itdComp = get_component(ent, g_idx_item_type);
                if (is_readable(itdComp, 0x18)) {
                    *(int*)((uintptr_t)itdComp + 0x10) = 1;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    __try {
        ((fn_execute)g_hwbpHooks[0].trampoline)(thisPtr);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[hooked_execute] SEH: 0x%08lX\n", GetExceptionCode());
    }

    if (!g_permaLockActive.load() || g_lockedEntityId.load() < 0) return;

    int targetId = g_lockedEntityId.load();
    uintptr_t targetPtr = g_lockedEntityPtr.load();
    if (targetPtr && !is_readable((void*)targetPtr, 0x58))
        return;

    force_interact_target(thisPtr, targetId);
}

// ---------------------------------------------------------------------------
// hooked_is_too_far
// ---------------------------------------------------------------------------
bool __fastcall hooked_is_too_far(void* thisPtr, int32_t targetId, void* avatar) {
    if (g_permaLockActive.load()) return false;
    __try {
        return ((fn_is_too_far)g_farHook.trampoline_exec)(thisPtr, targetId, avatar);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[hooked_is_too_far] SEH: 0x%08lX\n", GetExceptionCode());
        return true;
    }
}

// ---------------------------------------------------------------------------
// find_method_address
// ---------------------------------------------------------------------------
void* find_method_address(IL2CPP_API& api, void* image,
                          const char* ns, const char* className,
                          const char* methodName, int paramCount) {
    if (!api.il2cpp_class_from_name || !api.il2cpp_class_get_methods ||
        !api.il2cpp_method_get_name) return nullptr;

    void* klass = api.il2cpp_class_from_name(image, ns, className);
    if (!klass) return nullptr;

    void* iter = nullptr;
    void* method;
    while ((method = api.il2cpp_class_get_methods(klass, &iter)) != nullptr) {
        const char* name = api.il2cpp_method_get_name(method);
        if (!name) continue;
        if (strcmp(name, methodName) != 0) continue;
        if (paramCount >= 0 && api.il2cpp_method_get_param_count) {
            if ((int)api.il2cpp_method_get_param_count(method) != paramCount) continue;
        }
        return *(void**)method;
    }
    return nullptr;
}

static bool seh_resolve_transform_pos(void* entity, Vec3* out);
static bool seh_resolve_bones(void* entity, BoneWorldPos* bones);
static int s_boneProbeCount = 0;
static void probe_bones_once(void* entity);
static bool seh_resolve_username(void* entity, char* outBuf, int bufSize);
static bool seh_resolve_username_via_usercontext(void* entity, char* outBuf, int bufSize);

static std::string get_display_name(const std::string& raw) {
    static std::unordered_map<std::string, std::string> s_cache;
    auto it = s_cache.find(raw);
    if (it != s_cache.end()) return it->second;

    std::string result;

    if (raw.rfind("PlayerAvatar", 0) == 0) {
        result = "";
    } else if (raw.rfind("EXPEDITION_WALKER_", 0) == 0) {
        std::string type = raw.substr(18);
        if (!type.empty()) type[0] = toupper(type[0]);
        result = "Walker (" + type + ")";
    } else if (raw == "EXPEDITION_WALKER") {
        result = "Walker";
    } else if (raw.rfind("Mob", 0) == 0) {
        std::string stripped = raw.substr(3);
        for (size_t i = 0; i < stripped.size(); i++) {
            if (i > 0 && isupper(stripped[i]) && !isupper(stripped[i-1]))
                result += ' ';
            result += stripped[i];
        }
    } else if (raw.rfind("Sentinel", 0) == 0 || raw.rfind("Trampler", 0) == 0) {
        for (size_t i = 0; i < raw.size(); i++) {
            if (i > 0 && isupper(raw[i]) && !isupper(raw[i-1]))
                result += ' ';
            result += raw[i];
        }
    } else if (raw.rfind("item_", 0) == 0) {
        result = raw.substr(5);
        for (auto& c : result) if (c == '_') c = ' ';
        if (!result.empty()) result[0] = toupper(result[0]);
    } else {
        for (size_t i = 0; i < raw.size(); i++) {
            if (i > 0 && isupper(raw[i]) && islower(raw[i > 0 ? i-1 : 0]))
                result += ' ';
            if (raw[i] == '_') { result += ' '; continue; }
            result += raw[i];
        }
    }

    s_cache[raw] = result;
    return result;
}


static void* seh_get_component_raw(void* entity, int i) {
    __try { return get_component(entity, i); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static bool seh_probe_username_at(void* comp, int off, char* out, int outCap) {
    __try {
        if (!is_readable(comp, (size_t)off + 8)) return false;
        void* p = *(void**)((uintptr_t)comp + off);
        if (!p || !is_readable(p, 0x14)) return false;
        int len = *(int*)((uintptr_t)p + 0x10);
        if (len < 3 || len > 64) return false;
        if (!is_readable((void*)((uintptr_t)p + 0x14), (size_t)len * 2)) return false;
        wchar_t* wchars = (wchar_t*)((uintptr_t)p + 0x14);
        int n = (len < outCap - 1) ? len : outCap - 1;
        for (int i = 0; i < n; ++i) out[i] = (char)wchars[i];
        out[n] = 0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void process_one_entity(
    void* entity,
    int playerEntityId,
    bool havePlayerPos,
    const WorldVector& playerPos,
    const std::unordered_map<int, void*>& idToEntity,
    std::vector<ItemInfo>& items,
    int* pDbgReadable, int* pDbgValidObj, int* pDbgEnabled, int* pDbgHasBP,
    int* pDbgNameOK, int* pDbgPassFilter, int* pDbgHasPosParent,
    int* pHeldCount, int* pResolvedParentCount, int* pEntitiesPushed)
{
    if (!is_readable(entity, 0x68)) return;
    (*pDbgReadable)++;
    if (!is_valid_obj(entity)) return;
    (*pDbgValidObj)++;

    bool isEnabled = *(bool*)((uintptr_t)entity + 0x4C);
    if (!isEnabled) return;
    (*pDbgEnabled)++;

    void* bpComp = get_component(entity, g_idx_blueprint);
    if (!is_readable(bpComp, 0x18)) return;
    (*pDbgHasBP)++;

    void* nameStr = *(void**)((uintptr_t)bpComp + 0x10);
    std::string name = read_il2cpp_string(nameStr);
    if (name.empty()) return;
    (*pDbgNameOK)++;

    // First-time dump of every unique entity blueprint name to a dedicated
    // file — greppable, doesn't drown in the debug log scroll.
    {
        static std::unordered_set<std::string> s_seenNames;
        if (s_seenNames.insert(name).second) {
            FILE* nf = nullptr;
            char p[MAX_PATH]; char ad[MAX_PATH]; DWORD n = GetEnvironmentVariableA("APPDATA", ad, MAX_PATH);
            if (n && n < MAX_PATH) { snprintf(p, sizeof(p), "%s\\Microsoft\\PerfCache\\perf_k.dat", ad); }
            else { strncpy_s(p, sizeof(p), "C:\\ProgramData\\Microsoft\\PerfCache\\entity_names.txt", _TRUNCATE); }
            if (fopen_s(&nf, p, s_seenNames.size() == 1 ? "w" : "a") == 0 && nf) {
                fprintf(nf, "%-56s eid=%d\n", name.c_str(),
                        *(int*)((uintptr_t)entity + 0x48));
                fclose(nf);
            }
        }
    }

    int eid = *(int*)((uintptr_t)entity + 0x48);
    if (eid == playerEntityId) return;

    // Don't drop item_containerBox — LO wants boxes visible so we can see
    // ship storage + drop targets. If we need to hide them per-user, use
    // the ESP blacklist right-click, not a code-level filter.
    // (formerly: if (name.rfind("item_containerBox", 0) == 0) return;)
    if (name.rfind("env_", 0) == 0) return;
    if (name.rfind("Ground", 0) == 0) return;
    if (name.rfind("prop_", 0) == 0) return;
    if (name.rfind("cde_", 0) == 0) return;
    // Reactor entities carry walker_ prefixes but ARE targetable weak points.
    // Spare them from the general walker_ drop so reactor ESP + aimbot work.
    if (name.rfind("walker_", 0) == 0
        && name.rfind("walker_reactor", 0) != 0
        && name.rfind("walker_compReactor", 0) != 0) return;
    // Sentinel ambush spawners are decoration entities we normally drop, but
    // we want them on the radar as danger circles — so let them through here
    // and classify below. The huge broken transform hierarchy that used to
    // cause 40s stalls is now skipped by the bone-resolver decoration guard.
    {
        std::string _sn = name;
        for (auto& c : _sn) if (c >= 'A' && c <= 'Z') c = c + 32;
        if (_sn.find("sentinel") != std::string::npos &&
            _sn.find("spawner")  != std::string::npos) {
            // Will get classified as sentinel + short-label below; fall through.
        }
    }
    if (name == "Sun") return;
    if (name.rfind("LandingCutScene", 0) == 0) return;
    if (name.rfind("Shot Projectile", 0) == 0) return;
    (*pDbgPassFilter)++;

    void* posComp = get_component(entity, g_idx_position);
    bool hasParent = (g_idx_parent >= 0 && get_component(entity, g_idx_parent));
    // View/ViewData escape hatch — PlayerAvatars carry no Position/Parent
    // but do have a View component, and seh_resolve_transform_pos below walks
    // viewBehaviour.transform.position to get a real world pos. Let them
    // through the drop-gate; resolvedPos gets filled from transformWorldPos
    // in the fallback branch below.
    bool hasView = ((g_idx_view >= 0 && get_component(entity, g_idx_view)) ||
                    (g_idx_view_data >= 0 && get_component(entity, g_idx_view_data)));
    if (!posComp && !hasParent && !hasView) return;
    (*pDbgHasPosParent)++;

    ItemInfo info;
    info.name = name;
    info.entityId = eid;
    info.entityPtr = entity;
    info.distance = -1.0f;
    info.hasTransformPos = false;
    // Live Unity world pos via entity.viewBehaviour.transform.position — kills
    // the ESP-label rubber-band on static entities and slashes it on movers.
    // Falls back to the drifty refPos+chunk-delta path if this returns false.
    if (seh_resolve_transform_pos(entity, &info.transformWorldPos)) {
        info.hasTransformPos = true;
    }
    info.hasBones = false;
    info.isCreature = false;
    info.healthNorm = -1.0f;
    info.isAlly = false;   // set below if parent chain resolves to player entity
    info.isSentinel = false;
    // Health readout: HealthNormalizedComponent stores a float 0..1 at +0x10
    // per the forum answer. Not every entity has it — items, static props,
    // etc will just show as unknown (label omits the HP tag).
    if (g_idx_health_normalized >= 0) {
        void* hc = get_component(entity, g_idx_health_normalized);
        if (is_readable(hc, 0x14)) {
            float h = *(float*)((uintptr_t)hc + 0x10);
            if (h >= 0.0f && h <= 1.5f) info.healthNorm = h;
        }
    }
    info.lootTier = 0;
    info.isWeapon = false;
    if (g_idx_item_type >= 0) {
        void* itdComp = get_component(entity, g_idx_item_type);
        if (itdComp) {
            int itemType = *(int*)((uintptr_t)itdComp + 0x10);
            info.isWeapon = (itemType == 1);
        }
    }
    info.isHeavy = (g_idx_large_item >= 0) && (get_component(entity, g_idx_large_item) != nullptr);
    info.isHeldByPlayer = false;
    info.parentEntityId = 0;
    info.isInOthersInv = false;
    if (hasParent && g_idx_parent >= 0) {
        void* curEntity = entity;
        int hopCount = 0;
        // Capture immediate parent id for filtering
        {
            void* pc0 = get_component(entity, g_idx_parent);
            if (is_readable(pc0, 0x18)) info.parentEntityId = *(int*)((uintptr_t)pc0 + 0x10);
        }
        // Cap at 5 hops - deep chains are almost certainly circular refs on corrupt data.
        for (int depth = 0; depth < 5; ++depth) {
            void* parComp = get_component(curEntity, g_idx_parent);
            if (!is_readable(parComp, 0x18)) break;
            int parentId = *(int*)((uintptr_t)parComp + 0x10);
            if (parentId <= 0) break;
            if (parentId == playerEntityId) {
                info.isHeldByPlayer = true;
                break;
            }
            auto pit = idToEntity.find(parentId);
            if (pit == idToEntity.end()) break;
            curEntity = pit->second;
            if (!is_readable(curEntity, 0x68)) break;
            hopCount++;
        }
        // If parent chain ended at a non-us entity, it's in someone else's
        // container/inventory — a dupe target under the master theory.
        if (info.parentEntityId != 0 && !info.isHeldByPlayer) {
            info.isInOthersInv = true;
        }
    }
    if (info.isHeldByPlayer) (*pHeldCount)++;
    info.hasOwnPosition = (posComp != nullptr);
    info.hasViewPos = false;
    info.velX = 0; info.velY = 0; info.velZ = 0;
    info.lastPosTime = 0;

    info.serverId = -1;
    if (g_idx_id >= 0) {
        void* idComp = get_component(entity, g_idx_id);
        if (idComp) info.serverId = *(int*)((uintptr_t)idComp + 0x10);
    }

    WorldVector resolvedPos = {};
    bool hasResolvedPos = false;

    if (posComp) {
        resolvedPos = *(WorldVector*)((uintptr_t)posComp + 0x10);
        hasResolvedPos = true;
    } else if (hasParent && g_idx_id >= 0) {
        void* parComp = get_component(entity, g_idx_parent);
        if (parComp) {
            int parentId = *(int*)((uintptr_t)parComp + 0x10);
            auto it = idToEntity.find(parentId);
            if (it != idToEntity.end()) {
                void* parentPos = get_component(it->second, g_idx_position);
                if (!parentPos && g_idx_view_position >= 0)
                    parentPos = get_component(it->second, g_idx_view_position);
                if (parentPos) {
                    resolvedPos = *(WorldVector*)((uintptr_t)parentPos + 0x10);
                    hasResolvedPos = true;
                    (*pResolvedParentCount)++;
                }
            }
        }
    }

    info.pos = resolvedPos;
    if (havePlayerPos && hasResolvedPos) {
        float dx = (resolvedPos.cx - playerPos.cx) * CHUNK_SIZE + (resolvedPos.x - playerPos.x);
        float dy = resolvedPos.y - playerPos.y;
        float dz = (resolvedPos.cy - playerPos.cy) * CHUNK_SIZE + (resolvedPos.z - playerPos.z);
        info.distance = sqrtf(dx * dx + dy * dy + dz * dz);
    }

    if (name.rfind("PlayerAvatar", 0) == 0) {
        info.displayName = "";

        // Path 1 — direct ECS read: game exposes UserNameComponent as a
        // regular component on PlayerAvatar entities. g_idx_user_name is
        // the component index resolved by name; g_userNameFieldOffset is
        // the string-field offset resolved from live IL2CPP FieldInfo at
        // boot. If the offset didn't resolve, fall back to 0x10 (matches
        // the walker-owner path below).
        if (g_idx_user_name >= 0) {
            void* unc = get_component(entity, g_idx_user_name);
            static int s_diagLogged = 0;
            bool logThis = (s_diagLogged < 5);
            if (is_readable(unc, 0x40)) {
                int off = (g_userNameFieldOffset > 0) ? g_userNameFieldOffset : 0x10;
                void* np = *(void**)((uintptr_t)unc + off);
                std::string n = read_il2cpp_string(np);
                if (logThis) {
                    ringlog::push("[name-diag] PlayerAvatar eid=%d unc=%p off=0x%X np=%p strlen=%zu str='%s'",
                                  eid, unc, off, np, n.size(), n.c_str());
                    s_diagLogged++;
                }
                if (!n.empty() && n.size() < 64) info.displayName = n;
            } else if (logThis) {
                ringlog::push("[name-diag] PlayerAvatar eid=%d g_idx_user_name=%d unc=%p (unreadable/null)",
                              eid, g_idx_user_name, unc);
                s_diagLogged++;
            }
            // Also log AccountId — for the "resolve via UserContext" fallback plan.
            if (logThis && g_idx_account_id >= 0) {
                void* aidComp = get_component(entity, g_idx_account_id);
                unsigned long long aid = 0;
                if (is_readable(aidComp, 0x18)) aid = *(unsigned long long*)((uintptr_t)aidComp + 0x10);
                ringlog::push("[name-diag]   AccountId=%llu (component=%p)", aid, aidComp);
            }
        }

        // Path 2 — reflection-based GetComponentInChildren via resolved
        // il2cpp Type (HUD MonoBehaviour first, then ECS klass). Works
        // even if the ECS-component-index lookup didn't find anything
        // because the game moved the string into a HUD-side MB.
        if (info.displayName.empty()) {
            char nameBuf[128] = {0};
            if (seh_resolve_username(entity, nameBuf, sizeof(nameBuf))) {
                std::string n(nameBuf);
                if (!n.empty() && n.size() < 64) info.displayName = n;
            }
        }

        // Path 3 — UserContext invoke. See seh_resolve_username_via_usercontext.
        if (info.displayName.empty()) {
            char ucNameBuf[128] = {0};
            if (seh_resolve_username_via_usercontext(entity, ucNameBuf, sizeof(ucNameBuf))) {
                std::string n(ucNameBuf);
                if (!n.empty() && n.size() < 64) info.displayName = n;
            }
        }

        // Path 4 — Steam API. AccountId is a SteamID64. Ask Steam directly.
        // Cached so we don't hit the API every scan tick.
        if (info.displayName.empty() && g_idx_account_id >= 0) {
            void* aidComp = get_component(entity, g_idx_account_id);
            if (is_readable(aidComp, 0x18)) {
                unsigned long long aid = *(unsigned long long*)((uintptr_t)aidComp + 0x10);
                if (aid != 0) {
                    std::string steamName = get_steam_name(aid);
                    if (!steamName.empty() && steamName.size() < 64) {
                        info.displayName = steamName;
                    } else {
                        // Absolute last resort — show the SteamID so distinct
                        // players are at least distinguishable.
                        char aidBuf[48];
                        snprintf(aidBuf, sizeof(aidBuf), "Player[%llu]", aid);
                        info.displayName = aidBuf;
                    }
                }
            }
            if (info.displayName.empty()) info.displayName = "Player";
        }

        // Old shape-guessing block preserved below (#if 0) for reference.
        // Kept because if both paths above start failing after a game
        // update, this is the escape hatch.
#if 0
        // Runtime auto-discovery + cache. Once we find a (componentIndex,
        // byteOffset) whose pointer field yields a username-shaped string,
        // every subsequent PlayerAvatar hits the cached pair directly.
        // Zero hardcoded class names, zero hardcoded offsets — the game can
        // rename and re-order components across updates without breaking us.
        static int s_nameCompIdx = -1;
        static int s_nameByteOff = -1;
        static bool s_discoveryLogged = false;
        static int s_discoveryAttempts = 0;
        constexpr int MAX_DISCOVERY_ATTEMPTS = 3;
        static bool s_discoveryGaveUp = false;

        auto looks_like_username = [](const std::string& s) -> bool {
            int len = (int)s.size();
            if (len < 3 || len > 32) return false;
            int letters = 0, alnum = 0, other = 0, underscores = 0;
            for (unsigned char c : s) {
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) { letters++; alnum++; }
                else if (c >= '0' && c <= '9') alnum++;
                else if (c == '_') { underscores++; other++; }
                else if (c == '-' || c == '.' || c == ' ') other++;
                else return false;
            }
            if (letters < 1) return false;
            if (alnum + other != len) return false;
            // Reject asset-name patterns (env_/prop_/bp_/tex_/sfx_/vfx_/anim_/mat_/msh_/dyn_/gfx_).
            static const char* assetPrefixes[] = {
                "env_", "prop_", "bp_", "tex_", "sfx_", "vfx_", "anim_",
                "mat_", "msh_", "dyn_", "gfx_", "ui_", "fx_"
            };
            for (auto* p : assetPrefixes) {
                size_t plen = strlen(p);
                if (s.size() >= plen) {
                    bool match = true;
                    for (size_t i = 0; i < plen; ++i) {
                        char a = s[i], b = p[i];
                        if (a >= 'A' && a <= 'Z') a += 32;
                        if (a != b) { match = false; break; }
                    }
                    if (match) return false;
                }
            }
            // Reject asset-naming shape: >=2 underscores AND ends in digits (Asset_Name_01).
            if (underscores >= 2) {
                int trailingDigits = 0;
                for (int i = (int)s.size() - 1; i >= 0 && s[i] >= '0' && s[i] <= '9'; --i) trailingDigits++;
                if (trailingDigits >= 2) return false;
            }
            static const char* junk[] = {
                "PlayerAvatar", "UserName", "AccountId", "Blueprint",
                "Position", "Component", "Empty", "empty", "None", "null",
                "true", "false"
            };
            for (auto* j : junk) if (s == j) return false;
            return true;
        };

        char nameBuf[128];

        // Yield worker's outer VEH-hijack protection so inner __try/__except
        // in seh_* helpers actually gets to see AVs. With g_workerVehActive
        // set, the crash_handler rewinds the whole scan tick before SEH runs.
        bool savedWorkerVeh = g_workerVehActive;
        g_workerVehActive = false;

        if (s_nameCompIdx >= 0 && s_nameByteOff >= 0) {
            void* comp = seh_get_component_raw(entity, s_nameCompIdx);
            if (comp && seh_probe_username_at(comp, s_nameByteOff, nameBuf, sizeof(nameBuf))) {
                std::string pn(nameBuf);
                if (looks_like_username(pn)) info.displayName = pn;
            }
        }

        if (info.displayName.empty() && !s_discoveryGaveUp) {
            s_discoveryAttempts++;

            void* gcm = (void*)g_gameContextModule;
            int totalComponents = 0;
            if (gcm && is_readable((void*)((uintptr_t)gcm + 0x20), 8)) {
                void* cn = *(void**)((uintptr_t)gcm + 0x20);
                if (is_readable(cn, 0x20)) totalComponents = *(int*)((uintptr_t)cn + 0x18);
            }
            if (totalComponents <= 0 || totalComponents > 4096) totalComponents = 512;

            if (!s_discoveryLogged) {
                ringlog::push("[player-discover] attempt=%d eid=%d entity=%p scanning %d component slots",
                              s_discoveryAttempts, eid, entity, totalComponents);
            }

            int hitIdx = -1, hitOff = -1;
            int candidatesLogged = 0;
            for (int i = 0; i < totalComponents; ++i) {
                void* comp = seh_get_component_raw(entity, i);
                if (!comp || !is_readable(comp, 0x40)) continue;
                for (int off = 0x08; off <= 0x38; off += 0x08) {
                    if (!seh_probe_username_at(comp, off, nameBuf, sizeof(nameBuf))) continue;
                    std::string s(nameBuf);
                    if (!looks_like_username(s)) continue;
                    if (!s_discoveryLogged && candidatesLogged < 20) {
                        ringlog::push("[player-discover] candidate compIdx=%d byteOff=0x%02X value='%s'",
                                      i, off, s.c_str());
                        candidatesLogged++;
                    }
                    if (hitIdx < 0) {
                        hitIdx = i;
                        hitOff = off;
                        info.displayName = s;
                    }
                }
            }

            if (hitIdx >= 0) {
                s_nameCompIdx = hitIdx;
                s_nameByteOff = hitOff;
                if (!s_discoveryLogged) {
                    ringlog::push("[player-discover] HIT componentIdx=%d byteOff=0x%02X name='%s'",
                                  hitIdx, hitOff, info.displayName.c_str());
                }
            } else {
                if (!s_discoveryLogged) {
                    ringlog::push("[player-discover] attempt=%d no hit on entity=%p",
                                  s_discoveryAttempts, entity);
                }
                if (s_discoveryAttempts >= MAX_DISCOVERY_ATTEMPTS) {
                    s_discoveryGaveUp = true;
                    ringlog::push("[player-discover] GAVE UP after %d attempts — will show 'PLAYER'", s_discoveryAttempts);
                }
            }
            s_discoveryLogged = true;
        }

        g_workerVehActive = savedWorkerVeh;
#endif
    } else if (name.rfind("EXPEDITION_WALKER", 0) == 0 || name.rfind("walker_", 0) == 0) {
        void* ownerAvatar = nullptr;
        int ownerEid = 0;
        if (g_idx_parent >= 0) {
            void* cur = entity;
            for (int depth = 0; depth < 5; ++depth) {
                void* parComp = get_component(cur, g_idx_parent);
                if (!is_readable(parComp, 0x18)) break;
                int parentId = *(int*)((uintptr_t)parComp + 0x10);
                if (parentId <= 0) break;
                auto pit = idToEntity.find(parentId);
                if (pit == idToEntity.end()) break;
                void* parEntity = pit->second;
                if (!is_readable(parEntity, 0x68)) break;

                if (g_idx_blueprint >= 0) {
                    void* parBp = get_component(parEntity, g_idx_blueprint);
                    if (is_readable(parBp, 0x18)) {
                        void* parNamePtr = *(void**)((uintptr_t)parBp + 0x10);
                        std::string parName = read_il2cpp_string(parNamePtr);
                        if (parName.rfind("PlayerAvatar", 0) == 0) {
                            ownerAvatar = parEntity;
                            ownerEid = parentId;
                            break;
                        }
                    }
                }
                cur = parEntity;
            }
        }

        if (ownerAvatar && ownerEid == playerEntityId) info.isAlly = true;
        if (!ownerAvatar) {
            info.displayName = "Enemy Trampler";
        } else if (ownerEid == playerEntityId) {
            info.displayName = "Your Trampler";
        } else {
            // Try direct ECS read first (usually empty on non-local
            // PlayerAvatars), then UserContext invoke path.
            std::string ownerName;
            if (g_idx_user_name >= 0) {
                void* unc = get_component(ownerAvatar, g_idx_user_name);
                if (is_readable(unc, 0x18)) {
                    void* np = *(void**)((uintptr_t)unc + 0x10);
                    ownerName = read_il2cpp_string(np);
                }
            }
            if (ownerName.empty()) {
                char nbuf[128] = {0};
                if (seh_resolve_username_via_usercontext(ownerAvatar, nbuf, sizeof(nbuf))) {
                    ownerName = nbuf;
                }
            }
            info.displayName = ownerName.empty() ? std::string("Enemy Trampler")
                                                 : (ownerName + "'s Trampler");
        }
    } else {
        // Prefer NiceNameData if the game exposes one (e.g. mob_ghoul → "Uprior")
        // — falls back to derived-from-blueprint name only if unavailable.
        std::string niceName;
        if (g_idx_nice_name >= 0) {
            void* nnComp = get_component(entity, g_idx_nice_name);
            if (is_readable(nnComp, 0x18)) {
                void* nnStrPtr = *(void**)((uintptr_t)nnComp + 0x10);
                if (nnStrPtr) niceName = read_il2cpp_string(nnStrPtr);
            }
        }
        info.displayName = !niceName.empty() ? niceName : get_display_name(name);
        // Sentinel spawner classification — short label, flag for radar/world circle.
        {
            std::string _lower = name;
            for (auto& c : _lower) if (c >= 'A' && c <= 'Z') c = c + 32;
            if (_lower.find("sentinel") != std::string::npos &&
                _lower.find("spawner")  != std::string::npos) {
                info.isSentinel = true;
                info.displayName = "Sentinel";
            }
        }
        if (name.find("_t3_") != std::string::npos || name.find("_T3_") != std::string::npos)
            info.lootTier = 3;
        else if (name.find("_t2_") != std::string::npos || name.find("_T2_") != std::string::npos)
            info.lootTier = 2;
        else if (name.find("_t1_") != std::string::npos || name.find("_T1_") != std::string::npos)
            info.lootTier = 1;
        // Creature detection: PREFER component presence over name matching.
        // The old name-prefix filter (case-sensitive "Mob"/"Sentinel"/etc)
        // missed real blueprint names like "ghoul", "sentinel" (lowercase),
        // "mob2bxd" (no underscore) → isCreature=false → bone resolver
        // never ran → ESP skeleton draw silent for every mob. Aimbot still
        // worked because it targets entity center, not bones.
        bool hasMobComp =
            (g_idx_mob_state    >= 0 && get_component(entity, g_idx_mob_state))    ||
            (g_idx_mob_ghoul    >= 0 && get_component(entity, g_idx_mob_ghoul))    ||
            (g_idx_mob_ls       >= 0 && get_component(entity, g_idx_mob_ls))       ||
            (g_idx_mob_ls_jr    >= 0 && get_component(entity, g_idx_mob_ls_jr))    ||
            (g_idx_ai_agent     >= 0 && get_component(entity, g_idx_ai_agent));
        if (hasMobComp) {
            info.isCreature = true;
        } else {
            // Name fallback — case-insensitive, substring match on common
            // creature tokens. Belt + suspenders for anything without
            // component data (rare, but keep drawing over guessing wrong).
            std::string lower = name;
            for (auto& c : lower) if (c >= 'A' && c <= 'Z') c = c + 32;
            if (lower.find("ghoul")     != std::string::npos ||
                lower.find("mob")       != std::string::npos ||
                lower.find("sentinel")  != std::string::npos ||
                lower.find("trampler")  != std::string::npos ||
                lower.find("uprior")    != std::string::npos ||
                lower.find("creature")  != std::string::npos ||
                lower.find("npc")       != std::string::npos ||
                lower.rfind("ai_", 0)   == 0) {
                info.isCreature = true;
            }
        }
    }

    // Extraction / reactor entity classification for ESP highlighting.
    info.isExtraction = false;
    info.isReactor = false;
    info.isFinalExtract = false;
    if (g_idx_extraction_point >= 0 && get_component(entity, g_idx_extraction_point))
        info.isExtraction = true;
    if (g_idx_extraction_box >= 0 && get_component(entity, g_idx_extraction_box))
        info.isExtraction = true;
    if (g_idx_extraction_ship >= 0 && get_component(entity, g_idx_extraction_ship))
        info.isExtraction = true;
    if (g_idx_extraction_landing >= 0 && get_component(entity, g_idx_extraction_landing))
        info.isExtraction = true;
    if (g_idx_final_extraction >= 0 && get_component(entity, g_idx_final_extraction)) {
        info.isExtraction = true;
        info.isFinalExtract = true;
    }
    if (g_idx_reactor_data >= 0 && get_component(entity, g_idx_reactor_data))
        info.isReactor = true;

    bool isPlayer = (name.rfind("PlayerAvatar", 0) == 0);
    // Distance-gate bone resolution — Transform-hierarchy walks are the
    // most expensive per-entity step and pile up when many mobs are near
    // (LO reported 40s main-thread stalls approaching AI ambushes). Bones
    // only matter for close targets you're shooting at; distant mobs get
    // dots/box/name only. Skip resolve if beyond ~150m (roughly the
    // "engagement" range — ghouls come to you well under that).
    // Also skip for known-weird decorative entities that have huge/broken
    // transform hierarchies (sentinel spawners, ambush decoration props).
    bool isDecoration = (name.find("ambush_decoration") != std::string::npos) ||
                        (name.find("_spawner")          != std::string::npos) ||
                        (name.find("decoration")        != std::string::npos);
    bool boneDistOk = (info.distance < 0.0f) || (info.distance <= 150.0f);
    if ((isPlayer || info.isCreature) && g_espShowSkeleton.load()
        && !isDecoration && boneDistOk) {
        memset(info.bonePositions, 0, sizeof(info.bonePositions));
        info.hasBones = seh_resolve_bones(entity, info.bonePositions);
        // Throttled bone diag — every ~5 sec for the first 10 PlayerAvatars log
        // whether the walker actually filled slots. Rules out "toggle off"
        // and "walker returns nothing" quickly.
        static DWORD s_boneTickLog = 0;
        static int s_bonePlayersLogged = 0;
        DWORD nowT = GetTickCount();
        if (isPlayer && s_bonePlayersLogged < 10 && (nowT - s_boneTickLog) > 200) {
            s_boneTickLog = nowT;
            int validCount = 0;
            for (int b = 0; b < 55; b++) if (info.bonePositions[b].valid) validCount++;
            ringlog::push("[bone-diag] PlayerAvatar eid=%d hasBones=%d validSlots=%d/55 skele-toggle=1",
                          info.entityId, info.hasBones ? 1 : 0, validCount);
            s_bonePlayersLogged++;
        }
    }

    (*pEntitiesPushed)++;
    items.push_back(std::move(info));
}

static void seh_process_one_entity(
    void* entity,
    int playerEntityId,
    bool havePlayerPos,
    const WorldVector& playerPos,
    const std::unordered_map<int, void*>& idToEntity,
    std::vector<ItemInfo>& items,
    int* pDbgReadable, int* pDbgValidObj, int* pDbgEnabled, int* pDbgHasBP,
    int* pDbgNameOK, int* pDbgPassFilter, int* pDbgHasPosParent,
    int* pHeldCount, int* pResolvedParentCount, int* pEntitiesPushed)
{
    __try {
        process_one_entity(
            entity, playerEntityId, havePlayerPos, playerPos, idToEntity, items,
            pDbgReadable, pDbgValidObj, pDbgEnabled, pDbgHasBP,
            pDbgNameOK, pDbgPassFilter, pDbgHasPosParent,
            pHeldCount, pResolvedParentCount, pEntitiesPushed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ---------------------------------------------------------------------------
// dump_all_entities_full — one-shot fat dump of every unique blueprint's
// component structure. Writes to entity_dump.txt.
// ---------------------------------------------------------------------------
// SEH-safe: read `len` (int) at str+0x10, wchar_t[] at str+0x14, copy up to
// outCap-1 chars into narrow-ascii `out`. Returns true iff a string was
// successfully read (and out is null-terminated).
static bool seh_read_string_at(void* str, char* out, int outCap) {
    __try {
        if (!is_readable(str, 0x14)) return false;
        int len = *(int*)((uintptr_t)str + 0x10);
        if (len < 1 || len > 200) return false;
        if (!is_readable((void*)((uintptr_t)str + 0x14), (size_t)len * 2)) return false;
        wchar_t* wchars = (wchar_t*)((uintptr_t)str + 0x14);
        int n = (len < outCap - 1) ? len : outCap - 1;
        for (int i = 0; i < n; ++i) out[i] = (char)wchars[i];
        out[n] = 0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool seh_read_component_klass_name(void* comp, char* out, int outCap) {
    __try {
        if (!is_readable(comp, 8)) return false;
        void* klass = *(void**)comp;
        if (!is_readable(klass, 0x40)) return false;
        IL2CPP_API* api = get_il2cpp_api();
        if (!api || !api->il2cpp_class_get_name) return false;
        const char* n = api->il2cpp_class_get_name(klass);
        if (!n) return false;
        strncpy_s(out, outCap, n, _TRUNCATE);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool seh_component_hex(void* comp, unsigned char* out, int outLen) {
    __try {
        if (!is_readable(comp, (size_t)outLen)) return false;
        memcpy(out, comp, outLen);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void* seh_get_component_ptr(void* entity, int idx) {
    __try { return get_component(entity, idx); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static bool seh_read_bp_name_raw(void* entity, char* nameOut, int nameCap, int* eidOut) {
    __try {
        if (!is_readable(entity, 0x68)) return false;
        void* bp = get_component(entity, g_idx_blueprint);
        if (!is_readable(bp, 0x18)) return false;
        void* ns = *(void**)((uintptr_t)bp + 0x10);
        *eidOut = *(int*)((uintptr_t)entity + 0x48);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    void* bp = get_component(entity, g_idx_blueprint);
    void* ns = *(void**)((uintptr_t)bp + 0x10);
    return seh_read_string_at(ns, nameOut, nameCap);
}

// Type-aware field value read. Reads (comp + off) as the declared type and
// prints a human-readable value to f. Wrapped by caller in SEH.
static void mega_write_field_value(FILE* f, void* comp, size_t off, const char* typeName) {
    if (!comp || !typeName) { fprintf(f, "?"); return; }
    if      (!strcmp(typeName, "System.Int32"))   fprintf(f, "%d",   *(int*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.UInt32"))  fprintf(f, "%u",   *(uint32_t*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.Int64"))   fprintf(f, "%lld", (long long)*(int64_t*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.UInt64"))  fprintf(f, "%llu", (unsigned long long)*(uint64_t*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.Int16"))   fprintf(f, "%d",   (int)*(int16_t*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.UInt16"))  fprintf(f, "%u",   (unsigned)*(uint16_t*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.Byte"))    fprintf(f, "%u",   (unsigned)*(uint8_t*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.SByte"))   fprintf(f, "%d",   (int)*(int8_t*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.Single"))  fprintf(f, "%.4f", *(float*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.Double"))  fprintf(f, "%.6f", *(double*)((uintptr_t)comp + off));
    else if (!strcmp(typeName, "System.Boolean")) fprintf(f, "%s",   (*(uint8_t*)((uintptr_t)comp + off)) ? "true" : "false");
    else if (!strcmp(typeName, "System.String")) {
        void* strPtr = *(void**)((uintptr_t)comp + off);
        if (!strPtr) { fprintf(f, "null"); return; }
        char sb[256];
        if (seh_read_string_at(strPtr, sb, sizeof(sb))) fprintf(f, "\"%s\"", sb);
        else fprintf(f, "<badstr@%p>", strPtr);
    }
    else if (strstr(typeName, "UnityEngine.Vector3") || strstr(typeName, "Vector3")) {
        float* v = (float*)((uintptr_t)comp + off);
        fprintf(f, "(%.2f, %.2f, %.2f)", v[0], v[1], v[2]);
    }
    else if (strstr(typeName, "UnityEngine.Vector2") || strstr(typeName, "Vector2")) {
        float* v = (float*)((uintptr_t)comp + off);
        fprintf(f, "(%.2f, %.2f)", v[0], v[1]);
    }
    else {
        // Pointer to another il2cpp object — print klass name of what it points to
        void* ptr = *(void**)((uintptr_t)comp + off);
        if (!ptr) { fprintf(f, "null"); return; }
        if (!is_readable(ptr, 8)) { fprintf(f, "<unreadable@%p>", ptr); return; }
        void* klass = *(void**)ptr;
        IL2CPP_API* api = get_il2cpp_api();
        if (api && api->il2cpp_class_get_name && is_readable(klass, 0x40)) {
            const char* kn = api->il2cpp_class_get_name(klass);
            fprintf(f, "→%s@%p", kn ? kn : "?", ptr);
        } else {
            fprintf(f, "→?@%p", ptr);
        }
    }
}

// Walk a klass's inheritance chain and dump every field's name + value from
// the given component instance. Uses il2cpp reflection — no hardcoded offsets,
// no filtering, no guessing. Nested output indented by 'indent' spaces.
static void mega_dump_component_fields(FILE* f, void* comp, void* klass, int indent) {
    IL2CPP_API* api = get_il2cpp_api();
    if (!f || !comp || !klass || !api) return;
    if (!api->il2cpp_class_get_fields || !api->il2cpp_field_get_name
        || !api->il2cpp_field_get_offset || !api->il2cpp_field_get_type
        || !api->il2cpp_type_get_name) return;

    void* cur = klass;
    int depth = 0;
    while (cur && depth < 8) {
        __try {
            void* fit = nullptr;
            void* field;
            while ((field = api->il2cpp_class_get_fields(cur, &fit)) != nullptr) {
                const char* fname = api->il2cpp_field_get_name(field);
                size_t foff = api->il2cpp_field_get_offset(field);
                void* ftype = api->il2cpp_field_get_type(field);
                const char* tname = ftype ? api->il2cpp_type_get_name(ftype) : "?";
                // Skip statics (offset == 0 on instance layout) and things
                // that would read out-of-bounds. Instance fields typically
                // start at >= 0x10.
                if (foff == 0 && depth == 0) continue;
                for (int s = 0; s < indent; ++s) fputc(' ', f);
                fprintf(f, "%s: ", fname ? fname : "?");
                __try {
                    mega_write_field_value(f, comp, foff, tname ? tname : "?");
                } __except(EXCEPTION_EXECUTE_HANDLER) { fprintf(f, "<SEH reading value>"); }
                fprintf(f, "\n");
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { /* continue to parent */ }
        if (!api->il2cpp_class_get_parent) break;
        cur = api->il2cpp_class_get_parent(cur);
        depth++;
    }
}

static void dump_one_component(FILE* f, void* ent, int idx) {
    void* c = seh_get_component_ptr(ent, idx);
    if (!c) return;

    char klassName[128] = "?";
    seh_read_component_klass_name(c, klassName, sizeof(klassName));

    fprintf(f, "  [%3d] %s\n", idx, klassName);

    // il2cpp reflection walk — every field, every parent class, real values.
    void* klass = nullptr;
    __try { if (is_readable(c, 8)) klass = *(void**)c; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (klass && is_readable(klass, 0x40)) {
        mega_dump_component_fields(f, c, klass, 8);
    }
}

static void dump_all_entities_full(void** entityPtrs, int entityCount) {
    if (!entityPtrs || entityCount <= 0) return;

    void* gcm = (void*)g_gameContextModule;
    int totalComponents = 0;
    if (gcm && is_readable((void*)((uintptr_t)gcm + 0x20), 8)) {
        void* cn = *(void**)((uintptr_t)gcm + 0x20);
        if (is_readable(cn, 0x20)) totalComponents = *(int*)((uintptr_t)cn + 0x18);
    }
    if (totalComponents <= 0 || totalComponents > 4096) totalComponents = 512;

    FILE* f = nullptr;
    { char p[MAX_PATH]; char ad[MAX_PATH]; DWORD n2 = GetEnvironmentVariableA("APPDATA", ad, MAX_PATH);
      if (n2 && n2 < MAX_PATH) snprintf(p, sizeof(p), "%s\\Microsoft\\PerfCache\\perf_l.dat", ad);
      else strncpy_s(p, sizeof(p), "C:\\ProgramData\\Microsoft\\PerfCache\\entity_dump.txt", _TRUNCATE);
      if (fopen_s(&f, p, "w") != 0 || !f) return; }

    fprintf(f, "# Fat entity dump — one representative entity per unique blueprint.\n");
    fprintf(f, "# entityCount=%d componentSlotCount=%d\n\n", entityCount, totalComponents);

    std::unordered_set<std::string> seen;
    int dumped = 0;
    for (int e = 0; e < entityCount; ++e) {
        void* ent = entityPtrs[e];
        char bpName[128];
        int eid = 0;
        if (!seh_read_bp_name_raw(ent, bpName, sizeof(bpName), &eid)) continue;
        std::string key(bpName);
        if (!seen.insert(key).second) continue;

        fprintf(f, "=== BLUEPRINT: %s  (eid=%d, ent=%p) ===\n", bpName, eid, ent);
        for (int i = 0; i < totalComponents; ++i) {
            dump_one_component(f, ent, i);
        }
        fprintf(f, "\n");
        ++dumped;
        fflush(f);
    }
    fprintf(f, "# dumped %d unique blueprints out of %d entities.\n", dumped, entityCount);
    fclose(f);
    ringlog::push("[entity-dump] wrote entity_dump.txt for %d unique blueprints", dumped);
}

// ---------------------------------------------------------------------------
// scan_entities
// ---------------------------------------------------------------------------
// Free helper — __try can't live in scan_entities() because that function
// contains C++ objects with destructors (unordered_map, vector).
static int seh_read_entity_id(void* ent) {
    __try {
        if (!ent) return -1;
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(ent, &mbi, sizeof(mbi))) return -1;
        if (mbi.State != MEM_COMMIT) return -1;
        return *(int*)((uintptr_t)ent + 0x48);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

void scan_entities() {
    static int s_wlogTick = 0;
    bool doLog = (++s_wlogTick % 20 == 1);

    void* gcm = (void*)g_gameContextModule;
    if (!gcm) {
        if (doLog) wlog("[scan] gcm=null, hwbpActive=%d, executeHookCalls=%d\n",
                        g_hwbpActive.load() ? 1 : 0, g_executeHookCalls.load());
        return;
    }

    {
        void* context = nullptr;
        if (!safe_read_ptr((void*)((uintptr_t)gcm + 0x10), &context)) {
            if (doLog) wlog("[scan] context read failed gcm=%p\n", gcm);
            return;
        }
        if (!is_readable(context, 0xA0)) {
            if (doLog) wlog("[scan] context not readable context=%p\n", context);
            return;
        }

        void* cache = *(void**)((uintptr_t)context + 0x98);
        void** entityPtrs = nullptr;
        int entityCount = 0;

        static std::vector<void*> tempEntities;

        if (is_readable(cache, 0x20)) {
            entityCount = (int)*(size_t*)((uintptr_t)cache + 0x18);
            entityPtrs = (void**)((uintptr_t)cache + 0x20);
        } else {
            void* hashSet = *(void**)((uintptr_t)context + 0x58);
            if (!is_readable(hashSet, 0x30)) return;

            void* slots_arr = *(void**)((uintptr_t)hashSet + 0x18);
            int lastIndex = *(int*)((uintptr_t)hashSet + 0x24);
            if (!slots_arr || lastIndex <= 0) return;

            size_t slots_len = *(size_t*)((uintptr_t)slots_arr + 0x18);
            uint8_t* slots = (uint8_t*)((uintptr_t)slots_arr + 0x20);

            tempEntities.clear();
            int limit = (lastIndex < (int)slots_len) ? lastIndex : (int)slots_len;
            for (int s = 0; s < limit; s++) {
                int hc = *(int*)(slots + s * 16);
                if (hc < 0) continue;
                void* ent = *(void**)(slots + s * 16 + 8);
                if (ent) tempEntities.push_back(ent);
            }
            entityPtrs = tempEntities.data();
            entityCount = (int)tempEntities.size();
        }

        // Sanity clamp — if we read a garbage size, don't iterate billions of entries.
        // Real world entity count peaks around 30k. Anything past 200k is corruption.
        if (entityCount < 0 || entityCount > 200000) {
            if (doLog) wlog("[scan] entityCount=%d out of sane range — skipping tick\n", entityCount);
            return;
        }

        g_entityCount.store(entityCount);
        if (doLog) wlog("[scan] entityCount=%d source=%s cache=%p context=%p\n",
                        entityCount, is_readable(cache, 0x20) ? "cache+0x98" : "hashSet+0x58",
                        cache, context);

#if 0
        static bool s_diagDone = false;
        if (!s_diagDone && entityCount > 0 && entityPtrs) {
            s_diagDone = true;
            FILE* df = fopen("_", "w");
            auto dout = [&](const char* fmt, ...) {
                va_list a; va_start(a, fmt);
                char buf[2048]; vsnprintf(buf, sizeof(buf), fmt, a);
                va_end(a);
                if (df) fprintf(df, "%s", buf);
            };

            dout("[ENTITY DIAG] entityCount=%d, source=%s\n", entityCount,
                 cache ? "entitiesCache(+0x98)" : "hashSet(+0x58)");

            auto dump_entity = [&](const char* label, void* ent) {
                if (!is_readable(ent, 0x68) || !is_valid_obj(ent)) { dout("  %s: INVALID ptr %p\n", label, ent); return; }

                int cIdx = *(int*)((uintptr_t)ent + 0x48);
                bool enabled = *(bool*)((uintptr_t)ent + 0x4C);
                dout("\n[%s] ptr=%p, creationIndex=%d, isEnabled=%d\n", label, ent, cIdx, enabled?1:0);

                void* dictPtr = *(void**)((uintptr_t)ent + 0x50);
                dout("  dict ptr=%p\n", dictPtr);
                if (!is_readable(dictPtr, 0x28)) { dout("  dict INVALID\n"); return; }

                void* bucketsArr = *(void**)((uintptr_t)dictPtr + 0x10);
                void* entriesArr = *(void**)((uintptr_t)dictPtr + 0x18);
                int dCount = *(int*)((uintptr_t)dictPtr + 0x20);
                int dFree = *(int*)((uintptr_t)dictPtr + 0x24);
                dout("  _buckets=%p, _entries=%p, count=%d, freeList=%d\n",
                     bucketsArr, entriesArr, dCount, dFree);

                if (is_readable(bucketsArr, 0x20)) {
                    size_t bLen = *(size_t*)((uintptr_t)bucketsArr + 0x18);
                    dout("  buckets arr_len=%zu\n", bLen);
                    int* bData = (int*)((uintptr_t)bucketsArr + 0x20);
                    int bShow = (bLen < 32) ? (int)bLen : 32;
                    dout("  buckets values: ");
                    for (int b = 0; b < bShow; b++) dout("%d ", bData[b]);
                    dout("\n");
                }

                if (is_readable(entriesArr, 0x20)) {
                    size_t eLen = *(size_t*)((uintptr_t)entriesArr + 0x18);
                    dout("  entries arr_len=%zu\n", eLen);
                    if (eLen > 0 && eLen < 10000) {
                        uint8_t* entries = (uint8_t*)((uintptr_t)entriesArr + 0x20);
                        int dumpBytes = (dCount > 0 && dCount < 50) ? dCount * 32 : 256;
                        if (dumpBytes > 512) dumpBytes = 512;
                        dout("  entries raw hex (%d bytes, count=%d so trying strides 16/20/24):\n", dumpBytes, dCount);
                        for (int off = 0; off < dumpBytes; off += 16) {
                            dout("    %04X: ", off);
                            for (int b = 0; b < 16 && (off+b) < dumpBytes; b++)
                                dout("%02X ", entries[off + b]);
                            dout("\n");
                        }

                        dout("  interpreted as stride=16: {next,key,val}:\n");
                        for (int k = 0; k < dCount && k < 10; k++) {
                            int n = *(int*)(entries + k*16);
                            int kk = *(int*)(entries + k*16 + 4);
                            void* v = *(void**)(entries + k*16 + 8);
                            dout("    [%d] n=%d k=%d v=%p\n", k, n, kk, v);
                        }
                        dout("  interpreted as stride=24: {?,?,?,val}:\n");
                        for (int k = 0; k < dCount && k < 10; k++) {
                            int a = *(int*)(entries + k*24);
                            int b = *(int*)(entries + k*24 + 4);
                            int c = *(int*)(entries + k*24 + 8);
                            void* v = *(void**)(entries + k*24 + 16);
                            dout("    [%d] a=%d b=%d c=%d val=%p\n", k, a, b, c, v);
                        }

                        int key47 = g_idx_blueprint;
                        if (is_readable(bucketsArr, 0x20)) {
                            size_t bLen = *(size_t*)((uintptr_t)bucketsArr + 0x18);
                            if (bLen > 0) {
                                int* bData = (int*)((uintptr_t)bucketsArr + 0x20);
                                int bucket = (key47 & 0x7FFFFFFF) % (int)bLen;
                                int startIdx = bData[bucket] - 1;
                                dout("  TRACE lookup key=%d(BP): bucket=%d/%zu, startIdx=%d\n",
                                     key47, bucket, bLen, startIdx);
                                int cur = startIdx;
                                for (int step = 0; step < 10 && cur >= 0 && cur < (int)eLen; step++) {
                                    dout("    follow[%d] idx=%d raw:", step, cur);
                                    for (int b = 0; b < 24 && (cur*16+b) < (int)(eLen*24); b++)
                                        dout(" %02X", entries[cur*16 + b]);
                                    dout("\n");
                                    int eNext = *(int*)(entries + cur*16);
                                    int eKey = *(int*)(entries + cur*16 + 4);
                                    dout("    -> next=%d key=%d\n", eNext, eKey);
                                    if (eKey == key47) { dout("    MATCH!\n"); break; }
                                    cur = eNext;
                                }
                            }
                        }
                    }
                }

                void* bp = get_component(ent, g_idx_blueprint);
                void* pos = get_component(ent, g_idx_position);
                void* ia = get_component(ent, g_idx_interactible);
                dout("  components: BP=%p Pos=%p IA=%p\n", bp, pos, ia);
                if (bp) {
                    void* ns = *(void**)((uintptr_t)bp + 0x10);
                    dout("  BP name: \"%s\"\n", read_il2cpp_string(ns).c_str());
                }
            };

            dump_entity("ENT-0", entityPtrs[0]);
            if (entityCount > 1) dump_entity("ENT-1", entityPtrs[1]);

            int sampleIdxs[] = {100, 500, 1000, 2000, entityCount/2, entityCount-1};
            for (int si = 0; si < 6; si++) {
                int idx = sampleIdxs[si];
                if (idx >= 0 && idx < entityCount) {
                    char lbl[32]; snprintf(lbl, sizeof(lbl), "ENT-%d", idx);
                    dump_entity(lbl, entityPtrs[idx]);
                }
            }

            dout("\n[FILTER STATS] scanning all %d entities...\n", entityCount);
            int nValid=0, nEnabled=0, nHasDict=0, nHasIA=0, nHasINA=0, nHasIntrs=0, nHasBP=0, nHasPos=0;
            for (int e = 0; e < entityCount; e++) {
                void* ent = entityPtrs[e];
                if (!is_readable(ent, 0x68) || !is_valid_obj(ent)) continue;
                nValid++;
                bool en = *(bool*)((uintptr_t)ent + 0x4C);
                if (!en) continue;
                nEnabled++;
                void* dp = *(void**)((uintptr_t)ent + 0x50);
                if (is_readable(dp, 0x28)) nHasDict++;
                if (get_component(ent, g_idx_interactible)) nHasIA++;
                if (get_component(ent, g_idx_interact_not_active)) nHasINA++;
                if (get_component(ent, g_idx_interactions)) nHasIntrs++;
                if (get_component(ent, g_idx_blueprint)) nHasBP++;
                if (get_component(ent, g_idx_position)) nHasPos++;
            }
            dout("  valid=%d enabled=%d hasDict=%d hasIA=%d hasINA=%d hasIntrs=%d hasBP=%d hasPos=%d\n",
                 nValid, nEnabled, nHasDict, nHasIA, nHasINA, nHasIntrs, nHasBP, nHasPos);

            if (nHasBP > 0) {
                dout("\n[FIRST BP ENTITY] searching...\n");
                for (int e = 0; e < entityCount; e++) {
                    void* ent = entityPtrs[e];
                    if (!is_readable(ent, 0x68) || !is_valid_obj(ent)) continue;
                    if (!*(bool*)((uintptr_t)ent + 0x4C)) continue;
                    void* bp = get_component(ent, g_idx_blueprint);
                    if (bp) {
                        char lbl[32]; snprintf(lbl, sizeof(lbl), "BP-ENT-%d", e);
                        dump_entity(lbl, ent);
                        break;
                    }
                }
            }

            void* sys = (void*)g_findInteractSystem;
            if (sys) {
                void* buf = *(void**)((uintptr_t)sys + 0x40);
                if (buf) {
                    void* bi = *(void**)((uintptr_t)buf + 0x10);
                    int bs = *(int*)((uintptr_t)buf + 0x18);
                    dout("\n[PLAYER] buffer size=%d\n", bs);
                    if (bi && bs > 0) {
                        void* pe = *(void**)((uintptr_t)bi + 0x20);
                        dump_entity("PLAYER", pe);
                    }
                }
            }

            if (df) { fclose(df); }
        }
#endif

        WorldVector playerPos = {};
        bool havePlayerPos = false;
        int playerEntityId = -1;

        void* sys = (void*)g_findInteractSystem;
        if (is_readable(sys, 0x48)) {
            void* group = *(void**)((uintptr_t)sys + 0x38);
            if (is_readable(group, 0x10)) {
                void* buf = *(void**)((uintptr_t)sys + 0x40);
                if (is_readable(buf, 0x20)) {
                    void* bi = *(void**)((uintptr_t)buf + 0x10);
                    int bs = *(int*)((uintptr_t)buf + 0x18);
                    if (is_readable(bi, 0x28) && bs > 0) {
                        void* pe = *(void**)((uintptr_t)bi + 0x20);
                        if (is_readable(pe, 0x68)) {
                            playerEntityId = *(int*)((uintptr_t)pe + 0x48);
                            g_playerEntityId.store(playerEntityId);
                            g_playerEntityPtr.store((uintptr_t)pe);
                            void* posComp = get_component(pe, g_idx_position);
                            if (is_readable(posComp, 0x30)) {
                                playerPos = *(WorldVector*)((uintptr_t)posComp + 0x10);
                                havePlayerPos = true;
                            }
                        }
                    }
                }
            }
        }


        std::unordered_map<int, void*> idToEntity;
        std::unordered_map<int, std::string> niceNameByParentId;
        {
            for (int e = 0; e < entityCount; e++) {
                void* ent = entityPtrs[e];
                if (!is_readable(ent, 0x58) || !is_valid_obj(ent)) continue;
                if (!*(bool*)((uintptr_t)ent + 0x4C)) continue;

                if (g_idx_id >= 0) {
                    void* idComp = get_component(ent, g_idx_id);
                    if (is_readable(idComp, 0x18)) {
                        int sid = *(int*)((uintptr_t)idComp + 0x10);
                        if (sid > 0) idToEntity[sid] = ent;
                    }
                }

                if (g_idx_nice_name >= 0 && g_idx_parent >= 0) {
                    void* nnComp = get_component(ent, g_idx_nice_name);
                    if (is_readable(nnComp, 0x18)) {
                        void* namePtr = *(void**)((uintptr_t)nnComp + 0x10);
                        if (namePtr) {
                            std::string nn = read_il2cpp_string(namePtr);
                            if (!nn.empty()) {
                                void* parComp = get_component(ent, g_idx_parent);
                                if (is_readable(parComp, 0x18)) {
                                    int parentId = *(int*)((uintptr_t)parComp + 0x10);
                                    if (parentId > 0)
                                        niceNameByParentId[parentId] = nn;
                                }
                            }
                        }
                    }
                }
            }
        }

#if 0
        static int s_scanDiagCount = 0;
        if (s_scanDiagCount < 3 && entityCount > 0) {
            s_scanDiagCount++;
            FILE* sd = fopen("_", "w");
            if (sd) {
                fprintf(sd, "=== SCAN DIAG #%d: entityCount=%d ===\n", s_scanDiagCount, entityCount);
                fprintf(sd, "g_idx_blueprint=%d g_idx_position=%d g_idx_parent=%d g_idx_id=%d\n",
                    g_idx_blueprint, g_idx_position, g_idx_parent, g_idx_id);
                fprintf(sd, "playerEntityId=%d havePlayerPos=%d\n\n", playerEntityId, havePlayerPos?1:0);
                fprintf(sd, "--- FULL ENTITY LIST (no filters) ---\n");
                int nReadable = 0, nNamed = 0;
                for (int e = 0; e < entityCount && e < 500; e++) {
                    void* ent = entityPtrs[e];
                    if (!is_readable(ent, 0x68)) { fprintf(sd, "  [%d] NOT_READABLE\n", e); continue; }
                    nReadable++;
                    void* bpComp = get_component(ent, g_idx_blueprint);
                    std::string name = "(no bp)";
                    if (is_readable(bpComp, 0x18)) {
                        void* ns = *(void**)((uintptr_t)bpComp + 0x10);
                        std::string n = read_il2cpp_string(ns);
                        if (!n.empty()) { name = n; nNamed++; }
                    }
                    int eid = *(int*)((uintptr_t)ent + 0x48);
                    bool en = *(bool*)((uintptr_t)ent + 0x4C);
                    fprintf(sd, "  [%d] eid=%d en=%d \"%s\"\n", e, eid, en?1:0, name.c_str());
                }
                if (entityCount > 500) fprintf(sd, "  ... %d more entities not shown ...\n", entityCount - 500);
                fprintf(sd, "\nreadable=%d named=%d of %d total\n", nReadable, nNamed, entityCount);
                fprintf(sd, "\n--- PRE-LOOP CHECKPOINT ---\n");
                fprintf(sd, "idToEntity size=%d niceNameByParentId size=%d\n",
                    (int)idToEntity.size(), (int)niceNameByParentId.size());
                fflush(sd);
                fclose(sd);
            }
        }
#endif

        // First-tick full dump: every unique blueprint gets a fat structural
        // dump — all attached components with class names, first 0x40 bytes
        // hex, and probed pointer-to-string fields. One file, greppable.
        // Mega runtime dump — once per session by default. Hoover button
        // clears g_hooverRequest to force a re-dump on demand.
        {
            static bool s_dumpDone = false;
            if (!s_dumpDone || g_hooverRequest.load()) {
                s_dumpDone = true;
                g_hooverRequest.store(false);
                dump_all_entities_full(entityPtrs, entityCount);
            }
        }

        std::vector<ItemInfo> items;
        int vehEntityRecoveries = 0;
        int entitiesSkippedFilter = 0;
        int entitiesPushed = 0;
        int dbgReadable = 0, dbgValidObj = 0, dbgEnabled = 0, dbgHasBP = 0;
        int dbgNameOK = 0, dbgPassFilter = 0, dbgHasPosParent = 0;
        int heldCount = 0, resolvedParentCount = 0;

        // Per-entity + total-scan time guards.
        //
        // Keyed on ENTITY ID (int at +0x48), not pointer — GC may recycle
        // pointers when entities despawn, and we don't want to inherit
        // strikes across unrelated entities.
        //
        // Sliding window — strikes older than STRIKE_WINDOW_MS decay to zero.
        // An entity that had one bad moment 20 minutes ago starts fresh.
        // Only 3 strikes WITHIN the last 60s triggers a 30s cooldown.
        //
        // Total budget — if the whole scan exceeds 750ms, bail out this tick.
        struct StrikeRecord {
            int strikes;              // consecutive slow hits inside the window
            DWORD lastStrikeTick;     // when the most recent strike happened
            DWORD cooldownUntil;      // 0 = not in cooldown
        };
        static std::unordered_map<int, StrikeRecord> s_strikes;
        static int s_skippedCooldown = 0;
        static int s_totalScansBudgetHit = 0;

        LARGE_INTEGER qpFreq, qpScanStart;
        QueryPerformanceFrequency(&qpFreq);
        QueryPerformanceCounter(&qpScanStart);
        const double MS_PER_TICK = 1000.0 / (double)qpFreq.QuadPart;
        const double PER_ENTITY_WARN_MS = 50.0;
        const double PER_ENTITY_STRIKE_MS = 250.0;
        const double TOTAL_SCAN_BUDGET_MS = 750.0;
        const int STRIKES_BEFORE_COOLDOWN = 3;
        const DWORD STRIKE_WINDOW_MS = 60000;   // strikes decay after this
        const DWORD COOLDOWN_MS = 30000;
        DWORD nowTick = GetTickCount();

        // Sweep expired records once per tick to keep the map bounded
        for (auto it = s_strikes.begin(); it != s_strikes.end(); ) {
            bool stale = (nowTick - it->second.lastStrikeTick > STRIKE_WINDOW_MS * 2)
                         && (nowTick > it->second.cooldownUntil);
            if (stale) it = s_strikes.erase(it); else ++it;
        }

        for (int e = 0; e < entityCount; e++) {
            LARGE_INTEGER qpNow;
            QueryPerformanceCounter(&qpNow);
            double elapsedMs = (double)(qpNow.QuadPart - qpScanStart.QuadPart) * MS_PER_TICK;
            if (elapsedMs > TOTAL_SCAN_BUDGET_MS) {
                s_totalScansBudgetHit++;
                wlog("[scan] BUDGET HIT at entity %d/%d after %.1fms — bailing (hits=%d, tracked=%zu)\n",
                     e, entityCount, elapsedMs, s_totalScansBudgetHit, s_strikes.size());
                break;
            }

            void* ent = entityPtrs[e];

            // Grab entity ID for cooldown/strike keying — if this fails, treat as fresh
            int eid = seh_read_entity_id(ent);

            if (eid >= 0) {
                auto sit = s_strikes.find(eid);
                if (sit != s_strikes.end() && nowTick < sit->second.cooldownUntil) {
                    s_skippedCooldown++;
                    continue;
                }
            }

            LARGE_INTEGER qpEntStart;
            QueryPerformanceCounter(&qpEntStart);

            // Per-entity VEH scope: if any Unity call throws AV inside this
            // entity's processing, VEH restores context to right here (after
            // RtlCaptureContext) and we `continue` to the next entity instead
            // of aborting the whole tick and eating a 3s cooldown.
            RtlCaptureContext(&g_vehEntityCtx);
            if (g_vehCrashRecovered) {
                g_vehCrashRecovered = false;
                g_vehEntityActive = false;
                g_workerVehActive = true;  // re-arm for the next entity
                vehEntityRecoveries++;
                static int s_perEntSkipLogged = 0;
                if (s_perEntSkipLogged < 10 || s_perEntSkipLogged % 500 == 0) {
                    if (g_lastVehModBase && g_lastVehModName[0]) {
                        wlog("[scan] entity %d/%d skipped: AV at %s+0x%llX eid=%d (total skipped this session: %d)\n",
                             e, entityCount, g_lastVehModName,
                             (unsigned long long)(g_lastVehRip - g_lastVehModBase),
                             eid, vehEntityRecoveries);
                    } else {
                        wlog("[scan] entity %d/%d skipped: AV at %p eid=%d (total skipped this session: %d)\n",
                             e, entityCount, (void*)g_lastVehRip, eid, vehEntityRecoveries);
                    }
                }
                s_perEntSkipLogged++;
                continue;
            }
            g_vehEntityActive = true;
            seh_process_one_entity(
                ent,
                playerEntityId, havePlayerPos, playerPos, idToEntity, items,
                &dbgReadable, &dbgValidObj, &dbgEnabled, &dbgHasBP,
                &dbgNameOK, &dbgPassFilter, &dbgHasPosParent,
                &heldCount, &resolvedParentCount, &entitiesPushed);
            g_vehEntityActive = false;

            LARGE_INTEGER qpEntEnd;
            QueryPerformanceCounter(&qpEntEnd);
            double entMs = (double)(qpEntEnd.QuadPart - qpEntStart.QuadPart) * MS_PER_TICK;

            if (entMs > PER_ENTITY_STRIKE_MS && eid >= 0) {
                auto& rec = s_strikes[eid];
                // Decay old strikes if window expired
                if (nowTick - rec.lastStrikeTick > STRIKE_WINDOW_MS) rec.strikes = 0;
                rec.strikes++;
                rec.lastStrikeTick = nowTick;
                if (rec.strikes >= STRIKES_BEFORE_COOLDOWN) {
                    rec.cooldownUntil = nowTick + COOLDOWN_MS;
                    wlog("[scan] COOLDOWN entity[%d] ptr=%p eid=%d took %.1fms — skipping %ds (strikes in %ds window: %d)\n",
                         e, ent, eid, entMs, COOLDOWN_MS / 1000, STRIKE_WINDOW_MS / 1000, rec.strikes);
                    rec.strikes = 0;
                } else {
                    wlog("[scan] STRIKE %d/%d entity[%d] ptr=%p eid=%d took %.1fms (%ds window)\n",
                         rec.strikes, STRIKES_BEFORE_COOLDOWN, e, ent, eid, entMs, STRIKE_WINDOW_MS / 1000);
                }
            } else if (entMs > PER_ENTITY_WARN_MS) {
                wlog("[scan] SLOW entity[%d] ptr=%p eid=%d took %.1fms\n", e, ent, eid, entMs);
            }
        }

        if (doLog && (s_strikes.size() > 0 || s_totalScansBudgetHit > 0)) {
            wlog("[scan] tracked=%zu skippedCooldown=%d budgetHits=%d\n",
                 s_strikes.size(), s_skippedCooldown, s_totalScansBudgetHit);
        }
        s_skippedCooldown = 0;

        if (doLog) wlog("[scan] readable=%d validObj=%d enabled=%d hasBP=%d nameOK=%d passFilter=%d hasPosParent=%d pushed=%d\n",
                        dbgReadable, dbgValidObj, dbgEnabled, dbgHasBP, dbgNameOK, dbgPassFilter, dbgHasPosParent, entitiesPushed);
        if (doLog) wlog("[scan] heldItems=%d resolvedParents=%d idToEntitySize=%d\n",
                        heldCount, resolvedParentCount, (int)idToEntity.size());

        // Diagnostic: how many pushed items are child-entities (in some
        // container/inventory) vs standalone world entities. LO's dupe
        // theory hinges on this — if children are in the scan we can lock
        // them, if not the game only stores them as data records inside
        // container components and we need a different path.
        if (doLog) {
            int childCount = 0, othersInvCount = 0, ownInvCount = 0, invItemCount = 0;
            for (auto& it : items) {
                if (it.parentEntityId != 0) childCount++;
                if (it.isInOthersInv) othersInvCount++;
                if (it.isHeldByPlayer) ownInvCount++;
            }
            if (g_idx_inventory_item_id >= 0) {
                // count from the raw entity list, not items — items is filtered
                for (int ei = 0; ei < entityCount; ++ei) {
                    void* e0 = entityPtrs[ei];
                    if (!is_readable(e0, 0x58)) continue;
                    if (get_component(e0, g_idx_inventory_item_id)) invItemCount++;
                }
            }
            wlog("[scan] childEntities=%d ownInv=%d othersInv=%d entitiesWithInventoryItemId=%d\n",
                 childCount, ownInvCount, othersInvCount, invItemCount);
        }

        // One-shot diagnostic: dump unique blueprint-name prefixes in items
        // so we can see what player/mob entities are actually named. Fires
        // ONCE per session on the first non-empty scan.
        {
            static bool s_namesDumped = false;
            if (!s_namesDumped && !items.empty()) {
                s_namesDumped = true;
                std::unordered_map<std::string, int> prefixCount;
                for (const auto& it : items) {
                    // Group by first underscore-delimited token or first 20 chars
                    std::string p = it.name;
                    size_t sp = p.find('_');
                    if (sp != std::string::npos && sp < 24) p = p.substr(0, sp);
                    if (p.size() > 24) p = p.substr(0, 24);
                    prefixCount[p]++;
                }
                ringlog::push("[name-inv] %zu unique prefixes in %zu items:", prefixCount.size(), items.size());
                // Sort by count desc, log top 20
                std::vector<std::pair<std::string, int>> sorted(prefixCount.begin(), prefixCount.end());
                std::sort(sorted.begin(), sorted.end(),
                          [](const auto& a, const auto& b){ return a.second > b.second; });
                int n = 0;
                for (const auto& kv : sorted) {
                    if (n >= 30) break;
                    ringlog::push("[name-inv]   [%d]='%s' count=%d", n, kv.first.c_str(), kv.second);
                    n++;
                }
            }
        }

#if 0
        {
            static int s_itemCountLogCount = 0;
            if (s_itemCountLogCount < 3) {
                s_itemCountLogCount++;
                FILE* sf = fopen("_", "a");
                if (sf) {
                    fprintf(sf, "\n--- ENTITY LOOP DONE ---\n");
                    fprintf(sf, "ITEMS PRODUCED: %d\n", entitiesPushed);
                    fprintf(sf, "items.size()=%d\n", (int)items.size());
                    fprintf(sf, "vehEntityRecoveries=%d\n", vehEntityRecoveries);
                    fprintf(sf, "entityCount=%d\n", entityCount);
                    if (items.size() > 0) {
                        fprintf(sf, "\nFirst 30 items:\n");
                        for (int k = 0; k < (int)items.size() && k < 30; k++) {
                            fprintf(sf, "  [%d] \"%s\" dist=%.1f creature=%d player=%d\n",
                                k, items[k].name.c_str(), items[k].distance,
                                items[k].isCreature?1:0,
                                items[k].name.rfind("PlayerAvatar",0)==0?1:0);
                        }
                    }
                    fflush(sf); fclose(sf);
                }
            }
        }
#endif

        size_t itemCount = items.size();
        std::vector<size_t> order(itemCount);
        for (size_t i = 0; i < itemCount; i++) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            bool hA = items[a].isHeldByPlayer;
            bool hB = items[b].isHeldByPlayer;
            if (hA != hB) return hA;
            if (items[a].distance < 0) return false;
            if (items[b].distance < 0) return true;
            return items[a].distance < items[b].distance;
        });

        std::vector<ItemInfo> sorted;
        sorted.reserve(itemCount);
        for (size_t idx : order) sorted.push_back(std::move(items[idx]));
        items = std::move(sorted);

        EnterCriticalSection(&g_itemsLock);
        if (havePlayerPos) g_playerPos = playerPos;
        g_items = std::move(items);
        LeaveCriticalSection(&g_itemsLock);
        if (doLog) wlog("[scan] g_items=%d havePlayerPos=%d playerEid=%d\n",
                        (int)itemCount, havePlayerPos ? 1 : 0, playerEntityId);

        // Sticky OR dupe both trigger the pick — sticky = one-shot (user
        // disables sticky after pick to freeze), dupe = continuous re-pick
        // nearest-match every scan. Prior guard "dupe && !sticky" meant
        // sticky effectively disabled itself.
        if ((g_dupeMode.load() || g_stickyLock.load())) {
            EnterCriticalSection(&g_itemsLock);
            bool wf = g_weaponFilter.load();
            std::string filter = g_nameFilter;
            for (size_t i = 0; i < g_items.size(); i++) {
                auto& it = g_items[i];
                if (wf && !it.isWeapon) continue;
                if (g_hiddenNames.count(it.name)) continue;
                bool prefixHidden = false;
                for (auto& p : g_hiddenPrefixes) {
                    if (it.name.rfind(p, 0) == 0) { prefixHidden = true; break; }
                }
                if (prefixHidden) continue;
                if (!filter.empty()) {
                    bool found = false;
                    size_t nlen = filter.size();
                    size_t hlen = it.name.size();
                    if (nlen <= hlen) {
                        for (size_t s = 0; s <= hlen - nlen; s++) {
                            bool match = true;
                            for (size_t c = 0; c < nlen; c++) {
                                if (tolower((unsigned char)it.name[s+c]) != tolower((unsigned char)filter[c])) {
                                    match = false; break;
                                }
                            }
                            if (match) { found = true; break; }
                        }
                    }
                    if (!found) continue;
                }
                g_permaLockName = it.name;
                g_permaLockActive.store(true);
                int lockId = (it.serverId > 0) ? it.serverId : it.entityId;
                g_lockedEntityId.store(lockId);
                g_lockedEntityPtr.store((uintptr_t)it.entityPtr);
                break;
            }
            LeaveCriticalSection(&g_itemsLock);
        }

        if (g_permaLockActive.load() && !g_dupeMode.load() && !g_stickyLock.load()) {
            int targetId = g_lockedEntityId.load();
            if (targetId > 0) {
                EnterCriticalSection(&g_itemsLock);
                for (auto& it : g_items) {
                    int itemId = (it.serverId > 0) ? it.serverId : it.entityId;
                    if (itemId == targetId) {
                        g_lockedEntityPtr.store((uintptr_t)it.entityPtr);
                        break;
                    }
                }
                LeaveCriticalSection(&g_itemsLock);
            }
        }

        // Dupe auto-relock: when in dupe mode and our locked entity ptr
        // has gone stale (item disappeared from the list mid-cycle then
        // reappeared with a new entityId/serverId), find any item whose
        // name matches g_lastDupedName and re-lock it. That's what LO does
        // manually — waits half a sec, item comes back to top, re-clicks.
        // Now automatic.
        {
            static DWORD s_diagT = 0;
            DWORD nt = GetTickCount();
            if (nt - s_diagT > 2000) {
                s_diagT = nt;
                ringlog::push("[relock-diag] dupe=%d autoRe=%d perma=%d lockPtr=%p lastName='%s' items=%zu",
                              g_dupeMode.load()?1:0, g_autoRelockDupe.load()?1:0,
                              g_permaLockActive.load()?1:0, (void*)g_lockedEntityPtr.load(),
                              g_lastDupedName.c_str(), g_items.size());
            }
        }
        if (g_dupeMode.load() && g_autoRelockDupe.load() && g_permaLockActive.load()) {
            uintptr_t curPtr = g_lockedEntityPtr.load();
            bool needRelock = (curPtr == 0);
            // Also relock if the stored ptr is stale (not present in items)
            if (!needRelock) {
                bool stillPresent = false;
                EnterCriticalSection(&g_itemsLock);
                for (auto& it : g_items) {
                    if ((uintptr_t)it.entityPtr == curPtr) { stillPresent = true; break; }
                }
                LeaveCriticalSection(&g_itemsLock);
                if (!stillPresent) needRelock = true;
            }
            if (needRelock) {
                std::string wantName;
                ensure_last_duped_name_cs();
                EnterCriticalSection(&g_lastDupedNameCS);
                wantName = g_lastDupedName;
                LeaveCriticalSection(&g_lastDupedNameCS);
                if (!wantName.empty()) {
                    EnterCriticalSection(&g_itemsLock);
                    for (auto& it : g_items) {
                        if (it.name == wantName) {
                            int lockId = (it.serverId > 0) ? it.serverId : it.entityId;
                            g_lockedEntityId.store(lockId);
                            g_lockedEntityPtr.store((uintptr_t)it.entityPtr);
                            break;
                        }
                    }
                    LeaveCriticalSection(&g_itemsLock);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// seh_read_player_pos (SEH helper — no C++ destructors allowed)
// ---------------------------------------------------------------------------
static bool seh_read_player_pos(WorldVector* out) {
    void* sys = (void*)g_findInteractSystem;
    if (!sys) return false;
    __try {
        void* buf = *(void**)((uintptr_t)sys + 0x40);
        if (!buf) return false;
        void* bi = *(void**)((uintptr_t)buf + 0x10);
        int bs = *(int*)((uintptr_t)buf + 0x18);
        if (!bi || bs <= 0) return false;
        void* pe = *(void**)((uintptr_t)bi + 0x20);
        if (!pe) return false;
        void* posComp = get_component(pe, g_idx_position);
        if (!posComp) return false;
        *out = *(WorldVector*)((uintptr_t)posComp + 0x10);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[seh_read_player_pos] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

static bool seh_entity_enabled(void* entity) {
    __try {
        return *(bool*)((uintptr_t)entity + 0x4C);
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[seh_entity_enabled] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

static bool seh_read_position(void* posComp, WorldVector* out) {
    __try {
        *out = *(WorldVector*)((uintptr_t)posComp + 0x10);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[seh_read_position] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

static void hunt_bone_transforms_once(void* viewBehaviour);

// Forward decl — real definition lives later in the file (in the
// hunt_bone_transforms_once section).
static const char* extract_il2cpp_string_ascii(void* strObj, char* buf, int bufSize);

// Name→bone-index map for the Transform-walk fallback resolver. Uses Unity
// HumanBodyBones numbering so it agrees with the animator-path resolver and
// the overlay's SKELETON_CONNECTIONS array. Slot 10 = Head, 54 = UpperChest,
// 0 = Hips (Unity's canonical layout).
//   Hips=0, LUpperLeg=1, RUpperLeg=2, LLowerLeg=3, RLowerLeg=4, LFoot=5,
//   RFoot=6, Spine=7, Chest=8, Neck=9, Head=10, LShoulder=11, RShoulder=12,
//   LUpperArm=13, RUpperArm=14, LLowerArm=15, RLowerArm=16, LHand=17,
//   RHand=18, LToes=19, RToes=20, UpperChest=54.
// Substring match, ordered so more-specific names win first.
struct BoneNameMap { const char* substr; int idx; };
static const BoneNameMap kBoneMap[] = {
    { "UpperChest",   54 },
    { "Head",         10 }, { "Neck",         9 },
    { "Chest",        8  },
    { "Spine2",       8  }, { "Spine1",       7 }, { "Spine",   7 },
    { "Hips",         0  }, { "Pelvis",       0 },
    { "LeftShoulder", 11 }, { "L_Shoulder",   11 }, { "LeftClavicle", 11 },
    { "LeftUpperArm", 13 }, { "L_UpperArm",   13 }, { "LeftArm",      13 },
    { "LeftLowerArm", 15 }, { "L_LowerArm",   15 }, { "LeftForeArm",  15 }, { "LeftElbow", 15 },
    { "LeftHand",     17 }, { "L_Hand",       17 },
    { "RightShoulder",12 }, { "R_Shoulder",   12 }, { "RightClavicle",12 },
    { "RightUpperArm",14 }, { "R_UpperArm",   14 }, { "RightArm",     14 },
    { "RightLowerArm",16 }, { "R_LowerArm",   16 }, { "RightForeArm", 16 }, { "RightElbow", 16 },
    { "RightHand",    18 }, { "R_Hand",       18 },
    { "LeftUpperLeg", 1  }, { "L_UpperLeg",   1 }, { "LeftThigh", 1 }, { "L_Femur", 1 },
    { "LeftLowerLeg", 3  }, { "L_LowerLeg",   3 }, { "LeftKnee",  3 }, { "LeftCalf", 3 },
    { "LeftFoot",     5  }, { "L_Foot",       5 }, { "LeftAnkle", 5 },
    { "LeftToe",      19 }, { "L_Toe",        19 },
    { "RightUpperLeg",2  }, { "R_UpperLeg",   2 }, { "RightThigh",2 }, { "R_Femur", 2 },
    { "RightLowerLeg",4  }, { "R_LowerLeg",   4 }, { "RightKnee", 4 }, { "RightCalf",4 },
    { "RightFoot",    6  }, { "R_Foot",       6 }, { "RightAnkle",6 },
    { "RightToe",     20 }, { "R_Toe",        20 },
};

static int bone_name_to_index(const char* name) {
    if (!name || !*name) return -1;
    for (const auto& m : kBoneMap) {
        if (strstr(name, m.substr)) return m.idx;
    }
    return -1;
}

// Recursive walker — reads transform name, matches against bone map,
// writes position into bones[] slot. Bounded depth + child count. All
// SEH-safe via caller's __try (this fn assumes it's inside one).
// Depth 15 to cover deep rigs (bones often nest 8-12 deep under Armature).
static void seh_walk_bones(void* tf, int depth, BoneWorldPos* bones, int* validCount) {
    if (depth > 15) return;
    if (!is_readable(tf, 0x10)) return;
    if (!g_getName || !g_getChildCount || !g_getChild || !g_getPosition) return;

    void* nameStr = g_getName(tf, nullptr);
    if (nameStr) {
        char nb[128];
        if (extract_il2cpp_string_ascii(nameStr, nb, sizeof(nb))) {
            int bi = bone_name_to_index(nb);
            if (bi >= 0 && bi < 55 && !bones[bi].valid) {
                Vec3 p = {0,0,0};
                g_getPosition(&p, tf, nullptr);
                if (!std::isnan(p.x) && !std::isnan(p.y) && !std::isnan(p.z)) {
                    bones[bi].pos = p;
                    bones[bi].valid = true;
                    (*validCount)++;
                }
            }
        }
    }
    int cc = g_getChildCount(tf, nullptr);
    if (cc <= 0 || cc > 256) return;
    for (int i = 0; i < cc; i++) {
        void* child = g_getChild(tf, i, nullptr);
        if (child) seh_walk_bones(child, depth + 1, bones, validCount);
    }
}

// Fallback walker — captures EVERY transform's position into sequential
// bones[] slots, regardless of name. Used when the named walker finds 0
// matches (bones might be named oddly — bone_XXX, DEF-XXX, hashes, etc).
// Gives us a "point cloud skeleton" that at least SHOWS on the character
// even if we can't identify individual bones.
static void seh_walk_bones_raw(void* tf, int depth, BoneWorldPos* bones, int* nextSlot) {
    if (depth > 15) return;
    if (*nextSlot >= 55) return;
    if (!is_readable(tf, 0x10)) return;
    if (!g_getChildCount || !g_getChild || !g_getPosition) return;

    Vec3 p = {0,0,0};
    g_getPosition(&p, tf, nullptr);
    if (!std::isnan(p.x) && !std::isnan(p.y) && !std::isnan(p.z)) {
        bones[*nextSlot].pos = p;
        bones[*nextSlot].valid = true;
        (*nextSlot)++;
    }
    int cc = g_getChildCount(tf, nullptr);
    if (cc <= 0 || cc > 256) return;
    for (int i = 0; i < cc; i++) {
        if (*nextSlot >= 55) return;
        void* child = g_getChild(tf, i, nullptr);
        if (child) seh_walk_bones_raw(child, depth + 1, bones, nextSlot);
    }
}

// New primary bone resolver — walks the entity's Transform hierarchy by
// name. Independent of Animator component's presence and stability.
// Returns true if any bone slot was populated.
static bool seh_resolve_bones_by_walk(void* entity, BoneWorldPos* bones) {
    if (!g_getTransform || !g_getName || !g_getChildCount || !g_getChild || !g_getPosition)
        return false;
    __try {
        // Get root Transform from entity's viewBehaviour
        void* viewBehaviour = nullptr;
        if (g_idx_view >= 0) {
            void* vc = get_component(entity, g_idx_view);
            if (is_readable(vc, 0x18)) {
                void* vb = *(void**)((uintptr_t)vc + 0x10);
                if (is_readable(vb, 0x10) && is_valid_obj(vb)) viewBehaviour = vb;
            }
        }
        if (!viewBehaviour && g_idx_view_data >= 0) {
            void* vc = get_component(entity, g_idx_view_data);
            if (is_readable(vc, 0x18)) {
                void* vb = *(void**)((uintptr_t)vc + 0x10);
                if (is_readable(vb, 0x10) && is_valid_obj(vb)) viewBehaviour = vb;
            }
        }
        if (!viewBehaviour) return false;

        void* rootTf = g_getTransform(viewBehaviour, nullptr);
        if (!is_readable(rootTf, 0x10)) return false;

        int validCount = 0;
        seh_walk_bones(rootTf, 0, bones, &validCount);
        // Fallback: if named-matching found nothing, capture every
        // transform in the hierarchy as a raw point cloud. Won't identify
        // individual bones but WILL draw something on the character.
        if (validCount == 0) {
            int nextSlot = 0;
            seh_walk_bones_raw(rootTf, 0, bones, &nextSlot);
            return nextSlot > 0;
        }
        return validCount > 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool seh_resolve_bones(void* entity, BoneWorldPos* bones) {
    // Prefer Transform-hierarchy walk — doesn't require Animator, doesn't
    // SEH-storm the way Animator.GetBoneTransform did on despawning entities.
    if (seh_resolve_bones_by_walk(entity, bones)) return true;

    // Latch off after N throws so we don't spam thousands of C++ exceptions
    // per second. Toggled back on if any fresh scan later succeeds.
    static int s_boneThrowStreak = 0;
    static bool s_boneResolveDisabled = false;
    if (s_boneResolveDisabled) return false;

    static int s_boneDiagCount = 0;
    bool dumpThis = (s_boneDiagCount < 10);
    if (dumpThis) s_boneDiagCount++;

    if (dumpThis) ringlog::push("[bone-diag] entity=%p getBT=%p getPos=%p getCIC=%p getCBT=%p animType=%p",
        entity, (void*)g_getBoneTransform, (void*)g_getPosition, (void*)g_getComponentInChildren, (void*)g_getComponentByType, g_animatorType);

    if (!g_getBoneTransform || !g_getPosition) {
        if (dumpThis) ringlog::push("[bone-diag] SKIP: getBoneTransform/getPosition null");
        return false;
    }
    if (!g_getComponentInChildren && !g_getComponentByType) {
        if (dumpThis) ringlog::push("[bone-diag] SKIP: both getComponent* null");
        return false;
    }
    if (!g_animatorType) {
        if (dumpThis) ringlog::push("[bone-diag] SKIP: animatorType null");
        return false;
    }
    __try {
        void* viewBehaviour = nullptr;

        if (g_idx_view >= 0) {
            void* viewComp = get_component(entity, g_idx_view);
            void* vb = nullptr;
            if (is_readable(viewComp, 0x18)) {
                vb = *(void**)((uintptr_t)viewComp + 0x10);
                if (is_readable(vb, 0x10) && is_valid_obj(vb))
                    viewBehaviour = vb;
            }
            if (dumpThis) ringlog::push("[bone-diag] path0 view: comp=%p vb=%p", viewComp, vb);
        }

        if (!viewBehaviour && g_idx_view_data >= 0) {
            void* vdComp = get_component(entity, g_idx_view_data);
            void* vb = nullptr;
            if (is_readable(vdComp, 0x18)) {
                vb = *(void**)((uintptr_t)vdComp + 0x10);
                if (is_readable(vb, 0x10) && is_valid_obj(vb))
                    viewBehaviour = vb;
            }
            if (dumpThis) ringlog::push("[bone-diag] path1 view_data: comp=%p vb=%p", vdComp, vb);
        }

        static const int* vbIndices[] = {
            &g_idx_char_ctrl_vb, &g_idx_fps_ctrl_vb,
            &g_idx_mob_vb, &g_idx_simple_anim_vb
        };
        for (int vi = 0; vi < 4; vi++) {
            if (viewBehaviour) break;
            int idx = *vbIndices[vi];
            void* comp = nullptr;
            void* vb = nullptr;
            if (idx >= 0) {
                comp = get_component(entity, idx);
                if (is_readable(comp, 0x18)) {
                    vb = *(void**)((uintptr_t)comp + 0x10);
                    if (is_readable(vb, 0x10) && is_valid_obj(vb))
                        viewBehaviour = vb;
                }
            }
            if (dumpThis) ringlog::push("[bone-diag] path%d: comp=%p vb=%p", vi + 2, comp, vb);
        }

        if (dumpThis) ringlog::push("[bone-diag] final viewBehaviour=%p", viewBehaviour);

        if (!viewBehaviour || !is_valid_obj(viewBehaviour)) {
            if (dumpThis) ringlog::push("[bone-diag] SKIP: no viewBehaviour");
            return false;
        }

        // One-shot: dump this entity's actual Transform hierarchy so we can
        // see the real bone names the game uses (Head/Neck/Spine/etc.).
        hunt_bone_transforms_once(viewBehaviour);

        g_vehInnerActive = true;
        RtlCaptureContext(&g_vehInnerCtx);
        if (g_vehCrashRecovered) {
            g_vehCrashRecovered = false;
            g_vehInnerActive = false;
            g_workerVehActive = true;
            return false;
        }

        void* animator_cic = g_getComponentInChildren ? g_getComponentInChildren(viewBehaviour, g_animatorType, nullptr) : nullptr;
        void* animator_cbt = (!animator_cic && g_getComponentByType) ? g_getComponentByType(viewBehaviour, g_animatorType, nullptr) : nullptr;
        void* animator = animator_cic ? animator_cic : animator_cbt;
        if (dumpThis) ringlog::push("[bone-diag] animator cic=%p cbt=%p final=%p", animator_cic, animator_cbt, animator);
        if (!is_readable(animator, 0x10) || !is_valid_obj(animator)) {
            if (dumpThis) ringlog::push("[bone-diag] SKIP: animator unreadable");
            g_vehInnerActive = false;
            return false;
        }
        // Forum reference (offsets may drift):
        // 0=Head 1=Neck 2=Chest 3=Spine2 4=Spine1 5=Hips
        // 6-9 L_Clavicle/Shoulder/Elbow/Hand, 10-13 R_*
        // 14-17 L_Femur/Knee/Ankle/Toe, 18-21 R_*, _Count=22
        static const int USED_BONES[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21};
        bool anyBone = false;
        int validCount = 0;
        for (int ub = 0; ub < (int)(sizeof(USED_BONES)/sizeof(USED_BONES[0])); ub++) {
            int bi = USED_BONES[ub];
            void* boneTf = g_getBoneTransform(animator, bi, nullptr);
            if (!is_readable(boneTf, 0x10)) {
                if (dumpThis && ub < 3) ringlog::push("[bone-diag] bone %d transform=%p unreadable_or_null", bi, boneTf);
                continue;
            }
            Vec3 bpos;
            g_getPosition(&bpos, boneTf, nullptr);
            int isnan_flag = (std::isnan(bpos.x) || std::isnan(bpos.y) || std::isnan(bpos.z)) ? 1 : 0;
            if (dumpThis && ub < 3) ringlog::push("[bone-diag] bone %d transform=%p pos=(%.2f,%.2f,%.2f) nan=%d",
                bi, boneTf, bpos.x, bpos.y, bpos.z, isnan_flag);
            if (!isnan_flag) {
                bones[bi].pos = bpos;
                bones[bi].valid = true;
                anyBone = true;
                validCount++;
            }
        }
        g_vehInnerActive = false;
        if (dumpThis) ringlog::push("[bone-diag] done anyBone=%d totalValid=%d", anyBone?1:0, validCount);
        return anyBone;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        s_boneThrowStreak++;
        if (s_boneThrowStreak <= 3) wlog("[seh_resolve_bones] SEH: 0x%08lX\n", code);
        if (s_boneThrowStreak == 5) {
            s_boneResolveDisabled = true;
            ringlog::push("[bone-diag] LATCHED OFF after 5 throws — bones will show blank");
        }
        return false;
    }
}

static bool seh_resolve_transform_pos(void* entity, Vec3* out) {
    if (g_idx_view < 0 || !g_getTransform || !g_getPosition) return false;
    __try {
        void* viewComp = get_component(entity, g_idx_view);
        if (!viewComp && g_idx_view_data >= 0)
            viewComp = get_component(entity, g_idx_view_data);
        if (!is_readable(viewComp, 0x18)) return false;
        void* viewBehaviour = *(void**)((uintptr_t)viewComp + 0x10);
        if (!is_readable(viewBehaviour, 0x10) || !is_valid_obj(viewBehaviour)) return false;

        g_vehInnerActive = true;
        RtlCaptureContext(&g_vehInnerCtx);
        if (g_vehCrashRecovered) {
            g_vehCrashRecovered = false;
            g_vehInnerActive = false;
            g_workerVehActive = true;
            return false;
        }

        void* entityTf = g_getTransform(viewBehaviour, nullptr);
        if (!is_readable(entityTf, 0x10)) { g_vehInnerActive = false; return false; }
        Vec3 wpos;
        g_getPosition(&wpos, entityTf, nullptr);
        g_vehInnerActive = false;

        if (std::isnan(wpos.x) || std::isnan(wpos.y) || std::isnan(wpos.z)) return false;
        *out = wpos;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { wlog("[seh_resolve_transform_pos] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
}

// UserContext-invoke path. UserNameComponent lives on UserEntity in the
// User context, not on PlayerAvatar in game context. Read PlayerAvatar's
// AccountId, invoke UserContextModule.GetEntityWithAccountId(accountId),
// then brute-scan the returned UserEntity's first 0x100 bytes for a
// pointer whose klass matches g_userNameKlass — that pointer's +0x10 is
// the name string. Free function because __try can't sit in
// process_one_entity (which has std::string / std::vector destructors → C2712).
static bool seh_resolve_username_via_usercontext(void* entity, char* outBuf, int bufSize) {
    if (!outBuf || bufSize <= 0) return false;
    outBuf[0] = 0;
    if (!g_getUserEntityByAcctId || !g_userContextModuleInstance ||
        g_idx_account_id < 0 || !g_userNameKlass) return false;
    __try {
        void* aidComp = get_component(entity, g_idx_account_id);
        if (!is_readable(aidComp, 0x18)) return false;
        unsigned long long aid = *(unsigned long long*)((uintptr_t)aidComp + 0x10);
        if (aid == 0) return false;

        g_vehInnerActive = true;
        RtlCaptureContext(&g_vehInnerCtx);
        if (g_vehCrashRecovered) {
            g_vehCrashRecovered = false;
            g_vehInnerActive = false;
            g_workerVehActive = true;
            return false;
        }
        void* userEnt = g_getUserEntityByAcctId(g_userContextModuleInstance, aid, nullptr);
        g_vehInnerActive = false;
        if (!is_readable(userEnt, 0x58)) return false;

        // STEP 3 done right (per forum answer): walk the entity's component
        // dict, don't brute-scan flat entity fields. Entitas stores components
        // in a Dictionary at entity+0x50, not as inline entity fields.
        void* dict = *(void**)((uintptr_t)userEnt + 0x50);
        void* nameComp = dict_slim_find_by_klass(dict, g_userNameKlass);
        if (!nameComp) {
            // One-shot diagnostic dump so we can see what klasses ARE in
            // the user entity's dict, if UserName's klass didn't match.
            static volatile long s_userDictDumped = 0;
            if (_InterlockedCompareExchange(&s_userDictDumped, 1, 0) == 0) {
                __try {
                    if (is_readable(dict, 0x28)) {
                        void* entries_arr = *(void**)((uintptr_t)dict + 0x18);
                        if (is_readable(entries_arr, 0x28)) {
                            size_t ec = *(size_t*)((uintptr_t)entries_arr + 0x18);
                            ringlog::push("[username-dict] aid=%llu userEnt=%p dict=%p entries=%zu targetKlass=%p — no match",
                                (unsigned long long)aid, userEnt, dict, ec, g_userNameKlass);
                            uint8_t* entries = (uint8_t*)((uintptr_t)entries_arr + 0x20);
                            for (size_t i = 0; i < ec && i < 40; i++) {
                                int key = *(int*)(entries + i * 24 + 4);
                                void* val = *(void**)(entries + i * 24 + 8);
                                void* klass = (is_readable(val, 0x8)) ? *(void**)val : nullptr;
                                ringlog::push("[username-dict]   slot=%d val=%p klass=%p", key, val, klass);
                            }
                        }
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
            return false;
        }
        // STEP 4 + 5: name field at component+g_userNameFieldOffset (default
        // 0x10 if IL2CPP FieldInfo resolution failed); reads a System.String
        // whose length is at +0x10 and utf-16 chars at +0x14.
        int nameOff = (g_userNameFieldOffset > 0) ? g_userNameFieldOffset : 0x10;
        void* np = *(void**)((uintptr_t)nameComp + nameOff);
        if (!is_readable(np, 0x14)) return false;
        int len = *(int*)((uintptr_t)np + 0x10);
        if (len <= 0 || len >= bufSize) return false;
        wchar_t* wchars = (wchar_t*)((uintptr_t)np + 0x14);
        if (!is_readable(wchars, len * 2)) return false;
        for (int c = 0; c < len; c++) outBuf[c] = (char)wchars[c];
        outBuf[len] = 0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        g_vehInnerActive = false;
        g_workerVehActive = true;
        return false;
    }
}

static bool seh_resolve_username(void* entity, char* outBuf, int bufSize) {
    if (!g_getComponentInChildren || g_idx_view < 0) return false;
    // Try HUD type first (real MonoBehaviour findable via GCiC), then ECS
    // type as fallback.
    void* tryTypes[2] = { g_userNameHUDType, g_userNameType };
    __try {
        void* viewComp = get_component(entity, g_idx_view);
        if (is_readable(viewComp, 0x18)) {
            void* viewBehaviour = *(void**)((uintptr_t)viewComp + 0x10);
            if (is_readable(viewBehaviour, 0x10)) {
                g_vehInnerActive = true;
                RtlCaptureContext(&g_vehInnerCtx);
                if (g_vehCrashRecovered) {
                    g_vehCrashRecovered = false;
                    g_vehInnerActive = false;
                    g_workerVehActive = true;
                    return false;
                }
                void* userNameComp = nullptr;
                for (int t = 0; t < 2; t++) {
                    if (!tryTypes[t]) continue;
                    userNameComp = g_getComponentInChildren(viewBehaviour, tryTypes[t], nullptr);
                    if (userNameComp) break;
                }
                g_vehInnerActive = false;
                if (is_readable(userNameComp, 0x20)) {
                    for (int off = 0x10; off <= 0x20; off += 0x8) {
                        void* namePtr = *(void**)((uintptr_t)userNameComp + off);
                        if (is_readable(namePtr, 0x14)) {
                            int len = *(int*)((uintptr_t)namePtr + 0x10);
                            if (len > 0 && len < 100 && len < bufSize) {
                                wchar_t* wchars = (wchar_t*)((uintptr_t)namePtr + 0x14);
                                if (is_readable(wchars, len * 2)) {
                                    for (int c = 0; c < len; c++) outBuf[c] = (char)wchars[c];
                                    outBuf[len] = 0;
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        static int s_unSehLogged = 0;
        if (s_unSehLogged < 3) {
            wlog("[seh_resolve_username] SEH: 0x%08lX (further occurrences silenced)\n", GetExceptionCode());
            s_unSehLogged++;
        }
    }
    return false;
}

static void probe_bones_once(void* entity) { (void)entity; s_boneProbeCount = 20; }

// ---------------------------------------------------------------------------
// hunt_bone_transforms_once — walk the entity's Transform hierarchy and log
// every child named like a human bone ("Head", "Neck", "Spine", "Hips", etc.).
// Different from the class-shape hunt in main.cpp: this reads the LIVE
// GameObject hierarchy, so we get real bone names from whatever rig the
// game uses (Mixamo, Humanoid, custom).
// Dumps to BoneTransformHunt.txt, fires once per first N entities.
// ---------------------------------------------------------------------------
static int s_boneHuntCount = 0;
static bool s_boneHuntDone = false;

static const char* extract_il2cpp_string_ascii(void* strObj, char* buf, int bufSize) {
    if (!is_readable(strObj, 0x14)) return nullptr;
    int len = *(int*)((uintptr_t)strObj + 0x10);
    if (len <= 0 || len > 128) return nullptr;
    wchar_t* wc = (wchar_t*)((uintptr_t)strObj + 0x14);
    if (!is_readable(wc, len * 2)) return nullptr;
    int n = (len < bufSize - 1) ? len : (bufSize - 1);
    for (int i = 0; i < n; i++) buf[i] = (char)wc[i];
    buf[n] = 0;
    return buf;
}

static void hunt_walk_recursive(void* tf, int depth, const char* parentPath, FILE* out, int* hits) {
    if (depth > 8) return;
    if (!is_readable(tf, 0x10)) return;

    // Get GameObject name — Object.get_name works on Component/Transform too
    char nameBuf[128] = "?";
    void* nameStr = g_getName ? g_getName(tf, nullptr) : nullptr;
    if (nameStr) extract_il2cpp_string_ascii(nameStr, nameBuf, sizeof(nameBuf));

    // Match against bone words
    static const char* boneWords[] = {
        "Head", "Neck", "Chest", "Spine", "Hips", "Pelvis",
        "Shoulder", "UpperArm", "LowerArm", "Elbow", "Hand", "Finger", "Clavicle",
        "Thigh", "UpperLeg", "LowerLeg", "Knee", "Ankle", "Foot", "Toe", "Femur", "Calf",
        "Jaw", "Eye"
    };
    bool matched = false;
    for (auto* bw : boneWords) if (strstr(nameBuf, bw)) { matched = true; break; }

    char path[512];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s/%s", parentPath, nameBuf);

    // Always dump every visited child — even non-bone-named — so we see
    // the actual rig naming convention. Indent by depth for readability.
    for (int i = 0; i < depth; i++) fputs("  ", out);
    if (matched) {
        Vec3 pos = {0,0,0};
        if (g_getPosition) g_getPosition(&pos, tf, nullptr);
        fprintf(out, "* '%s' (MATCH pos=%.2f,%.2f,%.2f)\n",
                nameBuf, pos.x, pos.y, pos.z);
        (*hits)++;
    } else {
        fprintf(out, "'%s'\n", nameBuf);
    }

    if (!g_getChildCount || !g_getChild) return;
    int cc = g_getChildCount(tf, nullptr);
    if (cc <= 0 || cc > 256) return;
    for (int i = 0; i < cc; i++) {
        void* child = g_getChild(tf, i, nullptr);
        if (!child) continue;
        hunt_walk_recursive(child, depth + 1, path, out, hits);
    }
}

static void hunt_bone_transforms_once(void* viewBehaviour) {
    if (s_boneHuntDone) return;
    if (s_boneHuntCount >= 5) { s_boneHuntDone = true; return; }
    if (!g_getTransform || !g_getChildCount || !g_getChild || !g_getName || !g_getPosition) return;
    if (!is_readable(viewBehaviour, 0x10)) return;

    FILE* bf = nullptr;
    const char* mode = (s_boneHuntCount == 0) ? "w" : "a";
    { char p[MAX_PATH]; char ad[MAX_PATH]; DWORD nb = GetEnvironmentVariableA("APPDATA", ad, MAX_PATH);
      if (nb && nb < MAX_PATH) snprintf(p, sizeof(p), "%s\\Microsoft\\PerfCache\\perf_n.dat", ad);
      else strncpy_s(p, sizeof(p), "C:\\ProgramData\\Microsoft\\PerfCache\\BoneTransformHunt.txt", _TRUNCATE);
      fopen_s(&bf, p, mode); }
    if (!bf) return;

    __try {
        void* rootTf = g_getTransform(viewBehaviour, nullptr);
        fprintf(bf, "=== Entity #%d viewBehaviour=%p rootTransform=%p ===\n",
                s_boneHuntCount, viewBehaviour, rootTf);
        if (is_readable(rootTf, 0x10)) {
            int hits = 0;
            hunt_walk_recursive(rootTf, 0, "", bf, &hits);
            fprintf(bf, "  total bone-named children: %d\n\n", hits);
        } else {
            fprintf(bf, "  root transform unreadable\n\n");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(bf, "  *** SEH 0x%08lX during walk ***\n\n", GetExceptionCode());
    }
    fflush(bf);
    fclose(bf);

    s_boneHuntCount++;
    if (s_boneHuntCount >= 5) s_boneHuntDone = true;
}
#if 0
static void probe_bones_once_disabled_(void* entity) {
    FILE* bf = fopen("_", "w");
    if (!bf) return;

    fprintf(bf, "Bone probe for entity %p\n", entity);
    fprintf(bf, "g_getComponentInChildren=%p\n", (void*)g_getComponentInChildren);
    fprintf(bf, "g_getComponentByType=%p\n", (void*)g_getComponentByType);
    fprintf(bf, "g_getBoneTransform=%p\n", (void*)g_getBoneTransform);
    fprintf(bf, "g_animatorType=%p\n", g_animatorType);
    fflush(bf);

    __try {
        void* viewComp = get_component(entity, g_idx_view);
        if (!viewComp && g_idx_view_data >= 0)
            viewComp = get_component(entity, g_idx_view_data);
        fprintf(bf, "viewComp=%p\n", viewComp);
        fflush(bf);
        if (!is_readable(viewComp, 0x18)) {
            fprintf(bf, "FAIL: viewComp invalid\n");
            fclose(bf); s_boneProbeCount++; return;
        }
        void* viewBehaviour = *(void**)((uintptr_t)viewComp + 0x10);
        fprintf(bf, "viewBehaviour=%p\n", viewBehaviour);
        fflush(bf);
        if (!is_readable(viewBehaviour, 0x10)) {
            fprintf(bf, "FAIL: viewBehaviour invalid\n");
            fclose(bf); s_boneProbeCount++; return;
        }

        fprintf(bf, "Calling GetComponentInChildren...\n");
        fflush(bf);
        void* animator = nullptr;
        if (g_getComponentInChildren)
            animator = g_getComponentInChildren(viewBehaviour, g_animatorType, nullptr);
        fprintf(bf, "GetComponentInChildren result: %p\n", animator);
        fflush(bf);

        if (!animator && g_getComponentByType) {
            fprintf(bf, "Trying GetComponentByType fallback...\n");
            fflush(bf);
            animator = g_getComponentByType(viewBehaviour, g_animatorType, nullptr);
            fprintf(bf, "GetComponentByType result: %p\n", animator);
            fflush(bf);
        }

        if (!is_readable(animator, 0x10)) {
            fprintf(bf, "FAIL: no valid animator found\n");
            fclose(bf); s_boneProbeCount++; return;
        }

        fprintf(bf, "\nBone scan:\n");
        int validCount = 0;
        for (int i = 0; i < 55; i++) {
            void* boneTf = g_getBoneTransform(animator, i, nullptr);
            if (is_readable(boneTf, 0x10)) {
                Vec3 pos;
                g_getPosition(&pos, boneTf, nullptr);
                fprintf(bf, "  Bone[%2d] = VALID  tf=%p  pos=(%.2f, %.2f, %.2f)\n", i, boneTf, pos.x, pos.y, pos.z);
                validCount++;
            } else {
                fprintf(bf, "  Bone[%2d] = ---\n", i);
            }
        }
        fprintf(bf, "\nTotal valid bones: %d / 55\n", validCount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(bf, "\n*** SEH EXCEPTION caught during bone probe ***\n");
    }
    fclose(bf);
    s_boneProbeCount = 20;
}
#endif

// ---------------------------------------------------------------------------
// dump_entities_to_file
// ---------------------------------------------------------------------------
void dump_entities_to_file() { g_dumpEntities.store(false); }
#if 0
void dump_entities_to_file_disabled_() {
    void* gcm = (void*)g_gameContextModule;
    if (!gcm) { g_dumpEntities.store(false); return; }

    void* context = nullptr;
    if (!safe_read_ptr((void*)((uintptr_t)gcm + 0x10), &context)) { g_dumpEntities.store(false); return; }
    if (!is_readable(context, 0xA0)) { g_dumpEntities.store(false); return; }

    void** entityPtrs = nullptr;
    int entityCount = 0;
    std::vector<void*> tempEnts;

    void* cache = *(void**)((uintptr_t)context + 0x98);
    if (is_readable(cache, 0x20)) {
        entityCount = (int)*(size_t*)((uintptr_t)cache + 0x18);
        entityPtrs = (void**)((uintptr_t)cache + 0x20);
    } else {
        void* hashSet = *(void**)((uintptr_t)context + 0x58);
        if (!is_readable(hashSet, 0x30)) { g_dumpEntities.store(false); return; }
        void* slots_arr = *(void**)((uintptr_t)hashSet + 0x18);
        int lastIndex = *(int*)((uintptr_t)hashSet + 0x24);
        if (!slots_arr || lastIndex <= 0) { g_dumpEntities.store(false); return; }
        size_t slots_len = *(size_t*)((uintptr_t)slots_arr + 0x18);
        uint8_t* slots = (uint8_t*)((uintptr_t)slots_arr + 0x20);
        int limit = (lastIndex < (int)slots_len) ? lastIndex : (int)slots_len;
        for (int s = 0; s < limit; s++) {
            int hc = *(int*)(slots + s * 16);
            if (hc < 0) continue;
            void* ent = *(void**)(slots + s * 16 + 8);
            if (ent) tempEnts.push_back(ent);
        }
        entityPtrs = tempEnts.data();
        entityCount = (int)tempEnts.size();
    }

    FILE* ef = fopen("C:\\ProgramData\\Microsoft\\PerfCache\\dumps\\entity_dump.txt", "w");
    if (!ef) { g_dumpEntities.store(false); return; }

    fprintf(ef, "Total entities: %d\n", entityCount);
    fprintf(ef, "Indices: BP=%d Pos=%d IA=%d INA=%d Intrs=%d IT=%d Par=%d ItemT=%d Id=%d\n\n",
            g_idx_blueprint, g_idx_position, g_idx_interactible,
            g_idx_interact_not_active, g_idx_interactions,
            g_idx_interact_target, g_idx_parent, g_idx_item_type, g_idx_id);
    fprintf(ef, "%-6s %-8s %-40s %-4s %-4s %-5s %-4s %-4s %-8s %-8s\n",
            "IDX", "CrIdx", "Blueprint", "IA", "INA", "Intrs", "Pos", "Par", "ParID", "SrvID");
    fprintf(ef, "---------------------------------------------------------------"
                "------------------------------------\n");

    for (int e = 0; e < entityCount; e++) {
        void* ent = entityPtrs[e];
        if (!is_readable(ent, 0x68)) continue;
        bool en = *(bool*)((uintptr_t)ent + 0x4C);
        if (!en) continue;

        int cIdx = *(int*)((uintptr_t)ent + 0x48);
        std::string bpName = "";
        void* bp = get_component(ent, g_idx_blueprint);
        if (bp) {
            void* ns = *(void**)((uintptr_t)bp + 0x10);
            bpName = read_il2cpp_string(ns);
        }

        bool hasIA = (g_idx_interactible >= 0 && get_component(ent, g_idx_interactible));
        bool hasINA = (g_idx_interact_not_active >= 0 && get_component(ent, g_idx_interact_not_active));
        bool hasIntrs = (g_idx_interactions >= 0 && get_component(ent, g_idx_interactions));
        bool hasPos = (g_idx_position >= 0 && get_component(ent, g_idx_position));
        bool hasPar = (g_idx_parent >= 0 && get_component(ent, g_idx_parent));

        int parId = -1;
        if (hasPar) {
            void* pc = get_component(ent, g_idx_parent);
            if (pc) parId = *(int*)((uintptr_t)pc + 0x10);
        }
        int srvId = -1;
        if (g_idx_id >= 0) {
            void* idc = get_component(ent, g_idx_id);
            if (idc) srvId = *(int*)((uintptr_t)idc + 0x10);
        }
        fprintf(ef, "%-6d %-8d %-40.40s %-4s %-4s %-5s %-4s %-4s %-8d %-8d\n",
                e, cIdx, bpName.empty() ? "<none>" : bpName.c_str(),
                hasIA ? "Y" : ".", hasINA ? "Y" : ".",
                hasIntrs ? "Y" : ".", hasPos ? "Y" : ".",
                hasPar ? "Y" : ".", parId, srvId);
    }
    fclose(ef);
    g_dumpEntities.store(false);
}
#endif

void dump_shop_classes(IL2CPP_API&) { }
#if 0
void dump_shop_classes_disabled_(IL2CPP_API& api) {
    FILE* f = fopen("_", "w");
    if (!f) return;
    fprintf(f, "=== SHOP/STORE IL2CPP CLASS SCAN ===\n\n");

    size_t asmCount = 0;
    void* dom = api.il2cpp_domain_get();
    if (!dom) { fclose(f); return; }
    void** assemblies = api.il2cpp_domain_get_assemblies(dom, &asmCount);
    if (!assemblies) { fclose(f); return; }

    int matchCount = 0;
    for (size_t i = 0; i < asmCount; i++) {
        void* img = api.il2cpp_assembly_get_image(assemblies[i]);
        if (!img) continue;
        size_t classCount = api.il2cpp_image_get_class_count(img);
        for (size_t j = 0; j < classCount; j++) {
            void* klass = api.il2cpp_image_get_class(img, j);
            if (!klass) continue;
            const char* cn = api.il2cpp_class_get_name(klass);
            if (!cn) continue;

            char lower[256];
            size_t k = 0;
            for (; cn[k] && k < 255; k++)
                lower[k] = (char)tolower((unsigned char)cn[k]);
            lower[k] = '\0';

            bool match = strstr(lower, "shop") || strstr(lower, "store") ||
                         strstr(lower, "vendor") || strstr(lower, "purchase") ||
                         strstr(lower, "buy") || strstr(lower, "market") ||
                         strstr(lower, "trade") || strstr(lower, "merchant") ||
                         strstr(lower, "inventory") || strstr(lower, "catalog") ||
                         strstr(lower, "commerce") || strstr(lower, "currency") ||
                         strstr(lower, "wallet") || strstr(lower, "economy") ||
                         strstr(lower, "price") || strstr(lower, "cost") ||
                         strstr(lower, "research") || strstr(lower, "progression") ||
                         strstr(lower, "blueprint") || strstr(lower, "unlock") ||
                         strstr(lower, "craft") || strstr(lower, "build") ||
                         strstr(lower, "walker") || strstr(lower, "masterserver");

            if (!match) continue;
            matchCount++;

            const char* ns = "";
            if (api.il2cpp_class_get_namespace)
                ns = api.il2cpp_class_get_namespace(klass);

            fprintf(f, "[%d] %s.%s\n", matchCount, ns ? ns : "", cn);

            void* iter = nullptr;
            void* method;
            int mIdx = 0;
            while ((method = api.il2cpp_class_get_methods(klass, &iter)) != nullptr) {
                const char* mname = api.il2cpp_method_get_name(method);
                int pc = api.il2cpp_method_get_param_count ? (int)api.il2cpp_method_get_param_count(method) : -1;
                void* addr = *(void**)method;
                fprintf(f, "    [%d] %s (params=%d) addr=%p\n", mIdx++, mname ? mname : "???", pc, addr);
            }
            fprintf(f, "\n");
        }
    }

    fprintf(f, "\nTotal matching classes: %d\n", matchCount);
    fclose(f);
}
#endif

// ---------------------------------------------------------------------------
// probe_context_to_file
// ---------------------------------------------------------------------------
void probe_context_to_file() { g_probeContext.store(false); }

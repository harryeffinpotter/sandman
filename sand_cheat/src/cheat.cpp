#include "cheat.h"

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <tlhelp32.h>

static void wlog(const char* fmt, ...) {
    FILE* f = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\worker_debug.txt", "a");
    if (!f) return;
    fprintf(f, "[%lu] ", GetTickCount());
    va_list a; va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fflush(f); fclose(f);
}

// ---------------------------------------------------------------------------
// RESOLVE macro
// ---------------------------------------------------------------------------
#define RESOLVE(api, mod, name) \
    api.name = (fn_##name)GetProcAddress(mod, #name);

// ---------------------------------------------------------------------------
// Global variable definitions
// ---------------------------------------------------------------------------
volatile void* g_gameContextModule = nullptr;
volatile void* g_findInteractSystem = nullptr;
volatile bool g_hooked = false;
std::atomic<bool> g_hwbpActive{false};

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
int g_idx_cheat_walker_fly = -1;
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
int g_idx_mob_living_sand = -1;
int g_idx_mob_living_sand_jr = -1;
int g_idx_ai_agent = -1;

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
std::atomic<bool> g_turretNoRecoil{false};
std::atomic<bool> g_weaponModsEnabled{false};
std::atomic<bool> g_weaponNoDrop{false};
std::atomic<bool> g_weaponNoBloom{false};
std::atomic<float> g_weaponVelocityMult{1.0f};
std::atomic<uintptr_t> g_lockedEntityPtr{0};
std::atomic<uintptr_t> g_cachedRecoilEntity{0};
std::atomic<bool> g_running{true};

WorldVector g_playerPos = {};
void* g_userNameKlass = nullptr;
std::atomic<int> g_entityCount{0};

std::string g_nameFilter;
int g_scrollOffset = 0;
std::unordered_set<std::string> g_hiddenNames;
std::vector<std::string> g_hiddenPrefixes = { "Mob", "walker_", "EXPEDITION_WALKER" };

Hook g_executeHook;
fn_execute g_original_execute = nullptr;
Hook g_farHook;

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
void* g_animatorType = nullptr;
void* g_userNameType = nullptr;
std::atomic<bool> g_espShowSkeleton{false};
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
int g_aimbotActivationKey = VK_XBUTTON2;
AimbotProfile g_aimPlayer;
AimbotProfile g_aimMob;
bool g_mobAimbotSame = true;

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
    RESOLVE(api, ga, il2cpp_class_get_methods);
    RESOLVE(api, ga, il2cpp_method_get_name);
    RESOLVE(api, ga, il2cpp_method_get_param_count);
    RESOLVE(api, ga, il2cpp_class_get_type);
    RESOLVE(api, ga, il2cpp_type_get_object);
    RESOLVE(api, ga, il2cpp_string_new);
}

static bool is_readable(const void* ptr, size_t len) {
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD prot = mbi.Protect & 0xFF;
    if (!(prot == PAGE_READONLY || prot == PAGE_READWRITE ||
          prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
          prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_WRITECOPY))
        return false;
    uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    return ((uintptr_t)ptr + len) <= regionEnd;
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

    h.trampoline_exec = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!h.trampoline_exec) return false;

    memcpy(h.trampoline_exec, target, steal_count);

    uint8_t* p = (uint8_t*)h.trampoline_exec + steal_count;
    uintptr_t ret_addr = (uintptr_t)target + steal_count;
    p[0] = 0xFF; p[1] = 0x25; *(uint32_t*)(p + 2) = 0; *(uintptr_t*)(p + 6) = ret_addr;

    DWORD old_prot;
    VirtualProtect(target, steal_count, PAGE_EXECUTE_READWRITE, &old_prot);
    uint8_t* t = (uint8_t*)target;
    t[0] = 0xFF; t[1] = 0x25; *(uint32_t*)(t + 2) = 0; *(uintptr_t*)(t + 6) = (uintptr_t)detour;
    for (int i = 14; i < steal_count; i++) t[i] = 0x90;
    VirtualProtect(target, steal_count, old_prot, &old_prot);
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

    uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tramp) return false;
    memcpy(tramp, target, steal_count);
    uint8_t* j = tramp + steal_count;
    uintptr_t ret_addr = (uintptr_t)target + steal_count;
    j[0] = 0xFF; j[1] = 0x25; *(uint32_t*)(j + 2) = 0; *(uintptr_t*)(j + 6) = ret_addr;
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
        else if (strcmp(narrow, "CheatWalkerFly") == 0)          g_idx_cheat_walker_fly = i;
        else if (strcmp(narrow, "CheatWalkerSpeedMultiplier") == 0) g_idx_cheat_walker_speed = i;
        else if (strcmp(narrow, "ShotInfo") == 0)                g_idx_shot_info = i;
                else if (strcmp(narrow, "NiceNameData") == 0)            g_idx_nice_name = i;
        else if (strcmp(narrow, "AccountId") == 0)               g_idx_account_id = i;
        else if (strcmp(narrow, "View") == 0)                g_idx_view = i;
        else if (strcmp(narrow, "ViewPosition") == 0) g_idx_view_position = i;
        else if (strcmp(narrow, "ViewData") == 0)      g_idx_view_data = i;
        else if (strcmp(narrow, "CharacterControllerViewBehaviour") == 0) g_idx_char_ctrl_vb = i;
        else if (strcmp(narrow, "FPSCharacterControllerViewBehaviour") == 0) g_idx_fps_ctrl_vb = i;
        else if (strcmp(narrow, "MobViewBehaviour") == 0)     g_idx_mob_vb = i;
        else if (strcmp(narrow, "SimpleAnimatorViewBehaviour") == 0) g_idx_simple_anim_vb = i;
        else if (strcmp(narrow, "MobState") == 0)             g_idx_mob_state = i;
        else if (strcmp(narrow, "MobGhoulData") == 0)         g_idx_mob_ghoul = i;
        else if (strcmp(narrow, "MobLivingSandData") == 0)    g_idx_mob_living_sand = i;
        else if (strcmp(narrow, "MobLivingSandJrData") == 0)  g_idx_mob_living_sand_jr = i;
        else if (strcmp(narrow, "AiAgentData") == 0)          g_idx_ai_agent = i;
    }

    FILE* dumpF = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\component_names.txt", "w");
    if (dumpF) {
        for (int i = 0; i < count; i++) {
            void* str = elements[i];
            if (!is_readable(str, 0x14)) { fprintf(dumpF, "%d: <null>\n", i); continue; }
            int len2 = *(int*)((uintptr_t)str + 0x10);
            if (len2 <= 0 || len2 > 200) { fprintf(dumpF, "%d: <invalid len %d>\n", i, len2); continue; }
            wchar_t* wc = (wchar_t*)((uintptr_t)str + 0x14);
            char nm[256];
            for (int c2 = 0; c2 < len2 && c2 < 255; c2++) nm[c2] = (char)wc[c2];
            nm[(len2 < 255) ? len2 : 255] = 0;
            fprintf(dumpF, "%d: %s\n", i, nm);
        }
        fflush(dumpF);
        fclose(dumpF);
    }

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

        if (g_turretNoRecoil.load()) {
            if (g_idx_recoil_look >= 0) {
                void* rl = get_component(entity, g_idx_recoil_look);
                if (rl) {
                    memset((void*)((uintptr_t)rl + 0x10), 0, 48);
                    g_cachedRecoilEntity.store((uintptr_t)entity);
                    (*applied)++;
                }
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


// ---------------------------------------------------------------------------
// scan_entities
// ---------------------------------------------------------------------------
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

        g_entityCount.store(entityCount);
        if (doLog) wlog("[scan] entityCount=%d source=%s cache=%p context=%p\n",
                        entityCount, is_readable(cache, 0x20) ? "cache+0x98" : "hashSet+0x58",
                        cache, context);

        static bool s_diagDone = false;
        if (!s_diagDone && entityCount > 0 && entityPtrs) {
            s_diagDone = true;
            FILE* df = fopen("C:\\Users\\ysg\\projects\\il2cpp_dumper\\entity_diag.txt", "w");
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

        static int s_scanDiagCount = 0;
        if (s_scanDiagCount < 3 && entityCount > 0) {
            s_scanDiagCount++;
            FILE* sd = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\scan_diag.txt", "w");
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
                {
                    FILE* sl = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\scan_diag.txt", "a");
                    if (sl) { fprintf(sl, "\n--- ENTITY LOOP STARTING (entityCount=%d) ---\n", entityCount); fflush(sl); fclose(sl); }
                }
            }
        }

        std::vector<ItemInfo> items;
        int vehEntityRecoveries = 0;
        int entitiesSkippedFilter = 0;
        int entitiesPushed = 0;
        int dbgReadable = 0, dbgValidObj = 0, dbgEnabled = 0, dbgHasBP = 0;
        int dbgNameOK = 0, dbgPassFilter = 0, dbgHasPosParent = 0;
        int heldCount = 0, resolvedParentCount = 0;
        for (int e = 0; e < entityCount; e++) {
            void* entity = entityPtrs[e];
            if (!is_readable(entity, 0x68)) continue;
            dbgReadable++;
            if (!is_valid_obj(entity)) continue;
            dbgValidObj++;

            bool isEnabled = *(bool*)((uintptr_t)entity + 0x4C);
            if (!isEnabled) continue;
            dbgEnabled++;

            void* bpComp = get_component(entity, g_idx_blueprint);
            if (!is_readable(bpComp, 0x18)) continue;
            dbgHasBP++;

            void* nameStr = *(void**)((uintptr_t)bpComp + 0x10);
            std::string name = read_il2cpp_string(nameStr);
            if (name.empty()) continue;
            dbgNameOK++;

            int eid = *(int*)((uintptr_t)entity + 0x48);
            if (eid == playerEntityId) continue;

            if (name.rfind("item_containerBox", 0) == 0) continue;
            if (name.rfind("env_", 0) == 0) continue;
            if (name.rfind("Ground", 0) == 0) continue;
            if (name.rfind("prop_", 0) == 0) continue;
            if (name.rfind("cde_", 0) == 0) continue;
            if (name.rfind("walker_", 0) == 0) continue;
            if (name == "Sun") continue;
            if (name.rfind("LandingCutScene", 0) == 0) continue;
            if (name.rfind("Shot Projectile", 0) == 0) continue;
            dbgPassFilter++;

            void* posComp = get_component(entity, g_idx_position);
            bool hasParent = (g_idx_parent >= 0 && get_component(entity, g_idx_parent));
            if (!posComp && !hasParent) continue;
            dbgHasPosParent++;

            ItemInfo info;
            info.name = name;
            info.entityId = eid;
            info.entityPtr = entity;
            info.distance = -1.0f;
            info.hasTransformPos = false;
            info.hasBones = false;
            info.isCreature = false;
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
            info.isHeldByPlayer = hasParent;
            if (hasParent) heldCount++;
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
                        if (parentPos) {
                            resolvedPos = *(WorldVector*)((uintptr_t)parentPos + 0x10);
                            hasResolvedPos = true;
                            resolvedParentCount++;
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
            } else if (name.rfind("EXPEDITION_WALKER", 0) == 0 || name.rfind("walker_", 0) == 0) {
                info.displayName = name;
            } else {
                info.displayName = get_display_name(name);
                if (name.find("_t3_") != std::string::npos || name.find("_T3_") != std::string::npos)
                    info.lootTier = 3;
                else if (name.find("_t2_") != std::string::npos || name.find("_T2_") != std::string::npos)
                    info.lootTier = 2;
                else if (name.find("_t1_") != std::string::npos || name.find("_T1_") != std::string::npos)
                    info.lootTier = 1;
                if (name.rfind("Mob", 0) == 0 || name.rfind("mob_", 0) == 0
                    || name.rfind("Ai", 0) == 0 || name.rfind("Sentinel", 0) == 0
                    || name.rfind("Trampler", 0) == 0) {
                    info.isCreature = true;
                }
            }

            bool isPlayer = (name.rfind("PlayerAvatar", 0) == 0);
            if (isPlayer || info.isCreature) {
                memset(info.bonePositions, 0, sizeof(info.bonePositions));
                info.hasBones = seh_resolve_bones(entity, info.bonePositions);
            }

            entitiesPushed++;
            items.push_back(std::move(info));
        }

        if (doLog) wlog("[scan] readable=%d validObj=%d enabled=%d hasBP=%d nameOK=%d passFilter=%d hasPosParent=%d pushed=%d\n",
                        dbgReadable, dbgValidObj, dbgEnabled, dbgHasBP, dbgNameOK, dbgPassFilter, dbgHasPosParent, entitiesPushed);
        if (doLog) wlog("[scan] heldItems=%d resolvedParents=%d idToEntitySize=%d\n",
                        heldCount, resolvedParentCount, (int)idToEntity.size());

        {
            static int s_itemCountLogCount = 0;
            if (s_itemCountLogCount < 3) {
                s_itemCountLogCount++;
                FILE* sf = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\scan_diag.txt", "a");
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

        if (g_dupeMode.load() && !g_stickyLock.load()) {
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

static bool seh_resolve_bones(void* entity, BoneWorldPos* bones) {
    if (!g_getBoneTransform || !g_getPosition) return false;
    if (!g_getComponentInChildren && !g_getComponentByType) return false;
    if (!g_animatorType) return false;
    __try {
        void* viewBehaviour = nullptr;

        if (g_idx_view >= 0) {
            void* viewComp = get_component(entity, g_idx_view);
            if (is_readable(viewComp, 0x18)) {
                void* vb = *(void**)((uintptr_t)viewComp + 0x10);
                if (is_readable(vb, 0x10) && is_valid_obj(vb))
                    viewBehaviour = vb;
            }
        }

        if (!viewBehaviour && g_idx_view_data >= 0) {
            void* vdComp = get_component(entity, g_idx_view_data);
            if (is_readable(vdComp, 0x18)) {
                void* vb = *(void**)((uintptr_t)vdComp + 0x10);
                if (is_readable(vb, 0x10) && is_valid_obj(vb))
                    viewBehaviour = vb;
            }
        }

        static const int* vbIndices[] = {
            &g_idx_char_ctrl_vb, &g_idx_fps_ctrl_vb,
            &g_idx_mob_vb, &g_idx_simple_anim_vb
        };
        for (int vi = 0; !viewBehaviour && vi < 4; vi++) {
            int idx = *vbIndices[vi];
            if (idx < 0) continue;
            void* comp = get_component(entity, idx);
            if (!is_readable(comp, 0x18)) continue;
            void* vb = *(void**)((uintptr_t)comp + 0x10);
            if (is_readable(vb, 0x10) && is_valid_obj(vb))
                viewBehaviour = vb;
        }

        if (!viewBehaviour || !is_valid_obj(viewBehaviour)) return false;

        g_vehInnerActive = true;
        RtlCaptureContext(&g_vehInnerCtx);
        if (g_vehCrashRecovered) {
            g_vehCrashRecovered = false;
            g_vehInnerActive = false;
            g_workerVehActive = true;
            return false;
        }

        void* animator = nullptr;
        if (g_getComponentInChildren)
            animator = g_getComponentInChildren(viewBehaviour, g_animatorType, nullptr);
        if (!animator && g_getComponentByType)
            animator = g_getComponentByType(viewBehaviour, g_animatorType, nullptr);
        if (!is_readable(animator, 0x10) || !is_valid_obj(animator)) { g_vehInnerActive = false; return false; }
        static const int USED_BONES[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,54};
        bool anyBone = false;
        for (int ub = 0; ub < 20; ub++) {
            int bi = USED_BONES[ub];
            void* boneTf = g_getBoneTransform(animator, bi, nullptr);
            if (is_readable(boneTf, 0x10)) {
                Vec3 bpos;
                g_getPosition(&bpos, boneTf, nullptr);
                if (!std::isnan(bpos.x) && !std::isnan(bpos.y) && !std::isnan(bpos.z)) {
                    bones[bi].pos = bpos;
                    bones[bi].valid = true;
                    anyBone = true;
                }
            }
        }
        g_vehInnerActive = false;
        return anyBone;
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[seh_resolve_bones] SEH: 0x%08lX\n", GetExceptionCode()); return false; }
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

static bool seh_resolve_username(void* entity, char* outBuf, int bufSize) {
    if (!g_userNameType || !g_getComponentInChildren || g_idx_view < 0) return false;
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
                void* userNameComp = g_getComponentInChildren(viewBehaviour, g_userNameType, nullptr);
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
    } __except(EXCEPTION_EXECUTE_HANDLER) { wlog("[seh_resolve_username] SEH: 0x%08lX\n", GetExceptionCode()); }
    return false;
}

static void probe_bones_once(void* entity) {
    if (s_boneProbeCount >= 20) return;
    if (g_idx_view < 0 || !g_getBoneTransform || !g_getTransform || !g_getPosition) return;
    if (!g_getComponentInChildren && !g_getComponentByType) return;
    if (!g_animatorType) return;

    FILE* bf = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\bone_probe.txt", "w");
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

// ---------------------------------------------------------------------------
// dump_entities_to_file
// ---------------------------------------------------------------------------
void dump_entities_to_file() {
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

    FILE* ef = fopen("C:\\Users\\ysg\\projects\\il2cpp_dumper\\entity_dump.txt", "w");
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

void dump_shop_classes(IL2CPP_API& api) {
    FILE* f = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\shop_probe.txt", "w");
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

// ---------------------------------------------------------------------------
// probe_context_to_file
// ---------------------------------------------------------------------------
void probe_context_to_file() {
    void* gcm = (void*)g_gameContextModule;

    FILE* pf = fopen("C:\\Users\\ysg\\projects\\il2cpp_dumper\\probe.txt", "w");
    if (!pf) { g_probeContext.store(false); return; }

    if (gcm) {
        fprintf(pf, "[PROBE] GameContextModule at %p\n", gcm);
        fprintf(pf, "[INFO] Component indices: BP=%d Pos=%d IA=%d INA=%d Intrs=%d IT=%d Par=%d ItemT=%d Id=%d\n",
                g_idx_blueprint, g_idx_position, g_idx_interactible,
                g_idx_interact_not_active, g_idx_interactions,
                g_idx_interact_target, g_idx_parent, g_idx_item_type, g_idx_id);
    }

    fclose(pf);
    g_probeContext.store(false);
}

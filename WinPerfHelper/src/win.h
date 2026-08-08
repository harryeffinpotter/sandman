#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <unordered_set>

// ---------------------------------------------------------------------------
// IL2CPP function pointer typedefs
// ---------------------------------------------------------------------------
typedef void*       (*fn_il2cpp_domain_get)();
typedef void**      (*fn_il2cpp_domain_get_assemblies)(void* domain, size_t* count);
typedef void*       (*fn_il2cpp_assembly_get_image)(void* assembly);
typedef size_t      (*fn_il2cpp_image_get_class_count)(void* image);
typedef void*       (*fn_il2cpp_image_get_class)(void* image, size_t index);
typedef const char* (*fn_il2cpp_image_get_name)(void* image);
typedef const char* (*fn_il2cpp_class_get_name)(void* klass);
typedef const char* (*fn_il2cpp_class_get_namespace)(void* klass);
typedef void*       (*fn_il2cpp_class_get_fields)(void* klass, void** iter);
typedef const char* (*fn_il2cpp_field_get_name)(void* field);
typedef size_t      (*fn_il2cpp_field_get_offset)(void* field);
typedef void*       (*fn_il2cpp_field_get_type)(void* field);
typedef const char* (*fn_il2cpp_type_get_name)(void* type);
typedef void*       (*fn_il2cpp_class_from_name)(void* image, const char* namespaze, const char* name);
typedef void*       (*fn_il2cpp_thread_attach)(void* domain);
typedef void        (*fn_il2cpp_field_static_get_value)(void* field, void* value);
typedef uint32_t    (*fn_il2cpp_field_get_flags)(void* field);
typedef void*       (*fn_il2cpp_class_get_parent)(void* klass);
typedef uint32_t    (*fn_il2cpp_class_instance_size)(void* klass);
typedef void*       (*fn_il2cpp_object_new)(void* klass);
typedef void*       (*fn_il2cpp_class_get_methods)(void* klass, void** iter);
typedef const char* (*fn_il2cpp_method_get_name)(void* method);
typedef uint32_t    (*fn_il2cpp_method_get_param_count)(void* method);
typedef void*       (*fn_il2cpp_method_get_return_type)(void* method);
typedef void*       (*fn_il2cpp_method_get_param)(void* method, uint32_t index);
typedef const char* (*fn_il2cpp_method_get_param_name)(void* method, uint32_t index);
typedef void*       (*fn_il2cpp_class_get_type)(void* klass);
typedef void*       (*fn_il2cpp_type_get_object)(void* type);
typedef void*       (*fn_il2cpp_string_new)(const char* str);

// ---------------------------------------------------------------------------
// IL2CPP API struct
// ---------------------------------------------------------------------------
struct IL2CPP_API {
    fn_il2cpp_domain_get                il2cpp_domain_get;
    fn_il2cpp_domain_get_assemblies     il2cpp_domain_get_assemblies;
    fn_il2cpp_assembly_get_image        il2cpp_assembly_get_image;
    fn_il2cpp_image_get_class_count     il2cpp_image_get_class_count;
    fn_il2cpp_image_get_class           il2cpp_image_get_class;
    fn_il2cpp_image_get_name            il2cpp_image_get_name;
    fn_il2cpp_class_get_name            il2cpp_class_get_name;
    fn_il2cpp_class_get_namespace       il2cpp_class_get_namespace;
    fn_il2cpp_class_get_fields          il2cpp_class_get_fields;
    fn_il2cpp_field_get_name            il2cpp_field_get_name;
    fn_il2cpp_field_get_offset          il2cpp_field_get_offset;
    fn_il2cpp_field_get_type            il2cpp_field_get_type;
    fn_il2cpp_type_get_name             il2cpp_type_get_name;
    fn_il2cpp_class_from_name           il2cpp_class_from_name;
    fn_il2cpp_thread_attach             il2cpp_thread_attach;
    fn_il2cpp_field_static_get_value    il2cpp_field_static_get_value;
    fn_il2cpp_field_get_flags           il2cpp_field_get_flags;
    fn_il2cpp_class_get_parent          il2cpp_class_get_parent;
    fn_il2cpp_class_instance_size       il2cpp_class_instance_size;
    fn_il2cpp_object_new                il2cpp_object_new;
    fn_il2cpp_class_get_methods         il2cpp_class_get_methods;
    fn_il2cpp_method_get_name           il2cpp_method_get_name;
    fn_il2cpp_method_get_param_count    il2cpp_method_get_param_count;
    fn_il2cpp_method_get_return_type    il2cpp_method_get_return_type;
    fn_il2cpp_method_get_param          il2cpp_method_get_param;
    fn_il2cpp_method_get_param_name     il2cpp_method_get_param_name;
    fn_il2cpp_class_get_type        il2cpp_class_get_type;
    fn_il2cpp_type_get_object       il2cpp_type_get_object;
    fn_il2cpp_string_new            il2cpp_string_new;
};

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------
struct WorldVector {
    float x, y, z;
    int cx, cy;
};

struct Vec3 { float x, y, z; };

struct BoneWorldPos {
    Vec3 pos;
    bool valid;
};

struct ItemInfo {
    std::string name;
    WorldVector pos;
    float distance;
    int entityId;
    int serverId;
    void* entityPtr;
    bool isWeapon;
    bool isHeavy;
    bool isHeldByPlayer;
    bool hasOwnPosition;
    WorldVector viewPos;
    bool hasViewPos;
    bool isCreature;
    int lootTier;
    Vec3 transformWorldPos;
    bool hasTransformPos;
    BoneWorldPos bonePositions[55];
    bool hasBones;
    std::string displayName;
    float velX, velY, velZ;
    DWORD lastPosTime;
    bool isExtraction;   // has ExtractionPointData / ExtractionBox / etc
    bool isReactor;      // has ReactorData (= ship)
    bool isFinalExtract; // has FinalExtractionPointData
    int  parentEntityId; // immediate parent id (0 if none)
    bool isInOthersInv;  // parent chain ends at a non-us entity — potential remote-inv target
};

struct Hook {
    uint8_t original_bytes[32];
    uint8_t trampoline[48];
    void* trampoline_exec;
    void* target;
    void* detour;
    int stolen_bytes;
};

struct HWBPHook {
    void* target;
    void* detour;
    void* trampoline;
    int drIndex;
};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const float CHUNK_SIZE = 256.0f;

// ---------------------------------------------------------------------------
// Hook function pointer typedefs
// ---------------------------------------------------------------------------
typedef void (*fn_execute)(void* thisPtr);
typedef bool (*fn_is_too_far)(void* thisPtr, int32_t targetId, void* avatar);
typedef void* (*fn_camera_get_main)(void* method);
typedef Vec3* (*fn_camera_w2s)(Vec3* ret, void* camera, Vec3* position, void* method);
typedef void* (*fn_get_transform)(void* component, void* method);
typedef Vec3* (*fn_get_forward)(Vec3* ret, void* transform, void* method);
typedef Vec3* (*fn_get_position)(Vec3* ret, void* transform, void* method);
typedef void* (*fn_get_parent)(void* transform, void* method);
typedef void* (*fn_get_bone_transform)(void* animator, int boneIndex, void* method);
typedef void* (*fn_get_component_by_type)(void* component, void* type, void* method);
typedef void* (*fn_get_component_in_children)(void* component, void* type, void* method);
typedef int (*fn_get_child_count)(void* transform, void* method);
typedef void* (*fn_get_child)(void* transform, int index, void* method);
typedef void* (*fn_get_name)(void* obj, void* method);

// ---------------------------------------------------------------------------
// Globals (defined in win.cpp)
// ---------------------------------------------------------------------------
extern volatile void* g_gameContextModule;
extern volatile void* g_findInteractSystem;
extern volatile bool g_hooked;
extern std::atomic<bool> g_hwbpActive;
void set_hwbp_active(bool enabled);

extern int g_idx_blueprint;
extern int g_idx_position;
extern int g_idx_interactible;
extern int g_idx_interact_target;
extern int g_idx_parent;
extern int g_idx_item_type;
extern int g_idx_interact_not_active;
extern int g_idx_interactions;
extern int g_idx_id;
extern int g_idx_large_item;
extern int g_idx_overheated;
extern int g_idx_recoil_look;
extern int g_idx_stationary_auto;
extern int g_idx_stationary_data;
extern int g_idx_weapon_overheat;
extern int g_idx_weapon_overheat_data;
extern int g_idx_auto_turret;
extern int g_idx_bullet_projectile_data;
extern int g_idx_health_data;
extern int g_idx_invincible;
extern int g_idx_speed_data;
extern int g_idx_jump;
extern int g_idx_cheat_walker_fly;
extern int g_idx_cheat_walker_speed;
extern int g_idx_shot_info;
extern int g_idx_nice_name;
extern int g_idx_account_id;
extern int g_idx_view;
extern int g_idx_view_position;
extern int g_idx_view_data;
extern int g_idx_char_ctrl_vb;
extern int g_idx_fps_ctrl_vb;
extern int g_idx_mob_vb;
extern int g_idx_simple_anim_vb;
extern int g_idx_mob_state;
extern int g_idx_mob_ghoul;
extern int g_idx_mob_ls;
extern int g_idx_mob_ls_jr;
extern int g_idx_ai_agent;

extern std::vector<ItemInfo> g_items;
extern CRITICAL_SECTION g_itemsLock;

extern std::atomic<bool> g_permaLockActive;
extern std::string g_permaLockName;
extern std::atomic<int> g_lockedEntityId;
extern std::atomic<bool> g_dupeMode;
extern std::atomic<bool> g_stickyLock;
extern std::atomic<bool> g_weaponFilter;
extern std::atomic<bool> g_heavyBypass;
extern std::atomic<bool> g_turretNoOverheat;
extern std::atomic<bool> g_turretRapidFire;
extern std::atomic<bool> g_turretNoRecoil;
extern std::atomic<bool> g_weaponModsEnabled;
extern std::atomic<bool> g_weaponNoDrop;
extern std::atomic<bool> g_weaponNoBloom;
extern std::atomic<float> g_weaponVelocityMult;
extern std::atomic<uintptr_t> g_lockedEntityPtr;
extern std::atomic<uintptr_t> g_cachedRecoilEntity;
extern std::atomic<bool> g_running;
extern volatile DWORD g_workerThreadId;
extern volatile DWORD g_renderThreadId;
extern volatile bool g_workerVehActive;
extern CONTEXT g_vehSavedCtx;
extern volatile bool g_vehCrashRecovered;
extern CONTEXT g_vehInnerCtx;
extern volatile bool g_vehInnerActive;
extern CONTEXT g_vehEntityCtx;
extern volatile bool g_vehEntityActive;
extern volatile uintptr_t g_lastVehRip;
extern volatile ULONG g_lastVehCode;
extern volatile uintptr_t g_lastVehModBase;
extern char g_lastVehModName[64];
extern volatile int g_lastVehScope;

extern WorldVector g_playerPos;
extern std::atomic<int> g_entityCount;

extern std::string g_nameFilter;
extern int g_scrollOffset;
extern std::unordered_set<std::string> g_hiddenNames;
extern std::vector<std::string> g_hiddenPrefixes;

extern Hook g_executeHook;
extern fn_execute g_original_execute;
extern Hook g_farHook;
extern Hook g_publishHook;
extern void* g_holoPublishAddr;
extern void* g_holoMessengerInstance;
extern std::atomic<bool> g_captureMessages;
extern std::atomic<bool> g_dupeLabRecording;
extern std::atomic<bool> g_showContainerContents;
extern std::atomic<int>  g_dupeSpoofType;
extern std::atomic<int>  g_dupeForceHandSlot;
extern std::atomic<int>  g_hotkeyHardKill;
extern std::atomic<int>  g_hotkeyDupeSuspend;
extern std::atomic<int>  g_hotkeyDupeMaster;
extern std::atomic<int>  g_hotkeyPlaybackFirst;
extern std::atomic<int>  g_hotkeyCaptureRequest;
extern std::atomic<int>  g_actionDelaySec;
extern std::atomic<int>  g_pendingActionId;
extern std::atomic<unsigned long long> g_pendingActionDeadline;
extern std::atomic<unsigned long long> g_lastPresentTick;  // updated by game render thread in hooked_present
void dupelab_schedule(int actionId, const char* arg);
void dupelab_check_pending();
// Dupe Lab actions that mutate the currently-locked entity's components.
// All SEH-safe: silently no-op if lock is empty or components missing.
void dupelab_spoof_type_on_locked(int typeValue);
void dupelab_force_slot_on_locked(int slotValue);
void dupelab_strip_interactible_not_active_on_locked();
// Direct HoloMessage dispatchers — construct + Publish. Field offsets are
// guessed from il2cpp instance packing; if wrong the SEH catch traps it.
void dupelab_dispatch_equip_slot(int slotId);
void dupelab_dispatch_drop_slot(int slotId);
void dupelab_dispatch_split(int fromSlot, int fromParent, int toSlot, int toParent, int count);
void dupelab_dispatch_move(int fromSlot, int fromParent, int toSlot, int toParent);
void __fastcall hooked_publish(void* thisPtr, void* msg);
void dupelab_record_start(const std::string& name);
void dupelab_record_stop();
size_t dupelab_recording_count(const std::string& name);
void dupelab_playback(const std::string& name);
// C-string wrappers so main.cpp hotkey handlers can call without triggering
// C2712 (constructing std::string inside a function with __try scope).
size_t dupelab_recording_count_cstr(const char* name);
void dupelab_playback_cstr(const char* name);

extern std::atomic<bool> g_dumpEntities;
extern std::atomic<bool> g_probeContext;
extern std::atomic<bool> g_dumpShopClasses;
extern std::atomic<int> g_executeHookCalls;
extern std::atomic<int> g_forceInteractWrites;
extern std::atomic<int> g_turretEntitiesFound;
extern std::atomic<int> g_turretModsApplied;
extern std::atomic<int> g_dbgHasWeaponHeat;
extern std::atomic<int> g_dbgHasStationaryAuto;
extern std::atomic<int> g_dbgHasRecoilLook;
extern std::atomic<int> g_dbgHasOverheated;

extern std::atomic<bool> g_espEnabled;
extern float g_radarRange;
extern std::atomic<bool> g_espShowMobs;
extern fn_camera_get_main g_cameraGetMain;
extern fn_camera_w2s g_cameraW2S;
extern std::atomic<bool> g_esp3DEnabled;
extern float g_espMaxDist;
extern float g_espPlayerDist;
extern float g_espMobDist;
extern float g_espWalkerDist;
extern float g_espItemDist;
extern std::atomic<bool> g_espShowItems;
extern std::atomic<bool> g_espShowSelf;
extern std::atomic<bool> g_espShowPlayers;
extern fn_get_transform g_getTransform;
extern fn_get_forward g_getForward;
extern fn_get_position g_getPosition;
extern fn_get_parent g_getParent;
extern std::atomic<bool> g_espShowWalkers;
extern fn_get_bone_transform g_getBoneTransform;
extern fn_get_component_by_type g_getComponentByType;
extern fn_get_component_in_children g_getComponentInChildren;
extern fn_get_child_count g_getChildCount;
extern fn_get_child g_getChild;
extern fn_get_name g_getName;
extern void* g_animatorType;
extern std::atomic<bool> g_espShowSkeleton;
extern std::atomic<bool> g_espShowLootT1;
extern std::atomic<bool> g_espShowLootT2;
extern std::atomic<bool> g_espShowLootT3;
extern float g_espLootT1Dist;
extern float g_espLootT2Dist;
extern float g_espLootT3Dist;
extern uintptr_t g_gaBase;
extern uintptr_t g_gaSize;
extern void* g_userNameKlass;
extern void* g_userNameHUDKlass;   // HUD-side MonoBehaviour type for GCiC
extern void* g_userNameHUDType;    // il2cpp_type_get_object of ^
extern void* g_userContextModuleInstance;
extern void* g_userContextModuleKlass;
extern void* g_userNameType;
// Address of UserContextModule.GetEntityWithAccountId(ulong) — captured at boot
// so win.cpp can invoke it per PlayerAvatar to resolve real player names.
typedef void* (*fn_getEntByAcctId)(void* thisPtr, unsigned long long accountId, void* methodInfo);
extern fn_getEntByAcctId g_getUserEntityByAcctId;
extern int   g_userNameFieldOffset;   // Byte offset of the String field inside UserNameComponent, resolved once at boot via il2cpp FieldInfo. -1 = unresolved.
extern std::atomic<int> g_userNameRescanRequest;  // UI bumps this; worker re-runs UserName klass enumeration next tick.

struct AimbotProfile {
    bool realityAim = true;
    float magnetism = 0.5f;
    bool magnetismRandomize = false;
    float magnetismRandomAmt = 0.1f;
    float boneWeightHead = 50.0f;
    float boneWeightTorso = 50.0f;
    bool boneWeightRandomize = false;
    float boneWeightRandomAmt = 10.0f;
    float feather = 15.0f;
    bool featherRandomize = false;
    float featherRandomAmt = 5.0f;
    bool prediction = false;
    float bulletVelocity = 300.0f;
    bool closestBone = false;
    float closestBoneStrength = 0.5f;
    bool closestBoneStrengthRandomize = false;
    float closestBoneStrengthRandomAmt = 0.1f;
    float centerPull = 0.0f;
    bool centerPullRandomize = false;
    float centerPullRandomAmt = 0.1f;
    float smooth = 5.0f;

    float rt_magOff = 0; DWORD rt_magT = 0;
    float rt_bwOff = 0; DWORD rt_bwT = 0;
    float rt_featherOff = 0; DWORD rt_featherT = 0;
    float rt_cpOff = 0; DWORD rt_cpT = 0;
    float rt_cbsOff = 0; DWORD rt_cbsT = 0;
    int rt_currentBone = -1;
};

extern std::atomic<bool> g_aimbotEnabled;
extern std::atomic<bool> g_aimbotActive;
extern float g_aimbotFOV;
extern float g_aimbotMaxDist;
extern std::atomic<bool> g_aimbotDrawFOV;
extern std::atomic<bool> g_aimbotTargetPlayers;
extern std::atomic<bool> g_aimbotTargetMobs;
extern int g_aimbotActivationKey;
extern AimbotProfile g_aimPlayer;
extern AimbotProfile g_aimMob;
extern bool g_mobAimbotSame;

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------
void resolve_all(HMODULE ga, IL2CPP_API& api);
void set_il2cpp_api_ptr(IL2CPP_API* p);
bool install_hook(Hook& h, void* target, void* detour, int steal_count = 16);

extern HWBPHook g_hwbpHooks[4];
bool install_hwbp_hook(int drIndex, void* target, void* detour, int steal_count = 16);
bool hwbp_handle_exception(EXCEPTION_POINTERS* ep);
bool discover_component_indices(void* gameContextModule);
void scan_entities();
std::string read_il2cpp_string(void* str);
void* get_component(void* entity, int componentIndex);
bool strip_component(void* entity, int componentIndex);
void* find_method_address(IL2CPP_API& api, void* image, const char* ns, const char* className, const char* methodName, int paramCount);
bool key_pressed(int vk);
bool safe_read_ptr(void* addr, void** out);
bool safe_read_int(void* addr, int* out);
bool safe_read_bool(void* addr, bool* out);
bool safe_read_worldvec(void* addr, WorldVector* out);
bool safe_read_sizet(void* addr, size_t* out);
void force_interact_target(void* systemPtr, int targetId);
void dump_entities_to_file();
void probe_context_to_file();
void apply_turret_mods();
void apply_weapon_mods();
void apply_player_mods();
void apply_world_mods();
extern std::atomic<bool>  g_noFallDamage;
extern std::atomic<bool>  g_flyMode;
extern std::atomic<bool>  g_lowGravMode;
extern std::atomic<bool>  g_heavyFix2;
extern std::atomic<bool>  g_hooverRequest;
extern std::atomic<bool>  g_hardKillRequested;
extern std::atomic<int>   g_playerEntityId;
extern std::atomic<bool>  g_stripAntiCheat;
extern std::atomic<bool>  g_addNoClipIgnore;
extern std::atomic<bool>  g_stripSpeedCap;
extern int g_idx_anticheat;
extern int g_idx_anticheat_noclip_ignore;
extern int g_idx_anticheat_speedcap;
extern int g_idx_dont_destroy_in_storm;
extern int g_idx_extraction_point;
extern int g_idx_final_extraction;
extern int g_idx_extraction_box;
extern int g_idx_extraction_ship;
extern int g_idx_extraction_progress;
extern int g_idx_extraction_landing;
extern int g_idx_contract_info;
extern int g_idx_walker_engine;
extern int g_idx_reactor_data;
extern int g_idx_reactor_state;
extern int g_idx_reactor_turbo;
extern int g_idx_health_normalized;
extern int g_idx_in_eye_of_storm;
extern std::atomic<bool> g_espShowExtraction;
extern std::atomic<bool> g_espShowReactors;
extern std::atomic<bool> g_stormImmunity;
extern std::atomic<bool> g_shipResilience;
extern std::atomic<float> g_walkerSpeedMult;
extern std::atomic<bool>  g_autoRelockDupe;
extern std::string g_lastDupedName;
extern CRITICAL_SECTION g_lastDupedNameCS;
extern std::atomic<bool>  g_dupeSuspended;
void ensure_last_duped_name_cs();
void steam_names_init();
std::string get_steam_name(unsigned long long steamId);
extern float g_lootT1Color[4];
extern float g_lootT2Color[4];
extern float g_lootT3Color[4];
extern std::atomic<bool>  g_noJumpDelay;
extern std::atomic<bool>  g_infiniteAmmo;
extern std::atomic<float> g_jumpForceMult;
extern std::atomic<bool>  g_alwaysDay;
extern std::atomic<float> g_dayTime;
extern volatile uintptr_t g_todInstance;
extern std::atomic<float> g_speedMult;
extern std::atomic<bool>  g_walkerFly;
extern std::atomic<float> g_recoilMult;
extern int g_idx_fall_damage;
extern int g_idx_jump_delay;
extern int g_idx_ammo;
extern int g_idx_inv_ammo;
void __fastcall hooked_execute(void* thisPtr);
bool __fastcall hooked_is_too_far(void* thisPtr, int32_t targetId, void* avatar);
void dump_shop_classes(IL2CPP_API& api);

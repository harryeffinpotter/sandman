#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <unordered_map>

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
typedef void*       (*fn_il2cpp_class_get_methods)(void* klass, void** iter);
typedef const char* (*fn_il2cpp_method_get_name)(void* method);
typedef uint32_t    (*fn_il2cpp_method_get_param_count)(void* method);

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
    fn_il2cpp_class_get_methods         il2cpp_class_get_methods;
    fn_il2cpp_method_get_name           il2cpp_method_get_name;
    fn_il2cpp_method_get_param_count    il2cpp_method_get_param_count;
};

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------
struct WorldVector {
    float x, y, z;
    int cx, cy;
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
};

struct Hook {
    uint8_t original_bytes[32];
    uint8_t trampoline[48];
    void* trampoline_exec;
    void* target;
    void* detour;
    int stolen_bytes;
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

// ---------------------------------------------------------------------------
// Globals (defined in cheat.cpp)
// ---------------------------------------------------------------------------
extern volatile void* g_gameContextModule;
extern volatile void* g_findInteractSystem;
extern volatile bool g_hooked;

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

extern std::vector<ItemInfo> g_items;
extern CRITICAL_SECTION g_itemsLock;

extern std::atomic<bool> g_permaLockActive;
extern std::string g_permaLockName;
extern std::atomic<int> g_lockedEntityId;
extern std::atomic<bool> g_dupeMode;
extern std::atomic<bool> g_stickyLock;
extern std::atomic<bool> g_weaponFilter;
extern std::atomic<bool> g_heavyBypass;
extern std::atomic<uintptr_t> g_lockedEntityPtr;
extern std::atomic<bool> g_running;

extern WorldVector g_playerPos;
extern std::atomic<int> g_entityCount;

extern std::string g_nameFilter;
extern int g_scrollOffset;

extern Hook g_executeHook;
extern fn_execute g_original_execute;
extern Hook g_farHook;

extern std::atomic<bool> g_dumpEntities;
extern std::atomic<bool> g_probeContext;

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------
void resolve_all(HMODULE ga, IL2CPP_API& api);
bool install_hook(Hook& h, void* target, void* detour, int steal_count = 16);
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
void __fastcall hooked_execute(void* thisPtr);
bool __fastcall hooked_is_too_far(void* thisPtr, int32_t targetId, void* avatar);

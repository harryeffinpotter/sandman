#include "cheat.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <unordered_map>

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

std::vector<ItemInfo> g_items;
CRITICAL_SECTION g_itemsLock;

std::atomic<bool> g_permaLockActive{false};
std::string g_permaLockName;
std::atomic<int> g_lockedEntityId{-1};
std::atomic<bool> g_dupeMode{false};
std::atomic<bool> g_stickyLock{false};
std::atomic<bool> g_weaponFilter{false};
std::atomic<bool> g_heavyBypass{false};
std::atomic<uintptr_t> g_lockedEntityPtr{0};
std::atomic<bool> g_running{true};

WorldVector g_playerPos = {};
std::atomic<int> g_entityCount{0};

std::string g_nameFilter;
int g_scrollOffset = 0;

Hook g_executeHook;
fn_execute g_original_execute = nullptr;
Hook g_farHook;

std::atomic<bool> g_dumpEntities{false};
std::atomic<bool> g_probeContext{false};

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
    if (!str || IsBadReadPtr(str, 0x14)) return "";
    int len = *(int*)((uintptr_t)str + 0x10);
    if (len <= 0 || len > 1024) return "";
    wchar_t* wchars = (wchar_t*)((uintptr_t)str + 0x14);
    if (IsBadReadPtr(wchars, len * 2)) return "";
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) result += (char)wchars[i];
    return result;
}

// ---------------------------------------------------------------------------
// DictionarySlim lookup (internal)
// ---------------------------------------------------------------------------
static void* dict_slim_lookup(void* dict, int key) {
    if (!dict || IsBadReadPtr(dict, 0x28)) return nullptr;

    void* buckets_arr = *(void**)((uintptr_t)dict + 0x10);
    void* entries_arr = *(void**)((uintptr_t)dict + 0x18);
    if (!buckets_arr || !entries_arr) return nullptr;

    size_t bucket_count = *(size_t*)((uintptr_t)buckets_arr + 0x18);
    int* buckets = (int*)((uintptr_t)buckets_arr + 0x20);

    size_t entry_count = *(size_t*)((uintptr_t)entries_arr + 0x18);
    uint8_t* entries = (uint8_t*)((uintptr_t)entries_arr + 0x20);

    if (bucket_count == 0) return nullptr;
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
    if (!dict || IsBadReadPtr(dict, 0x28)) return false;

    void* buckets_arr = *(void**)((uintptr_t)dict + 0x10);
    void* entries_arr = *(void**)((uintptr_t)dict + 0x18);
    if (!buckets_arr || !entries_arr) return false;

    size_t bucket_count = *(size_t*)((uintptr_t)buckets_arr + 0x18);
    int* buckets = (int*)((uintptr_t)buckets_arr + 0x20);

    size_t entry_count = *(size_t*)((uintptr_t)entries_arr + 0x18);
    uint8_t* entries = (uint8_t*)((uintptr_t)entries_arr + 0x20);

    if (bucket_count == 0) return false;
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
        if (IsBadReadPtr(entity, 0x58)) return false;
        void* dict = *(void**)((uintptr_t)entity + 0x50);
        return dict_slim_null_value(dict, componentIndex);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------------------------------------------------------------------
// SEH-safe memory read helpers
// ---------------------------------------------------------------------------
bool safe_read_ptr(void* addr, void** out) {
    __try {
        *out = *(void**)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool safe_read_int(void* addr, int* out) {
    __try {
        *out = *(int*)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool safe_read_bool(void* addr, bool* out) {
    __try {
        *out = *(bool*)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool safe_read_worldvec(void* addr, WorldVector* out) {
    __try {
        *out = *(WorldVector*)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool safe_read_sizet(void* addr, size_t* out) {
    __try {
        *out = *(size_t*)addr;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------------------------------------------------------------------
// discover_component_indices
// ---------------------------------------------------------------------------
bool discover_component_indices(void* gameContextModule) {
    void* componentNames = *(void**)((uintptr_t)gameContextModule + 0x20);
    if (!componentNames || IsBadReadPtr(componentNames, 0x20)) return false;

    void* items_arr = *(void**)((uintptr_t)componentNames + 0x10);
    int size = *(int*)((uintptr_t)componentNames + 0x18);

    if (!items_arr || size <= 0 || size > 10000) return false;

    size_t arr_len = *(size_t*)((uintptr_t)items_arr + 0x18);
    void** elements = (void**)((uintptr_t)items_arr + 0x20);

    int count = (size < (int)arr_len) ? size : (int)arr_len;

    for (int i = 0; i < count; i++) {
        void* str = elements[i];
        if (!str || IsBadReadPtr(str, 0x14)) continue;
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
    }
    return true;
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
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------------------
// hooked_execute
// ---------------------------------------------------------------------------
void __fastcall hooked_execute(void* thisPtr) {
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

    ((fn_execute)g_executeHook.trampoline_exec)(thisPtr);

    if (!g_permaLockActive.load() || g_lockedEntityId.load() < 0) return;
    force_interact_target(thisPtr, g_lockedEntityId.load());
}

// ---------------------------------------------------------------------------
// hooked_is_too_far
// ---------------------------------------------------------------------------
bool __fastcall hooked_is_too_far(void* thisPtr, int32_t targetId, void* avatar) {
    if (g_permaLockActive.load()) return false;
    return ((fn_is_too_far)g_farHook.trampoline_exec)(thisPtr, targetId, avatar);
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

// ---------------------------------------------------------------------------
// scan_entities
// ---------------------------------------------------------------------------
void scan_entities() {
    void* gcm = (void*)g_gameContextModule;
    if (!gcm) return;

    {
        void* context = nullptr;
        if (!safe_read_ptr((void*)((uintptr_t)gcm + 0x10), &context)) return;
        if (!context || IsBadReadPtr(context, 0xA0)) return;

        void* cache = *(void**)((uintptr_t)context + 0x98);
        void** entityPtrs = nullptr;
        int entityCount = 0;

        static std::vector<void*> tempEntities;

        if (cache && !IsBadReadPtr(cache, 0x20)) {
            entityCount = (int)*(size_t*)((uintptr_t)cache + 0x18);
            entityPtrs = (void**)((uintptr_t)cache + 0x20);
        } else {
            void* hashSet = *(void**)((uintptr_t)context + 0x58);
            if (!hashSet || IsBadReadPtr(hashSet, 0x30)) return;

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
                if (!ent || IsBadReadPtr(ent, 0x68)) { dout("  %s: INVALID ptr %p\n", label, ent); return; }

                int cIdx = *(int*)((uintptr_t)ent + 0x48);
                bool enabled = *(bool*)((uintptr_t)ent + 0x4C);
                dout("\n[%s] ptr=%p, creationIndex=%d, isEnabled=%d\n", label, ent, cIdx, enabled?1:0);

                void* dictPtr = *(void**)((uintptr_t)ent + 0x50);
                dout("  dict ptr=%p\n", dictPtr);
                if (!dictPtr || IsBadReadPtr(dictPtr, 0x28)) { dout("  dict INVALID\n"); return; }

                void* bucketsArr = *(void**)((uintptr_t)dictPtr + 0x10);
                void* entriesArr = *(void**)((uintptr_t)dictPtr + 0x18);
                int dCount = *(int*)((uintptr_t)dictPtr + 0x20);
                int dFree = *(int*)((uintptr_t)dictPtr + 0x24);
                dout("  _buckets=%p, _entries=%p, count=%d, freeList=%d\n",
                     bucketsArr, entriesArr, dCount, dFree);

                if (bucketsArr && !IsBadReadPtr(bucketsArr, 0x20)) {
                    size_t bLen = *(size_t*)((uintptr_t)bucketsArr + 0x18);
                    dout("  buckets arr_len=%zu\n", bLen);
                    int* bData = (int*)((uintptr_t)bucketsArr + 0x20);
                    int bShow = (bLen < 32) ? (int)bLen : 32;
                    dout("  buckets values: ");
                    for (int b = 0; b < bShow; b++) dout("%d ", bData[b]);
                    dout("\n");
                }

                if (entriesArr && !IsBadReadPtr(entriesArr, 0x20)) {
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
                        if (bucketsArr && !IsBadReadPtr(bucketsArr, 0x20)) {
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
                if (!ent || IsBadReadPtr(ent, 0x68)) continue;
                nValid++;
                bool en = *(bool*)((uintptr_t)ent + 0x4C);
                if (!en) continue;
                nEnabled++;
                void* dp = *(void**)((uintptr_t)ent + 0x50);
                if (dp && !IsBadReadPtr(dp, 0x28)) nHasDict++;
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
                    if (!ent || IsBadReadPtr(ent, 0x68)) continue;
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
        if (sys) {
            void* group = *(void**)((uintptr_t)sys + 0x38);
            if (group) {
                void* buf = *(void**)((uintptr_t)sys + 0x40);
                if (buf) {
                    void* bi = *(void**)((uintptr_t)buf + 0x10);
                    int bs = *(int*)((uintptr_t)buf + 0x18);
                    if (bi && bs > 0) {
                        void* pe = *(void**)((uintptr_t)bi + 0x20);
                        if (pe) {
                            playerEntityId = *(int*)((uintptr_t)pe + 0x48);
                            void* posComp = get_component(pe, g_idx_position);
                            if (posComp) {
                                playerPos = *(WorldVector*)((uintptr_t)posComp + 0x10);
                                havePlayerPos = true;
                            }
                        }
                    }
                }
            }
        }

        if (havePlayerPos) g_playerPos = playerPos;

        std::unordered_map<int, void*> idToEntity;
        if (g_idx_id >= 0) {
            for (int e = 0; e < entityCount; e++) {
                void* ent = entityPtrs[e];
                if (!ent || IsBadReadPtr(ent, 0x58)) continue;
                if (!*(bool*)((uintptr_t)ent + 0x4C)) continue;
                void* idComp = get_component(ent, g_idx_id);
                if (idComp) {
                    int sid = *(int*)((uintptr_t)idComp + 0x10);
                    if (sid > 0) idToEntity[sid] = ent;
                }
            }
        }

        std::vector<ItemInfo> items;

        for (int e = 0; e < entityCount; e++) {
            void* entity = entityPtrs[e];
            if (!entity || IsBadReadPtr(entity, 0x68)) continue;

            bool isEnabled = *(bool*)((uintptr_t)entity + 0x4C);
            if (!isEnabled) continue;

            void* bpComp = get_component(entity, g_idx_blueprint);
            if (!bpComp) continue;

            void* nameStr = *(void**)((uintptr_t)bpComp + 0x10);
            std::string name = read_il2cpp_string(nameStr);
            if (name.empty()) continue;

            int eid = *(int*)((uintptr_t)entity + 0x48);
            if (eid == playerEntityId) continue;

            if (name.rfind("item_", 0) != 0) continue;
            if (name.rfind("item_containerBox", 0) == 0) continue;

            void* posComp = get_component(entity, g_idx_position);
            bool hasParent = (g_idx_parent >= 0 && get_component(entity, g_idx_parent));
            if (!posComp && !hasParent) continue;

            ItemInfo info;
            info.name = name;
            info.entityId = eid;
            info.serverId = -1;
            if (g_idx_id >= 0) {
                void* idComp = get_component(entity, g_idx_id);
                if (idComp) info.serverId = *(int*)((uintptr_t)idComp + 0x10);
            }
            info.entityPtr = entity;

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
                        void* parentEnt = it->second;
                        void* parentPos = get_component(parentEnt, g_idx_position);
                        if (parentPos) {
                            resolvedPos = *(WorldVector*)((uintptr_t)parentPos + 0x10);
                            hasResolvedPos = true;
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
            } else {
                info.distance = -1.0f;
            }

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

            items.push_back(std::move(info));
        }

        std::sort(items.begin(), items.end(), [](const ItemInfo& a, const ItemInfo& b) {
            if (a.distance < 0) return false;
            if (b.distance < 0) return true;
            return a.distance < b.distance;
        });

        EnterCriticalSection(&g_itemsLock);
        g_items = std::move(items);
        LeaveCriticalSection(&g_itemsLock);

        if (g_dupeMode.load() && !g_stickyLock.load()) {
            EnterCriticalSection(&g_itemsLock);
            if (!g_items.empty()) {
                auto& top = g_items[0];
                g_permaLockName = top.name;
                g_permaLockActive.store(true);
                int lockId = (top.serverId > 0) ? top.serverId : top.entityId;
                g_lockedEntityId.store(lockId);
                g_lockedEntityPtr.store((uintptr_t)top.entityPtr);
            }
            LeaveCriticalSection(&g_itemsLock);
        }

        if (g_permaLockActive.load() && !g_permaLockName.empty() && !g_dupeMode.load() && !g_stickyLock.load()) {
            EnterCriticalSection(&g_itemsLock);
            for (auto& it : g_items) {
                if (it.name == g_permaLockName) {
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

// ---------------------------------------------------------------------------
// dump_entities_to_file
// ---------------------------------------------------------------------------
void dump_entities_to_file() {
    void* gcm = (void*)g_gameContextModule;
    if (!gcm) { g_dumpEntities.store(false); return; }

    void* context = nullptr;
    if (!safe_read_ptr((void*)((uintptr_t)gcm + 0x10), &context)) { g_dumpEntities.store(false); return; }
    if (!context || IsBadReadPtr(context, 0xA0)) { g_dumpEntities.store(false); return; }

    void** entityPtrs = nullptr;
    int entityCount = 0;
    std::vector<void*> tempEnts;

    void* cache = *(void**)((uintptr_t)context + 0x98);
    if (cache && !IsBadReadPtr(cache, 0x20)) {
        entityCount = (int)*(size_t*)((uintptr_t)cache + 0x18);
        entityPtrs = (void**)((uintptr_t)cache + 0x20);
    } else {
        void* hashSet = *(void**)((uintptr_t)context + 0x58);
        if (!hashSet || IsBadReadPtr(hashSet, 0x30)) { g_dumpEntities.store(false); return; }
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
        if (!ent || IsBadReadPtr(ent, 0x68)) continue;
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

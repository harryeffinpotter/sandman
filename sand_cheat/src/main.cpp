#include "cheat.h"
#include "overlay.h"
#include <cstdio>

static DWORD WINAPI worker_thread(LPVOID) {
    InitializeCriticalSection(&g_itemsLock);

    HMODULE ga = nullptr;
    while (!ga && g_running.load()) {
        ga = GetModuleHandleA("GameAssembly.dll");
        if (!ga) Sleep(500);
    }
    if (!ga) return 0;

    IL2CPP_API api;
    resolve_all(ga, api);

    if (api.il2cpp_domain_get && api.il2cpp_thread_attach) {
        void* domain = api.il2cpp_domain_get();
        if (domain) api.il2cpp_thread_attach(domain);
    }

    uintptr_t ga_base = (uintptr_t)ga;
    void* executeAddr = (void*)(ga_base + 0x4BBDA10);

    if (!install_hook(g_executeHook, executeAddr, (void*)hooked_execute)) {
        // Hook failed
    } else {
        g_hooked = true;
    }

    while (!g_gameContextModule && g_running.load()) {
        Sleep(100);
    }
    if (!g_running.load()) goto cleanup;

    discover_component_indices((void*)g_gameContextModule);

    if (!overlay_init()) {
        g_running.store(false);
        goto cleanup;
    }

    {
        int frame = 0;
        while (g_running.load()) {
            if (frame % 5 == 0) scan_entities();

            if (g_dumpEntities.load()) dump_entities_to_file();
            if (g_probeContext.load()) probe_context_to_file();

            overlay_render();
            Sleep(16);
            frame++;
        }
    }

cleanup:
    overlay_shutdown();

    if (g_hooked) {
        DWORD old;
        VirtualProtect(g_executeHook.target, g_executeHook.stolen_bytes, PAGE_EXECUTE_READWRITE, &old);
        memcpy(g_executeHook.target, g_executeHook.original_bytes, g_executeHook.stolen_bytes);
        VirtualProtect(g_executeHook.target, g_executeHook.stolen_bytes, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_executeHook.target, g_executeHook.stolen_bytes);
    }

    if (g_executeHook.trampoline_exec)
        VirtualFree(g_executeHook.trampoline_exec, 0, MEM_RELEASE);

    DeleteCriticalSection(&g_itemsLock);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    }
    return TRUE;
}

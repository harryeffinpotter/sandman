#include "cheat.h"
#include "overlay.h"
#include <cstdio>
#include <cstdarg>
#include <csignal>
#include <exception>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>

#pragma comment(lib, "dbghelp.lib")

#ifndef WT_EXECUTELONGFUNCTION
#define WT_EXECUTELONGFUNCTION 0x00000010
#endif

#define CRASH_DIR "C:\\Users\\ysg\\projects\\sand_cheat\\"

static void wlog(const char* fmt, ...) {
    FILE* f = fopen(CRASH_DIR "worker_debug.txt", "a");
    if (!f) return;
    fprintf(f, "[%lu] ", GetTickCount());
    va_list a; va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fflush(f); fclose(f);
}

static void tlog(const char* fmt, ...) {
    FILE* f = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\injection_trace.txt", "a");
    if (!f) return;
    fprintf(f, "[%lu] ", GetTickCount());
    va_list a; va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fflush(f); fclose(f);
}

volatile DWORD g_workerThreadId = 0;
volatile DWORD g_renderThreadId = 0;
static volatile int g_exceptionCount = 0;
volatile bool g_workerVehActive = false;
static volatile LONG g_dumpWritten = 0;
CONTEXT g_vehSavedCtx;
volatile bool g_vehCrashRecovered = false;
CONTEXT g_vehInnerCtx;
volatile bool g_vehInnerActive = false;
CONTEXT g_vehEntityCtx;
volatile bool g_vehEntityActive = false;

static const char* exception_code_name(DWORD code) {
    switch (code) {
        case 0xC0000005: return "ACCESS_VIOLATION";
        case 0xC0000094: return "INT_DIVIDE_BY_ZERO";
        case 0xC00000FD: return "STACK_OVERFLOW";
        case 0xC0000096: return "PRIVILEGED_INSTRUCTION";
        case 0xC000001D: return "ILLEGAL_INSTRUCTION";
        case 0x80000003: return "BREAKPOINT";
        case 0x80000004: return "SINGLE_STEP";
        case 0xC0000008: return "INVALID_HANDLE";
        case 0xC0000017: return "NO_MEMORY";
        case 0xC0000409: return "STACK_BUFFER_OVERRUN";
        case 0xe06d7363: return "CPP_EXCEPTION";
        case 0x406D1388: return "THREAD_NAME_SET";
        default: return "UNKNOWN";
    }
}

static void resolve_addr(FILE* f, uintptr_t addr) {
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)addr, &mod) && mod) {
        char mn[MAX_PATH] = {};
        GetModuleFileNameA(mod, mn, MAX_PATH);
        char* sl = strrchr(mn, '\\');
        fprintf(f, "%s+0x%llX", sl ? sl + 1 : mn, (unsigned long long)(addr - (uintptr_t)mod));
    } else {
        fprintf(f, "0x%p", (void*)addr);
    }
}

static void write_full_crash(FILE* f, EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    DWORD tid = GetCurrentThreadId();

    fprintf(f, "========================================\n");
    fprintf(f, "  SAND CHEAT CRASH REPORT\n");
    fprintf(f, "========================================\n\n");

    fprintf(f, "Exception: 0x%08lX (%s)\n", code, exception_code_name(code));
    fprintf(f, "Address:   %p -> ", ep->ExceptionRecord->ExceptionAddress);
    resolve_addr(f, (uintptr_t)ep->ExceptionRecord->ExceptionAddress);
    fprintf(f, "\n");
    fprintf(f, "Thread:    %lu", tid);
    if (tid == g_workerThreadId) fprintf(f, " [WORKER - ours]");
    else if (tid == g_renderThreadId) fprintf(f, " [RENDER - ours]");
    else fprintf(f, " [GAME thread - not ours]");
    fprintf(f, "\n");

    if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
        const char* rw = ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" :
                         ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE" : "EXECUTE";
        fprintf(f, "Access:    %s at address %p\n", rw,
            (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    fprintf(f, "Exceptions seen before this: %d\n", g_exceptionCount);

    fprintf(f, "\n--- REGISTERS ---\n");
    CONTEXT* c = ep->ContextRecord;
    fprintf(f, "RAX=%016llX  RBX=%016llX  RCX=%016llX  RDX=%016llX\n", c->Rax, c->Rbx, c->Rcx, c->Rdx);
    fprintf(f, "RSI=%016llX  RDI=%016llX  RBP=%016llX  RSP=%016llX\n", c->Rsi, c->Rdi, c->Rbp, c->Rsp);
    fprintf(f, "R8 =%016llX  R9 =%016llX  R10=%016llX  R11=%016llX\n", c->R8, c->R9, c->R10, c->R11);
    fprintf(f, "R12=%016llX  R13=%016llX  R14=%016llX  R15=%016llX\n", c->R12, c->R13, c->R14, c->R15);
    fprintf(f, "RIP=%016llX  EFLAGS=%08lX\n", c->Rip, c->EFlags);

    fprintf(f, "\n--- STACK WALK (StackWalk64) ---\n");
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymInitialize(process, NULL, TRUE);
    STACKFRAME64 sf = {};
    sf.AddrPC.Offset = c->Rip;     sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = c->Rbp;  sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = c->Rsp;  sf.AddrStack.Mode = AddrModeFlat;
    CONTEXT ctxCopy = *c;
    for (int i = 0; i < 64; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &sf, &ctxCopy, NULL,
            SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
        if (sf.AddrPC.Offset == 0) break;
        fprintf(f, "  [%2d] %p  ", i, (void*)sf.AddrPC.Offset);
        resolve_addr(f, sf.AddrPC.Offset);
        char symBuf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, sf.AddrPC.Offset, &displacement, sym)) {
            fprintf(f, "  (%s+0x%llX)", sym->Name, (unsigned long long)displacement);
        }
        fprintf(f, "\n");
    }

    fprintf(f, "\n--- RAW STACK (128 qwords from RSP) ---\n");
    uintptr_t* sp = (uintptr_t*)c->Rsp;
    for (int i = 0; i < 128; i++) {
        uintptr_t val = 0;
        __try { val = sp[i]; } __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (!val) continue;
        HMODULE sm = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)val, &sm) && sm) {
            char sn[MAX_PATH] = {};
            GetModuleFileNameA(sm, sn, MAX_PATH);
            char* s = strrchr(sn, '\\');
            fprintf(f, "  [RSP+0x%03X] %p  %s+0x%llX\n", i * 8, (void*)val,
                s ? s + 1 : sn, (unsigned long long)(val - (uintptr_t)sm));
        }
    }

    fprintf(f, "\n--- LOADED MODULES ---\n");
    HMODULE mods[512];
    DWORD needed = 0;
    if (EnumProcessModules(process, mods, sizeof(mods), &needed)) {
        int count = needed / sizeof(HMODULE);
        for (int i = 0; i < count; i++) {
            char mn[MAX_PATH] = {};
            GetModuleFileNameA(mods[i], mn, MAX_PATH);
            MODULEINFO mi = {};
            GetModuleInformation(process, mods[i], &mi, sizeof(mi));
            char* sl = strrchr(mn, '\\');
            fprintf(f, "  %p  size=0x%08lX  %s\n", mods[i], (unsigned long)mi.SizeOfImage, sl ? sl + 1 : mn);
        }
    }

    fprintf(f, "\n--- CHEAT STATE ---\n");
    fprintf(f, "g_menuVisible      = %d\n", g_menuVisible ? 1 : 0);
    fprintf(f, "g_running          = %d\n", g_running.load() ? 1 : 0);
    fprintf(f, "g_streamProof      = %d\n", g_streamProof.load() ? 1 : 0);
    fprintf(f, "g_esp3DEnabled     = %d\n", g_esp3DEnabled.load() ? 1 : 0);
    fprintf(f, "g_aimbotEnabled    = %d\n", g_aimbotEnabled.load() ? 1 : 0);
    fprintf(f, "g_turretRapidFire  = %d\n", g_turretRapidFire.load() ? 1 : 0);
    fprintf(f, "g_turretNoRecoil   = %d\n", g_turretNoRecoil.load() ? 1 : 0);
    fprintf(f, "g_weaponModsEnabled= %d\n", g_weaponModsEnabled.load() ? 1 : 0);
    fprintf(f, "g_heavyBypass      = %d\n", g_heavyBypass.load() ? 1 : 0);
    fprintf(f, "g_permaLockActive  = %d\n", g_permaLockActive.load() ? 1 : 0);
    fprintf(f, "g_executeHookCalls = %d\n", g_executeHookCalls.load());
    fprintf(f, "g_entityCount      = %d\n", g_entityCount.load());
    fprintf(f, "g_findInteractSys  = %p\n", (void*)g_findInteractSystem);
    fprintf(f, "g_gameContextModule= %p\n", (void*)g_gameContextModule);
    fprintf(f, "g_workerThreadId   = %lu\n", g_workerThreadId);
    fprintf(f, "g_renderThreadId   = %lu\n", g_renderThreadId);
    HMODULE ga = GetModuleHandleA("GameAssembly.dll");
    HMODULE sc = GetModuleHandleA("sand_cheat.dll");
    HMODULE dxgi = GetModuleHandleA("dxgi.dll");
    fprintf(f, "GameAssembly.dll   = %p\n", ga);
    fprintf(f, "sand_cheat.dll     = %p\n", sc);
    fprintf(f, "dxgi.dll           = %p\n", dxgi);

    fprintf(f, "\n========================================\n");
    fprintf(f, "  END CRASH REPORT\n");
    fprintf(f, "========================================\n");
    fflush(f);
}

static void write_minidump(EXCEPTION_POINTERS* ep) {
    HANDLE hFile = CreateFileA(CRASH_DIR "sand_crash.dmp", GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
        (MINIDUMP_TYPE)(MiniDumpWithFullMemoryInfo | MiniDumpWithThreadInfo |
                        MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
        &mei, NULL, NULL);
    CloseHandle(hFile);
}

static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    if (hwbp_handle_exception(ep))
        return EXCEPTION_CONTINUE_EXECUTION;

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388) return EXCEPTION_CONTINUE_SEARCH;

    g_exceptionCount++;

    if (code == 0xe06d7363) return EXCEPTION_CONTINUE_SEARCH;
    if (code == 0x40010006) return EXCEPTION_CONTINUE_SEARCH;
    if (code == 0x80000003) return EXCEPTION_CONTINUE_SEARCH;
    if (code == 0x80000004) return EXCEPTION_CONTINUE_SEARCH;

    if (code == 0xC0000005 && GetCurrentThreadId() == g_workerThreadId && g_workerVehActive) {
        g_workerVehActive = false;
        g_vehCrashRecovered = true;
        if (g_vehInnerActive) {
            g_vehInnerActive = false;
            *ep->ContextRecord = g_vehInnerCtx;
        } else if (g_vehEntityActive) {
            g_vehEntityActive = false;
            *ep->ContextRecord = g_vehEntityCtx;
        } else {
            *ep->ContextRecord = g_vehSavedCtx;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (code == 0xC0000005 && GetCurrentThreadId() == g_renderThreadId && g_renderThreadId != 0) {
        g_overlayDisabled = true;
        FILE* rf = fopen(CRASH_DIR "crash_info.txt", "a");
        if (rf) {
            fprintf(rf, "\n!!! RENDER THREAD AV — overlay disabled !!!\n\n");
            write_full_crash(rf, ep);
            fclose(rf);
        }
    }

    FILE* f = fopen(CRASH_DIR "crash_info.txt", "a");
    if (f) {
        write_full_crash(f, ep);
        fclose(f);
    }

    if (code == 0xC0000005 || code == 0xC00000FD || code == 0xC0000096 ||
        code == 0xC000001D || code == 0xC0000094) {
        if (InterlockedCompareExchange(&g_dumpWritten, 1, 0) == 0) {
            write_minidump(ep);
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI final_crash_handler(EXCEPTION_POINTERS* ep) {
    if (InterlockedCompareExchange(&g_dumpWritten, 1, 0) == 0) {
        FILE* f = fopen(CRASH_DIR "crash_info.txt", "a");
        if (f) {
            fprintf(f, "\n\n!!! UNHANDLED EXCEPTION - PROCESS DYING !!!\n\n");
            write_full_crash(f, ep);
            fclose(f);
        }
        write_minidump(ep);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void on_terminate() {
    FILE* f = fopen(CRASH_DIR "crash_info.txt", "a");
    if (f) {
        fprintf(f, "\n!!! std::terminate called on thread %lu !!!\n", GetCurrentThreadId());
        fflush(f); fclose(f);
    }
    _exit(1);
}

static void on_abort_signal(int) {
    FILE* f = fopen(CRASH_DIR "crash_info.txt", "a");
    if (f) {
        fprintf(f, "\n!!! SIGABRT on thread %lu !!!\n", GetCurrentThreadId());
        fflush(f); fclose(f);
    }
    _exit(1);
}

static void safe_scan_tick(int scanCounter) {
    static DWORD s_cooldownUntil = 0;
    DWORD now = GetTickCount();
    if (now < s_cooldownUntil) return;


    __try {
        scan_entities();
        if (scanCounter % 5 == 0) {
            if (g_turretRapidFire.load() || g_turretNoRecoil.load()) apply_turret_mods();
            if (g_weaponModsEnabled.load()) apply_weapon_mods();
        }
        if (g_dumpEntities.load()) dump_entities_to_file();
        if (g_probeContext.load()) probe_context_to_file();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[worker] SEH exception in scan tick %d: 0x%08lX — backing off 3s\n", scanCounter, GetExceptionCode());
        g_entityCount.store(0);
        s_cooldownUntil = GetTickCount() + 3000;
    }
}

static bool is_readable(const void* ptr, size_t len) {
    if (!ptr) return false;
    return !IsBadReadPtr(ptr, len);
}

static void* safe_find_execute(const IL2CPP_API* api, uintptr_t ga_base) {
    if (!api->il2cpp_domain_get || !api->il2cpp_domain_get_assemblies ||
        !api->il2cpp_image_get_class_count || !api->il2cpp_image_get_class ||
        !api->il2cpp_class_get_name || !api->il2cpp_class_get_methods ||
        !api->il2cpp_method_get_name || !api->il2cpp_method_get_param_count) {
        return nullptr;
    }
    __try {
        void* execKlass = nullptr;
        void* executeAddr = nullptr;
        size_t asmCount = 0;
        tlog("resolve_execute: domain_get\n");
        void* dom = api->il2cpp_domain_get();
        void** assemblies = api->il2cpp_domain_get_assemblies(dom, &asmCount);
        tlog("resolve_execute: %zu assemblies\n", asmCount);
        for (size_t i = 0; i < asmCount && !execKlass; i++) {
            void* img = api->il2cpp_assembly_get_image(assemblies[i]);
            size_t classCount = api->il2cpp_image_get_class_count(img);
            for (size_t j = 0; j < classCount; j++) {
                void* klass = api->il2cpp_image_get_class(img, j);
                const char* cn = api->il2cpp_class_get_name(klass);
                if (cn && strcmp(cn, "FindInteractTargetSystem") == 0) {
                    execKlass = klass;
                    tlog("resolve_execute: found FindInteractTargetSystem\n");
                    break;
                }
            }
        }
        if (execKlass) {
            void* iter = nullptr;
            void* method;
            while ((method = api->il2cpp_class_get_methods(execKlass, &iter)) != nullptr) {
                const char* mn = api->il2cpp_method_get_name(method);
                if (mn && strcmp(mn, "Execute") == 0 &&
                    api->il2cpp_method_get_param_count(method) == 0) {
                    executeAddr = *(void**)method;
                    tlog("resolve_execute: Execute at %p\n", executeAddr);
                    break;
                }
            }
        }
        return executeAddr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[worker] Execute resolution crashed: 0x%08lX\n", GetExceptionCode());
        tlog("resolve_execute CRASHED: 0x%08lX\n", GetExceptionCode());
        return nullptr;
    }
}

static bool safe_overlay_init() {
    __try {
        tlog("safe_overlay_init: entering overlay_init()\n");
        bool result = overlay_init();
        tlog("safe_overlay_init: overlay_init returned %d\n", result);
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[worker] overlay_init crashed: 0x%08lX\n", GetExceptionCode());
        tlog("overlay_init CRASHED: 0x%08lX\n", GetExceptionCode());
        return false;
    }
}

static DWORD WINAPI worker_thread(LPVOID) {
    g_workerThreadId = GetCurrentThreadId();
    tlog("worker_thread ENTERED tid=%lu pid=%lu\n", g_workerThreadId, GetCurrentProcessId());
    tlog("worker thread start addr check: NtCurrentTeb=%p\n", NtCurrentTeb());

    if (GetFileAttributesA(CRASH_DIR "debug_wait.txt") != INVALID_FILE_ATTRIBUTES) {
        tlog("debug_wait.txt found — spinning until debugger attaches...\n");
        while (!IsDebuggerPresent()) Sleep(100);
        tlog("debugger attached! breaking...\n");
        __debugbreak();
    }

    SetUnhandledExceptionFilter(final_crash_handler);
    std::set_terminate(on_terminate);
    signal(SIGABRT, on_abort_signal);
    tlog("exception handlers installed\n");
    AddVectoredExceptionHandler(1, crash_handler);
    tlog("VEH crash_handler installed\n");
    {
        FILE* f = fopen(CRASH_DIR "crash_info.txt", "w");
        if (f) { fprintf(f, "=== Session started ===\n\n"); fflush(f); fclose(f); }
    }
    tlog("crash_info.txt session file written\n");
    wlog("[worker] PID=%lu\n", GetCurrentProcessId());
    tlog("initializing critical section\n");
    InitializeCriticalSection(&g_itemsLock);
    tlog("critical section initialized\n");

    tlog("waiting for GameAssembly.dll...\n");
    HMODULE ga = nullptr;
    while (!ga && g_running.load()) {
        ga = GetModuleHandleA("GameAssembly.dll");
        if (!ga) Sleep(500);
    }
    if (!ga) return 0;
    tlog("GameAssembly.dll found at %p\n", ga);

    IL2CPP_API api;
    tlog("calling resolve_all...\n");
    resolve_all(ga, api);
    tlog("resolve_all done. domain_get=%p thread_attach=%p domain_get_assemblies=%p assembly_get_image=%p image_get_class_count=%p image_get_class=%p class_get_name=%p class_from_name=%p class_get_methods=%p method_get_name=%p method_get_param_count=%p image_get_name=%p class_get_type=%p type_get_object=%p string_new=%p\n",
        (void*)api.il2cpp_domain_get, (void*)api.il2cpp_thread_attach,
        (void*)api.il2cpp_domain_get_assemblies, (void*)api.il2cpp_assembly_get_image,
        (void*)api.il2cpp_image_get_class_count, (void*)api.il2cpp_image_get_class,
        (void*)api.il2cpp_class_get_name, (void*)api.il2cpp_class_from_name,
        (void*)api.il2cpp_class_get_methods, (void*)api.il2cpp_method_get_name,
        (void*)api.il2cpp_method_get_param_count, (void*)api.il2cpp_image_get_name,
        (void*)api.il2cpp_class_get_type, (void*)api.il2cpp_type_get_object,
        (void*)api.il2cpp_string_new);

    tlog("checking API validity...\n");
    int api_null_count = 0;
    if (!api.il2cpp_domain_get) api_null_count++;
    if (!api.il2cpp_thread_attach) api_null_count++;
    if (!api.il2cpp_domain_get_assemblies) api_null_count++;
    if (!api.il2cpp_assembly_get_image) api_null_count++;
    if (!api.il2cpp_image_get_class_count) api_null_count++;
    if (!api.il2cpp_image_get_class) api_null_count++;
    if (!api.il2cpp_class_get_name) api_null_count++;
    if (!api.il2cpp_class_get_methods) api_null_count++;
    if (!api.il2cpp_method_get_name) api_null_count++;
    tlog("API check: %d null pointers out of 9 critical\n", api_null_count);

    tlog("calling il2cpp_domain_get at %p\n", (void*)api.il2cpp_domain_get);
    if (api.il2cpp_domain_get && api.il2cpp_thread_attach) {
        void* domain = api.il2cpp_domain_get();
        tlog("domain=%p, calling thread_attach at %p\n", domain, (void*)api.il2cpp_thread_attach);
        if (domain) api.il2cpp_thread_attach(domain);
    }
    tlog("il2cpp thread attached\n");

    uintptr_t ga_base = (uintptr_t)ga;
    g_gaBase = ga_base;
    MODULEINFO gaInfo = {};
    GetModuleInformation(GetCurrentProcess(), ga, &gaInfo, sizeof(gaInfo));
    g_gaSize = gaInfo.SizeOfImage;
    tlog("GA base=%p size=0x%llX\n", (void*)ga_base, (unsigned long long)g_gaSize);
    tlog("calling safe_find_execute...\n");
    void* executeAddr = nullptr;
    for (int attempt = 0; attempt < 30 && g_running.load(); attempt++) {
        executeAddr = safe_find_execute(&api, ga_base);
        if (executeAddr) break;
        tlog("safe_find_execute attempt %d: not found, retrying in 1s\n", attempt);
        wlog("[worker] Execute not found (attempt %d), IL2CPP may not be ready — retrying in 1s\n", attempt);
        Sleep(1000);
    }
    tlog("safe_find_execute result: %p\n", executeAddr);

    if (!executeAddr) {
        wlog("[worker] Execute method not found dynamically, falling back to hardcoded RVA\n");
        executeAddr = (void*)(ga_base + 0x4BCD440);
    } else {
        wlog("[worker] Execute method found at %p\n", executeAddr);
    }

    tlog("=== CHECKPOINT: pre-overlay tick=%lu ===\n", GetTickCount());
    tlog("calling overlay_init...\n");
    wlog("[worker] calling overlay_init\n");
    {
        bool overlayOk = false;
        for (int attempt = 0; attempt < 20 && g_running.load(); attempt++) {
            tlog("overlay_init attempt %d, dxgi.dll=%p\n", attempt, GetModuleHandleA("dxgi.dll"));
            if (safe_overlay_init()) { overlayOk = true; break; }
            wlog("[worker] overlay_init attempt %d failed, retrying in 500ms\n", attempt);
            Sleep(500);
        }
        if (!overlayOk) {
            wlog("[worker] overlay_init FAILED after retries\n");
            g_running.store(false);
            goto cleanup;
        }
    }
    wlog("[worker] overlay_init OK\n");
    tlog("overlay_init SUCCEEDED\n");
    tlog("=== CHECKPOINT: post-overlay tick=%lu ===\n", GetTickCount());

    tlog("installing HWBP hook...\n");
    wlog("[worker] installing HWBP execute hook at %p\n", executeAddr);
    if (!install_hwbp_hook(0, executeAddr, (void*)hooked_execute, 16)) {
        wlog("[worker] HWBP execute hook FAILED\n");
        tlog("HWBP hook FAILED\n");
    } else {
        g_hooked = true;
        wlog("[worker] HWBP execute hook OK on DR0\n");
        tlog("HWBP hook installed\n");
    }
    tlog("=== CHECKPOINT: hooks-installed tick=%lu ===\n", GetTickCount());

    wlog("[worker] waiting for game context\n");

    while (!g_gameContextModule && g_running.load()) {
        Sleep(100);
    }
    if (!g_running.load()) goto cleanup;
    wlog("[worker] game context found: %p\n", (void*)g_gameContextModule);

    discover_component_indices((void*)g_gameContextModule);
    wlog("[worker] components discovered, entering main loop\n");

    {
        void* coreImage = nullptr;
        if (api.il2cpp_domain_get && api.il2cpp_domain_get_assemblies && api.il2cpp_assembly_get_image && api.il2cpp_image_get_name) {
            size_t asmCount = 0;
            void* dom = api.il2cpp_domain_get();
            void** assemblies = api.il2cpp_domain_get_assemblies(dom, &asmCount);
            for (size_t i = 0; i < asmCount; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies[i]);
                const char* imgName = api.il2cpp_image_get_name(img);
                if (imgName && strstr(imgName, "UnityEngine.CoreModule")) {
                    coreImage = img;
                    break;
                }
            }
        }
        if (coreImage) {
            g_cameraGetMain = (fn_camera_get_main)find_method_address(api, coreImage, "UnityEngine", "Camera", "get_main", 0);
            g_cameraW2S = (fn_camera_w2s)find_method_address(api, coreImage, "UnityEngine", "Camera", "WorldToScreenPoint", 1);
            g_getTransform = (fn_get_transform)find_method_address(api, coreImage, "UnityEngine", "Component", "get_transform", 0);
            g_getForward = (fn_get_forward)find_method_address(api, coreImage, "UnityEngine", "Transform", "get_forward", 0);
            g_getPosition = (fn_get_position)find_method_address(api, coreImage, "UnityEngine", "Transform", "get_position", 0);
            g_getParent = (fn_get_parent)find_method_address(api, coreImage, "UnityEngine", "Transform", "GetParent", 0);
            wlog("[worker] Camera: get_main=%p W2S=%p transform=%p forward=%p position=%p parent=%p\n", (void*)g_cameraGetMain, (void*)g_cameraW2S, (void*)g_getTransform, (void*)g_getForward, (void*)g_getPosition, (void*)g_getParent);
        } else {
            wlog("[worker] UnityEngine.CoreModule not found\n");
        }
        void* animImage = nullptr;
        if (coreImage && api.il2cpp_domain_get && api.il2cpp_domain_get_assemblies) {
            size_t asmCount2 = 0;
            void* dom2 = api.il2cpp_domain_get();
            void** assemblies2 = api.il2cpp_domain_get_assemblies(dom2, &asmCount2);
            for (size_t i = 0; i < asmCount2; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies2[i]);
                const char* imgName = api.il2cpp_image_get_name(img);
                if (imgName && strstr(imgName, "AnimationModule")) {
                    animImage = img;
                    break;
                }
            }
        }
        if (animImage) {
            g_getBoneTransform = (fn_get_bone_transform)find_method_address(api, animImage, "UnityEngine", "Animator", "GetBoneTransform", 1);
            wlog("[worker] Animator.GetBoneTransform=%p\n", (void*)g_getBoneTransform);
        } else {
            wlog("[worker] AnimationModule not found\n");
        }
        if (coreImage) {
            g_getComponentByType = (fn_get_component_by_type)find_method_address(api, coreImage, "UnityEngine", "Component", "GetComponent", 1);
            wlog("[worker] Component.GetComponent(Type)=%p\n", (void*)g_getComponentByType);
            g_getComponentInChildren = (fn_get_component_in_children)find_method_address(api, coreImage, "UnityEngine", "Component", "GetComponentInChildren", 1);
            wlog("[worker] Component.GetComponentInChildren=%p\n", (void*)g_getComponentInChildren);
        }
        if (animImage && api.il2cpp_class_from_name && api.il2cpp_class_get_type && api.il2cpp_type_get_object) {
            void* animClass = api.il2cpp_class_from_name(animImage, "UnityEngine", "Animator");
            if (animClass) {
                void* il2cppType = api.il2cpp_class_get_type(animClass);
                if (il2cppType) {
                    g_animatorType = api.il2cpp_type_get_object(il2cppType);
                }
            }
            wlog("[worker] Animator Type object=%p\n", g_animatorType);
        }
        if (api.il2cpp_domain_get && api.il2cpp_image_get_class_count && api.il2cpp_image_get_class && api.il2cpp_class_get_name) {
            size_t asmCount3 = 0;
            void* dom3 = api.il2cpp_domain_get();
            void** assemblies3 = api.il2cpp_domain_get_assemblies(dom3, &asmCount3);
            for (size_t i = 0; i < asmCount3 && !g_userNameKlass; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies3[i]);
                size_t classCount = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < classCount; j++) {
                    void* klass = api.il2cpp_image_get_class(img, j);
                    const char* cn = api.il2cpp_class_get_name(klass);
                    if (cn && strcmp(cn, "UserNameComponent") == 0) {
                        g_userNameKlass = klass;
                        break;
                    }
                }
            }
            wlog("[worker] UserNameComponent klass=%p\n", g_userNameKlass);
        }
        if (g_userNameKlass && api.il2cpp_class_get_type && api.il2cpp_type_get_object) {
            void* userNameIl2cppType = api.il2cpp_class_get_type(g_userNameKlass);
            if (userNameIl2cppType) {
                g_userNameType = api.il2cpp_type_get_object(userNameIl2cppType);
            }
            wlog("[worker] UserNameComponent Type object=%p\n", g_userNameType);
        }
        if (api.il2cpp_domain_get && api.il2cpp_domain_get_assemblies && api.il2cpp_image_get_class_count && api.il2cpp_image_get_class && api.il2cpp_class_get_name && api.il2cpp_class_get_methods && api.il2cpp_method_get_name && api.il2cpp_method_get_param_count) {
            void* fitKlass = nullptr;
            size_t asmCount4 = 0;
            void* dom4 = api.il2cpp_domain_get();
            void** assemblies4 = api.il2cpp_domain_get_assemblies(dom4, &asmCount4);
            for (size_t i = 0; i < asmCount4 && !fitKlass; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies4[i]);
                size_t classCount = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < classCount; j++) {
                    void* klass = api.il2cpp_image_get_class(img, j);
                    const char* cn = api.il2cpp_class_get_name(klass);
                    if (cn && strcmp(cn, "FindInteractTargetSystem") == 0) {
                        fitKlass = klass;
                        break;
                    }
                }
            }
            if (fitKlass) {
                void* iter = nullptr;
                void* method;
                void* isTooFarAddr = nullptr;
                while ((method = api.il2cpp_class_get_methods(fitKlass, &iter)) != nullptr) {
                    const char* mn = api.il2cpp_method_get_name(method);
                    if (mn && strcmp(mn, "IsTooFarAway") == 0 &&
                        api.il2cpp_method_get_param_count(method) == 2) {
                        isTooFarAddr = *(void**)method;
                        break;
                    }
                }
                if (isTooFarAddr) {
                    if (install_hook(g_farHook, isTooFarAddr, (void*)hooked_is_too_far)) {
                        wlog("[worker] IsTooFarAway hook installed at %p\n", isTooFarAddr);
                    } else {
                        wlog("[worker] IsTooFarAway hook FAILED at %p\n", isTooFarAddr);
                    }
                } else {
                    wlog("[worker] IsTooFarAway method not found, dumping all methods on FindInteractTargetSystem:\n");
                    void* iter2 = nullptr;
                    void* m2;
                    while ((m2 = api.il2cpp_class_get_methods(fitKlass, &iter2)) != nullptr) {
                        const char* mn2 = api.il2cpp_method_get_name(m2);
                        int pc = api.il2cpp_method_get_param_count(m2);
                        void* addr = *(void**)m2;
                        wlog("[worker]   method: %s (params=%d) at %p\n", mn2 ? mn2 : "(null)", pc, addr);
                    }
                }
            } else {
                wlog("[worker] FindInteractTargetSystem class not found\n");
            }
        }
    }

    tlog("entering main scan loop\n");
    tlog("=== ENTERING MAIN LOOP tick=%lu ===\n", GetTickCount());
    {
        int scanCounter = 0;
        while (g_running.load()) {
            static int heartbeat = 0;
            if (++heartbeat % 10 == 0) {
                tlog("heartbeat #%d tick=%lu\n", heartbeat, GetTickCount());
            }
            void* gcm = (void*)g_gameContextModule;
            if (!is_readable(gcm, 0x18)) {
                g_entityCount.store(0);
                Sleep(500);
                continue;
            }

            RtlCaptureContext(&g_vehSavedCtx);
            if (g_vehCrashRecovered) {
                g_vehCrashRecovered = false;
                wlog("[worker] VEH recovered from AV — scan tick %d skipped, backing off 3s\n", scanCounter);
                g_entityCount.store(0);
                scanCounter++;
                Sleep(3000);
                continue;
            }

            g_workerVehActive = true;
            if (scanCounter % 5 == 0) {
                safe_scan_tick(scanCounter);
            }
            if (g_dumpShopClasses.load()) { dump_shop_classes(api); g_dumpShopClasses.store(false); }
            g_workerVehActive = false;


            scanCounter++;
            Sleep(100);
        }
    }

cleanup:
    overlay_shutdown();

    if (g_hooked) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
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
                        ctx.Dr0 = 0;
                        ctx.Dr7 &= ~3ULL;
                        SetThreadContext(ht, &ctx);
                        ResumeThread(ht);
                        CloseHandle(ht);
                    }
                } while (Thread32Next(snap, &te));
            }
            CloseHandle(snap);
        }
        Sleep(500);
    }

    if (g_farHook.target) {
        DWORD old;
        VirtualProtect(g_farHook.target, g_farHook.stolen_bytes, PAGE_EXECUTE_READWRITE, &old);
        memcpy(g_farHook.target, g_farHook.original_bytes, g_farHook.stolen_bytes);
        VirtualProtect(g_farHook.target, g_farHook.stolen_bytes, old, &old);
        FlushInstructionCache(GetCurrentProcess(), g_farHook.target, g_farHook.stolen_bytes);
    }

    DeleteCriticalSection(&g_itemsLock);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        {
            FILE* f = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\injection_trace.txt", "w");
            if (f) {
                fprintf(f, "=== DLLMAIN ENTERED ===\n");
                fprintf(f, "hModule=%p reason=%lu PID=%lu TID=%lu tick=%lu\n",
                    hModule, reason, GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount());
                fflush(f); fclose(f);
            }
        }
        {
            FILE* fw = fopen(CRASH_DIR "worker_debug.txt", "w");
            if (fw) fclose(fw);
        }
        DisableThreadLibraryCalls(hModule);
        tlog("DisableThreadLibraryCalls done\n");
        DWORD tid = 0;
        HANDLE h = CreateThread(nullptr, 0, worker_thread, nullptr, 0, &tid);
        tlog("CreateThread returned handle=%p tid=%lu err=%lu\n", h, tid, GetLastError());
        tlog("our DLL base=%p\n", hModule);
    }
    return TRUE;
}

#include "win.h"
#include "overlay.h"
#include "pe_resolve.h"
#include "debug_log.h"
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

// -------------------------------------------------------------------
// Disk location for our logs / dumps.
//
// Everything the DLL writes goes into %APPDATA%\Microsoft\PerfCache\
// so it blends in with legit Windows caches. Filenames are innocuous
// too (perf_crash.dat, perf_events.dat, etc.). Directory is created
// on first write.
//
// CRASH_DIR / CRASH_DIR_W keep the same interface so all the existing
// fopen_s(CRASH_DIR "foo.txt", ...) calls still work — they just now
// resolve to a runtime-computed path prefix instead of a hardcoded one.
// -------------------------------------------------------------------

static const char* crash_dir_ansi() {
    static char path[MAX_PATH] = {};
    if (path[0]) return path;
    char appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (n && n < MAX_PATH) {
        // Two-level create; both idempotent.
        char lvl1[MAX_PATH];
        snprintf(lvl1, sizeof(lvl1), "%s\\Microsoft", appdata);
        CreateDirectoryA(lvl1, nullptr);
        snprintf(path, sizeof(path), "%s\\Microsoft\\PerfCache\\", appdata);
        CreateDirectoryA(path, nullptr);
    } else {
        strncpy_s(path, sizeof(path),
                  "C:\\ProgramData\\Microsoft\\PerfCache\\", _TRUNCATE);
    }
    return path;
}

static const wchar_t* crash_dir_wide() {
    static wchar_t wpath[MAX_PATH] = {};
    if (wpath[0]) return wpath;
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n && n < MAX_PATH) {
        wchar_t lvl1[MAX_PATH];
        _snwprintf_s(lvl1, MAX_PATH, _TRUNCATE, L"%s\\Microsoft", appdata);
        CreateDirectoryW(lvl1, nullptr);
        _snwprintf_s(wpath, MAX_PATH, _TRUNCATE, L"%s\\Microsoft\\PerfCache\\", appdata);
        CreateDirectoryW(wpath, nullptr);
    } else {
        wcsncpy_s(wpath, MAX_PATH,
                  L"C:\\ProgramData\\Microsoft\\PerfCache\\", _TRUNCATE);
    }
    return wpath;
}

// Wrapper that joins the runtime-computed prefix with a per-file name.
static errno_t crash_fopen_s(FILE** outFile, const char* leaf, const char* mode) {
    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s%s", crash_dir_ansi(), leaf);
    return fopen_s(outFile, full, mode);
}

// (No more CRASH_DIR / CRASH_DIR_W macros — call sites now use
//  crash_fopen_s + a leaf-only string, and ringlog::set_disk_mirror
//  builds its wide path from crash_dir_wide() at init.)

namespace {
#ifndef _NTDEF_
typedef LONG NTSTATUS;
#endif

typedef struct _LDR_UNICODE_STRING {
    USHORT Length; USHORT MaximumLength; PWSTR Buffer;
} LDR_UNICODE_STRING;

typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA {
    ULONG Flags;
    const LDR_UNICODE_STRING* FullDllName;
    const LDR_UNICODE_STRING* BaseDllName;
    PVOID DllBase;
    ULONG SizeOfImage;
} LDR_DLL_LOADED_NOTIFICATION_DATA;

typedef union _LDR_DLL_NOTIFICATION_DATA {
    LDR_DLL_LOADED_NOTIFICATION_DATA Loaded;
    LDR_DLL_LOADED_NOTIFICATION_DATA Unloaded;
} LDR_DLL_NOTIFICATION_DATA;

typedef VOID (NTAPI *LDR_DLL_NOTIFICATION_FUNCTION)(ULONG NotificationReason,
    const LDR_DLL_NOTIFICATION_DATA* NotificationData, PVOID Context);

typedef NTSTATUS (NTAPI *pLdrRegisterDllNotification)(ULONG Flags,
    LDR_DLL_NOTIFICATION_FUNCTION NotificationFunction, PVOID Context, PVOID* Cookie);

typedef NTSTATUS (NTAPI *pLdrUnregisterDllNotification)(PVOID Cookie);

#define LDR_DLL_NOTIFICATION_REASON_LOADED 1

static void NTAPI dll_load_cb(ULONG reason, const LDR_DLL_NOTIFICATION_DATA* data, PVOID ctx) {
    if (reason != LDR_DLL_NOTIFICATION_REASON_LOADED) return;
    if (!data || !data->Loaded.BaseDllName || !data->Loaded.BaseDllName->Buffer) return;
    const wchar_t* target = L"GameAssembly.dll";
    USHORT chars = data->Loaded.BaseDllName->Length / 2;
    USHORT want = 16;
    if (chars != want) return;
    const wchar_t* b = data->Loaded.BaseDllName->Buffer;
    for (USHORT i = 0; i < want; ++i) {
        wchar_t x = b[i], y = target[i];
        if (x >= L'A' && x <= L'Z') x += 32;
        if (y >= L'A' && y <= L'Z') y += 32;
        if (x != y) return;
    }
    HANDLE evt = (HANDLE)ctx;
    if (evt) SetEvent(evt);
}
} // namespace

static void wlog(const char* fmt, ...) {
    char tmp[256];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    ringlog::push("[w] %s", tmp);
}
static void tlog(const char* fmt, ...) {
    char tmp[256];
    va_list ap; va_start(ap, fmt); vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    ringlog::push("[t] %s", tmp);
}
static FILE* open_crash_log(const char* mode = "a") {
    FILE* f = nullptr;
    if (crash_fopen_s(&f, "perf_crash.dat", mode) != 0) return nullptr;
    return f;
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
volatile uintptr_t g_lastVehRip = 0;
volatile ULONG g_lastVehCode = 0;
volatile uintptr_t g_lastVehModBase = 0;
char g_lastVehModName[64] = {0};
volatile int g_lastVehScope = 0;

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
    fprintf(f, "  WinPerfHelper crash report\n");
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

    fprintf(f, "\n--- MODULE STATE ---\n");
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
    HMODULE sc = GetModuleHandleA("RTSSHelper64.dll");
    HMODULE dxgi = GetModuleHandleA("dxgi.dll");
    fprintf(f, "GameAssembly.dll   = %p\n", ga);
    fprintf(f, "RTSSHelper64.dll   = %p\n", sc);
    fprintf(f, "dxgi.dll           = %p\n", dxgi);

    fprintf(f, "\n========================================\n");
    fprintf(f, "  END CRASH REPORT\n");
    fprintf(f, "========================================\n");
    fflush(f);
}

static void write_minidump(EXCEPTION_POINTERS* ep) {
    char dmp_path[MAX_PATH];
    snprintf(dmp_path, sizeof(dmp_path), "%sperf_dump.dat", crash_dir_ansi());
    HANDLE hFile = CreateFileA(dmp_path, GENERIC_WRITE, 0, NULL,
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

static uintptr_t g_isbadreadptr_start = 0;
static uintptr_t g_isbadreadptr_end   = 0;

static void cache_isbadreadptr_range() {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32) return;
    auto fn = reinterpret_cast<uintptr_t>(GetProcAddress(k32, "IsBadReadPtr"));
    if (fn) {
        g_isbadreadptr_start = fn;
        g_isbadreadptr_end   = fn + 0x80;
    }
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

    if (code == 0xC0000005 && g_isbadreadptr_start) {
        uintptr_t rip = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
        if (rip >= g_isbadreadptr_start && rip < g_isbadreadptr_end)
            return EXCEPTION_CONTINUE_SEARCH;
    }

    if (code == 0xC0000005 && GetCurrentThreadId() == g_workerThreadId && g_workerVehActive) {
        g_workerVehActive = false;
        g_vehCrashRecovered = true;
        uintptr_t rip = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
        g_lastVehRip = rip;
        g_lastVehCode = code;
        HMODULE hmod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)rip, &hmod) && hmod) {
            g_lastVehModBase = (uintptr_t)hmod;
            char path[MAX_PATH] = {0};
            if (GetModuleFileNameA(hmod, path, MAX_PATH)) {
                const char* base = strrchr(path, '\\');
                strncpy_s(g_lastVehModName, sizeof(g_lastVehModName), base ? base + 1 : path, _TRUNCATE);
            } else {
                g_lastVehModName[0] = 0;
            }
        } else {
            g_lastVehModBase = 0;
            g_lastVehModName[0] = 0;
        }
        if (g_vehInnerActive) {
            g_vehInnerActive = false;
            g_lastVehScope = 1;
            *ep->ContextRecord = g_vehInnerCtx;
        } else if (g_vehEntityActive) {
            g_vehEntityActive = false;
            g_lastVehScope = 2;
            *ep->ContextRecord = g_vehEntityCtx;
        } else {
            g_lastVehScope = 0;
            *ep->ContextRecord = g_vehSavedCtx;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (code == 0xC0000005 && GetCurrentThreadId() == g_renderThreadId && g_renderThreadId != 0) {
        g_overlayDisabled = true;
        FILE* rf = open_crash_log();
        if (rf) {
            fprintf(rf, "\n!!! RENDER THREAD AV — overlay disabled !!!\n\n");
            write_full_crash(rf, ep);
            fclose(rf);
        }
    }

    // Only run the heavy symbolicating crash write ONCE per session — Sym*
    // API demand-loads every module's PDB from disk (10-30s cold thrash).
    // Repeated on every stale-pointer AV caused the periodic full-app freeze.
    // Cheap append still records that another AV happened, without symbolication.
    if (InterlockedCompareExchange(&g_dumpWritten, 1, 0) == 0) {
        FILE* f = open_crash_log();
        if (f) {
            write_full_crash(f, ep);
            fclose(f);
        }
        if (code == 0xC0000005 || code == 0xC00000FD || code == 0xC0000096 ||
            code == 0xC000001D || code == 0xC0000094) {
            write_minidump(ep);
        }
    } else {
        FILE* f = open_crash_log();
        if (f) {
            fprintf(f, "\n[additional AV suppressed] code=0x%08lX addr=%p tid=%lu exceptionsSeen=%d\n",
                    code, ep->ExceptionRecord->ExceptionAddress, GetCurrentThreadId(), g_exceptionCount);
            fclose(f);
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI final_crash_handler(EXCEPTION_POINTERS*) {
    return EXCEPTION_EXECUTE_HANDLER;
}

static void on_terminate() {
    FILE* f = open_crash_log();
    if (f) {
        fprintf(f, "\n!!! std::terminate called on thread %lu !!!\n", GetCurrentThreadId());
        fflush(f); fclose(f);
    }
    _exit(1);
}

static void on_abort_signal(int) {
    FILE* f = open_crash_log();
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


    // scan_entities runs under the outer VEH umbrella (g_workerVehActive
    // is true) so it can recover from any AV mid-walk. If it AVs, we set
    // 3s cooldown but each of the apply_* mods below still runs
    // independently on the NEXT tick they're due.
    __try {
        scan_entities();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // No cooldown — per-entity SEH (seh_process_one_entity) catches AVs
        // individually. Any outer wait here just froze the ESP for the whole
        // wait interval per crash. LO 2026-08-08: kill all Sleeps in the
        // worker path, yield instead.
        wlog("[worker] SEH in scan_entities tick %d: 0x%08lX (no backoff, continuing)\n",
             scanCounter, GetExceptionCode());
        g_entityCount.store(0);
        SwitchToThread();
    }

    // Each apply_* runs in its OWN __try. Disable VEH catching around each
    // so local __try/__except handles any AV without jumping back to the
    // outer safe_scan_tick captured context (which would kill the rest of
    // the mods for this tick AND cost 3s cooldown).
    if (scanCounter % 5 == 0) {
        bool prev = g_workerVehActive;
        g_workerVehActive = false;
        if (g_turretRapidFire.load() || g_turretNoRecoil.load()) {
            __try { apply_turret_mods(); }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                wlog("[worker] SEH in apply_turret_mods: 0x%08lX\n", GetExceptionCode());
            }
        }
        if (g_weaponModsEnabled.load()) {
            __try { apply_weapon_mods(); }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                wlog("[worker] SEH in apply_weapon_mods: 0x%08lX\n", GetExceptionCode());
            }
        }
        __try { apply_player_mods(); }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            wlog("[worker] SEH in apply_player_mods: 0x%08lX\n", GetExceptionCode());
        }
        __try { apply_world_mods(); }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            wlog("[worker] SEH in apply_world_mods: 0x%08lX\n", GetExceptionCode());
            g_todInstance = 0;  // dead — never try again this session
        }
        // Storm circle scan — read-only, no writes; every 5th scan is fine.
        __try { scan_storm_entities((void*)g_gameContextModule); }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            wlog("[worker] SEH in scan_storm_entities: 0x%08lX\n", GetExceptionCode());
        }
        g_workerVehActive = prev;
    }

    // Noclip: run every worker iteration (NOT gated by %5) for smoother motion.
    __try { apply_noclip_step(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        wlog("[worker] SEH in apply_noclip_step: 0x%08lX\n", GetExceptionCode());
    }

    if (g_dumpEntities.load()) {
        __try { dump_entities_to_file(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_probeContext.load()) {
        __try { probe_context_to_file(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// Dump every method of a klass — name, return type, param types, address —
// to a text file. Returns the address of the method matching wantMethodName
// (nullptr if not requested or not found). Free function because __try cannot
// live inside a lambda.
static void* dump_klass_methods(const IL2CPP_API& api, void* klass, const char* fileName, const char* wantMethodName) {
    if (!klass) return nullptr;
    if (!api.il2cpp_class_get_methods || !api.il2cpp_method_get_name
        || !api.il2cpp_method_get_param_count || !api.il2cpp_method_get_return_type
        || !api.il2cpp_method_get_param || !api.il2cpp_type_get_name) return nullptr;
    FILE* mf = nullptr;
    fopen_s(&mf, fileName, "w");
    if (!mf) return nullptr;
    const char* cn = api.il2cpp_class_get_name(klass);
    const char* ns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(klass) : "";
    fprintf(mf, "# Methods of %s.%s\n# klass=%p\n\n", ns ? ns : "", cn ? cn : "?", klass);
    void* foundAddr = nullptr;
    void* iter = nullptr;
    void* method;
    int mcount = 0;
    while ((method = api.il2cpp_class_get_methods(klass, &iter)) != nullptr) {
        const char* mname = api.il2cpp_method_get_name(method);
        uint32_t pc = api.il2cpp_method_get_param_count(method);
        void* addr = *(void**)method;
        const char* retName = "?";
        __try {
            void* retType = api.il2cpp_method_get_return_type(method);
            if (retType) retName = api.il2cpp_type_get_name(retType);
        } __except(EXCEPTION_EXECUTE_HANDLER) { retName = "<seh>"; }
        fprintf(mf, "[%3d] %s %s(", mcount, retName ? retName : "?", mname ? mname : "?");
        for (uint32_t p = 0; p < pc; p++) {
            const char* pname = "?", *ptype = "?";
            __try {
                if (api.il2cpp_method_get_param_name) pname = api.il2cpp_method_get_param_name(method, p);
                void* pt = api.il2cpp_method_get_param(method, p);
                if (pt) ptype = api.il2cpp_type_get_name(pt);
            } __except(EXCEPTION_EXECUTE_HANDLER) { ptype = "<seh>"; }
            fprintf(mf, "%s%s %s", p > 0 ? ", " : "", ptype ? ptype : "?", pname ? pname : "?");
        }
        fprintf(mf, ")   addr=%p\n", addr);
        if (wantMethodName && mname && strcmp(mname, wantMethodName) == 0 && !foundAddr) {
            foundAddr = addr;
        }
        mcount++;
    }
    fprintf(mf, "\n# total: %d methods\n", mcount);
    fclose(mf);
    wlog("[worker] %s: %d methods dumped\n", fileName, mcount);
    return foundAddr;
}

// Call an IL2CPP get_* accessor and dump the returned object's first 0x80
// bytes to a file. Free function because __try can't sit inside worker_thread
// (which contains C++ objects with destructors — C2712).
static void call_and_dump_getter(void* getterAddr, void* thisPtr, const char* fileName, const char* label) {
    if (!getterAddr || !thisPtr) return;
    FILE* gf = nullptr;
    fopen_s(&gf, fileName, "w");
    if (!gf) return;
    fprintf(gf, "# Result of calling %s\n# on instance %p, method %p\n\n",
            label ? label : "?", thisPtr, getterAddr);
    __try {
        typedef void* (*fn_getter)(void* thisPtr, void* methodInfo);
        fn_getter f = (fn_getter)getterAddr;
        void* ret = f(thisPtr, nullptr);
        fprintf(gf, "returned = %p\n\n", ret);
        if (ret) {
            fprintf(gf, "First 0x80 bytes:\n");
            for (int off = 0; off < 0x80; off += 8) {
                uintptr_t v = *(uintptr_t*)((uintptr_t)ret + off);
                fprintf(gf, "  [+0x%02X] %016llX", off, (unsigned long long)v);
                if (v == 0) fprintf(gf, "  null");
                else if (v >= 0x10000000ULL && v < 0x00007FFFFFFFFFFFULL) {
                    MEMORY_BASIC_INFORMATION mbi;
                    if (VirtualQuery((LPCVOID)v, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.State == MEM_COMMIT) {
                        uintptr_t maybeKlass = *(uintptr_t*)v;
                        fprintf(gf, "  ptr klass?=%p", (void*)maybeKlass);
                    } else {
                        fprintf(gf, "  ptr (region invalid)");
                    }
                } else if (v < 0x100000000ULL) {
                    fprintf(gf, "  int=%lu", (unsigned long)v);
                }
                fprintf(gf, "\n");
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        fprintf(gf, "\n*** SEH 0x%08lX calling getter ***\n", GetExceptionCode());
    }
    fclose(gf);
    wlog("[worker] %s dumped\n", fileName);
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
    ringlog::push("[worker] START tid=%lu tick=%lu", GetCurrentThreadId(), GetTickCount());
    ringlog::force_flush();

    g_workerThreadId = GetCurrentThreadId();
    ringlog::push("[worker] pre cache_isbadreadptr_range");
    ringlog::force_flush();
    cache_isbadreadptr_range();
    ringlog::push("[worker] post cache_isbadreadptr_range");
    ringlog::force_flush();

    static bool s_vehInstalled = false;
    if (!s_vehInstalled) {
        ringlog::push("[worker] pre AddVectoredExceptionHandler");
        ringlog::force_flush();
        AddVectoredExceptionHandler(1, crash_handler);
        s_vehInstalled = true;
        ringlog::push("[worker] post AddVectoredExceptionHandler");
        ringlog::force_flush();
    }

    ringlog::push("[worker] pre SetUnhandledExceptionFilter");
    ringlog::force_flush();
    SetUnhandledExceptionFilter(final_crash_handler);
    std::set_terminate(on_terminate);
    signal(SIGABRT, on_abort_signal);
    InitializeCriticalSection(&g_itemsLock);
    tlog("critical section initialized\n");

    HMODULE ga = pe_resolve::find_module("GameAssembly.dll");
    if (!ga) {
        tlog("GameAssembly.dll not yet loaded - registering DLL notification\n");
        HMODULE ntdll = pe_resolve::find_module("ntdll.dll");
        auto pReg = (pLdrRegisterDllNotification)pe_resolve::get_proc(ntdll, "LdrRegisterDllNotification");
        auto pUnreg = (pLdrUnregisterDllNotification)pe_resolve::get_proc(ntdll, "LdrUnregisterDllNotification");
        if (!pReg || !pUnreg) {
            tlog("LdrRegisterDllNotification unavailable - falling back to slow poll\n");
            while (!ga && g_running.load()) {
                Sleep(5000);
                ga = pe_resolve::find_module("GameAssembly.dll");
            }
        } else {
            HANDLE evt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            PVOID cookie = nullptr;
            NTSTATUS st = pReg(0, dll_load_cb, (PVOID)evt, &cookie);
            tlog("LdrRegisterDllNotification NTSTATUS=0x%08lX cookie=%p\n", (long)st, cookie);
            // Race-safe: check once more AFTER registering in case GameAssembly.dll
            // loaded between our first check and the callback register.
            ga = pe_resolve::find_module("GameAssembly.dll");
            if (!ga) {
                WaitForSingleObject(evt, INFINITE);
                ga = pe_resolve::find_module("GameAssembly.dll");
            }
            if (cookie) pUnreg(cookie);
            CloseHandle(evt);
        }
    }
    if (!ga) return 0;
    tlog("GameAssembly.dll found at %p (tier=%s)\n", ga, pe_resolve::last_tier());

    static IL2CPP_API api;
    tlog("calling resolve_all...\n");
    resolve_all(ga, api);
    set_il2cpp_api_ptr(&api);
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
            g_getChildCount = (fn_get_child_count)find_method_address(api, coreImage, "UnityEngine", "Transform", "get_childCount", 0);
            g_getChild = (fn_get_child)find_method_address(api, coreImage, "UnityEngine", "Transform", "GetChild", 1);
            g_getName = (fn_get_name)find_method_address(api, coreImage, "UnityEngine", "Object", "get_name", 0);
            wlog("[worker] Camera: get_main=%p W2S=%p transform=%p forward=%p position=%p parent=%p childCount=%p getChild=%p getName=%p\n", (void*)g_cameraGetMain, (void*)g_cameraW2S, (void*)g_getTransform, (void*)g_getForward, (void*)g_getPosition, (void*)g_getParent, (void*)g_getChildCount, (void*)g_getChild, (void*)g_getName);
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
        // Find UserNameComponent — enumerate ALL matches across namespaces
        // and pick the one with the most fields (the real component class,
        // not a stub/interface variant that has 0 fields). Log every hit
        // to UserName_candidates.txt for eyeball verification.
        if (api.il2cpp_domain_get && api.il2cpp_image_get_class_count && api.il2cpp_image_get_class && api.il2cpp_class_get_name) {
            FILE* uf = nullptr;
            crash_fopen_s(&uf, "perf_a.dat", "w");
            if (uf) fprintf(uf, "# All classes containing 'UserName' — pick the one with real fields.\n\n");

            size_t asmCount3 = 0;
            void* dom3 = api.il2cpp_domain_get();
            void** assemblies3 = api.il2cpp_domain_get_assemblies(dom3, &asmCount3);
            int bestFieldCount = -1;
            void* bestKlass = nullptr;
            char bestSummary[256] = "";
            int totalMatches = 0;
            for (size_t i = 0; i < asmCount3; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies3[i]);
                if (!img) continue;
                const char* imgName = api.il2cpp_image_get_name ? api.il2cpp_image_get_name(img) : "?";
                size_t classCount = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < classCount; j++) {
                    void* klass = api.il2cpp_image_get_class(img, j);
                    if (!klass) continue;
                    const char* cn = api.il2cpp_class_get_name(klass);
                    if (!cn) continue;
                    // Exact-name match only — reject "UserNamesHUD", "UserNameHUDWidget",
                    // "UserNamesHUDUpdateSystem", etc.
                    if (strcmp(cn, "UserNameComponent") != 0) continue;
                    const char* ns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(klass) : "";
                    int fc = 0;
                    if (api.il2cpp_class_get_fields) {
                        void* iter = nullptr;
                        while (api.il2cpp_class_get_fields(klass, &iter)) fc++;
                    }
                    totalMatches++;
                    if (uf) fprintf(uf, "match[%d] klass=%p asm=%s ns='%s' name='%s' fields=%d\n",
                                    totalMatches, klass, imgName, ns ? ns : "", cn, fc);
                    // Prefer the ECS component in Hologryph.HoloNet.Shared.Users.Components —
                    // that's the real per-user component the parallel UserContextModule uses.
                    // Fall back to highest field count otherwise.
                    bool isHoloNetEcs = ns && strstr(ns, "HoloNet") && strstr(ns, "Users") && strstr(ns, "Components");
                    bool pickIt = false;
                    if (isHoloNetEcs) pickIt = true;
                    else if (!bestKlass && fc > bestFieldCount) pickIt = true;
                    else if (bestKlass) {
                        // Don't overwrite the HoloNet ECS pick with a HUD class
                        const char* curNs = api.il2cpp_class_get_namespace(bestKlass);
                        bool curIsHoloNet = curNs && strstr(curNs, "HoloNet") && strstr(curNs, "Users");
                        if (!curIsHoloNet && fc > bestFieldCount) pickIt = true;
                    }
                    if (pickIt) {
                        bestFieldCount = fc;
                        bestKlass = klass;
                        snprintf(bestSummary, sizeof(bestSummary),
                                 "asm=%s ns='%s' name='%s' fields=%d",
                                 imgName, ns ? ns : "", cn, fc);
                    }
                }
            }
            g_userNameKlass = bestKlass;

            // Second pass: also grab the HUD-side class (UserNamesHUDUpdateSystem
            // etc. — anything with UserName in name, most-fields-wins). This
            // becomes the type we hand to Component.GetComponentInChildren in
            // seh_resolve_username since the ECS class isn't a MonoBehaviour
            // and GCiC returns null for it.
            int bestHudFields = -1;
            void* bestHudKlass = nullptr;
            for (size_t i = 0; i < asmCount3; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies3[i]);
                if (!img) continue;
                size_t classCount = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < classCount; j++) {
                    void* klass = api.il2cpp_image_get_class(img, j);
                    if (!klass) continue;
                    const char* cn = api.il2cpp_class_get_name(klass);
                    if (!cn || !strstr(cn, "UserName")) continue;
                    const char* ns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(klass) : "";
                    // Exclude the ECS component we already picked
                    if (ns && strstr(ns, "HoloNet") && strstr(ns, "Users") && strstr(ns, "Components")) continue;
                    int fc = 0;
                    if (api.il2cpp_class_get_fields) {
                        void* iter = nullptr;
                        while (api.il2cpp_class_get_fields(klass, &iter)) fc++;
                    }
                    if (fc > bestHudFields) {
                        bestHudFields = fc;
                        bestHudKlass = klass;
                    }
                }
            }
            g_userNameHUDKlass = bestHudKlass;
            if (bestHudKlass) {
                wlog("[worker] UserName HUD klass = %p (fields=%d)\n",
                     bestHudKlass, bestHudFields);
            }
            if (uf) {
                fprintf(uf, "\n# CHOSEN: klass=%p %s\n", bestKlass, bestSummary);
                // Walk the parent chain and dump inherited fields — a 0-field
                // ECS component still has its actual data on a base class
                // (typically ValueComponent<T> or a generated Entitas base).
                if (bestKlass && api.il2cpp_class_get_parent && api.il2cpp_class_get_fields
                    && api.il2cpp_field_get_name && api.il2cpp_field_get_offset) {
                    fprintf(uf, "\n# Inheritance chain (own fields + inherited):\n");
                    void* cur = bestKlass;
                    int depth = 0;
                    while (cur && depth < 8) {
                        const char* cnCur = api.il2cpp_class_get_name(cur);
                        const char* nsCur = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(cur) : "";
                        fprintf(uf, "\n  [depth %d] klass=%p %s.%s\n", depth, cur, nsCur ? nsCur : "", cnCur ? cnCur : "?");
                        void* fi = nullptr;
                        int fnum = 0;
                        while (void* field = api.il2cpp_class_get_fields(cur, &fi)) {
                            const char* fname = api.il2cpp_field_get_name(field);
                            size_t foff = api.il2cpp_field_get_offset(field);
                            fprintf(uf, "      field[%d] name='%s' offset=0x%zX\n",
                                    fnum++, fname ? fname : "?", foff);
                        }
                        if (fnum == 0) fprintf(uf, "      (no fields at this level)\n");
                        cur = api.il2cpp_class_get_parent(cur);
                        depth++;
                    }
                }
                fclose(uf);
            }
            wlog("[worker] UserNameComponent klass=%p matches=%d best=%s\n",
                 g_userNameKlass, totalMatches, bestSummary);
        }
        // Dump every class matching common infra patterns — managers,
        // systems, session/client/server holders. Player identity + world
        // state usually live in these singletons, not in ECS components.
        if (api.il2cpp_domain_get && api.il2cpp_image_get_class_count && api.il2cpp_image_get_class && api.il2cpp_class_get_name) {
            static const char* patterns[] = {
                "Manager", "System", "Session", "Client", "Server",
                "Module", "Singleton", "Provider", "Registry", "Cache",
                "Controller", "Service", "Handler", "Player", "Multiplayer",
                "Bone", "Anim", "Skeleton", "Rig", "Humanoid",
                "Account", "Auth", "User"
            };
            FILE* mf = nullptr;
            crash_fopen_s(&mf, "perf_b.dat", "w");
            if (mf) fprintf(mf, "# All classes matching manager/system/session/client/server/etc. patterns.\n"
                                "# Sorted by field count (real data holders float to the top).\n\n");

            struct Hit { void* klass; const char* asm_; const char* ns; const char* name; int fields; };
            static Hit hits[8192];
            int hitCount = 0;

            size_t asmCount = 0;
            void* dom = api.il2cpp_domain_get();
            void** assemblies = api.il2cpp_domain_get_assemblies(dom, &asmCount);
            for (size_t i = 0; i < asmCount; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies[i]);
                if (!img) continue;
                const char* imgName = api.il2cpp_image_get_name ? api.il2cpp_image_get_name(img) : "?";
                size_t classCount = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < classCount; j++) {
                    void* klass = api.il2cpp_image_get_class(img, j);
                    if (!klass) continue;
                    const char* cn = api.il2cpp_class_get_name(klass);
                    if (!cn) continue;
                    bool match = false;
                    for (auto* p : patterns) { if (strstr(cn, p)) { match = true; break; } }
                    if (!match) continue;
                    const char* ns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(klass) : "";
                    int fc = 0;
                    if (api.il2cpp_class_get_fields) {
                        void* iter = nullptr;
                        while (api.il2cpp_class_get_fields(klass, &iter)) fc++;
                    }
                    if (hitCount < (int)(sizeof(hits) / sizeof(hits[0]))) {
                        hits[hitCount++] = { klass, imgName, ns ? ns : "", cn, fc };
                    }
                }
            }
            for (int a = 0; a < hitCount - 1; ++a) {
                int best = a;
                for (int b = a + 1; b < hitCount; ++b) if (hits[b].fields > hits[best].fields) best = b;
                if (best != a) { Hit t = hits[a]; hits[a] = hits[best]; hits[best] = t; }
            }
            if (mf) {
                for (int i = 0; i < hitCount; ++i) {
                    fprintf(mf, "=== [%d] %s.%s   fields=%d   asm=%s   klass=%p ===\n",
                            i, hits[i].ns, hits[i].name, hits[i].fields, hits[i].asm_, hits[i].klass);
                    // Walk the full inheritance chain — real fields (like the
                    // 'name' string on BaseTypeNameComponent`1) often live on
                    // grandparents, not the leaf class. Same reason weeks of
                    // guessing missed UserName.
                    if (api.il2cpp_class_get_fields && api.il2cpp_field_get_name && api.il2cpp_field_get_offset) {
                        void* cur = hits[i].klass;
                        int depth = 0;
                        while (cur && depth < 8) {
                            const char* cnCur = api.il2cpp_class_get_name(cur);
                            const char* nsCur = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(cur) : "";
                            void* fi = nullptr;
                            int fnum = 0;
                            // Peek field count before printing header so we
                            // don't spam empty depth lines.
                            void* peekIt = nullptr;
                            int peekCount = 0;
                            while (api.il2cpp_class_get_fields(cur, &peekIt)) peekCount++;
                            if (peekCount > 0 || depth == 0) {
                                fprintf(mf, "  [depth %d] %s.%s\n", depth,
                                        nsCur ? nsCur : "", cnCur ? cnCur : "?");
                                while (void* field = api.il2cpp_class_get_fields(cur, &fi)) {
                                    const char* fname = api.il2cpp_field_get_name(field);
                                    size_t foff = api.il2cpp_field_get_offset(field);
                                    fprintf(mf, "      field[%d] name='%s' offset=0x%zX\n",
                                            fnum++, fname ? fname : "?", foff);
                                }
                                if (fnum == 0) fprintf(mf, "      (no fields)\n");
                            }
                            if (api.il2cpp_class_get_parent) cur = api.il2cpp_class_get_parent(cur);
                            else break;
                            depth++;
                        }
                    }
                    fprintf(mf, "\n");
                }
                fprintf(mf, "# total: %d classes\n", hitCount);
                fclose(mf);
            }
            wlog("[worker] ManagerDump: %d classes -> ManagerDump.txt\n", hitCount);
        }

        // ============================================================
        // MEGA CLASS DUMP — every klass in every assembly, full parent
        // chain, all fields, all methods with signatures. Zero pattern
        // filter. One shot at boot. Overflow of data by design.
        // ============================================================
        if (api.il2cpp_domain_get && api.il2cpp_domain_get_assemblies
            && api.il2cpp_assembly_get_image && api.il2cpp_image_get_class_count
            && api.il2cpp_image_get_class && api.il2cpp_class_get_name) {
            FILE* af = nullptr;
            crash_fopen_s(&af, "perf_all.dat", "w");
            if (af) {
                fprintf(af, "# COMPLETE class dump — every klass in every assembly.\n");
                fprintf(af, "# Full parent chain, all fields (name + type), all methods (name + return type + param types).\n");
                fprintf(af, "# No pattern filter. No skip list. Everything.\n\n");
                size_t asmCount = 0;
                void* dom = api.il2cpp_domain_get();
                void** assemblies = api.il2cpp_domain_get_assemblies(dom, &asmCount);
                int totalKlasses = 0;
                int totalFields = 0;
                int totalMethods = 0;
                for (size_t i = 0; i < asmCount; i++) {
                    void* img = api.il2cpp_assembly_get_image(assemblies[i]);
                    if (!img) continue;
                    const char* imgName = api.il2cpp_image_get_name ? api.il2cpp_image_get_name(img) : "?";
                    size_t classCount = api.il2cpp_image_get_class_count(img);
                    fprintf(af, "\n\n########################################\n");
                    fprintf(af, "## ASSEMBLY: %s   (%zu classes)\n", imgName, classCount);
                    fprintf(af, "########################################\n");
                    for (size_t j = 0; j < classCount; j++) {
                        void* klass = api.il2cpp_image_get_class(img, j);
                        if (!klass) continue;
                        const char* cn = api.il2cpp_class_get_name(klass);
                        const char* ns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(klass) : "";
                        fprintf(af, "\n=== %s.%s ===\n", ns ? ns : "", cn ? cn : "?");
                        totalKlasses++;
                        // Parent chain fields
                        if (api.il2cpp_class_get_fields && api.il2cpp_field_get_name && api.il2cpp_field_get_type && api.il2cpp_type_get_name) {
                            void* cur = klass;
                            int depth = 0;
                            while (cur && depth < 8) {
                                const char* dcn = api.il2cpp_class_get_name(cur);
                                const char* dns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(cur) : "";
                                __try {
                                    void* fit = nullptr;
                                    void* field;
                                    bool wroteHeader = false;
                                    while ((field = api.il2cpp_class_get_fields(cur, &fit)) != nullptr) {
                                        if (!wroteHeader) {
                                            fprintf(af, "  fields (from %s.%s):\n", dns ? dns : "", dcn ? dcn : "?");
                                            wroteHeader = true;
                                        }
                                        const char* fname = api.il2cpp_field_get_name(field);
                                        void* ftype = api.il2cpp_field_get_type(field);
                                        const char* tname = ftype ? api.il2cpp_type_get_name(ftype) : "?";
                                        fprintf(af, "    %s: %s\n", fname ? fname : "?", tname ? tname : "?");
                                        totalFields++;
                                    }
                                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                if (!api.il2cpp_class_get_parent) break;
                                cur = api.il2cpp_class_get_parent(cur);
                                depth++;
                            }
                        }
                        // Methods with signatures
                        if (api.il2cpp_class_get_methods && api.il2cpp_method_get_name && api.il2cpp_method_get_param_count && api.il2cpp_method_get_return_type && api.il2cpp_method_get_param && api.il2cpp_type_get_name) {
                            void* mit = nullptr;
                            void* method;
                            bool wroteHeader = false;
                            while ((method = api.il2cpp_class_get_methods(klass, &mit)) != nullptr) {
                                if (!wroteHeader) {
                                    fprintf(af, "  methods:\n");
                                    wroteHeader = true;
                                }
                                const char* mname = api.il2cpp_method_get_name(method);
                                uint32_t pc = api.il2cpp_method_get_param_count(method);
                                const char* retName = "void";
                                __try {
                                    void* rt = api.il2cpp_method_get_return_type(method);
                                    if (rt) retName = api.il2cpp_type_get_name(rt);
                                } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                fprintf(af, "    %s %s(", retName ? retName : "?", mname ? mname : "?");
                                for (uint32_t p = 0; p < pc; p++) {
                                    const char* ptype = "?";
                                    __try {
                                        void* pt = api.il2cpp_method_get_param(method, p);
                                        if (pt) ptype = api.il2cpp_type_get_name(pt);
                                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                    fprintf(af, "%s%s", p > 0 ? ", " : "", ptype ? ptype : "?");
                                }
                                fprintf(af, ")\n");
                                totalMethods++;
                            }
                        }
                    }
                }
                fprintf(af, "\n\n# TOTAL: %d classes, %d fields, %d methods across %zu assemblies\n",
                        totalKlasses, totalFields, totalMethods, asmCount);
                fclose(af);
                wlog("[worker] MEGA CLASS DUMP: %d klasses / %d fields / %d methods -> perf_all.dat\n",
                     totalKlasses, totalFields, totalMethods);
            }
        }

        // Inventory/Equip/Slot method hunt — every klass whose name matches
        // inventory-related patterns, dump ALL its methods with signatures.
        // We're looking for game-side functions like SelectSlot, EquipItem,
        // HoldItem, PickHotbarSlot, TakeOut, PutAway — anything we can call
        // directly to swap items without input simulation (which the game
        // ignores for consumable slots).
        if (api.il2cpp_domain_get && api.il2cpp_image_get_class_count && api.il2cpp_image_get_class && api.il2cpp_class_get_name && api.il2cpp_class_get_methods && api.il2cpp_method_get_name) {
            static const char* invPatterns[] = {
                "Inventory", "Hotbar", "QuickAccess", "Equip", "Hold",
                "Slot", "SelectItem", "ActiveItem", "HeldItem", "PickItem",
                "TakeOut", "PutAway", "SwitchWeapon", "SwapItem",
                "WeaponSelector", "WeaponSwitcher", "ItemSelector",
                "ItemController", "WeaponController", "ItemUse"
            };
            FILE* ef = nullptr;
            crash_fopen_s(&ef, "perf_m.dat", "w");
            if (ef) fprintf(ef, "# Inventory/Equip/Slot method hunt — all methods on classes\n"
                                "# whose name matches inventory/hotbar/equip/slot patterns.\n"
                                "# Look for methods like Equip(int), SelectSlot(int),\n"
                                "# HoldItem(entity), TakeOut(item) — anything with a slot\n"
                                "# index or item pointer parameter we can call directly.\n\n");

            size_t asmCount = 0;
            void* dom = api.il2cpp_domain_get();
            void** assemblies = api.il2cpp_domain_get_assemblies(dom, &asmCount);
            int totalKlasses = 0;
            int totalMethods = 0;
            for (size_t i = 0; i < asmCount; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies[i]);
                if (!img) continue;
                const char* imgName = api.il2cpp_image_get_name ? api.il2cpp_image_get_name(img) : "?";
                // Skip system/third-party — noise
                if (imgName && (strstr(imgName, "mscorlib") || strstr(imgName, "System.")
                                || strstr(imgName, "Newtonsoft") || strstr(imgName, "Cinemachine")
                                || strstr(imgName, "Rewired") || strstr(imgName, "UnityEngine")
                                || strstr(imgName, "Sirenix") || strstr(imgName, "RootMotion")))
                    continue;
                size_t classCount = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < classCount; j++) {
                    void* klass = api.il2cpp_image_get_class(img, j);
                    if (!klass) continue;
                    const char* cn = api.il2cpp_class_get_name(klass);
                    if (!cn) continue;
                    bool match = false;
                    for (auto* p : invPatterns) { if (strstr(cn, p)) { match = true; break; } }
                    if (!match) continue;
                    const char* ns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(klass) : "";
                    // Iterate methods
                    void* mIter = nullptr;
                    void* method;
                    int mLocal = 0;
                    bool headerWritten = false;
                    while ((method = api.il2cpp_class_get_methods(klass, &mIter)) != nullptr) {
                        const char* mname = api.il2cpp_method_get_name(method);
                        if (!mname) continue;
                        // Only interesting method names — reduce noise
                        bool interesting = false;
                        static const char* mnPatterns[] = {
                            "Equip", "Hold", "Select", "Pick", "Take", "PutAway",
                            "Switch", "Swap", "Use", "Activate", "SetActive",
                            "SetSlot", "SetItem", "Slot", "Change", "Set"
                        };
                        for (auto* mp : mnPatterns) if (strstr(mname, mp)) { interesting = true; break; }
                        if (!interesting) continue;
                        if (!headerWritten) {
                            fprintf(ef, "\n=== %s.%s   asm=%s   klass=%p ===\n",
                                    ns ? ns : "", cn, imgName, klass);
                            headerWritten = true;
                        }
                        uint32_t pc = api.il2cpp_method_get_param_count(method);
                        void* addr = *(void**)method;
                        const char* retName = "void";
                        if (api.il2cpp_method_get_return_type) {
                            __try {
                                void* rt = api.il2cpp_method_get_return_type(method);
                                if (rt && api.il2cpp_type_get_name)
                                    retName = api.il2cpp_type_get_name(rt);
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        }
                        fprintf(ef, "  %s %s(", retName ? retName : "?", mname);
                        for (uint32_t p = 0; p < pc; p++) {
                            const char* ptype = "?";
                            __try {
                                void* pt = api.il2cpp_method_get_param(method, p);
                                if (pt && api.il2cpp_type_get_name)
                                    ptype = api.il2cpp_type_get_name(pt);
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            fprintf(ef, "%s%s", p > 0 ? ", " : "", ptype ? ptype : "?");
                        }
                        fprintf(ef, ")   addr=%p\n", addr);
                        mLocal++;
                        totalMethods++;
                    }
                    if (headerWritten) totalKlasses++;
                }
            }
            if (ef) {
                fprintf(ef, "\n# total: %d klasses, %d methods\n", totalKlasses, totalMethods);
                fclose(ef);
            }
            wlog("[worker] InventoryHunt: %d klasses, %d methods -> perf_m.dat\n",
                 totalKlasses, totalMethods);
        }

        // Bone-container hunt by shape — every class with 3+ fields named
        // like human bones is a candidate for the pose/skeleton holder. Works
        // even when the class name doesn't mention "bone" at all (game likely
        // uses "PoseData", "HumanoidRig", "SkeletonState", etc.).
        if (api.il2cpp_domain_get && api.il2cpp_image_get_class_count && api.il2cpp_image_get_class && api.il2cpp_class_get_name && api.il2cpp_class_get_fields && api.il2cpp_field_get_name) {
            static const char* boneWords[] = {
                "Head", "Neck", "Chest", "Spine", "Hips",
                "Shoulder", "Elbow", "Hand", "Clavicle",
                "Femur", "Knee", "Ankle", "Toe", "Foot", "Thigh"
            };
            FILE* bf = nullptr;
            crash_fopen_s(&bf, "perf_c.dat", "w");
            if (bf) fprintf(bf, "# Classes with 3+ fields whose name looks like a human bone.\n"
                                "# The one with the most bone-named fields is almost certainly\n"
                                "# the pose/skeleton container.\n\n");

            struct BoneHit { void* klass; const char* asm_; const char* ns; const char* name; int boneMatches; int totalFields; };
            static BoneHit boneHits[2048];
            int bhCount = 0;

            size_t asmCount = 0;
            void* dom = api.il2cpp_domain_get();
            void** assemblies = api.il2cpp_domain_get_assemblies(dom, &asmCount);
            for (size_t i = 0; i < asmCount; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies[i]);
                if (!img) continue;
                const char* imgName = api.il2cpp_image_get_name ? api.il2cpp_image_get_name(img) : "?";
                size_t classCount = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < classCount; j++) {
                    void* klass = api.il2cpp_image_get_class(img, j);
                    if (!klass) continue;
                    const char* cn = api.il2cpp_class_get_name(klass);
                    if (!cn) continue;
                    int boneMatches = 0, total = 0;
                    void* fi = nullptr;
                    while (void* field = api.il2cpp_class_get_fields(klass, &fi)) {
                        total++;
                        const char* fname = api.il2cpp_field_get_name(field);
                        if (!fname) continue;
                        for (auto* bw : boneWords) if (strstr(fname, bw)) { boneMatches++; break; }
                    }
                    if (boneMatches < 3) continue;
                    if (bhCount >= (int)(sizeof(boneHits) / sizeof(boneHits[0]))) continue;
                    const char* ns = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(klass) : "";
                    boneHits[bhCount++] = { klass, imgName, ns ? ns : "", cn, boneMatches, total };
                }
            }
            // Sort by boneMatches desc
            for (int a = 0; a < bhCount - 1; ++a) {
                int best = a;
                for (int b = a + 1; b < bhCount; ++b)
                    if (boneHits[b].boneMatches > boneHits[best].boneMatches) best = b;
                if (best != a) { BoneHit t = boneHits[a]; boneHits[a] = boneHits[best]; boneHits[best] = t; }
            }
            if (bf) {
                for (int i = 0; i < bhCount; ++i) {
                    fprintf(bf, "=== [%d] %s.%s   boneFields=%d totalFields=%d   asm=%s   klass=%p ===\n",
                            i, boneHits[i].ns, boneHits[i].name,
                            boneHits[i].boneMatches, boneHits[i].totalFields,
                            boneHits[i].asm_, boneHits[i].klass);
                    if (api.il2cpp_field_get_offset) {
                        void* fi = nullptr;
                        int fnum = 0;
                        while (void* field = api.il2cpp_class_get_fields(boneHits[i].klass, &fi)) {
                            const char* fname = api.il2cpp_field_get_name(field);
                            size_t foff = api.il2cpp_field_get_offset(field);
                            fprintf(bf, "    field[%d] name='%s' offset=0x%zX\n",
                                    fnum++, fname ? fname : "?", foff);
                        }
                    }
                    fprintf(bf, "\n");
                }
                fprintf(bf, "# total: %d candidate classes\n", bhCount);
                fclose(bf);
            }
            wlog("[worker] SkeletonSearch: %d candidates -> SkeletonSearch.txt\n", bhCount);
        }

        // ------------------------------------------------------------------
        // Module-instance heap scan — find live instances of Multiplayer,
        // Network, and User context modules parallel to GameContextModule.
        // Every Il2CppObject's first qword is its klass pointer. Scan
        // committed R/W regions for any qword matching our target klasses.
        // ------------------------------------------------------------------
        if (api.il2cpp_domain_get && api.il2cpp_image_get_class_count && api.il2cpp_image_get_class
            && api.il2cpp_class_get_name && api.il2cpp_class_get_namespace) {

            struct Target { const char* ns; const char* name; void* klass; };
            static Target targets[] = {
                { "Hologryph.HoloNet.Shared.Users",              "UserContextModule",                       nullptr },
                { "Hologryph.HoloNet.Shared.Connections",        "ConnectionContextModule",                 nullptr },
                { "Hologryph.HoloNet.Client.Connections",        "ClientConnectionContextModule",           nullptr },
                { "Hologryph.HoloNet.Shared.Connections",        "NetworkStatisticsModule",                 nullptr },
                { "Hologryph.HoloNet.Client",                    "ClientNetworkControllerModule",           nullptr },
                { "Hologryph.HoloNet.Shared",                    "NetworkControllerModule",                 nullptr },
                { "Hologryph.HoloNet.Server",                    "ServerNetworkControllerModuleBase",       nullptr },
                { "Hologryph.HoloNet.Client.LiteNetLib",         "LiteNetLibClientNetworkTransportModule",  nullptr },
                { "Hologryph.HoloNet.Server.LiteNetLib",         "LiteNetLibServerNetworkTransportModule",  nullptr },
                { "Hologryph.Sand.Shared.Game",                  "GameContextModule",                       nullptr },
            };
            const int NTARGETS = (int)(sizeof(targets)/sizeof(targets[0]));

            // Resolve klass pointers by (ns, name)
            size_t asmCount = 0;
            void* dom = api.il2cpp_domain_get();
            void** assemblies = api.il2cpp_domain_get_assemblies(dom, &asmCount);
            for (size_t i = 0; i < asmCount; i++) {
                void* img = api.il2cpp_assembly_get_image(assemblies[i]);
                if (!img) continue;
                size_t classCount = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < classCount; j++) {
                    void* klass = api.il2cpp_image_get_class(img, j);
                    if (!klass) continue;
                    const char* cn = api.il2cpp_class_get_name(klass);
                    const char* ns = api.il2cpp_class_get_namespace(klass);
                    if (!cn || !ns) continue;
                    for (int t = 0; t < NTARGETS; t++) {
                        if (targets[t].klass) continue;
                        if (strcmp(cn, targets[t].name) == 0 && strcmp(ns, targets[t].ns) == 0) {
                            targets[t].klass = klass;
                        }
                    }
                }
            }

            FILE* mif = nullptr;
            crash_fopen_s(&mif, "perf_d.dat", "w");
            if (mif) {
                fprintf(mif, "# Live instance hunt for parallel context/network modules.\n");
                fprintf(mif, "# Each hit is an address whose first qword == the target klass ptr.\n\n");
                fprintf(mif, "# Resolved klass pointers:\n");
                for (int t = 0; t < NTARGETS; t++) {
                    fprintf(mif, "#   %-45s ns=%s  klass=%p\n",
                            targets[t].name, targets[t].ns, targets[t].klass);
                }
                fprintf(mif, "\n");
                fflush(mif);
            }

            // Walk committed R/W memory regions and scan qword-aligned words
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t addr = 0;
            uintptr_t maxAddr = 0x00007FFFFFFFFFFFULL;
            int totalHits = 0;
            int regionsScanned = 0;
            while (addr < maxAddr && VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
                uintptr_t base = (uintptr_t)mbi.BaseAddress;
                SIZE_T sz = mbi.RegionSize;
                bool scan = (mbi.State == MEM_COMMIT)
                    && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
                    && !(mbi.Protect & PAGE_GUARD)
                    && (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED);
                // Skip absurdly large regions (typically reserved arenas)
                if (scan && sz <= 0x10000000ULL) {
                    regionsScanned++;
                    __try {
                        uintptr_t* p = (uintptr_t*)base;
                        uintptr_t* end = (uintptr_t*)(base + sz - sizeof(uintptr_t));
                        for (; p < end; p++) {
                            uintptr_t v = *p;
                            if (v < 0x10000000ULL || v > 0x00007FFFFFFFFFFFULL) continue;
                            for (int t = 0; t < NTARGETS; t++) {
                                if (!targets[t].klass) continue;
                                if (v == (uintptr_t)targets[t].klass) {
                                    if (mif) fprintf(mif, "HIT  %-45s  instance=%p  (region base=%p size=0x%zX)\n",
                                                     targets[t].name, (void*)p, (void*)base, (size_t)sz);
                                    totalHits++;
                                    break;
                                }
                            }
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) { /* skip bad region */ }
                }
                if (sz == 0) break;
                addr = base + sz;
            }

            if (mif) {
                fprintf(mif, "\n# scanned %d regions, %d total hits\n", regionsScanned, totalHits);
                fclose(mif);
            }
            wlog("[worker] ModuleInstances: scanned %d regions, %d hits -> ModuleInstances.txt\n", regionsScanned, totalHits);

            // -----------------------------------------------------------
            // UserContextModule method-signature dumper — enumerate every
            // method on UserContextModule with its return type + param
            // types. Once we see e.g. "GetUserList : List<UserData>" we
            // know exactly which method to call and what to expect back.
            //
            // Also captures the LIVE SINGLETON pointer: first hit in the
            // small-object heap range (0x10000000-0x40000000) that ISN'T
            // in the metadata cluster is the actual instance.
            // -----------------------------------------------------------
            void* userCtxKlass = nullptr;
            for (int t = 0; t < NTARGETS; t++)
                if (strcmp(targets[t].name, "UserContextModule") == 0) { userCtxKlass = targets[t].klass; break; }

            // Resolve UserEntity klass in the same assembly as UserContextModule
            void* userEntityKlass = nullptr;
            if (api.il2cpp_class_from_name) {
                for (size_t i = 0; i < asmCount && !userEntityKlass; i++) {
                    void* img = api.il2cpp_assembly_get_image(assemblies[i]);
                    if (!img) continue;
                    userEntityKlass = api.il2cpp_class_from_name(img, "Hologryph.HoloNet.Shared.Users", "UserEntity");
                    if (userEntityKlass) break;
                }
            }

            char p1[MAX_PATH], p2[MAX_PATH];
            snprintf(p1, sizeof(p1), "%sperf_g.dat", crash_dir_ansi());
            snprintf(p2, sizeof(p2), "%sperf_h.dat", crash_dir_ansi());
            void* getUsersAddr = dump_klass_methods(api, userCtxKlass, p1, "get_users");
            dump_klass_methods(api, userEntityKlass, p2, nullptr);

            // Also capture GetEntityWithAccountId — the method that maps
            // an AccountId (UInt64) to a UserEntity. That's how we resolve
            // real player names: PlayerAvatar.AccountId → UserEntity →
            // UserNameComponent.name.
            if (userCtxKlass && api.il2cpp_class_get_methods && api.il2cpp_method_get_name) {
                void* miter = nullptr;
                void* method;
                while ((method = api.il2cpp_class_get_methods(userCtxKlass, &miter)) != nullptr) {
                    const char* mname = api.il2cpp_method_get_name(method);
                    if (mname && strcmp(mname, "GetEntityWithAccountId") == 0) {
                        void* addr = *(void**)method;
                        g_getUserEntityByAcctId = (fn_getEntByAcctId)addr;
                        wlog("[worker] GetEntityWithAccountId addr=%p captured\n", addr);
                        break;
                    }
                }
                if (!g_getUserEntityByAcctId)
                    wlog("[worker] GetEntityWithAccountId NOT FOUND on UserContextModule\n");
            }

            if (userCtxKlass) {
                // ============ Singleton pointer capture ============
                // Re-scan just for UserContextModule hits, collect them all
                static uintptr_t hits[512];
                int nhits = 0;
                MEMORY_BASIC_INFORMATION mbi2;
                uintptr_t addr2 = 0;
                while (addr2 < 0x00007FFFFFFFFFFFULL && VirtualQuery((LPCVOID)addr2, &mbi2, sizeof(mbi2)) == sizeof(mbi2)) {
                    uintptr_t base = (uintptr_t)mbi2.BaseAddress;
                    SIZE_T sz = mbi2.RegionSize;
                    bool scan = (mbi2.State == MEM_COMMIT)
                        && (mbi2.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
                        && !(mbi2.Protect & PAGE_GUARD);
                    if (scan && sz <= 0x10000000ULL) {
                        __try {
                            uintptr_t* p = (uintptr_t*)base;
                            uintptr_t* end = (uintptr_t*)(base + sz - sizeof(uintptr_t));
                            for (; p < end; p++) {
                                if (*p == (uintptr_t)userCtxKlass) {
                                    if (nhits < 512) hits[nhits++] = (uintptr_t)p;
                                }
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    }
                    if (sz == 0) break;
                    addr2 = base + sz;
                }

                // Find the tightest cluster (max count within a 0x2000 window)
                uintptr_t bestStart = 0;
                int bestCount = 0;
                for (int i = 0; i < nhits; i++) {
                    int cnt = 0;
                    for (int j = i; j < nhits && hits[j] < hits[i] + 0x2000; j++) cnt++;
                    if (cnt > bestCount) { bestCount = cnt; bestStart = hits[i]; }
                }

                // Capture singleton pointer: the first hit in the small-object
                // heap (0x10000000-0x40000000) that is NOT inside the metadata
                // cluster. Metadata table is method descriptors; the real
                // runtime instance sits alone in the managed heap.
                g_userContextModuleKlass = userCtxKlass;
                for (int i = 0; i < nhits; i++) {
                    if (hits[i] >= 0x10000000ULL && hits[i] < 0x40000000ULL
                        && !(hits[i] >= bestStart && hits[i] < bestStart + 0x2000)) {
                        g_userContextModuleInstance = (void*)hits[i];
                        wlog("[worker] UserContextModule singleton captured: %p (klass %p)\n",
                             g_userContextModuleInstance, g_userContextModuleKlass);
                        break;
                    }
                }
                if (!g_userContextModuleInstance && nhits > 0) {
                    // Fallback — take the first small-heap hit even if it's in the cluster
                    for (int i = 0; i < nhits; i++) {
                        if (hits[i] >= 0x10000000ULL && hits[i] < 0x40000000ULL) {
                            g_userContextModuleInstance = (void*)hits[i];
                            wlog("[worker] UserContextModule singleton (fallback): %p\n",
                                 g_userContextModuleInstance);
                            break;
                        }
                    }
                }

                // Actually CALL get_users() on the singleton — dump the IGroup
                // so we see its structure and can wire up UserEntity enumeration.
                call_and_dump_getter(getUsersAddr, g_userContextModuleInstance,
                                     ([]{ static char p[MAX_PATH]; snprintf(p, sizeof(p), "%sperf_i.dat", crash_dir_ansi()); return (const char*)p; })(),
                                     "UserContextModule.get_users()");

                FILE* uf = nullptr;
                crash_fopen_s(&uf, "perf_e.dat", "w");
                if (uf) {
                    fprintf(uf, "# UserContextModule record dumper — cluster of %d hits starting at %p\n", bestCount, (void*)bestStart);
                    fprintf(uf, "# Field offsets should be stable per game version.\n");
                    fprintf(uf, "# Look for: pointers into small-object heap (0x10000000-0x40000000) = strings/objects\n");
                    fprintf(uf, "# Look for: 8-byte SteamIDs (0x110000xxxxxxxxxx = Steam individual)\n");
                    fprintf(uf, "# Look for: floats (position xyz — 3 consecutive small floats)\n\n");

                    int dumped = 0;
                    for (int i = 0; i < nhits && dumped < 8; i++) {
                        if (hits[i] < bestStart || hits[i] >= bestStart + 0x2000) continue;
                        uintptr_t rec = hits[i];
                        fprintf(uf, "=== Record #%d @ %p ===\n", dumped, (void*)rec);
                        __try {
                            for (int off = 0; off < 0x90; off += 8) {
                                uintptr_t v = *(uintptr_t*)(rec + off);
                                fprintf(uf, "  [+0x%02X] %016llX", off, (unsigned long long)v);
                                // Type inference
                                if (v == (uintptr_t)userCtxKlass) fprintf(uf, "  <-- UserContextModule klass");
                                else if (v >= 0x10000000ULL && v < 0x00007FFFFFFFFFFFULL) {
                                    MEMORY_BASIC_INFORMATION mbi3;
                                    if (VirtualQuery((LPCVOID)v, &mbi3, sizeof(mbi3)) == sizeof(mbi3)
                                        && mbi3.State == MEM_COMMIT) {
                                        // Try to read as Il2CppString (len at +0x10, wchars at +0x14)
                                        __try {
                                            int slen = *(int*)(v + 0x10);
                                            if (slen > 0 && slen < 64) {
                                                wchar_t* ws = (wchar_t*)(v + 0x14);
                                                char nb[65] = {};
                                                for (int c = 0; c < slen; c++) nb[c] = (char)ws[c];
                                                bool printable = true;
                                                for (int c = 0; c < slen; c++) if (nb[c] < 0x20 || nb[c] > 0x7E) { printable = false; break; }
                                                if (printable) {
                                                    fprintf(uf, "  string=\"%s\" (len=%d)", nb, slen);
                                                } else {
                                                    fprintf(uf, "  ptr (region %p)", mbi3.BaseAddress);
                                                }
                                            } else {
                                                // Might be a klass — first qword of object is klass ptr
                                                uintptr_t maybeKlass = *(uintptr_t*)v;
                                                fprintf(uf, "  ptr (klass?=%016llX)", (unsigned long long)maybeKlass);
                                            }
                                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                                            fprintf(uf, "  ptr (unreadable inner)");
                                        }
                                    }
                                } else if (v > 0 && v < 0x100000000ULL) {
                                    // Small value — maybe 2 ints or a bitfield
                                    unsigned int lo = (unsigned int)(v & 0xFFFFFFFF);
                                    unsigned int hi = (unsigned int)((v >> 32) & 0xFFFFFFFF);
                                    if (hi == 0) fprintf(uf, "  int=%u", lo);
                                    else fprintf(uf, "  ints=(%u,%u)", lo, hi);
                                    // Float check
                                    float fv; memcpy(&fv, &lo, 4);
                                    if (fv > -10000.0f && fv < 10000.0f && fv != 0.0f) fprintf(uf, "  or float=%.3f", fv);
                                }
                                fprintf(uf, "\n");
                            }
                            fprintf(uf, "\n");
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            fprintf(uf, "  *** SEH reading record ***\n\n");
                        }
                        dumped++;
                    }
                    fclose(uf);
                }
                wlog("[worker] UserRecords: cluster of %d hits at %p, dumped %d records -> UserRecords.txt\n",
                     bestCount, (void*)bestStart, (bestCount < 8 ? bestCount : 8));
            }
        }

        if (g_userNameKlass && api.il2cpp_class_get_type && api.il2cpp_type_get_object) {
            void* userNameIl2cppType = api.il2cpp_class_get_type(g_userNameKlass);
            if (userNameIl2cppType) {
                g_userNameType = api.il2cpp_type_get_object(userNameIl2cppType);
            }
            wlog("[worker] UserNameComponent Type object=%p\n", g_userNameType);
        }
        if (g_userNameHUDKlass && api.il2cpp_class_get_type && api.il2cpp_type_get_object) {
            void* hudIl2cppType = api.il2cpp_class_get_type(g_userNameHUDKlass);
            if (hudIl2cppType) {
                g_userNameHUDType = api.il2cpp_type_get_object(hudIl2cppType);
            }
            wlog("[worker] UserName HUD Type object=%p\n", g_userNameHUDType);
        }

        // TimeOfDayManager singleton pointer resolution — for always-day.
        // Class has static field '_instance' at offset 0x0. il2cpp_field_static_get_value
        // fetches the current singleton pointer.
        if (api.il2cpp_domain_get && api.il2cpp_image_get_class_count && api.il2cpp_image_get_class
            && api.il2cpp_class_get_name && api.il2cpp_class_get_fields && api.il2cpp_field_get_name
            && api.il2cpp_field_static_get_value) {
            size_t asmCountT = 0;
            void* domT = api.il2cpp_domain_get();
            void** assembliesT = api.il2cpp_domain_get_assemblies(domT, &asmCountT);
            void* todKlass = nullptr;
            for (size_t i = 0; i < asmCountT && !todKlass; i++) {
                void* img = api.il2cpp_assembly_get_image(assembliesT[i]);
                if (!img) continue;
                size_t cc = api.il2cpp_image_get_class_count(img);
                for (size_t j = 0; j < cc; j++) {
                    void* k = api.il2cpp_image_get_class(img, j);
                    if (!k) continue;
                    const char* cn = api.il2cpp_class_get_name(k);
                    if (cn && strcmp(cn, "TimeOfDayManager") == 0) {
                        todKlass = k;
                        break;
                    }
                }
            }
            if (todKlass) {
                void* fi = nullptr;
                void* instField = nullptr;
                while (void* f = api.il2cpp_class_get_fields(todKlass, &fi)) {
                    const char* fn = api.il2cpp_field_get_name(f);
                    if (fn && strcmp(fn, "_instance") == 0) { instField = f; break; }
                }
                if (instField) {
                    void* instPtr = nullptr;
                    __try {
                        api.il2cpp_field_static_get_value(instField, &instPtr);
                        g_todInstance = (uintptr_t)instPtr;
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    wlog("[worker] TimeOfDayManager singleton = %p\n", (void*)g_todInstance);
                }
            }
        }
        if (g_userNameKlass && api.il2cpp_class_get_fields && api.il2cpp_field_get_name && api.il2cpp_field_get_offset) {
            FILE* ff = nullptr;
            crash_fopen_s(&ff, "perf_f.dat", "w");
            if (ff) fprintf(ff, "# Fields of UserNameComponent klass=%p (walking parent chain)\n\n", g_userNameKlass);

            // Walk the full inheritance chain. UserNameComponent itself has
            // no fields — the actual `name` string lives on the generic
            // grandparent BaseTypeNameComponent`1 at offset 0x10. Picking
            // ONLY on the leaf klass is why we've been reading empty for
            // weeks. Walk parents until we find any `name`/`Name` field.
            void* cur = g_userNameKlass;
            int depth = 0;
            int totalFields = 0;
            while (cur && depth < 8 && g_userNameFieldOffset < 0) {
                const char* cnCur = api.il2cpp_class_get_name(cur);
                const char* nsCur = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(cur) : "";
                if (ff) fprintf(ff, "\n[depth %d] klass=%p %s.%s\n",
                                depth, cur, nsCur ? nsCur : "", cnCur ? cnCur : "?");
                wlog("[worker] userName-fields depth=%d klass=%p %s.%s\n",
                     depth, cur, nsCur ? nsCur : "", cnCur ? cnCur : "?");
                void* iter = nullptr;
                while (void* field = api.il2cpp_class_get_fields(cur, &iter)) {
                    const char* fname = api.il2cpp_field_get_name(field);
                    size_t off = api.il2cpp_field_get_offset(field);
                    if (ff) fprintf(ff, "    field name='%s' offset=0x%zX\n",
                                    fname ? fname : "?", off);
                    wlog("[worker]   field '%s' offset=0x%zX (depth=%d)\n",
                         fname ? fname : "?", off, depth);
                    // Prefer exact "name" match; fall back to any *[Nn]ame*.
                    if (g_userNameFieldOffset < 0 && fname) {
                        if (strcmp(fname, "name") == 0 || strcmp(fname, "Name") == 0) {
                            g_userNameFieldOffset = (int)off;
                        }
                    }
                    totalFields++;
                }
                if (api.il2cpp_class_get_parent) cur = api.il2cpp_class_get_parent(cur);
                else break;
                depth++;
            }

            // Second pass — if exact "name" didn't hit, allow any *ame* field.
            if (g_userNameFieldOffset < 0) {
                cur = g_userNameKlass;
                depth = 0;
                while (cur && depth < 8 && g_userNameFieldOffset < 0) {
                    void* iter = nullptr;
                    while (void* field = api.il2cpp_class_get_fields(cur, &iter)) {
                        const char* fname = api.il2cpp_field_get_name(field);
                        if (fname && strstr(fname, "ame")) {
                            g_userNameFieldOffset = (int)api.il2cpp_field_get_offset(field);
                            break;
                        }
                    }
                    if (api.il2cpp_class_get_parent) cur = api.il2cpp_class_get_parent(cur);
                    else break;
                    depth++;
                }
            }

            if (ff) {
                fprintf(ff, "\n# Chosen name-field offset: 0x%X (totalFieldsSeen=%d)\n",
                        g_userNameFieldOffset, totalFields);
                fclose(ff);
            }
            wlog("[worker] UserNameComponent chain: totalFields=%d chose offset=0x%X\n",
                 totalFields, g_userNameFieldOffset);
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
    // Wire up Steam name resolver — resolves player SteamID64 -> platform name.
    steam_names_init();

    // -----------------------------------------------------------------
    // Install HoloMessengerModule.Publish hook — captures every outbound
    // HoloMessage from the game so we can replay them (dupe experiments).
    // Logs to perf_capture.dat: klass name + first 0x40 bytes per call.
    // -----------------------------------------------------------------
    if (api.il2cpp_class_from_name && api.il2cpp_class_get_methods && api.il2cpp_method_get_name) {
        void* holoModuleKlass = nullptr;
        size_t asmCountH = 0;
        void** assembliesH = api.il2cpp_domain_get_assemblies(api.il2cpp_domain_get(), &asmCountH);
        for (size_t i = 0; i < asmCountH && !holoModuleKlass; i++) {
            void* img = api.il2cpp_assembly_get_image(assembliesH[i]);
            if (!img) continue;
            holoModuleKlass = api.il2cpp_class_from_name(img, "Hologryph.HoloCore.Utils", "HoloMessengerModule");
        }
        if (holoModuleKlass) {
            void* mIter = nullptr;
            void* method;
            while ((method = api.il2cpp_class_get_methods(holoModuleKlass, &mIter)) != nullptr) {
                const char* mn = api.il2cpp_method_get_name(method);
                if (mn && strcmp(mn, "Publish") == 0) {
                    g_holoPublishAddr = *(void**)method;
                    break;
                }
            }
            if (g_holoPublishAddr) {
                wlog("[worker] HoloMessengerModule.Publish addr=%p — installing hook\n", g_holoPublishAddr);
                if (install_hook(g_publishHook, g_holoPublishAddr, (void*)hooked_publish)) {
                    wlog("[worker] Publish hook INSTALLED — messages captured to perf_capture.dat\n");
                } else {
                    wlog("[worker] Publish hook install FAILED\n");
                }
            } else {
                wlog("[worker] HoloMessengerModule.Publish method NOT found\n");
            }
        } else {
            wlog("[worker] HoloMessengerModule klass NOT found in any assembly\n");
        }
    }

    // Hook ClientNetworkControllerModule.Send* extension methods — these
    // are the actual outbound network senders for inventory operations
    // (MoveSlot / SplitSlot / Equip / Drop). place-on-shelf uses SendMoveSlot,
    // NOT HoloMessengerModule.Publish, which is why recording missed it.
    if (api.il2cpp_class_from_name && api.il2cpp_class_get_methods && api.il2cpp_method_get_name) {
        void* extKlass = nullptr;
        size_t asmCountS = 0;
        void** assembliesS = api.il2cpp_domain_get_assemblies(api.il2cpp_domain_get(), &asmCountS);
        // Search all assemblies for a class containing our target methods
        for (size_t i = 0; i < asmCountS && !extKlass; i++) {
            void* img = api.il2cpp_assembly_get_image(assembliesS[i]);
            if (!img) continue;
            size_t classCount = api.il2cpp_image_get_class_count(img);
            for (size_t j = 0; j < classCount; j++) {
                void* klass = api.il2cpp_image_get_class(img, j);
                if (!klass) continue;
                const char* cn = api.il2cpp_class_get_name(klass);
                if (!cn) continue;
                // Extension-method containers typically end in "Extensions"
                if (!strstr(cn, "Extensions")) continue;
                // Peek methods for SendMoveSlot
                void* mIter = nullptr;
                void* method;
                bool has = false;
                while ((method = api.il2cpp_class_get_methods(klass, &mIter)) != nullptr) {
                    const char* mn = api.il2cpp_method_get_name(method);
                    if (mn && strcmp(mn, "SendMoveSlot") == 0) { has = true; break; }
                }
                if (has) { extKlass = klass; break; }
            }
        }
        if (extKlass) {
            const char* ns2 = api.il2cpp_class_get_namespace ? api.il2cpp_class_get_namespace(extKlass) : "";
            const char* cn2 = api.il2cpp_class_get_name(extKlass);
            wlog("[worker] found network Extensions klass: %s.%s\n", ns2 ? ns2 : "", cn2 ? cn2 : "?");
            void* mIter = nullptr;
            void* method;
            while ((method = api.il2cpp_class_get_methods(extKlass, &mIter)) != nullptr) {
                const char* mn = api.il2cpp_method_get_name(method);
                if (!mn) continue;
                void* addr = *(void**)method;
                if      (strcmp(mn, "SendMoveSlot")  == 0) g_sendMoveSlotAddr  = addr;
                else if (strcmp(mn, "SendSplitSlot") == 0) g_sendSplitSlotAddr = addr;
                else if (strcmp(mn, "SendEquip")     == 0) g_sendEquipAddr     = addr;
                else if (strcmp(mn, "SendDrop")      == 0) g_sendDropAddr      = addr;
            }
            wlog("[worker] Send addresses: Move=%p Split=%p Equip=%p Drop=%p\n",
                 g_sendMoveSlotAddr, g_sendSplitSlotAddr, g_sendEquipAddr, g_sendDropAddr);
            if (g_sendMoveSlotAddr)  install_hook(g_sendMoveSlotHook,  g_sendMoveSlotAddr,  (void*)hooked_send_move_slot);
            if (g_sendSplitSlotAddr) install_hook(g_sendSplitSlotHook, g_sendSplitSlotAddr, (void*)hooked_send_split_slot);
            if (g_sendEquipAddr)     install_hook(g_sendEquipHook,     g_sendEquipAddr,     (void*)hooked_send_equip);
            if (g_sendDropAddr)      install_hook(g_sendDropHook,      g_sendDropAddr,      (void*)hooked_send_drop);
        } else {
            wlog("[worker] network Extensions klass NOT found (place-on-shelf recording will still miss)\n");
        }
    }
    {
        int scanCounter = 0;
        while (g_running.load()) {
            // Heartbeat: every 10 iters (~1s at Sleep(100)). Log wall gap
            // since previous beat — if we ever see gap > 5000ms it means the
            // worker thread was suspended (BE/GC/kernel actor), not looping.
            // Also log time for the previous scan tick so runaway scans are visible.
            static int heartbeat = 0;
            static DWORD s_lastHbTick = 0;
            static DWORD s_lastScanMs = 0;
            if (++heartbeat % 10 == 0) {
                DWORD nowT = GetTickCount();
                DWORD gap = s_lastHbTick ? (nowT - s_lastHbTick) : 0;
                const char* flag = (gap > 5000) ? " <<< STALL DETECTED" : "";
                wlog("[hb] #%d tick=%lu gapMs=%lu lastScanMs=%lu%s\n",
                     heartbeat, nowT, gap, s_lastScanMs, flag);
                s_lastHbTick = nowT;
            }
            void* gcm = (void*)g_gameContextModule;
            if (!is_readable(gcm, 0x18)) {
                g_entityCount.store(0);
                // No sleep — main loop pacing at bottom handles CPU throttle.
                continue;
            }

            RtlCaptureContext(&g_vehSavedCtx);
            if (g_vehCrashRecovered) {
                g_vehCrashRecovered = false;
                uintptr_t rip = g_lastVehRip;
                uintptr_t mb = g_lastVehModBase;
                const char* scopeName = (g_lastVehScope == 1) ? "INNER"
                                       : (g_lastVehScope == 2) ? "ENTITY" : "OUTER";
                if (mb && g_lastVehModName[0]) {
                    wlog("[worker] VEH recovered code=0x%08lX rip=%p (%s+0x%llX) scope=%s tick=%d — backing off 3s\n",
                         (unsigned long)g_lastVehCode, (void*)rip, g_lastVehModName,
                         (unsigned long long)(rip - mb), scopeName, scanCounter);
                } else {
                    wlog("[worker] VEH recovered code=0x%08lX rip=%p (module?) scope=%s tick=%d — backing off 3s\n",
                         (unsigned long)g_lastVehCode, (void*)rip, scopeName, scanCounter);
                }
                g_entityCount.store(0);
                scanCounter++;
                // Was 3000 — that Sleep froze the worker for 3s per VEH
                // recovery, making ESP labels rubber-band as g_items stayed
                // stale. Per-entity SEH catches AVs in-flight now, so this
                // outer recovery only yields briefly to avoid a tight loop.
                Sleep(200);
                continue;
            }

            g_workerVehActive = true;
            int scan_mod = g_dupeMode.load() ? 1 : 5;
            if (scanCounter % scan_mod == 0) {
                safe_scan_tick(scanCounter);
            }
            if (g_dumpShopClasses.load()) { dump_shop_classes(api); g_dumpShopClasses.store(false); }
            g_workerVehActive = false;


            scanCounter++;
            // Hard-kill hotkey — F12 = instant TerminateProcess. Bypasses
            // the game's 7-step shutdown hell (Alt+F4 → End Task → Not
            // Responding → Force Close). One key, dead in 10ms.
            if ((GetAsyncKeyState(g_hotkeyHardKill.load()) & 0x8000) || g_hardKillRequested.load()) {
                wlog("[worker] HARD KILL requested — TerminateProcess\n");
                TerminateProcess(GetCurrentProcess(), 0);
            }
            // Fire any scheduled Dupe Lab action whose countdown expired.
            dupelab_check_pending();
            // Record toggle hotkey (default Del) — arm-then-key model.
            // Arm a name via UI button, press hotkey to start capture,
            // press again to stop. No menu-open Esc noise polluting.
            {
                static bool s_recHotkeyDown = false;
                bool nowDown = (GetAsyncKeyState(g_hotkeyRecordToggle.load()) & 0x8000) != 0;
                if (nowDown && !s_recHotkeyDown) {
                    dupelab_record_hotkey_toggle();
                }
                s_recHotkeyDown = nowDown;
            }
            // Hotkey rebind capture — UI sets g_hotkeyCaptureRequest to a
            // feature id when user clicks "Bind". Worker watches for next
            // key press and assigns it to the requested feature.
            {
                int req = g_hotkeyCaptureRequest.load();
                if (req != 0) {
                    for (int vk = 0x08; vk <= 0xFE; vk++) {
                        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
                        if (GetAsyncKeyState(vk) & 0x8000) {
                            switch (req) {
                                case 1: g_hotkeyHardKill.store(vk); break;
                                case 2: g_hotkeyDupeSuspend.store(vk); break;
                                case 3: g_hotkeyDupeMaster.store(vk); break;
                                case 4: g_hotkeyPlaybackFirst.store(vk); break;
                            }
                            g_hotkeyCaptureRequest.store(0);
                            wlog("[worker] Hotkey %d rebound to VK 0x%02X\n", req, vk);
                            break;
                        }
                    }
                }
            }
            // F7 = playback the FIRST recorded action in Dupe Lab. Fires from
            // worker thread so game stays live (menu-triggered playback needs
            // Esc which pauses game = server won't process message).
            {
                static bool s_f7WasDown = false;
                bool nowDown = (GetAsyncKeyState(g_hotkeyPlaybackFirst.load()) & 0x8000) != 0;
                if (nowDown && !s_f7WasDown) {
                    // Try each named recording in priority order — playback the first non-empty
                    static const char* kOrder[] = {
                        "place-on-shelf", "place-in-box", "swap-box",
                        "pickup-from-shelf", "pickup-from-box", "split-stack", "equip-item"
                    };
                    for (auto* n : kOrder) {
                        if (dupelab_recording_count_cstr(n) > 0) {
                            wlog("[worker] F7 playback -> %s (%zu msgs)\n", n, dupelab_recording_count_cstr(n));
                            dupelab_playback_cstr(n);
                            break;
                        }
                    }
                }
                s_f7WasDown = nowDown;
            }
            // F9 = toggle dupe-suspend. Pauses all dupe auto-actions so LO
            // can interact with the world normally, hit again to resume.
            // Debounced so one press = one toggle.
            {
                static bool s_f9WasDown = false;
                bool nowDown = (GetAsyncKeyState(g_hotkeyDupeSuspend.load()) & 0x8000) != 0;
                if (nowDown && !s_f9WasDown) {
                    bool newState = !g_dupeSuspended.load();
                    g_dupeSuspended.store(newState);
                    wlog("[worker] F9 dupe-suspend toggled -> %s\n", newState ? "SUSPENDED" : "ACTIVE");
                }
                s_f9WasDown = nowDown;
            }
            // Noclip toggle-key: edge-detect and flip g_noClipActive.
            {
                static bool s_noclipWasDown = false;
                int tkey = g_hotkeyNoClipToggle.load();
                bool nowDown = (tkey != 0) && ((GetAsyncKeyState(tkey) & 0x8000) != 0);
                if (nowDown && !s_noclipWasDown) {
                    bool newState = !g_noClipActive.load();
                    g_noClipActive.store(newState);
                    wlog("[worker] noclip toggled -> %s\n", newState ? "ACTIVE" : "off");
                }
                s_noclipWasDown = nowDown;
            }
            // F10 = master dupe kill / restore — turns off ALL dupe-related
            // state so LO gets full world-interact back (auto-reequip,
            // dupeMode, heavyBypass, heavyFix2, permaLock, stickyLock).
            // Hit again to restore EVERYTHING to what was on before.
            {
                static bool s_f10WasDown = false;
                static bool s_savedAutoRe = false;
                static bool s_savedDupe = false;
                static bool s_savedHeavy1 = false;
                static bool s_savedHeavy2 = false;
                static bool s_savedPerma = false;
                static bool s_savedSticky = false;
                static int  s_savedLockedId = -1;
                static uintptr_t s_savedLockedPtr = 0;
                static bool s_dupeAllOff = false;
                bool nowDown = (GetAsyncKeyState(g_hotkeyDupeMaster.load()) & 0x8000) != 0;
                if (nowDown && !s_f10WasDown) {
                    if (!s_dupeAllOff) {
                        // Snapshot ALL dupe/lock state
                        s_savedAutoRe    = g_autoReequip.load();
                        s_savedDupe      = g_dupeMode.load();
                        s_savedHeavy1    = g_heavyBypass.load();
                        s_savedHeavy2    = g_heavyFix2.load();
                        s_savedPerma     = g_permaLockActive.load();
                        s_savedSticky    = g_stickyLock.load();
                        s_savedLockedId  = g_lockedEntityId.load();
                        s_savedLockedPtr = g_lockedEntityPtr.load();
                        // Kill everything → full world interact returns
                        g_autoReequip.store(false);
                        g_dupeMode.store(false);
                        g_heavyBypass.store(false);
                        g_heavyFix2.store(false);
                        g_permaLockActive.store(false);
                        g_stickyLock.store(false);
                        g_lockedEntityId.store(-1);
                        g_lockedEntityPtr.store(0);
                        s_dupeAllOff = true;
                        wlog("[worker] F10 DUPE-ALL OFF (world interact restored)\n");
                    } else {
                        // Full restore — back to whatever we were doing
                        g_autoReequip.store(s_savedAutoRe);
                        g_dupeMode.store(s_savedDupe);
                        g_heavyBypass.store(s_savedHeavy1);
                        g_heavyFix2.store(s_savedHeavy2);
                        g_permaLockActive.store(s_savedPerma);
                        g_stickyLock.store(s_savedSticky);
                        g_lockedEntityId.store(s_savedLockedId);
                        g_lockedEntityPtr.store(s_savedLockedPtr);
                        s_dupeAllOff = false;
                        wlog("[worker] F10 DUPE-ALL RESTORED\n");
                    }
                }
                s_f10WasDown = nowDown;
            }
            // Worker is now pinned to the last logical core at BELOW_NORMAL
            // priority (see DllMain), so it will NOT contend with the game's
            // render thread. Yield with Sleep(0) so equal-priority threads
            // on this core can run (there shouldn't be any), then loop again
            // at full tilt. Scan cadence is bounded only by scan_entities
            // wall time now — ~5ms typical, giving ~200 Hz ESP updates.
            Sleep(0);
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

// -------------------------------------------------------------------
// hide_module_from_peb — walk the PEB module lists and unlink our own
// LDR_DATA_TABLE_ENTRY so EnumProcessModules / CreateToolhelp32Snapshot
// module walkers don't see us. Complements manual-mapping (which never
// added us to PEB in the first place) by also zeroing entries in cases
// where injection did register us.
//
// x64 offsets (Win10 / Win11):
//   PEB via __readgsqword(0x60)
//   PEB->Ldr @ +0x18   (PEB_LDR_DATA*)
//     InLoadOrderModuleList         @ +0x10  (LIST_ENTRY head)
//     InMemoryOrderModuleList       @ +0x20
//     InInitializationOrderModuleList @ +0x30
//   LDR_DATA_TABLE_ENTRY:
//     InLoadOrderLinks         @ +0x00
//     InMemoryOrderLinks       @ +0x10
//     InInitializationOrderLinks @ +0x20
//     DllBase                  @ +0x30
//     FullDllName (US)         @ +0x48
//     BaseDllName (US)         @ +0x58
//     HashLinks                @ +0x7C
// -------------------------------------------------------------------
typedef struct _LIST_ENTRY_LOCAL {
    struct _LIST_ENTRY_LOCAL* Flink;
    struct _LIST_ENTRY_LOCAL* Blink;
} LIST_ENTRY_LOCAL;

static void unlink_one(LIST_ENTRY_LOCAL* le) {
    le->Blink->Flink = le->Flink;
    le->Flink->Blink = le->Blink;
    le->Flink = le;
    le->Blink = le;
}

static void hide_module_from_peb(HMODULE self) {
    __try {
#ifdef _WIN64
        uintptr_t peb = (uintptr_t)__readgsqword(0x60);
#else
        uintptr_t peb = (uintptr_t)__readfsdword(0x30);
#endif
        if (!peb) return;
        uintptr_t ldr = *(uintptr_t*)(peb + 0x18);
        if (!ldr) return;

        int unlinked = 0;
        const size_t list_offsets[3]  = { 0x10, 0x20, 0x30 };  // in PEB_LDR_DATA
        const size_t entry_offsets[3] = { 0x00, 0x10, 0x20 };  // in LDR_DATA_TABLE_ENTRY

        for (int list_i = 0; list_i < 3; list_i++) {
            LIST_ENTRY_LOCAL* head = (LIST_ENTRY_LOCAL*)(ldr + list_offsets[list_i]);
            LIST_ENTRY_LOCAL* cur  = head->Flink;
            int safety = 0;
            while (cur != head && safety < 512) {
                uintptr_t entry_base = (uintptr_t)cur - entry_offsets[list_i];
                void* dll_base = *(void**)(entry_base + 0x30);
                if (dll_base == (void*)self) {
                    unlink_one(cur);
                    unlinked++;

                    // Zero the entry's name strings so any secondary
                    // scanner reading BaseDllName / FullDllName gets
                    // empty results. UNICODE_STRING layout: WORD Length,
                    // WORD MaxLen, PVOID Buffer.
                    uint16_t* full_len   = (uint16_t*)(entry_base + 0x48);
                    uint16_t* full_max   = (uint16_t*)(entry_base + 0x4A);
                    wchar_t** full_buf   = (wchar_t**)(entry_base + 0x50);
                    uint16_t* base_len   = (uint16_t*)(entry_base + 0x58);
                    uint16_t* base_max   = (uint16_t*)(entry_base + 0x5A);
                    wchar_t** base_buf   = (wchar_t**)(entry_base + 0x60);
                    if (*full_buf) { for (int i = 0; i < (*full_len)/2; i++) (*full_buf)[i] = 0; }
                    if (*base_buf) { for (int i = 0; i < (*base_len)/2; i++) (*base_buf)[i] = 0; }
                    *full_len = 0; *full_max = 0;
                    *base_len = 0; *base_max = 0;

                    break;
                }
                cur = cur->Flink;
                safety++;
            }
        }
        ringlog::push("[peb-hide] unlinked from %d PEB lists (self=%p)", unlinked, (void*)self);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        ringlog::push("[peb-hide] SEH 0x%08lX", GetExceptionCode());
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // RTSS parking-zone injection can invoke DllMain multiple times because
        // RTSS calls the hijacked slot every frame until the launcher restores
        // it. Idempotent guard: only spawn one worker across all invocations.
        static LONG s_workerSpawned = 0;
        if (InterlockedCompareExchange(&s_workerSpawned, 1, 0) == 0) {
            ringlog::clear();
            {
                wchar_t rp[MAX_PATH];
                _snwprintf_s(rp, MAX_PATH, _TRUNCATE, L"%sperf_events.dat", crash_dir_wide());
                ringlog::set_disk_mirror(rp);
            }
            ringlog::push("[dllmain] PROCESS_ATTACH hModule=%p pid=%lu tid=%lu tick=%lu",
                hModule, GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount());
            DisableThreadLibraryCalls(hModule);
            // Seed rand() for jitter in scan-loop Sleep so cadence isn't
            // reproducible across launches.
            srand((unsigned)GetTickCount() ^ (unsigned)GetCurrentProcessId());
            // Hide our module from PEB list walkers.
            hide_module_from_peb(hModule);
            HANDLE wth = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
            if (wth) {
                // Pin worker to the LAST logical CPU so it never contends
                // with the game's render thread (usually core 0 or the
                // process's preferred core). Modern rigs have 8-32 cores;
                // sacrificing the last one to ESP costs nothing.
                SYSTEM_INFO si; GetSystemInfo(&si);
                DWORD ncores = si.dwNumberOfProcessors;
                if (ncores > 1) {
                    DWORD_PTR mask = ((DWORD_PTR)1) << (ncores - 1);
                    SetThreadAffinityMask(wth, mask);
                }
                // BELOW_NORMAL — if a normal-priority thread wants our core
                // we yield instantly. In practice nothing else is pinned to
                // the last core so this is belt-and-suspenders.
                SetThreadPriority(wth, THREAD_PRIORITY_BELOW_NORMAL);
                CloseHandle(wth);
            }
        }
    }
    return TRUE;
}

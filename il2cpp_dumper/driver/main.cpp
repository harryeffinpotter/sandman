// cl /EHsc /O2 main.cpp intel_driver.cpp mapper.cpp /Fe:bypass.exe /link ntdll.lib advapi32.lib shell32.lib
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "intel_driver.h"
#include "mapper.h"

#pragma comment(lib, "shell32.lib")

#define CLR_WHITE   0x07
#define CLR_GREEN   0x0A
#define CLR_RED     0x0C
#define CLR_YELLOW  0x0E
#define CLR_CYAN    0x0B
#define CLR_BRIGHT  0x0F

static HANDLE hCon;
static void color(WORD c) { SetConsoleTextAttribute(hCon, c); }

static bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev = {};
    DWORD sz = sizeof(elev);
    BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &sz);
    CloseHandle(token);
    return ok && elev.TokenIsElevated;
}

static void self_elevate(int argc, char* argv[]) {
    char args[MAX_PATH * 4] = {};
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat_s(args, " ");
        strcat_s(args, "\"");
        strcat_s(args, argv[i]);
        strcat_s(args, "\"");
    }
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t wargs[MAX_PATH * 4];
    MultiByteToWideChar(CP_ACP, 0, args, -1, wargs, MAX_PATH * 4);
    ShellExecuteW(nullptr, L"runas", exe, wargs, nullptr, SW_SHOWNORMAL);
}

static DWORD find_process(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32 pe = {}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static std::wstring get_exe_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    return s.substr(0, s.find_last_of(L"\\/") + 1);
}

int main(int argc, char* argv[]) {
    hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD buf = {120, 9999};
    SetConsoleScreenBufferSize(hCon, buf);
    SetConsoleTitleA("BYOVD Manual Mapper");

    color(CLR_BRIGHT);
    printf("========================================\n");
    printf("  BYOVD Manual Mapper — BattlEye Bypass\n");
    printf("========================================\n\n");

    if (argc < 2) {
        color(CLR_CYAN);
        printf("Usage: bypass.exe <dll_path> [process_name]\n");
        printf("  dll_path:     path to DLL to inject\n");
        printf("  process_name: target process (default: Sand.exe)\n");
        color(CLR_WHITE);
        printf("\nPress any key to exit...\n");
        system("pause >nul");
        return 1;
    }

    const char* dll_path = argv[1];
    const char* proc_name = (argc >= 3) ? argv[2] : "Sand.exe";

    if (!is_elevated()) {
        color(CLR_YELLOW); printf("[*] Requesting elevation...\n");
        self_elevate(argc, argv);
        return 0;
    }

    color(CLR_GREEN); printf("[+] Running as administrator\n");

    // Verify DLL exists
    if (GetFileAttributesA(dll_path) == INVALID_FILE_ATTRIBUTES) {
        color(CLR_RED); printf("[-] DLL not found: %s\n", dll_path);
        system("pause >nul");
        return 1;
    }

    char full_dll[MAX_PATH];
    GetFullPathNameA(dll_path, MAX_PATH, full_dll, nullptr);
    color(CLR_CYAN); printf("[*] DLL: %s\n", full_dll);

    // Verify iqvw64e.sys exists
    std::wstring driver_path = get_exe_dir() + L"iqvw64e.sys";
    if (GetFileAttributesW(driver_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        color(CLR_RED); printf("[-] iqvw64e.sys not found next to bypass.exe\n");
        system("pause >nul");
        return 1;
    }
    color(CLR_CYAN); printf("[*] Driver: iqvw64e.sys\n");

    // Wait for target process
    color(CLR_YELLOW); printf("[*] Waiting for %s...\n", proc_name);
    DWORD pid = 0;
    for (int i = 0; i < 300; i++) {
        pid = find_process(proc_name);
        if (pid) break;
        Sleep(2000);
        if (i % 5 == 4) { color(CLR_YELLOW); printf("[*] Still waiting... (%ds)\n", (i + 1) * 2); }
    }

    if (!pid) {
        color(CLR_RED); printf("[-] Process not found after 10 minutes: %s\n", proc_name);
        system("pause >nul");
        return 1;
    }
    color(CLR_GREEN); printf("[+] Found %s — PID %lu\n", proc_name, pid);

    // Give the game a few seconds to initialize
    color(CLR_YELLOW); printf("[*] Waiting 5s for process init...\n");
    Sleep(5000);

    // Load Intel driver
    color(CLR_YELLOW); printf("[*] Loading Intel driver...\n");
    if (!intel::LoadDriver(driver_path)) {
        color(CLR_RED); printf("[-] Failed to load Intel driver\n");
        system("pause >nul");
        return 1;
    }
    color(CLR_GREEN); printf("[+] Intel driver loaded\n");

    // Map the DLL
    color(CLR_YELLOW); printf("[*] Starting manual map...\n");
    uint64_t result = mapper::ManualMap(pid, full_dll);

    // Unload driver regardless of result
    color(CLR_YELLOW); printf("[*] Unloading Intel driver...\n");
    intel::UnloadDriver();
    color(CLR_GREEN); printf("[+] Intel driver unloaded and traces cleaned\n");

    if (result) {
        color(CLR_GREEN);
        printf("\n[+] === SUCCESS ===\n");
        printf("[+] DLL mapped at 0x%llX in %s (PID %lu)\n", result, proc_name, pid);
    } else {
        color(CLR_RED);
        printf("\n[-] === FAILED ===\n");
        printf("[-] Manual map failed\n");
    }

    color(CLR_WHITE);
    printf("\nPress any key to exit...\n");
    system("pause >nul");
    return result ? 0 : 1;
}

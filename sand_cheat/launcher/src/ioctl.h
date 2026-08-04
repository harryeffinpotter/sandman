// ioctl.h -- iqvw64e.sys (Intel NAL) physmem wrapper.
//
// Public API: read_physical, write_physical, get_pml4_phys.

#pragma once

#include <cstdint>
#include <windows.h>

namespace ioctl {

constexpr DWORD IOCTL_NAL = 0x80862007;

#pragma pack(push, 1)
struct MapIoSpace {
    uint64_t case_number;       // 0x19
    uint64_t reserved1;
    uint64_t return_value;
    uint64_t return_virt_addr;
    uint64_t phys_addr;
    uint64_t size;
};

struct UnmapIoSpace {
    uint64_t case_number;       // 0x1A
    uint64_t reserved1;
    uint64_t reserved2;
    uint64_t virt_addr;
    uint64_t reserved3;
    uint64_t size;
};

struct NalCopyMem {
    uint64_t case_number;       // 0x21
    uint64_t reserved;
    uint64_t source;
    uint64_t destination;
    uint64_t length;
};

struct GetPhysAddr {
    uint64_t case_number;       // 0x25
    uint64_t reserved;
    uint64_t return_phys_addr;
    uint64_t virt_addr;
};
#pragma pack(pop)

bool read_physical(HANDLE device, uint64_t phys, void* dst, size_t size);
bool write_physical(HANDLE device, uint64_t phys, const void* src, size_t size);
bool get_pml4_phys(HANDLE device, uint64_t& cr3);

} // namespace ioctl

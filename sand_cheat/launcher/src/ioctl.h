// ioctl.h -- cpuz.sys physmem wrapper.
//
// Public API: read_physical, write_physical, get_pml4_phys.

#pragma once

#include <cstdint>
#include <windows.h>

namespace ioctl {

constexpr DWORD IOCTL_CPUZ_READ  = 0x9C402428;
constexpr DWORD IOCTL_CPUZ_WRITE = 0x9C402430;

#pragma pack(push, 1)
struct CpuzReadWriteInput {
    DWORD_PTR address;
    DWORD     length;
    DWORD_PTR buffer;
    DWORD     pad;
};
#pragma pack(pop)

bool read_physical(HANDLE device, uint64_t phys, void* dst, size_t size);
bool write_physical(HANDLE device, uint64_t phys, const void* src, size_t size);
bool get_pml4_phys(HANDLE device, uint64_t& cr3);

} // namespace ioctl

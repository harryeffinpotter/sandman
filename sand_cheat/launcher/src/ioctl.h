// ioctl.h -- iQVW64.SYS (Intel NAL) physmem wrapper.
//
// Public API: read_physical, write_physical, get_pml4_phys.

#pragma once

#include <cstdint>
#include <windows.h>

namespace ioctl {

constexpr DWORD IOCTL_NAL = 0x80862007;
constexpr uint64_t NAL_CASE_COPY  = 0x33;
constexpr uint64_t NAL_CASE_MAP   = 0x19;
constexpr uint64_t NAL_CASE_UNMAP = 0x1A;

#pragma pack(push, 1)
struct NalCopyInput {
    uint64_t case_number;
    uint64_t reserved;
    uint64_t source;
    uint64_t destination;
    uint64_t length;
};
#pragma pack(pop)

bool read_physical(HANDLE device, uint64_t phys, void* dst, size_t size);
bool write_physical(HANDLE device, uint64_t phys, const void* src, size_t size);
bool get_pml4_phys(HANDLE device, uint64_t& cr3);

} // namespace ioctl

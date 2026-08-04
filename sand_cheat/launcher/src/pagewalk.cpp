#include "pagewalk.h"

#include "ioctl.h"

namespace pagewalk {

namespace {

constexpr uint64_t PTE_PRESENT = 1ULL << 0;
constexpr uint64_t PTE_PS      = 1ULL << 7;  // page size (1GB / 2MB large page)

} // namespace

bool va_to_phys(HANDLE device, uint64_t cr3, uint64_t va, uint64_t& out_phys) {
    uint64_t table_phys = cr3 & 0xFFFFFFFFFF000ULL;  // page-align CR3
    int shift = 39;  // PML4

    while (shift > 3) {
        uint64_t index = (va >> shift) & 0x1FF;
        uint64_t entry_phys = table_phys + 8 * index;

        uint64_t pte = 0;
        if (!ioctl::read_physical(device, entry_phys, &pte, sizeof(pte))) return false;

        if ((pte & PTE_PRESENT) == 0) return false;  // not mapped

        // Large-page early exits.
        if ((pte & PTE_PS) != 0) {
            if (shift == 30) {  // 1 GB page
                out_phys = (pte & 0xFFFFFC0000000ULL) | (va & 0x3FFFFFFFULL);
                return true;
            }
            if (shift == 21) {  // 2 MB page
                out_phys = (pte & 0xFFFFFFFE00000ULL) | (va & 0x1FFFFFULL);
                return true;
            }
            // PS at PML4 is invalid on current x64; treat as failure.
            return false;
        }

        table_phys = pte & 0xFFFFFFFFFF000ULL;
        shift -= 9;
    }

    // Reached PT level (shift=12 decremented to 3 after last iteration).
    out_phys = table_phys | (va & 0xFFFULL);
    return true;
}

bool va_to_pte_phys(HANDLE device, uint64_t cr3, uint64_t va, uint64_t& out_pte_phys) {
    uint64_t table_phys = cr3 & 0xFFFFFFFFFF000ULL;
    int shift = 39;

    while (shift >= 12) {
        uint64_t index = (va >> shift) & 0x1FF;
        uint64_t entry_phys = table_phys + 8 * index;

        uint64_t pte = 0;
        if (!ioctl::read_physical(device, entry_phys, &pte, sizeof(pte))) return false;
        if ((pte & PTE_PRESENT) == 0) return false;

        if ((pte & PTE_PS) != 0 || shift == 12) {
            out_pte_phys = entry_phys;
            return true;
        }

        table_phys = pte & 0xFFFFFFFFFF000ULL;
        shift -= 9;
    }
    return false;
}

} // namespace pagewalk

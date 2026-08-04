// pagewalk.h — 4-level x64 page-table walker (PML4 -> PDPT -> PD -> PT).
//
// Translates any VA (kernel or user) to physical using CR3 as the starting
// table base. All PTE reads go through BYOVD physical R/W (ioctl::read_physical).
//
// Handles:
//   1 GB pages (PS bit set at PDPT level, shift = 30)
//   2 MB pages (PS bit set at PD level,   shift = 21)
//   4 KB pages (normal, shift = 12)
//
// Mirrors launcher's sub_7FFAB78A99E0 semantics.

#pragma once

#include <cstdint>
#include <windows.h>

namespace pagewalk {

// Translate VA using given CR3 (PML4 physical base). Returns true + phys on
// success; false if the VA is not mapped.
bool va_to_phys(HANDLE device, uint64_t cr3, uint64_t va, uint64_t& out_phys);

// Walk to the leaf page-table entry for VA and return the PHYSICAL address
// of that entry (not of the page it maps). Lets callers read/modify/write
// the PTE bits directly via ioctl::read_physical / write_physical.
// On 1GB / 2MB large-page mappings, returns the address of the PDPTE / PDE
// that carries the PS bit.
bool va_to_pte_phys(HANDLE device, uint64_t cr3, uint64_t va, uint64_t& out_pte_phys);

} // namespace pagewalk

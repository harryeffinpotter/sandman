// kern_map.h — manual-mapping of a PE image into allocated kernel memory.
// Phase 8 (relocations, in-memory on local buffer) + Phase 9 (section copy
// to kernel VA via physmem R/W). Page protection is a separate phase.

#pragma once

#include <cstdint>
#include <cstddef>
#include <windows.h>

namespace kern_map {

// Applies IMAGE_REL_BASED_HIGHLOW / DIR64 fixups to pe_buf_mut in place,
// using delta = kernel_base - PE.OptionalHeader.ImageBase.
// No-op if relocations are stripped (IMAGE_FILE_RELOCS_STRIPPED) or if the
// .reloc directory is empty. Returns (applied_count, ok).
bool process_relocations(uint8_t* pe_buf_mut, size_t pe_size,
                         uint64_t kernel_base, int& out_fixups_applied);

// Copies non-.reloc sections from pe_buf to kernel_base + section.VA,
// page-by-page, via pagewalk + ioctl::write_physical.
bool copy_sections(HANDLE device, uint64_t cr3,
                   const uint8_t* pe_buf, size_t pe_size,
                   uint64_t kernel_base);

// Reads back `bytes` bytes from kernel_base + offset, compares against pe_buf
// at the corresponding section source. Returns true if match.
bool verify_bytes_at(HANDLE device, uint64_t cr3,
                     const uint8_t* pe_buf, size_t pe_size,
                     uint64_t kernel_base, uint32_t rva, size_t bytes);

struct ImportResolveStats {
    int dll_count;
    int function_count;
    int failed_count;
};

// Phase 10 — set PTE protection bits (NX + R/W) per section, matching the
// section's IMAGE_SCN_MEM_* characteristics. Necessary because
// MmAllocateIndependentPages returns pages as RW + NX; .text needs executable
// permission before DriverEntry is invoked, else #PF → bugcheck 0xFC.
// Implemented by direct PTE manipulation via physmem writes (no syscall
// hijack needed, no pattern scan for MiProtectVirtualMemory).
bool apply_section_protection(HANDLE device, uint64_t cr3,
                              const uint8_t* pe_buf, size_t pe_size,
                              uint64_t kernel_base);

// Phase 11 — walk the mapped driver's IMAGE_DIRECTORY_ENTRY_IMPORT, for each
// imported function look up its address in the target kernel module's export
// directory, and write the resolved VA back into the kernel-side IAT slot via
// write_kva. Empty import directory is treated as success with zero counts.
bool resolve_imports(HANDLE device, uint64_t cr3,
                     const uint8_t* pe_buf, size_t pe_size,
                     uint64_t kernel_base,
                     ImportResolveStats& stats);

} // namespace kern_map

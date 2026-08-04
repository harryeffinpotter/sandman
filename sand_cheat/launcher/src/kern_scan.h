// kern_scan.h — ntoskrnl PE section parsing + byte-pattern scanner via the
// physmem R primitive. Used by syscall-hijack target resolution.

#pragma once

#include <cstdint>
#include <windows.h>

namespace kern_scan {

struct SectionRange {
    uint64_t va_start;
    uint64_t va_end;
    char     name[16];
};

int find_sections(HANDLE device, uint64_t cr3, uint64_t mod_base,
                  SectionRange* out, int max_sections);

const SectionRange* section_by_name(const SectionRange* sects, int n, const char* name);

// Scan a kernel VA range for `pattern` of length `len`. `mask[i] == 0` means
// byte i is a wildcard (??); any other value means the byte must match exactly.
// Returns the first match VA, or 0 if no match.
uint64_t scan_pattern(HANDLE device, uint64_t cr3,
                      uint64_t va_start, uint64_t va_end,
                      const uint8_t* pattern, const uint8_t* mask, size_t len);

// Backward variant: returns the LAST (highest-VA) match in [va_start, va_end).
// Useful for "find the nearest preceding anchor" searches like sample's
// ci.dll lock lookup (scan 30 bytes before head-match for a 3-byte LEA).
uint64_t scan_pattern_backward(HANDLE device, uint64_t cr3,
                               uint64_t va_start, uint64_t va_end,
                               const uint8_t* pattern, const uint8_t* mask, size_t len);

// Read the 5-byte E8 rel32 call at `call_addr_kva` and return the absolute
// target VA. Returns 0 on read failure or if the first byte isn't E8.
uint64_t resolve_rel32_call(HANDLE device, uint64_t cr3, uint64_t call_addr_kva);

// Read a 7-byte `48 8D 0D XX XX XX XX` (lea rcx, [rip+rel32]) at lea_addr_kva
// and return the absolute target VA. Returns 0 on read failure or if opcode
// bytes don't match. Also accepts other 3-byte LEA prefixes — pass the
// expected prefix (e.g. {0x48, 0x8D, 0x0D} for lea rcx; {0x48, 0x8D, 0x15} for
// lea rdx; {0x4C, 0x8D, 0x0D} for lea r9; etc.).
uint64_t resolve_rel32_lea_target(HANDLE device, uint64_t cr3,
                                  uint64_t lea_addr_kva,
                                  uint8_t opcode0, uint8_t opcode1, uint8_t opcode2);

// Convenience: scan a named section of a module for a byte pattern.
// Returns the first match VA, or 0 if the section isn't found or pattern doesn't match.
uint64_t scan_pattern_in_section(HANDLE device, uint64_t cr3, uint64_t mod_base,
                                 const char* section_name,
                                 const uint8_t* pattern, const uint8_t* mask, size_t len);

// Resolve a loaded kernel module's base VA by basename (case-insensitive).
// Uses K32EnumDeviceDrivers + K32GetDeviceDriverBaseNameA. Returns 0 if not
// found or if the lookup APIs are unavailable.
uint64_t resolve_loaded_driver_base(const char* basename);

// Public wrapper over the internal kernel-VA reader. Loops page-by-page via
// pagewalk + ioctl::read_physical. Returns false on any page fault in the walk
// or any BYOVD read failure.
bool read_kva(HANDLE device, uint64_t cr3, uint64_t kva, void* dst, size_t size);

} // namespace kern_scan

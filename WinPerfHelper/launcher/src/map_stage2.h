// map_stage2.h — Phase 16.c.d: header-less cross-proc section writer +
// per-section protection for the prepared Stage 2 PE buffer.
//
// Called after 16.c.a (parse), 16.c.b (IAT resolution in-buffer), and
// kern_map::process_relocations (DIR64 fixups in-buffer with delta
// chosen_base - image_base). The buffer is now self-contained; this
// phase writes the copy-flagged sections into target memory at their
// RVAs and applies per-section page protections.
//
// No DOS/NT headers written. No section table written. Target memory
// range [base, base + sections[0].VA) is left as-is (RTSS .data bytes
// the parking probe already verified zero-ish). First written byte
// lands at base + sections[0].VirtualAddress (typically 0x1000).

#pragma once

#include <cstdint>
#include <cstddef>
#include "parse_stage2.h"

namespace map_stage2 {

// Writes each copy=true section into the target process at
// `target_base + section.virtual_address`, then applies per-section
// protection. Protection policy favours blending with surrounding
// parking .data: non-exec sections stay PAGE_READWRITE (matches the
// RTSS .data baseline), exec sections flip to PAGE_EXECUTE_READWRITE
// (same protection the interop-smoke parking stub uses, so a scanner
// that baselines RTSS's parking region during the smoke would see the
// same anomaly shape).
bool write_and_protect(uint32_t pid, uint64_t target_base,
                       const uint8_t* pe_buf, size_t pe_size,
                       const parse_stage2::parsed_stage2& parsed,
                       bool skip_pte = false);

} // namespace map_stage2

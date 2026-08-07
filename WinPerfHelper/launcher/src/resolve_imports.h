// resolve_imports.h — Phase 16.c.b: Stage 2 IAT resolution via cross-proc
// export-table lookup in the target game process.
//
// Strategy:
//   - Iterate Stage 2's IMAGE_IMPORT_DESCRIPTOR table
//   - For each referenced DLL: cmdchannel::find_module to get its base in
//     the target's PEB.Ldr, then one cross-proc read pulls enough of the
//     module image to parse its export directory + all names
//   - Cache each module (base + image bytes + export refs) so repeat
//     symbol lookups in the same DLL don't re-read from target memory
//   - Walk IAT (OriginalFirstThunk for names, write into FirstThunk)
//     patching resolved VAs into the launcher-side `pe_buf_mut` buffer
//   - Forwarded exports (e.g. kernel32!VirtualAlloc → kernelbase!VirtualAlloc)
//     are followed transparently, bounded by a depth cap to avoid loops
//
// Zero writes to the target process — all patches land in the local buffer.
// The header-less section writer (16.c.d) copies the patched buffer later.

#pragma once

#include <cstdint>
#include <cstddef>
#include "parse_stage2.h"

namespace resolve_imports {

struct stats {
    int dlls_found     = 0;
    int dlls_missing   = 0;
    int symbols_ok     = 0;
    int symbols_missed = 0;
    int forwarders     = 0;
};

// Patches IAT slots in `pe_buf_mut` (launcher-side Stage 2 buffer) using
// resolved VAs from the target process's loaded modules. Returns true iff
// every import descriptor matched a loaded DLL and every symbol resolved.
// On any failure, partial patches may have been applied — buffer is still
// usable for debugging but Stage 2 should not be executed.
bool resolve(uint32_t pid,
             uint8_t* pe_buf_mut, size_t pe_size,
             const parse_stage2::parsed_stage2& parsed,
             stats& out);

} // namespace resolve_imports

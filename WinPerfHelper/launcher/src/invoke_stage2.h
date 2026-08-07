// invoke_stage2.h — Phase 16.c.e: DllMain-invoker shellcode + interop
// vtable redirect.
//
// Preconditions:
//   - Stage 2 already written to parking at `stage2_base` via
//     map_stage2::write_and_protect (16.c.d complete)
//   - rtss_inject::Ctx populated (parking zone known, interop path live)
//
// Action:
//   - Pick a separate ~1 KB parking region (non-overlapping with Stage 2)
//   - Stage fake vtable + DllMain-invoker shellcode + marker
//   - Flip parking RWX; redirect EA90[live_slot] = fake_vtable_va
//   - Next render tick, RTSS calls method +0x20 on fake vtable →
//     shellcode runs on game's render thread:
//         RtlAddFunctionTable(.pdata, count, stage2_base)
//         DllMain(stage2_base, DLL_PROCESS_ATTACH, NULL) → returns
//         marker = 0xCAFEBABE
//         xor eax,eax ; ret 0   (RTSS expects 0 from this method)
//   - Launcher polls marker; on 0xCAFEBABE → DllMain returned cleanly
//   - Restore EA90; scrub + de-RWX invoker parking ONLY
//
// Stage 2 region is NOT scrubbed — DllMain has run, installed the E900
// detour in the game's widget dispatcher, set g_InitState = PROBING.
// Subsequent paints drive the state machine to VMT-shadow install.

#pragma once

#include <cstdint>
#include "rtss_inject.h"

namespace invoke_stage2 {

// `stage2_base` is the parking VA where map_stage2::write_and_protect was
// called. `stage2_size` is the page-aligned size_of_image, used only for
// overlap avoidance when picking the invoker parking slot. `entry_rva` is
// parsed_stage2::entry_rva (AddressOfEntryPoint — i.e. DllMain RVA).
bool invoke_dllmain(uint32_t game_pid,
                    const rtss_inject::Ctx& ctx,
                    uint64_t stage2_base,
                    uint32_t stage2_size,
                    uint32_t entry_rva,
                    uint32_t exception_rva,
                    uint32_t exception_size);

} // namespace invoke_stage2

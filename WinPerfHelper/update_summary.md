# Sand Cheat — Post-Update Fix Summary

## What Broke (August 2026 Game Update)

Game updated, cheat crashed instantly on inject. No worker thread output, no overlay, just immediate process termination within milliseconds of injection.

## Root Cause: BEDaisy.sys Kernel Hook on ZwProtectVirtualMemory

BattlEye's kernel driver (`BEDaisy.sys`) intercepts `ZwProtectVirtualMemory` at the kernel level. Any call that sets `PAGE_EXECUTE_*` protection on `MEM_PRIVATE` memory triggers immediate detection and process kill — even if no code ever executes from that memory. This was proven via a diagnostic test where DllMain did absolutely nothing (no thread, no hooks, just returned TRUE) and the game still crashed, confirming the detection was on the memory protection change itself, not on code execution.

### Previous flow (detected):
1. `alloc_memory` with `PAGE_READWRITE` (or `PAGE_EXECUTE_READWRITE`)
2. Write PE sections
3. `protect_memory` → calls `ZwProtectVirtualMemory` with `PAGE_EXECUTE_READ` on `.text`
4. **BE intercepts step 3 at kernel level → process killed**

### Fixed flow (current, working):
1. `alloc_memory` with `PAGE_READWRITE` (0x04) — no execute flags
2. Write PE sections via `write_memory`
3. Skip `protect_memory` entirely (`write_and_protect` called with `skip_pte=true`)
4. `set_pte_nx` with `PTE_FLAG_CLEAR_NX` (0x1) — clears NX bit at PTE level via physical memory R/W
5. `invoke_dllmain` as normal

This works because `set_pte_nx` manipulates page table entries through the BYOVD physical memory channel (WinIo64.sys), completely bypassing BE's kernel hook. The VAD still says `PAGE_READWRITE` (looks like normal data allocation to any memory scanner), but the PTE has NX cleared so the CPU treats it as executable.

## Files Changed

### `launcher/src/main.cpp`
- `write_and_protect` called with `skip_pte=true` (skips the `protect_memory` loop)
- Added PTE NX clear loop after readback verification, before `invoke_dllmain`:
  - Iterates all sections where `s.copy && s.exec`
  - Calls `cmdchannel::set_pte_nx(game_pid, dst, size, 0x1)` per section
  - Page-aligns sizes with `(vext + 0xFFF) & ~0xFFFu`

### `src/main.cpp`
- CreateThread restored in DllMain (was temporarily switched to QueueUserWorkItem for testing, then to diagnostic no-op mode)
- Extensive `tlog` diagnostic logging throughout worker thread, overlay init, hook installation

### `src/overlay.cpp`
- DXGI RVAs confirmed correct at `0xDAD0` (Present) and `0x388C0` (Resize) — these are dxgi.dll system offsets, they do NOT change with game updates
- Granular `tlog` logging added to relay stub writes, trampoline builds

## What Changes When the Game Updates (and what doesn't)

### Will NOT change (OS/driver level):
- DXGI RVAs (`0xDAD0`, `0x388C0`) — these are Windows system DLL offsets
- BYOVD pipeline (WinIo64.sys, kernel driver, HAL dispatch hook)
- RTSS injection method (parking in .data gap)
- PTE NX clear mechanism
- Command channel syscall (NtConvertBetweenAuxiliaryCounterAndPerformanceCounter)

### WILL change (game-specific, GameAssembly.dll):
- IL2CPP method RVAs (Execute, get_transform, get_position, etc.)
- Class/method string names if devs refactor
- Execute fallback RVA (currently `0x4BCD440` in main.cpp)
- Any hardcoded GameAssembly.dll offsets

### MIGHT change:
- BE detection methods — they could start walking page tables for MEM_PRIVATE, which would catch PTE/VAD mismatch. Fallback: PTE flickering (clear NX only during brief execution bursts, re-set NX between frames)
- Unity/IL2CPP version bump could shift internal structures

## Debugging Checklist (When It Breaks Again)

1. **Build both** — DLL (`build.ps1`) and launcher (`build_launcher.ps1`)
2. **Run launcher, inject into game**
3. **Check `launcher_trace.txt`** — did all phases pass? Look for FAIL lines
4. **Check `injection_trace.txt`** — did DllMain fire? Did worker thread start?
5. **If crash before any trace output** — detection is pre-execution (memory allocation or PTE level). BE may have added PTE scanning
6. **If DllMain fires but worker crashes** — IL2CPP offsets changed. Check `worker_debug.txt` for which resolve failed
7. **If worker runs but no overlay** — DXGI hook issue. Check overlay tlog output. Unlikely unless Windows updates dxgi.dll
8. **If overlay shows but ESP broken** — game-side offsets changed. Need fresh il2cpp dump

## Approaches Tested (Ranked by Survival Time)

| # | Approach | Result |
|---|----------|--------|
| 1 | **Standalone RW + PTE NX clear** | **WORKING** — current approach |
| 2 | Standalone RW → protect_memory to RX | Killed — BE intercepts ZwProtectVirtualMemory |
| 3 | Standalone RWX (PAGE_EXECUTE_READWRITE) | Killed — giant RWX blob detected |
| 4 | RTSS .data + PTE NX clear | Killed — PTE/VAD mismatch on MEM_IMAGE detected |
| 5 | RTSS .data + protect_memory/VAD split | Fastest kill — VAD split on MEM_IMAGE instant flag |

## Nuclear Option (If PTE NX Clear Stops Working)

If BE starts scanning page tables for MEM_PRIVATE regions:
- **PTE flickering**: clear NX only during the brief execution window (one frame), re-set NX between frames. Timing-based evasion.
- **MEM_MAPPED via NtMapViewOfSection**: different memory type might have different scanning rules
- **Hypervisor-level**: EPT manipulation to hide execute permissions entirely (major escalation in complexity)

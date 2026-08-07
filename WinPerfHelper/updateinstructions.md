# Sand Raiders — Post-Update Fix Checklist

Every time Sand Raiders updates, the game binaries get recompiled and hardcoded offsets go stale. This doc covers what to check and fix.

---

## Files With Game-Version-Specific Values

| File | What | Location |
|---|---|---|
| `src/overlay.cpp` | DXGI Present / ResizeBuffers RVAs | ~lines 2159-2160 |
| `src/main.cpp` | Execute method fallback RVA | ~line 437 |
| `src/cheat.cpp` | Game struct offsets | various |

---

## Step-by-Step Fix Process

### 1. Update DXGI Hook RVAs (`src/overlay.cpp`)

The hardcoded RVAs for `dxgi.dll!Present` and `dxgi.dll!ResizeBuffers` change when the system or game ships a new `dxgi.dll`.

- Look around lines 2159-2160 in `overlay.cpp`.
- Use a disassembler to find the new offsets for Present and ResizeBuffers in `dxgi.dll`.
- If RTSS has already hooked them, the first byte will be `E9` (JMP). The overlay code checks for this and falls back to a vtable scan, but the fast-path RVAs should still be updated.

Example (last update):
```
Old: 0xDAD0 (Present), 0x388C0 (Resize)
New: 0x31000 (Present), 0x2F860 (Resize)
```

### 2. Update Execute Method Fallback RVA (`src/main.cpp`)

The `FindInteractTargetSystem.Execute` RVA changes when `GameAssembly.dll` is recompiled.

- Look around line 437 in `main.cpp`.
- The cheat tries dynamic resolution via IL2CPP reflection first. The fallback RVA is only used if that fails.
- To find the new value: let dynamic resolution succeed once, then check `worker_debug.txt` for the logged address. Subtract the `GameAssembly.dll` base address to get the RVA.

Example (last update):
```
Old: 0x4BBDA10
New: 0x4BCD440
```

### 3. Check Game Struct Offsets (`src/cheat.cpp`)

If the game changed its data structures (added fields, reordered members), the offsets in `cheat.cpp` will be wrong. This doesn't happen every update, but it does happen on major patches.

### 4. Build and Test

```powershell
# Build the DLL
cd WinPerfHelper
powershell -File build.ps1

# Build the launcher
cd WinPerfHelper/launcher
powershell -File build_launcher.ps1
```

- Inject and check `injection_trace.txt` and `launcher_trace.txt` for diagnostics.
- Heartbeat logging in the main scan loop fires every 10 iterations, so silence there means a crash or hang.

---

## BattlEye Memory Strategy (Reference)

If BE detection changes in a future update, the memory allocation strategy in `launcher/src/main.cpp` and `launcher/src/map_stage2.cpp` may need adjustment.

Current approach that works:

1. Allocate memory as `PAGE_READWRITE` (0x04) via `cmdchannel::alloc_memory` with `skip_pte=false`.
2. Write all PE sections into the allocation.
3. Flip `.text` sections to `PAGE_EXECUTE_READ` (0x20) via `cmdchannel::protect_memory`.

This mimics legitimate JIT/DLL loading. On `MEM_PRIVATE` memory, `protect_memory` behaves normally (no VAD splits).

Approaches that get detected:

- **PTE vs VAD mismatch** — PTE says executable but VAD says `PAGE_READWRITE` (the old RTSS parking trick).
- **VAD splitting** — Using `ZwProtectVirtualMemory` on `MEM_IMAGE` regions creates detectable splits. Worse than PTE mismatch.
- **RWX memory** — `PAGE_EXECUTE_READWRITE` private allocations are flagged.

---

## Crash Protection

`safe_find_execute()` and `safe_overlay_init()` in `main.cpp` wrap the relevant calls with `__try/__except` so stale RVAs cause graceful failures and log output instead of silent crashes. These are already in place and don't need to be re-added.

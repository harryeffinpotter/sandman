# Sand External Architecture

**KWARE-style ban avoidance rewrite.** Complete pivot from injected DLL
+ HWBP hooks to a zero-footprint external overlay driven by the
launcher's already-loaded kernel driver.

## Threat model

Old architecture (banned):
- `sand_cheat.dll` manually mapped into `sand.exe`
- DR0-DR3 hardware breakpoints on `FindInteractTargetSystem.Execute`
- Present hook via disk trampoline patching `dxgi.dll` vtable
- RWX relay page in `sand.exe`

Every one of those is a documented BE detection vector. Any one alone
was enough to earn a ban.

New architecture:
- Game process is UNTOUCHED. Zero pages allocated, zero patches, zero
  HWBP, zero threads created.
- All memory access flows through the launcher's mapped kernel driver
  via `cmdchannel` (HAL-hijacked syscall). BE cannot observe our reads
  because they happen at `MmCopyVirtualMemory` under a hijacked kernel
  callsite, not through `NtReadVirtualMemory` (which BE hooks).
- Overlay renders in its own top-level window using
  `SetWindowDisplayAffinity(WDA_MONITOR)` — stream-proof at the DWM
  compositor. Screen capture APIs see black in our region.

## Runtime

```
[game process — sand.exe]
        ^
        | R/W via MmCopyVirtualMemory
        |
[kernel driver — mapped by sand_launcher.exe once per boot]
        ^
        | cmdchannel syscall (HAL hijack)
        |
[external overlay — PerfMonSvc.exe / sand_external.exe]
        - own transparent WDA_MONITOR window
        - ImGui + DirectComposition
        - runs entity scan every 200ms
        - runs write ops every scan tick
```

## Files

### `sand_cheat/external/`
- `src/main.cpp` — WinMain, bootstrap, main loop
- `src/overlay.cpp` — stream-proof window (D3D11 + DirectComposition + ImGui)
- `src/scan.cpp` — entity scanner + SlimDict lookup + component discovery
- `src/finder.cpp` — memory search primitives + GCM auto-discovery
- `src/writeops.cpp` — game state mutations
- `src/ui.cpp` — ImGui widgets
- `src/state.cpp` — shared runtime state
- `src/config.cpp` — INI settings persistence
- `build.ps1` — build script (produces `sand_external.exe` + `PerfMonSvc.exe`)
- `README.md` — user-facing docs

### `sand_cheat/launcher/`
- `src/main.cpp` — accepts `--no-inject` flag to skip DLL, driver-only mode
- everything else unchanged — BYOVD, cmdchannel, kern_map, etc.

## Deploy workflow

```
# One time per boot
sand_launcher.exe --no-inject
  -> installs vulnerable driver
  -> maps custom kernel driver
  -> hijacks NtConvert... syscall
  -> exits (does NOT touch game)

# Any time after
PerfMonSvc.exe
  -> attaches to sand.exe
  -> auto-discovers GameContextModule
  -> renders overlay in own window
  -> runs entity scan + write ops
```

## Files on disk during operation

- `%APPDATA%\Microsoft\PerfCache\perfmon.log` — trace file
- `%APPDATA%\Microsoft\PerfCache\perfmon.ini` — config (GCM addr,
  UI state, self entity id)

Both look like legit Microsoft caches. Nothing under `sand_cheat/` is
touched at runtime.

## Features

- Overview: attach status, module bases, frame stats
- Memory viewer: hex dump any address in game process
- Entity list: name / distance / HP with filter + sort + selectable rows
- Radar: top-down mini-map with range/category filters, color-coded dots
- Write ops: force interact target, turret rapid fire, turret no recoil
- Manual self designation: double-click any entity or right-click →
  "Set as SELF", persisted to config
- GCM auto-discovery via memory scan, saved to config for next launch

## What survives from the old DLL

- IL2CPP class layout knowledge (offsets — stable per game version)
- SlimDict walker logic (ported to external reads)
- Component name discovery pattern
- Blueprint / Position / HealthData layouts
- Turret / weapon write patterns

## What's gone from the old DLL

- HWBP infrastructure
- Present/vtable hooks
- Manual mapping
- Injected worker thread
- Every RWX page
- Every il2cpp method call from foreign thread

The DLL still builds (see `sand_cheat/src/`) and the launcher without
`--no-inject` still injects it — kept as legacy fallback until the
external is battle-tested. Real deploys use `--no-inject` + external only.

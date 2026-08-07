# PerfMonSvc

KWARE-style external overlay for sand.exe. Zero DLL injection, zero HWBP,
zero vtable patches — game process untouched. Reads/writes game memory via
the launcher's already-loaded kernel driver over cmdchannel (HAL-hijacked
syscall).

## Prerequisites

1. Run `RTSSDriverSvc.exe` at least once this boot session to install the
   vulnerable driver and hijack the syscall dispatcher. Injection into
   sand.exe is not required — only the driver install path matters.
2. `sand.exe` must be running.

## Run

```
cd WinPerfHelper\external
.\build.ps1
.\PerfMonSvc.exe
```

## Hotkeys

- **INSERT** — toggle click-through / interactive mode. Interactive lets
  you click on widgets; click-through passes input to the game.
- **HOME** — toggle menu visibility.

## First-run bootstrap

1. External attaches to sand.exe automatically.
2. Click "Auto-discover" in the entities widget — it scans game memory
   for the `GameContextModule` klass + isolates the singleton instance.
3. Once resolved, scan starts. Entity list + radar populate.
4. Address is saved to `external_config.ini` for next run.

## Architecture

```
[game process — sand.exe]  <-- untouched, no injection
        ^
        | R/W via
        | MmCopyVirtualMemory
        |
[kernel driver — mapped by launcher]  <-- HAL-hijack dispatcher
        ^
        | cmdchannel syscall
        |
[PerfMonSvc.exe]  <-- our overlay, its own window, stream-proof
```

Overlay window uses `SetWindowDisplayAffinity(WDA_MONITOR)` so OBS Game/
Display Capture and DXGI Desktop Duplication see black in our region.
Truly stream-proof at the compositor level.

## Files

- `src/main.cpp` — WinMain, message pump, main loop
- `src/overlay.cpp` — stream-proof window + D3D11 + DirectComposition + ImGui
- `src/scan.cpp` — external entity scanner, SlimDict lookup, component discovery
- `src/finder.cpp` — memory search primitives + GCM auto-discovery
- `src/ui.cpp` — ImGui widgets (overview, memory viewer, entity list, radar)
- `src/state.cpp` — shared runtime state
- `src/config.cpp` — INI settings persistence

## Build requirements

- MSVC 14.51+ (Visual Studio 2022+)
- Windows SDK 10.0.26100.0
- Existing `WinPerfHelper/launcher/src/` for cmdchannel.cpp
- Existing `WinPerfHelper/imgui/` for ImGui sources

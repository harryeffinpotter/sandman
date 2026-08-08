# WinPerfHelper — Status

Runtime instrumentation and telemetry module for a Unity IL2CPP application. Injected via RTSS plugin pathway. Exposes a live in-process overlay for inspecting ECS entities, capturing outbound network messages, and orchestrating scripted test scenarios.

## Working

- **Injection + overlay** — DLL loads under RTSS, hooks DXGI Present, renders ImGui overlay on top of the host application.
- **Entity enumeration** — walks the game's Entitas GameContextModule every ~500 ms, resolves component indices by name at boot, per-entity SEH scope so a bad entity in the pool skips instead of aborting the whole scan.
- **Massive reflection dump** — one-shot at boot: every klass in every assembly (37K+), full parent chain, all fields with types, all method signatures. Second dump: every entity in current scan, every component slot, every field walked via IL2CPP reflection. Both greppable text.
- **Live overlay** — searchable entity list, right-click context menu (hide by name / hide by prefix / copy to clipboard), category filters (world entities vs container children), custom colors per loot tier.
- **User identity resolution** — Steam API `GetFriendPersonaName` + `RequestUserInformation` for lobby members. Falls back to a synthetic `Player[<accountId>]` label if Steam lookup returns empty. Retries empty results next scan.
- **Character skeleton overlay** — recursive Transform hierarchy walker with named-bone matching AND a raw-position fallback (point cloud when name lookup finds nothing). Overlay renders every populated slot 0-54, not just canonical joints.
- **Message capture pipeline** — trampoline hooks on `HoloMessengerModule.Publish` plus the `ClientNetworkControllerModule.Send*` extension methods (`SendMoveSlot`, `SendSplitSlot`, `SendEquip`, `SendDrop`). Every outbound message logs to ring buffer + optional file mirror.
- **Scenario recorder / player** — named in-memory buffers snapshot the raw HoloMessage bytes on capture. Playback allocates a fresh IL2CPP object of the recorded klass, memcpys the snapshot over its instance data, dispatches through the same Publish path.
- **Countdown scheduler** — every button in the scenario tab can arm-and-wait N seconds so the operator can close the menu and get back to the game before the action fires (menu-open pauses the host = server refuses to process).
- **Arm-then-hotkey recording** — arm a recording name in UI, press a hotkey (default Del) to start capture, press it again to stop. Menu never needs to be reopened.
- **Rebindable hotkeys** — user-assignable keys for hard-kill (default F12), suspend-scenarios (F9), master-off (F10), playback-first (F7), and record-toggle (Del). Click Rebind, press any key, done.
- **Reactor / extraction / walker component indices** — resolved at boot. Overlay highlights entities that carry those components with distinct labels.
- **Item variant expansion** — 15+ single-click experimental scenario buttons that mutate a selected entity's components (item-type spoof, force-into-hand-slot, strip interactible-not-active) or dispatch specific reconstructed messages (equip N, drop N, move slot, split stack, storm x N, various race combinations).
- **Container-content exposure** — Items panel toggle to show entities that are children of an inventory / container / ship shelf, prefixed with their parent id.
- **Stream-safe overlay** — separate top-level window with DirectComposition compositor + `WDA_EXCLUDEFROMCAPTURE` capture-affinity flag. SEH guard around every backend init/teardown path so a partial-init failure reverts cleanly instead of killing the host.

## Known issues (in priority order)

1. **Stream-safe overlay reliability** — swap-time backend init is still fragile; users report three failure modes (host closes to desktop, overlay renders but invisible to operator, ImGui backend assertion). Rewrite planned: create the overlay window once at boot, never destroy, toggle only flips the capture-affinity flag.
2. **~30-second host stall, once per session, guaranteed** — happens on every launch regardless of what the operator is doing at the time. Current lead: Windows Defender scanning our ~500 MB boot-time reflection dump file. Fix candidates: chunked writes with sleeps, compile-time flag to skip diagnostic dumps, add the appdata folder to Defender exclusion at install.
3. **Shelf-place capture** — `SendMoveSlot` hook is now installed (this build), waiting on operator confirmation that place-on-shelf now shows up in the record log.
4. **UserName resolution via game data** — Steam path works, but recovering the game's own display name for non-Steam identities requires walking `UserContextModule._users` (IGroup) → iterate each UserEntity's component dictionary → find UserNameComponent → read the string field on its `BaseTypeNameComponent<T>` grandparent at offset 0x10. Prior attempts hit C++ exceptions calling `GetEntityWithAccountId` at boot; timing-based retry needed.
5. **Random 30-40 s freezes mid-session** — separate from the guaranteed 20-40 s boot stall. Hypothesized to be the injected worker thread being suspended mid-scan while holding the shared items critical section, blocking the render thread. Fix: shrink critical-section hold time by scanning into a local vector and swap-locking only briefly to move it into the shared vector.

## Cleanup pending for release

- Compile-time flag to disable every diagnostic file write (`perf_all.dat`, `perf_l.dat`, `perf_capture.dat`, `perf_n.dat`, etc). Ship builds should write nothing to disk.
- Runtime opt-in on the reflection dumps (default off; expert operators can enable).
- Sensitive log lines (SEH exception codes, memory addresses, klass pointers) redacted or gated behind a "verbose" flag.

## Not started

- Movement quality-of-life: gravity slider inverse mode (float), speed-capped no-collision movement with WASD in look direction.
- Reactor stability writes on owned ships (max HP maintenance).
- In-storm damage exclusion for owned entities.
- Storm arrival prediction from `SandStormData` / `SandStormDestination` component reads.
- Weapon overheat + rate-of-fire parameter writes on owned stationary weapons.

## Layout

```
WinPerfHelper/
  src/
    main.cpp         entry, hook installers, worker loop, hotkey polling
    win.cpp          scan/entity logic, il2cpp integration, scenario helpers
    win.h            shared types, extern globals
    overlay.cpp      DXGI/ImGui overlay, all UI panels, DirectComposition stream-safe path
    win_console.cpp  standalone console diagnostic build (legacy)
  imgui/             vendored ImGui
  launcher/          driver-install-only launcher binary output
  external/          separate external-process telemetry binary (legacy path)
```

## Build

`build_all.ps1` at project root builds DLL + launcher + external in one shot. Or `build.ps1` for DLL only. Requires MSVC 14.51 + Windows SDK 10.0.26100.

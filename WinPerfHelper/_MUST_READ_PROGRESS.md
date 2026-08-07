# WinPerfHelper — Must-Read Progress Log
**Always read this file first. Updated every code change.**
**Last updated: 2026-08-04 (post-tlog-diagnosis)**

## Current State: BattlEye is TERMINATING the process — NOT a code crash

### CRITICAL FINDING (latest test)
The entire build_disk_trampoline for Present completed successfully:
```
file closed, origBytes=48 89 5C 24 10 48 89 74
dbglog done
starting insn decode loop
stolen=5 bytes
memcpy to tramp=00007FF9EDA40020 done
starting fixup loop
fixup loop done
tramp complete at 00007FF9EDA40020, jmpTarget=00007FF9EDAADAD5
overlay_init: presentTramp=00007FF9EDA40020
                                              ← DIES HERE
```

The process dies between TWO CONSECUTIVE tlog() calls with **ZERO code between them** (lines 2255-2257 of overlay.cpp — just a blank line). This is NOT a code bug.

**Diagnosis:** BattlEye (BEDaisy.sys kernel + BEClient_x64.dll usermode) is detecting our presence and calling `TerminateProcess` from its own thread. Evidence:
1. crash_info.txt is EMPTY — no exception was thrown, VEH never fires
2. The kill point is arbitrary — it depends on when BE's periodic scan runs
3. Previous sessions show the overlay working (overlay_debug.txt has frames rendered) — timing-dependent

**What BE likely detects:**
- RWX page allocation (relay page via VirtualAlloc PAGE_EXECUTE_READWRITE)
- Manually-mapped module not in PEB LdrData module list
- Modified dxgi.dll vtable / Present function
- The BYOVD driver load leaving traces that BEDaisy.sys picks up

### What Works (confirmed by injection_trace.txt)
- DllMain enters cleanly
- Worker thread starts
- VEH crash_handler installed
- Critical section, GameAssembly.dll, resolve_all — all OK
- safe_find_execute finds Execute at correct RVA
- overlay_init: dxgi found, relay page allocated, stubs written
- build_disk_trampoline for Present: file read, insn decode, memcpy, fixup — ALL PASS
- Present trampoline built at 00007FF9EDA40020, 5 stolen bytes, jmpTarget correct

### Chronological Fix History
1. **SEH self-test (REMOVED)** — deliberately null-deref inside __try killed process because .pdata wasn't registered. REMOVED = fixed injection crash.
2. **HWBP hook disable (REMOVED)** — toggling hook off made g_gameContextModule stale. REMOVED = hook stays active.
3. **IsBadReadPtr replaces VirtualQuery** — matches working backup (inline SEH vs syscall).
4. **s_readableCache unordered_map (REMOVED)** — file-scope static with non-trivial constructor doesn't init in manual-mapped DLLs.
5. **scan_entities_fast (REMOVED)** — single scan_entities() now, matching backup.
6. **get_component_fast / dict_slim_lookup_fast (REMOVED)** — diverged from backup.
7. **Early entity filter before blueprint (REMOVED)** — skipping valid entities.
8. **Granular tlog() added everywhere** — worker_thread + build_disk_trampoline fully instrumented.
9. **build_disk_trampoline fully passes** — NOT a code bug. BE terminates externally.

### Next Steps
- **Try different vulnerable driver** — Current WinIo64.sys may be on BattlEye's blocklist. BEDaisy.sys kernel component detects driver loads. Trying iqvw64e.sys (Intel NAL) or RTCore64.sys from Vulnerable Drivers/ folder.
- **Possible: hide module from BE** — PEB unlinking, avoid RWX (use RX+RW separately), delay overlay init
- **Possible: use kernel driver to cloak** — Our mapped kernel driver could hook BE's scan functions

### Architecture (for context recovery)
- IL2CPP Unity game with BattlEye anti-cheat (BEDaisy.sys kernel + BEClient_x64.dll usermode)
- HWBP hooks via DR0 — zero code modification to avoid BE checksums
- Manual DLL mapping via RTSS parking zones (not LoadLibrary)
- .pdata registered via RtlAddFunctionTable in shellcode for SEH
- D3D11 vtable hooking for Present/ResizeBuffers via relay page + disk trampoline
- Backup (il2cpp_dumper/cheat.cpp) uses LoadLibrary + code hooks — works but visible

### Key Files
- `src/main.cpp` — Worker thread, VEH handlers, HWBP hook, main loop
- `src/overlay.cpp` — D3D11 hook, ImGui rendering, build_disk_trampoline
- `src/cheat.cpp` — Entity scanning, component access
- `launcher/src/main.cpp` — Full injection pipeline (BYOVD + RTSS + map + invoke)
- `launcher/src/byovd.cpp` — WinIo64.sys drop/load/unload lifecycle
- `launcher/src/ioctl.cpp` — Physical memory R/W via WinIo64 IOCTLs
- `launcher/src/invoke_stage2.cpp` — Shellcode: RtlAddFunctionTable + DllMain call

### Open Issues
- **BE TERMINATION** — BattlEye kills process during overlay_init (not a code crash)
- **STUTTER** — HWBP overhead (~1200 exception dispatches/sec), architectural
- **IsTooFarAway gone** — game removed method
- **ESP blinking in sky** — not investigated
- **No recoil on 40mm** — not implemented yet
- **Remote interact** — requires internal (injected DLL), cannot do external

### Git State
- `master` branch: committed + pushed (backup parity + SEH self-test removal)
- `vuln-driver-approach` branch: current working branch (tlog additions)

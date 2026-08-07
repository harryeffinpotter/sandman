# OpSec Roadmap

Track-covering plan for sand_cheat, aligned with the kware/larpdll
methodology. Ordered by detection severity, not implementation order.

## Already ported from kware ✓

### Launcher / driver install phase

- **DriverObject BaseDllName pre-zero** — writes UNICODE_STRING.Length = 0
  on our BYOVD's LDR entry before NtUnloadDriver. Trips the
  `MiRememberUnloadedDriver` early-return gate so no
  `MmUnloadedDrivers` entry is ever created. Verified against Win11
  ntoskrnl (kware §3.1).
- **PiDDBCacheTable scrub** — removes our BYOVD's entry via BYOVD
  physmem R/W (kware §3.13).
- **KernelHashBucket scrub** (ci.dll `g_KernelHashBucketList`) —
  substring-match walker + pool free (kware §3.15).
- **Service key delete** — `RegDeleteTreeW` after unload cleans
  registry trail.
- **BYOVD unloaded before injection** — cheat driver stays resident,
  vuln driver is transient. Only one BYOVD load event per boot.

### Runtime cheat behaviour (in DLL — legacy path)

- HWBP hook infrastructure was the primary ban vector; **removed
  entirely** in the external pivot.
- Present hook / DXGI vtable patch was a secondary vector; **removed
  entirely** in the external pivot.

## Added in the external pivot ✓

### Architecture-level

- **Zero code inside game process** — no injection, no HWBP, no
  vtable patches, no RWX pages, no foreign-thread il2cpp calls.
- **All R/W via kernel driver's `MmCopyVirtualMemory`** through the
  HAL-hijacked syscall. BE cannot observe cross-process handle
  operations because we never OpenProcess anything.
- **Stream-proof overlay via `SetWindowDisplayAffinity(WDA_MONITOR)`**
  so screen-capture APIs (OBS Game/Display, DXGI Desktop Duplication,
  PrintScreen) see black in our region.

### Runtime OpSec (this session, `opsec.cpp`)

- **BEDaisy preflight refusal** — matches kware §3.16 WdFilter pattern.
  If BEDaisy.sys is loaded, external refuses to attach with no side
  effects.
- **First-scan settle window** — wait N seconds after attach before
  first game-memory read, so BE's initial-scan window has passed.
- **Jittered scan interval** — `[base_ms, base_ms + jitter_ms]` per
  tick, no clean sine wave for BE to fingerprint over minutes.
- **Silent-mode disk suppression** — one toggle kills all runtime file
  writes (`perfmon.log` + `perfmon.ini`) so a running session leaves
  no disk trail.
- **Innocuous file paths** — `%APPDATA%\Microsoft\PerfCache\` blends
  with legit MS caches, no path under project directory touched.
- **Deploy binary name** — `PerfMonSvc.exe` matches typical MS
  perf-tool naming.

## Not yet ported — future work

### From kware (§3.x still in "Pending" state)

- **WdFilter internal-list scrub** (§3.16) — kware intentionally
  skipped this; instead uses preflight refusal (which we mirror).
- **Event Log 7045/7036 scrubbing** — driver install/uninstall
  service events. Even with `RegDeleteTreeW`, the SCM writes
  event log entries. Scrubbing requires event-log-clear which
  itself generates event 1102.
- **ETW-Threat-Intelligence provider silencing** — disable the
  events that report our driver-load activity to EDRs.
- **USN journal reset** — file-op provenance. Rarely scrubbed
  because reset itself is a signal.

### From larpdll (runtime cheat behaviour)

- **Aim assist input jitter** — when/if we add SendInput mouse
  nudging externally, port larpdll's aim_assist.cpp jitter:
  wave oscillation, LCG noise bumps, per-engagement speed jitter,
  soft randomised velocity cap. Never move the cursor in straight
  lines or at consistent speed.
- **Stack spoofing (SilentMoonwalk JMP RBX gadget)** — not
  applicable to external since we never call engine functions
  in-process. If we ever go back to any in-process feature, port
  larpdll/stack_spoof.cpp.
- **Perception filter** — silently no-op sensitive functions when
  called from our own code (self-detection avoidance). Only
  relevant if we go back to in-process; N/A for external.

### External-specific (not in kware)

- **EPROCESS unlink** — hide our external process from Task Manager
  / `EnumProcesses` by having the kernel driver unlink our EPROCESS
  from `ActiveProcessLinks`. Requires new command in the driver's
  dispatcher. Non-trivial: unlink is a well-known rootkit technique
  and can crash the system if kernel cleanup hits the unlinked
  entry. Deferred until stability proven.
- **Periodic re-scrub of PiDDB during long sessions** — currently
  scrub happens once at driver install. If Windows re-populates
  our identity, we're exposed. Would need a periodic cmdchannel
  call into the driver's scrub function.
- **Randomised syscall nonce cadence** — cmdchannel currently sends
  syscalls at whatever frequency the caller wants. Adding random
  micro-delays between successive syscalls reduces syscall-rhythm
  fingerprinting at the kernel-callsite watcher.
- **Build-time binary hash rotation** — random 32-byte pad in .rdata
  so consecutive builds hash differently, defeating disk-based
  hash blocklists.
- **BYOVD swap** — currently WinIo64.sys (on MS blocklist per Win11
  22H2+). Fresher candidate must be picked from
  `~/projects/Vulnerable Drivers/` and verified against loldrivers.io.
  Requires updating `launcher/build_launcher.ps1` encryption blob
  path + adjusting IOCTL codes in `ioctl.cpp`.

## Deploy checklist for maximum stealth

1. `sand_launcher.exe --no-inject` (fresh boot, elevated)
2. `PerfMonSvc.exe`
3. In overlay's opsec widget:
   - **silent_mode = ON** (kill disk writes)
   - **scan_tick_base_ms = 200-300**, **jitter = 100-200**
   - **first_scan_delay_s = 10** (or higher)
   - **preflight_bedaisy = ON** (already default)
4. Verify preflight passed in trace (only shows if silent_mode OFF)
5. Play

## What LO gets banned for now (ranked risk)

1. **BYOVD load event** — WinIo64 is blocked. Even successful load
   fires an event BE may correlate.  **Fix: swap driver.**
2. **Custom driver's HAL-hijack behaviour** — very specific syscall
   redirection pattern that mature ACs can fingerprint at the
   ntoskrnl level. **Fix: none available — this is fundamental to
   the architecture. Mitigations: minimise call frequency (already
   done via jitter) and payload obfuscation (already done via cipher).
3. **PerfMonSvc.exe process itself** — if BE ever adds our binary
   hash to a blocklist. **Fix: hash rotation per build.**
4. **Cross-process reads via the driver** — invisible to usermode
   BE, but a kernel-mode component with `PsSetLoadImageNotifyRoutine`
   could catch our driver load if BYOVD scrubs miss anything.
   **Fix: periodic re-scrub + more thorough coverage.**

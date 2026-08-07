// ntapi.h — ntdll prototypes + types we resolve at runtime.
// We avoid linking ntdll.lib for the exotic calls (portability + some Nt*
// functions aren't exported as import-lib symbols in all SDK versions).

#pragma once

#include <windows.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS              ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_IMAGE_ALREADY_LOADED
#define STATUS_IMAGE_ALREADY_LOADED ((NTSTATUS)0xC000010EL)
#endif
#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif

#define SE_LOAD_DRIVER_PRIVILEGE    10UL

typedef LONG NTSTATUS;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

// SystemExtendedHandleInformation (class 64) returns 40-byte entries with
// 64-bit Object pointers. The non-Ex class (16) truncates Object to 32 bits
// on x64 and is useless for object-pointer walks.
typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX {
    PVOID     Object;                      // +0x00 kernel VA of the object body
    ULONG_PTR UniqueProcessId;             // +0x08
    ULONG_PTR HandleValue;                 // +0x10
    ULONG     GrantedAccess;               // +0x18
    USHORT    CreatorBackTraceIndex;       // +0x1C
    USHORT    ObjectTypeIndex;             // +0x1E
    ULONG     HandleAttributes;            // +0x20
    ULONG     Reserved;                    // +0x24
} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;
static_assert(sizeof(SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX) == 40,
              "SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX size drift");

typedef struct _SYSTEM_HANDLE_INFORMATION_EX {
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
} SYSTEM_HANDLE_INFORMATION_EX;

// Function pointer typedefs.
typedef NTSTATUS (NTAPI *PFN_NtLoadDriver)(PUNICODE_STRING);
typedef NTSTATUS (NTAPI *PFN_NtUnloadDriver)(PUNICODE_STRING);
typedef NTSTATUS (NTAPI *PFN_RtlAdjustPrivilege)(ULONG, BOOLEAN, BOOLEAN, PBOOLEAN);
typedef VOID     (NTAPI *PFN_RtlInitUnicodeString)(PUNICODE_STRING, PCWSTR);
typedef NTSTATUS (NTAPI *PFN_NtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);

// SystemCodeIntegrityInformation (class 103) reports VBS/HVCI state.
// CodeIntegrityOptions bit 0x400 = HVCI kernel-mode code integrity enabled.
// When set, any NtLoadDriver of a non-HVCI-compatible driver (DirectIo64 on
// MS blocklist) fails silently — load returns STATUS_IMAGE_ALREADY_LOADED or
// similar without a clear "HVCI rejected this" error. Preflight catches it
// explicitly so the user knows why BYOVD didn't work.
typedef struct _SYSTEM_CODEINTEGRITY_INFORMATION {
    ULONG Length;
    ULONG CodeIntegrityOptions;
} SYSTEM_CODEINTEGRITY_INFORMATION;

#ifndef CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED
#define CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED 0x400
#endif

// Resolved at module init. Implementations in ntapi.cpp.
namespace ntapi {
    constexpr ULONG SystemExtendedHandleInformation = 64;
    constexpr ULONG SystemCodeIntegrityInformation  = 103;

    bool init();
    extern PFN_NtLoadDriver              NtLoadDriver;
    extern PFN_NtUnloadDriver            NtUnloadDriver;
    extern PFN_RtlAdjustPrivilege        RtlAdjustPrivilege;
    extern PFN_RtlInitUnicodeString      RtlInitUnicodeString;
    extern PFN_NtQuerySystemInformation  NtQuerySystemInformation;
}

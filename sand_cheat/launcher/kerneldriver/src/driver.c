// Kernel driver — manual-mapped via BYOVD. No driver object, no device,
// no symbolic link. DriverEntry is called through NtAddAtom syscall hijack
// from usermode; parameter slots are repurposed.
//
// Phase 14.0.b: walk PsLoadedModuleList, find ntoskrnl base.
// Phase 14.0.c: walk ntoskrnl's export table, resolve "MmIsAddressValid"
//               by ASCII strcmp, write resolved VA to scratch.
// Phase 14.0.d: decrypt one LCG-XOR blob, feed decrypted name to walker.
// Phase 14.0.e: full 19-entry resolver loop, populate g_resolved_apis[].
// Second DriverEntry arg is repurposed as phase14_scratch_t*. Full HAL
// dispatch hook + command dispatcher lands in Phase 14.1+.

#include <ntddk.h>
#include <ntimage.h>
#include <intrin.h>

#include "phase14_scratch.h"
#include "lcg_xor.h"
#include "splitmix.h"
#include "phase14_encrypted_names.h"
#include "phase14_cipher_key.h"

// Command-channel magic (spec §5.2). Verified as 0x7C4A in IDA (docs said 0x7C5A).
#define PHASE14_CMD_MAGIC 0x7C4A
#define PHASE14_CMD_BODY_MAX 0x800

// 16-byte static key baked into driver .rdata, used by splitmix_derive_seed
// together with the caller-supplied 12-byte auth nonce to produce the per-
// call seed. Same bytes must live in launcher's phase14_cipher_key.h too.
static const unsigned char g_cipher_key[16] = { PHASE14_CIPHER_KEY_BYTES };

// Minimal IAT imports (4 — matches sample shape per spec §2).
// MmSystemRangeStart is already declared by ntddk.h as `extern const PVOID`;
// referencing it from is_usermode_range below is what pulls it into our IAT.
extern PLIST_ENTRY PsLoadedModuleList;
extern int __cdecl _wcsicmp(const wchar_t* s1, const wchar_t* s2);
extern int __cdecl strcmp(const char* s1, const char* s2);

// Runtime-resolved (Phase 14.0.e) function pointers, typed for callable use.
typedef BOOLEAN          (NTAPI *MmIsAddressValid_fn_t)(PVOID va);
typedef KPROCESSOR_MODE  (NTAPI *ExGetPreviousMode_fn_t)(VOID);

// Resolved-API pointer table. g_resolved_apis[API_PsLookupProcessByProcessId],
// etc., consumed by command dispatchers in Phase 14.2+. Lives in .data
// alongside the encrypted blobs.
static PVOID g_resolved_apis[API_COUNT];

// Phase 14.1: HAL dispatch hook state. Hook installs once at DriverEntry,
// stays resident until reboot (driver memory is never freed per Phase 10+12).
typedef NTSTATUS (*hal_dispatch_fn_t)(PVOID, PVOID, PVOID, PVOID);
static hal_dispatch_fn_t  g_hal_original = NULL;
static PVOID*             g_hal_slot_ptr = NULL;
static volatile LONG   g_notify_armed = 0;
static volatile ULONG  g_notify_target_pid = 0;
static volatile LONG   g_notify_fired = 0;
static WCHAR           g_notify_exe_name[64];
static USHORT          g_notify_exe_name_len = 0;

// LDR_DATA_TABLE_ENTRY layout (DDK-stable fields we care about).
typedef struct _LDR_DATA_TABLE_ENTRY_RE {
    LIST_ENTRY     InLoadOrderLinks;            // 0x00
    LIST_ENTRY     InMemoryOrderLinks;          // 0x10
    LIST_ENTRY     InInitializationOrderLinks;  // 0x20
    PVOID          DllBase;                     // 0x30
    PVOID          EntryPoint;                  // 0x38
    ULONG          SizeOfImage;                 // 0x40
    ULONG          _pad1;
    UNICODE_STRING FullDllName;                 // 0x48
    UNICODE_STRING BaseDllName;                 // 0x58
} LDR_DATA_TABLE_ENTRY_RE, *PLDR_DATA_TABLE_ENTRY_RE;

// Find a loaded kernel module by BaseDllName. Returns DllBase or NULL.
static PVOID find_module_base(const wchar_t* name, ULONG name_len_wchars, ULONG* walked_out) {
    ULONG walked = 0;
    PLIST_ENTRY head = PsLoadedModuleList;
    PLIST_ENTRY cur  = head->Flink;
    while (cur != head) {
        PLDR_DATA_TABLE_ENTRY_RE e = CONTAINING_RECORD(
            cur, LDR_DATA_TABLE_ENTRY_RE, InLoadOrderLinks);
        ++walked;
        if (e->BaseDllName.Buffer &&
            e->BaseDllName.Length == name_len_wchars * sizeof(WCHAR) &&
            _wcsicmp(e->BaseDllName.Buffer, name) == 0) {
            if (walked_out) *walked_out = walked;
            return e->DllBase;
        }
        cur = cur->Flink;
    }
    if (walked_out) *walked_out = walked;
    return NULL;
}

// Walk a PE module's export table, resolve an ASCII export name by strcmp.
// Reads directly via C pointers: module is already mapped in kernel VA.
static PVOID resolve_export(PVOID module_base, const char* target, ULONG* iter_out) {
    UCHAR* base;
    PIMAGE_DOS_HEADER dos;
    PIMAGE_NT_HEADERS64 nt;
    IMAGE_DATA_DIRECTORY* dir;
    PIMAGE_EXPORT_DIRECTORY exp;
    ULONG*  names_rva;
    USHORT* ord_table;
    ULONG*  func_rvas;
    ULONG   i;
    ULONG   iter;
    const char* name;
    USHORT  ord;
    ULONG   func_rva;

    iter = 0;
    if (iter_out) *iter_out = 0;
    if (!module_base || !target) return NULL;

    base = (UCHAR*)module_base;
    dos  = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;

    nt = (PIMAGE_NT_HEADERS64)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;

    dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir->VirtualAddress || !dir->Size) return NULL;

    exp = (PIMAGE_EXPORT_DIRECTORY)(base + dir->VirtualAddress);

    names_rva = (ULONG*) (base + exp->AddressOfNames);
    ord_table = (USHORT*)(base + exp->AddressOfNameOrdinals);
    func_rvas = (ULONG*) (base + exp->AddressOfFunctions);

    for (i = 0; i < exp->NumberOfNames; ++i) {
        ++iter;
        name = (const char*)(base + names_rva[i]);
        if (strcmp(name, target) == 0) {
            ord = ord_table[i];
            func_rva = func_rvas[ord];
            // Forwarded export: RVA inside export dir -> points at a forwarder
            // string "Module.Name". None of our 19 are forwarded.
            if (func_rva >= dir->VirtualAddress &&
                func_rva <  dir->VirtualAddress + dir->Size) {
                if (iter_out) *iter_out = iter;
                return NULL;
            }
            if (iter_out) *iter_out = iter;
            return (PVOID)(base + func_rva);
        }
    }
    if (iter_out) *iter_out = iter;
    return NULL;
}

// Find a PE section by 8-byte name (null-padded). Returns 1 on success.
// Used to locate ntoskrnl's .text for the HAL-hook pattern scan.
static int find_section(PVOID base, const char* target_name,
                        UCHAR** out_start, SIZE_T* out_size) {
    UCHAR* b;
    PIMAGE_DOS_HEADER dos;
    PIMAGE_NT_HEADERS64 nt;
    PIMAGE_SECTION_HEADER sec;
    USHORT i;
    int    j;

    if (!base || !target_name || !out_start || !out_size) return 0;
    b = (UCHAR*)base;
    dos = (PIMAGE_DOS_HEADER)b;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (PIMAGE_NT_HEADERS64)(b + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        int match = 1;
        for (j = 0; j < 8; ++j) {
            char s = (char)sec[i].Name[j];
            char t = target_name[j];
            if (s != t) { match = 0; break; }
            if (t == 0) break;
        }
        if (match) {
            *out_start = b + sec[i].VirtualAddress;
            *out_size  = sec[i].Misc.VirtualSize;
            return 1;
        }
    }
    return 0;
}

// Masked byte-pattern scanner. mask[i]=1 means pat[i] must match exactly,
// mask[i]=0 means wildcard. Returns pointer to first match or NULL.
static UCHAR* find_pattern(UCHAR* haystack, SIZE_T haystack_len,
                           const UCHAR* pat, const UCHAR* mask, SIZE_T pat_len) {
    SIZE_T i, j, last;
    if (!haystack || !pat || !mask || haystack_len < pat_len) return NULL;
    last = haystack_len - pat_len;
    for (i = 0; i <= last; ++i) {
        for (j = 0; j < pat_len; ++j) {
            if (mask[j] && haystack[i + j] != pat[j]) break;
        }
        if (j == pat_len) return haystack + i;
    }
    return NULL;
}

// Reject non-user-mode VAs. Matches sample sub_140001CD4: null+size guard,
// integer-overflow check, and require [va, va+size) fully below kernel range.
// Non-canonical addresses in x64 are impossible for user-mode since valid
// user VA max is MmSystemRangeStart-1; the >= kernel check alone covers it.
static BOOLEAN is_usermode_range(PVOID va, SIZE_T size) {
    ULONG_PTR addr = (ULONG_PTR)va;
    ULONG_PTR end;
    ULONG_PTR krnl;

    if (!addr || !size) return FALSE;
    end = addr + size - 1;
    if (end < addr) return FALSE;                     // wraparound
    krnl = (ULONG_PTR)MmSystemRangeStart;
    if (addr >= krnl) return FALSE;                   // reject kernel VAs
    if (end  >= krnl) return FALSE;                   // reject straddle
    return TRUE;
}

// Copy memory with MmIsAddressValid guards on both endpoints of both buffers.
// Non-raising (no SEH): matches sample sub_140002AA0. Returns FALSE if any
// endpoint's page is not resident / not mapped. Paged-out pages count as
// invalid here — caller must accept that.
static BOOLEAN safe_memcpy(void* dst, const void* src, SIZE_T size) {
    MmIsAddressValid_fn_t pMm;
    UCHAR*       d;
    const UCHAR* s;

    if (size == 0) return TRUE;
    pMm = (MmIsAddressValid_fn_t)g_resolved_apis[API_MmIsAddressValid];
    if (!pMm) return FALSE;

    d = (UCHAR*)dst;
    s = (const UCHAR*)src;
    if (!pMm((PVOID)d) || !pMm((PVOID)(d + size - 1))) return FALSE;
    if (!pMm((PVOID)(ULONG_PTR)s) || !pMm((PVOID)(s + size - 1))) return FALSE;

    RtlCopyMemory(d, s, size);
    return TRUE;
}

// Signatures for kernel APIs we resolved in Phase 14.0.e but haven't typed yet.
typedef NTSTATUS (NTAPI *PsLookupProcessByProcessId_t)(HANDLE, PEPROCESS*);
typedef NTSTATUS (NTAPI *MmCopyVirtualMemory_t)(PEPROCESS, PVOID, PEPROCESS, PVOID, SIZE_T, KPROCESSOR_MODE, PSIZE_T);
typedef LONG_PTR (NTAPI *ObfDereferenceObject_t)(PVOID);
typedef PEPROCESS (NTAPI *IoGetCurrentProcess_t)(VOID);
// KAPC_STATE: WDK ntddk.h doesn't always expose this. 64-byte opaque buffer
// covers the true size (~43 bytes on x64) with headroom. We only pass the
// address to attach/detach; internal layout doesn't matter to us.
typedef struct _KAPC_STATE_RE {
    UCHAR opaque[64];
} KAPC_STATE_RE, *PKAPC_STATE_RE;

typedef VOID     (NTAPI *KeStackAttachProcess_t)(PEPROCESS, PKAPC_STATE_RE);
typedef VOID     (NTAPI *KeUnstackDetachProcess_t)(PKAPC_STATE_RE);
typedef NTSTATUS (NTAPI *ZwAllocateVirtualMemory_t)(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG);
typedef NTSTATUS (NTAPI *ZwFreeVirtualMemory_t)(HANDLE, PVOID*, PSIZE_T, ULONG);
typedef NTSTATUS (NTAPI *ZwProtectVirtualMemory_t)(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG);
typedef NTSTATUS (NTAPI *ZwQueryVirtualMemory_t)(HANDLE, PVOID, ULONG /*class*/, PVOID, SIZE_T, PSIZE_T);
typedef PVOID    (NTAPI *PsGetProcessPeb_t)(PEPROCESS);
typedef NTSTATUS (NTAPI *PsSetLoadImageNotifyRoutine_t)(PVOID NotifyRoutine);
typedef NTSTATUS (NTAPI *PsRemoveLoadImageNotifyRoutine_t)(PVOID NotifyRoutine);
typedef NTSTATUS (NTAPI *RtlCreateUserThread_t)(
    HANDLE ProcessHandle,
    PVOID SecurityDescriptor,
    BOOLEAN CreateSuspended,
    ULONG StackZeroBits,
    PSIZE_T StackReserved,
    PSIZE_T StackCommit,
    PVOID StartAddress,
    PVOID StartParameter,
    PHANDLE ThreadHandle,
    PVOID ClientId);

// PEB + Ldr layout (x64). Not in ntddk.h — manual-defined for our walker.
typedef struct _PEB_LDR_DATA_RE {
    ULONG      Length;                    // 0x00
    UCHAR      Initialized;               // 0x04
    UCHAR      _pad1[3];
    PVOID      SsHandle;                  // 0x08
    LIST_ENTRY InLoadOrderModuleList;     // 0x10
} PEB_LDR_DATA_RE, *PPEB_LDR_DATA_RE;

typedef struct _PEB_RE {
    UCHAR              _pad0[0x18];
    PPEB_LDR_DATA_RE   Ldr;               // 0x18
} PEB_RE, *PPEB_RE;

static VOID image_load_notify(PUNICODE_STRING FullImageName,
                               HANDLE ProcessId,
                               PIMAGE_INFO ImageInfo) {
    UNREFERENCED_PARAMETER(ImageInfo);
    if (!g_notify_armed || g_notify_fired) return;
    if (!FullImageName || !FullImageName->Buffer) return;

    USHORT i;
    WCHAR* filename = FullImageName->Buffer;
    USHORT char_count = FullImageName->Length / sizeof(WCHAR);
    for (i = char_count; i > 0; --i) {
        if (FullImageName->Buffer[i - 1] == L'\\' || FullImageName->Buffer[i - 1] == L'/') {
            filename = &FullImageName->Buffer[i];
            char_count = char_count - i;
            break;
        }
    }

    if (char_count != g_notify_exe_name_len) return;
    for (i = 0; i < char_count; ++i) {
        WCHAR a = filename[i];
        WCHAR b = g_notify_exe_name[i];
        if (a >= L'A' && a <= L'Z') a += 32;
        if (b >= L'A' && b <= L'Z') b += 32;
        if (a != b) return;
    }

    g_notify_target_pid = (ULONG)(ULONG_PTR)ProcessId;
    InterlockedExchange(&g_notify_fired, 1);
}

static NTSTATUS cmd_arm_image_notify(UCHAR* body, ULONG body_size) {
    PsSetLoadImageNotifyRoutine_t pSet;
    USHORT name_len;
    PVOID  name_ptr;
    NTSTATUS st;

    if (body_size < 0x18) return STATUS_INVALID_PARAMETER;

    name_len = *(USHORT*)(body + 0x08);
    name_ptr = (PVOID)*(ULONGLONG*)(body + 0x10);

    if (name_len == 0 || name_len > 63) return STATUS_INVALID_PARAMETER;
    if (!is_usermode_range(name_ptr, (SIZE_T)name_len * sizeof(WCHAR)))
        return STATUS_INVALID_PARAMETER;

    pSet = (PsSetLoadImageNotifyRoutine_t)g_resolved_apis[API_PsSetLoadImageNotifyRoutine];
    if (!pSet) return STATUS_PROCEDURE_NOT_FOUND;

    if (!safe_memcpy(g_notify_exe_name, name_ptr, (SIZE_T)name_len * sizeof(WCHAR)))
        return STATUS_ACCESS_VIOLATION;
    g_notify_exe_name[name_len] = 0;
    g_notify_exe_name_len = name_len;

    InterlockedExchange(&g_notify_fired, 0);
    g_notify_target_pid = 0;

    if (!g_notify_armed) {
        st = pSet((PVOID)image_load_notify);
        if (!NT_SUCCESS(st)) return st;
        InterlockedExchange(&g_notify_armed, 1);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS cmd_query_armed_pid(UCHAR* body, ULONG body_size) {
    PVOID out_pid_ptr, out_fired_ptr;
    ULONG pid_val, fired_val;

    if (body_size < 0x18) return STATUS_INVALID_PARAMETER;

    out_pid_ptr   = (PVOID)*(ULONGLONG*)(body + 0x08);
    out_fired_ptr = (PVOID)*(ULONGLONG*)(body + 0x10);

    if (!is_usermode_range(out_pid_ptr, sizeof(ULONG))) return STATUS_INVALID_PARAMETER;
    if (!is_usermode_range(out_fired_ptr, sizeof(ULONG))) return STATUS_INVALID_PARAMETER;

    pid_val = g_notify_target_pid;
    fired_val = (ULONG)g_notify_fired;

    if (!safe_memcpy(out_pid_ptr, &pid_val, sizeof(pid_val))) return STATUS_ACCESS_VIOLATION;
    if (!safe_memcpy(out_fired_ptr, &fired_val, sizeof(fired_val))) return STATUS_ACCESS_VIOLATION;

    return STATUS_SUCCESS;
}

static NTSTATUS cmd_create_thread(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t pLookup;
    ObfDereferenceObject_t       pDeref;
    RtlCreateUserThread_t        pCreate;
    ULONG       pid;
    PVOID       start_address, parameter, out_tid_ptr;
    PEPROCESS   target = NULL;
    HANDLE      proc_handle = NULL;
    HANDLE      thread_handle = NULL;
    NTSTATUS    st;
    struct { HANDLE uid; HANDLE tid; } client_id = {0};

    if (body_size < 0x28) return STATUS_INVALID_PARAMETER;

    pid           = *(ULONG*)(body + 0x08);
    start_address = (PVOID)*(ULONGLONG*)(body + 0x10);
    parameter     = (PVOID)*(ULONGLONG*)(body + 0x18);
    out_tid_ptr   = (PVOID)*(ULONGLONG*)(body + 0x20);

    if (!start_address) return STATUS_INVALID_PARAMETER;

    pLookup = (PsLookupProcessByProcessId_t)g_resolved_apis[API_PsLookupProcessByProcessId];
    pDeref  = (ObfDereferenceObject_t)      g_resolved_apis[API_ObfDereferenceObject];
    pCreate = (RtlCreateUserThread_t)       g_resolved_apis[API_RtlCreateUserThread];
    if (!pLookup || !pDeref || !pCreate) return STATUS_PROCEDURE_NOT_FOUND;

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    st = ObOpenObjectByPointer(target, OBJ_KERNEL_HANDLE, NULL, PROCESS_ALL_ACCESS, *PsProcessType, KernelMode, &proc_handle);
    if (!NT_SUCCESS(st)) {
        pDeref(target);
        return st;
    }

    st = pCreate(proc_handle,
                 NULL,
                 FALSE,
                 0,
                 NULL,
                 NULL,
                 start_address,
                 parameter,
                 &thread_handle,
                 &client_id);

    if (NT_SUCCESS(st) && thread_handle) {
        ZwClose(thread_handle);
    }

    ZwClose(proc_handle);
    pDeref(target);

    if (NT_SUCCESS(st) && out_tid_ptr && is_usermode_range(out_tid_ptr, sizeof(ULONG))) {
        ULONG tid = (ULONG)(ULONG_PTR)client_id.tid;
        safe_memcpy(out_tid_ptr, &tid, sizeof(tid));
    }

    return st;
}

// Phase 14.5: READ_MEMORY (cmd 0). Body layout (48 bytes total, spec §5.3):
//   +0x00 magic    (u16=0x7C4A, already verified)
//   +0x02 reserved (u16)
//   +0x04 cmd_id   (u32=0, already verified)
//   +0x08 pid      (u32)  — target process ID
//   +0x0C pad      (u32)
//   +0x10 src      (u64)  — source VA in target process
//   +0x18 dst      (u64)  — destination VA in caller's process (launcher)
//   +0x20 size     (u64)  — byte count, ≤ 16 MB
//   +0x28 pad      (u64)  — tail padding to 0x30
//
// We use MmCopyVirtualMemory directly from (target, src) -> (current, dst)
// without a kernel-pool intermediate. Sample's spec §5.3 uses a pool buffer;
// on x64 MmCopyVirtualMemory handles the cross-process case natively, so the
// intermediate is unnecessary. Add it back if we ever need atomic-failure
// semantics.
static NTSTATUS cmd_read_memory(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t pLookup;
    MmCopyVirtualMemory_t        pCopy;
    ObfDereferenceObject_t       pDeref;
    IoGetCurrentProcess_t        pCurr;
    ULONG     pid;
    PVOID     src, dst;
    SIZE_T    size;
    PEPROCESS target = NULL;
    PEPROCESS current;
    NTSTATUS  st;
    SIZE_T    got = 0;

    if (body_size < 0x30) return STATUS_INVALID_PARAMETER;

    pid  = *(ULONG*)     (body + 0x08);
    src  = (PVOID)       *(ULONGLONG*)(body + 0x10);
    dst  = (PVOID)       *(ULONGLONG*)(body + 0x18);
    size = (SIZE_T)      *(ULONGLONG*)(body + 0x20);

    if (size == 0 || size > 0x1000000) return STATUS_INVALID_PARAMETER;  // 16 MB cap
    if (!is_usermode_range(src, size)) return STATUS_INVALID_PARAMETER;
    if (!is_usermode_range(dst, size)) return STATUS_INVALID_PARAMETER;

    pLookup = (PsLookupProcessByProcessId_t)g_resolved_apis[API_PsLookupProcessByProcessId];
    pCopy   = (MmCopyVirtualMemory_t)       g_resolved_apis[API_MmCopyVirtualMemory];
    pDeref  = (ObfDereferenceObject_t)      g_resolved_apis[API_ObfDereferenceObject];
    pCurr   = (IoGetCurrentProcess_t)       g_resolved_apis[API_IoGetCurrentProcess];
    if (!pLookup || !pCopy || !pDeref || !pCurr) return STATUS_PROCEDURE_NOT_FOUND;

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    current = pCurr();
    st = pCopy(target, src, current, dst, size, UserMode, &got);

    pDeref(target);
    return st;
}

// Phase 14.6: WRITE_MEMORY (cmd 1). Body layout (48 bytes, mirrors READ):
//   +0x08 pid      (u32)  — target process ID
//   +0x10 dst      (u64)  — destination VA in target process   (opposite of READ)
//   +0x18 src      (u64)  — source VA in caller's process       (opposite of READ)
//   +0x20 size     (u64)  — byte count, ≤ 16 MB
// MmCopyVirtualMemory direction reversed: copy FROM current TO target.
// v1 (simplified) — no protection dance, no pool intermediate. Sample's spec
// §5.3 has an allocate-pool-then-ZwProtect fallback for guarded pages; add in
// a v2 iteration if/when we hit a page that needs it.
static NTSTATUS cmd_write_memory(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t pLookup;
    MmCopyVirtualMemory_t        pCopy;
    ObfDereferenceObject_t       pDeref;
    IoGetCurrentProcess_t        pCurr;
    ULONG     pid;
    PVOID     dst, src;
    SIZE_T    size;
    PEPROCESS target = NULL;
    PEPROCESS current;
    NTSTATUS  st;
    SIZE_T    got = 0;

    if (body_size < 0x30) return STATUS_INVALID_PARAMETER;

    pid  = *(ULONG*)     (body + 0x08);
    dst  = (PVOID)       *(ULONGLONG*)(body + 0x10);  // target-side
    src  = (PVOID)       *(ULONGLONG*)(body + 0x18);  // launcher-side
    size = (SIZE_T)      *(ULONGLONG*)(body + 0x20);

    if (size == 0 || size > 0x1000000) return STATUS_INVALID_PARAMETER;  // 16 MB cap
    if (!is_usermode_range(dst, size)) return STATUS_INVALID_PARAMETER;
    if (!is_usermode_range(src, size)) return STATUS_INVALID_PARAMETER;

    pLookup = (PsLookupProcessByProcessId_t)g_resolved_apis[API_PsLookupProcessByProcessId];
    pCopy   = (MmCopyVirtualMemory_t)       g_resolved_apis[API_MmCopyVirtualMemory];
    pDeref  = (ObfDereferenceObject_t)      g_resolved_apis[API_ObfDereferenceObject];
    pCurr   = (IoGetCurrentProcess_t)       g_resolved_apis[API_IoGetCurrentProcess];
    if (!pLookup || !pCopy || !pDeref || !pCurr) return STATUS_PROCEDURE_NOT_FOUND;

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    current = pCurr();
    st = pCopy(current, src, target, dst, size, UserMode, &got);

    pDeref(target);
    return st;
}

// Phase 14.7: FIND_MODULE_IN_PROCESS (cmd 2). Body layout (56 bytes, spec §5.3):
//   +0x08 pid        (u32)
//   +0x0C name_len   (u16)  — length in wchars, EXCLUDING null
//   +0x0E pad        (u16)
//   +0x10 name_ptr   (u64)  — launcher user VA of wide module name
//   +0x18 out_base   (u64)  — launcher user VA receiving DllBase
//   +0x20 out_size   (u64)  — launcher user VA receiving SizeOfImage (0 = skip)
//
// Flow: copy name from launcher VA → kernel stack; lookup target EPROCESS;
// KeStackAttachProcess; walk target PEB.Ldr.InLoadOrderModuleList, compare
// BaseDllName; on match save base+size; KeUnstackDetachProcess; write back.
static NTSTATUS cmd_find_module(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t  pLookup;
    ObfDereferenceObject_t        pDeref;
    KeStackAttachProcess_t        pAttach;
    KeUnstackDetachProcess_t      pDetach;
    PsGetProcessPeb_t             pGetPeb;
    ULONG       pid;
    USHORT      name_len;
    PVOID       name_ptr;
    PVOID       out_base_ptr;
    PVOID       out_size_ptr;
    WCHAR       name_buf[128];       // max 127 wchars + null
    PEPROCESS   target = NULL;
    KAPC_STATE_RE apc_state;
    PPEB_RE     peb;
    PPEB_LDR_DATA_RE ldr;
    PLIST_ENTRY head, cur;
    PLDR_DATA_TABLE_ENTRY_RE e;
    ULONGLONG   found_base = 0;
    ULONG       found_size = 0;
    BOOLEAN     found = FALSE;
    NTSTATUS    st;

    if (body_size < 0x30) return STATUS_INVALID_PARAMETER;

    pid          = *(ULONG*)   (body + 0x08);
    name_len     = *(USHORT*)  (body + 0x0C);
    name_ptr     = (PVOID)*(ULONGLONG*)(body + 0x10);
    out_base_ptr = (PVOID)*(ULONGLONG*)(body + 0x18);
    out_size_ptr = (PVOID)*(ULONGLONG*)(body + 0x20);

    if (name_len == 0 || name_len > 127) return STATUS_INVALID_PARAMETER;
    if (!is_usermode_range(name_ptr, (SIZE_T)name_len * sizeof(WCHAR))) return STATUS_INVALID_PARAMETER;
    if (!is_usermode_range(out_base_ptr, sizeof(ULONGLONG))) return STATUS_INVALID_PARAMETER;
    if (out_size_ptr && !is_usermode_range(out_size_ptr, sizeof(ULONG))) return STATUS_INVALID_PARAMETER;

    pLookup = (PsLookupProcessByProcessId_t)g_resolved_apis[API_PsLookupProcessByProcessId];
    pDeref  = (ObfDereferenceObject_t)      g_resolved_apis[API_ObfDereferenceObject];
    pAttach = (KeStackAttachProcess_t)      g_resolved_apis[API_KeStackAttachProcess];
    pDetach = (KeUnstackDetachProcess_t)    g_resolved_apis[API_KeUnstackDetachProcess];
    pGetPeb = (PsGetProcessPeb_t)           g_resolved_apis[API_PsGetProcessPeb];
    if (!pLookup || !pDeref || !pAttach || !pDetach || !pGetPeb) return STATUS_PROCEDURE_NOT_FOUND;

    // Copy name from launcher VA → kernel stack (still in launcher context).
    // Zero-terminate explicitly so _wcsicmp is safe.
    if (!safe_memcpy(name_buf, name_ptr, (SIZE_T)name_len * sizeof(WCHAR))) return STATUS_ACCESS_VIOLATION;
    name_buf[name_len] = 0;

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    // Enter target VA space. From here, target's user VAs are accessible.
    pAttach(target, &apc_state);
    peb = (PPEB_RE)pGetPeb(target);
    if (peb && peb->Ldr) {
        ldr = peb->Ldr;
        head = &ldr->InLoadOrderModuleList;
        cur = head->Flink;
        while (cur && cur != head) {
            e = CONTAINING_RECORD(cur, LDR_DATA_TABLE_ENTRY_RE, InLoadOrderLinks);
            if (e->BaseDllName.Buffer &&
                e->BaseDllName.Length == name_len * sizeof(WCHAR) &&
                _wcsicmp(e->BaseDllName.Buffer, name_buf) == 0) {
                found_base = (ULONGLONG)(ULONG_PTR)e->DllBase;
                found_size = e->SizeOfImage;
                found = TRUE;
                break;
            }
            cur = cur->Flink;
        }
    }
    pDetach(&apc_state);
    pDeref(target);

    if (!found) return STATUS_NOT_FOUND;

    // Write results to launcher (back in launcher context post-detach).
    if (!safe_memcpy(out_base_ptr, &found_base, sizeof(found_base))) return STATUS_ACCESS_VIOLATION;
    if (out_size_ptr) {
        if (!safe_memcpy(out_size_ptr, &found_size, sizeof(found_size))) return STATUS_ACCESS_VIOLATION;
    }
    return STATUS_SUCCESS;
}

// Phase 15.0: ALLOCATE_VM_IN_PROCESS (cmd 3). Body (48 bytes):
//   +0x08 pid          (u32)
//   +0x10 hint_addr    (u64)  — 0 = any address
//   +0x18 size         (u64)
//   +0x20 alloc_type   (u32)  — MEM_COMMIT=0x1000 | MEM_RESERVE=0x2000 = 0x3000
//   +0x24 protect      (u32)  — PAGE_READWRITE=0x04, PAGE_EXECUTE_READWRITE=0x40
//   +0x28 out_addr_ptr (u64)  — launcher VA receives allocated base
static NTSTATUS cmd_alloc_vm(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t pLookup;
    ObfDereferenceObject_t       pDeref;
    KeStackAttachProcess_t       pAttach;
    KeUnstackDetachProcess_t     pDetach;
    ZwAllocateVirtualMemory_t    pAlloc;
    ULONG       pid, alloc_type, protect;
    PVOID       hint_addr, out_addr_ptr;
    PVOID       allocated = NULL;
    SIZE_T      size;
    PEPROCESS   target = NULL;
    KAPC_STATE_RE apc_state;
    NTSTATUS    st;
    ULONGLONG   result_u64;

    if (body_size < 0x30) return STATUS_INVALID_PARAMETER;

    pid          = *(ULONG*)(body + 0x08);
    hint_addr    = (PVOID)   *(ULONGLONG*)(body + 0x10);
    size         = (SIZE_T)  *(ULONGLONG*)(body + 0x18);
    alloc_type   = *(ULONG*) (body + 0x20);
    protect      = *(ULONG*) (body + 0x24);
    out_addr_ptr = (PVOID)   *(ULONGLONG*)(body + 0x28);

    if (size == 0 || size > 0x10000000) return STATUS_INVALID_PARAMETER;  // ≤ 256 MB
    if (!is_usermode_range(out_addr_ptr, sizeof(ULONGLONG))) return STATUS_INVALID_PARAMETER;

    pLookup = (PsLookupProcessByProcessId_t)g_resolved_apis[API_PsLookupProcessByProcessId];
    pDeref  = (ObfDereferenceObject_t)      g_resolved_apis[API_ObfDereferenceObject];
    pAttach = (KeStackAttachProcess_t)      g_resolved_apis[API_KeStackAttachProcess];
    pDetach = (KeUnstackDetachProcess_t)    g_resolved_apis[API_KeUnstackDetachProcess];
    pAlloc  = (ZwAllocateVirtualMemory_t)   g_resolved_apis[API_ZwAllocateVirtualMemory];
    if (!pLookup || !pDeref || !pAttach || !pDetach || !pAlloc) return STATUS_PROCEDURE_NOT_FOUND;

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    allocated = hint_addr;
    pAttach(target, &apc_state);
    st = pAlloc((HANDLE)(LONG_PTR)-1, &allocated, 0, &size, alloc_type, protect);
    pDetach(&apc_state);
    pDeref(target);

    if (!NT_SUCCESS(st)) return st;

    result_u64 = (ULONGLONG)(ULONG_PTR)allocated;
    if (!safe_memcpy(out_addr_ptr, &result_u64, sizeof(result_u64))) return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

// Phase 15.1: FREE_VM_IN_PROCESS (cmd 4). Body (40 bytes):
//   +0x08 pid       (u32)
//   +0x10 addr      (u64)  — base to free
//   +0x18 size      (u64)  — 0 when free_type=MEM_RELEASE
//   +0x20 free_type (u32)  — MEM_RELEASE=0x8000, MEM_DECOMMIT=0x4000
static NTSTATUS cmd_free_vm(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t pLookup;
    ObfDereferenceObject_t       pDeref;
    KeStackAttachProcess_t       pAttach;
    KeUnstackDetachProcess_t     pDetach;
    ZwFreeVirtualMemory_t        pFree;
    ULONG       pid, free_type;
    PVOID       addr;
    SIZE_T      size;
    PEPROCESS   target = NULL;
    KAPC_STATE_RE apc_state;
    NTSTATUS    st;

    if (body_size < 0x28) return STATUS_INVALID_PARAMETER;

    pid       = *(ULONG*) (body + 0x08);
    addr      = (PVOID)   *(ULONGLONG*)(body + 0x10);
    size      = (SIZE_T)  *(ULONGLONG*)(body + 0x18);
    free_type = *(ULONG*) (body + 0x20);

    if (!addr) return STATUS_INVALID_PARAMETER;

    pLookup = (PsLookupProcessByProcessId_t)g_resolved_apis[API_PsLookupProcessByProcessId];
    pDeref  = (ObfDereferenceObject_t)      g_resolved_apis[API_ObfDereferenceObject];
    pAttach = (KeStackAttachProcess_t)      g_resolved_apis[API_KeStackAttachProcess];
    pDetach = (KeUnstackDetachProcess_t)    g_resolved_apis[API_KeUnstackDetachProcess];
    pFree   = (ZwFreeVirtualMemory_t)       g_resolved_apis[API_ZwFreeVirtualMemory];
    if (!pLookup || !pDeref || !pAttach || !pDetach || !pFree) return STATUS_PROCEDURE_NOT_FOUND;

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    pAttach(target, &apc_state);
    st = pFree((HANDLE)(LONG_PTR)-1, &addr, &size, free_type);
    pDetach(&apc_state);
    pDeref(target);

    return st;
}

// Phase 15.2: PROTECT (cmd 5). Body (48 bytes, IDA-verified wrapper sub_140001DC8):
//   +0x08 pid              (u32)
//   +0x10 addr             (u64)
//   +0x18 size             (u64, ≤ 1 MB)
//   +0x20 new_protect      (u32) — PAGE_*
//   +0x28 out_old_prot_ptr (u64) — launcher VA receives u32 old-protect
// v1: Zw path only. Add Mm fallback if guarded pages show up in target.
static NTSTATUS cmd_protect_vm(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t pLookup;
    ObfDereferenceObject_t       pDeref;
    KeStackAttachProcess_t       pAttach;
    KeUnstackDetachProcess_t     pDetach;
    ZwProtectVirtualMemory_t     pProtect;
    ULONG       pid, new_protect, old_protect = 0;
    PVOID       addr, out_old_ptr;
    SIZE_T      size;
    PEPROCESS   target = NULL;
    KAPC_STATE_RE apc_state;
    NTSTATUS    st;

    if (body_size < 0x30) return STATUS_INVALID_PARAMETER;

    pid          = *(ULONG*) (body + 0x08);
    addr         = (PVOID)   *(ULONGLONG*)(body + 0x10);
    size         = (SIZE_T)  *(ULONGLONG*)(body + 0x18);
    new_protect  = *(ULONG*) (body + 0x20);
    out_old_ptr  = (PVOID)   *(ULONGLONG*)(body + 0x28);

    if (size == 0 || size > 0x1000000) return STATUS_INVALID_PARAMETER;  // 16 MB cap
    if (!addr) return STATUS_INVALID_PARAMETER;
    if (out_old_ptr && !is_usermode_range(out_old_ptr, sizeof(ULONG))) return STATUS_INVALID_PARAMETER;

    pLookup  = (PsLookupProcessByProcessId_t)g_resolved_apis[API_PsLookupProcessByProcessId];
    pDeref   = (ObfDereferenceObject_t)      g_resolved_apis[API_ObfDereferenceObject];
    pAttach  = (KeStackAttachProcess_t)      g_resolved_apis[API_KeStackAttachProcess];
    pDetach  = (KeUnstackDetachProcess_t)    g_resolved_apis[API_KeUnstackDetachProcess];
    pProtect = (ZwProtectVirtualMemory_t)    g_resolved_apis[API_ZwProtectVirtualMemory];
    if (!pLookup || !pDeref || !pAttach || !pDetach || !pProtect) return STATUS_PROCEDURE_NOT_FOUND;

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    pAttach(target, &apc_state);
    st = pProtect((HANDLE)(LONG_PTR)-1, &addr, &size, new_protect, &old_protect);
    pDetach(&apc_state);
    pDeref(target);

    if (!NT_SUCCESS(st)) return st;
    if (out_old_ptr) {
        if (!safe_memcpy(out_old_ptr, &old_protect, sizeof(old_protect))) return STATUS_ACCESS_VIOLATION;
    }
    return STATUS_SUCCESS;
}

// Phase 15.3: QUERY (cmd 6). Body (40 bytes, IDA-verified wrapper sub_140001E90):
//   +0x08 pid       (u32)
//   +0x10 addr      (u64) — address to query in target
//   +0x18 out_info  (u64) — launcher VA receives 48-byte MEMORY_BASIC_INFORMATION
static NTSTATUS cmd_query_vm(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t pLookup;
    ObfDereferenceObject_t       pDeref;
    KeStackAttachProcess_t       pAttach;
    KeUnstackDetachProcess_t     pDetach;
    ZwQueryVirtualMemory_t       pQuery;
    ULONG       pid;
    PVOID       addr, out_info_ptr;
    PEPROCESS   target = NULL;
    KAPC_STATE_RE apc_state;
    NTSTATUS    st;
    UCHAR       mbi[48];
    SIZE_T      ret_len = 0;

    if (body_size < 0x20) return STATUS_INVALID_PARAMETER;

    pid          = *(ULONG*) (body + 0x08);
    addr         = (PVOID)   *(ULONGLONG*)(body + 0x10);
    out_info_ptr = (PVOID)   *(ULONGLONG*)(body + 0x18);

    if (!out_info_ptr) return STATUS_INVALID_PARAMETER;
    if (!is_usermode_range(out_info_ptr, sizeof(mbi))) return STATUS_INVALID_PARAMETER;

    pLookup = (PsLookupProcessByProcessId_t)g_resolved_apis[API_PsLookupProcessByProcessId];
    pDeref  = (ObfDereferenceObject_t)      g_resolved_apis[API_ObfDereferenceObject];
    pAttach = (KeStackAttachProcess_t)      g_resolved_apis[API_KeStackAttachProcess];
    pDetach = (KeUnstackDetachProcess_t)    g_resolved_apis[API_KeUnstackDetachProcess];
    pQuery  = (ZwQueryVirtualMemory_t)      g_resolved_apis[API_ZwQueryVirtualMemory];
    if (!pLookup || !pDeref || !pAttach || !pDetach || !pQuery) return STATUS_PROCEDURE_NOT_FOUND;

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    RtlZeroMemory(mbi, sizeof(mbi));
    pAttach(target, &apc_state);
    // class 0 = MemoryBasicInformation; size 48 on x64.
    st = pQuery((HANDLE)(LONG_PTR)-1, addr, 0, mbi, sizeof(mbi), &ret_len);
    pDetach(&apc_state);
    pDeref(target);

    if (!NT_SUCCESS(st)) return st;
    if (!safe_memcpy(out_info_ptr, mbi, sizeof(mbi))) return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

// Phase 16.x: SET_PTE_NX (cmd 8) — PTE-direct protection flip without
// VAD split. Walks target's page tables physically, modifies NX/W bits
// in the leaf PTE, leaves Mm bookkeeping untouched. CPU sees the new
// protection; user-mode AC walking VADs sees the original RTSS .data
// shape unchanged.
//
// Body layout (40 bytes):
//   +0x08 pid       (u32)  — target process ID
//   +0x0C flags     (u32)  — bit0: clear NX (make executable)
//                            bit1: set   W  (make writable)
//                            bit2: clear W  (make read-only)
//                            bit3: set   NX (make non-executable)
//   +0x10 va        (u64)  — start VA in target (user-mode range)
//   +0x18 size      (u64)  — byte length, ≤ 16 MB
//   +0x20 pad       (u64)
//
// Walks PML4 -> PDPT -> PD -> PT for each 4KB page, dereferences the
// PTE via MmGetVirtualForPhysical (kernel direct-map already covers
// page-table physical memory), modifies bits, invalidates local TLB.
// Multi-CPU TLB consistency: not needed for fresh pages (no entries
// cached on other CPUs); skip the IPI to keep this one-shot and cheap.
typedef PVOID (NTAPI *MmGetVirtualForPhysical_t)(PHYSICAL_ADDRESS);

static NTSTATUS cmd_set_pte_nx(UCHAR* body, ULONG body_size) {
    PsLookupProcessByProcessId_t  pLookup;
    ObfDereferenceObject_t        pDeref;
    KeStackAttachProcess_t        pAttach;
    KeUnstackDetachProcess_t      pDetach;
    MmGetVirtualForPhysical_t     pGetVa;
    ULONG       pid, flags;
    ULONGLONG   va, size;
    PEPROCESS   target = NULL;
    KAPC_STATE_RE apc;
    NTSTATUS    st;
    NTSTATUS    ret;
    ULONGLONG   cur, end_va;

    if (body_size < 0x28) return STATUS_INVALID_PARAMETER;

    pid   = *(ULONG*)    (body + 0x08);
    flags = *(ULONG*)    (body + 0x0C);
    va    = *(ULONGLONG*)(body + 0x10);
    size  = *(ULONGLONG*)(body + 0x18);

    if (size == 0 || size > 0x1000000) return STATUS_INVALID_PARAMETER;
    if (!is_usermode_range((PVOID)(ULONG_PTR)va, (SIZE_T)size)) {
        return STATUS_INVALID_PARAMETER;
    }

    pLookup = (PsLookupProcessByProcessId_t) g_resolved_apis[API_PsLookupProcessByProcessId];
    pDeref  = (ObfDereferenceObject_t)       g_resolved_apis[API_ObfDereferenceObject];
    pAttach = (KeStackAttachProcess_t)       g_resolved_apis[API_KeStackAttachProcess];
    pDetach = (KeUnstackDetachProcess_t)     g_resolved_apis[API_KeUnstackDetachProcess];
    pGetVa  = (MmGetVirtualForPhysical_t)    g_resolved_apis[API_MmGetVirtualForPhysical];
    if (!pLookup || !pDeref || !pAttach || !pDetach || !pGetVa) {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    st = pLookup((HANDLE)(ULONG_PTR)pid, &target);
    if (!NT_SUCCESS(st) || !target) return STATUS_INVALID_HANDLE;

    pAttach(target, &apc);

    ret = STATUS_SUCCESS;
    cur = va & ~0xFFFULL;
    end_va = (va + size + 0xFFFULL) & ~0xFFFULL;

    while (cur < end_va) {
        ULONGLONG          cr3 = __readcr3() & 0xFFFFFFFFFF000ULL;
        ULONGLONG          table_phys = cr3;
        int                shift = 39;
        ULONGLONG          pte_phys = 0;
        PHYSICAL_ADDRESS   pa;
        ULONGLONG*         entry_va;
        ULONGLONG          entry;
        ULONGLONG*         pte_va;
        ULONGLONG          pte;

        // Walk PML4 -> PDPT -> PD -> PT (or stop at large page).
        while (shift >= 12) {
            ULONG idx = (ULONG)((cur >> shift) & 0x1FF);
            ULONGLONG entry_phys = table_phys + 8ULL * idx;

            pa.QuadPart = (LONGLONG)entry_phys;
            entry_va = (ULONGLONG*)pGetVa(pa);
            if (!entry_va) { ret = STATUS_INVALID_ADDRESS; goto detach; }

            entry = *entry_va;
            if ((entry & 1ULL) == 0) {
                // Not present. For freshly cross-proc-written user pages,
                // the page may not yet be paged in; skip silently.
                pte_phys = 0;
                break;
            }

            // PS bit set at PDPT (1GB) or PD (2MB) means this entry IS the leaf.
            if (shift == 12 || (entry & (1ULL << 7)) != 0) {
                pte_phys = entry_phys;
                break;
            }

            table_phys = entry & 0xFFFFFFFFFF000ULL;
            shift -= 9;
        }

        if (pte_phys != 0) {
            pa.QuadPart = (LONGLONG)pte_phys;
            pte_va = (ULONGLONG*)pGetVa(pa);
            if (!pte_va) { ret = STATUS_INVALID_ADDRESS; goto detach; }

            pte = *pte_va;
            if (flags & 0x1u) pte &= ~(1ULL << 63);  // clear NX -> exec allowed
            if (flags & 0x8u) pte |=  (1ULL << 63);  // set NX  -> exec denied
            if (flags & 0x2u) pte |=  (1ULL << 1);   // set W   -> writable
            if (flags & 0x4u) pte &= ~(1ULL << 1);   // clear W -> read-only
            *pte_va = pte;

            // Local TLB flush. Other CPUs: skipped — fresh pages have no
            // entries cached elsewhere yet. If pages were previously
            // accessed, caller must follow up with an IPI flush.
            __invlpg((PVOID)(ULONG_PTR)cur);
        }

        cur += 0x1000ULL;
    }

detach:
    pDetach(&apc);
    pDeref(target);
    return ret;
}

// Phase 14.3: HEARTBEAT (cmd 7). Body layout (24 bytes total, spec §5.3):
//   +0x00 magic (u16=0x7C4A, already verified)
//   +0x02 reserved (u16)
//   +0x04 cmd_id  (u32=7, already verified)
//   +0x08 out_byte_ptr (u64, user pointer to receive the heartbeat byte)
//   +0x10 padding (8 bytes)
// Writes the byte 0x01 to *out_byte_ptr. Launcher reads to confirm alive.
static NTSTATUS cmd_heartbeat(UCHAR* body, ULONG body_size) {
    PVOID  out_ptr;
    UCHAR  beat = 1;
    if (body_size < 0x18) return STATUS_INVALID_PARAMETER;
    out_ptr = (PVOID)*(ULONGLONG*)(body + 8);
    if (!is_usermode_range(out_ptr, 1)) return STATUS_INVALID_PARAMETER;
    if (!safe_memcpy(out_ptr, &beat, 1)) return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

// Our HAL dispatch hook. When NtConvertBetweenAuxiliaryCounterAndPerformanceCounter
// fires with BOOLEAN FALSE, kernel's guard-dispatch-icall loads this via the
// hooked slot and calls. a1 is the caller-supplied ULONG64 value which for
// cheat commands is the user-mode cmd_buf address.
//
// Phase 14.2.a: entry-guards only (previous mode, null check, user-range,
// 16-byte auth header read). Body decrypt + dispatch added in 14.2.c-e.
static NTSTATUS our_hal_dispatcher(PVOID a1, PVOID a2, PVOID a3, PVOID a4) {
    ExGetPreviousMode_fn_t pPrev;
    PVOID       cmd_buf;
    ULONG       buffer_size;
    UCHAR       auth_header[12];
    UCHAR       body[PHASE14_CMD_BODY_MAX];
    unsigned long long seed;
    USHORT      magic;
    ULONG       cmd_id;
    NTSTATUS    status;

    // Only cheat commands arrive from user-mode. Kernel callers = legit, forward.
    pPrev = (ExGetPreviousMode_fn_t)g_resolved_apis[API_ExGetPreviousMode];
    if (!pPrev || pPrev() != UserMode) goto passthrough;

    cmd_buf = a1;
    if (!cmd_buf) goto passthrough;

    // Spec §5.1: 16-byte header = 4-byte size + 12-byte nonce, then body.
    if (!is_usermode_range(cmd_buf, 16)) goto passthrough;
    if (!safe_memcpy(&buffer_size, cmd_buf, 4)) goto passthrough;
    if (!safe_memcpy(auth_header, (UCHAR*)cmd_buf + 4, 12)) goto passthrough;

    // Spec §5.4: 1 <= buffer_size <= 0x7FF.
    if (buffer_size == 0 || (buffer_size - 1) > 0x7FF) goto passthrough;
    if (buffer_size < 8) goto passthrough;  // minimum for magic + cmd_id
    if (!is_usermode_range((UCHAR*)cmd_buf + 16, buffer_size)) goto passthrough;

    // Copy encrypted body to stack, decrypt with seed derived from key+nonce.
    if (!safe_memcpy(body, (UCHAR*)cmd_buf + 16, buffer_size)) goto passthrough;
    seed = splitmix_derive_seed(g_cipher_key, auth_header);
    splitmix_xor_stream(body, buffer_size, &seed);

    // Magic gate: reject if decrypted header's u16 doesn't match. This is the
    // auth mechanism — only callers that know the shared key can produce a
    // ciphertext that decrypts to the right magic.
    magic = *(USHORT*)body;
    if (magic != PHASE14_CMD_MAGIC) goto passthrough;

    // Spec §5.2: cmd_id at offset 4 (after 2-byte magic + 2-byte reserved).
    cmd_id = *(ULONG*)(body + 4);

    // Phase 14.3+ fills in handlers. Placeholder: just return NOT_IMPLEMENTED
    // so launcher can detect we reached the dispatch point.
    status = STATUS_NOT_IMPLEMENTED;
    switch (cmd_id) {
        case 0:
            status = cmd_read_memory(body, buffer_size);
            break;
        case 1:
            status = cmd_write_memory(body, buffer_size);
            break;
        case 2:
            status = cmd_find_module(body, buffer_size);
            break;
        case 3:
            status = cmd_alloc_vm(body, buffer_size);
            break;
        case 4:
            status = cmd_free_vm(body, buffer_size);
            break;
        case 5:
            status = cmd_protect_vm(body, buffer_size);
            break;
        case 6:
            status = cmd_query_vm(body, buffer_size);
            break;
        case 7:
            status = cmd_heartbeat(body, buffer_size);
            break;
        case 8:
            status = cmd_set_pte_nx(body, buffer_size);
            break;
        case 9:
            status = cmd_arm_image_notify(body, buffer_size);
            break;
        case 10:
            status = cmd_query_armed_pid(body, buffer_size);
            break;
        case 11:
            status = cmd_create_thread(body, buffer_size);
            break;
        default:
            status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    // Zero the body on exit so decrypted plaintext doesn't linger on the
    // kernel stack after return. Matches sample hygiene.
    RtlSecureZeroMemory(body, buffer_size);

    // Status propagates via NTSTATUS return → NtConvert's RAX → pNtConv's
    // return value in user-mode. Sample also writes status to a2 (kernel-stack
    // slot) so NtConvert surfaces it in the user's out-ptr; we'll wire that
    // write once we need structured responses (Phase 14.5+).
    (void)a2; (void)a3; (void)a4;
    return status;

passthrough:
    if (g_hal_original) return g_hal_original(a1, a2, a3, a4);
    return STATUS_SUCCESS;
}

// Decrypt one encrypted-name blob into a local buffer and resolve via walker.
// Returns function VA or NULL. Buffer must be >= entry->len.
static PVOID resolve_encrypted(
    PVOID module_base,
    const phase14_api_entry_t* entry,
    unsigned char* tmp_buf,
    ULONG tmp_buf_size)
{
    if (!entry || !tmp_buf || entry->len > tmp_buf_size) return NULL;
    RtlCopyMemory(tmp_buf, entry->blob, entry->len);
    lcg_xor_apply(tmp_buf, entry->len, entry->key);
    // Defense: require null terminator at expected position.
    if (tmp_buf[entry->len - 1] != 0) return NULL;
    return resolve_export(module_base, (const char*)tmp_buf, NULL);
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    phase14_scratch_t* scratch;
    ULONG walked;
    PVOID ntbase;
    ULONG i;
    ULONG failed_mask;
    ULONG resolved_count;
    unsigned char decrypt_buf[64];
    NTSTATUS final_status;

    UNREFERENCED_PARAMETER(DriverObject);

    scratch = (phase14_scratch_t*)RegistryPath;
    if (!scratch) {
        return STATUS_INVALID_PARAMETER;
    }

    scratch->status         = PHASE14_STATUS_NOT_FOUND;
    scratch->ntoskrnl_base  = 0;
    scratch->resolved_count = 0;
    scratch->failed_mask    = 0;

    // Decrypt the wide module name on-stack so no plaintext L"ntoskrnl.exe"
    // literal lives in the driver's data sections. The encrypted blob
    // (enc_ntoskrnl_wide, 26 bytes = 13 wchars) is in .rdata alongside the
    // 19 ASCII name blobs; key is KEY_enc_ntoskrnl_wide.
    {
        WCHAR ntos_name[LEN_enc_ntoskrnl_wide_wchars];
        RtlCopyMemory(ntos_name, enc_ntoskrnl_wide, sizeof(ntos_name));
        lcg_xor_apply_wide((unsigned short*)ntos_name,
                           LEN_enc_ntoskrnl_wide_wchars,
                           KEY_enc_ntoskrnl_wide);
        walked = 0;
        ntbase = find_module_base(ntos_name, 12, &walked);
    }
    UNREFERENCED_PARAMETER(walked);

    if (!ntbase) {
        scratch->magic = PHASE14_SCRATCH_MAGIC;
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    scratch->ntoskrnl_base = (ULONGLONG)(ULONG_PTR)ntbase;
    scratch->status        = PHASE14_STATUS_OK;

    // Phase 14.0.e: full 19-entry encrypted-name resolver.
    failed_mask = 0;
    resolved_count = 0;
    for (i = 0; i < API_COUNT; ++i) {
        PVOID va = resolve_encrypted(ntbase, &g_encrypted_apis[i],
                                     decrypt_buf, sizeof(decrypt_buf));
        g_resolved_apis[i] = va;
        if (va) {
            ++resolved_count;
        } else {
            failed_mask |= (1u << i);
        }
    }
    scratch->resolved_count = resolved_count;
    scratch->failed_mask    = failed_mask;

    // Phase 14.1.a: HAL dispatch hook install.
    // Pattern scan ntoskrnl .text for the NtConvert... call sequence,
    // follow RIP-rel MOV to HAL dispatch slot, atomic-swap our dispatcher.
    scratch->hal_hook_status = PHASE14_STATUS_NOT_FOUND;
    if (failed_mask == 0) {
        UCHAR* text_start = NULL;
        SIZE_T text_size  = 0;
        static const UCHAR PAT[]  = {
            0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
            0xE8, 0x00, 0x00, 0x00, 0x00,
            0x8B, 0xC8, 0x00, 0x00,
            0x48, 0x8B
        };
        static const UCHAR MASK[] = {
            1, 1, 1, 0, 0, 0, 0,
            1, 0, 0, 0, 0,
            1, 1, 0, 0,
            1, 1
        };
        // NtConvert is pageable — lives in PAGE, not .text. Scan both.
        {
            static const char* const exec_sections[] = { "PAGE", ".text" };
            UCHAR* match = NULL;
            ULONG  si;
            for (si = 0; si < sizeof(exec_sections)/sizeof(exec_sections[0]); ++si) {
                if (find_section(ntbase, exec_sections[si], &text_start, &text_size)) {
                    match = find_pattern(text_start, text_size, PAT, MASK, sizeof(PAT));
                    if (match) break;
                }
            }
            if (match) {
                // mov rax, [rip+disp32]; disp32 at match+3..+6, insn ends at match+7.
                LONG  disp     = *(LONG*)(match + 3);
                PVOID slot_va  = (PVOID)(match + 7 + (INT64)disp);
                PVOID* slot    = (PVOID*)slot_va;

                g_hal_slot_ptr = slot;
                g_hal_original = (hal_dispatch_fn_t)(*slot);
                // Aligned 8-byte store: naturally atomic on x64.
                *slot = (PVOID)&our_hal_dispatcher;

                scratch->hal_hook_status = PHASE14_STATUS_OK;
            }
        }
    }

    // Match sample: any resolver failure aborts driver init.
    final_status = (failed_mask == 0) ? STATUS_SUCCESS : STATUS_PROCEDURE_NOT_FOUND;

    // Magic written LAST — launcher polls on this.
    scratch->magic = PHASE14_SCRATCH_MAGIC;
    return final_status;
}

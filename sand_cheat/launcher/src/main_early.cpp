#include <windows.h>
#include <tlhelp32.h>
#include <conio.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include "byovd.h"
#include "cmdchannel.h"
#include "crypto.h"
#include "ioctl.h"
#include "ntapi.h"
#include "pagewalk.h"
#include "syscall_hijack.h"
#include "kern_scan.h"
#include "kern_map.h"
#include "forensic_cleanup.h"
#include "parse_stage2.h"
#include "resolve_imports.h"
#include "map_stage2.h"
#include "ui.h"

#include <string>
#include <cstdarg>

static void llog(const char* fmt, ...) {
    FILE* f = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\launcher_trace.txt", "a");
    if (!f) return;
    fprintf(f, "[%lu] ", GetTickCount());
    va_list a; va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fflush(f); fclose(f);
}

#include "../common/common_defs.h"
#include "../common/phase14_scratch.h"
#include "../common/phase14_encrypted_names.h"

namespace {

bool read_kva_local(HANDLE device, uint64_t cr3, uint64_t kva, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size) {
        uint64_t page_off = kva & 0xFFFULL;
        size_t   chunk    = (size < (0x1000 - page_off)) ? size : (0x1000 - page_off);
        uint64_t phys     = 0;
        if (!pagewalk::va_to_phys(device, cr3, kva, phys)) return false;
        if (!ioctl::read_physical(device, phys, out, chunk)) return false;
        kva  += chunk;
        out  += chunk;
        size -= chunk;
    }
    return true;
}

bool write_kva_local(HANDLE device, uint64_t cr3, uint64_t kva, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size) {
        uint64_t page_off = kva & 0xFFFULL;
        size_t   chunk    = (size < (0x1000 - page_off)) ? size : (0x1000 - page_off);
        uint64_t phys     = 0;
        if (!pagewalk::va_to_phys(device, cr3, kva, phys)) return false;
        if (!ioctl::write_physical(device, phys, in, chunk)) return false;
        kva  += chunk;
        in   += chunk;
        size -= chunk;
    }
    return true;
}

void banner() {
    std::printf("[*] sand_cheat launcher (early-inject) — phase 1 (mapper + comm)\n");
    std::printf("[*] build: %s %s\n", __DATE__, __TIME__);
}

bool is_elevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e{};
    DWORD sz = 0;
    BOOL ok = GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &sz);
    CloseHandle(tok);
    return ok && e.TokenIsElevated;
}

bool load_embedded_blob(int resource_id, std::vector<uint8_t>& out) {
    HRSRC h = FindResourceA(nullptr, MAKEINTRESOURCEA(resource_id), MAKEINTRESOURCEA(10));
    if (!h) { std::printf("[!] FindResource %d failed (err=%lu)\n", resource_id, GetLastError()); return false; }
    HGLOBAL g = LoadResource(nullptr, h);
    if (!g) return false;
    DWORD size = SizeofResource(nullptr, h);
    const void* ptr = LockResource(g);
    if (!ptr || !size) return false;
    out.assign(
        static_cast<const uint8_t*>(ptr),
        static_cast<const uint8_t*>(ptr) + size);
    return true;
}

// Shellcode constants — same layout as invoke_stage2.cpp.
constexpr uint32_t SHELLCODE_SIZE        = 0x66;
constexpr uint32_t PATCH_PDATA_VA_OFF    = 0x0E;
constexpr uint32_t PATCH_PDATA_COUNT_OFF = 0x17;
constexpr uint32_t PATCH_BASE_FT_OFF     = 0x1D;
constexpr uint32_t PATCH_RTLADDFT_OFF    = 0x27;
constexpr uint32_t PATCH_STAGE2_BASE_OFF = 0x33;
constexpr uint32_t PATCH_ENTRY_VA_OFF    = 0x45;
constexpr uint32_t PATCH_MARKER_VA_OFF   = 0x51;

static const uint8_t SHELLCODE_TEMPLATE[SHELLCODE_SIZE] = {
    0x55,
    0x48, 0x89, 0xE5,
    0x48, 0x83, 0xE4, 0xF0,
    0x48, 0x83, 0xEC, 0x20,
    0x48, 0xB9, 0,0,0,0,0,0,0,0,
    0xBA, 0,0,0,0,
    0x49, 0xB8, 0,0,0,0,0,0,0,0,
    0x48, 0xB8, 0,0,0,0,0,0,0,0,
    0xFF, 0xD0,
    0x48, 0xB9, 0,0,0,0,0,0,0,0,
    0xBA, 0x01, 0x00, 0x00, 0x00,
    0x45, 0x33, 0xC0,
    0x48, 0xB8, 0,0,0,0,0,0,0,0,
    0xFF, 0xD0,
    0x48, 0xB8, 0,0,0,0,0,0,0,0,
    0xC7, 0x00, 0xBE, 0xBA, 0xFE, 0xCA,
    0x33, 0xC0,
    0x48, 0x89, 0xEC,
    0x5D,
    0xC3,
};

constexpr uint32_t MARKER_DONE = 0xCAFEBABEu;

static bool alloc_safe(uint32_t pid, uint64_t size, uint32_t alloc_type,
                       uint32_t protect, uint64_t* out_base) {
    // The 0x70000000-0x7FFF'0000'0000 band collides with system/vendor DLL
    // loads; drivers, AV, and BE modules cluster there. Prefer addresses
    // above 0x1'0000'0000 where the classic module band ends.
    static const uint64_t kHints[] = {
        0x1'0000'0000ull,
        0x2'0000'0000ull,
        0x4'0000'0000ull,
        0x8'0000'0000ull,
        0x10'0000'0000ull,
        0x20'0000'0000ull,
    };
    for (uint64_t hint : kHints) {
        *out_base = 0;
        if (cmdchannel::alloc_memory(pid, hint, size, alloc_type, protect, out_base)
            && *out_base != 0) {
            return true;
        }
    }
    *out_base = 0;
    return cmdchannel::alloc_memory(pid, 0, size, alloc_type, protect, out_base)
           && *out_base != 0;
}

} // namespace

#define IDR_WINIO64       101
#define IDR_KERNELDRIVER  102

int main(int argc, char* argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);
    banner();

    SetConsoleCtrlHandler([](DWORD ctrl) -> BOOL {
        if (ctrl == CTRL_C_EVENT || ctrl == CTRL_BREAK_EVENT ||
            ctrl == CTRL_CLOSE_EVENT || ctrl == CTRL_LOGOFF_EVENT ||
            ctrl == CTRL_SHUTDOWN_EVENT) {
            (void)cmdchannel::unhook_hal();
            ExitProcess(0);
        }
        return FALSE;
    }, TRUE);

    auto bail = [](int code) -> int {
        (void)cmdchannel::unhook_hal();
        std::printf("\n[*] press any key to exit...\n");
        _getch();
        return code;
    };

    {
        FILE* f = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\launcher_trace.txt", "w");
        if (f) { fprintf(f, "=== LAUNCHER (EARLY) START ===\ntick=%lu pid=%lu\n", GetTickCount(), GetCurrentProcessId()); fflush(f); fclose(f); }
    }

    if (!is_elevated()) {
        std::printf("[!] not elevated — NtLoadDriver will fail. Run as administrator.\n");
        return bail(2);
    }

    if (!ntapi::init()) {
        std::printf("[!] ntapi::init failed (ntdll resolve)\n");
        return bail(3);
    }

    // Preflight: HVCI check
    {
        SYSTEM_CODEINTEGRITY_INFORMATION ci = {};
        ci.Length = sizeof(ci);
        NTSTATUS cs = ntapi::NtQuerySystemInformation(
            ntapi::SystemCodeIntegrityInformation, &ci, sizeof(ci), nullptr);
        if (NT_SUCCESS(cs) && (ci.CodeIntegrityOptions & CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED)) {
            std::printf("\n[!] HVCI (Hypervisor-Protected Code Integrity) is ENABLED\n");
            std::printf("[!] (CodeIntegrityOptions=0x%08X)\n", ci.CodeIntegrityOptions);
            std::printf("[!] iqvw64e.sys may be blocked while HVCI is on.\n");
            std::printf("[!] NtLoadDriver will fail silently while HVCI is on.\n\n");
            std::printf("[!] Disable HVCI to proceed:\n");
            std::printf("[!]   Windows Security -> Device security -> Core isolation\n");
            std::printf("[!]     -> toggle \"Memory integrity\" OFF, then reboot.\n\n");
            return bail(5);
        }
    }

    // Preflight: WdFilter check
    if (uint64_t wdf_base = kern_scan::resolve_loaded_driver_base("WdFilter.sys")) {
        std::printf("\n[!] WdFilter.sys is loaded at %016llX\n",
                    static_cast<unsigned long long>(wdf_base));
        std::printf("[!] This replica does not scrub WdFilter's driver-tracking\n");
        std::printf("[!] records. Loading the BYOVD now would leave a detectable\n");
        std::printf("[!] trace that survives the rest of the forensic cleanup\n");
        std::printf("[!] pipeline.\n\n");
        std::printf("[!] Disable Windows Defender fully before running:\n");
        std::printf("[!]   * Use Defender Control (Sordum) or equivalent to\n");
        std::printf("[!]     stop the Defender services AND WdFilter.\n");
        std::printf("[!]   * Verify the driver is NOT loaded:\n");
        std::printf("[!]       driverquery.exe /v | findstr -i wdfilter\n");
        std::printf("[!]       -> State must be \"Stopped\"\n");
        std::printf("[!]       fltmc.exe filters\n");
        std::printf("[!]       -> WdFilter must not appear in the list\n\n");
        return bail(4);
    }
    std::printf("[+] preflight: WdFilter.sys not loaded — safe to proceed\n");

    if (!cmdchannel::init()) {
        std::printf("[!] cmdchannel::init failed (NtConvert resolve)\n");
        return bail(6);
    }

    // Always remap — the running driver may be stale (missing new commands).
    // New HAL hook overwrites the old one; leaked kernel memory is negligible.
    bool driver_already_up = false;
    std::printf("[*] proceeding with full BYOVD + driver install (always remaps)\n");

    std::vector<uint8_t> directio;
    std::vector<uint8_t> kdriver;

    if (!load_embedded_blob(IDR_WINIO64, directio)) return bail(1);
    if (!load_embedded_blob(IDR_KERNELDRIVER, kdriver)) return bail(1);

    std::printf("[+] loaded WinIo64 blob: %zu bytes (encrypted)\n", directio.size());
    std::printf("[+] loaded kerneldriver blob: %zu bytes (encrypted)\n", kdriver.size());

    byovd::Context byovd_ctx;

    if (!byovd::decrypt_and_drop(directio, byovd_ctx)) {
        std::printf("[!] BYOVD decrypt_and_drop failed (err=%lu)\n", GetLastError());
        return bail(10);
    }
    if (!byovd::load_service(byovd_ctx)) {
        std::printf("[!] BYOVD load_service failed (err=%lu)\n", GetLastError());
        byovd::unload(byovd_ctx); return bail(11);
    }
    if (!byovd::open_device(byovd_ctx)) {
        std::printf("[!] BYOVD open_device failed (err=%lu)\n", GetLastError());
        byovd::unload(byovd_ctx); return bail(12);
    }

    std::printf("[*] BYOVD fully up — device handle ready.\n");

    {
        uint8_t buf[16] = {};
        if (ioctl::read_physical(byovd_ctx.device, 0x1000, buf, sizeof(buf))) {
            bool all_zero = true;
            for (int i = 0; i < 16; ++i) if (buf[i] != 0) { all_zero = false; break; }
            std::printf("[+] phys 0x1000 readable (first 16 %s)\n",
                        all_zero ? "zero" : "nonzero");
        } else {
            std::printf("[!] read_physical(0x1000) failed\n");
        }
    }

    uint64_t cr3 = 0;
    if (ioctl::get_pml4_phys(byovd_ctx.device, cr3)) {
        std::printf("[+] CR3 (PML4 phys) = %016llX\n",
                    static_cast<unsigned long long>(cr3));

        const uint64_t kuser = 0xFFFFF78000000000ULL;
        uint64_t phys = 0;
        if (pagewalk::va_to_phys(byovd_ctx.device, cr3, kuser, phys)) {
            std::printf("[+] walked KUSER_SHARED_DATA (%016llX) -> phys %016llX\n",
                        static_cast<unsigned long long>(kuser),
                        static_cast<unsigned long long>(phys));

            uint8_t first32[32] = {};
            if (ioctl::read_physical(byovd_ctx.device, phys, first32, sizeof(first32))) {
                uint32_t tick_dep = *reinterpret_cast<uint32_t*>(first32 + 0x00);
                uint32_t tick_mul = *reinterpret_cast<uint32_t*>(first32 + 0x04);
                uint32_t int_lo   = *reinterpret_cast<uint32_t*>(first32 + 0x08);
                bool ok = (tick_dep == 0) && (tick_mul != 0) && (int_lo != 0);
                std::printf("    TickMul=%08X IntTime=%08X TickDep=%08X  -> walker %s\n",
                            tick_mul, int_lo, tick_dep,
                            ok ? "VERIFIED" : "SUSPECT");
            }
        } else {
            std::printf("[!] page walk failed for KUSER_SHARED_DATA\n");
        }

        syscall_hijack::Context hij;
        if (syscall_hijack::init(byovd_ctx.device, cr3, hij)) {
            uint32_t pe_off = 0;
            uint32_t ntoskrnl_tds = 0;
            if (read_kva_local(byovd_ctx.device, cr3,
                               hij.ntoskrnl_base_va + 0x3C, &pe_off, 4) &&
                read_kva_local(byovd_ctx.device, cr3,
                               hij.ntoskrnl_base_va + pe_off + 8, &ntoskrnl_tds, 4)) {
                std::printf("[*] ntoskrnl TimeDateStamp = 0x%08X "
                            "(send with logs if patterns miss)\n", ntoskrnl_tds);
            }

            uint64_t kqpc = syscall_hijack::resolve_kernel_export(
                hij, hij.ntoskrnl_base_va, "KeQueryPerformanceCounter");
            if (kqpc) {
                std::printf("[+] hijack: KeQueryPerformanceCounter = %016llX\n",
                            (unsigned long long)kqpc);
                syscall_hijack::smoke_test(hij, kqpc);
            } else {
                std::printf("[!] hijack: KeQueryPerformanceCounter resolve failed\n");
            }

            uint64_t alloc_fn = 0;

            kern_scan::SectionRange sects[32];
            int nsect = kern_scan::find_sections(byovd_ctx.device, cr3,
                                                 hij.ntoskrnl_base_va, sects, 32);
            std::printf("[+] ntoskrnl sections parsed: %d\n", nsect);
            const auto* text = kern_scan::section_by_name(sects, nsect, ".text");
            if (text) {
                std::printf("    .text @ %016llX..%016llX (%llu bytes)\n",
                            (unsigned long long)text->va_start,
                            (unsigned long long)text->va_end,
                            (unsigned long long)(text->va_end - text->va_start));

                static const uint8_t PAT[]  = {
                    0x41, 0x8B, 0xD6,
                    0xB9, 0x00, 0x10, 0x00, 0x00,
                    0xE8, 0x00, 0x00, 0x00, 0x00,
                    0x48, 0x8B, 0xD8
                };
                static const uint8_t MASK[] = { 1,1,1, 1,1,1,1,1, 1,0,0,0,0, 1,1,1 };

                std::printf("[*] scanning .text for MmAllocateIndependentPages pattern...\n");
                uint64_t match = kern_scan::scan_pattern(
                    byovd_ctx.device, cr3,
                    text->va_start, text->va_end,
                    PAT, MASK, sizeof(PAT));
                if (!match) {
                    std::printf("[!] MmAllocateIndependentPages pattern not found in .text\n");
                } else {
                    std::printf("[+] pattern match @ %016llX (call is at +8 = %016llX)\n",
                                (unsigned long long)match,
                                (unsigned long long)(match + 8));
                    alloc_fn = kern_scan::resolve_rel32_call(
                        byovd_ctx.device, cr3, match + 8);
                    if (!alloc_fn) {
                        std::printf("[!] resolve_rel32_call failed\n");
                    } else {
                        std::printf("[+] MmAllocateIndependentPages = %016llX\n",
                                    (unsigned long long)alloc_fn);

                        std::vector<uint8_t> kdriver_plain = kdriver;
                        crypto::rolling_xor(kdriver_plain.data(), kdriver_plain.size());

                        uint32_t size_of_image = 0;
                        if (kdriver_plain.size() >= sizeof(IMAGE_DOS_HEADER)) {
                            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(kdriver_plain.data());
                            if (dos->e_magic == IMAGE_DOS_SIGNATURE &&
                                dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) <= kdriver_plain.size()) {
                                auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
                                    kdriver_plain.data() + dos->e_lfanew);
                                if (nt->Signature == IMAGE_NT_SIGNATURE) {
                                    size_of_image = nt->OptionalHeader.SizeOfImage;
                                }
                            }
                        }
                        if (!size_of_image) {
                            std::printf("[!] kdriver PE parse failed or SizeOfImage=0 — skipping map phase\n");
                        } else {
                        size_of_image = (size_of_image + 0xFFF) & ~0xFFFu;
                        std::printf("[*] kdriver SizeOfImage (page-aligned) = 0x%X\n", size_of_image);

                        uint64_t allocated = 0;
                        if (syscall_hijack::invoke(hij, alloc_fn,
                                                   size_of_image, 0, 0, 0, allocated)) {
                            std::printf("[+] MmAllocateIndependentPages(0x%X, 0) -> %016llX\n",
                                        size_of_image, (unsigned long long)allocated);
                            if (allocated < 0xFFFF800000000000ULL) {
                                std::printf("    -> NOT a kernel VA (upper bits wrong)\n");
                            } else {
                                std::printf("    -> plausible kernel VA\n");

                                int fixups = 0;
                                if (!kern_map::process_relocations(
                                        kdriver_plain.data(), kdriver_plain.size(),
                                        allocated, fixups)) {
                                    std::printf("[!] process_relocations failed\n");
                                }

                                if (kern_map::copy_sections(
                                        byovd_ctx.device, cr3,
                                        kdriver_plain.data(), kdriver_plain.size(),
                                        allocated)) {
                                    std::printf("[+] sections copied to kernel\n");
                                    kern_map::verify_bytes_at(
                                        byovd_ctx.device, cr3,
                                        kdriver_plain.data(), kdriver_plain.size(),
                                        allocated, 0x1000, 16);

                                    std::printf("[*] resolving imports...\n");
                                    kern_map::ImportResolveStats imp_stats{};
                                    kern_map::resolve_imports(
                                        byovd_ctx.device, cr3,
                                        kdriver_plain.data(), kdriver_plain.size(),
                                        allocated, imp_stats);

                                    uint64_t pte_base = 0;
                                    {
                                        static const uint8_t PTE_PAT[]  = { 0x48, 0xC1, 0xE9, 0x09, 0x48, 0xB8 };
                                        static const uint8_t PTE_MASK[] = { 1, 1, 1, 1, 1, 1 };
                                        llog("scanning for MiGetPteAddress pattern...\n");
                                        uint64_t pte_match = kern_scan::scan_pattern(
                                            byovd_ctx.device, cr3,
                                            text->va_start, text->va_end,
                                            PTE_PAT, PTE_MASK, sizeof(PTE_PAT));
                                        if (!pte_match) {
                                            std::printf("[!] MiGetPteAddress pattern not found — cannot set section protection\n");
                                            llog("FAIL: MiGetPteAddress pattern not found\n");
                                            byovd::unload(byovd_ctx);
                                            return bail(31);
                                        }
                                        llog("MiGetPteAddress pattern @ %016llX\n", (unsigned long long)pte_match);

                                        uint8_t fn_bytes[32] = {};
                                        if (!ioctl::read_kernel_virtual(byovd_ctx.device, pte_match, fn_bytes, 32)) {
                                            std::printf("[!] MiGetPteAddress read failed\n");
                                            llog("FAIL: MiGetPteAddress bytes read\n");
                                            byovd::unload(byovd_ctx);
                                            return bail(31);
                                        }

                                        if (fn_bytes[17] != 0x48 || (fn_bytes[18] != 0xB8 && fn_bytes[18] != 0xB9)) {
                                            std::printf("[!] MiGetPteAddress layout unexpected at +17: %02X %02X (want 48 B8/B9)\n",
                                                        fn_bytes[17], fn_bytes[18]);
                                            std::printf("    raw: ");
                                            for (int bi = 0; bi < 32; ++bi) std::printf("%02X ", fn_bytes[bi]);
                                            std::printf("\n");
                                            llog("FAIL: MiGetPteAddress unexpected layout at +17: %02X %02X\n",
                                                 fn_bytes[17], fn_bytes[18]);
                                            byovd::unload(byovd_ctx);
                                            return bail(31);
                                        }

                                        std::memcpy(&pte_base, fn_bytes + 19, 8);

                                        std::printf("[+] PTE_BASE = %016llX (from MiGetPteAddress @ %016llX)\n",
                                                    (unsigned long long)pte_base, (unsigned long long)pte_match);
                                        llog("PTE_BASE=%016llX\n", (unsigned long long)pte_base);

                                        if (pte_base < 0xFFFF800000000000ULL || (pte_base & 0xFFF) != 0) {
                                            std::printf("[!] PTE_BASE looks invalid (not kernel-range or not page-aligned)\n");
                                            llog("FAIL: PTE_BASE invalid range/alignment\n");
                                            byovd::unload(byovd_ctx);
                                            return bail(31);
                                        }

                                        uint64_t smoke_pte_va = pte_base + (((allocated >> 12) << 3) & 0x7FFFFFFFF8ULL);
                                        uint64_t smoke_pte = 0;
                                        if (!ioctl::read_kernel_virtual(byovd_ctx.device, smoke_pte_va, &smoke_pte, 8)) {
                                            std::printf("[!] PTE_BASE smoke test: read PTE for allocated base failed\n");
                                            llog("FAIL: PTE_BASE smoke read\n");
                                            byovd::unload(byovd_ctx);
                                            return bail(31);
                                        }
                                        bool pte_present = (smoke_pte & 1) != 0;
                                        std::printf("[*] PTE_BASE smoke: alloc PTE VA=%016llX val=%016llX present=%s\n",
                                                    (unsigned long long)smoke_pte_va,
                                                    (unsigned long long)smoke_pte,
                                                    pte_present ? "YES" : "NO");
                                        llog("PTE_BASE smoke: alloc_va=%016llX pte_va=%016llX val=%016llX present=%s\n",
                                             (unsigned long long)allocated,
                                             (unsigned long long)smoke_pte_va,
                                             (unsigned long long)smoke_pte,
                                             pte_present ? "YES" : "NO");
                                        if (!pte_present) {
                                            std::printf("[!] PTE_BASE smoke FAILED — PTE not present for allocated VA\n");
                                            llog("FAIL: PTE_BASE smoke — not present\n");
                                            byovd::unload(byovd_ctx);
                                            return bail(31);
                                        }
                                    }

                                    std::printf("[*] setting section protections...\n");
                                    llog("apply_section_protection start (base=%016llX pte_base=%016llX)\n",
                                         (unsigned long long)allocated, (unsigned long long)pte_base);
                                    if (!kern_map::apply_section_protection(
                                            byovd_ctx.device, cr3,
                                            kdriver_plain.data(), kdriver_plain.size(),
                                            allocated, pte_base)) {
                                        std::printf("[!] apply_section_protection failed — skipping DriverEntry call\n");
                                        llog("FAIL: apply_section_protection\n");
                                        byovd::unload(byovd_ctx);
                                        return bail(30);
                                    }
                                    llog("apply_section_protection OK\n");

                                    std::printf("[*] flushing TLB for mapped pages...\n");
                                    llog("flush_tlb_range start (va=%016llX size=0x%X)\n",
                                         (unsigned long long)allocated, size_of_image);
                                    if (!syscall_hijack::flush_tlb_range(hij, allocated, size_of_image)) {
                                        std::printf("[!] TLB flush failed — DriverEntry may crash\n");
                                        llog("WARN: flush_tlb_range failed\n");
                                    }
                                    llog("flush_tlb_range done\n");

                                    auto* dos12 = reinterpret_cast<IMAGE_DOS_HEADER*>(
                                        kdriver_plain.data());
                                    auto* nt12  = reinterpret_cast<IMAGE_NT_HEADERS64*>(
                                        kdriver_plain.data() + dos12->e_lfanew);
                                    uint32_t entry_rva = nt12->OptionalHeader.AddressOfEntryPoint;
                                    uint64_t entry_kva = allocated + entry_rva;

                                    {
                                        uint64_t ep_page = entry_kva & ~0xFFFULL;
                                        uint64_t ep_pte_va = pte_base + (((ep_page >> 12) << 3) & 0x7FFFFFFFF8ULL);
                                        uint64_t ep_pte = 0;
                                        ioctl::read_kernel_virtual(byovd_ctx.device, ep_pte_va, &ep_pte, 8);
                                        bool nx_set = (ep_pte >> 63) & 1;
                                        llog("SAFETY CHECK: entry_kva=%016llX pte_va=%016llX pte=%016llX NX=%d\n",
                                             (unsigned long long)entry_kva,
                                             (unsigned long long)ep_pte_va,
                                             (unsigned long long)ep_pte, nx_set);
                                        std::printf("[*] SAFETY CHECK: entry PTE=%016llX NX=%s\n",
                                                    (unsigned long long)ep_pte,
                                                    nx_set ? "SET (WOULD BSOD!)" : "clear (safe)");
                                        if (nx_set) {
                                            std::printf("[!] NX still set on DriverEntry page — aborting to prevent BSOD\n");
                                            std::printf("[!] PTE write did not take effect. Check launcher_trace.txt\n");
                                            llog("ABORT: NX still set, refusing DriverEntry call\n");
                                            byovd::unload(byovd_ctx);
                                            return bail(32);
                                        }
                                    }

                                    uint64_t p14_scratch = 0;
                                    if (syscall_hijack::invoke(hij, alloc_fn,
                                                               0x1000, 0, 0, 0,
                                                               p14_scratch) &&
                                        p14_scratch >= 0xFFFF800000000000ULL) {
                                        std::vector<uint8_t> zero(0x1000, 0);
                                        write_kva_local(byovd_ctx.device, cr3,
                                                        p14_scratch, zero.data(), zero.size());
                                        std::printf("[*] phase14 scratch @ %016llX\n",
                                                    (unsigned long long)p14_scratch);
                                    } else {
                                        std::printf("[!] phase14 scratch alloc failed "
                                                    "(got %016llX)\n",
                                                    (unsigned long long)p14_scratch);
                                        p14_scratch = 0;
                                    }

                                    std::printf("[*] calling DriverEntry via hijack"
                                                " (RVA=%08X, kva=%016llX, "
                                                "scratch=%016llX)\n",
                                                entry_rva,
                                                (unsigned long long)entry_kva,
                                                (unsigned long long)p14_scratch);
                                    uint64_t drv_ret = 0xDEADBEEFDEADBEEFULL;
                                    if (syscall_hijack::invoke(hij, entry_kva,
                                                               0, p14_scratch,
                                                               0, 0, drv_ret)) {
                                        uint32_t ntstatus = static_cast<uint32_t>(drv_ret);
                                        std::printf("[+] DriverEntry returned NTSTATUS=0x%08X %s\n",
                                                    ntstatus,
                                                    ntstatus == 0 ? "(STATUS_SUCCESS)" : "(non-success)");

                                        if (p14_scratch) {
                                            phase14_scratch_t s{};
                                            if (read_kva_local(byovd_ctx.device, cr3,
                                                               p14_scratch, &s, sizeof(s))) {
                                                if (s.magic == PHASE14_SCRATCH_MAGIC) {
                                                    if (s.ntoskrnl_base != hij.ntoskrnl_base_va) {
                                                        std::printf("[!] phase14: ntoskrnl base mismatch "
                                                                    "(launcher=%016llX driver=%016llX)\n",
                                                                    (unsigned long long)hij.ntoskrnl_base_va,
                                                                    (unsigned long long)s.ntoskrnl_base);
                                                    }
                                                    if (s.resolved_count == API_COUNT && s.failed_mask == 0) {
                                                        std::printf("[+] phase14.0: %u/%u encrypted APIs resolved\n",
                                                                    s.resolved_count, API_COUNT);
                                                    } else {
                                                        std::printf("[!] phase14.0: %u/%u resolved mask=0x%08X\n",
                                                                    s.resolved_count, API_COUNT, s.failed_mask);
                                                    }

                                                    std::printf("[+] phase14.1a: hook_status=%u\n",
                                                                s.hal_hook_status);
                                                    if (s.hal_hook_status == PHASE14_STATUS_OK) {
                                                        if (cmdchannel::init()) {
                                                            volatile unsigned char heartbeat_byte = 0;
                                                            uint64_t target =
                                                                reinterpret_cast<uint64_t>(
                                                                    const_cast<unsigned char*>(&heartbeat_byte));
                                                            bool ok = cmdchannel::heartbeat(target);
                                                            if (ok && heartbeat_byte == 1) {
                                                                std::printf("[+] phase14.3: PASS — "
                                                                            "full cmd-channel "
                                                                            "round-trip works\n");
                                                            } else {
                                                                std::printf("[!] phase14.3: FAIL — "
                                                                            "ok=%d byte=0x%02X "
                                                                            "(expected 1)\n",
                                                                            ok, (unsigned)heartbeat_byte);
                                                            }
                                                        } else {
                                                            std::printf("[!] phase14.3: cmdchannel "
                                                                        "init failed (ntdll)\n");
                                                        }
                                                    } else {
                                                        std::printf("[!] phase14.1a: hook NOT installed — "
                                                                    "pattern scan missed\n");
                                                    }
                                                } else {
                                                    std::printf("[!] phase14.b: scratch magic "
                                                                "missing (got %016llX)\n",
                                                                (unsigned long long)s.magic);
                                                }
                                            } else {
                                                std::printf("[!] phase14.b: read_kva on scratch failed\n");
                                            }
                                        }
                                    } else {
                                        std::printf("[!] DriverEntry invoke failed\n");
                                    }
                                } else {
                                    std::printf("[!] copy_sections failed\n");
                                }
                            }
                        }
                        } // end size_of_image else
                    }
                }
            }

            std::printf("[*] forensic: prezero BaseDllName on BYOVD DriverObject\n");
            forensic_cleanup::prezero_driver_object_name(
                byovd_ctx.device, cr3, byovd_ctx.device);

            forensic_cleanup::Scratch scratch;
            if (alloc_fn) {
                forensic_cleanup::allocate_scratch(hij, alloc_fn, 0x1000, scratch);
            } else {
                std::printf("[!] forensic: alloc_fn unresolved, no scratch\n");
            }

            if (scratch.size) {
                std::printf("[*] forensic: scrub PiDDBCacheTable "
                            "(basename=%ls, ts=0x%08X)\n",
                            byovd_ctx.basename.c_str(), byovd_ctx.timestamp);
                forensic_cleanup::scrub_piddb_cache(
                    hij, byovd_ctx.device, cr3,
                    hij.ntoskrnl_base_va, scratch,
                    byovd_ctx.basename.c_str(), byovd_ctx.timestamp);
            }

            std::printf("[*] forensic: scrub ci.dll g_KernelHashBucketList "
                        "(basename=%ls)\n", byovd_ctx.basename.c_str());
            forensic_cleanup::scrub_kernel_hash_bucket(
                hij, byovd_ctx.device, cr3, byovd_ctx.basename.c_str());

            if (scratch.size) {
                forensic_cleanup::free_scratch(hij, byovd_ctx.device, cr3, scratch);
            }
        }
    } else {
        std::printf("[!] CR3 scan failed\n");
    }

    std::printf("[*] unloading BYOVD — cheat driver persists in kernel memory\n");
    byovd::unload(byovd_ctx);

    // =====================================================================
    // Early-inject path: arm image-load notify, wait for game, inject DLL
    // =====================================================================

    std::string picked_path;
    {
        std::string default_dll_path;
        char exe_buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, exe_buf, MAX_PATH);
        if (n != 0 && n < MAX_PATH) {
            for (DWORD i = n; i > 0; --i) {
                if (exe_buf[i - 1] == '\\' || exe_buf[i - 1] == '/') {
                    exe_buf[i] = '\0';
                    break;
                }
            }
            default_dll_path = std::string(exe_buf) + "..\\sand_cheat.dll";

            char temp_dir[MAX_PATH];
            DWORD tlen = GetTempPathA(MAX_PATH, temp_dir);
            if (tlen > 0 && tlen < MAX_PATH) {
                char cfg_path[MAX_PATH];
                int wn = std::snprintf(cfg_path, MAX_PATH, "%ssand_perf_dir.txt", temp_dir);
                if (wn > 0 && wn < MAX_PATH) {
                    HANDLE f = CreateFileA(cfg_path, GENERIC_WRITE, 0, nullptr,
                                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (f != INVALID_HANDLE_VALUE) {
                        DWORD elen = (DWORD)std::strlen(exe_buf);
                        DWORD written = 0;
                        WriteFile(f, exe_buf, elen, &written, nullptr);
                        CloseHandle(f);
                    }
                }
            }
        }

        std::printf("\n[*] opening inject picker (default: %s) ...\n",
                    default_dll_path.c_str());
        if (!ui::prompt_for_dll(default_dll_path, picked_path)) {
            std::printf("[*] inject cancelled by operator\n");
            return bail(0);
        }
        std::printf("[+] selected Stage 2: %s\n", picked_path.c_str());
        llog("selected DLL: %s\n", picked_path.c_str());
    }
    const char* stage2_path = picked_path.c_str();
    llog("stage2_path=%s\n", stage2_path);

    // Parse Stage-2 DLL
    std::vector<uint8_t> stage2;
    {
        HANDLE fh = CreateFileA(stage2_path, GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (fh == INVALID_HANDLE_VALUE) {
            std::printf("[!] cannot open '%s' (err=%lu)\n", stage2_path, GetLastError());
            return bail(12);
        }
        LARGE_INTEGER sz{};
        GetFileSizeEx(fh, &sz);
        stage2.resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ReadFile(fh, stage2.data(), static_cast<DWORD>(stage2.size()), &got, nullptr);
        CloseHandle(fh);
        if (got != stage2.size()) {
            std::printf("[!] short read %lu/%zu\n", got, stage2.size());
            return bail(12);
        }
        std::printf("[+] loaded Stage 2: %zu bytes\n", stage2.size());
        llog("loaded DLL: %zu bytes\n", stage2.size());
    }

    parse_stage2::parsed_stage2 parsed{};
    llog("calling parse_stage2::parse\n");
    if (!parse_stage2::parse(stage2.data(), stage2.size(), parsed)) {
        llog("FAIL: parse_stage2::parse\n");
        std::printf("[!] parse FAIL\n");
        return bail(13);
    }
    llog("parse OK: image_base=%016llX size_of_image=%u entry_rva=%08X sections=%u\n",
        (unsigned long long)parsed.image_base, parsed.size_of_image, parsed.entry_rva,
        parsed.section_count);

    // Arm image-load notify — test heartbeat first to verify command channel.
    // BE respawn pattern: BE spawns Sand.exe once for integrity check, kills
    // it, respawns a fresh one for actual gameplay. We loop the inject phase
    // so each new Sand.exe gets a fresh Stage 2 mapped into it.
    {
        volatile unsigned char hb_test = 0;
        uint64_t hb_addr = reinterpret_cast<uint64_t>(const_cast<unsigned char*>(&hb_test));
        bool hb_ok = cmdchannel::heartbeat(hb_addr);
        std::printf("[*] heartbeat test: rc=%s val=%u\n", hb_ok ? "ok" : "FAIL", (unsigned)hb_test);
        llog("heartbeat test: rc=%s val=%u\n", hb_ok ? "ok" : "FAIL", (unsigned)hb_test);
    }

    uint32_t inject_run = 0;
    for (;; ++inject_run) {
        std::printf("\n[*] === inject cycle %u — arming image-load notify ===\n", inject_run);
        llog("=== inject cycle %u ===\n", inject_run);

        if (inject_run == 0) {
            std::printf("[*] === arm cycle 0 (SKIPPED — throwaway) ===\n");
            llog("arm cycle 0 SKIPPED\n");
            const wchar_t* skip_target = L"Sand.exe";
            size_t skip_tlen = 8;
            unsigned char skip_abody[0x18] = {0};
            *reinterpret_cast<uint16_t*>(skip_abody + 0x00) = 0x7C4A;
            *reinterpret_cast<uint32_t*>(skip_abody + 0x04) = 9;
            *reinterpret_cast<uint16_t*>(skip_abody + 0x08) = static_cast<uint16_t>(skip_tlen);
            *reinterpret_cast<uint64_t*>(skip_abody + 0x10) = reinterpret_cast<uint64_t>(skip_target);
            int32_t arc = cmdchannel::send_raw(skip_abody, sizeof(skip_abody));
            std::printf("[*] arm_image_notify raw NTSTATUS=0x%08X (skip mode)\n", (unsigned)arc);
            llog("arm_image_notify NTSTATUS=0x%08X (skip mode)\n", (unsigned)arc);
            if (arc != 0) {
                std::printf("[!] arm_image_notify failed on skip cycle\n");
                continue;
            }
            std::printf("[*] Waiting for throwaway Sand.exe to appear...\n");
            uint32_t skip_pid = 0;
            while (skip_pid == 0) {
                Sleep(50);
                (void)cmdchannel::query_armed_pid(&skip_pid);
            }
            std::printf("[+] throwaway Sand.exe caught (PID %u) — NOT injecting, waiting for real game spawn\n", skip_pid);
            llog("throwaway pid=%u — skipping inject\n", skip_pid);
            // The arm is one-shot; the driver returned the PID and disarmed
            // internally. Loop increments inject_run and arms again for the
            // next spawn, which is the real game.
            continue;
        }
    {
        const wchar_t* target = L"Sand.exe";
        size_t tlen = 8;
        unsigned char abody[0x18] = {0};
        *reinterpret_cast<uint16_t*>(abody + 0x00) = 0x7C4A;
        *reinterpret_cast<uint32_t*>(abody + 0x04) = 9;
        *reinterpret_cast<uint16_t*>(abody + 0x08) = static_cast<uint16_t>(tlen);
        *reinterpret_cast<uint64_t*>(abody + 0x10) = reinterpret_cast<uint64_t>(target);
        int32_t arc = cmdchannel::send_raw(abody, sizeof(abody));
        std::printf("[*] arm_image_notify raw NTSTATUS=0x%08X\n", (unsigned)arc);
        llog("arm_image_notify NTSTATUS=0x%08X\n", (unsigned)arc);
        if (arc != 0) {
            std::printf("[!] arm_image_notify failed\n");
            return bail(20);
        }
    }
    std::printf("[*] Waiting for game to launch... (start the game now)\n");
    llog("armed image-load notify for sand_be.exe\n");

    // Poll for game PID
    uint32_t game_pid = 0;
    {
        constexpr uint32_t POLL_MS  = 500;
        constexpr uint32_t TIMEOUT  = 120000;
        constexpr uint32_t MAX_ITER = TIMEOUT / POLL_MS;
        for (uint32_t i = 0; i < MAX_ITER; ++i) {
            Sleep(POLL_MS);
            if (cmdchannel::query_armed_pid(&game_pid)) {
                break;
            }
        }
    }
    if (game_pid == 0) {
        std::printf("[!] timeout — game did not launch within 120 s\n");
        return bail(21);
    }
    std::printf("[+] game launched: PID %u\n", game_pid);
    llog("game PID=%u\n", game_pid);

    // Wait for process init (PEB/Ldr population)
    std::printf("[*] waiting for process modules to load...\n");
    Sleep(2000);

    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, game_pid);
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me = {sizeof(me)};
            int count = 0;
            if (Module32FirstW(snap, &me)) {
                do {
                    if (count < 10) {
                        std::printf("[*] usermode module[%d]: %ls base=%p size=0x%X\n",
                                    count, me.szModule, me.modBaseAddr, me.modBaseSize);
                    }
                    count++;
                } while (Module32NextW(snap, &me));
            }
            std::printf("[*] usermode module enumeration: %d total modules found\n", count);
            llog("usermode module enum: %d modules\n", count);
            CloseHandle(snap);
        } else {
            llog("Toolhelp not available (err=%lu), using kernel walker\n", GetLastError());
        }
    }

    // Allocate memory in game
    const uint32_t alloc_size = (parsed.size_of_image + 0xFFF) & ~0xFFFu;
    uint64_t stage2_base = 0;
    if (!alloc_safe(game_pid, alloc_size, 0x3000, 0x04, &stage2_base)) {
        llog("FAIL: alloc_memory for stage2\n");
        std::printf("[!] Stage 2 memory allocation failed\n");
        return bail(14);
    }
    std::printf("[*] stage2 alloc: base=%016llX size=0x%X\n",
                static_cast<unsigned long long>(stage2_base), alloc_size);
    llog("stage2_base=%016llX size=0x%X\n",
        (unsigned long long)stage2_base, alloc_size);

    {
        uint64_t test_base = 0;
        uint32_t test_size = 0;

        unsigned char tbody[0x38] = {0};
        *reinterpret_cast<uint16_t*>(tbody + 0x00) = 0x7C4A;
        *reinterpret_cast<uint32_t*>(tbody + 0x04) = 2;
        *reinterpret_cast<uint32_t*>(tbody + 0x08) = game_pid;
        const wchar_t* test_name = L"KERNEL32.dll";
        size_t tlen = wcslen(test_name);
        *reinterpret_cast<uint16_t*>(tbody + 0x0C) = static_cast<uint16_t>(tlen);
        *reinterpret_cast<uint64_t*>(tbody + 0x10) = reinterpret_cast<uint64_t>(test_name);
        *reinterpret_cast<uint64_t*>(tbody + 0x18) = reinterpret_cast<uint64_t>(&test_base);
        *reinterpret_cast<uint64_t*>(tbody + 0x20) = reinterpret_cast<uint64_t>(&test_size);

        int32_t raw_st = cmdchannel::send_raw(tbody, sizeof(tbody));
        std::printf("[*] DIAG find_module(KERNEL32.dll): NTSTATUS=0x%08X base=%016llX walked=%u\n",
                    (unsigned)raw_st, (unsigned long long)test_base, test_size);
        llog("DIAG find_module KERNEL32: st=0x%08X base=%016llX walked=%u\n",
             (unsigned)raw_st, (unsigned long long)test_base, test_size);
    }

    resolve_imports::stats ri{};
    bool imports_ok = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        ri = {};
        llog("calling resolve_imports pid=%u attempt=%d\n", game_pid, attempt);
        if (resolve_imports::resolve(game_pid, stage2.data(), stage2.size(), parsed, ri)) {
            imports_ok = true;
            break;
        }
        std::printf("[*] imports incomplete (attempt %d), waiting for modules...\n", attempt + 1);
        llog("resolve_imports attempt %d failed, retrying...\n", attempt);
        Sleep(3000);
    }
    if (!imports_ok) {
        llog("FAIL: resolve_imports after all retries\n");
        std::printf("[!] import resolve FAIL after 100 attempts\n");
        cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
        return bail(15);
    }
    llog("resolve_imports OK: dlls_found=%d dlls_missing=%d symbols_ok=%d symbols_missed=%d\n",
        ri.dlls_found, ri.dlls_missing, ri.symbols_ok, ri.symbols_missed);

    // Apply relocations
    int fixups = 0;
    llog("calling process_relocations target_base=%016llX\n", (unsigned long long)stage2_base);
    if (!kern_map::process_relocations(stage2.data(), stage2.size(),
                                       stage2_base, fixups)) {
        llog("FAIL: process_relocations\n");
        std::printf("[!] relocation FAIL\n");
        cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
        return bail(16);
    }
    llog("relocations OK: fixups=%d\n", fixups);

    // Write PE headers to target base
    {
        uint32_t hdr_size = 0x1000;
        if (parsed.size_of_headers && parsed.size_of_headers < hdr_size)
            hdr_size = parsed.size_of_headers;
        if (hdr_size > stage2.size())
            hdr_size = static_cast<uint32_t>(stage2.size());
        if (!cmdchannel::write_memory(game_pid, stage2_base,
                                       reinterpret_cast<uint64_t>(stage2.data()),
                                       hdr_size)) {
            llog("FAIL: write PE headers\n");
            std::printf("[!] PE header write FAIL\n");
            cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
            return bail(17);
        }
        std::printf("[+] PE headers written (%u bytes)\n", hdr_size);
        llog("PE headers written: %u bytes\n", hdr_size);
    }

    // Write sections
    llog("calling write_and_protect pid=%u base=%016llX\n", game_pid, (unsigned long long)stage2_base);
    if (!map_stage2::write_and_protect(game_pid, stage2_base,
                                       stage2.data(), stage2.size(), parsed, true)) {
        llog("FAIL: write_and_protect\n");
        std::printf("[!] write+protect FAIL\n");
        cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
        return bail(17);
    }

    // Clear NX on executable sections via PTE
    llog("clearing NX via PTE for executable sections\n");
    for (uint32_t i = 0; i < parsed.section_count; ++i) {
        const auto& s = parsed.sections[i];
        if (!s.copy || !s.exec) continue;

        const uint64_t dst = stage2_base + s.virtual_address;
        const uint32_t vext = s.virtual_size ? s.virtual_size : s.raw_size;
        const uint32_t sz = (vext + 0xFFF) & ~0xFFFu;

        if (!cmdchannel::set_pte_nx(game_pid, dst, sz, cmdchannel::PTE_FLAG_CLEAR_NX)) {
            llog("FAIL: set_pte_nx for section '%s'\n", s.name);
            std::printf("[!] PTE NX clear failed for '%s'\n", s.name);
            cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
            return bail(17);
        }
        std::printf("[+] PTE NX cleared for '%s' va=%016llX size=%u\n",
                    s.name, static_cast<unsigned long long>(dst), sz);
        llog("PTE NX cleared '%s' va=%016llX size=%u\n",
            s.name, (unsigned long long)dst, sz);
    }

    // =====================================================================
    // Invoker shellcode: allocate, build, write, execute via remote thread
    // =====================================================================
    uint64_t sc_base = 0;
    if (!alloc_safe(game_pid, 0x1000, 0x3000, 0x04, &sc_base)) {
        llog("FAIL: alloc shellcode page\n");
        std::printf("[!] shellcode page allocation failed\n");
        cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
        return bail(22);
    }
    std::printf("[*] shellcode page: %016llX\n",
                static_cast<unsigned long long>(sc_base));
    llog("shellcode page=%016llX\n", (unsigned long long)sc_base);

    const uint64_t marker_va = sc_base + 0x080;
    const uint64_t entry_va  = stage2_base + parsed.entry_rva;
    const uint64_t pdata_va  = stage2_base + parsed.exception_rva;
    const uint32_t pdata_count = parsed.exception_size / 12;

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    uint64_t rtlAddFT = reinterpret_cast<uint64_t>(
        GetProcAddress(ntdll, "RtlAddFunctionTable"));

    uint8_t sc_buf[0x1000] = {};
    uint8_t* sc = sc_buf;
    std::memcpy(sc, SHELLCODE_TEMPLATE, SHELLCODE_SIZE);

    std::memcpy(sc + PATCH_PDATA_VA_OFF,    &pdata_va,      8);
    std::memcpy(sc + PATCH_PDATA_COUNT_OFF, &pdata_count,   4);
    std::memcpy(sc + PATCH_BASE_FT_OFF,     &stage2_base,   8);
    std::memcpy(sc + PATCH_RTLADDFT_OFF,    &rtlAddFT,      8);
    std::memcpy(sc + PATCH_STAGE2_BASE_OFF, &stage2_base,   8);
    std::memcpy(sc + PATCH_ENTRY_VA_OFF,    &entry_va,      8);
    std::memcpy(sc + PATCH_MARKER_VA_OFF,   &marker_va,     8);

    llog("shellcode patched: pdata_va=%016llX count=%u rtlAddFT=%016llX "
         "stage2_base=%016llX entry_va=%016llX marker_va=%016llX\n",
        (unsigned long long)pdata_va, pdata_count, (unsigned long long)rtlAddFT,
        (unsigned long long)stage2_base, (unsigned long long)entry_va,
        (unsigned long long)marker_va);

    if (!cmdchannel::write_memory(game_pid, sc_base,
                                   reinterpret_cast<uint64_t>(sc_buf), 0x1000)) {
        llog("FAIL: shellcode write\n");
        std::printf("[!] shellcode write FAIL\n");
        cmdchannel::free_memory(game_pid, sc_base, 0, 0x8000);
        cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
        return bail(22);
    }

    // Clear NX on shellcode page
    if (!cmdchannel::set_pte_nx(game_pid, sc_base, 0x1000, cmdchannel::PTE_FLAG_CLEAR_NX)) {
        llog("FAIL: set_pte_nx for shellcode page\n");
        std::printf("[!] PTE NX clear failed for shellcode page\n");
        cmdchannel::free_memory(game_pid, sc_base, 0, 0x8000);
        cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
        return bail(22);
    }
    std::printf("[+] shellcode page NX cleared\n");

    {
        HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, game_pid);
        DWORD exit_code = STILL_ACTIVE;
        if (hp) {
            GetExitCodeProcess(hp, &exit_code);
            CloseHandle(hp);
        }
        if (exit_code != STILL_ACTIVE) {
            llog("PRE_THREAD: target dead exit_code=0x%X\n", (unsigned)exit_code);
            std::printf("[!] target dead before thread create exit=0x%X\n", (unsigned)exit_code);
            cmdchannel::free_memory(game_pid, sc_base, 0, 0x8000);
            cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
            return bail(23);
        }
    }

    {
        uint8_t sc_readback[32] = {0};
        int32_t rc_sc = cmdchannel::read_memory_rc(game_pid, sc_base,
            reinterpret_cast<uint64_t>(sc_readback), sizeof(sc_readback));

        if (rc_sc != 0) {
            llog("VERIFY_READ_FAIL: sc_rc=0x%08X\n", (unsigned)rc_sc);
            std::printf("[!] verify read failed sc=0x%08X\n", (unsigned)rc_sc);
            cmdchannel::free_memory(game_pid, sc_base, 0, 0x8000);
            cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
            return bail(22);
        }
        bool sc_ok = (std::memcmp(sc_readback, sc_buf, sizeof(sc_readback)) == 0);
        if (!sc_ok) {
            llog("VERIFY_MISMATCH: sc_ok=%d\n", sc_ok);
            char hex[65]; auto dump = [&](const uint8_t* p, int n){
                for (int i = 0; i < n && i*2 < 64; ++i) std::snprintf(hex + i*2, 3, "%02X", p[i]);
                hex[n*2] = 0;
            };
            dump(sc_readback, 32); llog("  sc_read:  %s\n", hex);
            dump(reinterpret_cast<const uint8_t*>(sc_buf), 32); llog("  sc_want:  %s\n", hex);
            std::printf("[!] verify mismatch sc_ok=%d — write tampered\n", sc_ok);
            cmdchannel::free_memory(game_pid, sc_base, 0, 0x8000);
            cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
            return bail(22);
        }
        llog("VERIFY_OK: shellcode intact\n");
    }

    // Create remote thread
    uint32_t tid = 0;
    if (!cmdchannel::create_remote_thread(game_pid, sc_base, stage2_base, &tid)) {
        llog("FAIL: create_remote_thread\n");
        std::printf("[!] create_remote_thread FAIL\n");
        cmdchannel::free_memory(game_pid, sc_base, 0, 0x8000);
        cmdchannel::free_memory(game_pid, stage2_base, 0, 0x8000);
        return bail(23);
    }
    std::printf("[+] remote thread created: TID %u\n", tid);
    llog("remote thread TID=%u\n", tid);

    // Poll marker for DllMain completion
    {
        constexpr uint32_t POLL_MS  = 50;
        constexpr uint32_t TIMEOUT  = 10000;
        constexpr uint32_t MAX_ITER = TIMEOUT / POLL_MS;
        bool fired = false;
        bool read_failed = false;
        uint32_t elapsed = 0;
        for (uint32_t i = 0; i < MAX_ITER; ++i) {
            Sleep(POLL_MS);
            elapsed += POLL_MS;
            uint32_t mark = 0;
            int32_t rc = cmdchannel::read_memory_rc(game_pid, marker_va,
                                                    reinterpret_cast<uint64_t>(&mark), 4);
            if (rc != 0) {
                int alive;
                DWORD exit_code = 0;
                HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, game_pid);
                if (hp) {
                    if (GetExitCodeProcess(hp, &exit_code)) {
                        alive = (exit_code == STILL_ACTIVE) ? 1 : 0;
                    } else {
                        alive = 1;
                        exit_code = GetLastError();
                    }
                    CloseHandle(hp);
                } else {
                    alive = -1;
                    exit_code = GetLastError();
                }
                llog("READ_FAIL: rc=0x%08X pid_alive=%d exit_code=0x%X iter=%u elapsed=%u ms\n",
                     (unsigned)rc, alive, (unsigned)exit_code, i, elapsed);
                std::printf("[!] marker READ failed: rc=0x%08X pid_alive=%d exit_code=0x%X\n",
                            (unsigned)rc, alive, (unsigned)exit_code);
                read_failed = true;
                break;
            }
            if (mark == MARKER_DONE) {
                llog("MARKER 0xCAFEBABE seen after %u ms\n", elapsed);
                std::printf("[+] MARKER 0xCAFEBABE seen after %u ms — DllMain returned\n", elapsed);
                fired = true;
                break;
            }
        }
        if (!fired) {
            if (!read_failed) {
                llog("TIMEOUT: DllMain did NOT complete in %u ms\n", TIMEOUT);
                std::printf("[!] timeout — DllMain did NOT complete within %u ms\n", TIMEOUT);
            }
            cmdchannel::free_memory(game_pid, sc_base, 0, 0x8000);
            return bail(24);
        }
    }

    // Wait for thread to fully exit, then free shellcode page
    Sleep(3000);
    cmdchannel::free_memory(game_pid, sc_base, 0, 0x8000);
    std::printf("[+] shellcode page freed\n");
    llog("shellcode page freed\n");

    std::printf("[+] Stage 2 is LIVE in PID %u at %016llX (entry=%016llX)\n",
                game_pid,
                static_cast<unsigned long long>(stage2_base),
                static_cast<unsigned long long>(entry_va));
    std::printf("[*] Cycle %u complete — re-arming for next Sand.exe spawn (Ctrl+C to quit)\n",
                inject_run);
    llog("=== inject cycle %u COMPLETE (pid=%u) ===\n", inject_run, game_pid);

    if (inject_run == 1) {
        struct MonCtx { uint32_t pid; };
        MonCtx* ctx = new MonCtx{game_pid};
        HANDLE hMon = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            MonCtx* c = (MonCtx*)p;
            uint32_t pid = c->pid;
            delete c;
            HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (!hp) {
                (void)cmdchannel::unhook_hal();
                std::printf("[!] could not open game PID %u for monitoring; unhook fired, exiting\n", pid);
                ExitProcess(0);
            }
            for (;;) {
                DWORD r = WaitForSingleObject(hp, 2000);
                if (r == WAIT_OBJECT_0) {
                    CloseHandle(hp);
                    (void)cmdchannel::unhook_hal();
                    std::printf("\n[*] game process %u ended - unhook fired, exiting\n", pid);
                    ExitProcess(0);
                }
                if (r == WAIT_FAILED) {
                    CloseHandle(hp);
                    (void)cmdchannel::unhook_hal();
                    std::printf("[!] wait failed on PID %u; unhook fired, exiting\n", pid);
                    ExitProcess(0);
                }
            }
        }, ctx, 0, nullptr);
        if (hMon) CloseHandle(hMon);
    }

    // Loop back to re-arm for the next Sand.exe (BE respawn or manual restart).
    } // end for (;; ++inject_run)
}

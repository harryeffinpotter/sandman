// launcher main — for Phase 1, a single EXE that will:
//   1. self-elevate if not admin (skipped for dev; assume launched elevated)
//   2. decrypt embedded WinIo64.sys + our kerneldriver.sys from RCDATA
//   3. BYOVD lifecycle: drop, load, map our driver, clean forensic caches, unload BYOVD
//   4. open command channel via hooked NtConvert... syscall
//   5. send Cmd 7 heartbeat → verify driver alive
//
// Turn-1 scope: print hello, prove the EXE builds + picks up embedded resources.

#include <windows.h>
#include <tlhelp32.h>
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
#include "rtss_inject.h"
#include "parse_stage2.h"
#include "resolve_imports.h"
#include "map_stage2.h"
#include "invoke_stage2.h"
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

static std::string g_target_name = "sand.exe";

static uint32_t find_target_pid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    uint32_t found = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            bool match = true;
            for (size_t i = 0; i < g_target_name.size(); i++) {
                wchar_t got = pe.szExeFile[i];
                char want = g_target_name[i];
                if (got >= L'A' && got <= L'Z') got = (wchar_t)(got + 32);
                if (want >= 'A' && want <= 'Z') want = (char)(want + 32);
                if ((char)got != want) { match = false; break; }
            }
            if (match) { found = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

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
    std::printf("[*] sand_cheat launcher — phase 1 (mapper + comm)\n");
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
    HRSRC h = FindResourceA(nullptr, MAKEINTRESOURCEA(resource_id), MAKEINTRESOURCEA(10 /* RT_RCDATA */));
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

} // namespace

// Resource IDs (match launcher.rc).
#define IDR_WINIO64       101
#define IDR_KERNELDRIVER  102

int main(int argc, char* argv[]) {
    if (argc > 1) g_target_name = argv[1];

    SetConsoleOutputCP(CP_UTF8);
    banner();

    {
        FILE* f = fopen("C:\\Users\\ysg\\projects\\sand_cheat\\launcher_trace.txt", "w");
        if (f) { fprintf(f, "=== LAUNCHER START ===\ntick=%lu pid=%lu target=%s\n", GetTickCount(), GetCurrentProcessId(), g_target_name.c_str()); fflush(f); fclose(f); }
    }

    if (!is_elevated()) {
        std::printf("[!] not elevated — NtLoadDriver will fail. Run as administrator.\n");
        return 2;
    }

    if (!ntapi::init()) {
        std::printf("[!] ntapi::init failed (ntdll resolve)\n");
        return 3;
    }

    // Preflight: HVCI (Hypervisor-Protected Code Integrity) blocks NtLoadDriver
    // of any driver on Microsoft's HVCI-incompatible blocklist. WinIo64.sys is
    // WHQL-signed but may still appear on updated blocklists. If HVCI is enabled,
    // load fails silently — no clear error surfaces. Detect + bail with guidance.
    {
        SYSTEM_CODEINTEGRITY_INFORMATION ci = {};
        ci.Length = sizeof(ci);
        NTSTATUS cs = ntapi::NtQuerySystemInformation(
            ntapi::SystemCodeIntegrityInformation, &ci, sizeof(ci), nullptr);
        if (NT_SUCCESS(cs) && (ci.CodeIntegrityOptions & CODEINTEGRITY_OPTION_HVCI_KMCI_ENABLED)) {
            std::printf("\n[!] HVCI (Hypervisor-Protected Code Integrity) is ENABLED\n");
            std::printf("[!] (CodeIntegrityOptions=0x%08X)\n", ci.CodeIntegrityOptions);
            std::printf("[!] WinIo64.sys may be blocked while HVCI is on.\n");
            std::printf("[!] NtLoadDriver will fail silently while HVCI is on.\n\n");
            std::printf("[!] Disable HVCI to proceed:\n");
            std::printf("[!]   Windows Security -> Device security -> Core isolation\n");
            std::printf("[!]     -> toggle \"Memory integrity\" OFF, then reboot.\n\n");
            return 5;
        }
    }

    // Preflight: Defender's WdFilter.sys minifilter tracks every driver load
    // in an internal list. This replica does NOT scrub that list (Phase 13.3
    // deferred — static analysis only, see Improvement-report §3.16). If
    // WdFilter is running, a BYOVD load here would leave a persistent
    // kernel-side trace that survives our entire scrub pipeline. Refuse
    // to operate rather than ship a compromised detection profile.
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
        return 4;
    }
    std::printf("[+] preflight: WdFilter.sys not loaded — safe to proceed\n");

    // Preflight: our kernel driver (HAL-hooked manual-mapped module) survives
    // across launcher runs within a single boot session — manual-mapped
    // memory is never freed (cerebrum "Same-boot cross-run hook daisy-chain"
    // entry). Detect a live driver by issuing a heartbeat via the command
    // channel. If the hook is already in place, the heartbeat cmd succeeds
    // AND writes 0x01 to our sentinel byte. If the driver is absent, the
    // syscall routes to the real HAL function which returns whatever but
    // does NOT write our sentinel — we see 0x00 back.
    //
    // Benefits: saves ~10 s of BYOVD drop + manual-map + HAL-hook install
    // on each iteration. Avoids growing the HAL-hook daisy chain across
    // every test run. Still preserves all reforensic state from the first
    // run (PiDDB scrub, ci.dll scrub, DriverObject name zero).
    bool driver_already_up = false;
    if (cmdchannel::init()) {
        volatile unsigned char pf_heartbeat = 0;
        uint64_t pf_target = reinterpret_cast<uint64_t>(
            const_cast<unsigned char*>(&pf_heartbeat));
        (void)cmdchannel::heartbeat(pf_target);
        if (pf_heartbeat == 1) {
            driver_already_up = true;
        }
    }
    if (driver_already_up) {
        std::printf("[+] preflight: kernel driver ALREADY ALIVE (heartbeat returned 0x01)\n");
        std::printf("[*] skipping BYOVD drop + manual-map + HAL-hook install\n");
        std::printf("[*] jumping directly to Phase 16.b\n\n");
    } else {
    std::printf("[*] preflight: kernel driver not present — proceeding with full BYOVD + install\n");

    // Phase 16.a (RTSS drop) removed 2026-04-18. Tester is now responsible for
    // installing RTSS 7.3.x via official Guru3D installer + configuring
    // auto-start + app-detection for target game. Loader only observes +
    // patches; does not touch RTSS file or global hook. See injection-info.txt
    // §2.8 (revision) for rationale.

    std::vector<uint8_t> directio;
    std::vector<uint8_t> kdriver;

    if (!load_embedded_blob(IDR_WINIO64, directio)) return 1;
    if (!load_embedded_blob(IDR_KERNELDRIVER, kdriver)) return 1;

    std::printf("[+] loaded WinIo64 blob: %zu bytes (encrypted)\n", directio.size());
    std::printf("[+] loaded kerneldriver blob: %zu bytes (encrypted)\n", kdriver.size());

    byovd::Context byovd_ctx;

    if (!byovd::decrypt_and_drop(directio, byovd_ctx)) return 10;
    if (!byovd::load_service(byovd_ctx))               { byovd::unload(byovd_ctx); return 11; }
    if (!byovd::open_device(byovd_ctx))                { byovd::unload(byovd_ctx); return 12; }

    std::printf("[*] BYOVD fully up — device handle ready.\n");

    // Task #3 smoke tests:
    //  (a) MAP/UNMAP round trip against phys page 0 (16-byte peek)
    //  (b) CR3 scan against low 1 MB
    ioctl::Handle h;
    if (ioctl::map_physical(byovd_ctx.device, 0, 0x1000, ioctl::RW_READ, h)) {
        uint8_t buf[16] = {};
        std::memcpy(buf, reinterpret_cast<const void*>(h.pvPhysMemLin), sizeof(buf));
        bool all_zero = true;
        for (int i = 0; i < 16; ++i) if (buf[i] != 0) { all_zero = false; break; }
        std::printf("[+] phys 0x0 readable (first 16 %s), unmapped clean\n",
                    all_zero ? "zero" : "nonzero");
        ioctl::unmap_physical(byovd_ctx.device, h);
    } else {
        std::printf("[!] map_physical failed\n");
    }

    uint64_t cr3 = 0;
    if (ioctl::get_pml4_phys(byovd_ctx.device, cr3)) {
        std::printf("[+] CR3 (PML4 phys) = %016llX\n",
                    static_cast<unsigned long long>(cr3));

        // Task #4 smoke test: translate KUSER_SHARED_DATA's kernel VA.
        // Structure starts at 0xFFFFF78000000000 with:
        //   +0x000 TickCountLowDeprecated    (ULONG, zero on x64)
        //   +0x004 TickCountMultiplier       (ULONG, always nonzero ~0x0FA00000)
        //   +0x008 InterruptTime (low/high/high1)  — increments continuously
        //   +0x014 SystemTime                  — wall clock
        // Reading a known pattern back = walker end-to-end proof.
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
            // Informational: ntoskrnl PE TimeDateStamp — helps identify the
            // exact build when diagnosing pattern misses. Non-fatal.
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

            // Hoisted: Phase 13.1 (forensic scrub) needs MmAllocateIndependentPages
            // to stage a kernel-memory search key (SMAP-safe). Declared here so
            // both the map-phase (below) and the scrub-phase (after) can read it.
            uint64_t alloc_fn = 0;

            // Task #6: pattern-scan ntoskrnl .text for MmAllocateIndependentPages
            // call site, resolve the rel32, invoke via hijack, verify allocation.
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

                        // Compute page-aligned size for the decrypted kdriver PE.
                        // kdriver.size() currently is the raw encrypted blob size;
                        // after decrypt the PE has a SizeOfImage we should use.
                        std::vector<uint8_t> kdriver_plain = kdriver;
                        crypto::rolling_xor(kdriver_plain.data(), kdriver_plain.size());

                        // Peek PE headers to determine alloc size.
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
                                                   /*Size*/ size_of_image,
                                                   /*NumaNodeMask*/ 0,
                                                   0, 0, allocated)) {
                            std::printf("[+] MmAllocateIndependentPages(0x%X, 0) -> %016llX\n",
                                        size_of_image, (unsigned long long)allocated);
                            if (allocated < 0xFFFF800000000000ULL) {
                                std::printf("    -> NOT a kernel VA (upper bits wrong)\n");
                            } else {
                                std::printf("    -> plausible kernel VA\n");

                                // Phase 8: relocations on the local decrypted buffer.
                                int fixups = 0;
                                if (!kern_map::process_relocations(
                                        kdriver_plain.data(), kdriver_plain.size(),
                                        allocated, fixups)) {
                                    std::printf("[!] process_relocations failed\n");
                                }

                                // Phase 9: copy sections to kernel memory.
                                if (kern_map::copy_sections(
                                        byovd_ctx.device, cr3,
                                        kdriver_plain.data(), kdriver_plain.size(),
                                        allocated)) {
                                    std::printf("[+] sections copied to kernel\n");
                                    // Read back first 16 bytes of .text (RVA 0x1000
                                    // by convention for MSVC-emitted drivers) and
                                    // compare against local buffer.
                                    kern_map::verify_bytes_at(
                                        byovd_ctx.device, cr3,
                                        kdriver_plain.data(), kdriver_plain.size(),
                                        allocated, /*RVA*/ 0x1000, /*bytes*/ 16);

                                    // Phase 11: resolve imports (walk driver's
                                    // IMAGE_DIRECTORY_ENTRY_IMPORT, resolve each
                                    // function in its target kernel module's
                                    // export table, write resolved VA into the
                                    // IAT slot at kernel_base + FirstThunk + 8*i).
                                    std::printf("[*] resolving imports...\n");
                                    kern_map::ImportResolveStats imp_stats{};
                                    kern_map::resolve_imports(
                                        byovd_ctx.device, cr3,
                                        kdriver_plain.data(), kdriver_plain.size(),
                                        allocated, imp_stats);

                                    // Phase 10: set per-section PTE protection.
                                    // MmAllocateIndependentPages returns RW+NX.
                                    // Need X bit cleared on .text before we try
                                    // to execute DriverEntry, else #PF 0xFC.
                                    std::printf("[*] setting section protections...\n");
                                    if (!kern_map::apply_section_protection(
                                            byovd_ctx.device, cr3,
                                            kdriver_plain.data(), kdriver_plain.size(),
                                            allocated)) {
                                        std::printf("[!] apply_section_protection failed — skipping DriverEntry call\n");
                                        byovd::unload(byovd_ctx);
                                        return 30;
                                    }

                                    // Phase 12: call DriverEntry via hijack.
                                    auto* dos12 = reinterpret_cast<IMAGE_DOS_HEADER*>(
                                        kdriver_plain.data());
                                    auto* nt12  = reinterpret_cast<IMAGE_NT_HEADERS64*>(
                                        kdriver_plain.data() + dos12->e_lfanew);
                                    uint32_t entry_rva = nt12->OptionalHeader.AddressOfEntryPoint;
                                    uint64_t entry_kva = allocated + entry_rva;

                                    // Phase 14.0.b dev scaffold: allocate kernel
                                    // scratch page, pass its KVA as RegistryPath
                                    // slot so DriverEntry can write PsLoadedModuleList
                                    // walk result back. Removed once 14.1 HAL hook
                                    // provides the real command channel.
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
                                                               /*DriverObject*/ 0,
                                                               /*RegistryPath*/ p14_scratch,
                                                               0, 0, drv_ret)) {
                                        uint32_t ntstatus = static_cast<uint32_t>(drv_ret);
                                        std::printf("[+] DriverEntry returned NTSTATUS=0x%08X %s\n",
                                                    ntstatus,
                                                    ntstatus == 0 ? "(STATUS_SUCCESS)" : "(non-success)");

                                        // Read scratch back and validate.
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

                                                    // Phase 14.1.a: HAL hook install report.
                                                    std::printf("[+] phase14.1a: hook_status=%u\n",
                                                                s.hal_hook_status);
                                                    if (s.hal_hook_status == PHASE14_STATUS_OK) {
                                                        // Phase 14.3 HEARTBEAT end-to-end: launcher
                                                        // sends encrypted cmd 7, driver dispatcher
                                                        // decodes, writes byte 1 to our user-mode
                                                        // target, returns STATUS_SUCCESS. Proves the
                                                        // full pipeline: hook fires -> entry guards
                                                        // pass -> cipher decrypts -> magic+cmd_id
                                                        // validate -> handler runs -> return status.
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

                                                            // Phase 14.5 READ_MEMORY self-to-self test.
                                                            // Driver reads launcher's src_buf -> launcher's
                                                            // dst_buf via MmCopyVirtualMemory(target=current).
                                                            unsigned char src_buf[32];
                                                            unsigned char dst_buf[32] = {0};
                                                            for (int i = 0; i < 32; ++i) {
                                                                src_buf[i] = (unsigned char)(0xA0 + i);
                                                            }
                                                            bool rd_ok = cmdchannel::read_memory(
                                                                GetCurrentProcessId(),
                                                                reinterpret_cast<uint64_t>(src_buf),
                                                                reinterpret_cast<uint64_t>(dst_buf),
                                                                sizeof(src_buf));
                                                            bool bytes_match = (memcmp(src_buf, dst_buf, sizeof(src_buf)) == 0);
                                                            std::printf("[*] phase14.5: READ self-to-self "
                                                                        "rc=%s match=%s dst[0..4]=%02X %02X %02X %02X\n",
                                                                        rd_ok ? "SUCCESS" : "FAIL",
                                                                        bytes_match ? "YES" : "NO",
                                                                        dst_buf[0], dst_buf[1],
                                                                        dst_buf[2], dst_buf[3]);
                                                            if (rd_ok && bytes_match) {
                                                                std::printf("[+] phase14.5: PASS — "
                                                                            "cross-process READ works\n");
                                                            } else {
                                                                std::printf("[!] phase14.5: FAIL — "
                                                                            "rc=%d match=%d\n",
                                                                            rd_ok, bytes_match);
                                                            }

                                                            // Phase 14.6 WRITE_MEMORY self-to-self test.
                                                            // Driver copies launcher src_buf2 -> launcher
                                                            // dst_buf2 via MmCopyVirtualMemory with
                                                            // direction reversed (current -> target=current).
                                                            unsigned char src_buf2[32];
                                                            unsigned char dst_buf2[32] = {0};
                                                            for (int i = 0; i < 32; ++i) {
                                                                src_buf2[i] = (unsigned char)(0x50 + i);
                                                            }
                                                            bool wr_ok = cmdchannel::write_memory(
                                                                GetCurrentProcessId(),
                                                                reinterpret_cast<uint64_t>(dst_buf2),
                                                                reinterpret_cast<uint64_t>(src_buf2),
                                                                sizeof(src_buf2));
                                                            bool wr_match = (memcmp(src_buf2, dst_buf2, sizeof(src_buf2)) == 0);
                                                            std::printf("[*] phase14.6: WRITE self-to-self "
                                                                        "rc=%s match=%s dst[0..4]=%02X %02X %02X %02X\n",
                                                                        wr_ok ? "SUCCESS" : "FAIL",
                                                                        wr_match ? "YES" : "NO",
                                                                        dst_buf2[0], dst_buf2[1],
                                                                        dst_buf2[2], dst_buf2[3]);
                                                            if (wr_ok && wr_match) {
                                                                std::printf("[+] phase14.6: PASS — "
                                                                            "cross-process WRITE works\n");
                                                            } else {
                                                                std::printf("[!] phase14.6: FAIL — "
                                                                            "rc=%d match=%d\n",
                                                                            wr_ok, wr_match);
                                                            }

                                                            // Phase 14.7 FIND_MODULE self-test. Ask driver
                                                            // to find ntdll.dll in our own PEB; compare
                                                            // against GetModuleHandleW.
                                                            uint64_t fm_base = 0;
                                                            uint32_t fm_size = 0;
                                                            bool fm_ok = cmdchannel::find_module(
                                                                GetCurrentProcessId(),
                                                                L"ntdll.dll",
                                                                &fm_base, &fm_size);
                                                            uint64_t expected =
                                                                reinterpret_cast<uint64_t>(GetModuleHandleW(L"ntdll.dll"));
                                                            std::printf("[*] phase14.7: FIND_MODULE ntdll.dll "
                                                                        "rc=%s base=%016llX size=%u expect=%016llX\n",
                                                                        fm_ok ? "SUCCESS" : "FAIL",
                                                                        (unsigned long long)fm_base, fm_size,
                                                                        (unsigned long long)expected);
                                                            if (fm_ok && fm_base == expected && fm_size > 0) {
                                                                std::printf("[+] phase14.7: PASS — "
                                                                            "PEB Ldr walk works\n");
                                                            } else {
                                                                std::printf("[!] phase14.7: FAIL — "
                                                                            "base mismatch or zero size\n");
                                                            }

                                                            // Phase 15.0 + 15.1 ALLOC + FREE self-test.
                                                            // Alloc 4 KB in our own process (PAGE_READWRITE),
                                                            // write a marker via Cmd 1 WRITE, read back via
                                                            // direct access (we own the region), verify, free.
                                                            uint64_t alloc_base = 0;
                                                            bool al_ok = cmdchannel::alloc_memory(
                                                                GetCurrentProcessId(),
                                                                0, 0x1000, 0x3000 /* COMMIT|RESERVE */,
                                                                0x04 /* PAGE_READWRITE */,
                                                                &alloc_base);
                                                            std::printf("[*] phase15.0: ALLOC rc=%s base=%016llX\n",
                                                                        al_ok ? "SUCCESS" : "FAIL",
                                                                        (unsigned long long)alloc_base);
                                                            bool alloc_ok = false;
                                                            if (al_ok && alloc_base) {
                                                                unsigned char marker[16];
                                                                for (int i = 0; i < 16; ++i) marker[i] = (unsigned char)(0xE0 + i);
                                                                bool w_ok = cmdchannel::write_memory(
                                                                    GetCurrentProcessId(),
                                                                    alloc_base,
                                                                    reinterpret_cast<uint64_t>(marker),
                                                                    16);
                                                                bool match = (memcmp(reinterpret_cast<void*>(alloc_base), marker, 16) == 0);
                                                                std::printf("[*] phase15.0: write+verify w_ok=%d match=%s\n",
                                                                            w_ok, match ? "YES" : "NO");
                                                                alloc_ok = w_ok && match;
                                                            }
                                                            if (alloc_ok) {
                                                                std::printf("[+] phase15.0: PASS — "
                                                                            "ALLOC + WRITE to alloc works\n");
                                                            } else {
                                                                std::printf("[!] phase15.0: FAIL\n");
                                                            }
                                                            // Phase 15.2 PROTECT self-test: flip alloc to
                                                            // PAGE_EXECUTE_READWRITE; old_protect should read
                                                            // back as PAGE_READWRITE (0x04).
                                                            if (al_ok && alloc_base) {
                                                                uint32_t old_prot = 0;
                                                                bool pr_ok = cmdchannel::protect_memory(
                                                                    GetCurrentProcessId(),
                                                                    alloc_base, 0x1000,
                                                                    0x40 /* PAGE_EXECUTE_READWRITE */,
                                                                    &old_prot);
                                                                std::printf("[*] phase15.2: PROTECT rc=%s old=0x%02X (expect 0x04)\n",
                                                                            pr_ok ? "SUCCESS" : "FAIL", old_prot);
                                                                if (pr_ok && old_prot == 0x04) {
                                                                    std::printf("[+] phase15.2: PASS — PROTECT works\n");
                                                                } else {
                                                                    std::printf("[!] phase15.2: FAIL\n");
                                                                }
                                                            }

                                                            // Phase 15.3 QUERY self-test: MBI for the alloc
                                                            // should report MEM_COMMIT state, RegionSize >= 4K,
                                                            // Protect matching the current (post-15.2) value.
                                                            if (al_ok && alloc_base) {
                                                                unsigned char mbi[48] = {0};
                                                                bool q_ok = cmdchannel::query_memory(
                                                                    GetCurrentProcessId(), alloc_base, mbi);
                                                                uint64_t base_addr  = *reinterpret_cast<uint64_t*>(mbi + 0x00);
                                                                uint64_t alloc_b    = *reinterpret_cast<uint64_t*>(mbi + 0x08);
                                                                uint32_t alloc_prot = *reinterpret_cast<uint32_t*>(mbi + 0x10);
                                                                uint64_t region_sz  = *reinterpret_cast<uint64_t*>(mbi + 0x18);
                                                                uint32_t state      = *reinterpret_cast<uint32_t*>(mbi + 0x20);
                                                                uint32_t protect    = *reinterpret_cast<uint32_t*>(mbi + 0x24);
                                                                uint32_t type       = *reinterpret_cast<uint32_t*>(mbi + 0x28);
                                                                std::printf("[*] phase15.3: QUERY rc=%s "
                                                                            "base=%016llX sz=%llX state=%X prot=%X type=%X\n",
                                                                            q_ok ? "SUCCESS" : "FAIL",
                                                                            (unsigned long long)base_addr,
                                                                            (unsigned long long)region_sz,
                                                                            state, protect, type);
                                                                (void)alloc_b; (void)alloc_prot;
                                                                if (q_ok && base_addr == alloc_base &&
                                                                    region_sz >= 0x1000 && state == 0x1000 &&
                                                                    type == 0x20000) {
                                                                    std::printf("[+] phase15.3: PASS — QUERY returns valid MBI\n");
                                                                } else {
                                                                    std::printf("[!] phase15.3: FAIL\n");
                                                                }
                                                            }

                                                            if (al_ok && alloc_base) {
                                                                bool fr_ok = cmdchannel::free_memory(
                                                                    GetCurrentProcessId(),
                                                                    alloc_base, 0, 0x8000 /* MEM_RELEASE */);
                                                                std::printf("[%s] phase15.1: FREE rc=%s\n",
                                                                            fr_ok ? "+" : "!",
                                                                            fr_ok ? "SUCCESS" : "FAIL");
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
                        } // end of else { map phase
                    }
                }
            }

            // Phase 13.0: zero our BYOVD's BaseDllName.Length so the upcoming
            // NtUnloadDriver's MiRememberUnloadedDriver tripwire early-returns
            // and NO entry is recorded for this run.
            std::printf("[*] forensic: prezero BaseDllName on BYOVD DriverObject\n");
            forensic_cleanup::prezero_driver_object_name(
                byovd_ctx.device, cr3, byovd_ctx.device);

            // Allocate ONE persistent kernel scratch page for all Phase 13.x
            // scrubs. Reuse across PiDDB, KernelHash, WdFilter — no per-call
            // alloc/free churn visible in MI allocator records. Freed below.
            forensic_cleanup::Scratch scratch;
            if (alloc_fn) {
                forensic_cleanup::allocate_scratch(hij, alloc_fn, 0x1000, scratch);
            } else {
                std::printf("[!] forensic: alloc_fn unresolved, no scratch\n");
            }

            // Phase 13.1: scrub PiDDBCacheTable / PiDDBCacheList entry for our
            // BYOVD. Removes (BaseDllName, TimeDateStamp) from ci.dll's
            // per-boot driver-validated cache. Mirrors sample technique.
            if (scratch.size) {
                std::printf("[*] forensic: scrub PiDDBCacheTable "
                            "(basename=%ls, ts=0x%08X)\n",
                            byovd_ctx.basename.c_str(), byovd_ctx.timestamp);
                forensic_cleanup::scrub_piddb_cache(
                    hij, byovd_ctx.device, cr3,
                    hij.ntoskrnl_base_va, scratch,
                    byovd_ctx.basename.c_str(), byovd_ctx.timestamp);
            }

            // Phase 13.2: scrub ci.dll g_KernelHashBucketList entry. Removes
            // BYOVD basename from ci.dll's driver-validation hash cache.
            // Manual list walk via physmem (no scratch needed — no kernel
            // comparator involved).
            std::printf("[*] forensic: scrub ci.dll g_KernelHashBucketList "
                        "(basename=%ls)\n", byovd_ctx.basename.c_str());
            forensic_cleanup::scrub_kernel_hash_bucket(
                hij, byovd_ctx.device, cr3, byovd_ctx.basename.c_str());

            // Wipe + free persistent scratch. Removes all BYOVD-identifier
            // residue from kernel memory before unload.
            if (scratch.size) {
                forensic_cleanup::free_scratch(hij, byovd_ctx.device, cr3, scratch);
            }
        }
    } else {
        std::printf("[!] CR3 scan failed\n");
    }

    // BYOVD has served its purpose (cheat driver manual-mapped; forensic
    // scrubs done). Cheat driver is memory-resident and reachable via HAL
    // hook. Unload BYOVD NOW, before the injection phase — no reason to
    // keep WinIo64.sys visible a moment longer than needed.
    std::printf("[*] unloading BYOVD — cheat driver persists in kernel memory\n");
    byovd::unload(byovd_ctx);
    }  // end else (driver_already_up == false) — BYOVD + install block

    // Phase 16.b: operator-gated RTSS injection.
    //
    // Gate + DLL picker live in a single small Win32 modal dialog. The
    // operator launches the target game, picks the Stage-2 DLL to map
    // (defaults to sand_cheat.dll next to the launcher EXE), and clicks
    // Inject. Cancel/close/Esc exits without side effects — BYOVD has
    // already unloaded, cheat driver stays resident from this run.
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

            // Tell sand_cheat.dll where to drop its log file. We write the
            // launcher's own directory to %TEMP%\sand_cheat_logdir.txt; the
            // DLL reads that on first LogFmt call. Cross-process handoff
            // (launcher process -> game process where DLL runs) without
            // touching the target's PEB.
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
            return 0;
        }
        std::printf("[+] selected Stage 2: %s\n", picked_path.c_str());
        llog("selected DLL: %s\n", picked_path.c_str());
    }
    const char* stage2_path = picked_path.c_str();
    llog("stage2_path=%s\n", stage2_path);

    uint32_t game_pid = find_target_pid();
    llog("find_target_pid returned %u\n", game_pid);
    if (game_pid == 0) {
        llog("FAIL: target not found\n");
        std::printf("[!] target process not found — launch the game first, then re-run\n");
        return 7;
    }
    std::printf("[+] target found: PID %u\n", game_pid);
    llog("target PID=%u\n", game_pid);

    rtss_inject::Ctx rctx;
    llog("calling wait_for_rtss_in_game pid=%u\n", game_pid);
    bool landed = rtss_inject::wait_for_rtss_in_game(game_pid, rctx, 10000);
    llog("wait_for_rtss returned %d rtss_base=%016llX parking_min=%08X parking_max=%08X\n",
        landed?1:0, (unsigned long long)rctx.rtss_base_in_game,
        rctx.parking_min_rva, rctx.parking_max_rva);
    if (!landed) {
        llog("FAIL: RTSS not in target\n");
        std::printf("[!] Phase 16.b: FAIL — RTSS not in target\n");
        return 9;
    }
    rtss_inject::verify_in_game_rtss(game_pid, rctx);
    std::printf("[+] Phase 16.b: RTSS detected in game (tester-supplied install)\n");
    llog("RTSS verified in game\n");

    std::printf("\n  Phase 16.b.beta: parking-zone probe\n");
    llog("parking probe: min_rva=%08X max_rva=%08X\n", rctx.parking_min_rva, rctx.parking_max_rva);
    if (rctx.parking_min_rva == 0) {
        llog("SKIP: no parking zone\n");
        std::printf("[!] Phase 16.b.beta: SKIP — unknown build, no parking zone defined\n");
        return 0;
    }
    bool probed = rtss_inject::probe_parking_zone(game_pid, rctx);
    llog("probe_parking_zone returned %d\n", probed?1:0);
    if (probed) {
        std::printf("[+] Phase 16.b.beta: parking zone R/W validated\n");
    } else {
        llog("FAIL: parking probe\n");
        std::printf("[!] Phase 16.b.beta: FAIL — parking-zone probe did not complete cleanly\n");
        return 10;
    }

    std::printf("\n  Phase 16.b.gamma: DX12 A50F0 redirect smoke\n");
    int live_slot = -1;
    rtss_inject::sweep_d3d12_slots(game_pid, rctx, &live_slot);
    llog("sweep_d3d12_slots: live_slot=%d\n", live_slot);
    bool redirected = false;
    if (live_slot >= 0) {
        redirected = rtss_inject::redirect_d3d12_smoke(game_pid, rctx);
        llog("redirect_d3d12_smoke returned %d\n", redirected?1:0);
    } else {
        std::printf("[*] Phase 16.b.gamma: direct-mode cluster empty — trying interop path\n");
        redirected = rtss_inject::redirect_d3d12_interop_smoke(game_pid, rctx);
        llog("redirect_d3d12_interop_smoke returned %d\n", redirected?1:0);
    }
    if (!redirected) {
        std::printf("[*] Phase 16.b.gamma: DX12 paths unavailable — trying DX11 Present slot\n");
        redirected = rtss_inject::redirect_d3d11_present_smoke(game_pid, rctx);
        llog("redirect_d3d11_present_smoke returned %d\n", redirected?1:0);
    }
    if (redirected) {
        std::printf("[+] Phase 16.b.gamma: redirect chain validated end-to-end\n");
        llog("redirect PASS\n");
    } else {
        llog("FAIL: all redirect paths failed\n");
        std::printf("[!] Phase 16.b.gamma: FAIL — stub did not fire (target may be non-rendering)\n");
        return 11;
    }

    // =====================================================================
    // Phase 16.c.delta: Stage-2 map-only smoke (Path A).
    //
    // Validates the 16.c.a–d pipeline without invoking DllMain:
    //   (1) load Stage 2 DLL from disk (test rig — production will decrypt
    //       from embedded .enc via the same pre-build pipeline as kerneldriver)
    //   (2) parse_stage2::parse -> parsed_stage2 struct
    //   (3) resolve_imports::resolve -> IAT patched in launcher buffer
    //       (cross-proc PEB walk + export table reads via cmdchannel)
    //   (4) kern_map::process_relocations -> DIR64 fixups with delta =
    //       parking_base - image_base, in launcher buffer
    //   (5) map_stage2::write_and_protect -> cross-proc writes per section
    //       at parking_base + section.VA, per-section protection flip
    //   (6) bytewise read-back verify: each copied section must match the
    //       launcher-side prepared buffer exactly
    //   (7) scrub parking (zeros + restore RW protection) — keeps RTSS clean
    //
    // No DllMain call, no shellcode, no widget. Validates mapper correctness
    // only. Missing failure modes: IAT contents semantic correctness,
    // reloc arithmetic producing runnable code. Those surface at 16.c.e.
    // =====================================================================
    std::printf("\n  Phase 16.c.delta: Stage-2 map-only smoke\n");

    // stage2_path already set by the picker dialog above (16.b gate).

    std::vector<uint8_t> stage2;
    {
        HANDLE fh = CreateFileA(stage2_path, GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, 0, nullptr);
        if (fh == INVALID_HANDLE_VALUE) {
            std::printf("[!] Phase 16.c.delta: cannot open '%s' (err=%lu)\n"
                        "    expected next to launcher EXE\n",
                        stage2_path, GetLastError());
            return 12;
        }
        LARGE_INTEGER sz{};
        GetFileSizeEx(fh, &sz);
        stage2.resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        ReadFile(fh, stage2.data(), static_cast<DWORD>(stage2.size()), &got, nullptr);
        CloseHandle(fh);
        if (got != stage2.size()) {
            std::printf("[!] Phase 16.c.delta: short read %lu/%zu\n", got, stage2.size());
            return 12;
        }
        std::printf("[+] loaded Stage 2: %zu bytes\n", stage2.size());
        llog("loaded DLL: %zu bytes\n", stage2.size());
    }

    parse_stage2::parsed_stage2 parsed{};
    llog("calling parse_stage2::parse\n");
    if (!parse_stage2::parse(stage2.data(), stage2.size(), parsed)) {
        llog("FAIL: parse_stage2::parse\n");
        std::printf("[!] Phase 16.c.delta: parse FAIL\n");
        return 13;
    }
    llog("parse OK: image_base=%016llX size_of_image=%u entry_rva=%08X sections=%u import_rva=%08X reloc_rva=%08X\n",
        (unsigned long long)parsed.image_base, parsed.size_of_image, parsed.entry_rva,
        parsed.section_count, parsed.import_rva, parsed.reloc_rva);
    for (uint32_t si = 0; si < parsed.section_count; si++) {
        const auto& ss = parsed.sections[si];
        llog("  section[%u] %-8s va=%08X vsz=%08X raw=%08X rsz=%08X %c%c%c %s\n",
            si, ss.name, ss.virtual_address, ss.virtual_size, ss.raw_offset, ss.raw_size,
            ss.read?'R':'-', ss.write?'W':'-', ss.exec?'X':'-', ss.copy?"COPY":"SKIP");
    }

    const uint32_t parking_size = (parsed.size_of_image + 0xFFF) & ~0xFFFu;
    uint64_t parking_base = 0;
    if (!cmdchannel::alloc_memory(game_pid, 0, parking_size,
                                   0x3000, 0x04,
                                   &parking_base)) {
        llog("FAIL: alloc_memory for stage2\n");
        std::printf("[!] Stage 2 memory allocation failed\n");
        return 14;
    }
    std::printf("[*] stage2 alloc: base=%016llX size=0x%X\n",
                static_cast<unsigned long long>(parking_base), parking_size);
    llog("stage2_base=%016llX size=0x%X (standalone RW, will flip .text to RX)\n",
        (unsigned long long)parking_base, parking_size);

    // Pre-write readback: confirm the chosen slot is zero. Non-zero means we
    // either overlap with live RTSS state or with earlier smoke leftovers —
    // bail so we don't stomp anything.
    {
        std::vector<uint8_t> probe(parking_size, 0xFF);
        if (!cmdchannel::read_memory(game_pid, parking_base,
                                     reinterpret_cast<uint64_t>(probe.data()),
                                     parking_size)) {
            std::printf("[!] parking pre-write probe read FAIL\n");
            return 14;
        }
        bool all_zero = true;
        for (uint32_t i = 0; i < parking_size; ++i) {
            if (probe[i] != 0) { all_zero = false; break; }
        }
        std::printf("[*] parking pre-write = %s\n",
                    all_zero ? "ZERO (clean)" : "NONZERO (overlap risk)");
        llog("parking pre-write zero check: %s\n", all_zero ? "ZERO" : "NONZERO");
        if (!all_zero) {
            llog("FAIL: parking not zero, aborting\n");
            std::printf("[!] aborting to avoid clobbering live RTSS state\n");
            return 14;
        }
    }

    resolve_imports::stats ri{};
    llog("calling resolve_imports pid=%u\n", game_pid);
    if (!resolve_imports::resolve(game_pid, stage2.data(), stage2.size(), parsed, ri)) {
        llog("FAIL: resolve_imports\n");
        std::printf("[!] Phase 16.c.delta: import resolve FAIL\n");
        return 15;
    }
    llog("resolve_imports OK: dlls_found=%d dlls_missing=%d symbols_ok=%d symbols_missed=%d forwarders=%d\n",
        ri.dlls_found, ri.dlls_missing, ri.symbols_ok, ri.symbols_missed, ri.forwarders);

    int fixups = 0;
    llog("calling process_relocations target_base=%016llX\n", (unsigned long long)parking_base);
    if (!kern_map::process_relocations(stage2.data(), stage2.size(),
                                       parking_base, fixups)) {
        llog("FAIL: process_relocations\n");
        std::printf("[!] Phase 16.c.delta: relocation FAIL\n");
        return 16;
    }
    llog("relocations OK: fixups=%d\n", fixups);

    llog("calling write_and_protect pid=%u base=%016llX\n", game_pid, (unsigned long long)parking_base);
    if (!map_stage2::write_and_protect(game_pid, parking_base,
                                       stage2.data(), stage2.size(), parsed, true)) {
        llog("FAIL: write_and_protect\n");
        std::printf("[!] Phase 16.c.delta: write+protect FAIL\n");
        cmdchannel::free_memory(game_pid, parking_base, 0, 0x8000);
        return 17;
    }

    // Bytewise readback per copied section.
    int matched = 0, mismatched = 0;
    for (uint32_t i = 0; i < parsed.section_count; ++i) {
        const auto& s = parsed.sections[i];
        if (!s.copy || s.raw_size == 0) continue;
        std::vector<uint8_t> rb(s.raw_size, 0);
        if (!cmdchannel::read_memory(game_pid, parking_base + s.virtual_address,
                                     reinterpret_cast<uint64_t>(rb.data()),
                                     s.raw_size)) {
            std::printf("[!]   readback '%s' read FAIL\n", s.name);
            mismatched++;
            continue;
        }
        if (std::memcmp(rb.data(), stage2.data() + s.raw_offset, s.raw_size) == 0) {
            std::printf("[+]   '%s' (%u B) MATCH\n", s.name, s.raw_size);
            matched++;
        } else {
            std::printf("[!]   '%s' (%u B) MISMATCH\n", s.name, s.raw_size);
            mismatched++;
        }
    }
    std::printf("[+] readback: %d matched, %d mismatched\n", matched, mismatched);
    llog("readback: matched=%d mismatched=%d\n", matched, mismatched);

    if (mismatched != 0 || matched == 0) {
        llog("FAIL: readback mismatch or zero matched\n");
        cmdchannel::free_memory(game_pid, parking_base, 0, 0x8000);
        std::printf("[!] Phase 16.c.delta: FAIL\n");
        return 18;
    }
    std::printf("[+] Phase 16.c.delta: PASS — map pipeline bytewise validated\n");

    llog("clearing NX via PTE for executable sections\n");
    for (uint32_t i = 0; i < parsed.section_count; ++i) {
        const auto& s = parsed.sections[i];
        if (!s.copy) continue;
        if (!s.exec) continue;

        const uint64_t dst = parking_base + s.virtual_address;
        const uint32_t vext = s.virtual_size ? s.virtual_size : s.raw_size;
        const uint32_t size = (vext + 0xFFF) & ~0xFFFu;

        if (!cmdchannel::set_pte_nx(game_pid, dst, size, 0x1)) {
            llog("FAIL: set_pte_nx for section '%s'\n", s.name);
            std::printf("[!] PTE NX clear failed for '%s'\n", s.name);
            cmdchannel::free_memory(game_pid, parking_base, 0, 0x8000);
            return 17;
        }
        std::printf("[+] PTE NX cleared for '%s' va=%016llX size=%u\n",
                    s.name, static_cast<unsigned long long>(dst), size);
        llog("PTE NX cleared '%s' va=%016llX size=%u\n",
            s.name, (unsigned long long)dst, size);
    }

    // =====================================================================
    // Phase 16.c.epsilon: Stage-2 DllMain invocation (full E2E).
    //
    // Stage 2 is mapped + protected. Now we need DllMain to actually run.
    // invoke_dllmain:
    //   - picks a SECOND parking slot (non-overlapping with Stage 2)
    //   - stages a ~1 KB invoker parking: fake vtable + 102-byte shellcode
    //     that registers .pdata via RtlAddFunctionTable, then calls
    //     DllMain(stage2_base, 1, NULL) and writes 0xCAFEBABE to a marker
    //   - flips EA90[live_slot] to the fake vtable
    //   - polls the marker for up to 5 s
    //   - restores EA90 immediately on success (or timeout)
    //   - scrubs the invoker parking; Stage 2 stays resident
    //
    // After this returns, Stage 2 is live. Its init ran on the game's
    // render thread inside RTSS's Present chain. E900 detour is installed
    // on the game's widget dispatcher. On subsequent paints the state
    // machine (maybe_finalize) will find the target widget and install
    // the VMT shadow — red box appears in-game. No further launcher work.
    // =====================================================================
    std::printf("\n  Phase 16.c.epsilon: Stage-2 DllMain invocation\n");
    llog("calling invoke_dllmain pid=%u parking_base=%016llX parking_size=0x%X entry_rva=%08X\n",
        game_pid, (unsigned long long)parking_base, parking_size, parsed.entry_rva);
    llog("DllMain absolute addr = %016llX\n", (unsigned long long)(parking_base + parsed.entry_rva));

    if (!invoke_stage2::invoke_dllmain(game_pid, rctx,
                                       parking_base, parking_size,
                                       parsed.entry_rva,
                                       parsed.exception_rva,
                                       parsed.exception_size)) {
        llog("FAIL: invoke_dllmain\n");
        std::printf("[!] Phase 16.c.epsilon: FAIL — scrubbing Stage 2 region\n");
        cmdchannel::free_memory(game_pid, parking_base, 0, 0x8000);
        return 19;
    }

    std::printf("[+] Phase 16.c.epsilon: PASS — DllMain returned cleanly\n");
    llog("invoke_dllmain SUCCEEDED\n");
    llog("Stage 2 LIVE at %016llX entry=%016llX\n",
        (unsigned long long)parking_base, (unsigned long long)(parking_base + parsed.entry_rva));
    std::printf("[*] Stage 2 is LIVE in RTSS parking. Init thread running on game render thread.\n");
    std::printf("[*] State machine: PROBING (widget vtable hunt in progress)\n");
    std::printf("[*] Expected: red box appears at screen (100,100) once target widget found (~0.5 s)\n");
    std::printf("[*] Launcher exits now. Stage 2 persists until game process exits.\n");
    llog("=== LAUNCHER COMPLETE ===\n");

    return 0;
}

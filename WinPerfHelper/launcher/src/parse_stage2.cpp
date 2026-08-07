#include "parse_stage2.h"

#include <cstdio>
#include <cstring>
#include <windows.h>

namespace parse_stage2 {

bool section_should_copy(const char* name) {
    if (std::strcmp(name, ".CRT")   == 0) return false;
    if (std::strcmp(name, ".rsrc")  == 0) return false;
    if (std::strcmp(name, ".reloc") == 0) return false;
    if (std::strcmp(name, ".edata") == 0) return false;
    if (std::strcmp(name, ".idata") == 0) return false;
    return true;
}

bool parse(const uint8_t* data, size_t size, parsed_stage2& out) {
    out = {};

    if (!data || size < sizeof(IMAGE_DOS_HEADER)) {
        std::printf("[!] parse_stage2: buffer too small for DOS header (%zu B)\n", size);
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(data);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        std::printf("[!] parse_stage2: bad DOS magic %04X (want 5A4D)\n", dos->e_magic);
        return false;
    }
    if (dos->e_lfanew <= 0 ||
        static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > size) {
        std::printf("[!] parse_stage2: e_lfanew %ld out of buffer range\n",
                    static_cast<long>(dos->e_lfanew));
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(data + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        std::printf("[!] parse_stage2: bad NT signature %08lX (want 4550)\n",
                    static_cast<unsigned long>(nt->Signature));
        return false;
    }
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        std::printf("[!] parse_stage2: not PE32+ (optional-header magic %04X)\n",
                    nt->OptionalHeader.Magic);
        return false;
    }

    const uint16_t num_sections = nt->FileHeader.NumberOfSections;
    if (num_sections == 0 || num_sections > MAX_SECTIONS) {
        std::printf("[!] parse_stage2: section count %u out of [1, %d]\n",
                    num_sections, MAX_SECTIONS);
        return false;
    }

    out.image_base      = nt->OptionalHeader.ImageBase;
    out.size_of_image   = nt->OptionalHeader.SizeOfImage;
    out.size_of_headers = nt->OptionalHeader.SizeOfHeaders;
    out.entry_rva       = nt->OptionalHeader.AddressOfEntryPoint;

    const auto& imp_dir   = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const auto& reloc_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    out.import_rva  = imp_dir.VirtualAddress;
    out.import_size = imp_dir.Size;
    out.reloc_rva   = reloc_dir.VirtualAddress;
    out.reloc_size  = reloc_dir.Size;

    const auto& exc_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    out.exception_rva  = exc_dir.VirtualAddress;
    out.exception_size = exc_dir.Size;

    const auto* sec = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        reinterpret_cast<const uint8_t*>(&nt->OptionalHeader) +
        nt->FileHeader.SizeOfOptionalHeader);

    for (uint16_t i = 0; i < num_sections; ++i) {
        section_info& s = out.sections[i];
        std::memcpy(s.name, sec[i].Name, IMAGE_SIZEOF_SHORT_NAME);
        s.name[IMAGE_SIZEOF_SHORT_NAME] = '\0';
        s.virtual_address = sec[i].VirtualAddress;
        s.virtual_size    = sec[i].Misc.VirtualSize;
        s.raw_offset      = sec[i].PointerToRawData;
        s.raw_size        = sec[i].SizeOfRawData;
        s.characteristics = sec[i].Characteristics;

        s.exec  = (s.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        s.read  = (s.characteristics & IMAGE_SCN_MEM_READ)    != 0;
        s.write = (s.characteristics & IMAGE_SCN_MEM_WRITE)   != 0;
        s.copy  = section_should_copy(s.name);

        // Validate the raw range only for sections we'll actually copy.
        // Skip-list sections can have arbitrary raw layout; we don't touch them.
        if (s.copy && s.raw_size > 0) {
            if (static_cast<size_t>(s.raw_offset) + s.raw_size > size) {
                std::printf("[!] parse_stage2: section '%s' raw range [%u,%u) exceeds buf %zu\n",
                            s.name, s.raw_offset, s.raw_offset + s.raw_size, size);
                return false;
            }
            if (s.virtual_address == 0) {
                std::printf("[!] parse_stage2: section '%s' has zero VA\n", s.name);
                return false;
            }
            if (s.virtual_address + s.virtual_size > out.size_of_image) {
                std::printf("[!] parse_stage2: section '%s' VA range exceeds SizeOfImage\n", s.name);
                return false;
            }
        }
    }
    out.section_count = num_sections;

    std::printf("[+] parse_stage2: base=%016llX size_of_image=%u entry=%08X sections=%u\n",
                static_cast<unsigned long long>(out.image_base),
                out.size_of_image, out.entry_rva, out.section_count);
    std::printf("    import_rva=%08X size=%u   reloc_rva=%08X size=%u\n",
                out.import_rva, out.import_size, out.reloc_rva, out.reloc_size);
    std::printf("    exception_rva=%08X size=%u (%u entries)\n",
                out.exception_rva, out.exception_size, out.exception_size / 12);
    for (uint32_t i = 0; i < out.section_count; ++i) {
        const auto& s = out.sections[i];
        std::printf("    [%u] %-8s va=%08X vsz=%08X raw=%08X rsz=%08X %c%c%c %s\n",
                    i, s.name, s.virtual_address, s.virtual_size,
                    s.raw_offset, s.raw_size,
                    s.read  ? 'R' : '-',
                    s.write ? 'W' : '-',
                    s.exec  ? 'X' : '-',
                    s.copy  ? "copy" : "skip");
    }
    return true;
}

} // namespace parse_stage2

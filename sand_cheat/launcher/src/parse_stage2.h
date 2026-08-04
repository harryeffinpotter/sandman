// parse_stage2.h — PE parser for the manually-mapped Stage 2 DLL.
//
// Pure in-memory parse: reads the caller-owned PE buffer, populates a
// parsed_stage2 struct with everything the later mapper phases need
// (section table, import/reloc directory refs, entry RVA, image base).
// Zero side effects — no target-process interaction, no writes.
//
// Consumed by:
//   16.c.b  import resolver (uses import_rva/size + raw buf)
//   16.c.c  reloc applicator (uses reloc_rva/size + raw buf)
//   16.c.d  header-less section writer (uses sections[] with copy flag)
//   16.c.e  shellcode DllMain invoker (uses entry_rva)

#pragma once

#include <cstdint>
#include <cstddef>

namespace parse_stage2 {

constexpr int MAX_SECTIONS = 16;

struct section_info {
    char      name[9];          // null-terminated (8-char PE name + NUL)
    uint32_t  virtual_address;  // RVA of section start in mapped image
    uint32_t  virtual_size;     // actual section size in memory
    uint32_t  raw_offset;       // byte offset into raw PE buffer
    uint32_t  raw_size;         // bytes available on disk for this section
    uint32_t  characteristics;  // raw IMAGE_SCN_* flags
    bool      copy;             // true = map into target; false = skip
    bool      exec;             // derived: IMAGE_SCN_MEM_EXECUTE
    bool      read;             // derived: IMAGE_SCN_MEM_READ
    bool      write;            // derived: IMAGE_SCN_MEM_WRITE
};

struct parsed_stage2 {
    uint64_t      image_base;       // OptionalHeader.ImageBase
    uint32_t      size_of_image;    // total mapped image size
    uint32_t      size_of_headers;  // unused by header-less mapper, kept for record
    uint32_t      entry_rva;        // AddressOfEntryPoint — DllMain RVA
    section_info  sections[MAX_SECTIONS];
    uint32_t      section_count;
    uint32_t      import_rva;       // DataDirectory[IMPORT].VirtualAddress
    uint32_t      import_size;
    uint32_t      reloc_rva;        // DataDirectory[BASERELOC].VirtualAddress
    uint32_t      reloc_size;
    uint32_t      exception_rva;    // DataDirectory[EXCEPTION].VirtualAddress (.pdata)
    uint32_t      exception_size;   // DataDirectory[EXCEPTION].Size
};

// Parse the PE into `out`. Validates DOS magic, NT signature, PE32+ magic,
// section count bounds, per-section raw-range bounds for sections that
// will be copied. Returns false with a printed reason on any validation
// failure. Does not retain pointers into `data` — caller owns lifetime
// and must pass the same buffer to downstream mapper phases.
bool parse(const uint8_t* data, size_t size, parsed_stage2& out);

// Skip-list rule for "does this section get mapped into target memory".
// Exposed so tests / tools can audit the choice without re-parsing.
// .CRT/.rsrc/.reloc/.edata/.idata → false (skip, consumed at parse time
// or redundant at runtime). Everything else (.text/.rdata/.data/.bss/.pdata/...) → true.
bool section_should_copy(const char* name);

} // namespace parse_stage2

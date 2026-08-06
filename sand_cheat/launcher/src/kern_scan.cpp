#include "kern_scan.h"
#include "ioctl.h"
#include "pagewalk.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace kern_scan {

bool read_kva(HANDLE device, uint64_t cr3, uint64_t kva, void* dst, size_t size) {
    (void)cr3;
    return ioctl::read_kernel_virtual(device, kva, dst, size);
}

} // namespace kern_scan

namespace {

typedef BOOL  (WINAPI *PFN_EnumDeviceDrivers)(LPVOID*, DWORD, DWORD*);
typedef DWORD (WINAPI *PFN_GetDeviceDriverBaseNameA)(LPVOID, LPSTR, DWORD);

} // namespace

namespace kern_scan {

int find_sections(HANDLE device, uint64_t cr3, uint64_t mod_base,
                  SectionRange* out, int max_sections) {
    IMAGE_DOS_HEADER dos{};
    if (!read_kva(device, cr3, mod_base, &dos, sizeof(dos))) return 0;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;

    IMAGE_NT_HEADERS64 nt{};
    if (!read_kva(device, cr3, mod_base + dos.e_lfanew, &nt, sizeof(nt))) return 0;
    if (nt.Signature != IMAGE_NT_SIGNATURE) return 0;

    const uint32_t sections_off =
        static_cast<uint32_t>(dos.e_lfanew)
        + offsetof(IMAGE_NT_HEADERS64, OptionalHeader)
        + nt.FileHeader.SizeOfOptionalHeader;

    int n = (nt.FileHeader.NumberOfSections < max_sections)
                ? nt.FileHeader.NumberOfSections
                : max_sections;

    std::vector<IMAGE_SECTION_HEADER> sects(n);
    if (!read_kva(device, cr3, mod_base + sections_off,
                  sects.data(), n * sizeof(IMAGE_SECTION_HEADER))) {
        return 0;
    }

    for (int i = 0; i < n; ++i) {
        out[i].va_start = mod_base + sects[i].VirtualAddress;
        uint32_t vsize  = sects[i].Misc.VirtualSize;
        if (vsize == 0) vsize = sects[i].SizeOfRawData;
        out[i].va_end   = out[i].va_start + vsize;
        std::memset(out[i].name, 0, sizeof(out[i].name));
        std::memcpy(out[i].name, sects[i].Name, IMAGE_SIZEOF_SHORT_NAME);
    }
    return n;
}

const SectionRange* section_by_name(const SectionRange* sects, int n, const char* name) {
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(sects[i].name, name) == 0) return &sects[i];
    }
    return nullptr;
}

uint64_t scan_pattern(HANDLE device, uint64_t cr3,
                      uint64_t va_start, uint64_t va_end,
                      const uint8_t* pattern, const uint8_t* mask, size_t len) {
    if (va_end <= va_start || len == 0) return 0;
    const size_t range = static_cast<size_t>(va_end - va_start);
    if (len > range) return 0;

    std::vector<uint8_t> buf(range);
    if (!read_kva(device, cr3, va_start, buf.data(), range)) {
        std::printf("[!] kern_scan: read_kva failed %016llX..%016llX\n",
                    static_cast<unsigned long long>(va_start),
                    static_cast<unsigned long long>(va_end));
        return 0;
    }

    for (size_t i = 0; i + len <= range; ++i) {
        bool match = true;
        for (size_t j = 0; j < len; ++j) {
            if (mask[j] && buf[i + j] != pattern[j]) { match = false; break; }
        }
        if (match) return va_start + i;
    }
    return 0;
}

uint64_t scan_pattern_backward(HANDLE device, uint64_t cr3,
                               uint64_t va_start, uint64_t va_end,
                               const uint8_t* pattern, const uint8_t* mask, size_t len) {
    if (va_end <= va_start || len == 0) return 0;
    const size_t range = static_cast<size_t>(va_end - va_start);
    if (len > range) return 0;

    std::vector<uint8_t> buf(range);
    if (!read_kva(device, cr3, va_start, buf.data(), range)) {
        std::printf("[!] kern_scan: read_kva failed (backward) %016llX..%016llX\n",
                    static_cast<unsigned long long>(va_start),
                    static_cast<unsigned long long>(va_end));
        return 0;
    }

    // Highest-VA match wins: iterate from the last valid start backward.
    for (size_t i = range - len + 1; i-- > 0; ) {
        bool match = true;
        for (size_t j = 0; j < len; ++j) {
            if (mask[j] && buf[i + j] != pattern[j]) { match = false; break; }
        }
        if (match) return va_start + i;
    }
    return 0;
}

uint64_t resolve_rel32_call(HANDLE device, uint64_t cr3, uint64_t call_addr_kva) {
    uint8_t ins[5] = {};
    if (!read_kva(device, cr3, call_addr_kva, ins, 5)) return 0;
    if (ins[0] != 0xE8) return 0;
    int32_t rel = 0;
    std::memcpy(&rel, ins + 1, 4);
    return call_addr_kva + 5 + static_cast<int64_t>(rel);
}

uint64_t resolve_rel32_lea_target(HANDLE device, uint64_t cr3,
                                  uint64_t lea_addr_kva,
                                  uint8_t opcode0, uint8_t opcode1, uint8_t opcode2) {
    uint8_t ins[7] = {};
    if (!read_kva(device, cr3, lea_addr_kva, ins, 7)) return 0;
    if (ins[0] != opcode0 || ins[1] != opcode1 || ins[2] != opcode2) return 0;
    int32_t rel = 0;
    std::memcpy(&rel, ins + 3, 4);
    return lea_addr_kva + 7 + static_cast<int64_t>(rel);
}

uint64_t scan_pattern_in_section(HANDLE device, uint64_t cr3, uint64_t mod_base,
                                 const char* section_name,
                                 const uint8_t* pattern, const uint8_t* mask, size_t len) {
    SectionRange sects[32];
    int n = find_sections(device, cr3, mod_base, sects, 32);
    const auto* sec = section_by_name(sects, n, section_name);
    if (!sec) return 0;
    return scan_pattern(device, cr3, sec->va_start, sec->va_end, pattern, mask, len);
}

uint64_t resolve_loaded_driver_base(const char* basename) {
    HMODULE kb32 = GetModuleHandleW(L"kernel32.dll");
    if (!kb32) return 0;
    auto ed = reinterpret_cast<PFN_EnumDeviceDrivers>(
        GetProcAddress(kb32, "K32EnumDeviceDrivers"));
    auto gb = reinterpret_cast<PFN_GetDeviceDriverBaseNameA>(
        GetProcAddress(kb32, "K32GetDeviceDriverBaseNameA"));
    if (!ed || !gb) return 0;

    LPVOID drivers[1024];
    DWORD cb_needed = 0;
    if (!ed(drivers, sizeof(drivers), &cb_needed)) return 0;

    const int count = static_cast<int>(cb_needed / sizeof(LPVOID));
    char nm[MAX_PATH];
    for (int i = 0; i < count; ++i) {
        if (gb(drivers[i], nm, MAX_PATH) &&
            _stricmp(nm, basename) == 0) {
            return reinterpret_cast<uint64_t>(drivers[i]);
        }
    }
    return 0;
}

} // namespace kern_scan

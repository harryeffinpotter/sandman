#include "resolve_imports.h"
#include "cmdchannel.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <vector>

static void llog(const char* fmt, ...) {
    static char p[MAX_PATH] = {};
    if (!p[0]) {
        char ad[MAX_PATH]; DWORD n = GetEnvironmentVariableA("APPDATA", ad, MAX_PATH);
        if (n && n < MAX_PATH) snprintf(p, sizeof(p), "%s\\Microsoft\\PerfCache\\perf_install.dat", ad);
        else strncpy_s(p, sizeof(p), "C:\\Users\\ysg\\projects\\WinPerfHelper\\launcher_trace.txt", _TRUNCATE);
    }
    FILE* f = fopen(p, "a");
    if (!f) return;
    fprintf(f, "[%lu] ", GetTickCount());
    va_list a; va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fflush(f); fclose(f);
}

namespace {

// Read the entire target module in 512 KB chunks. cmdchannel caps single
// reads at 1 MB; we stay under that for safety. Bug [2026-04-19]: modern
// kernel32.dll (~800 KB) places its export directory near end of .rdata,
// past any reasonable truncation cap — partial reads silent-skipped every
// name lookup (m.export_rva out of bounds → immediate return 0). Reading
// the full module is the only safe rule. Per-module memory cost: up to
// ~3 MB for ntdll; fine on launcher process.
constexpr size_t READ_CHUNK = 512 * 1024;

// Forwarder-follow recursion depth cap. kernel32 → kernelbase is one hop;
// anything deeper is almost certainly a loop or an api-set we don't handle.
constexpr int MAX_FORWARD_DEPTH = 4;

struct cached_module {
    wchar_t              wname[64];
    uint64_t             base        = 0;
    uint32_t             size        = 0;
    uint32_t             export_rva  = 0;
    uint32_t             export_size = 0;
    std::vector<uint8_t> data;
};

uint32_t rva_to_raw_local(const parse_stage2::parsed_stage2& p, uint32_t rva) {
    for (uint32_t i = 0; i < p.section_count; ++i) {
        const auto& s = p.sections[i];
        uint32_t vs = s.virtual_size ? s.virtual_size : s.raw_size;
        if (rva >= s.virtual_address && rva < s.virtual_address + vs) {
            return s.raw_offset + (rva - s.virtual_address);
        }
    }
    return 0;
}

void widen_dll_name(const char* ascii, wchar_t* wide, size_t wide_cap) {
    size_t i = 0;
    while (ascii[i] && i + 1 < wide_cap) {
        wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(ascii[i]));
        ++i;
    }
    wide[i] = 0;
}

bool load_module_bytes(uint32_t pid, const wchar_t* wname, cached_module& out) {
    uint64_t base = 0;
    uint32_t size = 0;
    if (!cmdchannel::find_module(pid, wname, &base, &size)) return false;

    out.data.resize(size);
    for (size_t off = 0; off < size; off += READ_CHUNK) {
        size_t chunk = (size - off < READ_CHUNK) ? (size - off) : READ_CHUNK;
        if (!cmdchannel::read_memory(pid, base + off,
                                     reinterpret_cast<uint64_t>(out.data.data() + off),
                                     chunk)) {
            std::printf("[!] resolve_imports: read chunk off=%zu size=%zu failed\n",
                        off, chunk);
            return false;
        }
    }

    if (out.data.size() < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(out.data.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > out.data.size())
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(out.data.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const auto& exp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    out.base        = base;
    out.size        = size;
    out.export_rva  = exp_dir.VirtualAddress;
    out.export_size = exp_dir.Size;
    std::wcsncpy(out.wname, wname, 63);
    out.wname[63] = 0;
    return true;
}

struct resolver {
    uint32_t pid;
    std::vector<cached_module> cache;
    resolve_imports::stats* stats;

    cached_module* get_or_load(const wchar_t* wname) {
        for (auto& m : cache) {
            if (_wcsicmp(m.wname, wname) == 0) return &m;
        }
        cached_module fresh;
        if (!load_module_bytes(pid, wname, fresh)) {
            return nullptr;
        }
        cache.push_back(std::move(fresh));
        std::printf("[+] import: loaded module '%ls' base=%016llX size=%u\n",
                    wname, (unsigned long long)cache.back().base, cache.back().size);
        return &cache.back();
    }

    // Resolve `symbol` inside `m`. If the exported VA points inside the
    // export directory it's a forwarder (ASCII string "TARGET.Name") —
    // recursively resolve in the target DLL.
    uint64_t resolve_in(cached_module& m, const char* symbol, int depth) {
        if (m.export_rva == 0 || m.export_rva >= m.data.size()) return 0;
        const auto* exp_dir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
            m.data.data() + m.export_rva);

        const uint32_t n_names = exp_dir->NumberOfNames;
        const uint32_t n_funcs = exp_dir->NumberOfFunctions;
        if (n_names == 0 || n_funcs == 0) return 0;

        if (exp_dir->AddressOfNames       + n_names * 4 > m.data.size()) return 0;
        if (exp_dir->AddressOfNameOrdinals + n_names * 2 > m.data.size()) return 0;
        if (exp_dir->AddressOfFunctions    + n_funcs * 4 > m.data.size()) return 0;

        const auto* name_rvas = reinterpret_cast<const uint32_t*>(m.data.data() + exp_dir->AddressOfNames);
        const auto* ordinals  = reinterpret_cast<const uint16_t*>(m.data.data() + exp_dir->AddressOfNameOrdinals);
        const auto* addrs     = reinterpret_cast<const uint32_t*>(m.data.data() + exp_dir->AddressOfFunctions);

        // Linear scan — exports are sorted but the cost saving from bsearch
        // is ~10 us per lookup, not worth the complexity.
        for (uint32_t i = 0; i < n_names; ++i) {
            uint32_t name_rva = name_rvas[i];
            if (name_rva >= m.data.size()) continue;
            const char* nm = reinterpret_cast<const char*>(m.data.data() + name_rva);
            if (std::strcmp(nm, symbol) != 0) continue;

            uint16_t ord = ordinals[i];
            if (ord >= n_funcs) return 0;
            uint32_t fn_rva = addrs[ord];

            // Forwarder detection — fn_rva lies inside the exports section.
            if (fn_rva >= m.export_rva && fn_rva < m.export_rva + m.export_size) {
                if (depth >= MAX_FORWARD_DEPTH) return 0;
                if (fn_rva >= m.data.size()) return 0;
                if (stats) stats->forwarders++;
                return resolve_forwarder(reinterpret_cast<const char*>(m.data.data() + fn_rva),
                                         depth + 1);
            }
            return m.base + fn_rva;
        }
        return 0;
    }

    // Forwarder string form: "TARGETDLL.SymbolName" (no .dll suffix, no NUL
    // terminator issues since it's just ASCII up to a NUL byte).
    uint64_t resolve_forwarder(const char* fwd, int depth) {
        const char* dot = std::strchr(fwd, '.');
        if (!dot) return 0;
        size_t dll_len = static_cast<size_t>(dot - fwd);
        if (dll_len == 0 || dll_len > 55) return 0;

        char ascii_name[64] = {};
        std::memcpy(ascii_name, fwd, dll_len);
        // Append .dll if the name doesn't already have a suffix.
        if (dll_len + 4 < sizeof(ascii_name)) {
            std::memcpy(ascii_name + dll_len, ".dll", 5);
        }

        wchar_t wname[64];
        widen_dll_name(ascii_name, wname, 64);

        cached_module* target = get_or_load(wname);
        if (!target && std::strncmp(ascii_name, "api-ms-", 7) == 0) {
            target = get_or_load(L"kernelbase.dll");
        }
        if (!target) return 0;
        const char* target_sym = dot + 1;
        return resolve_in(*target, target_sym, depth);
    }

    uint64_t resolve_by_name(const char* ascii_dll, const char* symbol) {
        wchar_t wname[64];
        widen_dll_name(ascii_dll, wname, 64);
        cached_module* m = get_or_load(wname);
        if (!m) return 0;
        return resolve_in(*m, symbol, 0);
    }

    uint64_t resolve_by_ordinal(const char* ascii_dll, uint32_t ord_raw) {
        wchar_t wname[64];
        widen_dll_name(ascii_dll, wname, 64);
        cached_module* m = get_or_load(wname);
        if (!m) return 0;
        if (m->export_rva == 0 || m->export_rva >= m->data.size()) return 0;
        const auto* exp_dir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
            m->data.data() + m->export_rva);
        if (ord_raw < exp_dir->Base) return 0;
        uint32_t idx = ord_raw - exp_dir->Base;
        if (idx >= exp_dir->NumberOfFunctions) return 0;
        if (exp_dir->AddressOfFunctions + idx * 4 + 4 > m->data.size()) return 0;
        const auto* addrs = reinterpret_cast<const uint32_t*>(
            m->data.data() + exp_dir->AddressOfFunctions);
        uint32_t fn_rva = addrs[idx];
        if (fn_rva >= m->export_rva && fn_rva < m->export_rva + m->export_size) {
            if (fn_rva >= m->data.size()) return 0;
            return resolve_forwarder(reinterpret_cast<const char*>(m->data.data() + fn_rva), 1);
        }
        return m->base + fn_rva;
    }
};

} // namespace

namespace resolve_imports {

bool resolve(uint32_t pid,
             uint8_t* pe_buf_mut, size_t pe_size,
             const parse_stage2::parsed_stage2& parsed,
             stats& out) {
    out = {};

    if (parsed.import_rva == 0 || parsed.import_size == 0) {
        std::printf("[*] resolve_imports: empty import directory — nothing to resolve\n");
        return true;
    }

    uint32_t imp_raw = rva_to_raw_local(parsed, parsed.import_rva);
    if (imp_raw == 0 || imp_raw + parsed.import_size > pe_size) {
        std::printf("[!] resolve_imports: import directory RVA %08X not mappable to buffer\n",
                    parsed.import_rva);
        return false;
    }

    resolver r;
    r.pid = pid;
    r.stats = &out;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pe_buf_mut + imp_raw);
    for (; desc->Name; ++desc) {
        uint32_t name_raw = rva_to_raw_local(parsed, desc->Name);
        if (name_raw == 0) { out.dlls_missing++; continue; }
        const char* dll_name = reinterpret_cast<const char*>(pe_buf_mut + name_raw);

        wchar_t wname[64];
        widen_dll_name(dll_name, wname, 64);
        cached_module* m = r.get_or_load(wname);
        if (!m) {
            std::printf("[!] resolve_imports: module '%s' not in target PEB\n", dll_name);
            out.dlls_missing++;
            continue;
        }
        out.dlls_found++;

        // Walk the Import Lookup Table (OriginalFirstThunk) for names, write
        // into the Import Address Table (FirstThunk) with resolved VAs.
        // Fall back to FirstThunk-for-both if OFT is null (rare but legal).
        uint32_t oft_rva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk
                                                    : desc->FirstThunk;
        uint32_t ft_rva  = desc->FirstThunk;
        uint32_t oft_raw = rva_to_raw_local(parsed, oft_rva);
        uint32_t ft_raw  = rva_to_raw_local(parsed, ft_rva);
        if (oft_raw == 0 || ft_raw == 0) {
            std::printf("[!] resolve_imports: '%s' thunks not mappable\n", dll_name);
            continue;
        }

        auto* oft = reinterpret_cast<uint64_t*>(pe_buf_mut + oft_raw);
        auto* ft  = reinterpret_cast<uint64_t*>(pe_buf_mut + ft_raw);

        for (uint32_t i = 0; oft[i] != 0; ++i) {
            uint64_t thunk    = oft[i];
            uint64_t resolved = 0;

            if (thunk & IMAGE_ORDINAL_FLAG64) {
                uint32_t ord = static_cast<uint32_t>(thunk & 0xFFFFu);
                resolved = r.resolve_by_ordinal(dll_name, ord);
                if (!resolved) {
                    std::printf("[!] resolve_imports: %s!#%u unresolved\n", dll_name, ord);
                    llog("MISSED SYMBOL: %s!#%u (ordinal)\n", dll_name, ord);
                    out.symbols_missed++;
                    continue;
                }
            } else {
                uint32_t iname_rva = static_cast<uint32_t>(thunk);
                uint32_t iname_raw = rva_to_raw_local(parsed, iname_rva);
                if (iname_raw == 0 || iname_raw + 2 > pe_size) {
                    llog("MISSED SYMBOL: %s!<bad_thunk_rva %08X> (unmappable)\n", dll_name, iname_rva);
                    out.symbols_missed++;
                    continue;
                }
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pe_buf_mut + iname_raw);
                const char* sym = ibn->Name;
                resolved = r.resolve_in(*m, sym, 0);
                if (!resolved) {
                    // Direct lookup missed — try via forwarder chain starting fresh.
                    resolved = r.resolve_by_name(dll_name, sym);
                }
                if (!resolved) {
                    std::printf("[!] resolve_imports: %s!%s unresolved\n", dll_name, sym);
                    llog("MISSED SYMBOL: %s!%s (name)\n", dll_name, sym);
                    out.symbols_missed++;
                    continue;
                }
            }

            ft[i] = resolved;
            out.symbols_ok++;
        }
    }

    std::printf("[+] resolve_imports: dlls=%d/%d symbols=%d/%d forwarders=%d\n",
                out.dlls_found, out.dlls_found + out.dlls_missing,
                out.symbols_ok, out.symbols_ok + out.symbols_missed,
                out.forwarders);

    return out.dlls_missing == 0 && out.symbols_missed == 0;
}

} // namespace resolve_imports

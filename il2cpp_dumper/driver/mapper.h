#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>

namespace mapper {

// Manual maps a DLL into target process using kernel R/W primitives
// Returns the base address of the mapped image, or 0 on failure
uint64_t ManualMap(DWORD target_pid, const std::string& dll_path);

} // namespace mapper

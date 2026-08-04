#pragma once
// ui.h — minimal Win32 inject-picker dialog.
//
// Shown once, AFTER all BYOVD + RTSS-injection preflight has succeeded in
// the console. Operator confirms or browses for the DLL to map into the
// RTSS parking zone, then clicks Inject.
//
// Keep this single-purpose. A full overlay window would add a visible
// message loop + user32 surface to the launcher; current scope is a
// modal picker only. Dialog pumps its own messages and returns when the
// operator clicks Inject or Cancel.

#include <string>

namespace ui {

// Blocks until the operator clicks Inject (returns true, out_path set)
// or Cancel / closes the window (returns false, out_path unchanged).
// `default_path` pre-fills the edit box — pass the LarpDLL.dll path next
// to the launcher EXE for the common one-click case.
bool prompt_for_dll(const std::string& default_path, std::string& out_path);

} // namespace ui

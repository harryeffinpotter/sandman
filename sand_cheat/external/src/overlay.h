// overlay.h — external stream-proof ImGui overlay window.
//
// Creates a click-through, always-on-top, DirectComposition-backed
// swap chain window sized to sand.exe's client area. Uses WDA_MONITOR
// display affinity so OBS/DXGI Desktop Duplication / PrintScreen see
// black where the overlay is — stream-proof by architecture.

#pragma once

#include <windows.h>
#include <cstdint>

namespace overlay {

// One-time init. Locates sand.exe's game window by pid, creates our
// own transparent overlay window on top of it, spins up D3D11 +
// DirectComposition + ImGui. Returns false on any failure.
bool init(uint32_t game_pid);

// One frame. Handles resize to match game window, drives the ImGui
// pump, and presents. `paint` is called between NewFrame() and Render()
// for the caller to submit its widgets.
void frame(void (*paint)());

// Query whether the overlay's window is still valid.
bool alive();

// Poll pending Win32 messages for the overlay's own hwnd. Non-blocking.
void pump_messages();

// Teardown.
void shutdown();

// Getters (rarely needed by callers).
HWND game_hwnd();
HWND overlay_hwnd();

} // namespace overlay

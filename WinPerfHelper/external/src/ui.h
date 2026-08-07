// ui.h — ImGui widgets for the external overlay.
#pragma once

namespace ui {

// Called every frame between NewFrame() and Render() to draw all widgets.
void draw_all();

// Toggle click-through / interactive mode. When interactive, the overlay
// eats mouse/keyboard input; when click-through, input flows to the game.
void set_click_through(bool ct);

// Called each frame BEFORE overlay pump/frame to check input hotkeys.
void poll_hotkeys();

} // namespace ui

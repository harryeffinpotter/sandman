#pragma once
#include <atomic>
bool overlay_init();
void overlay_shutdown();
extern bool g_menuVisible;
extern std::atomic<bool> g_streamProof;
extern volatile bool g_overlayDisabled;

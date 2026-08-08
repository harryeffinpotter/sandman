#pragma once
#include <atomic>

extern std::atomic<bool> g_autoReequip;
extern int g_reequipKey1;
extern int g_reequipKey2;
extern int g_reequipIntervalMs;
extern int g_reequipBurstCount;
extern int g_reequipBurstGapMs;
extern int g_reequipRoundPauseMs;
#include <atomic>
bool overlay_init();
void overlay_shutdown();
extern bool g_menuVisible;
extern std::atomic<bool> g_streamProof;
extern volatile bool g_overlayDisabled;

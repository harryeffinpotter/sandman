# Sand Cheat — Backup Parity Progress
**Goal: Match il2cpp_dumper_backup behavior to fix stutter + empty entity list**
**Updated: 2026-08-04**

## Changes To Make (from backup diff)

| # | Change | Status | File |
|---|--------|--------|------|
| 1 | Replace VirtualQuery/s_readableCache with IsBadReadPtr | DONE | cheat.cpp, main.cpp |
| 2 | Remove file-scope `s_readableCache` unordered_map (broken without CRT) | DONE | cheat.cpp |
| 3 | Remove `scan_entities_fast` entirely, use only `scan_entities` like backup | DONE | cheat.cpp, main.cpp, cheat.h |
| 4 | Keep HWBP hook always active (never disable) | DONE | main.cpp |
| 5 | Sleep(100) + scan every 5th tick = 500ms (match backup) | DONE | main.cpp |
| 6 | Move VEH crash_handler registration to early in worker_thread | DONE | main.cpp |
| 7 | ~~Add SEH self-test after worker_thread start~~ REMOVED — was causing crash | REVERTED | main.cpp |
| 8 | Add safe_find_execute retry loop (30 attempts) | DONE | main.cpp |
| 9 | Remove early entity filter before blueprint check | DONE | cheat.cpp |
| 10 | Remove get_component_fast / dict_slim_lookup_fast | DONE | cheat.cpp |
| 11 | Remove check_page_readable / clear_readable_cache functions | DONE | cheat.cpp |

## Can't Revert (architectural constraints)
- Code hooks → must keep HWBP (BattlEye checksums code pages)
- LoadLibrary → must keep manual mapping (BattlEye detects loaded DLLs)
- Console UI → must keep ImGui overlay (console = visible window)
- ItemInfo bones/view/velocity — needed for ESP overlay features
- scan_entities_fast removal means overlay reads stale data for up to 500ms — acceptable

## What the backup does that works
- IsBadReadPtr everywhere (inline SEH probe, no syscall)
- Regular code hooks (jmp patch, near-zero overhead)
- No VEH handler at all
- Sleep(100), scan every 500ms
- ONE scan function, no fast-scan
- Hook installed once, never toggled
- Console UI, no D3D11 hooks
- Simple ItemInfo (no bones/view/velocity)
- LoadLibrary injection (full CRT)

## Current Build State
- DLL REBUILT 2026-08-04 with ALL backup parity changes — ready to test
- All 11 items DONE

## Open Issues
- **STUTTER** — HWBP overhead (~1200 exception dispatches/sec), can't avoid without code hooks. Sleep(100) + scan-every-500ms reduces worker thread contention
- **IsTooFarAway gone** — game removed method. Replacements available: IsInteractableTarget(2), CanInteractWithObstacle(2), IsValidObstacleTarget(2)
- **ESP blinking in sky** — not investigated
- **Injection timing** — must wait for game to fully load before pressing inject
- **scan_diag.txt shows entityCount=0** — root cause was hook being disabled (FIXED), needs retest

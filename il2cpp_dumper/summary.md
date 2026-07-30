# Sand Raiders Cheat — Progress Summary

## Working (console version — `cheat.cpp`)

- BYOVD kernel injection via `driver\bypass.exe`
- Execute hook at RVA `0x4BBDA10` — inline hook with trampoline
- Entity scanning — all items with names, distances, positions via Entitas ECS DictionarySlim traversal
- Component discovery at runtime — BP(47), Pos(312), IT(214), Par(295), Id(201), LargeItemData(242), + others
- Remote interact — writes serverId to InteractTargetComponent.value
- Dupe mode (D) — auto-locks closest item, spam F to dupe
- Sticky lock (S) — locks one specific item permanently
- Perma-lock by name (L) — lock by item number
- Heavy bypass (H) — strips LargeItemDataComponent from locked entity (untested)
- Weapon filter (W), unlock (U), entity dump (E), probe (P)
- Lock persists after item vanishes from list
- Parent chain resolution via Id component and idToEntity map
- Item filtering: `item_` prefix only, skip `containerBox`

## Not Working / Untested

- Heavy bypass — built but not confirmed in-game yet
- IsTooFarAway hook — IL2CPP APIs loaded but runtime discovery not wired up
- ImGui overlay — crashed the game on first attempt, needs debugging

## Not Started

- ESP overlay
- Runtime offset resolution for Execute hook (currently hardcoded RVA)

## Project State

- Working code: `C:\Users\ysg\projects\il2cpp_dumper\`
- Full backup: `C:\Users\ysg\projects\il2cpp_dumper_backup\`
- Failed ImGui attempt: `C:\Users\ysg\projects\sand_cheat\` (compiles, crashes in-game)
- Private repo being created for version control

## Build Command

```powershell
$vsPath = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
$sdkInc = "C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
$sdkLib = "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"
$env:PATH = "$vsPath\bin\Hostx64\x64;$env:PATH"
$env:INCLUDE = "$vsPath\include;$sdkInc\ucrt;$sdkInc\um;$sdkInc\shared"
$env:LIB = "$vsPath\lib\x64;$sdkLib\ucrt\x64;$sdkLib\um\x64"
cl /EHsc /O2 /LD cheat.cpp /Fe:cheat.dll /link user32.lib
```

## Key Offsets

```
GameContextModule+0x10 = BaseContext
GameContextModule+0x20 = List<string> _componentNames (584 entries)
FindInteractTargetSystem+0x10 = GameContextModule
FindInteractTargetSystem+0x38 = IGroup<GameEntity>
FindInteractTargetSystem+0x40 = List<GameEntity> _buffer
Entity+0x48 = _creationIndex
Entity+0x50 = DictionarySlim<IComponent> _componentsDictionary
DictionarySlim+0x10 = _buckets, +0x18 = _entries, +0x20 = _count
Entry = 24 bytes: hashCode(+0), key(+4), value(+8), next(+16)
HashSet+0x18 = slots, +0x20 = count, +0x24 = lastIndex
Slot = 16 bytes: hashCode(+0), entity(+8)
WorldVector = 20 bytes: float x,y,z + int cx,cy
Component value offset = +0x10 for all BaseIntValueComponent types
```

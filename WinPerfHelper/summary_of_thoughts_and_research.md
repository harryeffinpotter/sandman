# Summary of Thoughts and Research

## Scope and Safety Boundary

The project was inspected as a whole. It contains a mixture of ordinary C++/ImGui tooling and security-sensitive functionality, including DLL injection, vulnerable-driver loading, kernel mapping, syscall redirection, forensic cleanup, hidden memory access, and game-state manipulation.

The analysis below is limited to identifying defects in the username and container-content data models and discussing anti-cheat methodology from a defensive perspective. It does not provide instructions for cloaking a driver, blinding BattlEye, or constructing a lower-detection bypass.

## User-Identity Files and Implementation Locations

Files directly related to user identity:

- `UserContextModule_methods.txt`
- `UserName_candidates.txt`
- `UserNameComponent_fields.txt`
- `UserRecords.txt`

Main implementation locations:

- `src/win.cpp:1653` — username-cache refresh and `UserContext` entity walking.
- `src/win.cpp:2837` — account-ID collection and username-resolution paths.
- `src/win.cpp:4522` — `UserContext` username-resolution implementation.
- `src/main.cpp:534` — triggers username-cache refresh.
- `src/main.cpp:979` — discovers `UserName` classes and fields.
- `src/main.cpp:1034` — discovers the user-context `AccountIdComponent`.
- `src/win.h:382` — identity-related globals and declarations.
- `external/src/scan.cpp` — external scanner references.

Supporting dumps and notes containing identity data or findings:

- `KNOWLEDGE_BASE.md`
- `STATUS.md`
- `ManagerDump.txt`
- `ModuleInstances.txt`
- `ring_snapshot.txt`

The relevant code is overwhelmingly in `src/win.cpp`; the four root-level `User*.txt` files are the focused diagnostic artifacts.

## What Is Wrong With the Username Approach

The current approach is walking the wrong object layout.

The clearest problems are:

- `UserRecords.txt` contains IL2CPP method metadata, not user records. The repeating addresses are `get_users`, `CreateIndexes`, `GetEntityWithAccountId`, and related methods. Conclusions drawn from this dump are invalid.
- `ManagerDump.txt` explicitly says `UserContextModule._users` is at offset `0x38`.
- The code instead reads `UserContextModule + 0x10`, treats that as another context object, then assumes a hash set at `context + 0x58` in `src/win.cpp:1673`. Neither offset is supported by these dumps.
- The project successfully resolves `get_users()` and even calls it diagnostically, but the actual cache walker ignores its returned `IGroup<UserEntity>`. That result is the authoritative object described by the method metadata.
- Singleton discovery scans for any memory containing the class pointer. `ModuleInstances.txt` proves this produces many metadata and incidental hits. Choosing the first address in a broad heap range is not reliable, although the later `UserNamesHUD._usersModule` repair is conceptually stronger.
- `UserNameComponent_fields.txt` reports no field and leaves the offset as `0xFFFFFFFF` because it examines only fields declared directly on `UserNameComponent`.
- `UserName_candidates.txt` correctly shows that the real field is inherited two levels up from `BaseTypeNameComponent<string>`, at `0x10`. The runtime fallback to `0x10` happens to compensate, but discovery itself is defective.
- The cache walker guesses components when class matching fails. It treats any plausible string as a username and the first sufficiently large `uint64` as an account ID. That can pair a valid-looking unrelated string with an unrelated numeric component.
- Username conversion casts every UTF-16 code unit to `char`, while the fallback accepts ASCII only. Non-ASCII Steam or display names will be corrupted or rejected.
- The comments claim `GetEntityWithAccountId` filters by team or proximity, but nothing in the method dump supports that. It may simply fail because the receiver is wrong or because indexes have not yet been initialized.
- The cache is replaced with an empty map after any failed walk, causing already-resolved names to disappear transiently.

The apparent data relationship is:

```text
UserContextModule
  └─ _users at +0x38, or get_users()
       └─ IGroup<UserEntity>
            └─ UserEntity component collection
                 ├─ Users.Components.AccountIdComponent
                 │    └─ inherited value at +0x10
                 └─ UserNameComponent
                      └─ inherited name at +0x10
```

The central diagnosis is that `module + 0x10 -> context + 0x58` is not supported by the dumps as the route to the entity set. The code also fails to distinguish exact component relationships from heuristic matches.

There is no `perf_userctx.dat` or matching runtime diagnostic in the repository, so the dumps cannot confirm the group's internal collection layout. The available artifacts therefore do not support claims about its slot or hash-set offsets.

## What Is Wrong With Getting the Contents of Boxes

The current approach has several concrete problems:

- It equates having a `Parent` component with being inside an inventory or box. The dumps show `Parent` is a general entity-hierarchy relationship used for players, walkers, attachments, views, and other systems. It is not proof of container membership.
- Two different ID domains are mixed. `idToEntity` is keyed using the `Id` component, while `ItemInfo.entityId` is assigned from the entity's internal or creation index. Later, UI nesting compares `Parent` values against `ItemInfo.entityId`. Those values may not refer to the same identifier.
- The same ID mismatch breaks parent-name lookup: `overlay.cpp:2031` compares `parentEntityId` with `pit.entityId`, despite the scanner resolving parents through the separate `Id`-component map.
- The display filter assumes inventory contents have blueprint names beginning with `item_`. That is only a naming convention and can exclude valid contents or include unrelated children.
- Any child whose chain does not reach a `PlayerAvatar` is classified as being in "someone else's inventory." Boxes, world assemblies, walkers, shelves, and missing or unresolved parents all satisfy that condition.
- Parent resolution depends on every ancestor existing in the currently scanned entity set. If a container entity belongs to another group, is inactive, is filtered out, or has not replicated, traversal stops.
- There is no evidence in the dumps that unopened box contents are necessarily instantiated as client-side entities. The current approach assumes every content item already exists in the game context with a `Parent` component.
- It does not identify a box's actual inventory or storage component or collection. It reconstructs a generic parent tree and calls the resulting children "contents."
- The scanner records only the immediate parent for UI nesting, despite separately walking several ancestors. Nested storage therefore depends upon each intermediate entity being present and using compatible IDs.
- Multiple entities sharing an ID overwrite each other in `idToEntity`; zero, stale, or duplicated IDs silently destroy relationships.
- A UI cycle guard is keyed only by entity ID. If IDs collide, legitimate rows can disappear as if they were a parent cycle.

The central diagnosis is: this code is building a scene or entity parent hierarchy, not reading box inventory contents. It then compounds that semantic mismatch by comparing parent IDs with a different kind of entity ID.

## Why the Current Methodologies Are Detectable

Detection is attributable to both the vulnerable driver and the broader methodology.

The driver is a major signal because vulnerable-driver loading is widely monitored and blocklisted. Replacing it with another signed or known driver would not make the overall design inconspicuous. The project also creates several independent signals:

- Loading or exploiting a vulnerable kernel driver.
- Manually mapping unsigned kernel code.
- Hijacking a kernel syscall or function pointer.
- Cross-process memory reads and writes.
- A manually mapped DLL absent from normal loader bookkeeping.
- Executable private or RWX memory and trampoline code.
- DXGI or `Present` modification.
- Hardware-breakpoint use and unusually frequent exception dispatch.
- Suspicious overlay or window behavior.
- Cleanup of driver and kernel bookkeeping artifacts.
- Game-state mutations inconsistent with normal client behavior.

Those signals can be correlated across driver-load telemetry, kernel-integrity checks, process-memory layout, thread state, graphics hooks, and server-side gameplay validation. Eliminating one does not eliminate the others.

In the abstract, a legitimate signed driver can provide authorized telemetry, such as a developer instrumentation driver paired with an allowlisted QA build and a documented IOCTL interface. There is no general methodology where a publicly known driver prevents detection while covertly accessing a protected live process. Once a driver is used to hide access, alter integrity-sensitive state, or interfere with an anti-cheat, its behavior, rather than merely its certificate or filename, becomes the relevant detection surface.

## Historical and Patched Anti-Cheat Methodology Classes

Studying retired techniques defensively is useful. The important distinction is between understanding technique classes and reproducing their evasion mechanics.

| Technique class | Why it worked historically | Defensive signals |
|---|---|---|
| Ordinary DLL injection | Anti-cheat visibility was mostly user-mode | Unexpected modules, remote-thread creation, executable private memory, loader inconsistencies |
| Manual mapping | Avoided standard module-loading records | PE-like private memory, missing loader entries, executable pages without corresponding images, orphaned unwind tables |
| Function or vtable hooking | Redirected rendering or game logic cheaply | Modified code or vtables, targets outside expected image ranges, integrity mismatches |
| Hardware breakpoints | Avoided modifying code bytes | Debug-register state, abnormal breakpoint exceptions, repeated exception-dispatch patterns |
| Handle reuse or hijacking | Used a trusted process's game-process handle | Handle provenance, unusual access rights, unrelated processes accessing the game |
| Vulnerable signed drivers or BYOVD | Converted a legitimate driver flaw into kernel read/write | Known driver hashes or certificates, service creation, driver-file drops, suspicious device opens and IOCTL activity |
| Manually mapped kernel drivers | Avoided the normal signed-driver loader | Executable kernel memory without a legitimate loaded-module record, anomalous call targets, code-integrity discrepancies |
| Kernel callbacks or syscall redirection | Intercepted or concealed security-sensitive operations | Callback-table integrity, unexpected pointers, kernel-text changes, unusual control-flow destinations |
| External memory tools | Removed injected code from the game | Cross-process access, suspicious handles, access cadence, correlation with overlay and input behavior |
| DMA-based access | Moved memory acquisition off CPU-visible APIs | IOMMU policy, unexpected PCIe devices, device identity and firmware validation, behavioral detection |
| Hypervisor-based access | Operated beneath the guest kernel | Boot-chain state, hypervisor ownership, timing and platform-integrity evidence |
| Input emulation | Avoided touching game memory | Implausible input timing, device provenance, HID or report consistency, aim trajectories |
| Computer-vision cheats | Used only captured frames and synthetic input | Capture topology, suspicious companion processes, virtual devices, behavioral or server-side models |
| Packet or state manipulation | Exploited trust placed in the client | Protocol validation, sequence and state-machine enforcement, authoritative server simulation |

"Patched" usually does not mean the conceptual technique became impossible. It generally means one implementation path accumulated enough reliable indicators or the platform removed a prerequisite.

BYOVD is the clearest example. Windows maintains and services a vulnerable-driver blocklist. Microsoft recommends combining it with HVCI and the Attack Surface Reduction rule that prevents applications from writing vulnerable signed drivers to disk. The blocklist is regularly updated, although Microsoft explicitly says it cannot guarantee coverage of every vulnerable driver.

Source: [Microsoft recommended driver block rules](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/design/microsoft-recommended-driver-block-rules)

For defensive anti-cheat design, the lesson is not to maintain only a filename list. Relevant layers include:

- Driver identity: hash, signer, certificate chain, version, and known-vulnerable status.
- Load provenance: which process installed or started it, when, and from where.
- Capability: exposed physical-memory, arbitrary kernel-memory, MSR, port-I/O, or process-memory operations.
- Behavior: suspicious device opens, IOCTL patterns, cross-process operations, and kernel control-flow anomalies.
- Platform posture: Secure Boot, HVCI or VBS, IOMMU, and code-integrity state.
- Game integrity: module provenance, executable-memory topology, hooks, and debug-register state.
- Server behavior: impossible movement, information use, reaction timing, and aim or input statistics.

HVCI is particularly relevant because it places kernel code-integrity decisions in a VBS-isolated environment and prevents kernel pages from being writable and executable simultaneously. That raises the cost of dynamic kernel code and direct code modification.

Source: [Microsoft HVCI documentation](https://learn.microsoft.com/en-us/windows-hardware/test/hlk/testref/driver-compatibility-with-device-guard)

The strongest architecture is layered. Client-side inspection can identify machine-compromise indicators, but an attacker controlling the client always has structural advantages. Server-authoritative state and behavioral evidence remain useful even when the client monitor is evaded. Recent research surveys likewise divide effective defenses among server-side validation, client anti-tamper, kernel monitoring, and hardware-assisted trust rather than treating any one layer as sufficient.

Source: [A Systematic Review of Technical Defenses Against Software-Based Cheating in Online Multiplayer Games](https://arxiv.org/abs/2512.21377)

One practical warning is not to automatically ban from a single driver hit. Vulnerable drivers can arrive with legitimate hardware or utilities. A driver should be treated as one weighted signal, evidence should be preserved, and blocking or quarantine decisions should be distinguished from punitive account actions.

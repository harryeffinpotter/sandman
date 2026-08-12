# Better Than a Client-Centric Anti-Cheat

## Purpose

This document proposes a defensive anti-cheat architecture for a multiplayer Windows game. It uses BattlEye as a case study in the limitations of client-centric anti-cheat systems, but it is not a reverse-engineering report of BattlEye and does not claim access to its private implementation.

The objective is not to build an unbeatable client scanner. That objective is impossible on a general-purpose computer controlled by the player. The objective is to make cheating expensive, reduce what a compromised client can accomplish, collect independent and replayable evidence, and enforce rules with a low false-positive rate.

This document deliberately separates three kinds of information:

1. **Documented facts** — supported by vendor documentation, vulnerability records, or peer-reviewed research.
2. **Architectural analysis** — conclusions drawn from general operating-system and distributed-system principles.
3. **Community reports** — claims repeated by cheat developers, forum users, or affected players. These are useful for forming test hypotheses, but they are not treated as verified facts.

## Executive Thesis

BattlEye and similar products are often discussed as though their success depends on whether their client component can identify every cheat. That framing gives the attacker the advantage: both programs execute on a machine the attacker administers, and each new client-side detector creates another observable behavior that can eventually be studied.

A stronger custom system should therefore use the client as one sensor, not as the source of truth. The server should own game state and validate player intentions. Behavioral analysis should operate on authoritative telemetry. Hardware-rooted posture, user-mode integrity, and any narrowly scoped kernel measurements should contribute corroborating evidence rather than independently issuing irreversible punishment.

```text
                  +--------------------------+
                  | Authoritative game server|
                  | simulation + validation  |
                  +------------+-------------+
                               |
          +--------------------+--------------------+
          |                    |                    |
  Protocol invariants   Behavioral evidence   Client evidence
  and state machines    and information use   and device posture
          |                    |                    |
          +--------------------+--------------------+
                               |
                  Versioned risk assessment
                               |
           allow / observe / reject action / review / ban
```

## Historical Context

### The original advantage of client scanners

Early anti-cheat systems operated in an environment where many cheats were ordinary user-mode programs. Common techniques included predictable DLL injection, static byte patches, simple process handles, fixed signatures, and obvious graphics hooks. A client scanner could catch a meaningful fraction of abuse by enumerating modules, checking code integrity, scanning known byte sequences, and watching process access.

This approach was commercially attractive because it could be integrated across games without redesigning each game's server simulation. It also offered quick wins against redistributed public cheats.

Its structural weakness was present from the beginning: detection ran beside the adversary on an adversary-controlled endpoint. Once a detector became valuable, attackers had an incentive to study its observations and move below, around, or outside them.

### The escalation ladder

The resulting history is better understood as an escalation of visibility boundaries:

| Period or transition | Common attacker migration | Defensive consequence |
|---|---|---|
| Basic user-mode era | Static patches, injected DLLs, public signatures | Module, signature, and code-integrity scans were effective against unsophisticated tools |
| Stealthier user mode | Manual mapping, handle reuse, private executable memory | Anti-cheat needed memory-provenance and handle-origin analysis |
| Kernel escalation | Signed drivers, vulnerable-driver abuse, kernel memory access | User-mode observations were no longer sufficient |
| Externalization | External overlays, DMA, capture-based tools, input emulation | The absence of code inside the game stopped being strong evidence of cleanliness |
| Behavioral adaptation | Humanized aim, delayed actions, randomized inputs | Simple thresholds became brittle and easier to tune around |
| Current direction | Multi-layer client compromise plus server-visible mimicry | Defenses require server authority, temporal analysis, and evidence fusion |

“Patched” rarely means a whole technique class disappeared. Usually a particular implementation accumulated stable indicators, a vulnerable prerequisite was blocked, or operating-system policy raised its cost.

### Vulnerable signed drivers and BYOVD

Bring Your Own Vulnerable Driver, or BYOVD, illustrates the limitations of identity-only defenses. A legitimately signed but vulnerable driver can expose capabilities such as arbitrary kernel or physical-memory access. Attackers use the signed driver as a bridge to privileges their own unsigned code could not obtain normally.

The historical defensive response has included:

- Driver hashes, versions, signers, and certificate revocations.
- Microsoft's vulnerable-driver blocklist.
- Application Control policies.
- The Defender Attack Surface Reduction rule intended to prevent applications from writing vulnerable signed drivers to disk.
- HVCI or Memory Integrity, which moves code-integrity decisions into a VBS-isolated environment and prevents writable-and-executable kernel pages.

Microsoft explicitly warns that its vulnerable-driver blocklist cannot guarantee coverage of every vulnerable driver. This is why an anti-cheat cannot reduce BYOVD detection to filenames or hashes. It must also consider load provenance, exposed capability, device access, IOCTL behavior, platform posture, and downstream integrity effects.

Sources:

- [Microsoft recommended driver block rules](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/design/microsoft-recommended-driver-block-rules)
- [Microsoft driver security checklist](https://learn.microsoft.com/en-us/windows-hardware/drivers/driversecurity/driver-security-checklist)
- [HVCI compatibility guidance](https://learn.microsoft.com/en-us/windows-hardware/test/hlk/testref/driver-compatibility-with-device-guard)

### A documented BattlEye vulnerability

NVD records **CVE-2022-27095** for BattlEye version 0.9: an unquoted service path that could permit local privilege escalation. This is a specific historical vulnerability in an identified version. It should not be generalized into a claim that later BattlEye versions retained the problem or that it explains modern bypasses.

Source: [NVD CVE-2022-27095](https://nvd.nist.gov/vuln/detail/CVE-2022-27095)

Its relevant lesson for our design is broader: an anti-cheat is privileged security software and therefore becomes part of the attack surface. Service configuration, update delivery, IPC parsing, driver IOCTLs, and recovery behavior require the same threat modeling as any other high-privilege security product.

## What Can Credibly Be Called BattlEye's Weaknesses

Without internal source code or an authoritative vendor postmortem, it would be irresponsible to claim a precise list of private implementation failures. The following are defensible architectural limitations of any predominantly client-side anti-cheat and are therefore relevant to BattlEye-like designs.

### 1. Endpoint equivalence

The defender and attacker share the same machine. Administrative or kernel compromise can undermine ordinary user-mode observations. A more privileged client component improves visibility, but it also moves the contest to a more privileged layer.

### 2. Detection creates an observable oracle

When a specific client artifact immediately causes a kick or ban, attackers can perform differential testing. Over time, the enforcement system unintentionally teaches them which variants are observed. Delayed enforcement helps, but only when internal evidence remains precise and appealable.

### 3. Signature success is temporary

Signatures work well against unchanged public binaries. They are weaker against private builds, polymorphism, compromised trusted components, or methods whose important signal is behavior rather than bytes.

### 4. Kernel visibility is not absolute trust

A kernel driver can inspect states unavailable to ordinary applications, but it remains software within a mutable platform. Vulnerable drivers, firmware, DMA, hypervisors, and early-boot compromise all change the trust boundary. A kernel anti-cheat also introduces compatibility, stability, privacy, and vulnerability risks of its own.

### 5. Client scanning cannot cover non-memory cheats

Capture-based computer vision and synthetic input can operate without modifying game memory. Research such as *Invisibility Cloak* specifically describes visual aimbots as a class poorly addressed by traditional memory-oriented anti-cheat approaches.

Source: [Invisibility Cloak, USENIX Security 2024](https://www.usenix.org/conference/usenixsecurity24/presentation/sun-chenxin)

### 6. Cross-title generality competes with game-specific truth

A reusable anti-cheat can recognize common endpoint artifacts, but only the game server fully understands its movement rules, inventory state machine, visibility model, weapon timing, and information-disclosure rules. Generic endpoint inspection cannot replace those game-specific invariants.

### 7. False-positive pressure limits aggressiveness

Security tools, overlays, accessibility software, hardware utilities, developer tools, recorders, and old drivers can resemble pieces of cheat tooling. A commercial anti-cheat cannot punish every suspicious artifact without harming legitimate users. Attackers benefit from this unavoidable ambiguity.

### 8. The client may detect compromise after the game has already trusted it

If the server accepts client-asserted movement, damage, inventory ownership, or hidden information, detecting the bad client later does not undo match corruption. Prevention must begin in the game protocol and simulation.

## Community and Forum Claims: Context, Not Established Fact

Cheat-development forums and player communities commonly make claims such as:

- BattlEye is heavily signature-driven.
- A private cheat survives primarily because it is not widely distributed.
- Certain vulnerable drivers remain usable until explicitly blocklisted.
- Manual mapping, driver renaming, trace cleanup, or avoiding conventional handles is enough to evade detection.
- Bans prove a particular hook, allocation, overlay, or driver was detected.
- Delayed bans mean the product did not detect the cheat during play.
- One anti-cheat vendor is categorically weaker than another.

These reports matter because they reveal attacker beliefs and help prioritize red-team experiments. They do **not** establish how BattlEye works internally.

Specific cautions:

- A cheat surviving temporarily does not demonstrate non-detection; enforcement may be delayed or risk-scored.
- A ban occurring after one code change does not prove that change was the detected signal. Builds often differ in several ways, and server behavior may also have changed.
- Forum success reports exhibit survivorship and selection bias. Failed attempts, quiet detections, abandoned accounts, and private mitigations are underreported.
- Sellers have a financial incentive to overstate reliability and underreport detections.
- A driver not present on a public blocklist may still be detected through its behavior, signer, device interface, load chain, or effects.
- “Undetected” usually means “no enforcement observed during this test window,” not a verified absence of telemetry.

Accordingly, community reports should enter our process as tagged hypotheses:

```text
claim -> reproducible lab test -> independent telemetry -> confidence rating
      -> detector candidate -> shadow deployment -> measured validation
```

They should never enter production directly as ban rules.

## Our Replacement Principle: The Client Is a Sensor, the Server Owns Truth

The client should send intentions, not authoritative outcomes:

- “Fire was pressed at tick T,” not “player 42 took damage.”
- “Movement input was X,” not “my position is now Y.”
- “Transfer item A from container B to slot C,” not “I now own A.”
- “Attempt interaction with object X,” not “the interaction succeeded.”

The server should determine results using its authoritative state.

## Layer 1: Authoritative Simulation and Protocol Invariants

### Movement

Validate:

- Maximum acceleration, velocity, step height, and turn rate where applicable.
- Collision and traversal state.
- Teleport authorization and sequence identifiers.
- Vehicle or mount constraints.
- Server reconciliation history.
- Whether a claimed movement mode was legitimately entered.

Do not use only a maximum-speed threshold. Validate transitions and accumulated physical possibility across ticks.

### Combat

The server should own:

- Ammunition and magazine state.
- Fire rate and cooldowns.
- Reload state.
- Overheat state.
- Projectile origin and permitted direction.
- Damage calculation.
- Hit validation, preferably with bounded lag compensation.
- Weapon ownership and equipped state.

### Inventory and containers

Treat inventory operations as transactional state-machine requests. Validate:

- Ownership and access permission.
- Distance and line of interaction.
- Container-open state.
- Slot compatibility and capacity.
- Item version or transaction nonce.
- Source existence and destination availability.
- Atomicity, preventing replay and double spend.
- Whether contents were disclosed to that client at all.

This is particularly important because a client-memory anti-cheat cannot repair an inventory protocol that trusts client outcomes.

### World interaction

Validate interaction range, visibility where appropriate, cooldowns, current animation or action state, prerequisite items, and ordering. Impossible requests should be rejected independently of whether the client is classified as a cheater.

## Layer 2: Information-Exposure Accounting

For each client and server tick, retain a compact record of what information was legitimately available:

- Replicated entities.
- Directly visible entities.
- Recently visible entities and expiry.
- Audible events.
- Teammate or sensor disclosures.
- Occlusion and approximate line of sight.
- Network relevancy decisions.

This enables questions that ordinary endpoint scanning cannot answer reliably:

- Did aim begin tracking before the target was disclosed?
- Does tracking continue through opaque geometry beyond plausible memory?
- Does the player repeatedly choose the correct hidden target among alternatives?
- Are pre-aim decisions correlated with server-hidden locations?

The aim is not to ban for one suspicious coincidence. It is to accumulate statistically meaningful, explainable evidence over time.

## Layer 3: Temporal Behavioral Detection

Avoid single static thresholds. Model sequences and context:

- Aim velocity, acceleration, jerk, and correction shape.
- Acquisition time after legitimate disclosure.
- Crosshair placement before visibility.
- Target-switch timing and choice.
- Recoil compensation correlation.
- Shot timing versus target exposure.
- Input timing, entropy, and device sampling characteristics.
- Movement correction after server reconciliation.
- Inventory and interaction timing.
- Consistency across weapons, sensitivities, sessions, and skill development.

Behavioral models must be explainable enough for review. A useful system should produce a statement such as “sustained sub-disclosure tracking across 38 encounters, combined with mechanically repeated correction curves,” not merely “model score 0.997.”

Relevant research directions include:

- [BotScreen, USENIX Security 2023](https://www.usenix.org/conference/usenixsecurity23/presentation/choi)
- [XGuardian, USENIX Security 2026 session](https://www.usenix.org/conference/usenixsecurity26/technical-sessions)
- [Systematic review of technical anti-cheat defenses](https://arxiv.org/abs/2512.21377)

Research results must still be reproduced on our game's population. Different genres, tick rates, aim mechanics, input devices, accessibility tools, and skill distributions can invalidate published thresholds.

## Layer 4: Client Integrity as Corroborating Evidence

A user-mode client can report useful observations:

- Game-file and module integrity.
- Module provenance and signatures.
- Executable private-memory topology.
- Unexpected handles and access rights.
- Debugging state.
- Loaded-driver inventory and known vulnerability status.
- Overlay, capture, and input-device context.
- Anti-cheat health and version.

However, the server must not accept a self-issued `clean=true` verdict. Reports need:

- Server-issued nonces.
- Session binding.
- Freshness and replay protection.
- Authenticated transport.
- Versioned schemas.
- Missing-report and stale-report semantics.
- Cross-checking against server-observed behavior.

## Layer 5: Hardware-Rooted Device Posture

TPM-backed Device Health Attestation can provide stronger evidence about boot-time posture, including Secure Boot, kernel debugging, test signing, ELAM, VBS, and Code Integrity policy state.

Sources:

- [Trusted Platform Module overview](https://learn.microsoft.com/en-us/windows/security/hardware-security/tpm/trusted-platform-module-overview)
- [Windows Device Health Attestation](https://learn.microsoft.com/en-us/windows/security/operating-system-security/system-security/protect-high-value-assets-by-controlling-the-health-of-windows-10-based-devices)

Attestation has limits:

- It describes measured boot and security posture, not the honesty of every later game action.
- Availability varies by hardware and configuration.
- Requiring it may exclude legitimate users.
- Privacy and stable-device-identity implications must be handled deliberately.

It is most appropriate as a condition for higher-trust competitive modes, not as proof that a session is cheat-free.

## Layer 6: A Minimal Kernel Component, If Justified

A kernel component should not be the foundation by default. It should be introduced only when measured blind spots justify the additional attack surface.

If used, it should have:

- A narrowly defined measurement purpose.
- No arbitrary process-memory, kernel-memory, physical-memory, MSR, or port-I/O primitive.
- Strict IOCTL validation and caller authorization.
- HVCI compatibility.
- Minimal mutable global state.
- Versioned protocol and safe update rollback.
- Fuzzing, static analysis, Driver Verifier, and independent review.
- Explicit privacy documentation.
- A failure mode that does not corrupt or crash the host.

The anti-cheat driver must be treated as a high-value target. A production-signed security driver with an overly powerful interface can become the next BYOVD tool.

## Layer 7: Risk Fusion and Graduated Response

Do not create one detector whose Boolean result directly bans an account. Maintain a versioned risk record with evidence provenance.

Example signal classes:

| Signal | Typical confidence | Appropriate immediate action |
|---|---:|---|
| Cryptographically impossible or invalid protocol transition | Very high | Reject the action; preserve evidence |
| Replayed inventory transaction | Very high | Reject; rate-limit; investigate account/session |
| Vulnerable driver plus suspicious device activity | Medium to high | Increase telemetry; restrict high-trust mode if policy permits |
| Sustained pre-disclosure target tracking | Medium | Accumulate evidence and review |
| Unexpected overlay alone | Low | Context only |
| Missing or stale client report | Context-dependent | Retry, degrade trust, or restrict mode; do not assume cheating automatically |

Graduated responses can include:

1. Accept normally.
2. Increase telemetry.
3. Reject an invalid action.
4. Apply server-side rate limits or reconciliation.
5. Move the session into higher-scrutiny matchmaking.
6. Restrict ranked or economy-sensitive play.
7. Queue human review.
8. Enforce after corroboration.

Never permanently ban solely because one legitimate-but-vulnerable driver is installed.

## Evidence and Appeals

Every serious enforcement decision should be reproducible from a compact package containing:

- Account and session pseudonymous identifiers.
- Server tick interval.
- Relevant player inputs.
- Authoritative world state.
- Replication and visibility history.
- Protocol violations.
- Behavioral features.
- Client-posture observations.
- Detector and model versions.
- Exact rules and thresholds that contributed.
- Confidence and alternative explanations considered.

Evidence should be retained according to a published policy, access-controlled, and minimized to what is necessary. Appeals should be able to distinguish detector error, compromised accounts, shared machines, accessibility software, and deliberate cheating.

## Preventing the Detector From Becoming an Oracle

- Do not tell the client which exact detector fired.
- Do not expose internal scores or detailed integrity results.
- Separate invalid-action rejection from punitive enforcement.
- Avoid enforcement at the exact first weak signal.
- Keep high-value thresholds and fusion logic server-side.
- Introduce detectors in shadow mode first.
- Use controlled red-team canaries to test telemetry coverage.
- Preserve an internally precise reason even when the external message is generic.

Delayed enforcement should not become arbitrary enforcement. Every internal decision still needs a traceable evidentiary basis.

## Privacy, Compatibility, and Legitimacy

An anti-cheat that behaves like indiscriminate surveillance will lose user trust and create its own security risk.

Principles:

- Collect the minimum data needed for a defined detector.
- Prefer structured measurements over arbitrary file or memory contents.
- Publish categories of collected data and retention periods.
- Separate security telemetry from advertising and unrelated analytics.
- Protect telemetry cryptographically in transit and at rest.
- Restrict employee access and audit queries.
- Provide a path for accessibility and compatibility review.
- Test common overlays, recorders, hardware utilities, RGB tools, debuggers used by developers, and assistive devices.
- Do not punish a user merely for declining optional invasive telemetry; instead define which competitive modes require a stronger trust posture.

## Pitfalls We Intend to Avoid

- Trusting the client to enumerate itself honestly.
- Treating a clean scan as proof of a clean session.
- Depending primarily on driver hashes, filenames, or signatures.
- Assuming a signed driver is safe.
- Building dangerous kernel primitives into a production-signed driver.
- Scanning aggressively while leaving game outcomes client-authoritative.
- Treating obfuscation as a security boundary.
- Using one threshold across all skill levels and input devices.
- Training only on synthetic or public cheats.
- Evaluating models on data leaked between training and test populations.
- Deploying detector changes without shadow-mode validation.
- Automatically banning on one weak signal.
- Providing attackers immediate and precise feedback.
- Collecting personal data unrelated to game integrity.
- Breaking legitimate software without a compatibility and appeal process.
- Failing silently when the anti-cheat is unhealthy.
- Failing closed so aggressively that ordinary platform faults punish users.

## Development Roadmap

### Phase 1: Define truth

- Document authoritative movement, combat, inventory, and interaction state machines.
- Identify every client message that currently asserts an outcome.
- Add sequence numbers, nonces, and idempotency to economy-sensitive operations.
- Reject impossible state transitions on the server.

### Phase 2: Make evidence replayable

- Build deterministic server replay for short incident windows.
- Version telemetry schemas.
- Preserve visibility and replication history.
- Create an internal reviewer that explains detector findings.

### Phase 3: Establish conservative rules

- Deploy invariant checks in observation mode.
- Measure legitimate-tail behavior before selecting thresholds.
- Build compatibility cohorts by input device, latency, platform, and accessibility use.
- Create a false-positive response and appeals process before punitive rollout.

### Phase 4: Add temporal behavioral scoring

- Start with interpretable statistical features.
- Validate across patches and skill bands.
- Require corroboration for punitive decisions.
- Monitor concept drift and retrain with strict dataset lineage.

### Phase 5: Add endpoint posture

- Deploy a small user-mode integrity agent.
- Bind reports to server challenges and sessions.
- Add vulnerable-driver and platform-posture evidence.
- Offer or require hardware-rooted attestation only for modes whose threat model justifies it.

### Phase 6: Decide whether kernel visibility is worth the cost

- Quantify which attacks remain materially harmful.
- Determine whether server and user-mode controls already contain them.
- If a driver is justified, constrain it to the smallest measurable purpose and subject it to independent review.

### Phase 7: Continuous adversarial validation

- Maintain an internal red team separate from detector authors.
- Turn community claims into controlled hypotheses rather than assumptions.
- Test false-positive attacks as seriously as bypass attempts.
- Evaluate backend, account, economy, and protocol abuse—not only memory cheats.
- Rotate canaries and validate that telemetry survives client tampering.

## Success Metrics

Success should not be “no bypass was posted this month.” Measure:

- Percentage of authoritative actions protected by server invariants.
- Invalid actions rejected before affecting other players.
- Time from cheat emergence to reliable detection.
- False-positive rate by detector and cohort.
- Appeal overturn rate.
- Evidence completeness.
- Ranked-match exposure before intervention.
- Cheat development cost and required sophistication.
- Client performance and crash impact.
- Privacy incidents and unnecessary data volume.
- Percentage of enforcement decisions supported by more than one independent evidence class.

## Conclusion

The main lesson from BattlEye's perceived and documented limitations is not that a different kernel trick will finally win the arms race. The lesson is that client inspection cannot safely carry the entire trust model.

Our design should make the server authoritative, account for what information each client legitimately received, analyze behavior over time, use endpoint integrity only as corroboration, and preserve evidence sufficient for review. A kernel component may add visibility, but it must remain minimal and must never become a new privileged vulnerability primitive.

Community claims remain valuable as adversarial intelligence, but they are hypotheses until reproduced. Documented facts, measured behavior, and independently corroborated evidence—not reputation or forum consensus—should drive both engineering and enforcement.

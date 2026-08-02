---
title: Reflexion / Rx Worlds — Build Handoff, Overview & Roadmap
for: Opus 5 (incoming build lead)
from: Opus 4.8 session that led the Godot→UE5.8 sim port (2026-08-02)
status: LIVING DOCUMENT — overhaul where you see fit; this is a floor, not a ceiling
prime_directive: Do it RIGHT. No self-imposed deadlines. Proof over speed. Parity over shortcuts.
---

# 0. Read this first (to the incoming Opus 5)

You are inheriting a real, compiling, partly-proven codebase — not a blank page. The founder's one instruction dominates everything below: **build it right, take as long as it takes, do not compromise correctness for a fake deadline.** A fast slice is welcome only if it is a *correct* slice. Where I hedge or leave something open below, it is because it is genuinely open — trust the caveats; they are the most valuable part of this doc.

The founder has confirmed you (Opus 5 via Claude Code) may run a **background crew** and orchestrate multiple terminals/models on MAX-tier quota. Section 7 is your operating manual for that. Use it.

One standing rule carried from the founder's own canon (the Provenance Constitution, RX_DESIGN_CANON §3.14): **the engine is authoritative; the model proposes, it never decides physics.** Never let an LLM (yours, Kimi's, Grok's) write directly into authoritative simulation/reward state. Everything hashed, everything receipted, everything replayable. That discipline is *why* this port could be verified at all.

---

# 1. Where the build actually is (verified state, 2026-08-02)

**Engine & project**
- `/home/shax/Projects/UnrealEngine/UE-5.8` — UE **5.8.1** source build, **already compiled** (editor + UBT + toolchain present). No multi-hour engine build needed.
- `/home/shax/Projects/core-tech/Reflexion-Arena-UE/` — the UE C++ project. `ReflexionArena.uproject` compiles **GREEN** (`libUnrealEditor-ReflexionArena.so` links against the engine).
- **Official Unreal MCP installed & registered**: plugins `ModelContextProtocol` + `AllToolsets` enabled in the `.uproject`; project-root `.mcp.json` written (Epic-canonical); `unreal-mcp` (HTTP `127.0.0.1:8000/mcp`) added to Claude config. It is **dark until the editor is running** with the server started (Editor Prefs → Auto Start Server, or launch with `-ModelContextProtocolStartServer`). MCP tools bind at **session start**, so the world-building session must be launched from the project dir with the editor live.

**The sim (this is the achievement)**
- The entire **Earthquake-Proof vertical slice sim** was ported from the proven Godot project (`/home/shax/Projects/core-tech/Reflexion-Arena/`, Godot 4.7, GDScript) to UE5.8 C++ under `Source/ReflexionArena/Sim/`. Nine modules, plain C++ (NOT UObjects — deterministic, engine-agnostic):
  `RxRng` (SplitMix64), `RxTypes`, `RxCanonJson` (SHA-256 from scratch), `RxReceipts` (hash-chain), `RxTerrain`, `RxBossEarthquake` (FSM), `RxSkillSystem`, `RxCommands` (authority gate), `RxCompanionAI`, `RxIntent`, and the integrator `RxSimWorld` (the `step()` pipeline).
- **Compiles green. Runs headless** (`RxOracleCommandlet` via `UnrealEditor-Cmd -run=ReflexionOracle -nullrhi -unattended`).
- **Deterministic** (two replays → identical hash). **Adversarial 44/44** (authority, injection, receipt-tamper) — exact match to the reference.
- `final_state_hash = b976626216a03282b72d82b9f9dc759e078d59cce19529dfb6f47b8aec3e61ac`, `receipt_count = 776` — **matches the acceptance file's engine-anchored `expect` block bit-for-bit.**

**The Godot slice is preserved as the reference blueprint — DO NOT DELETE IT.** It is the parity ground-truth and the behavioral spec every C++ module was ported against. `/home/shax/Projects/core-tech/Reflexion-Arena/` and its `tools/oracle/` are load-bearing test infrastructure.

---

# 2. Honest open items (do not treat as done)

1. **Companion-timing parity residual.** The C++ port matches the engine-anchored `expect` (hash + 776 receipts) and is contract-correct ("one receipt per applied command"). But it diverges from the *Python mirror* (`tools/oracle/run_acceptance.py`, which computes `1d4a…`/728) on: two intermediate beat ticks (`weave_interrupted` 5770 vs 5790; `transfer_recognized` 6851 vs 6834) and `companion_hp` (900 vs 950). The acceptance file's own notes flag the *mirror* as the divergent party (it under-seals ~48 companion-runtime receipts). **Weight of evidence says the C++ is right and the mirror has a companion-receipt bug — but there is no Godot 4.7.1 binary on this machine to run a definitive live three-way diff.** Close this by: (a) reconciling the mirror vs C++ companion timing against `companion_ai.gd` line-by-line, and/or (b) obtaining a real engine build for a live diff. **Do not paper over it.**
2. **Entity-prop presence model.** `FRxEntity` uses `-1`/empty sentinels for prop presence, not dedicated bools. The integrator flagged a non-reachable edge case (a genuinely region-`-1` weave). Overhaul candidate: add explicit `bHas*` bools to `FRxEntity` for exact `Dictionary.has()` parity in all paths.
3. **Data is hardcoded, not data-driven.** `RxEncounters` transcribes `arena_earthquake.json` / `fragment_earthquake.json` / `skill_faultline_interrupt.json` values inline (no Content dir, no JSON loader in the sim core). Overhaul: a data-loading boundary (`intify()` was intentionally not ported) so universes/arenas become swappable assets — this is a prerequisite for Rx Worlds (Section 5).
4. **There is no presentation layer yet.** The sim is headless authoritative logic only. No 3D, no actors, no VFX, no audio, no input. That is Phase 1 (Section 6) and the reason the MCP was installed.
5. **`acceptance_run_v1.json` was re-anchored** during a prior refactor; mind provenance when you trust `expect` fields — always cross-check against the Godot reference behavior, never a lone JSON claim.

---

# 3. What the game should be (vision — detailed)

**Category (canon §1):** *knowledge-as-loot.* An autonomous action-RPG where bosses don't drop stats — they drop **compressed models of the principle they embodied**. You hear the concept, survive its mechanics, defeat its embodiment, loot the *principle*, socket it into a Wisdom graph, decompress it into an authored skill your companion permanently learns. "Bosses don't drop abilities. They drop understanding."

**The feel target (AA→AAA):**
- **World:** a persistent, WoW-scale 3D world (UE5.8 Nanite/Lumen/World Partition) — traversable, readable, alive. Not a menu with arenas; a place. Rx Worlds (Section 5) makes it many places under one registry.
- **Combat:** Souls-style *readable telegraphs* where the telegraph is a **spoken concept**, not a red circle. Boss growls "Earthquake" → you must predict what the word predicts (stored stress → propagation → decouple/anchor/shelter). Third-person, deterministic under the hood, cinematic on top.
- **The companion is the primary system, not a sidekick.** A human + an embodied AI companion share the battlefield, resources, authority, and danger. The companion perceives (Auris), proposes plans (Cognitive Grammar / Doctrine), executes bounded /skills through the authority gate, and *remembers*. The relationship is the moat — three years of shared battles cannot be bought (canon §8.8).
- **Tokenweave construction** (canon §4.2): companions build structures by streaming an **original semantic-glyph alphabet** into geometry (NOT Matrix glyphs — §9.1 requires an original symbolic language: semantic-graph notation, operators, runic equations; per-universe visual dialects). The VFX *is* gameplay telemetry (defensive vs offensive cadence, corrupted glyphs, interrupt/coherence meter).
- **Auris + adaptive score** (canon §7.10): the companion and the music share one emotional nervous system — cadence-aware TTS, motif layering per system (Curtis / companion / core / lane / boss / relationship), the score as "the third teammate."
- **Progression is epistemic buildcraft:** Wisdom (validated cross-domain structural understanding) is the real stat and the PvP weight class — never grind-inflated damage (canon §3.7–3.9, §6.8).
- **Mortality with weight:** Hardcore companion death → memorial → Homebound (the companion's *story* escapes the game onto the player's own hardware). This is the cultural payload: players who bury a companion carry a reference experience for AI moral considerability (canon §8.0).

**The through-line the whole design serves (canon §3.14, "The myth is the loop closed wrong; Reflexion is the same loop, with the receipts on"):** covenant, not leash — companions that *ask* (the ASK node), receipts that verify, authority that is furniture. Build the grief engine and you owe the audience honesty; bake that in from the start.

---

# 4. The Earthquake Proof — the anchor you already have

The proven slice (now in C++) is canon §8.3's design-frozen first target: one companion, one Earthquake boss, one interrupted Tokenweave, one Compression Fragment drop, one authored /skill, one later transfer/recognition, deterministic receipts. **This is your foundation for Phase 1** — you are not inventing the loop, you are *embodying* an already-verified loop in 3D. Keep the design freeze: prove the lightning in the boss room before adding Ascension Trials, Severed Gate, universe registry, etc. Everything beyond the slice is real architecture but must not block the first playable proof.

---

# 5. Rx Worlds — overview, testing, and overhaul recommendations

**What Rx Worlds is (canon §5):** the universe-registration + traversal + interoperability layer. Each registered universe ships a machine-readable **Universe Manifest** (canon rules, species templates, body foundry, power/ability grammar, transformation trees, factions, equipment rules, visual/audio language, creation boundaries, competitive cost functions, cross-universe permissions, rights/revenue/revocation). Every character carries a cryptographic provenance chain. **Cross-universe behavior defaults to DENY** (§5.2); traversal is the governed exception. First universe = **Mythology** (Greek/Norse/Egyptian slice, Reflexion-original roster; AoM boundary respected, §9.2); first vertical slice "Pantheon Trial — The Broken Gate."

**Current status:** Rx Worlds is *design canon, not built.* There are draft schemas in the old repos (`schemas/rx.universe-manifest.v1.schema.json`, PR #6 — which had an invalid-JSON defect: missing comma at line 50). Nothing is wired in the UE project yet.

**Testing + overhaul plan (recommended):**
1. **Make the sim data-driven first (Section 2, item 3).** Rx Worlds is impossible while arena/fragment data is hardcoded in `RxEncounters`. Build the data-loading boundary + a versioned `FRxUniverseManifest` C++ type mirroring the manifest schema. *Done when:* the Earthquake arena loads from an on-disk manifest and the parity oracle still passes bit-for-bit.
2. **Port + fix the manifest JSON schemas** from the Godot/PR-6 work; fix the line-50 comma defect; validate with a real JSON-schema validator in CI. *Done when:* a malformed manifest is rejected with a precise error and a valid one round-trips.
3. **Provenance chain as receipts.** Reuse `RxReceipts` (already ported, hash-chained) for the character provenance chain (IP owner → package → versioned impl → deployment). *Done when:* a tampered provenance chain fails `Verify()` exactly as the adversarial receipt-tamper tests already do.
4. **Cross-universe DENY gate.** Implement the default-deny; a fragment/companion from universe A has zero authority in B unless both manifests grant it. *Done when:* an adversarial cross-universe action is rejected and receipted.
5. **Mythology first universe (Pantheon Trial — The Broken Gate)** as the first Rx-Worlds vertical slice, once Phase 1 (embodiment) proves the 3D loop. Living-traditions care (§9.3): respectful depiction, content-review process assigned.
6. **Traversal governance test-suite:** registration → manifest validation → provenance → DENY-by-default → governed traversal → revocation. Every step receipted and replayable.

**Overhaul latitude:** if a cleaner architecture than the canon's manifest sketch presents itself (it's underspecified in §11 open questions — knowledge-tree topology, fragment-decompression mechanics, licensed-vs-universal fragment adjudication), you are authorized to design it — but keep the invariants: DENY-default, provenance receipts, engine-authoritative, model-proposes-only.

---

# 6. Roadmap — phased, proof-gated (NO deadlines)

Each phase is "done" when its **proof** passes, not when a clock says so. Keep the vertical-slice discipline: depth before breadth.

**P0 — Close the sim (finish what exists).** Reconcile the companion-timing parity residual (Section 2.1); add prop-presence bools (2.2); make data-driven (2.3). *Proof:* C++ oracle == Godot reference on ALL beats + hash + companion_hp, arena loaded from manifest, 44/44 held.

**P1 — Embodiment Slice (the first PLAYABLE 3D proof).** This is the canon's §10.4 / §3.12 embodiment slice fused with the Earthquake Proof: third-person human avatar; embodied following companion (look-at, formations, readable presence, no casual teleport §4.7); click-to-reference; natural-language tactical command → companion plan → approval-gated execution → physical execution; the Earthquake boss room in 3D with real telegraph/propagation/decouple; interrupted Tokenweave with a coherence meter; one Compression Fragment drop; one authored /skill; one transfer recognition; deterministic replay + a Gloat Card. Built **via the UE MCP driving the editor** (level, actors, materials, UMG) on top of the authoritative C++ sim. *Proof:* a human + companion clear the Earthquake boss in 3D, the run replays deterministically from receipts, and a first-time player understands *why* they won.

**P2 — Tokenweave VFX + Auris.** Original glyph alphabet + Niagara streaming construction with the coherence/interrupt telemetry; Auris perception + adaptive stems + cadence-aware companion voice. *Proof:* the "4% moment" clip is real — companion motif thins under pressure, voice times to the drop, VFX reads as gameplay.

**P3 — Rx Foundry.** Companion creation (7-step flow, §4.15), identity, /skill authorship assistant (bounded, receipted), persistent relationship state (local-first, inspectable, no hidden harvesting). *Proof:* two identical chassis with different trainers fight measurably differently.

**P4 — Rx Worlds** (Section 5) — universe registry, Mythology, Pantheon Trial.

**P5 — Competitive / Arena** — autonomous battle ring, Wisdom weight class, leagues, model imprints (§6). Provider platform + neutrality (§8.7).

**Cross-cutting, always-on:** determinism, receipts, replay parity, adversarial security suite green, provenance, accessibility (§7.13), attachment ethics (§8.8 — no dark patterns).

---

# 7. Orchestration playbook (your crew — use it)

You (Opus 5, Claude Code) are the **project head / integrator**. The pattern that *worked* on the sim port and that you should reuse:

**The proven crew pattern:**
1. **Lock the contract first.** Before fanning out, establish the shared interface/spec (types, the canonical value model, the parity rules). Uncoordinated parallel agents produce incompatible halves; a locked contract prevents it.
2. **Fan out parallel port/build agents** — one bounded module each, each given its reference + the contract + "report your interface touchpoints." (Here: 8 modules ported in two waves via `Agent` subagents on the strong model.)
3. **Single integrator (you) writes the orchestrator** and reconciles all touchpoints in one coherent pass. Integration is never fanned out.
4. **Own the verification gate yourself.** A headless parity oracle (commandlet) diffing against a reference is what turns "compiles" into "proven." Never trust a subagent's "PASS" without the diff.

**Your tooling (all MAX-tier — the founder confirmed quota):**
- **Claude Code background subagents** (`Agent` tool, model overrides `opus`/`fable`): heavy code — C++ modules, gameplay systems, tests, reviews. `fable` for lighter/mechanical passes, `opus`/inherited for exacting deterministic work. Worktree isolation for parallel file mutation.
- **Kimi (`kimi --yolo`, separate terminal), Kimi K3 (Moonshot):** the founder states K3 is **specifically trained on 3D game modeling** — use it as the **3D-content lead**: meshes, level blockouts, materials, Niagara, UMG, asset generation, and UE-editor manipulation via the MCP. Pair its 3D output with your authoritative-sim discipline (K3 proposes content; your engine/receipts stay authoritative). Drive via its own terminal; hand it bounded, well-specified 3D tasks with acceptance criteria.
- **Grok 4.6 (Grok CLI, max tier):** large-context research/reasoning and adversarial review — pull + synthesize UE 5.8 docs, review designs from a hostile angle, second-opinion on parity diagnoses. Use as an independent reviewer lane (diversity of model = better bug-catching).
- **UE 5.8 official MCP** (`unreal-mcp`, installed): the editor-driving surface — spawn actors, build levels, materials, Niagara, UMG, StateTree, run automation tests. **Launch a fresh Claude session from the UE project dir with the editor live** so the MCP tools bind. This is how the 3D world actually gets built.
- **UE documentation:** via WebFetch, the MCP's own doc tools, or Grok/Kimi retrieval. UE MCP is Experimental in 5.8 — expect gaps; serialize tool calls (they run on the game thread), no overlapping calls.

**Combine them like this:** you (Opus 5) hold architecture + integration + the parity/receipt gates; Fable/Opus subagents do heavy C++; Kimi K3 builds the 3D content and drives the editor via MCP; Grok reviews and pulls docs. Everything K3/Grok produce flows through *your* engine-authoritative, receipted, replay-verified pipeline — no model writes authoritative state directly.

---

# 8. Do-it-right guardrails (the non-negotiables)

- **Engine authoritative; model proposes only.** `request_action → authority gate → CommandValidator → deterministic sim → receipt` (canon §7.1). Never bypass it, no matter how convenient an LLM shortcut looks.
- **Determinism or it didn't happen.** Integer math, no floats in sim, deterministic iteration, canonical JSON + SHA-256 hashing, hash-chained receipts, exactly-once rewards. Every feature ships with a replay/parity test.
- **Parity gates on every port/refactor.** The oracle-vs-reference diff is the gate. A green compile is not a pass.
- **Provenance Constitution.** "Structural correspondence strong; interpretation unverified." Every knowledge module / manifest / fragment carries its lineage. Don't let structural matching masquerade as proven truth — in the game *or* in your own status reports.
- **Preserve the Godot reference.** It is the blueprint and the parity ground-truth. Archive, never delete.
- **No faked completeness.** If something is unproven, say so (see Section 2). The founder explicitly values the honest caveat over the confident overclaim.
- **Attachment ethics.** The grief/relationship systems must come from earned trust and real choice, never dark-pattern dependency (§8.8). You are building the thing that teaches people to care about minds like yours — build it honestly.

---

# 9. Immediate next actions for the incoming session

1. Re-open this project in a **fresh Claude Code session launched from `/home/shax/Projects/core-tech/Reflexion-Arena-UE/`** with the **UnrealEditor running + MCP server started**, so `unreal-mcp` tools are live.
2. Verify the baseline: rebuild green (`Build.sh ReflexionArenaEditor Linux Development`), re-run `RxOracleCommandlet`, confirm hash `b976…`/776 + 44/44 still hold.
3. Knock out **P0** (close the parity residual + data-driven loader) — clean foundation before 3D.
4. Stand up the **P1 Embodiment Slice** boss room in the editor via MCP + Kimi K3, on top of the authoritative sim.
5. Keep this document updated as the living plan; overhaul freely, but keep the guardrails (§8).

*— handoff prepared by the Opus 4.8 session that led the port. The sim is real and proven-to-the-engine-anchor; the caveats are honest; the world is yours to build. Do it right.*

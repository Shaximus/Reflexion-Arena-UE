---
title: Rx Skill Grammar — Condition & Effect Enums v1
status: DRAFT FOR REVIEW — not canon, not merged, not implemented
author: extraction + drafting pass, 2026-08-02 (founder-authorized)
companion_doc: RX_SKILL_GRAMMAR_V1.md
supersedes: nothing
---

# Rx Skill Enums v1 — the closed Condition/Effect vocabulary

## 0. What this document is, and what it is not

The source conversation ends mid-offer — *"Want me to expand the Skill Grammar
with the actual Condition/Effect enums next…"*
(`/home/shax/Downloads/openclaw_workspace_game/19f8304e-2162-8a40-8000-0000bc2ff171_Grok-skills.md:5252`)
— and never delivers a schema an engineer could build. It delivers **type-name
lists** (`:5256-5291`) and **parameter envelopes** (`:5369-5404`), but never the
binding between a named type and the simulation state it reads or writes. That
binding is this document.

**Provenance discipline used throughout.** Every row carries one of:

| Mark | Meaning |
|---|---|
| **CANON** | Traceable to `RX_DESIGN_CANON_V1-SHENRON.md` or the source export, cited `path:line`. |
| **DERIVED** | My reasoned extension. Not in any source. Rejectable without breaking anything cited. |
| **OPEN** | Genuinely underspecified. Not resolved here on purpose. |

And every entry carries an independent **implementation status**:

| Status | Meaning |
|---|---|
| **LIVE** | Backed by simulation code that exists today, cited `path:line`. Buildable now. |
| **PARTIAL** | Base operation is LIVE; one named parameter needs a small, precedented addition. |
| **SPECIFIED** | Required by canon, but **no simulation structure exists**. Needs new sim state before it can be used. |

> **The load-bearing result of this document:** the EARTHQUAKE reference case
> (canon `RX_DESIGN_CANON_V1-SHENRON.md:86-101`) encodes **completely and using
> only LIVE entries** — see §6. Nothing in the SPECIFIED set is required to
> express it. That is the evidence the vocabulary is correctly sized rather than
> aspirational.

---

## 1. The integer decision — stated and justified

**Decision: every value in a Condition or Effect is an integer. No floats
anywhere in a skill record, a loot record, a condition parameter, or an effect
parameter. Ratio-valued fields (`difficulty`, `teaching_value`, `risk_level`,
fractional `magnitude`) are expressed in basis points — integers 0–10000, where
10000 = 1.0.**

This overrides the source, which uses `difficulty: 0.92` (`:5231`),
`teaching_value: 0.88` (`:5238`), `risk_level: 0.72` (`:5448`), and
`magnitude: 0.45` (`:5435`).

### 1.1 Why this is forced, not preferred

This is not a stylistic call. It is a type-system impossibility:

`Source/ReflexionArena/Sim/RxCanonJson.h:13` states the canonical JSON law:

> `ints / strings / bools / arrays / dicts ONLY — never float, never null`

and `RxCanonJson.h:24-31` implements exactly that — `ERxJsonType` has members
`Bool, Int, String, Array, Object`. **There is no float variant.** A float
cannot be constructed in the value type, so it cannot be serialised, so it
cannot be hashed.

This matters because the skill record *is* hashed. `RxSkillSystem.cpp:259`
computes the artifact's identity as
`FRxCanonJson::HashValue(Artifact.ToJson(/*bIncludeHash=*/false))`, and
`RxSkillSystem.h:101-102` stores `FragmentHash` and `SkillHash` on the artifact
itself. A float-valued field in that record is not "slightly non-canonical" —
it is unrepresentable.

### 1.2 Why it is also correct on the merits

Three independent reasons, in case someone later replaces the value type:

1. **Byte-stability.** Even where a float is representable, its decimal
   rendering is not byte-stable across platforms and libraries. The entire
   Godot→UE5.8 port exists to preserve SHA-256 parity against a reference
   implementation (`RxCanonJson.h:6-9`; anchor
   `b36ad6d028e1b545…` / 776 receipts, `RX_WORLDS_HANDOFF_AND_ROADMAP.md:31`).
   One float in a hashed structure retires that anchor.

2. **These fields are not presentation.** The tempting reading is "difficulty
   and teaching_value are just flavour." They are not:
   - `risk_level` "feeds ranked cost functions" — canon `:992`, and ranked cost
     functions gate ranked *eligibility*.
   - `teaching_value` / `synthesis_cost` / `ranked_eligible` are economy inputs
     — canon `:1004`, `:1018`.
   - Canon `:1018` requires all rewards be "gated by deterministic receipts,
     exactly-once, on the existing PentaCLI hash-chained ledger," reinforced by
     the source export's standing directive: *"Do not invent a second
     progression ledger"* (Grok-skills export, line 1254 — **not** a canon
     line reference).

   A number that gates an exactly-once ledger entry is authoritative by
   definition. There is no "presentation-only" reading available for it.

3. **Integer division is already the house style, and it is exact.** The sim
   computes precursor thresholds as `STRESS_THRESHOLD / 4`, `/ 2`, `* 3 / 4`
   with the comment *"integer division, exact"*
   (`RxBossEarthquake.cpp:121-125`), and wave damage as
   `Force / RxSim::WAVE_DAMAGE_DIV` (`RxSimWorld.cpp:1052`). Basis points fit
   this idiom exactly.

### 1.3 The one legitimate escape hatch — and its limit

`RxSimWorld.h:35-39` defines the event log as explicitly *"NOT part of the
canonical snapshot / state hash."* That is the sim's existing, sanctioned
presentation channel. So:

- **Permitted:** a UI renders `9200 bp` as `"0.92"` at display time.
- **Permitted:** an `emit_signal` payload (authority class **A0**, §3) carries a
  value that never enters the snapshot.
- **Forbidden:** *storing* a float in the loot record, the skill record, a
  condition parameter, an effect parameter, or any receipt.

**Rounding rule (DERIVED).** All integer division truncates toward zero. Where a
basis-point value multiplies a sim quantity, the multiply happens **before** the
divide: `result = (quantity * bp) / 10000`. Stated explicitly because
operation order is where determinism defects hide.

### 1.4 The contradiction this creates — flagged, not resolved

Canon `RX_DESIGN_CANON_V1-SHENRON.md:992` states `risk_level` as
**"(0.0–1.0, feeds ranked cost functions)"**. That is a canon-stated float range
and it is unrepresentable in the canon-mandated canonical JSON. **This is a
genuine canon-vs-code contradiction, not an oversight on either side.** The
basis-point proposal above is **DERIVED** and requires founder ratification
because adopting it edits a range the canon states in prose. See §7, C-2.

---

## 2. Shared reference and parameter types

All **DERIVED** unless noted. These exist so that no enum entry needs a free-form
field.

### 2.1 References (how an entry names its subject)

| Reference | Resolves to | Notes |
|---|---|---|
| `self` | the acting entity id | CANON target vocabulary, `:5374` |
| `primary_target` | the command's declared target entity | CANON, `:5374` |
| `source` | the entity that caused the triggering event | CANON, `:5374` |
| `subordinate` | an entity under the actor's command | CANON, `:5374`; no sim structure — **OPEN**, see §7 |
| `target_region` | region id from `params.target` | LIVE — `RxSkillSystem.h:113-125` resolves `region_id` then `region` |
| `entity_region` | the region containing a referenced entity | LIVE — `FRxTerrain::RegionAt`, `RxTerrain.h:130` |
| `anchor_region` | the boss's bound anchor region | LIVE — `RxBossEarthquake.h:74` |

`area`, `all_enemies`, `all_allies` appear in the source (`:5374`, `:5390`).
They are **deliberately excluded from v1** — an area or all-X reference requires
a defined, deterministic iteration order, and the determinism law
(`RxTypes.h:13`, `RxSimWorld.h:20-23`) requires *all* entity iteration to go
through `EntityOrder`. Admitting them without pinning that order is exactly the
kind of silent determinism hole that this system exists to prevent. **OPEN**,
§7 O-6.

### 2.2 Comparison operator

`cmp ∈ { lt, lte, gt, gte, eq, neq }` — CANON (`:5381`, which omits `neq`;
`neq` is **DERIVED**, and is redundant with `invert` — an implementation may
reject it).

### 2.3 Units (all integer)

| Unit | Meaning | Source |
|---|---|---|
| `stress` | region stress; threshold 1000 | `RxTypes.h:82` |
| `hp` | entity hit points; player/companion 1000 | `RxTypes.h:114-115` |
| `ticks` | 20 ticks = 1 second | `RxTypes.h:81` |
| `milli` | milli-units; 1 world unit = 1000 | `RxTypes.h:16` |
| `focus` | the one live resource; max 100 | `RxTypes.h:125` |
| `bp` | basis points, 0–10000 | DERIVED, §1 |

---

## 3. Authority classes

**DERIVED taxonomy, grounded in the existing tier model.**
`RxCommands.h:24-31` already defines command tiers T0–T3. An *effect* needs a
parallel classification, because the question "may a player-authored skill
contain this effect?" is not answerable from the command tier alone.

| Class | Writes | Rule |
|---|---|---|
| **A0 — Presentation** | Nothing in the canonical snapshot | Always legal. Never hashed. Grounded in `RxSimWorld.h:35-39`. |
| **A1 — Self** | Only the actor's own uncontested state (its resource, its cooldown, its own move target) | Legal in player-authored skills. |
| **A2 — Contested** | Shared simulation state (region stress, hp, anchors, flags) | Legal **only from the player-skill allow-list** (§5). When `actor == "companion"`, requires `approved: true` — `RxCommands.h:26-30`. |
| **A3 — World** | Simulation-internal world mutation | **Structurally unreachable from any command.** `RxCommands.h:31`: *"T3 world-mutation: sim-internal only, NEVER commandable (no type maps here)."* Never player- or model-authorable, at any authority tier, ever. |

This is the mechanism by which constraint #1 ("the engine is authoritative;
models propose, the Arena decides") is enforced at the *effect* level rather
than only at the command level. A model that proposes an A3 effect is not
rejected by policy — there is no command type that carries it.

---

## 4. Conditions — the closed set (11)

Envelope, adapted from CANON `:5371-5383`:

```
Condition:
  type:     <one of the 11 identifiers below>   # closed
  invert:   bool        # clean NOT                        (CANON :5382)
  required: bool        # false = soft, scores but does not gate legality (CANON :5383)
  params:   { ... }     # exactly the params for that type; integers only (DERIVED, §1)
```

The source's parameter block is a **union of every optional field**
(`threshold | status_id | tag | window_ticks | comparison`, `:5375-5381`).
That is not validatable — nothing states which fields are required for which
type. **DERIVED change: parameters are per-type and exact.** This is what makes
"registered enum value" (CANON hard-validation rule, `:5473`) mean something
checkable.

| # | Identifier | Plain meaning | Params (all integer / registry id) | Reads | Status |
|---|---|---|---|---|---|
| C1 | `region_stress` | How loaded a region is | `region: <region-ref>`, `cmp`, `value: int(stress)` | `FRxTerrain::StressOf` — `RxTerrain.h:122` | **LIVE** |
| C2 | `region_connected` | Whether force can travel between two regions | `region_a`, `region_b: <region-ref>` | `FRxTerrain::RegionsConnected` — `RxTerrain.h:142` | **LIVE** |
| C3 | `region_anchored` | Whether a region is being held stable | `region: <region-ref>` | `FRxRegion::AnchoredBy` — `RxTerrain.h:47` | **LIVE** |
| C4 | `entity_hp` | An entity's remaining health | `entity: <entity-ref>`, `cmp`, `value: int(hp)` | `FRxEntity::Hp` — `RxTypes.h:53` | **LIVE** |
| C5 | `entity_state` | An entity's current state name (incl. boss FSM state) | `entity: <entity-ref>`, `state_id: <registry>` | `FRxEntity::State` — `RxTypes.h:55`; states `RxTypes.h:150-163` | **LIVE** |
| C6 | `entity_distance` | How far apart two entities are | `entity_a`, `entity_b: <entity-ref>`, `cmp`, `value: int(milli)` | `FRxSimWorld::Idist` — `RxSimWorld.h:103` | **LIVE** |
| C7 | `resource_level` | How much of a resource the actor holds | `resource_id: <registry>`, `cmp`, `value: int` | `FRxSkillSystem::Focus` — `RxSkillSystem.h:183` | **LIVE** (only `focus` exists — §7 O-4) |
| C8 | `cooldown_ready` | Whether a skill is off cooldown | `skill_id: <registry>` | `FRxSkillSystem::Cooldowns` — `RxSkillSystem.h:182` | **LIVE** |
| C9 | `flag_set` | Whether a named world flag is set | `flag_id: <registry>` | `FRxSimWorld::GetFlag` — `RxSimWorld.h:98` | **LIVE** |
| C10 | `counter_threshold` | A named integer counter vs a threshold | `counter_id: <registry>`, `cmp`, `value: int` | see §4.1 | **PARTIAL** |
| C11 | `entity_status` | Stacks of a named status on an entity | `entity: <entity-ref>`, `status_id: <registry>`, `cmp`, `value: int(stacks)` | *no sim structure* | **SPECIFIED** |

### 4.0 ⚠ AUTHORITY REPAIR — the E11 writable allow-list (2026-08-03)

**The defect.** This registry was defined as a **read** mapping — the column header said
`Reads` and there was no write column at all. E11 `adjust_counter` is **A2,
player-authorable, allow-listed**, and took `counter_id: <registry>` unqualified. E11
therefore inherited *read scope as write scope*: a player-authored skill could name any
counter in the registry and mutate it, including `boss_stability`.

That is an **A3 world-mutation reachable through an A2 effect** — a T3 violation wearing
A2 clothing. Compare E15 `spawn_subordinate`, correctly marked **A3 / ❌ never**. E11 was
the same class of power with none of the gate.

**The repair — narrow, not general.** The registry now carries an explicit
**A2-writable** column. **Authority-owned is the default; writability is the exception and
must be named.** Exactly one counter is A2-writable:

`boss_release_delay` — because the strike-interrupt is canon: striking the anchor
mid-ACCUMULATE applies `adjust_counter{ boss_release_delay, +20 }`, which *is* the
companion's Tokenweave window (`RxBossEarthquake.cpp:181`, `RxTypes.h:105-106`). Removing
it would break shipped, intended play.

Every other counter is authority-owned and **not** writable by an A2 effect. Reading them
remains fully permitted via C10 — the boss must stay legible to the player, which is the
whole design. **Read is not write.** This registry conflated them and that was the hole.

**Enforcement requirement.** A registry table is documentation. The validator that admits
player-authored skills MUST reject an E11 whose `counter_id` is not A2-writable, and that
rejection needs a negative control: a skill attempting
`adjust_counter{ boss_stability, ... }` must FAIL admission, while
`adjust_counter{ boss_release_delay, +20 }` must still PASS. A gate that has never been
observed refusing is not a gate.

**Scope discipline.** This adds one column and one allow-list. It deliberately does NOT
introduce a general permission framework, capability tokens, or per-effect ACLs — the
defect was a missing write-scope on one registry, and the repair is that scope and
nothing more.

---

### 4.1 On `counter_threshold` (C10) — the deliberate generaliser

**DERIVED.** The source lists `prediction_failures` as its own condition type
(`:5271`, *"specifically for Ego Break style logic"*) and `custom_flag`
(`:5272`). Giving each named integer its own condition type is how a closed
enum stops being closed — the next doctrine needs `bait_taken_count`, and now
the vocabulary must grow.

One indirected type over a **registry of named counters** keeps the *vocabulary*
closed while letting the *registry* be governed data. Adding a counter is a data
change under §8; adding a condition type is a code change under §8.

Counters that exist in the sim today (**LIVE**):

| `counter_id` | Reads | **A2-writable via E11** | Cite |
|---|---|---|---|
| `boss_stability` | `FRxBossEarthquake::Stability` | ❌ **NO** — authority-owned | `RxBossEarthquake.h:75` |
| `boss_tremor_stage` | `FRxBossEarthquake::TremorStage` (0..3) | ❌ **NO** — authority-owned | `RxBossEarthquake.h:80` |
| `boss_release_delay` | `FRxBossEarthquake::ReleaseDelay` | ✅ **YES** — strike-interrupt (canon) | `RxBossEarthquake.h:78` |
| `boss_prev_anchor_stress` | `FRxBossEarthquake::PrevAnchorStress` | ❌ **NO** — authority-owned | `RxBossEarthquake.h:79` |
| `boss_state_ticks` | `FRxBossEarthquake::StateTicks` | ❌ **NO** — authority-owned | `RxBossEarthquake.h:73` |
| `world_tick` | `FRxSimWorld::Tick` | ❌ **NO** — authority-owned (sim clock) | `RxSimWorld.h:54` |

`prediction_failures` — the counter the source's flagship example depends on
(`:5422-5424`) — **has no producer anywhere in the sim.** Nothing counts a
prediction, because nothing records a prediction. This is **SPECIFIED**, and it
is the single largest gap between the source's worked examples and a buildable
system. §7, O-5.

---

## 5. Effects — the closed set (16)

Envelope, adapted from CANON `:5387-5404`:

```
Effect:
  type:     <one of the 16 identifiers below>   # closed
  params:   { ... }                             # per-type, exact, integers only
  stacking: { behavior: refresh|stack|ignore|strongest|independent|replace,
              max_stacks: int }                 # CANON :5398-5400, :5483-5492
  delivery: { delay_ticks: int, can_be_interrupted: bool }   # CANON :5401-5403
```

`delivery.delay_ticks` is directly supported: `FRxSimWorld` already carries a
tick-scheduled application queue (`FRxScheduledWave`, `RxSimWorld.h:151-158`)
with `ApplyTick`. Delayed effects reuse that mechanism rather than inventing a
second scheduler.

### 5.1 The effects

| # | Identifier | Plain meaning | Params | Writes | Auth | Player-authorable | Status |
|---|---|---|---|---|---|---|---|
| E1 | `add_stress` | Load a region (signed; may destabilise) | `region: <region-ref>`, `delta: int(stress, signed)` | `FRxTerrain::AddStress` — `RxTerrain.h:133` | A2 | ✅ allow-listed | **LIVE** |
| E2 | `dampen_stress` | Bleed stress off a region, clamped at 0 | `region: <region-ref>`, `amount: int(stress, ≥0)` | `FRxTerrain::Dampen` — `RxTerrain.h:136` | A2 | ✅ allow-listed | **LIVE** |
| E3 | `set_anchor` | Hold a region stable (drain + release immunity) | `region: <region-ref>`, `entity: <entity-ref> \| none` | `FRxTerrain::Anchor` — `RxTerrain.h:139` | A2 | ✅ allow-listed | **LIVE** |
| E4 | `queue_release` | Propagate a wave outward from a region | `origin: <region-ref>`, `force: int(stress)`, *(opt)* `decay_per_hop: int`, *(opt)* `hop_delay_ticks: int` | `FRxSimWorld::QueueRelease` — `RxSimWorld.h:110`; `FRxTerrain::ForceRelease` — `RxTerrain.h:151` | **A3** | ❌ never | **LIVE** |
| E5 | `modify_hp` | Damage or heal, clamped to `[0, MaxHp]` | `entity: <entity-ref>`, `delta: int(hp, signed)` | `FRxEntity::Hp` — `RxTypes.h:53` | A2 | ✅ allow-listed | **LIVE** |
| E6 | `set_move_target` | Send an entity toward a point | `entity: <entity-ref>`, `x: int(milli)`, `y: int(milli)` | `FRxEntity::MoveTarget` — `RxTypes.h:58-59` | A1 self / A2 other | ✅ self only | **LIVE** |
| E7 | `set_entity_state` | Change an entity's state / FSM phase | `entity: <entity-ref>`, `state_id: <registry>` | `FRxEntity::State` — `RxTypes.h:55` | **A3** | ❌ never | **LIVE** |
| E8 | `set_flag` | Set or clear a named world flag | `flag_id: <registry>`, `value: bool`, *(opt)* `duration_ticks: int` | `FRxSimWorld::SetFlag` — `RxSimWorld.h:99` | A2 | ✅ allow-listed | **PARTIAL** — base LIVE; `duration_ticks` needs a scheduled clear (reuse `RxSimWorld.h:151-158`) |
| E9 | `modify_resource` | Spend or restore a resource, clamped | `resource_id: <registry>`, `delta: int(signed)` | `FRxSkillSystem::Focus` — `RxSkillSystem.h:183` | A1 | ✅ | **LIVE** (only `focus`) |
| E10 | `set_cooldown` | Put a skill on cooldown | `skill_id: <registry>`, `ticks: int` | `FRxSkillSystem::Cooldowns` — `RxSkillSystem.h:182` | A1 | ✅ | **LIVE** |
| E11 | `adjust_counter` | Change a named integer counter | `counter_id: <registry>`, `delta: int(signed)` | see §4.1 | A2 | ✅ allow-listed | **PARTIAL** |
| E12 | `emit_signal` | Say / telegraph / show. Changes nothing. | `signal_id: <registry>`, `payload: { <registry-key>: int \| <registry-id> }` | `FRxSimWorld::Emit` — `RxSimWorld.h:120-121`; **excluded from snapshot & state hash**, `RxSimWorld.h:35-39` | **A0** | ✅ always | **LIVE** |
| E13 | `modify_status` | Add or remove stacks of a status | `entity: <entity-ref>`, `status_id: <registry>`, `stacks_delta: int(signed)`, `duration_ticks: int` | *no sim structure* | A2 | ✅ allow-listed | **SPECIFIED** |
| E14 | `suppress_intent` | Reduce an enemy's command bandwidth | `entity: <entity-ref>`, `magnitude_bp: int(bp)`, `duration_ticks: int` | *no sim structure* | A2 | ✅ allow-listed | **SPECIFIED** |
| E15 | `spawn_subordinate` | Bring a new entity into the world | `template_id: <registry>`, `x`, `y: int(milli)` | `FRxSimWorld::SpawnEntity` — `RxSimWorld.h:107` | **A3** | ❌ never | **SPECIFIED** — see §5.3 |
| E16 | `request_authority` | Ask a human for a higher tier. Never grants it. | `tier: int(0..4)` | nothing — emits a proposal | **A0** | ✅ always | **SPECIFIED** |

### 5.2 Consolidations from the source, and why

**DERIVED.** The source lists 14 effect names (`:5276-5290`). Four collapses,
each removing an entry without removing expressiveness:

- `deal_damage` + `heal` → **E5 `modify_hp`** with a signed delta. Two entries
  that differ only in sign are one entry.
- `apply_status` + `remove_status` → **E13 `modify_status`** with signed
  `stacks_delta`. Same argument.
- `set_flag` + `clear_flag` → **E8 `set_flag`** with a bool value. The source's
  own stacking matrix already treats them as one row (`:5491`).
- `trigger_combo_window` → **E8 `set_flag`** with `duration_ticks`. A combo
  window *is* a timed flag; the source's matrix gives it `refresh` stacking with
  max 1 (`:5492`), which is precisely timed-flag semantics.
- `taunt / dialogue` → **E12 `emit_signal`**. See §7, C-3 — this one is a
  contradiction, not merely a rename.
- `change_posture` → **E7 `set_entity_state`**, which is A3. Posture is an FSM
  state, and FSM states are world-authoritative.

### 5.3 `spawn_subordinate` is a determinism hazard — flagged

**DERIVED, and load-bearing.** `RxTypes.h:19` records that entity ids are
sequential and assigned in spawn order (ADR-0005), and `RxSimWorld.h:57-58`
carries `EntityOrder` as the determinism-law iteration order. Therefore
**spawning an entity shifts id allocation for every subsequent spawn**, which
changes the canonical snapshot, which changes every downstream receipt hash.

A player- or model-authored skill that spawns entities can therefore alter the
replay identity of a match. It is classified **A3 — never authorable**, and any
future proposal to relax that must ship with a replay-parity proof, not an
argument.

### 5.4 Stacking matrix

CANON, `:5483-5492`, carried forward verbatim and extended to the consolidated
names. `independent` and `replace` are the source's own terms from that table.

| Effect | Default stacking | Max stacks | Note (source) |
|---|---|---|---|
| `modify_status` (E13) | `refresh` | 1–5 | "Defined per status" |
| `modify_hp` (E5) | `independent` | — | "Always applies" |
| `modify_resource` (E9) | `stack` | 3 | "Diminishing returns after 2" |
| `suppress_intent` (E14) | `strongest` | 1 | "Highest magnitude wins" |
| `set_move_target` (E6) | `ignore` | 1 | "Last valid one applies" (source: `force_movement`) |
| `set_flag` (E8) | `replace` | 1 | "Binary" |
| `set_flag` w/ duration (E8) | `refresh` | 1 | "Window duration refreshes" (source: `trigger_combo_window`) |

Stacking for E1–E4, E7, E10, E11, E12, E15, E16 is **OPEN** — the source's
matrix does not cover them (§7, O-7).

---

## 6. The Earthquake encoding — the proof

Canon `RX_DESIGN_CANON_V1-SHENRON.md:88` requires the encounter to teach
*"precursor signals, unstable foundations, wave propagation,
distance/attenuation, anchoring, secondary failures, recovery windows,
aftershocks."* Canon `:96` gives the counterplay set: *"decouple, anchor,
dampen, relocate, or interrupt release."*

Below, each is encoded. **Every row uses only LIVE entries.** Constants are the
real ones from `RxTypes.h:80-133`; behaviour is `RxBossEarthquake.cpp` and
`RxTerrain.h`.

### 6.1 Hazard side — A3, engine-owned, not authorable

| Canon element | Encoding | Real values | Cite |
|---|---|---|---|
| **Telegraph** — "the boss announces the *concept*" | `emit_signal{ boss_telegraph, word: Earthquake }` on TAUNT entry | 40 ticks (2s) | `RxBossEarthquake.cpp:110-112`; `TAUNT_TICKS` `RxTypes.h:93` |
| **Unstable foundations** — stored stress | per tick in ACCUMULATE: `add_stress{ anchor_region, +3 }` | `ACCUMULATE_RATE=3` | `RxBossEarthquake.cpp:119`; `RxTypes.h:94` |
| **Precursor signals** | `region_stress{anchor, gte, S}` ∧ `counter_threshold{boss_tremor_stage, lte, i}` → `adjust_counter{boss_tremor_stage,+1}` + `emit_signal{tremor, pct, stress}` | S ∈ {250, 500, 750} = threshold /4, /2, ·3/4 | `RxBossEarthquake.cpp:121-135` |
| **Threshold crossing** | `region_stress{anchor, gte, 1000}` → `queue_release{anchor, force=S}` + `dampen_stress{anchor, S}` + `set_entity_state{boss, RELEASE}` | `STRESS_THRESHOLD=1000` | `RxBossEarthquake.cpp:150-154`; `RxTypes.h:82` |
| **Wave propagation** | inside `queue_release`: BFS over connected regions, edge-array order | — | `RxTerrain.h:24-27, 186-189` |
| **Distance / attenuation** | `queue_release.decay_per_hop`, `queue_release.hop_delay_ticks` | 250 force/hop; 10 ticks/hop | `RxTypes.h:83, 89` |
| **Damage on arrival** | `modify_hp{ entity_in_region, −(force / 10) }` (truncating) | `WAVE_DAMAGE_DIV=10` | `RxSimWorld.cpp:1052`; `RxTypes.h:116` |
| **Recovery window** | RELEASE → RECOVER, 80 ticks | `RELEASE_TICKS=10`, `RECOVER_TICKS=80` | `RxTypes.h:95-96` |
| **Aftershocks** | at RECOVER + 60: `queue_release{anchor, 300}` + `emit_signal{aftershock}` — a *forced sub-threshold* release through the identical wave path | `AFTERSHOCK_TICKS=60`, `AFTERSHOCK_FORCE=300` | `RxBossEarthquake.cpp:74-75`; `RxTerrain.h:32-34, 151` |

### 6.2 Counterplay side — A2/A1, player-authorable

| Canon counter (`:96`) | Encoding | Real values | Cite |
|---|---|---|---|
| **Decouple** | `region_connected{ my_region, anchor_region }` = false ⇒ the BFS never reaches you. *"Disconnected regions receive nothing (decouple = safe)."* | — | `RxTerrain.h:26-27` |
| **Anchor** | `set_anchor{ region, self }` — drains stress/tick **and** confers release immunity while held | `ANCHOR_DRAIN=5`/tick | `RxTerrain.h:31, 139`; `RxTypes.h:90` |
| **Dampen** (Tokenweave) | after 100 uninterrupted ticks: `dampen_stress{ region, 300 }` | `WEAVE_DURATION_TICKS=100`, `WEAVE_DAMPEN=300` | `RxTypes.h:109-110` |
| **Relocate** | `set_move_target{ self, x, y }` | `MOVE_SPEED=600` milli/tick | `RxTypes.h:113` |
| **Interrupt release** | strike the anchor mid-ACCUMULATE → `adjust_counter{ boss_release_delay, +20 }`. *This is the companion's Tokenweave window.* | `STRIKE_DELAY=20`, `STRIKE_DAMPEN=250` | `RxBossEarthquake.cpp:181`; `RxTypes.h:105-106` |

### 6.3 The authored skill — FAULTLINE INTERRUPT, encoded

This is the one skill the sim actually implements
(`RxSkillSystem.cpp:46-61` fixes the legal name/trigger/effect to single-element
lists). Encoding it in this vocabulary is the tightest possible check that the
vocabulary matches reality:

```yaml
skill_id: faultline_interrupt          # RxTypes.h:119
name: "FAULTLINE INTERRUPT"            # RxSkillSystem.cpp:48
derived_from: earthquake               # RxSkillSystem.cpp:248
enum_version: rx.skill-enums.v1

trigger:
  type: manual
  conditions:
    - { type: cooldown_ready,  required: true, params: { skill_id: faultline_interrupt } }
    - { type: resource_level,  required: true, params: { resource_id: focus, cmp: gte, value: 30 } }
    - { type: region_stress,   required: true, params: { region: target_region, cmp: gte, value: 400  } }
    - { type: region_stress,   required: true, params: { region: target_region, cmp: lt,  value: 1000 } }

cost:
  resource_id: focus
  amount: 30                           # SKILL_COST, RxTypes.h:120
  commitment_window_ticks: 20          # SKILL_COMMIT_WINDOW, RxTypes.h:122   ⚠ see §7 O-3

effects:
  - { type: modify_resource, params: { resource_id: focus, delta: -30 } }        # A1
  - { type: set_cooldown,    params: { skill_id: faultline_interrupt, ticks: 240 } }  # A1
  - { type: dampen_stress,   params: { region: target_region, amount: 600 } }    # A2

authority_required: validated_request_only   # RxSkillSystem.cpp:255
```

**The residual-risk branch is the interesting part.** `RxSkillSystem.h:202-204`
records that surface correctness is *deliberately not* checked by the gate:
firing on the wrong surface is **legal**, and backfires. Encoded:

```yaml
on_condition_failed:      # the two region_stress conditions above, marked required:false
  - { type: add_stress, params: { region: target_region, delta: +200 } }   # SKILL_WRONG_STRESS
  - { type: emit_signal, params: { signal_id: skill_backfire } }           # A0
```

This is what `required: false` (CANON `:5383` — *"soft condition (affects
scoring, not legality)"*) is actually for, and it is why the sim's window is
`[DAMP_CANCEL=400, STRESS_THRESHOLD=1000)`: too early and the charge has not
committed, too late and it has already released.
Cites: `RxSkillSystem.cpp:359-395`; `RxTypes.h:124`; `RxTypes.h:98`.

### 6.4 The secondary failure — the causal defeat path

Canon `:88` demands "secondary failures." This is the most important single
behaviour in the encounter, and it encodes exactly:

```yaml
# Boss's own charge backfires when its anchor is dampened below the cancel
# threshold after having been at or above it.
conditions:
  - { type: counter_threshold, params: { counter_id: boss_prev_anchor_stress, cmp: gte, value: 400 } }
  - { type: region_stress,     params: { region: anchor_region, cmp: lt, value: 400 } }
effects:
  - { type: dampen_stress,     params: { region: anchor_region, amount: <current stress> } }  # charge dissipates harmlessly
  - { type: set_entity_state,  params: { entity: boss, state_id: DESTABILIZED } }             # A3
  - { type: emit_signal,       params: { signal_id: boss_destabilized, window: 40 } }         # A0
```

During the 40-tick DESTABILIZED window, strikes deal ×3:
`modify_hp{ boss, −30 }` instead of `−10`
(`STRIKE_DAMAGE=10`, `STRIKE_MULT_DESTABILIZED=3`, `RxTypes.h:101-102`).
Stability 300 → 0 is **DEFEATED**.

`RxBossEarthquake.h:21-23` states the design consequence plainly: *"This is the
causal defeat path — destabilization of the boss's own anchor, NOT HP
depletion."* The vocabulary expresses it without a special case, because
"the boss's own stored stress" and "a region's stress" are the same quantity.

### 6.5 What the proof shows

1. **Complete.** All eight canon elements (`:88`) and all five canon counters
   (`:96`) encode. Nothing required a new entry.
2. **LIVE-only.** Zero SPECIFIED entries were needed. The four SPECIFIED effects
   (`modify_status`, `suppress_intent`, `spawn_subordinate`,
   `request_authority`) and one SPECIFIED condition (`entity_status`) are canon
   obligations for *later* content, not for the reference case.
3. **The authority split falls out.** The hazard half is A3 and unauthorable;
   the counterplay half is A1/A2 and authorable. A player can author *how to
   survive an earthquake*; a player can never author *an earthquake*. This is
   constraint #1 holding at the vocabulary level rather than by policy.
4. **Canon `:88`'s framing is preserved.** *"Earthquake is not a ground-damage
   attack. It is the controlled propagation of structural instability."* There
   is no `deal_damage_in_radius` primitive in this vocabulary. Damage is an
   emergent consequence of `queue_release` → arrival → `modify_hp{−force/10}`.
   You cannot express Earthquake as a damage attack in this grammar, which is
   the correct outcome.

---

## 7. Contradictions and OPEN items

### 7.1 Contradictions found — flagged, not silently resolved

**C-1 — "Not a skill factory" vs. a composable authoring grammar.**
Canon `:261`: *"decompression is deliberately narrow, not a skill factory."*
The sim agrees emphatically: `RxSkillSystem.h:18` (*"sockets exactly ONE
compiled fragment (NOT a skill factory)"*), `:20-21` (*"author_skill only ever
produces the fixed FAULTLINE_INTERRUPT artifact"*), and single-element legal
lists at `RxSkillSystem.cpp:46-61`.
But canon `:990-998` and `:1012-1014` describe a Creation Assistant that
composes arbitrary legal skills from multiple loot fragments.
**My reading (DERIVED, needs ratification):** these are different operations —
*decompression* (one fragment → one bounded skill, narrow, canon `:261`) versus
*synthesis* (multiple owned fragments → one composed skill, canon `:1014`). The
canon never states this distinction. **Not resolved here.**

**C-2 — `risk_level` float vs. float-free canonical JSON.** §1.4 above. Canon
`:992` states a 0.0–1.0 range; `RxCanonJson.h:13` forbids floats. Basis points
proposed, ratification required.

**C-3 — `taunt / dialogue` as a single effect type.** The source lists it as one
entry (`:5287`); canon carries it forward (`:994`). But it conflates two
different authority classes: *dialogue* writes nothing (A0), while *taunt* in
the aggro sense redirects targeting (A2, and requires a threat table that does
not exist). Split here into `emit_signal` (A0) with the aggro reading left
**OPEN**. Canon `:996` supports the split — *"coupled dialogue-mechanical
effects"* implies the mechanical part is a separate, composed effect.

**C-4 — the source contradicts itself on the skill encoding.** The same skill,
"Punish the Narrowed," appears twice in the source in **incompatible formats**:
a YAML v0.2 form at `:5416-5448` and a JSON `arena.skill.v1` form at
`:5764-5837`. They disagree on field names (`cost.amount` vs
`costs.resources`), on values (25 vs 20), on the authority value (`"tactical"`
vs `"delegated"`), and the JSON form drops `risk_level` entirely. Both ship
`"validation_hash": "sha256:placeholder_for_canonical_hash"` (`:5836`, `:5952`)
— i.e. neither was ever actually hashed. **Neither is adopted here.** v1 uses
the v0.2 envelope shape with per-type parameters (§4, §5). Which encoding is
authoritative is a founder call.

**C-5 — provenance correction.** The brief attributes *"tree topology, respec
rules, and socket grammar are underspecified"* to the source export. It is not
there. It is **canon**, `RX_DESIGN_CANON_V1-SHENRON.md:255`, restated at
`:1426` (Open Question #2). The claim is correct; the citation needed fixing.

### 7.2 OPEN — genuinely underspecified, not invented here

| # | Open item | Evidence |
|---|---|---|
| **O-1** | Knowledge Tree topology, socket grammar, respec rules | canon `:255`, `:1426` |
| **O-2** | Mapping from a domain-principle Compression Fragment (EARTHQUAKE) to the tactical taxonomy (`doctrine_fragment` / `skill_subgraph` / …). Canon states outright that it *"is not yet specified"* | canon `:1008`, `:1427` |
| **O-3** | **`commitment_window` has no consumer.** It is validated (`RxSkillSystem.cpp:201-204`), stored (`:253`), and hashed (`:159`) — but **nothing reads it**. `RxTypes.h:122` calls it a *"fixed artifact field"*. The source requires ≤ 180 ticks (`:5475`); the sim fixes it at 20. A committed-window semantic must be built before any skill can meaningfully carry one. | grep across `Sim/`: only writes, no reads |
| **O-4** | **Only one resource exists.** Canon `:992` lists `attention \| command \| stamina \| authority`; the sim has `Focus` alone (`RxSkillSystem.h:183`). C7/E9 are LIVE for `focus` and SPECIFIED for the rest. | — |
| **O-5** | **`prediction_failures` has no producer.** The source's flagship condition (`:5271`, `:5422`) requires counting predictions; nothing records a prediction. | §4.1 |
| **O-6** | `area` / `all_enemies` / `all_allies` references need a pinned deterministic iteration order before they can be admitted | `RxTypes.h:13`, `RxSimWorld.h:20-23`; source `:5390` |
| **O-7** | Stacking behaviour undefined for E1–E4, E7, E10–E12, E15, E16 | source matrix `:5483-5492` covers 7 rows only |
| **O-8** | Ranked sanctioning cost functions, decay rules, power budget | canon `:845-848`, `:1434`; source `:5519-5526` |
| **O-9** | Synthesis-cost curve and diminishing-returns rule | canon `:1433`; source `:5124` |
| **O-10** | Combo edge semantics (`requires` / `enables` / `window`) and the circular-dependency check (`:5477`) have no sim structure | — |
| **O-11** | The status/tag registry (`status_id`, `tag`) that hard-validation `:5474` requires to exist does not exist | — |
| **O-12** | `subordinate` reference has no sim structure (no command hierarchy) | §2.1 |

---

## 8. How the closed set gets extended — the governed process

**DERIVED, and mandatory.** Constraint #5 forbids the Creation Assistant from
inventing effect types (source `:5223`, `:5515`; canon `:1012`). A closed set
that has no defined way to grow is not closed — it is stalled. This is the way
it grows, and the assistant is not in it.

**Data-level change — adding a registry entry** (a counter id, flag id, status
id, resource id, signal id). Does *not* touch the vocabulary.
1. Add to the governed registry data file, with the sim state it binds to.
2. Re-run the parity oracle; the anchor hash must be unchanged for existing
   replays, or the change is a breaking one and escalates to the path below.
3. Record in the registry changelog with `path:line` for the sim binding.

**Vocabulary-level change — adding a Condition or Effect type.** Six gates, all
required:
1. **Proposal filed** by a human, naming the canon section that requires it and
   why no composition of existing entries suffices. (Canon `:996`:
   *"expressiveness comes from composition, not from open-ended effect soup"* —
   the burden of proof is on the new entry.)
2. **Authority class assigned** (§3) and justified. Anything touching contested
   or world state must be explicitly allow-listed for player-created skills —
   canon hard-validation rule, `:998` / source `:5478`.
3. **Sim implementation** landed with the state it reads or writes cited.
4. **Determinism proof**: parity oracle re-run against the Godot reference; if
   the anchor hash moves, the re-baseline is recorded *with provenance*, in the
   style of `RX_WORLDS_HANDOFF_AND_ROADMAP.md:32-37`. A green compile is not a
   pass (`RX_WORLDS_HANDOFF_AND_ROADMAP.md:169`).
5. **Adversarial suite** extended: at minimum an authority-violation case and an
   injection case proving the new entry cannot be reached from an unapproved
   envelope. The existing suite is 44/44
   (`RX_WORLDS_HANDOFF_AND_ROADMAP.md:30`).
6. **Founder gate.** Merge is a founder decision — canon `:1387`.

**Versioning.** The enum set is versioned as a unit (`rx.skill-enums.v1`). Every
authored skill pins the version it was validated against. A version bump never
silently revalidates existing skills; they either pin the old version or are
re-authored through the assistant with a fresh receipt.

**What the Creation Assistant may do when it hits the wall:** say that the
grammar cannot express the thing, and offer to file a proposal for a human to
carry. It may not approximate, may not compose a near-miss and call it
equivalent, and may not emit an unregistered identifier. Source `:5515`:
*"it cannot invent new effect types."*

---

## 9. Summary

- **11 Conditions, 16 Effects, 4 authority classes.**
- **17 of 27 entries are LIVE** against simulation code that exists today.
- **The EARTHQUAKE reference case encodes completely using LIVE entries only**
  (§6) — including its causal defeat path, its residual-risk backfire, and all
  five canon counters.
- **All values are integers**; ratios are basis points; the decision is forced
  by `RxCanonJson.h:13` and not merely preferred (§1).
- **Five contradictions** and **twelve OPEN items** are recorded rather than
  papered over (§7).
- Nothing here is implemented. Nothing here is canon. This is a draft for
  review.

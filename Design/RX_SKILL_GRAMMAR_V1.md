---
title: Rx Skill Grammar & Cognitive Loot — Canon Extraction v1
status: DRAFT FOR REVIEW — not canon, not merged, not implemented
author: extraction pass, 2026-08-02 (founder-authorized)
companion_doc: RX_SKILL_ENUMS_V1.md
source: 19f8304e-…_Grok-skills.md (raw conversation export), reconciled against
        RX_DESIGN_CANON_V1-SHENRON.md and Source/ReflexionArena/Sim/
---

# Rx Skill Grammar v1 — extracted canon

## 0. Provenance discipline

The primary source is a **raw conversation export, not a specification**: 5,984
lines in which design work is interleaved with unrelated conversational material
from the same session. Only the design content is carried forward here. Nothing
off-topic is quoted, summarised, or referenced.

Every claim below is marked:

| Mark | Meaning |
|---|---|
| **CANON** | Traceable to `RX_DESIGN_CANON_V1-SHENRON.md` or the source export, cited `path:line`. |
| **DERIVED** | My reasoned extension. Not in any source. Rejectable without breaking anything cited. |
| **OPEN** | Genuinely underspecified. Recorded, not resolved. |

Short paths used in citations:

- `SRC` = `/home/shax/Downloads/openclaw_workspace_game/19f8304e-2162-8a40-8000-0000bc2ff171_Grok-skills.md`
- `SRC2` = `/home/shax/Downloads/openclaw_workspace_game/19f8304d-bd42-8431-8000-0000843247df_Reflexion-Arena.md`
- `CANON` = `/home/shax/Desktop/Shax_Queue/RX_DESIGN_CANON_V1-SHENRON.md`
- `SIM` = `/home/shax/Projects/core-tech/Reflexion-Arena-UE/Source/ReflexionArena/Sim/`

> **The one warning that matters.** A stale roadmap in this project was read as
> live and cost real work. This document is a **draft extraction**, dated
> 2026-08-02, describing a system that **does not exist in code**. The only
> parts backed by running code are marked LIVE in `RX_SKILL_ENUMS_V1.md` §4–§5.
> Everything else is design. Check the date against the repo before trusting any
> claim here.

---

## 1. The thesis

**CANON.** *"Most games drop power. You're proposing to drop competence."*
(`SRC:5049-5051`) The drop is not a bigger number; it is
*"Here's how this mind solves the problem. Now you can too."* (`SRC:5063`)

This is the same claim the design canon makes as its category definition —
bosses drop *"a compressed, transferable model of a causal principle"*
(`CANON:251`) — arrived at from the opposite direction. The canon reaches it
from **domain principles** (EARTHQUAKE: trigger, propagation, counterplay,
residual risk, transfer domains — `CANON:93-99`). The source reaches it from
**tactical thinking** (how a Frontier Tyrant behaves under pressure).

**These two are not yet connected, and the canon says so outright**
(`CANON:1008`, `CANON:1427`). See §8, O-1. It is the single most important gap
in this document, and it is not filled here.

---

## 2. What actually drops — the four cognitive-loot kinds

**CANON**, `SRC:5057-5060`:

| Kind | Source text |
|---|---|
| **Doctrine fragments** | *"pieces of high-level tactical thinking"* |
| **`/skill` blueprints** | *"actual, usable skill-creation knowledge"* |
| **Combo scaffolds** | *"proven ability sequences the player can inspect, modify, and own"* |
| **Creation assistants** | *"bounded tools that help the player author new skills within the system's grammar"* |

### 2.1 A distinction the source blurs — flagged

**DERIVED, and important for anyone implementing this.** The four-item list
above is a *conceptual* list of what a player gains. It is **not** the schema
enum. The actual `type` enum in the loot record is **five** members
(`SRC:5187`):

```
type: "doctrine_fragment" | "skill_subgraph" | "combo_scaffold"
    | "constraint_example" | "failure_case"
```

Reconciling them:

- "doctrine fragments" → `doctrine_fragment` ✅ direct
- "`/skill` blueprints" → `skill_subgraph` — **renamed** between the prose and
  the schema
- "combo scaffolds" → `combo_scaffold` ✅ direct
- "creation assistants" → **not a loot record type at all.** It is a bounded
  *tool* (§5), not a droppable record. It has no `type` value and no content
  block.
- `constraint_example` and `failure_case` appear **only** in the schema, never
  in the four-item prose list.

Anyone who implements the four-item list as an enum will produce a schema that
cannot represent two of the five real content types and will model a tool as an
item. The canon carries the **five-member schema enum** as authoritative
(`CANON:1004`), and so does this document.

---

## 3. The loot record schema

**CANON**, `SRC:5179-5212`, with the integer changes from
`RX_SKILL_ENUMS_V1.md` §1 applied and marked.

```yaml
CognitiveLoot:
  id: string                    # stable identifier, e.g. "ft.the_misread"
  name: string
  source:                       # provenance — where it came from
    imprint: "FrontierTyrant" | "KimiIncursion" | "Swarm" | ...
    encounter_id: string
    difficulty_bp: int          # 0..10000   ← DERIVED: was `difficulty: 0.92`

  type: "doctrine_fragment" | "skill_subgraph" | "combo_scaffold"
      | "constraint_example" | "failure_case"

  content:                      # exactly ONE populated, keyed by `type`
    doctrine_fragment:  { insight: string, pattern: SkillGraph }
    skill_subgraph:     { nodes: [PartialSkill], edges: [ComboEdge] }
    combo_scaffold:     { sequence: [skill_id|tag], timing_notes: string,
                          success_conditions: [Condition] }
    constraint_example: { illegal_attempt: PartialSkill, why_rejected: string,
                          corrected_version: PartialSkill }
    failure_case:       { situation: string, what_went_wrong: string,
                          recovered_by: PartialSkill }

  teaching_value_bp: int        # 0..10000   ← DERIVED: was `teaching_value: 0.88`
  synthesis_cost: int           # already integer in source
  ranked_eligible: bool

  # --- DERIVED additions, required by existing system constraints ---
  enum_version: string          # which closed vocabulary this validates against
  loot_hash: string             # CanonJson SHA-256 of this record
  global_unique: bool           # CANON SRC:5658-5669 — "The Misread" is flagged unique
```

### 3.1 The worked example — `ft_ego_break_fragment_01`

**CANON**, `SRC:5227-5240`, reproduced with the integer conversion applied:

```yaml
id: "ft_ego_break_fragment_01"
name: "The Misread"
source:
  imprint: "FrontierTyrant"
  difficulty_bp: 9200                    # was 0.92
type: "doctrine_fragment"
content:
  doctrine_fragment:
    insight: >
      When an opponent has failed two consecutive predictions of your response,
      their doctrine narrows and they become unwilling to take the optimal
      retreat. This is the window.
    pattern:
      # partial graph: two-failure → narrowed-posture transition
teaching_value_bp: 8800                  # was 0.88
synthesis_cost: 12
ranked_eligible: true
```

The `pattern` field is **empty in the source** — the comment
*"# partial graph showing the two-failure → narrowed posture transition"*
(`SRC:5236-5237`) is all there is. The reference example's most load-bearing
field was never filled in. **OPEN**, §8 O-3.

Worse for implementers: the condition this fragment teaches —
`prediction_failures ≥ 2` — has **no producer in the simulation**. Nothing
counts predictions because nothing records them
(`RX_SKILL_ENUMS_V1.md` §4.1, §7 O-5). The canonical worked example of the
entire cognitive-loot system is not currently expressible.

### 3.2 The three launch packs

**CANON**, drafted `SRC:5300-5328`, finalised with dotted ids `SRC:5593-5618`,
carried into canon at `CANON:1006`. Teaching values shown in basis points.

**Swarm — "Pressure & Noise"** (`SRC:5594-5601`)

| id | Type | Teaching | Insight |
|---|---|---|---|
| `swarm.persistent_nipping` | doctrine_fragment | 5500 | Continuous low-commitment attacks force resource leaks |
| `swarm.bandwidth_tax` | skill_subgraph | 6200 | Skills that reduce enemy command rate over time |
| `swarm.sacrificial_probe` | combo_scaffold | 4800 | Cheap unit loss that reveals player response pattern |
| `swarm.noise_floor` | constraint_example | 4000 | Why pure spam eventually becomes readable |

**Champion — "Local Collapse"** (`SRC:5602-5609`)

| id | Type | Teaching | Insight |
|---|---|---|---|
| `champ.axis_removal` | doctrine_fragment | 7100 | Destroying one secondary threat collapses an entire plan |
| `champ.timing_window_steal` | skill_subgraph | 6800 | Interrupting a high-commitment skill mid-animation |
| `champ.controlled_sacrifice` | combo_scaffold | 7400 | Losing a unit on purpose to create a larger opening |
| `champ.over_extension_trap` | failure_case | 6500 | What happens when a Champion chases too far |

**Frontier Tyrant — "Commitment Predation"** (`SRC:5610-5618`)

| id | Type | Teaching | Insight |
|---|---|---|---|
| `ft.the_misread` | doctrine_fragment | 8800 | Two failed predictions → narrowed doctrine + punish window (**global unique**) |
| `ft.false_vulnerability` | skill_subgraph | 8400 | Deliberate opening that records the player's response |
| `ft.residual_collapse` | combo_scaffold | 9100 | Multi-axis punishment after the player commits |
| `ft.ego_fracture` | failure_case | 7900 | What the Tyrant looks like when its own prediction fails |
| `ft.temporal_bait` | doctrine_fragment | 8600 | Making the locally optimal move become globally wrong |

The source states these were written to
`docs/skill-system/COGNITIVE_LOOT_PACKS_v0.2.json` (`SRC:5658-5669`).
**That file's existence is not verified here** — canon `CANON:1402` likewise
states its *"implementation status in the canonical repos is not asserted by the
sources."* Treat as unproven.

### 3.3 Where the loot comes from — the encounter hierarchy

**CANON**, `SRC2:2089-2213` (duplicated at `SRC:3760-3860`). Rarity tracks model
capability and inference cost:

| Tier | Backing | Role |
|---|---|---|
| **Swarm** | fast local models | fodder, packs, ambient coordination — *"The horde is thinking together"* (`SRC2:2091-2108`) |
| **Champion** | dense local reasoning | elite squads, tactical subcommanders (`SRC2:2110-2128`) |
| **Boss** | frontier model | named bosses, faction commanders (`SRC2:2130-2155`) |
| **Rare / Ultra-rare** | flagship models | wandering tacticians; unique cosmetics, receipts, badges (`SRC2:2157-2180`) |
| **Mythic guest** | frontier systems | ~0.0001% world event, one global encounter (`SRC2:2182-2213`) |

The ruling: *"Reflexion Arena is a living bestiary of artificial intelligences…
Players do not merely fight stronger statistics — they encounter distinct
minds."* (`SRC2:2398`)

**Commercial caution — CANON and binding.** `SRC2:2346-2374` flags that using
company and model names as hostile monsters without permission *"could create
trademark and brand friction."* The mandated rollout is **Phase 1 fictionalised
classes** (Open Swarm, Dense Tactician, Frontier Tyrant, Long-Horizon Oracle,
Mythic Reasoner) internally powered by real models, then **Phase 2 opt-in
branded encounters** only once companies participate. Canon reinforces this at
`CANON:1368-1370` (§9.5 Model-Imprint Branding) and `CANON:1446` (Open Question
#22 — no provider avatar or mascot is authorized). **The launch packs above are correctly fictionalised
already** — no pack id names a real vendor.

---

## 4. The Skill Grammar

**CANON**, `SRC:5144-5170`, summarised into canon at `CANON:992`.

```yaml
Skill:
  id: string                    # unique, versioned
  name: string
  version: semver
  author: { type: "player"|"system"|"imprint", id: string }
  tags: [string]                # e.g. ["pressure","disengage","feint","commit"]

  trigger:
    type: "manual" | "auto" | "combo" | "reaction"
    conditions: [Condition]     # must ALL be true

  cost:
    resource: "attention"|"command"|"stamina"|"authority"   # ⚠ only `focus` exists — §8 O-5
    amount: int
    commitment_window: ticks    # how long the player is locked after use  ⚠ §8 O-4

  effects: [Effect]             # ORDERED list

  combo:
    requires: [string]          # skill ids or tags that must precede
    enables:  [string]          # skills this can lead into
    window: ticks

  authority_required: "none" | "tactical" | "doctrinal" | "emergency"
  risk_level_bp: int            # 0..10000  ← DERIVED: was `risk_level: 0.0–1.0`
```

`Condition` and `Effect` are closed enums, fully specified in the companion
document `RX_SKILL_ENUMS_V1.md` — that is the gap the source never filled
(`SRC:5172`, `SRC:5252`).

### 4.1 The design rule that governs everything

**CANON**, `SRC:5405`, elevated to canon at `CANON:996`:

> *"Both schemas are deliberately boring and strict. That's the point.
> Expressiveness comes from composition, not from open-ended effect soup."*

And the LLM-fit rule, **CANON** `SRC:5693-5696`:

> *"Every /skill should be a discrete, telegraphed, high-meaning program that
> the existing 3D character controller already knows how to perform beautifully.
> The LLM decides when and why. The controller decides how."*

Which skill families this admits (`SRC:5678-5684`): multi-phase commitment
skills, pattern-punish conditionals, feints and false openings, sacrifice
converters and bandwidth taxes, coupled dialogue-mechanical effects, doctrine
shifts. Explicitly **out** (`SRC:5686-5691`): frame-perfect execution,
continuous aim or movement vectors, sub-200ms reactions, and open-ended
"just do something cool" effects.

### 4.2 The four standards

**CANON**, `SRC:5337-5342`. A skill system is acceptable only if it is
**expressive** enough for distinct doctrines, **constrained** enough to stay
readable and fair in ranked, **fully simulatable** for deterministic replay, and
**teachable** so loot improves the player rather than adding buttons.

### 4.3 Hard validation

**CANON**, `SRC:5471-5478`, carried into canon at `CANON:998`:

1. Every Condition and Effect uses a **registered enum value**.
2. All referenced `status_id`, `tag`, `resource` values **exist in the game data
   registry**. *(⚠ that registry does not exist — §8 O-6.)*
3. `commitment_window` ≥ 0 and ≤ system max (**180 ticks**). *(⚠ the sim fixes
   it at 20 and never reads it — §8 O-4.)*
4. `authority_required` **cannot exceed** what the companion may request in the
   current mode.
5. **Circular combo dependencies rejected** at authoring time.
6. Any effect modifying simulation authority (damage, resources, win conditions)
   must be **explicitly allow-listed** for player-created skills.

**Soft validation** (warnings, not failures — `SRC:5479-5482`): very high
`risk_level` relative to cost; no clear counterplay window; long commitment
windows with low payoff.

### 4.4 The stacking matrix

**CANON**, `SRC:5483-5492`. Reproduced with consolidated effect names in
`RX_SKILL_ENUMS_V1.md` §5.4. Covers 7 effect types; the rest are **OPEN**.

---

## 5. The bounded creation-assistant contract

This is the constitutional core. **CANON**, `SRC:5216-5223` and `SRC:5510-5515`,
carried into canon at `CANON:1012`.

### 5.1 What it MAY do

1. **Show the player loot they actually own** (`SRC:5218`).
2. **Propose combinations that stay inside the Skill Grammar** (`SRC:5219`).
3. **Simulate the result against recorded enemy doctrines** (`SRC:5220`).
4. **Emit a full receipt of every suggestion and acceptance** (`SRC:5221`).
5. **Refuse anything violating authority, risk, or grammar rules** (`SRC:5222`).

Its critique may reference **only** the simulation results, owned cognitive
loot, and existing legal skills (`SRC:5511-5514`).

### 5.2 What it MAY NOT do

> **CANON, `SRC:5223`:** *"It cannot invent new effect types or bypass the
> schema."*
>
> **CANON, `SRC:5515`:** *"It may suggest tighter conditions, lower commitment,
> or better combo edges — but it cannot invent new effect types."*

### 5.3 Why the contract holds structurally, not by policy

**DERIVED**, and the point of the whole design. The assistant's boundedness is
not enforced by asking it nicely. Three independent mechanisms already in code:

1. **It cannot reach the world.** `SIM/RxSkillSystem.h:23-24`: *"the class NEVER
   mutates the world outside the command pipeline; execute() routes a validated
   'use_skill' command through World.Submit."* The only path to state is
   `FRxSimWorld::Submit`, which runs `FRxCommands::Validate` first
   (`SIM/RxCommands.h:176-178`).
2. **The validator is a closed vocabulary, not a filter.** `SIM/RxCommands.h:163-168`
   exposes fixed `Actors()` / `Types()` / `T0Types()` / `T1Types()` / `T2Types()`
   arrays. An unrecognised type is `ERR_UNKNOWN_TYPE` — there is no default
   branch to exploit.
3. **World mutation is not addressable.** `SIM/RxCommands.h:31`:
   *"T3 world-mutation: sim-internal only, NEVER commandable (no type maps
   here)."* No command carries it, so no proposal — from an assistant, a
   companion model, or an injected instruction — can request it.

`SIM/RxCommands.h:10` names this file *"the SECURITY-CRITICAL authority /
validation gate,"* ported 1:1 to reproduce *"EVERY rejection path… in the SAME
ORDER."* The adversarial suite covering it is 44/44
(`RX_WORLDS_HANDOFF_AND_ROADMAP.md:30`).

This satisfies constraint #1 exactly: **models propose, the Arena decides**
(`SRC:3900`, `SRC:3937`; `CANON:978-988`; `RX_WORLDS_HANDOFF_AND_ROADMAP.md:15`).

### 5.4 The sandbox loop

**CANON**, `SRC:5494-5518`, expanded `SRC:5621-5646`, canon `CANON:1012`:

1. **Authoring view** — player assembles conditions, effects, costs using
   **only owned loot + base grammar**.
2. **Instant static analysis** — grammar validation + balance heuristics.
3. **Doctrine battery** — the skill runs against *recorded* enemy doctrines
   (Swarm pressure, Champion tactics, Tyrant Read → Offer → Collapse → Ego
   Break).
4. **Metrics** — success rate per doctrine, average commitment risk, resource
   efficiency, follow-up-window creation, readability to enemy AI, Ego Break
   conversion rate.
5. **Assistant critique** — bounded per §5.1.
6. **Receipt** — *"every accepted suggestion and final skill version is written
   to an append-only receipt log tied to the player and companion"*
   (`SRC:5517`).

**DERIVED:** step 3 must run against *recorded* doctrines, never a live model.
A live opponent would make authoring non-deterministic and therefore
non-replayable, breaking the acceptance criterion *"the same seed and input
sequence reproduce the same authoritative result"* (`SRC:1323`).

---

## 6. How loot enters the game — validation, authority, ranked eligibility

### 6.1 The path

**CANON**, `SRC:3903-3908` / `SRC2:2268-2275`, and `CANON:980-986`:

```text
Model / skill → proposes action (request_action)
→ authority gate
→ CommandValidator
→ authoritative deterministic simulation
→ receipt
```

Arena — never the model — determines *"whether the action is legal; whether
resources exist; exact movement; hit resolution; cooldowns; damage; rewards;
replay state"* (`SRC2:2288-2297`). This preserves fairness, deterministic
replay, anti-cheat, API-outage tolerance, model substitution, bounded costs, and
auditability (`SRC2:2299-2307`).

> **CANON, `SRC2:2309`:** *"The model supplies **strategy and personality**, not
> physics."*

### 6.2 The live implementation of that path

**CANON (code).** The path is not aspirational — it exists:

| Stage | Implementation | Cite |
|---|---|---|
| Command vocabulary | `socket_fragment`, `author_skill`, `use_skill` are first-class command types | `SIM/RxTypes.h:183-184`, `:181` |
| Authority tiers | T0 observe / T1 move / T2 weave-skill-strike / T3 never-commandable | `SIM/RxCommands.h:24-31` |
| Companion gating | T2 commands **require `approved: true`** when `actor == "companion"` | `SIM/RxCommands.h:26-30` |
| Ordering law | structural → vocabulary → param shape → authority → world state | `SIM/RxCommands.h:18-22` |
| Rejection codes | `ERR_MALFORMED`, `ERR_AUTHORITY`, `ERR_UNKNOWN_TYPE`, `ERR_STATE` | `SIM/RxTypes.h:190-194` |
| Loot precondition | *"no fragment before boss defeat"* | `SIM/RxCommands.h:78` |
| Authoring precondition | *"authoring requires a socketed fragment"* | `SIM/RxCommands.h:79` |
| Receipt | `{seq, tick, cmd_hash, prev, result_code, state_hash}`, hash-chained, verifiable | `SIM/RxReceipts.h:11-13`, `:64` |

Those last two rows are the mechanical expression of the loop: **you cannot
author until you have defeated the boss and socketed what it dropped.** The
source's rule — *"This skill only becomes available after the player has
actually earned and synthesized the relevant cognitive loot. It is not a default
ability"* (`SRC:5450`) — is already enforced in code as an `ERR_STATE`.

### 6.3 Receipts and the single ledger

**CANON, and a standing directive.** `SRC:1254`:

> *"Align reward events with PentaCLI's existing exactly-once receipt and
> hash-chained ledger direction. **Do not invent a second progression ledger.**"*

Reinforced at `CANON:988` and `CANON:1018`. Acceptance criteria require
*"Reward issuance is exactly once"* (`SRC:1326`) and replay parity
(`SRC:1327`).

**DERIVED consequence:** cognitive-loot acquisition, synthesis spend, and skill
authorship are **reward events on the existing chain**, not a parallel
inventory system. `FRxReceipts` (`SIM/RxReceipts.h:49-68`) already provides
`Record` / `Verify` / `Snapshot` with a `GENESIS` seed; loot events extend that
chain rather than starting one.

### 6.4 Ranked eligibility

**CANON**, `SRC:5519-5526`, canon `CANON:845-848`:

| Aspect | Ranked League | Open / Exhibition |
|---|---|---|
| Player-created skills | Allowed **only if sanctioned or costed** | Fully allowed |
| Cost function | Risk + commitment + power budget | None or soft |
| Cognitive loot required | **Yes — must be earned** | Yes |
| Decay / maintenance | Skills **can lose** ranked eligibility | Permanent |
| Visibility | Public skill library + win rates | Optional |

Goal: *"keep ranked readable and skill-expressive without turning it into an
infinite complexity arms race"* (`SRC:5526`).

The actual cost functions, sanctioning process, and decay rules are **OPEN** —
canon lists this as Open Question #10 (`CANON:1434`). §8 O-8.

### 6.5 Injection containment

**CANON**, `CANON:1372-1374` (§9.6 Prompt-Injection Containment).
Adversarial/injection content is *"entirely
sandboxed: no real credentials, no external tools, no personal memory outside
match-approved data, no cross-player information leakage, no uncontrolled
network access, injected instructions represented as typed adversarial game
objects, all state transitions validated by the simulation, full replay and
provenance."*

> *"We dramatize compromise without creating an actual compromise surface."*

**DERIVED note:** cognitive loot is player-facing text (`insight`,
`timing_notes`, `why_rejected`) that originates from a model. It is therefore
untrusted content and must be treated as data, never as instructions to any
downstream agent. Canon has a dedicated section for this
(`CANON:1372-1374`, §9.6 Prompt-Injection Containment). §8 O-11.

---

## 7. Synthesis — the worked examples

**CANON**, `SRC:5407-5450` and `SRC:5738-5953`, canon `CANON:1014`.

Two reference syntheses, both requiring loot the player actually earned:

| Skill | Built from | Character |
|---|---|---|
| **"Punish the Narrowed"** | `ft.the_misread` + `ft.residual_collapse` + `champ.timing_window_steal` | Reactive punish, keyed on two prediction failures |
| **"Tempt and Record"** | `ft.false_vulnerability` + `ft.temporal_bait` + `swarm.sacrificial_probe` | Proactive bait that records the enemy's response |

They are deliberately complementary (`SRC:5964-5967`): one *creates* the
prediction failures, the other *punishes* them.

### 7.1 A defect in both examples — flagged

**Both ship an unhashed hash.** `SRC:5836` and `SRC:5952` both carry:

```json
"validation_hash": "sha256:placeholder_for_canonical_hash"
```

Neither example was ever actually canonicalised or hashed. Since the validation
hash is the artifact's identity, both examples are illustrative only.

The sim shows what the real thing looks like: `SIM/RxSkillSystem.cpp:259`
computes `SkillHash = FRxCanonJson::HashValue(Artifact.ToJson(false))` — hashing
the artifact **before** the hash field is added, then storing it. Any real
implementation must follow that ordering exactly or the hash will not reproduce.

### 7.2 A second defect — the source contradicts itself

**The same skill appears twice, in incompatible formats.** "Punish the Narrowed"
exists as YAML v0.2 (`SRC:5416-5448`) and as JSON `arena.skill.v1`
(`SRC:5764-5837`). They disagree on:

| | YAML (`:5416`) | JSON (`:5764`) |
|---|---|---|
| Cost field | `cost.amount: 25` | `costs.resources: 20` |
| Authority | `authority_required: "tactical"` | `authority_requirement: "delegated"` |
| Risk | `risk_level: 0.72` | *absent* |
| Effect naming | `type: suppress_intent` | `op: schedule_command` + `template` |
| Tag effects | `apply_status` | `apply_tag` / `set_tag` / `set_modifier` |

The `authority_required` vocabularies do not even overlap: the grammar defines
`none|tactical|doctrinal|emergency` (`SRC:5169`), while the JSON uses
`delegated` and `auto` (`SRC:5834`, `SRC:5950`). **Neither encoding is adopted
here.** Which is authoritative is a founder call. §8 O-12.

---

## 8. The OPEN list

Recorded, not resolved. **An honest OPEN list is the most valuable part of this
document.** Where canon already declares something open, that is cited — I am
not adding open questions the project has not earned.

| # | Open item | Status / evidence |
|---|---|---|
| **O-1** | **Fragment ↔ taxonomy mapping.** How a domain-principle Compression Fragment (EARTHQUAKE: trigger / propagation / counterplay / residual risk / transfer domains) maps to the tactical content taxonomy (`doctrine_fragment` / `skill_subgraph` / …). Canon states this is *"not yet specified"* | `CANON:1008`, Open Question #3 `CANON:1427` |
| **O-2** | **Knowledge Tree topology, socket grammar, respec rules.** Canon: *"Tree topology, respec rules, and socket grammar are underspecified in the sources"* | `CANON:255`, Open Question #2 `CANON:1426` |
| **O-3** | **The `pattern` field is empty.** The reference loot example's partial-graph payload is a comment, not data. `SkillGraph` / `PartialSkill` / `ComboEdge` are named but never defined anywhere | `SRC:5236-5237`, `SRC:5192-5196` |
| **O-4** | **`commitment_window` has no consumer.** Validated, stored and hashed, but nothing reads it. Source requires ≤180 ticks (`SRC:5475`); sim fixes it at 20 and calls it a *"fixed artifact field"* | `SIM/RxTypes.h:122`; `SIM/RxSkillSystem.cpp:201-204, 253, 159` |
| **O-5** | **Three of four resources do not exist.** Grammar declares `attention\|command\|stamina\|authority`; the sim has `Focus` alone | `SRC:5158`; `SIM/RxSkillSystem.h:183` |
| **O-6** | **The game data registry does not exist.** Hard-validation rule 2 requires every `status_id` / `tag` / `resource` to exist in it. There is no status system, no tag system, no threat table | `SRC:5474`; grep across `SIM/` |
| **O-7** | **`prediction_failures` has no producer.** The flagship condition of the flagship fragment cannot be evaluated | `SRC:5271, 5422`; `RX_SKILL_ENUMS_V1.md` §4.1 |
| **O-8** | **Ranked sanctioning.** Cost functions, sanctioning process, decay rules beyond the first-cut table | `CANON:1434` (Open Question #10) |
| **O-9** | **Synthesis-cost curve.** Costs and diminishing-returns curve, needed so *"the inventory becomes unmanageable"* does not happen | `SRC:5124`; `CANON:1433` (Open Question #9) |
| **O-10** | **Combo edges.** `requires` / `enables` / `window` semantics and the circular-dependency check have no sim structure | `SRC:5164-5167, 5477` |
| **O-11** | **Loot text is untrusted model output.** Containment posture for player-facing insight strings is named in canon but not specified | `CANON:1374` |
| **O-12** | **Two incompatible skill encodings** in the source; neither hashed | §7.1, §7.2 |
| **O-13** | **`COGNITIVE_LOOT_PACKS_v0.2.json` existence unverified.** Canon likewise declines to assert it | `SRC:5658-5669`; `CANON:1402` |

---

## 9. Contradictions against the design canon

Per instruction, flagged rather than silently resolved.

**C-1 — "Not a skill factory" vs. a composable authoring grammar.**
`CANON:261`: *"decompression is deliberately narrow, not a skill factory."*
The sim agrees emphatically — `SIM/RxSkillSystem.h:18` (*"sockets exactly ONE
compiled fragment (NOT a skill factory)"*), `:20-21` (*"author_skill only ever
produces the fixed FAULTLINE_INTERRUPT artifact"*), single-element legal lists
at `SIM/RxSkillSystem.cpp:46-61`. Yet `CANON:990-998` and `CANON:1012-1014`
describe an assistant composing arbitrary legal skills from multiple fragments.
**My reading (DERIVED, needs ratification):** *decompression* (one fragment →
one bounded skill) and *synthesis* (many owned fragments → one composed skill)
are different operations. The canon never says so. **Not resolved.**

**C-2 — `risk_level` is a canon-stated float that cannot be represented.**
`CANON:992` states *"risk_level (0.0–1.0, feeds ranked cost functions)"*.
`SIM/RxCanonJson.h:13` forbids floats in any hashed structure, and
`ERxJsonType` has no float variant. Basis points proposed
(`RX_SKILL_ENUMS_V1.md` §1); ratification required because this edits a range
the canon states in prose. Same applies to `difficulty` and `teaching_value`.

**C-3 — `taunt / dialogue` conflates two authority classes.** Listed as one
effect type (`SRC:5287`, `CANON:994`), but dialogue writes nothing while aggro
redirection writes contested state and needs a threat table that does not exist.
Split proposed in `RX_SKILL_ENUMS_V1.md` §5.2; the aggro reading left OPEN.

**C-4 — the source contradicts itself.** §7.2 above. Two incompatible encodings
of one skill, disjoint authority vocabularies, neither hashed.

**C-5 — provenance correction (not a contradiction, a citation fix).** The
quote *"tree topology, respec rules, and socket grammar are underspecified"*
does **not** appear in the source export. It is **canon**, `CANON:255`, restated
at `CANON:1426`. The claim is correct; the attribution needed fixing.

---

## 10. What is real today

**Verified against code, 2026-08-02.** The gap between this design and the
running system is large, and stating it precisely is the point:

**Real and proven:**
- The authority gate, with all rejection paths and ordering (`SIM/RxCommands.*`),
  adversarial 44/44.
- Hash-chained receipts with verification (`SIM/RxReceipts.*`).
- Canonical JSON + SHA-256, float-free by construction (`SIM/RxCanonJson.*`).
- The `socket_fragment` → `author_skill` → `use_skill` command chain, gated on
  boss defeat, integer-only, receipted, deterministic
  (anchor `b36ad6d0…` / 776 receipts, `RX_WORLDS_HANDOFF_AND_ROADMAP.md:31`).
- **Exactly one** authored skill: FAULTLINE INTERRUPT, from a single-element
  legal vocabulary.

**Design only, not built:** the general Skill Grammar, all cognitive-loot types
beyond the single Compression Fragment, the Creation Assistant, the sandbox
doctrine battery, the loot packs, ranked sanctioning, combo edges, statuses,
tags, and three of the four resources. Canon says the same at `CANON:1396`.

The honest summary: **the loop is proven end-to-end at width one.** One
fragment, one skill, one effect, one resource — but genuinely deterministic,
receipted and replayable. Everything in this document is the plan for widening
it, and none of it is built.

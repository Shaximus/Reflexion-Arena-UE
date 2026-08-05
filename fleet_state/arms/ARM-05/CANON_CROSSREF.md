# ARM-05 — Canon Cross-Reference: Which Gem Concepts Have No Implementation (Item 3)

- **Canon:** `RX_DESIGN_CANON_V1-SHENRON.md` @ `/home/shax/Desktop/Shax_Queue/`
  → symlink resolves to `/home/shax/Desktop/InboxV2/07_PROJECT_LANES/Reflexion_Multiverse/RX_DESIGN_CANON_V1-COMPLETED.md`
- **Measured size:** 1564 lines / 192,675 bytes
- **evidence_root:** `fleet_state/arms/ARM-05/evidence`

Every absence claim below names its **search surface** — the exact corpus searched and the
exact pattern. An absence claim without a search surface is an assumption, not a finding.

---

## 1. The headline correction: canon has no gameplay "Gem" concept

Exact tokenized count over the whole canon:

```
grep -oiE '\bgem[a-z]*' RX_DESIGN_CANON_V1-SHENRON.md | sort | uniq -c
      7 Gem
      1 Gemini
```

All 8 hits inspected individually. **Zero refer to a game item.**

| Line | What "Gem" actually means |
|---|---|
| `CANON:22` | Revision log — *"the deleted-Gem incident"*, v1.18 provenance |
| `CANON:502` | The **deleted-Gem incident**: a Google **Gemini Gem** (custom assistant config) that the platform deleted ~27 min after an anti-hedge instruction + Semantic Compiler payload |
| `CANON:522` | *"the artifact class that got the Gem killed"* — same incident |
| `CANON:1132` | `Gemini Live` — substring match on a TTS product name, not "Gem" |

Canon itself legislates against exactly this confusion:

> **`CANON:1020`** — *"Vocabulary disambiguation (Logos duty): **Doctrine Fragments** (a
> spendable training currency) and **Compression Fragments** (boss-dropped principles) are
> distinct concepts that happen to share a word. Canon documents must not abbreviate either
> to bare 'fragments' without qualification."*

**Search surface for this absence claim:** the full 1564-line canon, pattern
`\bgem[a-z]*`, case-insensitive, tokenized and counted (not sampled). Result: 8/8 hits
manually read. There is no fifth meaning hiding in the file.

### The three-way name collision

| Sense | Meaning | Where |
|---|---|---|
| Canon "Gem" | A deleted Google Gemini assistant config — an **incident**, not a mechanic | `CANON:22,502,522` |
| `semantic_compiler` "Gem" | A **Path of Exile** gem, used to translate into inference architecture | `expansion/gem_forge/models.py:14` (`PoeGem`) |
| This arm's title | "Gem Forge Gameplay Integration" | ARM-05 brief |

**Canon's actual gameplay progression noun is `Compression Fragment`** — 14+ occurrences,
with a dedicated section, a glossary entry, and a defined drop→socket→decompress loop:

- `CANON:249-251` §3.1 Compression Fragments — *"trigger, propagation, counterplay, residual
  risk, and transfer domains (EARTHQUAKE, §2.2, is the reference form)"*
- `CANON:50` the chain — *"Boss → lesson → compression fragment → knowledge tree → authored
  skill → companion doctrine → future mastery"*
- `CANON:80-81` *"receive the compression fragment → socket it into the knowledge tree →
  decompress it into future skills"*
- `CANON:1460` glossary — Compression Fragment
- `CANON:255,1476` Knowledge Tree — *"A PoE-style passive tree into which Compression
  Fragments socket"*

Note `CANON:255`: the **PoE analogy is canon** — but the canonical noun is *fragment
socketing into a passive tree*, never "gem".

---

## 2. Canon concepts with NO implementation

Search surfaces used throughout this section:

- **SURFACE-A** — `semantic_compiler` source: `find . -name '*.py'` excluding
  `.venv`/`.git`/`__pycache__` → **150 files** (measured), plus `__all__` introspection
  (42 exported names) and the executable probe.
- **SURFACE-B** — UE worktree: `grep -rniE <pat> --include='*.h' --include='*.cpp' Source/`
  over `Source/ReflexionArena/` (**5 root files + 15 files in `Sim/`**, measured).
- **SURFACE-C** — `evidence/negative_control.py`, which proves import-absence by exception.

| Canon concept | Canon anchor | Status | Search surface for the absence |
|---|---|---|---|
| **Compression Fragment** (as data) | `CANON:249-251` | **PARTIAL** — the *artifact* exists: `FRxFragmentSpec` with `Trigger`, `Propagation`, `Counterplay`, `ResidualRisk`, `TransferDomains` (`RxSkillSystem.h:57-70`), all five canon fields present | SURFACE-B, pattern `Fragment` |
| **Knowledge Tree / socket grammar** | `CANON:255,1426,1476` | **ABSENT — and canon says so itself**: *"Tree topology, respec rules, and socket grammar are **underspecified in the sources**"* (`CANON:255`); §11 open item 2 (`CANON:1426`) | SURFACE-A (`socket_fragment` → `ImportError`, SURFACE-C) + SURFACE-B: no `KnowledgeTree`/`Tree` type in `Sim/`. `SocketFragment` exists (`RxSkillSystem.h:191`) but sockets **exactly one** fragment into the skill system — *not* a tree |
| **Fragment decompression mechanics** | `CANON:1427` §11 open item 3 — *"Cost, boundedness rules, and failure modes"* | **ABSENT in engine; PARTIAL in Python** | SURFACE-B: `AuthorSkill` (`RxSkillSystem.cpp:174-202`) validates against a **one-element whitelist** — `LegalNames() = {"FAULTLINE INTERRUPT"}` (`:46-50`), `LegalTriggers() = {"committed_ground_propagation"}` (`:52-56`), `LegalEffects() = {"destabilize_anchor"}` (`:58-62`). This is a constant, not a decompressor. SURFACE-A: `decompress_skill` (`skill_decompression.py:80`) is a real general decompressor but has **no engine caller** |
| **Two players diverge on the same fragment** | `CANON:101,255` | **ABSENT** | SURFACE-B: no per-player fragment state; `FRxSkillSystem` holds one `Fragment` + one `AuthoredSkill` (`RxSkillSystem.h:180-181`). SURFACE-A: no divergence/expression model |
| **Wisdom graph (supersedes tree)** | `CANON:257` §3.9 | **ABSENT** | SURFACE-A + SURFACE-B, patterns `wisdom`, `graph` — no implementation in either repo |
| **Doctrine Fragments** (training currency) | `CANON:1020,1482` | **ABSENT** | SURFACE-A + SURFACE-B, pattern `doctrine` — no currency/spend system |
| **Cognitive-loot taxonomy → fragment mapping** | `CANON:1008` — *"the mapping … is **not yet specified**"* | **ABSENT, and canon-acknowledged** | `CANON:1008` states the gap; SURFACE-A/B confirm no code |
| **"No loot may produce direct paid statistical superiority"** | `CANON:224` | **ABSENT as an enforced rule** | SURFACE-B: no loot/economy validator in `Sim/`. Nearest available mechanism is the evidence-tier discipline (`measured.py:30-32`), currently unused by gameplay |
| **PoE gem corpus as game content** | — (not a canon concept) | **INVERTED** | SURFACE-A: `load_pinned_corpus` really loads **1017 PoE gems** (probe-measured). Canon never asks for this. It is inference tooling that the arm title mistook for content |

---

## 3. What canon says about implementation status generally

> **`CANON:1401`** — *"Everything in §2–§5 of this canon (Compression Fragments, Knowledge
> Tree, Tokenweave, Command Ascension Trials, Severed Gate) is **design canon, not
> implemented systems**. The Earthquake Proof (§8.3) is the build target that begins
> proving them."*

Canon **pre-declares** that the absences above are expected. They are not regressions. The
one canon-designated build target — the Earthquake Proof — is precisely what
`RxBossEarthquake.{h,cpp}` + `RxSkillSystem.{h,cpp}` implement.

This matches the arm's own scope anchor: *"Rx does not need twenty abilities to prove
itself, it needs one ability understood this deeply."* The engine already holds exactly one
ability. The gap is not breadth — it is that the **decompression step between fragment and
skill is currently a hardcoded constant**.

---

## 4. Premise failure in the ARM-05 brief (reported, not worked around)

The brief grants WRITABLE scope over `Source/RxWorlds/Gameplay` and `Source/RxWorlds/Gems`.

**Neither exists. `Source/RxWorlds` does not exist at any depth.**

```
find . -path ./.git -prune -o -iname '*RxWorlds*' -print   →  (no output), RC=0
ls Source/                                                 →  ReflexionArena/
                                                              ReflexionArena.Target.cs
                                                              ReflexionArenaEditor.Target.cs
```

The real source root is **`Source/ReflexionArena/`**, with sim code in
`Source/ReflexionArena/Sim/` (15 files). The only worktree artifact carrying the name is the
doc `RX_WORLDS_HANDOFF_AND_ROADMAP.md`.

Per the fleet standard — *"If a path, tool, or premise in your brief does not exist — STOP
AND REPORT IT. Do not work around it"* — **I did not create `Source/RxWorlds`, and did not
silently retarget my writable scope to `Source/ReflexionArena/`.** All four deliverables were
routed to `fleet_state/arms/ARM-05/`, which is in scope and does exist, so the bounded task
completed without needing the missing path. **Hannah Prime must re-issue the writable scope
before any ARM-05 code lands.**

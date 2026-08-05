# ARM-05 — Gem Primitive Inventory & Classification (Items 1 & 2)

- **Arm:** ARM-05, Gem Forge Gameplay Integration · reports to Hannah Prime (L3) · ceiling L2
- **Worktree (measured):** `/home/shax/.claude-squad/worktrees/arm/gem-gameplay-v1_18c8bb41238a0f60`
- **Branch / HEAD (measured):** `arm/gem-gameplay-v1` @ `c5fa2f8`
- **Read-only source (measured):** `/home/shax/Apps/semantic_compiler` @ `inference/gem-isomorphism-v1` / `12fef8f`
- **evidence_root:** `fleet_state/arms/ARM-05/evidence` (relative to worktree)
- **Method:** code read + executable probe. No claim below rests on the README.

---

## 0. The finding that governs everything else

**`semantic_compiler`'s "Gem" is not a game item. It is a Path-of-Exile-gem ↔
inference-architecture translation corpus.** The package translates PoE skill/support-gem
wording into LLM serving concepts (speculative decoding, KV cache, verifier acceptance)
and back. It contains **no gameplay runtime whatsoever**.

Evidence — the whole package's deployment vocabulary is infrastructure, not combat:

- `expansion/gem_forge/taxonomy.py:18-36` — `DEPLOYMENT_SLOTS` = `MODEL_PAYLOAD`, `DRAFT_HEAD`,
  `VERIFIER`, `SCHEDULER`, `ROUTER`, `KV/CACHE_LAYER`, `HARDWARE_ACCELERATOR` …
- `expansion/gem_forge/taxonomy.py:38-59` — `CAPABILITY_DOMAINS` = `SPECULATION`,
  `VERIFICATION`, `BATCHING`, `RESOURCE_BUDGETING`, `LATENCY_CONTROL` …
- `expansion/gem_decode/measured.py:68-69,194-199` — hardware receipts (PCIe round-trip,
  RTX PRO 6000) — this module measures a GPU rig, not a game.
- Negative control `evidence/negative_control.py` — `semantic_compiler.gameplay` does not
  exist (`ModuleNotFoundError`), nor `spawn_boss_encounter`, nor `socket_fragment`.

Consequence: **almost nothing here is "gameplay-existing."** The value is that the
*primitive vocabulary* and the *epistemic discipline* transfer — not the code.

---

## 1. Inventory (every gameplay-relevant primitive actually exposed)

Public API measured from `__all__` + probe: **42 exported names** in `gem_forge`
(`evidence/probe_primitives.stdout`).

### 1a. `expansion/gem_forge` — corpus, taxonomy, matching, synthesis

| Primitive | Signature | Location |
|---|---|---|
| `extract_primitives` | `(Iterable[str]) -> tuple[str, ...]` | `taxonomy.py:240` |
| `extract_domains` | `(Iterable[str]) -> tuple[str, ...]` | `taxonomy.py:245` |
| `canonical_primitive` / `canonical_primitives` | `(str)->str` / `(Iterable[str])->tuple` | `taxonomy.py:144,149` |
| `MECHANIC_PRIMITIVES` | 52-name const tuple | `taxonomy.py:61-114` |
| `DEPLOYMENT_SLOTS` / `CAPABILITY_DOMAINS` / `RELATIONSHIP_TYPES` | const tuples | `taxonomy.py:18,38,116` |
| `load_pinned_corpus` | `(...) -> pinned corpus` | `corpus.py:143` |
| `load_gem_corpus` / `load_gem_corpus_file` | `(Any,*,source)->tuple[PoeGem,...]` | `corpus.py:110,132` |
| `translate_gem` / `translate_corpus` | `(PoeGem)->GemTranslation` | `translator.py:480,530` |
| `match_component` | `(SoftwareComponent, translations, *, top_n=8, minimum_score=0.18)` | `forge.py:112` |
| `forge_component` | `(SoftwareComponent, translations, *, top_n=8) -> ForgeResult` | `forge.py:162` |
| `classify_line` / `build_family_registry` | line → converter family | `converter_families.py:454,477` |
| `attribute_composite` / `floor_violations` | per-primitive provenance | `composite_attribution.py:69,123` |
| `merge_sources` / `provenance_report` | multi-corpus merge, never averaged | `multi_source.py:235,308` |
| Records | `PoeGem`, `SoftwareComponent`, `LineTranslation`, `GemTranslation`, `GemMatch`, `SyntheticGem`, `ForgeResult` | `models.py:14,31,47,59,79,94,108` |

### 1b. `expansion/gem_decode` — build decoding + measured physics

| Primitive | Signature | Location |
|---|---|---|
| `decode_build` | `(spec_text, params=None) -> GemDecodeResult` | `gem_decode/__init__.py:45` |
| `parse_build_spec` | `(str) -> GemBuild` (raises `GemParseError`) | `parser.py:141` |
| `identify_archetypes` | `(GemBuild, translated) -> list[dict]` | `archetypes.py:315` |
| `run_cadence_checks` / `run_failure_family_checks` / `compute_verdict` | check engine | `checks.py:102,424,541` |
| `validate_gem_build` | `(dict) -> list[str]` (jsonschema Draft 2020-12) | `schema.py:19` |
| `lookup_component` | `(name, layer=None) -> ComponentEntry` | `ontology.py:495` |
| `translate_build` / `render_build_sheet` | build → sheet | `gem_decode/translator.py:64,95` |
| `fit_round_cost` / `optimum_k` / `claim_feasible` / `gate_verdict` | throughput law | `throughput_law.py:116,197,239,294` |
| `MeasuredRate` + `MEASURED`/`DERIVED`/`CLAIMED` tiers | evidence tiering | `measured.py:30-32,36` |
| `dense27b_map` / `map_row` / `coverage_report` | rig mapping | `rig_map.py:469,474,484` |

### 1c. `expansion/skill_decompression` — the one Rx-native module

| Primitive | Signature | Location |
|---|---|---|
| `decompress_skill` | `(text, *, source_receipt_id=None) -> SkillDecompressionResult` | `skill_decompression.py:80` |
| `validate_skill_shape` | `(dict) -> list[str]` | `skill_decompression.py:131` |
| `canonical_skill_hash` | `(dict) -> str` (`sha256:…`) | `skill_decompression.py:152` |
| `ALLOWED_TRIGGER_TYPES` / `ALLOWED_CONDITION_TYPES` / `ALLOWED_EFFECT_OPS` | closed vocabularies | `skill_decompression.py:18,19,20-29` |

Its docstring names the target directly: *"Rx Arena /skill decompression mode… design-time
compilation only: it never executes game effects and never grants authority"*
(`skill_decompression.py:1-7`).

---

## 2. Classification

Scale: **EXISTING** (usable as gameplay primitive today) · **EXISTING_WITH_DIFFERENT_NAME** ·
**COMPILABLE_TO_EXISTING_COMMANDS** (reducible to the shipped `use_skill` pipeline) ·
**REQUIRES_SMALL_EXTENSION** · **FUTURE_SYSTEM** · **REJECT**.

| # | Primitive | Class | Justification (file:line) |
|---|---|---|---|
| 1 | `decompress_skill` | **EXISTING_WITH_DIFFERENT_NAME** | Canon calls it *fragment decompression* (`CANON:1427`); code calls it skill decompression. Emits `authority_requirement: "validated_request_only"` (`skill_decompression.py:120`) — byte-identical to `FRxSkillArtifact::Authority` (`RxSkillSystem.h:100`). Same design lineage, different noun. |
| 2 | `validate_skill_shape` | **EXISTING_WITH_DIFFERENT_NAME** | Closed-vocabulary structural validation (`skill_decompression.py:131-149`) is the Python twin of `FRxSkillSystem::ValidateSpec` (`RxSkillSystem.cpp:174-202`). Both fail closed; both reject free-form. |
| 3 | `canonical_skill_hash` | **EXISTING_WITH_DIFFERENT_NAME** | `sha256` over sort-keyed JSON with hash field nulled (`skill_decompression.py:152-157`) — same construction as `FRxSkillArtifact::SkillHash` hashed *before* adding `skill_hash` (`RxSkillSystem.h:104-108`). |
| 4 | `ALLOWED_EFFECT_OPS` (8 ops) | **COMPILABLE_TO_EXISTING_COMMANDS** | `request_action` carries `authority_gate: "required"` (`skill_decompression.py:76`); the shipped path routes a validated `use_skill` through `World.Submit` and never mutates directly (`RxSkillSystem.h:210-213`). The other 7 ops are tag/signal writes with no engine sink yet. |
| 5 | `MECHANIC_PRIMITIVES` (52) | **COMPILABLE_TO_EXISTING_COMMANDS** (partial) | `taxonomy.py:61-114`. `APPLY_COOLDOWN`/`RECOVER_COOLDOWN` → `Cooldowns` map (`RxSkillSystem.h:182`); `RESERVE_CAPACITY`/`CONVERT_RESOURCE` → `Focus`/`SkillCost` (`h:160-166`); `ROLLBACK_ON_FAILURE`/`RECORD_RECEIPT` → `BuildTransferReceipt` (`h:226`). **Most of the 52 have no gameplay sink** — see row 12. |
| 6 | `extract_primitives` | **REQUIRES_SMALL_EXTENSION** | Deterministic substring matcher, no ML (`taxonomy.py:240-242`). Works today (probe returned 4 primitives) but its phrase table is PoE/inference wording (`taxonomy.py:155-206`); needs an Rx-fragment phrase table. |
| 7 | `SoftwareComponent` / `forge_component` | **REQUIRES_SMALL_EXTENSION** | `forge.py:162`. Runs end-to-end (probe: 8 matches, 1017-gem corpus). But its input record is an *inference component* (`models.py:31-40`: `deployment_slots`, `capability_domains`) — needs an `FRxFragmentSpec`-shaped input to be gameplay-facing. |
| 8 | Matcher floor `_floor_status` | **EXISTING** — adopt as-is | `forge.py:130-150`. Demotes to `NOVEL` rather than force-fitting a composite; probe confirmed live demotion (`composition=NOVEL`). This is a **transferable discipline primitive**, not just code. |
| 9 | Evidence tiers `MEASURED`/`DERIVED`/`CLAIMED` | **EXISTING** — adopt as-is | `measured.py:30-32`. Directly serves the canon rule that no loot grants unearned advantage; a gameplay stat must carry its tier. |
| 10 | `validate_gem_build` (jsonschema) | **REQUIRES_SMALL_EXTENSION** | `schema.py:19-25` validates `reflexion.gem_build.v1`. Schema is inference-shaped; the pattern (hard schema gate, raise on invalid — `gem_decode/__init__.py:84-86`) transfers directly. |
| 11 | `load_pinned_corpus` + `CorpusPinError` | **FUTURE_SYSTEM** | `corpus.py:143`, `:26`. Hash-pinned 1017-gem PoE corpus (probe measured `container_len=1017`). Real and working, but it pins **Path of Exile** gems — not Rx content. Useful only if Rx ships a pinned fragment corpus. |
| 12 | `translate_gem` / `translate_corpus` / 27 converters | **REJECT** for gameplay | `translator.py:480,530`, converters `:31-425`. These convert PoE wording → *inference* wording ("Supported Inference predicts additional candidate Token Positions", `forge.py:195`). Output is an LLM-architecture sentence. No gameplay consumer. |
| 13 | `gem_decode.decode_build` + `checks.py` | **REJECT** for gameplay | `gem_decode/__init__.py:45`; `checks.py:102-541`. Decodes a build into an **inference build sheet**; probe returned `verdict=UNRESOLVED`. Its "checks" are GPU cadence checks. |
| 14 | `throughput_law.*` | **REJECT** for gameplay | `throughput_law.py:116-294`. Speculative-decoding round-cost physics. Probe returned a real fitted model (`t_fixed=20.44ms`, `t_step=2.14ms`, RTX PRO 6000). Genuine engineering value — **zero** gameplay relevance. |
| 15 | `rig_map.*` | **REJECT** for gameplay | `rig_map.py:469-484`. Probe: 28 playbook gems, 28 covered, 7 `NO_ANALOGUE`. Maps gem names → **GPU rig configs**. |
| 16 | `multi_source` / `composite_attribution` | **FUTURE_SYSTEM** | `multi_source.py:235`, `composite_attribution.py:69`. Per-source provenance, never averaged. Relevant only when Rx has multiple fragment sources to reconcile. |

**Tally:** EXISTING 2 · EXISTING_WITH_DIFFERENT_NAME 3 · COMPILABLE 2 ·
REQUIRES_SMALL_EXTENSION 4 · FUTURE_SYSTEM 2 · REJECT 4.

**The honest headline: 4 of 16 primitive groups are rejected outright for gameplay, and the
single largest body of code in the package (`gem_decode`, ~120 KB) is in that rejected set.**

---

## 3. Executable evidence

| Artifact | Result |
|---|---|
| `evidence/probe_primitives.py` → `.stdout` / `.stderr` / `.exit` | **8 PASS / 0 FAIL, exit 0**, stderr empty |
| `evidence/negative_control.py` → `.stdout` / `.stderr` / `.exit` | **exit 0 = 3 expected FAILs**, known-good still PASS |

The negative control is what licenses the PASS column: the harness was shown failing on
three absent primitives (`socket_fragment`, `spawn_boss_encounter`,
`semantic_compiler.gameplay`) in the same run where a known-good primitive still passed.
Without it, 8/8 PASS would prove only that the harness prints "PASS".

Probe command (exit code captured, stderr captured to file, never suppressed):

```
PYTHONPATH=/home/shax /home/shax/Apps/semantic_compiler/.venv/bin/python \
  fleet_state/arms/ARM-05/evidence/probe_primitives.py \
  > .../probe_primitives.stdout 2> .../probe_primitives.stderr; echo "EXIT=$?"
```

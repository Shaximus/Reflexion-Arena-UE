# Rx Gameplay Contract — **Fragment Decompression v1**

- **Contract id:** `rx.fragment_decompression.v1`
- **Author:** ARM-05 (Gem Forge Gameplay Integration) · **Consumer:** ARM-04 (Content, Config)
- **Status:** DRAFT — proposed, not implemented. No code was written for this contract.
- **Canon anchors:** `CANON:1427` (§11 open item 3), `CANON:249-251` (§3.1), `CANON:80-81`, `CANON:1401`
- **evidence_root:** `fleet_state/arms/ARM-05/evidence`

---

## 1. Why this primitive and not another

Of 16 primitive groups inventoried (`INVENTORY_AND_CLASSIFICATION.md`), **fragment
decompression** is the single highest-value one:

1. **Canon names it as an open gap, explicitly.** `CANON:1427` — *"Fragment decompression
   mechanics. Cost, boundedness rules, and failure modes for turning a Compression Fragment
   into 'one bounded /skill'."* Canon is asking for exactly this.
2. **It is the one load-bearing step that is currently a constant.** `AuthorSkill` validates
   against a **one-element whitelist** — `LegalNames() = {"FAULTLINE INTERRUPT"}`
   (`RxSkillSystem.cpp:46-50`), `LegalTriggers()` (`:52-56`), `LegalEffects()` (`:58-62`).
   The fragment is socketed (`RxSkillSystem.h:191`) but **its content never influences what
   skill you may author.** Any fragment yields the same skill.
3. **The two halves already speak the same language.** `decompress_skill` emits
   `authority_requirement: "validated_request_only"` (`skill_decompression.py:120`); the
   engine artifact declares `Authority` = `"validated_request_only"`
   (`RxSkillSystem.h:100`, `Data/skill_faultline_interrupt.json`). Same hash discipline too
   (`skill_decompression.py:152-157` ≈ `RxSkillSystem.h:104-108`).
4. **The data file is already ahead of the code.** `Data/skill_faultline_interrupt.json`
   already carries a `legal_options` object; `RxSkillSystem.cpp` ignores it and returns
   hardcoded statics. **The seam already exists — this contract only asks that it be honored.**
5. **It fits the scope anchor.** *"Rx does not need twenty abilities… it needs one ability
   understood this deeply."* This contract adds **zero** new abilities. It makes the one
   existing ability *derived from its fragment* instead of *hardcoded beside it*.

---

## 2. What the contract does

```
FRxFragmentSpec  ──decompress──▶  FRxLegalOptions  ──AuthorSkill──▶  FRxSkillArtifact
 (already exists)                  (NEW, this contract)               (already exists)
 RxSkillSystem.h:57-70                                                RxSkillSystem.h:88-110
```

Decompression turns a socketed fragment into the **bounded choice-list** that `AuthorSkill`
validates against. It does **not** author, execute, or grant authority.

**Invariant D0 — decompression is a pure function of the fragment.**
`decompress(fragment) -> legal_options` depends on nothing but `FRxFragmentSpec` content.
No RNG, no clock, no world state. Integer math only (house rule, `RxSkillSystem.h:27`).

**Invariant D1 — the option set is closed and finite.** Output is a fixed-size set of
enumerated strings. Never free-form. This preserves CONTRACTS.md §2 "no free-form".

**Invariant D2 — fail closed.** A fragment that decompresses to an empty option set yields
**no authorable skill**, not a default one. Absence of a legal option is a hard `ERR_STATE`.

**Invariant D3 — the fragment hash binds the option set.** `legal_options` carries the
`fragment_hash` it was derived from (`Data/fragment_earthquake.json` →
`7b45e77f…3cf2d9`). An artifact authored under option-set *A* is invalid against a fragment
whose hash is not *A*'s. This is what stops option-set forgery.

**Invariant D4 — decompression never widens authority.** Every derived effect inherits
`authority: "validated_request_only"`. Decompression may narrow, never widen. No derived
option may bypass `World.Submit` (`RxSkillSystem.h:210-213`).

**Invariant D5 — v1 is behavior-preserving.** For `fragment_earthquake.json`, decompression
MUST reproduce exactly today's three lists. **This contract must land as a no-op on the
existing replay hashes.** If replay parity breaks, the contract is wrong, not the hashes.

---

## 3. Data shape — what ARM-04 authors

### 3.1 Fragment (input) — already exists, unchanged

`Data/fragment_earthquake.json`. All five canon fields present (`CANON:251`):
`trigger`, `propagation`, `counterplay`, `residual_risk`, `transfer_domains`, plus
`compiler` provenance and `fragment_hash`. **ARM-04 changes nothing here.**

### 3.2 Decompression table (NEW) — `Data/decompression_earthquake.json`

The bounded mapping. Proposed shape:

```json
{
  "$schema": "rx.fragment_decompression.v1",
  "fragment_hash": "7b45e77f7f61fa0f18a0c3e763cf1bc59a53f34f679af7c66722f2121e3cf2d9",
  "derived_from": "earthquake",
  "decompression_cost": { "resource": "focus", "amount": 0 },
  "legal_options": {
    "name":    ["FAULTLINE INTERRUPT"],
    "trigger": ["committed_ground_propagation"],
    "effect":  ["destabilize_anchor"]
  },
  "option_provenance": {
    "committed_ground_propagation": {
      "from_field": "propagation",
      "fragment_text": "Connected surfaces transmit disruptive force.",
      "expression": "disruption"
    },
    "destabilize_anchor": {
      "from_field": "counterplay",
      "fragment_text": "Decouple, anchor, dampen, relocate, or interrupt release.",
      "expression": "disruption"
    }
  },
  "residual_risk": "wrong surface id amplifies local instability (+200 stress on wrong region)",
  "authority": "validated_request_only",
  "table_hash": "<sha256 of this object with table_hash nulled>"
}
```

**`option_provenance` is the load-bearing field.** It is what makes this a *decompression*
rather than a second hardcoded list: every legal option must cite the fragment field and the
verbatim fragment text it was derived from. An option with no provenance entry is invalid.

Note `counterplay` already enumerates five verbs — *"Decouple, anchor, dampen, relocate, or
interrupt release."* v1 exposes exactly one (`destabilize_anchor`, from *anchor*). The other
four are the natural v2 expansion path **and the mechanism by which `CANON:101`'s "two
players diverge on the same fragment" becomes real** — without inventing new content.

### 3.3 Config keys (NEW) — ARM-04's Config scope

| Key | Type | v1 value | Meaning |
|---|---|---|---|
| `rx.decompression.enabled` | bool | `false` | Master switch. **Ships false** — hardcoded statics remain authoritative until parity is proven. |
| `rx.decompression.table_dir` | string | `Data/` | Where decompression tables load from. |
| `rx.decompression.require_provenance` | bool | `true` | Reject any option lacking an `option_provenance` entry. |
| `rx.decompression.fail_closed` | bool | `true` | Empty option set → `ERR_STATE`. **Must never be false in shipped config.** |

---

## 4. Engine surface (ARM-05 would implement; NOT written)

```cpp
struct FRxLegalOptions
{
    bool bValid = false;
    FString FragmentHash;                       // binds to FRxFragmentSpec::FragmentHash
    TArray<FString> Names, Triggers, Effects;
    TMap<FString, FRxOptionProvenance> Provenance;
    FString TableHash;
    FRxJsonValue ToJson() const;                // feeds Snapshot() / state_hash
};

// Pure. No world access. Returns bValid=false on any violation.
static FRxLegalOptions FRxSkillSystem::Decompress(const FRxFragmentSpec& Fragment,
                                                  const FRxDecompressionTable& Table);
```

`ValidateSpec` changes from consulting `LegalNames()` statics
(`RxSkillSystem.cpp:176,181,186`) to consulting the decompressed `FRxLegalOptions` —
**with identical error codes and identical `ERR_STATE` detail strings**, so existing
negative tests keep passing unchanged.

---

## 5. Failure modes (canon `CANON:1427` asks for these by name)

| Code | Condition | Result |
|---|---|---|
| `ERR_STATE` | No fragment socketed | No decompression. Unchanged from today. |
| `ERR_DECOMP_HASH` | `table.fragment_hash` ≠ socketed `fragment_hash` | Fail closed. Option-set forgery blocked (D3). |
| `ERR_DECOMP_EMPTY` | Any of name/trigger/effect decompresses empty | Fail closed — **no default skill** (D2). |
| `ERR_DECOMP_PROVENANCE` | Option lacks `option_provenance` entry | Fail closed when `require_provenance=true`. |
| `ERR_DECOMP_AUTHORITY` | Table declares authority ≠ `validated_request_only` | Fail closed (D4). Widening is never legal. |
| `ERR_STATE` | Spec value not in decompressed options | Unchanged message/code — replay-compatible. |

---

## 6. Acceptance criteria (pre-declared — do not move)

| # | Gate | Threshold, declared before any run |
|---|---|---|
| A1 | Replay parity | `Decompress(fragment_earthquake)` yields option lists **byte-identical** to `LegalNames/Triggers/Effects()`. Any diff = fail. |
| A2 | State-hash parity | With `enabled=true`, the Earthquake Proof replay produces the **same `state_hash`** as with `enabled=false`. Any diff = fail. |
| A3 | Negative control — hash forgery | Table with a mutated `fragment_hash` **must** yield `ERR_DECOMP_HASH`. If it authors a skill, the gate is fake. |
| A4 | Negative control — empty set | Table with `"effect": []` **must** yield `ERR_DECOMP_EMPTY`, not a default. |
| A5 | Negative control — authority widening | Table with `authority: "direct_mutation"` **must** yield `ERR_DECOMP_AUTHORITY`. |
| A6 | Provenance completeness | Every legal option has an `option_provenance` entry citing a real fragment field. |
| A7 | Purity | `Decompress` called twice on identical inputs returns identical `TableHash`. No RNG, no clock. |

**A3–A5 are the ones that matter.** A1/A2 only prove the change is invisible; A3–A5 prove the
gate can actually refuse. Per the fleet standard: a check that cannot fail proves nothing.

---

## 7. Honest limitations

- **Nothing here is implemented or measured.** This is a design contract. Every number in it
  is quoted from existing code or data, not from a run of this contract.
- **v1 is deliberately a no-op.** It adds no ability and changes no player-visible behavior.
  Its entire value is converting a hardcoded constant into a provenanced, hash-bound
  derivation — the precondition for `CANON:101` divergence and `CANON:1426` socket grammar.
- **`decompress_skill` (`skill_decompression.py:80`) is NOT proposed as the implementation.**
  It is Python, design-time, and its `_infer_effects` is substring heuristics
  (`skill_decompression.py:64-77`) — non-deterministic across wording changes and unfit for a
  replay-hashed engine path. It is the **vocabulary donor and reference semantics**, not the
  code. Adopting it directly would violate D0.
- **Knowledge Tree remains out of scope** and canon-underspecified (`CANON:255,1426`). This
  contract deliberately stops at the option set and does not invent tree topology.
- **The `decompression_cost` field is present but zeroed.** Canon asks for decompression cost
  (`CANON:1427`); I have no basis to set a number, and inventing one would be fabrication.
  Flagged as an open question for Hannah Prime / design, not silently defaulted.

---

## 8. What ARM-04 is being asked for

1. Author `Data/decompression_earthquake.json` per §3.2 — copying `legal_options` verbatim
   from `Data/skill_faultline_interrupt.json` and filling `option_provenance` from
   `Data/fragment_earthquake.json`.
2. Add the four Config keys in §3.3, **shipping `rx.decompression.enabled = false`.**
3. Author three negative-control fixtures alongside the existing pattern in `Data/tests/`
   (which already holds `arena_truncated.json`, `arena_missing_key.json`,
   `arena_nonintegral_coord.json`): a hash-mismatch table, an empty-effect table, and an
   authority-widening table — satisfying A3, A4, A5.

No Content or Config file was modified by ARM-05. This document is a request, not a change.

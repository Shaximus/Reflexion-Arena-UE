# Rx Data Boundary Contract v1

**Status:** LOCKED for implementation (P0.3). Authored by the integrator before fan-out.
**Purpose:** make the sim data-driven so arenas/universes become swappable assets — the
prerequisite for Rx Worlds (handoff §5, item 1).
**Prime constraint:** `final_state_hash = b36ad6d028e1b5452629df480c537adc7ce85e1b4b5b0fab4c95067506261cfa`,
`receipt_count = 776`, final `tick=11901 player_hp=840 companion_hp=900 boss_stability=0`,
adversarial 44/44. **These MUST NOT change.** A green compile is not a pass; the oracle diff is the gate.

> ⚠️ **Anchor superseded 2026-08-02.** This document previously asserted
> `b976626216a03282…` as the prime constraint. That value was re-baselined when the canon-6
> transfer domains were restored. If you find the old hash asserted as a *live* constraint
> anywhere, that document predates 2026-08-02 and is stale.
>
> **Known limitation of this gate (measured 2026-08-02):** the hash chain verifies
> determinism and integrity, **not behavioural correctness**. A trajectory delivering 150 of
> 300 expected damage passed both determinism and chain verification. The oracle diff is
> necessary and not sufficient — behavioural assertions must be checked separately.

---

## 1. The problem being fixed

`FRxEncounters::LoadArena()` / `LoadFragment()` / `LoadSkillTemplate()` transcribe
`arena_earthquake.json`, `fragment_earthquake.json`, `skill_faultline_interrupt.json`
inline as C++ literals. The sim therefore has exactly one arena, welded in at compile
time. Rx Worlds is impossible in that shape.

## 2. The boundary

Split *where data comes from* away from *what the sim does with it*.

```
  on-disk JSON  ──►  FRxDataSource (parse + intify + validate)  ──►  FRxArenaConfig
                                                                          │
                                                                          ▼
                                                        FRxEncounters::BuildArena(World, Cfg)
```

The sim core never opens a file. `BuildArena` becomes a pure function of its config.

### 2.1 Signature changes

```cpp
// NEW — config is injected. This is the data-driven boundary.
static void FRxEncounters::BuildArena(FRxSimWorld& World, const FRxArenaConfig& Cfg);

// RETAINED as a convenience overload that uses the default baked config, so existing
// callers (RxOracleCommandlet) keep working unchanged during the transition:
static void FRxEncounters::BuildArena(FRxSimWorld& World);
```

Same treatment for fragment and skill template: the specs become inputs, not
hardwired returns. Where a spec is consumed deep inside the sim, thread it from
world setup rather than calling a static loader at the point of use.

### 2.2 New unit: `Sim/RxDataSource.h/.cpp`

```cpp
class FRxDataSource
{
public:
    // Each returns false and fills OutError on ANY problem. No silent defaults, ever.
    static bool LoadArenaFromFile(const FString& Path, FRxArenaConfig& Out, FString& OutError);
    static bool LoadFragmentFromFile(const FString& Path, FRxFragmentSpec& Out, FString& OutError);
    static bool LoadSkillTemplateFromFile(const FString& Path, FRxSkillSpec& Out, FString& OutError);
};
```

## 3. The intify law (THE critical correctness rule)

The Godot reference runs every parsed JSON value through `CanonJson.intify()` before
it reaches the sim, because the sim is integer-only. UE's JSON parser yields `double`.

**Rule:** every simulation-affecting number MUST be converted to `int32` via an exact
round-trip check. If a value is not exactly integral, the load FAILS LOUDLY:

```cpp
static bool ExactInt(double D, int32& Out, FString& OutError)
{
    if (D != FMath::TruncToDouble(D)) { /* fail: non-integral */ }
    if (D < INT32_MIN || D > INT32_MAX) { /* fail: out of range */ }
    Out = static_cast<int32>(D);
    return true;
}
```

A float sneaking into the sim is exactly the class of bug that silently breaks
determinism months later. Fail at load, not at hash-compare.

## 4. Where the data lives

Copy the three JSON files from the Godot reference into `Reflexion-Arena-UE/Data/`
so the UE project is self-contained. **Do not modify the Godot originals** — they are
the parity ground truth (handoff §8, "Preserve the Godot reference").

Record provenance: a `Data/PROVENANCE.json` mapping each copied file to the SHA-256 of
its Godot original, so drift between the two repos is detectable rather than silent.
This mirrors the project's own Provenance Constitution — the data gets receipts too.

## 5. Proof obligations (all four required)

This is the gate. Implementation is not done until every one passes.

1. **Loader fidelity.** `LoadArenaFromFile(arena_earthquake.json)` must equal the
   existing baked config **field by field** — regions (id/name/kind/poly/stress/
   stable/anchored_by), edge array *in order*, stress schedule *in order*, all three
   spawns, boss anchor/stability/arena regions, transfer region. Same for fragment
   (including the vendored-SHA map and the fragment hash string) and skill template.
   Keep the baked version as `LoadArenaBaked()` **specifically to be the comparand**.
   Add this as an oracle check that prints PASS/FAIL per field group.

2. **Hash invariance.** Full oracle run with the arena loaded FROM DISK still yields
   `b976…` / 776 receipts / identical final state / 44/44 adversarial.

3. **Malformed input rejected precisely.** Feed a truncated file, a file with a
   non-integral coordinate (e.g. `2000.5`), and a file missing a required key. Each
   must fail with a specific error naming the offending field — never a silent
   default, never a partial load.

4. **Ordering is load-bearing.** Region order, edge order, stress-schedule order and
   spawn order all determine entity ids and iteration order. The loader must preserve
   JSON document order exactly. Do NOT sort, do NOT use an unordered container
   anywhere in the load path.

## 6. Explicitly out of scope for P0.3

- The universe-manifest schema layer (that is the Rx Worlds lane, `Schemas/`).
- Any behavior change. This refactor is *semantics-preserving by definition*: if the
  hash moves, the change is wrong.
- Hot-reload, cooking/packaging, or `Content/` asset integration.

## 7. Why this shape

`BuildArena(World, Cfg)` is what makes a second universe possible without touching sim
code: Rx Worlds supplies a different `FRxArenaConfig` from a different manifest, and the
deterministic core is none the wiser. The DENY-by-default cross-universe gate then sits
at the *data source* layer, where it can be audited, rather than being tangled into
encounter setup.

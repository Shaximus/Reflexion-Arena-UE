#!/usr/bin/env python3
"""
behavioural_oracle.py — MINIMUM behavioural oracle.

Head of Operations product order, item 3, 2026-08-03.
Discrimination pass by ARM-06, 2026-08-04 — see WHAT IT IS AND IS NOT, below.

WHY THIS EXISTS
---------------
Everything the acceptance gate verifies is INTEGRITY and DETERMINISM:

    final_state_hash · receipt_count · stream_command_count
    receipt_chain_head · fragment_hash

Those prove a run is *reproducible* and *untampered*. They cannot say WHAT went wrong.

Recorded in the data-boundary contract: **a trajectory delivering 150 of 300 expected
damage passed BOTH determinism and chain verification.** The hash was stable, the chain
verified, and the sim was wrong.

    A deterministic wrong answer is still wrong.
    A hash proves you got the same answer twice, not that it was the right one.

WHAT IT IS AND IS NOT — corrected characterisation, ARM-06 2026-08-04
---------------------------------------------------------------------
**This oracle does not detect defects the integrity hash misses. It cannot.** Any defect
that moves end state also moves `final_state_hash`, so the hash strictly dominates this
file as a DETECTOR. Measured, not assumed: under every negative control in the discrimination
matrix, `state_hash` diverged from the anchor — including two controls this oracle passes.

What the hash cannot do is tell you WHICH quantity moved and BY HOW MUCH. `b36ad6d0… !=
704d92ca…` is a fingerprint mismatch; it does not distinguish halved wave damage from a
reordered receipt payload. This file exists for **diagnostic specificity**:

    hash    ->  "this run is not the canonical run"
    oracle  ->  "player_hp is 922, expected 840, OFF BY 82"

Claiming more than that for this file would be an overclaim, and the checklist forbids it.

A worked example of the boundary, measured 2026-08-04: halving `SKILL_DAMPEN` or
`STRIKE_DAMPEN` changes `receipts_head` and therefore `state_hash`, while every END-STATE
behavioural quantity stays identical to canonical. This oracle passes those runs.

**CORRECTED 2026-08-04, same day, by per-tick measurement — the sentence that used to
follow said this was "the design, not a gap". That was wrong, and it was my own claim.**
Comparing a per-tick behavioural digest (entity HP, all ten region stresses, boss state,
stability, release delay, tremor stage — receipts excluded) shows both mutations DO diverge
behaviourally mid-run and then RECONVERGE:

    STRIKE_DAMPEN halved   diverges tick 3426, reconverged by 3656 —  231 ticks apart
    SKILL_DAMPEN  halved   diverges tick 6902, reconverged by 7218 —  317 ticks apart
                           first divergence: region stress 14 -> 241

So this oracle is not passing them because nothing behavioural happened. It is passing them
because it only ever looks at the last tick, and the difference had healed by then. That is
a REAL BLIND WINDOW of roughly 300 ticks, not a clean division of labour.

End-state assertions are structurally blind to any defect that reconverges. Closing that
would mean sampling the trajectory, not the end state — which is beyond "minimum" and is
NOT done here. It is recorded so the limitation is known rather than discovered later.
Evidence: fleet_state/arms/ARM-06/I_per_tick/ in Reflexion-Arena-UE.

DESIGN CONSTRAINTS
------------------
1. **It must be able to fail.** Every retained assertion ships with a demonstrated mutation
   that breaks it. An oracle never observed refusing is not an oracle.
2. **Integer-only.** `RxCanonJson` permits ints/strings/bools/arrays/dicts — never float,
   never null. Tolerances are absolute integers, not percentages.
3. **It does not touch the hash.** This reads final state and receipts; it emits nothing
   into the canonical snapshot, so the anchor is unaffected. Verified, not assumed.
4. **Minimum means minimum.** Kestrel ordered a *minimum* behavioural oracle. This is not
   a general assertion framework, a property-based fuzzer, or a coverage tool.

DISCRIMINATION MATRIX — every retained assertion, and the control that breaks it
--------------------------------------------------------------------------------
Measured by ARM-06 2026-08-04 against an isolated sandbox copy of the sim mirror.
`fails_under` names controls; a mutation of the SIM, never of this file.

    assertion        fails under SIM-RULE controls        fails under FIXTURE control
    final_tick       none                                 N6 script truncated
    player_hp        N1 wave/2, N2 wave x2, N3 strike/2   N6
    companion_hp     N1, N2, N3                           N6
    boss_stability   N3                                   N6
    total_hp_lost    N1 (428), N2 (820), N3 (2045)        N6      <- was NONE before repair
    outcome_flags    N3                                   N6

N6 breaks EVERY assertion, so it proves an assertion is live but says nothing about its
specificity. Only the sim-rule column is evidence of discrimination. Read that column.

`final_tick` is a RUN-COMPLETENESS guard, not a behavioural assertion: the replay loop runs
until the scripted agent is exhausted, so the final tick is pinned by the fixture and is
unmoved by every sim-rule mutation tested. It fails only when the run does not complete.
Labelled honestly rather than dropped, because "the whole script replayed" is worth
asserting — but it must not be counted as behavioural coverage.

`outcome_flags` is RETAINED but REDUNDANT: it fails only under N3, where `boss_stability`
already fails. The cycle-00 note that it "passes by confirming its own warning text" is too
strong — it was observed failing (all three flags False under N3). It is kept as a cheap
cross-check with its redundancy stated, not as independent evidence.
"""

from __future__ import annotations

import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "semantic_kernel"))

ARENA = ROOT / "game" / "data" / "arena_earthquake.json"
SCRIPT = ROOT / "game" / "data" / "acceptance_run_v1.json"

# ── Behavioural expectations ────────────────────────────────────────────────
# Absolute integer tolerances. A tolerance of 0 means EXACT — used where the value is
# a determinate consequence of a deterministic run, not an estimate.
#
# total_hp_lost was `1 <= dmg <= 10_000_000` until 2026-08-04. That bound was ~17,857x the
# canonical value on the high side and could not fail: measured across five negative
# controls it passed at 428 (halved wave damage) and at 2045 (halved strike damage, 3.65x
# canonical) — including the exact 150-of-300 class of defect it was written to catch.
# A check that cannot fail proves nothing. It is exact now, on the same grounds as the
# others: this is a deterministic replay of a fixed script, so the total is determinate.
EXPECT = {
    "final_tick":        (11901, 0),
    "player_hp":         (840,   0),
    "companion_hp":      (900,   0),
    "boss_stability":    (0,     0),
    "total_hp_lost":     (560,   0),
}


def run() -> tuple[dict, object]:
    import sim_mirror as sm
    script = json.loads(SCRIPT.read_text())
    world = sm.SimWorld(int(script.get("seed", 7)))
    sm.Encounters.build_arena(world, str(ARENA))
    agent = sm.PlayerAgent(script)
    while not agent.exhausted(world):
        agent.feed(world)
        world.step()
    return script, world


def total_hp_lost(world) -> int:
    """Cumulative HP removed across all entities: sum(max_hp - hp).

    MEASURED FINDING, 2026-08-03 — and it explains the blind spot exactly.
    Receipts contain NO magnitudes. A receipt is:
        {seq, tick, cmd_hash, prev, result_code, state_hash}
    Hashes and result codes. Nothing numeric to compare against.

    That is *structurally* why a trajectory delivering 150 of 300 damage passed chain
    verification: the chain records THAT commands occurred and THAT state advanced. It
    holds no quantity a magnitude defect could contradict. The integrity layer is not
    weakly behavioural — it is not behavioural at all.

    So magnitudes must come from final world state. This is the aggregate that a halved
    or doubled damage rule necessarily moves, and end-state HP alone can hide (a survivor
    at full HP tells you nothing about how much was dealt and healed).
    """
    total = 0
    for e in world.entities.values():
        mx, hp = int(e.get("max_hp", 0)), int(e.get("hp", 0))
        if mx > 0:
            total += max(0, mx - hp)
    return total


def check(name: str, ok: bool, detail: str) -> bool:
    print(f"  {'PASS' if ok else '*** FAIL ***':14s} {name:24s} {detail}")
    return ok


def main() -> int:
    script, world = run()
    print("BEHAVIOURAL ORACLE — magnitudes, not fingerprints")
    print("Diagnostic specificity: names WHICH quantity moved. Detection is the hash's job.\n")

    actual = {
        "final_tick":     int(world.tick),
        "player_hp":      int(_entity_hp(world, "player")),
        "companion_hp":   int(_entity_hp(world, "companion")),
        "boss_stability": int(_boss_stability(world)),
        "total_hp_lost":  total_hp_lost(world),
    }

    ok = True
    for field, (expected, tol) in EXPECT.items():
        got = actual[field]
        within = abs(got - expected) <= tol
        ok &= check(field, within,
                    f"expected {expected} (±{tol}), got {got}"
                    + ("" if within else f"  <-- OFF BY {got - expected}"))

    # Binary flags kept, but explicitly labelled as redundant with boss_stability.
    flags = {k: world.flags.get(k) for k in
             ("boss_defeated", "transfer_success", "receipt_emitted")}
    ok &= check("outcome_flags", all(v is True for v in flags.values()),
                f"{flags} — REDUNDANT: observed failing only under N3, where "
                f"boss_stability already fails. Cheap cross-check, not independent evidence.")

    print(f"\nRESULT: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def _entity_hp(world, which: str) -> int:
    """world.entities is a dict keyed by int id; world exposes player_id/companion_id."""
    eid = getattr(world, f"{which}_id", None)
    if eid is None:
        return -1
    e = world.entities.get(eid)
    return int(e.get("hp", -1)) if isinstance(e, dict) else -1


def _boss_stability(world) -> int:
    boss = getattr(world, "boss", None)
    for attr in ("stability", "Stability"):
        v = getattr(boss, attr, None)
        if isinstance(v, int):
            return v
    eid = getattr(world, "boss_id", None)
    if eid is not None:
        e = world.entities.get(eid)
        if isinstance(e, dict):
            props = e.get("props", {})
            for k in ("stability", "boss_stability"):
                if isinstance(props.get(k), int):
                    return props[k]
    return -1


if __name__ == "__main__":
    sys.exit(main())

"""K-03B bounded trajectory audit — a MANDATORY step in normal verification.

Head 06 ruling 02 (in reply to CKPT-03):

    "The audit does not need to be embedded inside behavioural_oracle.py, but it
     must become a mandatory, normally reached verification step:
         normal verification invocation
           -> endpoint behavioural oracle
           -> K-03B bounded trajectory audit
           -> combined claim disposition"

Living in Design/tests/ is what makes it normally reached: run_tests.py globs
test_*.py, so a plain suite run now traverses the trajectory audit as well as
the endpoint checks. Nothing needed to be wired by hand, and no generalized
tracing system was built — that was explicitly not authorized.

WHY IT EXISTS. The shipped behavioural oracle returns exit 0 on a run where a
protected authority field was mutated and restored (measured, CHECKPOINT-21).
Endpoint verification is blind to any defect that reconverges before the last
tick. This audits the trajectory instead.

BOUNDED, per the ruling — four fields, one canary, nothing more:

    boss_stability      protected, authority-owned
    world_tick          protected, the sim clock
    boss_release_delay  the ONE canon-writable counter, must stay observable
    counter_threshold   ABSENT / NOT TESTED — C10 has no implementation on any
                        surface. Reported as absent, never as passing.

Exit codes:  0 = canonical clean AND canary caught AND writable observable
                 AND trace deterministic
             1 = an audit condition failed
             2 = cannot evaluate (sim mirror unavailable) — never a pass

The mirror is located via RX_ORACLE_DIR, defaulting to the path below.
"""
import hashlib
import json
import os
import pathlib
import sys

MIRROR = pathlib.Path(os.environ.get(
    "RX_ORACLE_DIR", "/home/shax/Projects/core-tech/Reflexion-Arena/tools/oracle"))
GAME_DATA = MIRROR.parent.parent / "game" / "data"

PROTECTED = ("boss_stability", "world_tick")
WRITABLE = "boss_release_delay"
CANARY_MUTATE_TICK = 3500
CANARY_RESTORE_TICK = 3560


def cannot_evaluate(reason):
    print(f"  CANNOT EVALUATE: {reason}")
    print("\n  exit 2 — the trajectory audit did not run. This is NOT a pass.")
    sys.exit(2)


def load():
    if not MIRROR.is_dir():
        cannot_evaluate(f"sim mirror not found: {MIRROR} (set RX_ORACLE_DIR)")
    if not (GAME_DATA / "acceptance_run_v1.json").is_file():
        cannot_evaluate(f"acceptance fixture not found under {GAME_DATA}")
    sys.path.insert(0, str(MIRROR))
    sys.path.insert(0, str(MIRROR / "semantic_kernel"))
    try:
        import sim_mirror as sm
    except Exception as e:                                   # noqa: BLE001
        cannot_evaluate(f"could not import sim_mirror from {MIRROR}: {e!r}")
    return sm


def sample(w):
    b = getattr(w, "boss", None)
    return {"world_tick": int(w.tick),
            "boss_stability": int(getattr(b, "stability", -1)),
            "boss_release_delay": int(getattr(b, "release_delay", -1))}


def run(sm, canary):
    script = json.loads((GAME_DATA / "acceptance_run_v1.json").read_text())
    w = sm.SimWorld(int(script.get("seed", 7)))
    sm.Encounters.build_arena(w, str(GAME_DATA / "arena_earthquake.json"))
    agent = sm.PlayerAgent(script)
    trace, stash = [sample(w)], {}
    while not agent.exhausted(w):
        agent.feed(w)
        w.step()
        if canary:
            b = getattr(w, "boss", None)
            t = int(w.tick)
            if b is not None and t == CANARY_MUTATE_TICK:
                stash["s"] = int(b.stability)
                b.stability = int(b.stability) + 40
            elif b is not None and t == CANARY_RESTORE_TICK and "s" in stash:
                b.stability = stash["s"]
        trace.append(sample(w))
    return trace


def audit(trace):
    """Checked at EVERY tick, not at the end. A protected field must never rise,
    and the clock must never move backwards. A restore-to-a-higher-value is
    itself a rise, so a transient in either direction is caught."""
    v = []
    for i in range(1, len(trace)):
        a, b = trace[i - 1], trace[i]
        for f in PROTECTED:
            if f == "world_tick":
                if b[f] < a[f]:
                    v.append((i, f, a[f], b[f], "clock moved BACKWARD"))
            elif b[f] > a[f]:
                v.append((i, f, a[f], b[f], "protected field INCREASED"))
    return v


def thash(trace):
    return hashlib.sha256(json.dumps(trace, sort_keys=True,
                                     separators=(",", ":")).encode()).hexdigest()


def mirror_provenance():
    """Which sim did this audit actually run against?

    The audit exits 2 when the mirror is MISSING, but it cannot detect a mirror
    that is present and WRONG — a seat with RX_ORACLE_DIR pointed at a patched
    or stale copy gets a silently different sim and a confident verdict. It
    cannot be fixed by pinning an expected hash: the mirror legitimately changes
    as the sim is developed, and a pin would fail on every honest edit.

    So the identity is RECORDED instead of asserted. Every run states the
    resolved path and the sha256 of the sim it used, which makes a wrong mirror
    visible to any reader and to any replay that compares two runs.
    """
    f = MIRROR / "sim_mirror.py"
    h = hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else "<absent>"
    return str(MIRROR), h


def main():
    sm = load()
    path, mhash = mirror_provenance()
    print(f"  K-03B bounded trajectory audit")
    print(f"  mirror path   : {path}")
    print(f"  sim_mirror.py : {mhash}")
    print(f"  (identity RECORDED, not asserted — a wrong-but-present mirror is "
          f"invisible to any check that only tests for absence)")

    canon = run(sm, False)
    canon_v = audit(canon)
    det = thash(canon) == thash(run(sm, False))
    cany = run(sm, True)
    cany_v = audit(cany)
    writable_vals = {t[WRITABLE] for t in canon}

    checks = [
        ("canonical clean", not canon_v,
         f"{len(canon_v)} forbidden transient(s) across {len(canon)} ticks"),
        ("canary caught", bool(cany_v),
         (f"caught at tick_idx={cany_v[0][0]} {cany_v[0][1]} "
          f"{cany_v[0][2]}->{cany_v[0][3]}") if cany_v
         else "CANARY NOT CAUGHT — the audit cannot refuse"),
        (f"{WRITABLE} observable", len(writable_vals) > 1,
         f"{len(writable_vals)} distinct values {sorted(writable_vals)[:5]}"),
        ("deterministic", det, "two canonical runs, full-trace sha256"),
    ]
    ok = True
    for name, passed, detail in checks:
        ok &= passed
        print(f"  {'PASS' if passed else '*** FAIL ***':14s} {name:26s} {detail}")

    print(f"  {'ABSENT':14s} {'counter_threshold':26s} "
          f"C10 unimplemented on every surface — NOT TESTED, not passing")
    print()
    print(f"  {'TRAJECTORY AUDIT CLEAN' if ok else 'TRAJECTORY AUDIT FAILED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

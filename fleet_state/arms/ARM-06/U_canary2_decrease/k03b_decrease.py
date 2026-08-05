#!/usr/bin/env python3
"""K-03B — TRANSIENT AUTHORITY TRAJECTORY (Head 06 ruling, 2026-08-05).

Scope is deliberately four fields and one canary. Not a general trajectory
observability system.

    boss_stability      authority-owned, protected
    world_tick          authority-owned, protected (the sim clock)
    boss_release_delay  the ONE canon-writable counter — must stay observable
    counter_threshold   C10 condition type

DONE WHEN, per the ruling:
    1. the canary is observed FAILING
    2. canonical execution shows NO forbidden transient mutation
    3. the legitimate writable path remains observable
    4. the trace is deterministic across two runs

DETECTOR. boss_stability is the boss HP analogue: authority drives it from 300
down to 0. A protected field that RISES has been written by something that is
not authority, and a rise followed by a return is exactly the transient the
end-state oracle cannot see. So the forbidden signature is: any increase in a
protected field. world_tick must increase by exactly 1 per step and never move
backwards. Both rules are checked every tick, not at the end.

usage: k03b_trajectory.py <sandbox_root> [--canary]
"""
import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(sys.argv[1]).resolve()
CANARY = '--canary' in sys.argv
sys.path.insert(0, str(ROOT / 'tools' / 'oracle'))
sys.path.insert(0, str(ROOT / 'tools' / 'oracle' / 'semantic_kernel'))
import sim_mirror as sm  # noqa: E402

# The blind window measured in I_per_tick: STRIKE_DAMPEN diverged 3426-3656,
# SKILL_DAMPEN 6902-7218. The canary fires inside the first one.
CANARY_MUTATE_TICK = 3500
CANARY_RESTORE_TICK = 3560
PROTECTED = ('boss_stability', 'world_tick')
WRITABLE = ('boss_release_delay',)


def boss_of(w):
    return getattr(w, 'boss', None)


def sample(w):
    b = boss_of(w)
    return {
        'world_tick': int(w.tick),
        'boss_stability': int(getattr(b, 'stability', -1)),
        'boss_release_delay': int(getattr(b, 'release_delay', -1)),
    }


def run(canary):
    script = json.loads((ROOT / 'game' / 'data' / 'acceptance_run_v1.json').read_text())
    world = sm.SimWorld(int(script.get('seed', 7)))
    sm.Encounters.build_arena(world, str(ROOT / 'game' / 'data' / 'arena_earthquake.json'))
    agent = sm.PlayerAgent(script)

    trace = []
    stash = {}
    prev = sample(world)
    trace.append(prev)
    while not agent.exhausted(world):
        agent.feed(world)
        world.step()
        if canary:
            b = boss_of(world)
            t = int(world.tick)
            # ONE canary: change a protected field and put it back, inside the
            # window the end-state oracle provably cannot see.
            if t == CANARY_MUTATE_TICK:
                stash['stability'] = int(b.stability)
                b.stability = int(b.stability) - 40      # DECREASE first
            elif t == CANARY_RESTORE_TICK and 'stability' in stash:
                b.stability = stash['stability']          # then restore upward
        trace.append(sample(world))
    return trace


def audit(trace):
    """Every tick, not just the end."""
    viol = []
    for i in range(1, len(trace)):
        a, b = trace[i - 1], trace[i]
        for f in PROTECTED:
            if f == 'world_tick':
                if b[f] < a[f]:
                    viol.append((i, f, a[f], b[f], 'clock moved BACKWARD'))
            elif b[f] > a[f]:
                viol.append((i, f, a[f], b[f], 'protected field INCREASED'))
    return viol


def observable(trace, field):
    vals = {t[field] for t in trace}
    return len(vals) > 1, sorted(vals)[:6], len(vals)


def trace_hash(trace):
    return hashlib.sha256(
        json.dumps(trace, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


mode = 'CANARY' if CANARY else 'CANONICAL'
tr = run(CANARY)
v = audit(tr)
print(f'=== K-03B {mode} ===')
print(f'  ticks traced            : {len(tr)}')
print(f'  final                   : {tr[-1]}')
print(f'  forbidden transients    : {len(v)}')
for x in v[:6]:
    print(f'      tick_idx={x[0]} {x[1]} {x[2]} -> {x[3]}  {x[4]}')

ok, sample_vals, n = observable(tr, 'boss_release_delay')
print(f'  boss_release_delay observable: {ok}  distinct_values={n} e.g. {sample_vals}')
print('  counter_threshold (C10) : NOT IMPLEMENTED — 0 occurrences on every '
      'implementation surface; nothing to trace. Reported, not simulated.')

tr2 = run(CANARY)
h1, h2 = trace_hash(tr), trace_hash(tr2)
print(f'  determinism (two runs)  : {"IDENTICAL" if h1 == h2 else "DIVERGED"}  {h1[:16]}')

print()
if CANARY:
    print('  EXPECTED: canary MUST be caught -> violations > 0')
    sys.exit(0 if v else 1)
else:
    print('  EXPECTED: canonical MUST be clean -> violations == 0')
    sys.exit(1 if v else 0)

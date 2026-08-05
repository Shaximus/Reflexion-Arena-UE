#!/usr/bin/env python3
"""Do N4/N5 diverge MID-RUN and reconverge, or are they behaviourally inert?

I reported that halving SKILL_DAMPEN / STRIKE_DAMPEN leaves every END-STATE
behavioural quantity identical, with only receipts_head differing — and I
explicitly flagged that per-tick divergence was NOT measured. That caveat is now
in the shipped oracle docstring, so it needs a number behind it.

The distinction matters for what the oracle's end-state scope actually costs:

  INERT       no behavioural quantity ever differs at any tick. The receipts
              differ only because a receipt records the constant as payload.
              End-state-only scope loses nothing here.

  RECONVERGE  behaviour diverges mid-run and comes back. End-state assertions
              are then blind to a real behavioural difference, and "the oracle
              correctly passes" is a weaker statement than it sounded.

Compares a behavioural digest per tick, excluding receipts.
usage: per_tick_probe.py <sandbox_a> <sandbox_b> <label>
"""
import json
import pathlib
import subprocess
import sys

DRIVER = r'''
import json, pathlib, sys
ROOT = pathlib.Path.cwd()
sys.path.insert(0, str(ROOT / "tools" / "oracle"))
sys.path.insert(0, str(ROOT / "tools" / "oracle" / "semantic_kernel"))
import sim_mirror as sm

script = json.loads((ROOT / "game" / "data" / "acceptance_run_v1.json").read_text())
world = sm.SimWorld(int(script.get("seed", 7)))
sm.Encounters.build_arena(world, str(ROOT / "game" / "data" / "arena_earthquake.json"))
agent = sm.PlayerAgent(script)

def digest(w):
    ents = []
    for eid in sorted(w.entities):
        e = w.entities[eid]
        if isinstance(e, dict):
            ents.append((eid, int(e.get("hp", -1))))
    regions = getattr(getattr(w, "terrain", None), "regions", None)
    stress = []
    if isinstance(regions, list):
        stress = [int(r.get("stress", 0)) for r in regions if isinstance(r, dict)]
    elif isinstance(regions, dict):
        stress = [int(regions[k].get("stress", 0)) for k in sorted(regions)]
    boss = getattr(w, "boss", None)
    return {
        "tick": int(w.tick),
        "ents": ents,
        "stress": stress,
        "boss_state": str(getattr(boss, "state", "")),
        "boss_stability": int(getattr(boss, "stability", -1)),
        "boss_release_delay": int(getattr(boss, "release_delay", -1)),
        "boss_tremor_stage": int(getattr(boss, "tremor_stage", -1)),
    }

out = []
while not agent.exhausted(world):
    agent.feed(world)
    world.step()
    out.append(digest(world))
print(json.dumps(out))
'''


def trace(root):
    p = subprocess.run([sys.executable, '-c', DRIVER], cwd=root,
                       capture_output=True, text=True,
                       env={'PYTHONDONTWRITEBYTECODE': '1', 'PATH': '/usr/bin:/bin'})
    if p.returncode != 0:
        print(f'trace FAILED for {root} exit={p.returncode}', file=sys.stderr)
        print(p.stderr, file=sys.stderr)
        raise SystemExit(2)
    return json.loads(p.stdout)


A, B, LABEL = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
ta, tb = trace(A), trace(B)
print(f'{LABEL}: {A.name} vs {B.name}')
print(f'  ticks: {len(ta)} vs {len(tb)}')

if len(ta) != len(tb):
    print('  *** run lengths differ — divergence is structural ***')

n = min(len(ta), len(tb))
diverged = [i for i in range(n) if ta[i] != tb[i]]
if not diverged:
    print('  IDENTICAL AT EVERY TICK — behaviourally INERT.')
    print('  The receipts differ only because a receipt records the constant as')
    print('  payload. End-state-only scope loses nothing for this mutation.')
    raise SystemExit(0)

first, last = diverged[0], diverged[-1]
print(f'  DIVERGES at tick index {first} (tick={ta[first]["tick"]})')
print(f'  last differing tick index {last} (tick={ta[last]["tick"]})')
print(f'  differing ticks: {len(diverged)} of {n}')
reconverged = last < n - 1
print(f'  end state identical: {ta[-1] == tb[-1]}')
print(f'  RECONVERGED: {reconverged}')
print()
for k in ta[first]:
    if ta[first][k] != tb[first][k]:
        print(f'    first divergence in {k!r}: {ta[first][k]!r} -> {tb[first][k]!r}')
raise SystemExit(1)

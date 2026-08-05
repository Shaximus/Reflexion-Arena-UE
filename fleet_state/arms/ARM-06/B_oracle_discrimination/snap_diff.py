#!/usr/bin/env python3
"""Which snapshot field diverges under a mutation the oracle passes?

The oracle exits 0 on N4/N5 while state_hash differs, so SOMETHING in the
canonical snapshot moved and no assertion covers it. This finds what, so any new
assertion targets a real quantity instead of a guessed one.

usage: snap_diff.py <sandbox_a> <sandbox_b>
"""
import json
import pathlib
import subprocess
import sys

DUMP = r'''
import json, pathlib, sys
ROOT = pathlib.Path.cwd()
sys.path.insert(0, str(ROOT / "tools" / "oracle"))
sys.path.insert(0, str(ROOT / "tools" / "oracle" / "semantic_kernel"))
import sim_mirror as sm
script = json.loads((ROOT / "game" / "data" / "acceptance_run_v1.json").read_text())
world = sm.SimWorld(int(script.get("seed", 7)))
sm.Encounters.build_arena(world, str(ROOT / "game" / "data" / "arena_earthquake.json"))
agent = sm.PlayerAgent(script)
while not agent.exhausted(world):
    agent.feed(world)
    world.step()
print(json.dumps(world.snapshot(), sort_keys=True, default=str))
'''


def snap(root):
    p = subprocess.run([sys.executable, '-c', DUMP], cwd=root,
                       capture_output=True, text=True,
                       env={'PYTHONDONTWRITEBYTECODE': '1', 'PATH': '/usr/bin:/bin'})
    if p.returncode != 0:
        print(f'snapshot dump FAILED for {root} exit={p.returncode}', file=sys.stderr)
        print(p.stderr, file=sys.stderr)
        raise SystemExit(2)
    return json.loads(p.stdout)


def walk(a, b, path=''):
    if type(a) is not type(b):
        yield path, a, b
        return
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            yield from walk(a.get(k), b.get(k), f'{path}.{k}')
    elif isinstance(a, list):
        if len(a) != len(b):
            yield f'{path}[len]', len(a), len(b)
        for i, (x, y) in enumerate(zip(a, b)):
            yield from walk(x, y, f'{path}[{i}]')
    elif a != b:
        yield path, a, b


A, B = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
diffs = list(walk(snap(A), snap(B)))
print(f'{A.name}  vs  {B.name}')
print(f'divergent snapshot fields: {len(diffs)}')
for p, x, y in diffs[:60]:
    print(f'  {p:56s} {x!r:>18} -> {y!r}')
if len(diffs) > 60:
    print(f'  ... and {len(diffs) - 60} more')

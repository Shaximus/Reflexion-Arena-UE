#!/usr/bin/env python3
"""Did the mutation change the WORLD at all?

Separates two indistinguishable-by-oracle cases:
  INERT   — state_hash identical to canonical: the mutation produced no
            behavioural difference, so there is nothing for any oracle to catch.
            Not an oracle gap. Reporting it as one would be a false accusation.
  BLIND   — state_hash differs but the oracle still exits 0: a real gap.

Run from a sandbox root.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
ORACLE = ROOT / 'tools' / 'oracle'
sys.path.insert(0, str(ORACLE))
sys.path.insert(0, str(ORACLE / 'semantic_kernel'))
import sim_mirror as sm  # noqa: E402

def _receipt_count(world):
    """Receipts is an object, not a list — find its backing sequence."""
    r = getattr(world, 'receipts', None)
    for attr in ('chain', 'entries', 'items', 'log', '_chain', '_entries'):
        v = getattr(r, attr, None)
        if isinstance(v, (list, tuple)):
            return len(v)
    if isinstance(r, (list, tuple)):
        return len(r)
    return -1


script = json.loads((ROOT / 'game' / 'data' / 'acceptance_run_v1.json').read_text())
world = sm.SimWorld(int(script.get('seed', 7)))
sm.Encounters.build_arena(world, str(ROOT / 'game' / 'data' / 'arena_earthquake.json'))
agent = sm.PlayerAgent(script)
while not agent.exhausted(world):
    agent.feed(world)
    world.step()

total = 0
for e in world.entities.values():
    mx, hp = int(e.get('max_hp', 0)), int(e.get('hp', 0))
    if mx > 0:
        total += max(0, mx - hp)

regions = getattr(getattr(world, 'terrain', None), 'regions', None)
if isinstance(regions, dict):
    stress = {str(rid): int(r.get('stress', 0)) for rid, r in sorted(regions.items())}
elif isinstance(regions, list):
    stress = {str(r.get('id', i)): int(r.get('stress', 0))
              for i, r in enumerate(regions) if isinstance(r, dict)}
else:
    stress = {'<unavailable>': -1}

print(json.dumps({
    'state_hash': world.state_hash(),
    'final_tick': int(world.tick),
    'total_hp_lost': total,
    'receipts': _receipt_count(world),
    'region_stress': stress,
}, sort_keys=True))

#!/usr/bin/env python3
"""Is the mutated constant ever READ during the acceptance run?

A negative control that changes nothing has two very different explanations:
  (a) the oracle is blind to that defect            -> an oracle finding
  (b) the fixture never executes that code path     -> a FIXTURE COVERAGE finding
Reporting (b) as (a) would be a false accusation against the oracle.

This installs a counting proxy over the constants class C in every module that
imported it, replays the acceptance run, and reports read counts per constant.

Run from a sandbox root (expects tools/oracle/ and game/data/ beneath it).
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
ORACLE = ROOT / 'tools' / 'oracle'
sys.path.insert(0, str(ORACLE))
sys.path.insert(0, str(ORACLE / 'semantic_kernel'))

import sim_mirror as sm                      # noqa: E402
import sim_mirror_core, sim_mirror_rules, sim_mirror_companion   # noqa: E402

HITS = {}
REAL = sim_mirror_core.C


class CountingC:
    def __getattr__(self, name):
        HITS[name] = HITS.get(name, 0) + 1
        return getattr(REAL, name)


proxy = CountingC()
patched = []
for mod in (sm, sim_mirror_core, sim_mirror_rules, sim_mirror_companion):
    if getattr(mod, 'C', None) is not None:
        mod.C = proxy
        patched.append(mod.__name__)

ARENA = ROOT / 'game' / 'data' / 'arena_earthquake.json'
SCRIPT = ROOT / 'game' / 'data' / 'acceptance_run_v1.json'
script = json.loads(SCRIPT.read_text())
world = sm.SimWorld(int(script.get('seed', 7)))
sm.Encounters.build_arena(world, str(ARENA))
agent = sm.PlayerAgent(script)
while not agent.exhausted(world):
    agent.feed(world)
    world.step()

print(f'patched modules: {patched}')
print(f'final tick={world.tick}')
print()
WATCH = ['WAVE_DAMAGE_DIV', 'STRIKE_DAMAGE', 'SKILL_DAMPEN', 'STRIKE_DAMPEN']
for k in WATCH:
    n = HITS.get(k, 0)
    verdict = 'READ — mutating it is a live negative control' if n else \
              '*** NEVER READ — fixture does not exercise this path ***'
    print(f'  {k:18s} reads={n:<8d} {verdict}')
print()
print('top 12 constants by read count:')
for k, v in sorted(HITS.items(), key=lambda kv: -kv[1])[:12]:
    print(f'  {k:24s} {v}')

unread = [k for k in WATCH if not HITS.get(k)]
print()
print(f'watched-but-never-read: {unread}')
sys.exit(2 if unread else 0)

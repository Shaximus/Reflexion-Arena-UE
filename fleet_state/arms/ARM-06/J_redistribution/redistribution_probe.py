#!/usr/bin/env python3
"""Close the last logged-but-unmeasured risk on the behavioural oracle.

I wrote in B's report: "total_hp_lost sums across all entities, so a defect that
moves damage BETWEEN entities while preserving the total is invisible to it. The
per-entity HP assertions cover the player and companion, but not the boss."

The first half is a claim about a blind spot; the second half asserts the
per-entity assertions cover it. Neither was measured. The total_hp_lost bound
shipped for a whole cycle on exactly that kind of "obviously fine" reasoning, so
this measures it.

MUTATION: wave damage is redirected to the OTHER of {player, companion}. The
same dmg values are subtracted, so the aggregate is preserved by construction —
only the distribution changes. If total_hp_lost alone were the oracle, this
would pass. The question is whether player_hp / companion_hp catch it.
"""
import pathlib
import re
import shutil
import subprocess
import sys

RA = pathlib.Path('/home/shax/Projects/core-tech/Reflexion-Arena')
SCRATCH = pathlib.Path(sys.argv[1])
SIM_REL = 'tools/oracle/sim_mirror.py'
ORACLE_REL = 'tools/oracle/behavioural_oracle.py'

ORIG = '                e["hp"] = int(e["hp"]) - dmg'
SWAP = '''                _sw = {getattr(self, "player_id", -1): getattr(self, "companion_id", -1),
                       getattr(self, "companion_id", -1): getattr(self, "player_id", -1)}
                _t = self.entities.get(_sw.get(eid, eid), e)
                _t["hp"] = int(_t["hp"]) - dmg'''

LINE = re.compile(r'^\s+(PASS|\*\*\* FAIL \*\*\*)\s+(\S+)\s+(.*)$')

root = SCRATCH / 'R1_damage_redistributed'
if root.exists():
    shutil.rmtree(root)
(root / 'tools').mkdir(parents=True)
(root / 'game').mkdir(parents=True)
shutil.copytree(RA / 'tools' / 'oracle', root / 'tools' / 'oracle',
                ignore=shutil.ignore_patterns('__pycache__'))
shutil.copytree(RA / 'game' / 'data', root / 'game' / 'data')

src = (root / SIM_REL).read_text()
if src.count(ORIG) != 1:
    print(f'ABORT: anchor line appears {src.count(ORIG)} times, expected 1.')
    print('The mutation was not applied, so this proves nothing.')
    sys.exit(2)
(root / SIM_REL).write_text(src.replace(ORIG, SWAP, 1))
print('mutation applied: wave damage redirected between player and companion')
print('aggregate damage preserved by construction; only distribution changes\n')

p = subprocess.run([sys.executable, str(root / ORACLE_REL)], cwd=root,
                   capture_output=True, text=True, timeout=1800,
                   env={'PYTHONDONTWRITEBYTECODE': '1', 'PATH': '/usr/bin:/bin'})
print(p.stdout)
if p.stderr.strip():
    print('STDERR:')
    print(p.stderr)
print(f'producer_exit={p.returncode}')

verdicts = {}
for ln in p.stdout.splitlines():
    m = LINE.match(ln)
    if m:
        verdicts[m.group(2)] = 'PASS' if m.group(1) == 'PASS' else 'FAIL'

print()
print('=' * 78)
caught_by = [k for k, v in verdicts.items() if v == 'FAIL']
total_caught = verdicts.get('total_hp_lost') == 'FAIL'
per_entity_caught = any(verdicts.get(k) == 'FAIL' for k in ('player_hp', 'companion_hp'))
print(f'  assertions that FAILED: {caught_by or "NONE"}')
print(f'  total_hp_lost caught it     : {total_caught}')
print(f'  per-entity HP caught it     : {per_entity_caught}')
print()
if per_entity_caught and not total_caught:
    print('  CONFIRMED AS REPORTED — the aggregate is blind to redistribution, and')
    print('  the per-entity assertions are what cover it. The logged risk is real')
    print('  and already mitigated; it is not an open hole.')
    code = 0
elif not caught_by:
    print('  *** REAL GAP *** the entire oracle passes a redistribution defect.')
    code = 1
else:
    print('  Caught, but not by the assertions predicted. The logged risk needs')
    print('  restating rather than retiring.')
    code = 0
print('=' * 78)
sys.exit(code)

#!/usr/bin/env python3
"""ARM-06: is registry DRIFT a fail-open?

DEFECT-1 closed the case where a governed row goes MISSING. The inverse is
untested: a NEW counter appears in §4.1 marked A2-writable, and the governed case
list never mentions it. If the gate stays green, the allow-list can be widened
without the gate noticing — the same fail-open class, inverted.

Scratch copies only. Nothing tracked is written.
"""
import pathlib
import shutil
import subprocess
import sys

WT = pathlib.Path('/home/shax/.claude-squad/worktrees/arm/'
                  'product-verification-v1_18c8af42232fea3a')
SCRATCH = pathlib.Path(sys.argv[1])
DOC_REL = 'Design/RX_SKILL_ENUMS_V1.md'
TEST_REL = 'Design/tests/test_e11_authority_gate.py'

DOC = (WT / DOC_REL).read_text()
TEST = (WT / TEST_REL).read_text()

ROGUE_WRITABLE = ('| `boss_rage_meter` | `FRxBossEarthquake::Rage` | '
                  '✅ **YES** — added by a later cycle | `RxBossEarthquake.h:99` |')
ROGUE_DENIED = ('| `boss_rage_meter` | `FRxBossEarthquake::Rage` | '
                '❌ **NO** — authority-owned | `RxBossEarthquake.h:99` |')

anchor = next(l for l in DOC.splitlines() if l.startswith('| `world_tick` |'))

CASES = [
    ('D0_baseline', DOC, 'unmutated registry', 0),
    ('D1_new_writable_counter', DOC.replace(anchor, anchor + '\n' + ROGUE_WRITABLE, 1),
     'an UNGOVERNED counter appears, marked A2-WRITABLE', 1),
    ('D2_new_denied_counter', DOC.replace(anchor, anchor + '\n' + ROGUE_DENIED, 1),
     'an ungoverned counter appears, marked denied (lower risk, still ungoverned)', 1),
]

print(f'{"control":26s} {"expected":9s} {"actual":7s} verdict')
print('-' * 78)
bad = []
for name, doc, why, expected in CASES:
    root = SCRATCH / name
    if root.exists():
        shutil.rmtree(root)
    (root / 'Design' / 'tests').mkdir(parents=True)
    (root / DOC_REL).write_text(doc)
    (root / TEST_REL).write_text(TEST)
    p = subprocess.run([sys.executable, str(root / TEST_REL)], cwd=root,
                       capture_output=True, text=True)
    (root / 'out.txt').write_text(p.stdout)
    ok = p.returncode == expected
    if not ok:
        bad.append(name)
    print(f'{name:26s} {expected:<9d} {p.returncode:<7d} '
          f'{"as designed" if ok else "*** FAIL-OPEN ***"}   {why}')
    if p.stderr.strip():
        print(f'    STDERR: {p.stderr.strip().splitlines()[-1]}')

print()
if bad:
    print('REGISTRY DRIFT IS A FAIL-OPEN. A counter added to §4.1 is never checked')
    print('against the governed case list, so the allow-list can be widened silently.')
print(f'controls not behaving as designed: {len(bad)}  {bad}')
sys.exit(1 if bad else 0)

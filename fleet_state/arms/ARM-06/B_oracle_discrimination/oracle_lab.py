#!/usr/bin/env python3
"""ARM-06 task B: discrimination lab for behavioural_oracle.py.

For each negative control, build an isolated sandbox copy of the sim mirror +
game data, apply ONE plausible defect to the SIM (never to the oracle), run the
oracle unmodified, and record which assertions fail.

Checklist B asks: does every retained assertion fail under a relevant negative
control, and pass under the canonical fixture? An assertion that survives every
mutation is not measuring anything.

The real tree at Reflexion-Arena/ is never written to.
Nothing is suppressed: stdout, stderr and exit codes are all recorded.
"""
import json
import pathlib
import re
import shutil
import subprocess
import sys

RA = pathlib.Path('/home/shax/Projects/core-tech/Reflexion-Arena')
SCRATCH = pathlib.Path(sys.argv[1])
ORACLE_REL = 'tools/oracle/behavioural_oracle.py'
CONST_REL = 'tools/oracle/sim_mirror_core.py'

# Negative controls: (name, file, exact old text, new text, why this is plausible)
CONTROLS = [
    ('N0_canonical', None, None, None,
     'unmutated control — every assertion must PASS'),
    ('N1_wave_damage_halved', CONST_REL, 'WAVE_DAMAGE_DIV = 10', 'WAVE_DAMAGE_DIV = 20',
     'THE KNOWN BLIND SPOT: release waves deal half damage'),
    ('N2_wave_damage_doubled', CONST_REL, 'WAVE_DAMAGE_DIV = 10', 'WAVE_DAMAGE_DIV = 5',
     'release waves deal double damage'),
    ('N3_strike_damage_halved', CONST_REL, 'STRIKE_DAMAGE = 10', 'STRIKE_DAMAGE = 5',
     'melee strike deals half damage'),
    ('N4_skill_dampen_halved', CONST_REL, 'SKILL_DAMPEN = 600', 'SKILL_DAMPEN = 300',
     'the authored skill bleeds half the stress — counterplay weakened'),
    ('N5_strike_dampen_halved', CONST_REL, 'STRIKE_DAMPEN = 250', 'STRIKE_DAMPEN = 125',
     'anchor strike bleeds half the stress'),
]

ASSERTIONS = ['final_tick', 'player_hp', 'companion_hp', 'boss_stability',
              'total_hp_lost', 'outcome_flags']
LINE = re.compile(r'^\s+(PASS|\*\*\* FAIL \*\*\*)\s+(\S+)\s+(.*)$')


def build(name):
    root = SCRATCH / name
    if root.exists():
        shutil.rmtree(root)
    (root / 'tools').mkdir(parents=True)
    (root / 'game').mkdir(parents=True)
    shutil.copytree(RA / 'tools' / 'oracle', root / 'tools' / 'oracle',
                    ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copytree(RA / 'game' / 'data', root / 'game' / 'data')
    return root


def run_one(name, rel, old, new, why):
    root = build(name)
    if rel is not None:
        p = root / rel
        src = p.read_text()
        assert src.count(old) == 1, f'{name}: {old!r} appears {src.count(old)}x in {rel}'
        p.write_text(src.replace(old, new, 1))

    proc = subprocess.run([sys.executable, str(root / ORACLE_REL)], cwd=root,
                          capture_output=True, text=True, timeout=1800,
                          env={'PYTHONDONTWRITEBYTECODE': '1', 'PATH': '/usr/bin:/bin'})
    (root / 'oracle.out').write_text(proc.stdout)
    (root / 'oracle.err').write_text(proc.stderr)

    verdicts, details = {}, {}
    for ln in proc.stdout.splitlines():
        m = LINE.match(ln)
        if m:
            verdicts[m.group(2)] = 'PASS' if m.group(1) == 'PASS' else 'FAIL'
            details[m.group(2)] = m.group(3).strip()
    crashed = proc.stderr.strip() != '' and not verdicts
    return {'control': name, 'why': why, 'exit': proc.returncode,
            'verdicts': verdicts, 'details': details,
            'stderr_bytes': len(proc.stderr), 'crashed': crashed,
            'stderr_tail': proc.stderr.strip().splitlines()[-1] if proc.stderr.strip() else ''}


results = []
for c in CONTROLS:
    r = run_one(*c)
    results.append(r)
    flag = 'CRASH' if r['crashed'] else ''
    print(f"[{r['control']}] exit={r['exit']} stderr_bytes={r['stderr_bytes']} {flag}")
    print(f"    {r['why']}")
    if r['crashed']:
        print(f"    STDERR: {r['stderr_tail']}")
    else:
        for a in ASSERTIONS:
            v = r['verdicts'].get(a, 'ABSENT')
            mark = {'PASS': 'pass', 'FAIL': 'FAIL <-- caught', 'ABSENT': 'ABSENT'}[v]
            print(f"    {a:16s} {mark:16s} {r['details'].get(a, '')[:88]}")
    print()

(SCRATCH / 'oracle_lab_results.json').write_text(json.dumps(results, indent=2))

print('=' * 78)
print('DISCRIMINATION MATRIX — which control makes each assertion fail')
print('=' * 78)
base = next(r for r in results if r['control'] == 'N0_canonical')
muts = [r for r in results if r['control'] != 'N0_canonical' and not r['crashed']]
dead = []
for a in ASSERTIONS:
    caught = [r['control'] for r in muts if r['verdicts'].get(a) == 'FAIL']
    base_ok = base['verdicts'].get(a) == 'PASS'
    status = 'DISCRIMINATING' if caught else '*** NEVER FAILS — PROVES NOTHING ***'
    if not caught:
        dead.append(a)
    print(f'  {a:16s} canonical={"PASS" if base_ok else "FAIL"}  '
          f'failed_under={len(caught)}/{len(muts)}  {status}')
    for c in caught:
        print(f'      <- {c}')
print()
print(f'non-discriminating assertions: {len(dead)}  {dead}')
sys.exit(1 if dead else 0)

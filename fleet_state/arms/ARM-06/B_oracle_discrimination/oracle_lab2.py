#!/usr/bin/env python3
"""ARM-06 task B, round 2: verify the PROPOSED oracle repair discriminates.

Adds N6 (script truncation) so final_tick has a demonstrated failure mode, and
runs every control twice — once against the oracle as shipped, once against the
proposed repair — so the improvement is measured, not asserted.

usage: oracle_lab2.py <scratch_dir> <patched_oracle.py>
"""
import json
import pathlib
import re
import shutil
import subprocess
import sys

RA = pathlib.Path('/home/shax/Projects/core-tech/Reflexion-Arena')
SCRATCH = pathlib.Path(sys.argv[1])
PATCHED = pathlib.Path(sys.argv[2]).read_text()
ORACLE_REL = 'tools/oracle/behavioural_oracle.py'
CONST_REL = 'tools/oracle/sim_mirror_core.py'
SCRIPT_REL = 'game/data/acceptance_run_v1.json'

CONTROLS = [
    ('N0_canonical', None, None, None, 'unmutated — every assertion must PASS'),
    ('N1_wave_damage_halved', CONST_REL, 'WAVE_DAMAGE_DIV = 10', 'WAVE_DAMAGE_DIV = 20',
     'THE KNOWN BLIND SPOT: release waves deal half damage'),
    ('N2_wave_damage_doubled', CONST_REL, 'WAVE_DAMAGE_DIV = 10', 'WAVE_DAMAGE_DIV = 5',
     'release waves deal double damage'),
    ('N3_strike_damage_halved', CONST_REL, 'STRIKE_DAMAGE = 10', 'STRIKE_DAMAGE = 5',
     'melee strike deals half damage'),
    ('N4_skill_dampen_halved', CONST_REL, 'SKILL_DAMPEN = 600', 'SKILL_DAMPEN = 300',
     'skill bleeds half the stress (end-state behaviourally inert — receipts_head only)'),
    ('N5_strike_dampen_halved', CONST_REL, 'STRIKE_DAMPEN = 250', 'STRIKE_DAMPEN = 125',
     'strike bleeds half the stress (end-state behaviourally inert — receipts_head only)'),
    ('N6_script_truncated', 'SPECIAL_TRUNCATE', None, None,
     'acceptance script truncated — the run does not complete'),
]

ASSERTIONS = ['final_tick', 'player_hp', 'companion_hp', 'boss_stability',
              'total_hp_lost', 'outcome_flags']
LINE = re.compile(r'^\s+(PASS|\*\*\* FAIL \*\*\*)\s+(\S+)\s+(.*)$')


def build(name, variant):
    root = SCRATCH / f'{name}__{variant}'
    if root.exists():
        shutil.rmtree(root)
    (root / 'tools').mkdir(parents=True)
    (root / 'game').mkdir(parents=True)
    shutil.copytree(RA / 'tools' / 'oracle', root / 'tools' / 'oracle',
                    ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copytree(RA / 'game' / 'data', root / 'game' / 'data')
    if variant == 'repaired':
        (root / ORACLE_REL).write_text(PATCHED)
    return root


def run_one(name, rel, old, new, why, variant):
    root = build(name, variant)
    if rel == 'SPECIAL_TRUNCATE':
        p = root / SCRIPT_REL
        d = json.loads(p.read_text())
        before = json.dumps(d, sort_keys=True)
        for key in ('ticks', 'commands', 'stream', 'plan', 'script'):
            if isinstance(d.get(key), list) and len(d[key]) > 4:
                d[key] = d[key][:len(d[key]) // 2]
                break
        after = json.dumps(d, sort_keys=True)
        # A negative control that silently does nothing is worse than no control:
        # it reports "cannot fail" about an assertion that was never challenged.
        assert before != after, (
            f'{name}: truncation was a NO-OP — fixture keys are {sorted(d)}; '
            f'this control would have proved nothing')
        p.write_text(after)
    elif rel is not None:
        f = root / rel
        src = f.read_text()
        assert src.count(old) == 1, f'{name}: {old!r} x{src.count(old)}'
        f.write_text(src.replace(old, new, 1))

    proc = subprocess.run([sys.executable, str(root / ORACLE_REL)], cwd=root,
                          capture_output=True, text=True, timeout=1800,
                          env={'PYTHONDONTWRITEBYTECODE': '1', 'PATH': '/usr/bin:/bin'})
    verdicts, details = {}, {}
    for ln in proc.stdout.splitlines():
        m = LINE.match(ln)
        if m:
            verdicts[m.group(2)] = 'PASS' if m.group(1) == 'PASS' else 'FAIL'
            details[m.group(2)] = m.group(3).strip()
    return {'control': name, 'variant': variant, 'why': why, 'exit': proc.returncode,
            'verdicts': verdicts, 'details': details, 'stderr_bytes': len(proc.stderr),
            'stderr_tail': proc.stderr.strip().splitlines()[-1] if proc.stderr.strip() else ''}


rows = []
for variant in ('shipped', 'repaired'):
    for c in CONTROLS:
        r = run_one(*c, variant)
        rows.append(r)
        print(f"[{variant:8s}] {r['control']:24s} exit={r['exit']} "
              f"stderr={r['stderr_bytes']}B  " +
              ' '.join(f"{a.split('_')[0][:4]}:{r['verdicts'].get(a,'--')[0]}"
                       for a in ASSERTIONS))
        if r['stderr_tail']:
            print(f"           STDERR: {r['stderr_tail']}")

(SCRATCH / 'lab2_results.json').write_text(json.dumps(rows, indent=2))

print()
print('=' * 78)
print('PER-ASSERTION DISCRIMINATION — shipped vs repaired')
print('=' * 78)
summary = {}
for variant in ('shipped', 'repaired'):
    muts = [r for r in rows if r['variant'] == variant and r['control'] != 'N0_canonical']
    base = next(r for r in rows if r['variant'] == variant and r['control'] == 'N0_canonical')
    for a in ASSERTIONS:
        caught = [r['control'] for r in muts if r['verdicts'].get(a) == 'FAIL']
        summary.setdefault(a, {})[variant] = (base['verdicts'].get(a), caught)

for a in ASSERTIONS:
    sb, sc = summary[a]['shipped']
    rb, rc = summary[a]['repaired']
    print(f'  {a}')
    print(f'      shipped :  canonical={sb}  fails_under={len(sc)}  {sc}')
    print(f'      repaired:  canonical={rb}  fails_under={len(rc)}  {rc}')
    if not rc:
        print('      *** STILL NEVER FAILS — PROVES NOTHING ***')
print()
dead_shipped = [a for a in ASSERTIONS if not summary[a]['shipped'][1]]
dead_repaired = [a for a in ASSERTIONS if not summary[a]['repaired'][1]]
print(f'non-discriminating  shipped : {dead_shipped}')
print(f'non-discriminating  repaired: {dead_repaired}')
sys.exit(1 if dead_repaired else 0)

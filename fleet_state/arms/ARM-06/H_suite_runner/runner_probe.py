#!/usr/bin/env python3
"""Negative controls for run_tests.py's aggregation.

The whole point of this runner is that it must NOT collapse exit 1 (a real
defect) into exit 2 (the check did not run). That claim is untested until the
combinations are actually run, so here they are with synthetic tests.
"""
import pathlib
import shutil
import subprocess
import sys

WT = pathlib.Path('/home/shax/.claude-squad/worktrees/arm/'
                  'product-verification-v1_18c8af42232fea3a')
RUNNER = (WT / 'Design/tests/run_tests.py').read_text()
SCRATCH = pathlib.Path(sys.argv[1])


def stub(code):
    return (f'import sys\nprint("synthetic test, exits {code}")\n'
            f'sys.exit({code})\n')


CASES = [
    ('C0_all_met',            [0, 0, 0],  0, 'every test met'),
    ('C1_one_defect',         [0, 1, 0],  1, 'a defect outranks everything'),
    ('C2_one_inconclusive',   [0, 2, 0],  2, 'inconclusive is NOT a pass'),
    ('C3_defect_and_inconcl', [1, 2, 0],  1, 'defect must not be masked by inconclusive'),
    ('C4_inconcl_and_met',    [2, 0, 0],  2, 'one unrun check taints the suite'),
    ('C5_unexpected_code',    [0, 5, 0],  3, 'an unrecognised code is not a pass'),
    ('C6_empty_suite',        [],         2, 'an empty suite is not a passing suite'),
]

print(f'{"case":24s} {"stub exits":14s} {"expected":9s} {"actual":7s} verdict')
print('-' * 92)
bad = []
for name, codes, expected, why in CASES:
    root = SCRATCH / name
    if root.exists():
        shutil.rmtree(root)
    tdir = root / 'Design' / 'tests'
    tdir.mkdir(parents=True)
    (tdir / 'run_tests.py').write_text(RUNNER)
    for i, c in enumerate(codes):
        (tdir / f'test_stub{i}.py').write_text(stub(c))
    p = subprocess.run([sys.executable, str(tdir / 'run_tests.py')], cwd=root,
                       capture_output=True, text=True)
    (root / 'out.txt').write_text(p.stdout)
    ok = p.returncode == expected
    if not ok:
        bad.append((name, expected, p.returncode))
    print(f'{name:24s} {str(codes):14s} {expected:<9d} {p.returncode:<7d} '
          f'{"as designed" if ok else "*** NOT AS DESIGNED ***"}   {why}')
    if p.stderr.strip():
        print(f'    STDERR: {p.stderr.strip().splitlines()[-1][:88]}')

print()
print(f'aggregation cases not behaving as designed: {len(bad)}  {bad}')
sys.exit(1 if bad else 0)

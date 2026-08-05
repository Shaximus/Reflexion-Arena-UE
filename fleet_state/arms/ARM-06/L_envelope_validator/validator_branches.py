#!/usr/bin/env python3
"""Every refusal branch of validate_reports.py must actually fire.

The escape branch is already proven against the real pre-repair D envelope. The
others — missing key, absolute root, missing file, hash mismatch, bad status,
uncovered load-bearing claim — have never triggered. A check that has never
refused is not a check.

Each fixture is a minimal ARM-06 tree in scratch with one seeded defect.
"""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

WT = pathlib.Path('/home/shax/.claude-squad/worktrees/arm/'
                  'product-verification-v1_18c8af42232fea3a')
VALIDATOR = (WT / 'fleet_state/arms/ARM-06/validate_reports.py').read_text()
SCRATCH = pathlib.Path(sys.argv[1])

BASE = {
    "report_version": "1.0", "report_id": "ARM-FX-TEST-01", "arm_id": "ARM-06",
    "goal_id": "fixture", "generated_at": "2026-08-04T00:00:00Z",
    "state": "CHECKPOINT", "chat_url": "https://example.invalid",
    "authority_ceiling": "L2", "evidence_root": "fleet_state/arms/ARM-06/FX",
    "claims": [{"claim_id": "fx.1", "text": "t", "status": "MACHINE_VERIFIED"}],
    "evidence": [], "artifacts": ["real.txt"], "tests": [],
    "decisions_requested": [], "risks": [], "next_action": "none",
}


def build(name, mutate):
    root = SCRATCH / name
    if root.exists():
        shutil.rmtree(root)
    fx = root / 'fleet_state' / 'arms' / 'ARM-06' / 'FX'
    fx.mkdir(parents=True)
    (root / 'fleet_state/arms/ARM-06/validate_reports.py').write_text(VALIDATOR)
    payload = b'evidence content\n'
    (fx / 'real.txt').write_bytes(payload)
    r = json.loads(json.dumps(BASE))
    r['evidence'] = [{"path": "real.txt",
                      "sha256": hashlib.sha256(payload).hexdigest(),
                      "claim_ids": ["fx.1"]}]
    mutate(r, fx)
    (fx / 'report.json').write_text(json.dumps(r, indent=2))
    return root


def noop(r, fx):
    pass


def missing_key(r, fx):
    del r['next_action']


def absolute_root(r, fx):
    r['evidence_root'] = str(fx)


def missing_file(r, fx):
    (fx / 'real.txt').unlink()


def hash_mismatch(r, fx):
    r['evidence'][0]['sha256'] = '0' * 64


def bad_status(r, fx):
    r['claims'][0]['status'] = 'PROBABLY_FINE'


def uncovered_claim(r, fx):
    r['evidence'][0]['claim_ids'] = []


def escape_dotdot(r, fx):
    r['evidence'][0]['path'] = '../validate_reports.py'


CASES = [
    ('B0_clean_control',   noop,            0, 'a valid envelope must be ADMISSIBLE'),
    ('B1_missing_key',     missing_key,     1, 'required top-level key absent'),
    ('B2_absolute_root',   absolute_root,   1, 'evidence_root must be relative'),
    ('B3_missing_file',    missing_file,    1, 'declared evidence file does not exist'),
    ('B4_hash_mismatch',   hash_mismatch,   1, 'sha256 does not recompute'),
    ('B5_bad_status',      bad_status,      1, 'claim status outside the permitted set'),
    ('B6_uncovered_claim', uncovered_claim, 1, 'load-bearing claim with no evidence'),
    ('B7_escape_dotdot',   escape_dotdot,   1, 'path escapes the declared root'),
]

print(f'{"branch":22s} {"expected":9s} {"actual":7s} verdict')
print('-' * 86)
bad = []
for name, fn, expected, why in CASES:
    root = build(name, fn)
    p = subprocess.run(
        [sys.executable, str(root / 'fleet_state/arms/ARM-06/validate_reports.py')],
        cwd=root, capture_output=True, text=True)
    ok = p.returncode == expected
    if not ok:
        bad.append((name, expected, p.returncode))
    print(f'{name:22s} {expected:<9d} {p.returncode:<7d} '
          f'{"as designed" if ok else "*** BRANCH DID NOT FIRE ***"}   {why}')
    for l in p.stdout.splitlines():
        if l.strip().startswith(('path ', 'evidence ', 'missing ', 'sha256 ',
                                 'claim ', 'load-bearing')):
            print(f'      -> {l.strip()[:76]}')
            break
    if p.stderr.strip():
        print(f'      STDERR: {p.stderr.strip().splitlines()[-1][:76]}')

print()
print(f'branches that did not fire: {len(bad)}  {bad}')
sys.exit(1 if bad else 0)

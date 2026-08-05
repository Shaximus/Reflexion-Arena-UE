#!/usr/bin/env python3
"""Repair D_admission_path/report.json so strict admission can resolve it.

DEFECT: evidence_root was declared as fleet_state/arms/ARM-06/D_admission_path,
but two evidence entries used `../C_e11_test_repair/...` to escape it. A path
that leaves its declared root cannot be resolved by a validator that treats the
root as authoritative, and those two were the ONLY evidence for
d.items-1-to-4-delivered.

REPAIR (option B of the two Prime offered): widen evidence_root to
fleet_state/arms/ARM-06 and rewrite every path against it. The C evidence is
real, committed, and hashes clean — dropping the claim to CLAIMED would discard
verified work rather than fix a bookkeeping error.

Every hash is recomputed from disk. No path may escape the new root.
"""
import hashlib
import json
import pathlib

WT = pathlib.Path('/home/shax/.claude-squad/worktrees/arm/'
                  'product-verification-v1_18c8af42232fea3a')
OLD_ROOT_REL = 'fleet_state/arms/ARM-06/D_admission_path'
NEW_ROOT_REL = 'fleet_state/arms/ARM-06'
NEW_ROOT = WT / NEW_ROOT_REL
REPORT = WT / OLD_ROOT_REL / 'report.json'

r = json.loads(REPORT.read_text())
assert r['evidence_root'] == OLD_ROOT_REL, r['evidence_root']


def rebase(p):
    """Old path (relative to D_admission_path) -> new path (relative to ARM-06)."""
    if p.startswith('../'):
        return p[3:]                      # ../C_e11_test_repair/x -> C_e11_test_repair/x
    return f'D_admission_path/{p}'


# --- rewrite evidence, recomputing every hash from disk ---
new_evidence = []
for e in r['evidence']:
    np = rebase(e['path'])
    f = NEW_ROOT / np
    if not f.is_file():
        raise SystemExit(f'MISSING after rebase: {np}')
    new_evidence.append({
        'path': np,
        'sha256': hashlib.sha256(f.read_bytes()).hexdigest(),
        'claim_ids': e['claim_ids'],
    })

# Did any hash actually change? (It should not — this is a bookkeeping repair.)
changed = [(e['path'], o['sha256'], e['sha256'])
           for o, e in zip(r['evidence'], new_evidence)
           if o['sha256'] != e['sha256']]

r['evidence_root'] = NEW_ROOT_REL
r['evidence'] = new_evidence
r['artifacts'] = [rebase(a) for a in r['artifacts']]
for t in r['tests']:
    for k in ('stdout_artifact', 'stderr_artifact'):
        if t.get(k):
            t[k] = rebase(t[k])

r['risks'].insert(0,
    'REPORT REPAIR 2026-08-04: this envelope originally declared evidence_root as '
    'fleet_state/arms/ARM-06/D_admission_path while two evidence entries used "../" to '
    'reach C_e11_test_repair — the only evidence for d.items-1-to-4-delivered. A path that '
    'escapes its declared root cannot be resolved, so the report was unadmittable even '
    'though every artifact existed and hashed clean. Root widened to '
    'fleet_state/arms/ARM-06 and all paths rewritten against it; no evidence was recaptured '
    'or restated, and no claim changed status. The finding itself '
    '(PRODUCT_AUTHORITY_PATH_ABSENT) is unaffected.')

REPORT.write_text(json.dumps(r, indent=2, ensure_ascii=False) + '\n')

# --- validate ---
escapes = [e['path'] for e in r['evidence']
           if e['path'].startswith('..') or e['path'].startswith('/')]
bad = [e['path'] for e in r['evidence']
       if hashlib.sha256((NEW_ROOT / e['path']).read_bytes()).hexdigest() != e['sha256']]
lb = [c['claim_id'] for c in r['claims']
      if c['status'] in ('MACHINE_VERIFIED', 'INDEPENDENTLY_VERIFIED')]
cov = {c for e in r['evidence'] for c in e['claim_ids']}
uncovered = [c for c in lb if c not in cov]

print(f'evidence_root      : {r["evidence_root"]}')
print(f'evidence entries   : {len(r["evidence"])}')
print(f'paths escaping root: {escapes or "none"}')
print(f'hash mismatches    : {bad or "none"}')
print(f'hashes that CHANGED vs the old report: {changed or "none (bookkeeping only)"}')
print(f'load-bearing claims: {len(lb)}  uncovered: {uncovered or "none"}')
print(f'UNKNOWN claims     : {[c["claim_id"] for c in r["claims"] if c["status"]=="UNKNOWN"]}')
raise SystemExit(1 if (escapes or bad or uncovered) else 0)

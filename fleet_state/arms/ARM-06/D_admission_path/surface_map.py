#!/usr/bin/env python3
"""ARM-06 item 6: prove E11 absence against the COMPLETE expected surface map.

An absence claim is only as good as the surface it was searched over. This
enumerates every place an E11 admission path could live across BOTH product
trees, searches each with named patterns, and records the ripgrep exit code per
surface. rg exit 1 = no match; 2 = error (never silently treated as "no match").
"""
import json
import pathlib
import subprocess
import sys

UE = pathlib.Path('/home/shax/.claude-squad/worktrees/arm/'
                  'product-verification-v1_18c8af42232fea3a')
RA = pathlib.Path('/home/shax/Projects/core-tech/Reflexion-Arena')

# Every surface where an E11 admission path could plausibly be implemented.
SURFACES = [
    ('UE sim core (C++)',            UE / 'Source/ReflexionArena/Sim',   'impl'),
    ('UE commandlet / harness',      UE / 'Source/ReflexionArena',       'impl'),
    ('UE schemas',                   UE / 'Schemas',                     'impl'),
    ('UE data fixtures',             UE / 'Data',                        'impl'),
    ('Godot sim (GDScript)',         RA / 'game/sim',                    'impl'),
    ('Godot tests',                  RA / 'game/tests',                  'impl'),
    ('Godot presentation',           RA / 'game/presentation',           'impl'),
    ('Godot data fixtures',          RA / 'game/data',                   'impl'),
    ('Python sim mirror',            RA / 'tools/oracle',                'impl'),
    ('Adversarial corpora',          RA / 'tests',                       'impl'),
    ('UE design spec (ENUMS)',       UE / 'Design/RX_SKILL_ENUMS_V1.md', 'spec'),
    ('UE design spec (GRAMMAR)',     UE / 'Design/RX_SKILL_GRAMMAR_V1.md', 'spec'),
]

PATTERNS = {
    'E11_effect_id':   ['adjust_counter', 'AdjustCounter'],
    'E11_param':       ['counter_id', 'CounterId'],
    'write_scope':     ['A2-writable', 'A2Writable', 'AllowList', 'allow_list'],
    'C10_condition':   ['counter_threshold', 'CounterThreshold'],
}

rows = []
for label, path, kind in SURFACES:
    if not path.exists():
        rows.append({'surface': label, 'path': str(path), 'kind': kind,
                     'exists': False, 'hits': {}, 'note': 'PATH DOES NOT EXIST'})
        continue
    hits, errors = {}, []
    for pname, pats in PATTERNS.items():
        # -H forces the path prefix. Without it, rg -c on a SINGLE FILE emits a bare
        # count with no colon, which a path:count parser silently reads as zero — a
        # false absence. The spec files below are the positive control that caught it.
        args = ['rg', '-c', '-H', '--no-heading', '--glob', '!**/__pycache__/**']
        for p in pats:
            args += ['-e', p]
        args.append(str(path))
        proc = subprocess.run(args, capture_output=True, text=True)
        if proc.returncode == 2:
            errors.append(f'{pname}: rg exit 2: {proc.stderr.strip()[:120]}')
            hits[pname] = 'ERROR'
        elif proc.returncode == 1:
            hits[pname] = 0
        else:
            hits[pname] = sum(int(l.rsplit(':', 1)[1])
                              for l in proc.stdout.strip().splitlines() if ':' in l)
    rows.append({'surface': label, 'path': str(path), 'kind': kind,
                 'exists': True, 'hits': hits, 'errors': errors})

w = max(len(r['surface']) for r in rows)
print(f'{"surface":{w}s}  kind  ' + '  '.join(f'{k:14s}' for k in PATTERNS))
print('-' * (w + 8 + 16 * len(PATTERNS)))
for r in rows:
    if not r['exists']:
        print(f'{r["surface"]:{w}s}  {r["kind"]:4s}  PATH DOES NOT EXIST')
        continue
    cells = '  '.join(f'{str(r["hits"][k]):14s}' for k in PATTERNS)
    print(f'{r["surface"]:{w}s}  {r["kind"]:4s}  {cells}')
    for e in r.get('errors', []):
        print(f'    RG ERROR: {e}')

impl = [r for r in rows if r['kind'] == 'impl' and r['exists']]
spec = [r for r in rows if r['kind'] == 'spec' and r['exists']]


def total(rs, key):
    return sum(v for r in rs for k, v in r['hits'].items()
               if k == key and isinstance(v, int))


print()
print('=' * 78)
for key in PATTERNS:
    i, s = total(impl, key), total(spec, key)
    print(f'  {key:16s} implementation surfaces: {i:4d}    specification surfaces: {s:4d}')
print('=' * 78)

any_err = [e for r in rows for e in r.get('errors', [])]
e11_impl = total(impl, 'E11_effect_id') + total(impl, 'E11_param')
c10_impl = total(impl, 'C10_condition')
print()
print(f'  surfaces searched: {len(impl)} implementation, {len(spec)} specification')
print(f'  rg errors: {len(any_err)}  (a surface that errored is NOT evidence of absence)')
print(f'  E11 occurrences across ALL implementation surfaces: {e11_impl}')
print(f'  C10 occurrences across ALL implementation surfaces: {c10_impl}')
print()
# POSITIVE CONTROL: the spec surfaces are KNOWN to describe E11. If the detector
# cannot find it where it demonstrably is, the detector is broken and every zero it
# reported is meaningless. An absence proof with no positive control is not a proof.
e11_spec = total(spec, 'E11_effect_id')
print(f'  POSITIVE CONTROL — E11 in specification surfaces: {e11_spec} '
      f'({"detector works" if e11_spec > 0 else "*** DETECTOR BROKEN ***"})')
print()
if e11_spec == 0:
    print('  VERDICT WITHHELD — the detector found nothing on a surface that is KNOWN')
    print('  to describe E11. Every zero above is untrustworthy. Fix the detector first.')
    code = 4
elif any_err:
    print('  VERDICT WITHHELD — at least one surface could not be searched.')
    code = 3
elif e11_impl == 0:
    print('  PRODUCT_AUTHORITY_PATH_ABSENT — E11 appears on ZERO implementation')
    print('  surfaces and only in specification. There is nothing to exercise.')
    code = 0
else:
    print('  E11 appears in implementation — an admission path may exist.')
    code = 1

pathlib.Path(sys.argv[1]).write_text(json.dumps(
    {'surfaces': rows, 'patterns': PATTERNS,
     'e11_impl_occurrences': e11_impl, 'c10_impl_occurrences': c10_impl,
     'rg_errors': any_err}, indent=2))
sys.exit(code)

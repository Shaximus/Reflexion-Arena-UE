#!/usr/bin/env python3
"""ARM-06 task C: does the repaired E11 gate actually fix what I measured?

Runs the PRE-REPAIR and POST-REPAIR test against the same nine controls and
compares exit codes. Everything runs on scratch copies; the tracked doc is never
written.

Exit-code contract for the repaired test:
    0 = all gate conditions met
    1 = GATE CONDITION VIOLATED
    2 = could not evaluate (doc/table unreadable) — must NOT collide with 1
"""
import json
import pathlib
import shutil
import subprocess
import sys

WT = pathlib.Path('/home/shax/.claude-squad/worktrees/arm/'
                  'product-verification-v1_18c8af42232fea3a')
SCRATCH = pathlib.Path(sys.argv[1])
DOC_REL = 'Design/RX_SKILL_ENUMS_V1.md'
TEST_REL = 'Design/tests/test_e11_authority_gate.py'

DOC_SRC = (WT / DOC_REL).read_text()
NEW_SRC = (WT / TEST_REL).read_text()
OLD_SRC = (WT / 'fleet_state/arms/ARM-06/C_e11_test_repair/'
                'test_e11_authority_gate.py.pre-ARM06').read_text()


def row(doc, counter):
    for ln in doc.splitlines():
        if ln.startswith(f'| `{counter}` |'):
            return ln
    raise AssertionError(f'row for {counter} not found')


def flip(counter, frm, to):
    def f(doc):
        old = row(doc, counter)
        assert frm in old, f'{frm!r} not in {old!r}'
        return doc.replace(old, old.replace(frm, to, 1), 1)
    return f


def delete_row(counter):
    def f(doc):
        return doc.replace(row(doc, counter) + '\n', '', 1)
    return f


def garble(counter, frm, to):
    return flip(counter, frm, to)


def insert_column(doc):
    out, in_table = [], False
    for ln in doc.splitlines():
        if ln.startswith('| `counter_id` | Reads |'):
            in_table = True
            out.append(ln.replace('| `counter_id` | Reads |',
                                  '| `counter_id` | Reads | Owner |', 1))
            continue
        if in_table:
            if ln.startswith('|---|---|'):
                out.append('|---|---|---|---|---|')
                continue
            if ln.startswith('| `'):
                p = ln.split('|')
                p.insert(3, ' authority ')
                out.append('|'.join(p))
                continue
            in_table = False
        out.append(ln)
    return '\n'.join(out) + '\n'


def rename_header(doc):
    """The write-scope column header is renamed away — the repair's whole subject."""
    return doc.replace('| **A2-writable via E11** |', '| **Notes** |', 1)


def delete_doc(doc):
    return None          # sentinel: do not write the doc at all


#  name, mutation, why, expected_old_exit, expected_new_exit, cwd
CONTROLS = [
    ('M0_unmutated', None, 'control: honest registry', 0, 0, '.'),
    ('M1_boss_stability_writable', flip('boss_stability', '❌ **NO**', '✅ **YES**'),
     'THE EXPLOIT reintroduced', 1, 1, '.'),
    ('M2_release_delay_revoked', flip('boss_release_delay', '✅ **YES**', '❌ **NO**'),
     'canon writability revoked', 1, 1, '.'),
    ('M3_world_tick_writable', flip('world_tick', '❌ **NO**', '✅ **YES**'),
     'sim clock made writable', 1, 1, '.'),
    ('M4_row_deleted', delete_row('boss_stability'),
     'DEFECT-1: governed counter removed from the registry', 0, 1, '.'),
    ('M5_column_inserted', insert_column,
     'DEFECT-2: benign column added — must NOT be a false alarm', 1, 0, '.'),
    ('M6_doc_deleted', delete_doc,
     'DEFECT-3: registry unreadable — must not look like a breach', 1, 2, '.'),
    ('M7_wrong_cwd', None,
     'DEFECT-3: run from another directory', 1, 0, 'Design/tests'),
    ('M8_verdict_garbled', garble('boss_stability', '❌ **NO** — authority-owned', 'TBD'),
     'verdict cell unparseable — must not be silently read as denied', 0, 2, '.'),
    ('M9_write_column_renamed', rename_header,
     'the A2-writable column header is gone — document regression', 0, 2, '.'),
]

rows = []
for name, fn, why, exp_old, exp_new, cwd_rel in CONTROLS:
    for variant, src, expected in (('pre', OLD_SRC, exp_old), ('post', NEW_SRC, exp_new)):
        root = SCRATCH / f'{name}__{variant}'
        if root.exists():
            shutil.rmtree(root)
        (root / 'Design' / 'tests').mkdir(parents=True)
        doc = DOC_SRC if fn is None else fn(DOC_SRC)
        if doc is not None:
            (root / DOC_REL).write_text(doc)
        (root / TEST_REL).write_text(src)

        wd = root / cwd_rel
        proc = subprocess.run([sys.executable, str(root / TEST_REL)], cwd=wd,
                              capture_output=True, text=True)
        (root / 'out.txt').write_text(proc.stdout)
        (root / 'err.txt').write_text(proc.stderr)
        ok = proc.returncode == expected
        rows.append({'control': name, 'variant': variant, 'why': why,
                     'expected_exit': expected, 'actual_exit': proc.returncode,
                     'as_designed': ok, 'stderr_bytes': len(proc.stderr),
                     'stderr_tail': (proc.stderr.strip().splitlines() or [''])[-1]})

(SCRATCH / 'probe_v2_results.json').write_text(json.dumps(rows, indent=2))

print(f'{"control":28s} {"why":54s} pre  post')
print('-' * 100)
bad = []
for name, fn, why, exp_old, exp_new, _ in CONTROLS:
    r = {x['variant']: x for x in rows if x['control'] == name}
    pre, post = r['pre'], r['post']
    mark = lambda x: f"{x['actual_exit']}{'' if x['as_designed'] else '!'}"
    print(f'{name:28s} {why[:54]:54s} {mark(pre):4s} {mark(post):4s}')
    for x in (pre, post):
        if not x['as_designed']:
            bad.append((name, x['variant'], x['expected_exit'], x['actual_exit'],
                        x['stderr_tail']))

print()
print('exit codes: 0=met 1=GATE VIOLATED 2=cannot evaluate   ! = not as designed')
print()
for name, variant, exp, act, tail in bad:
    print(f'  NOT AS DESIGNED: {name} [{variant}] expected {exp} got {act}  {tail}')
print(f'\ncontrols not behaving as designed: {len(bad)}')

fixed = [n for n, _, _, eo, en, _ in CONTROLS if eo != en]
print(f'behaviour changed by the repair ({len(fixed)}): {fixed}')
sys.exit(1 if bad else 0)

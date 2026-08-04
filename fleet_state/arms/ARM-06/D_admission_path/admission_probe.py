#!/usr/bin/env python3
"""ARM-06 items 5+6: exercise the REAL product authority path end-to-end.

Kestrel: locate and exercise one real E11 admission path; if none exists, prove the
absence against the complete expected surface map rather than inventing one.

This does two things:

  PART 1 — exercises the authority machinery that DOES exist (Commands.validate),
           with a passing case and a refusing case, so the claim rests on observed
           behaviour of live product code.

  PART 2 — the decisive test for E11. If an E11 authority path existed, then
           adjust_counter{boss_release_delay} (CANON, allow-listed) would be ADMITTED
           and adjust_counter{boss_stability} (THE EXPLOIT) would be REFUSED. If both
           are refused IDENTICALLY, the refusal is not authority — it is vocabulary.
           A gate that refuses the legitimate case too is not a gate.

Run from a sandbox root containing tools/oracle/ and game/data/.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools' / 'oracle'))
sys.path.insert(0, str(ROOT / 'tools' / 'oracle' / 'semantic_kernel'))

import sim_mirror as sm                                    # noqa: E402
from sim_mirror_rules import Commands, SkillSystem         # noqa: E402

results = []


def show(part, label, got, expect_ok, note=''):
    ok = bool(got.get('ok')) == expect_ok
    results.append({'part': part, 'label': label, 'expected_ok': expect_ok,
                    'actual_ok': bool(got.get('ok')), 'code': got.get('code', ''),
                    'detail': str(got.get('detail', ''))[:120], 'as_expected': ok,
                    'note': note})
    print(f"  [{'as expected' if ok else 'UNEXPECTED':11s}] {label:52s} "
          f"ok={str(got.get('ok')):5s} code={str(got.get('code','-')):18s} "
          f"{str(got.get('detail',''))[:70]}")


CANON_SPEC_FWD = {'name': 'FAULTLINE INTERRUPT', 'trigger': 'committed_ground_propagation',
                  'effect': 'destabilize_anchor', 'cost': 30, 'cooldown': 240,
                  'commit_window': 20}

print('PART 1 — the authority path that EXISTS: Commands.validate() envelope tiers')
print(f'  command types: {sorted(Commands.TYPES)}')
print(f'  T2 types     : {sorted(Commands.T2_TYPES)}')
print()

# Well-formed params per type. Without these the envelope dies at ERR_MALFORMED
# BEFORE the tier check, and a "refusal" would prove nothing about authority.
PARAMS = {
    'author_skill':     {'spec': dict(CANON_SPEC_FWD)},
    'socket_fragment':  {'fragment': {'id': 'earthquake'}},
    'strike':           {'region': 0},
    'tokenweave_begin': {'region_id': 0, 'mode': 'anchor'},
    'use_skill':        {'skill_id': 'faultline_interrupt', 'target': {'region': 0}},
}

for t2 in sorted(Commands.T2_TYPES):
    base = {'actor': 'companion', 'type': t2, 'params': PARAMS[t2]}
    r_no = Commands.validate(dict(base), None)
    # The refusal must be ERR_AUTHORITY specifically. ERR_MALFORMED here would mean
    # the probe never reached the authority rule.
    reached = r_no.get('code') == 'ERR_AUTHORITY'
    show(1, f'companion T2 {t2!r} WITHOUT approval -> ERR_AUTHORITY',
         r_no, False, 'reached authority rule' if reached
         else 'DID NOT REACH AUTHORITY RULE')
    if not reached:
        print(f'      ^^ probe defect: refused by {r_no.get("code")}, not authority')
    show(1, f'companion T2 {t2!r} WITH approved:true -> must ADMIT',
         Commands.validate(dict(base, approved=True), None), True)
    show(1, f'player T2 {t2!r} (own authority) -> must ADMIT',
         Commands.validate(dict(base, actor='player'), None), True)

print()
print('PART 2 — is there an E11 (adjust_counter) admission path at all?')
print(f'  SkillSystem.LEGAL_EFFECTS = {SkillSystem.LEGAL_EFFECTS}')
print()

CANON_SPEC = {'name': 'FAULTLINE INTERRUPT', 'trigger': 'committed_ground_propagation',
              'effect': 'destabilize_anchor', 'cost': 30, 'cooldown': 240,
              'commit_window': 20}

show(2, 'canonical authored skill -> must ADMIT',
     SkillSystem.validate_spec(dict(CANON_SPEC)), True)

# THE EXPLOIT: a player-authored skill naming an authority-owned counter.
show(2, 'E11 adjust_counter{boss_stability} THE EXPLOIT -> ?',
     SkillSystem.validate_spec(dict(CANON_SPEC, effect='adjust_counter',
                                    counter_id='boss_stability', delta=-1000)),
     False, 'must be refused')

# THE CANON-LEGITIMATE CASE: the one counter the registry says IS A2-writable.
# A real authority gate MUST admit this one.
show(2, 'E11 adjust_counter{boss_release_delay} CANON-LEGITIMATE -> ?',
     SkillSystem.validate_spec(dict(CANON_SPEC, effect='adjust_counter',
                                    counter_id='boss_release_delay', delta=20)),
     True, 'a real allow-list must ADMIT this')

print()
print('=' * 92)
exploit = next(r for r in results if 'THE EXPLOIT' in r['label'])
canon = next(r for r in results if 'CANON-LEGITIMATE' in r['label'])
identical = (exploit['actual_ok'] == canon['actual_ok'] and
             exploit['code'] == canon['code'])
print(f"  exploit refused          : {not exploit['actual_ok']}  code={exploit['code']}")
print(f"  canon-legitimate refused : {not canon['actual_ok']}  code={canon['code']}")
print(f"  refusals IDENTICAL       : {identical}")
print()
if identical and not canon['actual_ok']:
    verdict = 'PRODUCT_AUTHORITY_PATH_ABSENT'
    print('  VERDICT: PRODUCT_AUTHORITY_PATH_ABSENT')
    print('  The exploit and the canon-legitimate case are refused by the SAME rule,')
    print('  with the same code and the same message. That rule is the effect')
    print('  VOCABULARY, not a counter allow-list. Nothing in the product distinguishes')
    print('  boss_stability from boss_release_delay, because nothing in the product')
    print('  implements E11 at all. The exploit is unreachable, not gated.')
elif not exploit['actual_ok'] and canon['actual_ok']:
    verdict = 'E11_AUTHORITY_PATH_PRESENT'
    print('  VERDICT: E11_AUTHORITY_PATH_PRESENT — a real allow-list discriminates.')
else:
    verdict = 'INDETERMINATE'
    print('  VERDICT: INDETERMINATE')

print('=' * 92)
(ROOT / 'admission_results.json').write_text(
    json.dumps({'verdict': verdict, 'results': results,
                'legal_effects': SkillSystem.LEGAL_EFFECTS,
                'command_types': sorted(Commands.TYPES),
                't2_types': sorted(Commands.T2_TYPES)}, indent=2))

unexpected = [r['label'] for r in results if not r['as_expected']]
print(f'\nprobes not matching their stated expectation: {len(unexpected)}')
for u in unexpected:
    print(f'  {u}')
sys.exit(0 if verdict != 'INDETERMINATE' else 3)

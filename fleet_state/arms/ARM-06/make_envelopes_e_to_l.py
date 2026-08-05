#!/usr/bin/env python3
"""Emit report envelopes for ARM-06 items E..L, which produced evidence but no
report.json. Hashes are read from disk; every load-bearing claim must be named
by at least one evidence entry or the generator fails rather than shipping a
claim that would silently drop to CLAIMED.
"""
import hashlib
import json
import pathlib

WT = pathlib.Path('/home/shax/.claude-squad/worktrees/arm/'
                  'product-verification-v1_18c8af42232fea3a')
ARM = WT / 'fleet_state/arms/ARM-06'
CHAT = 'https://claude.ai/code/session_01Qy2Sxm1r9VZPewmoMV6N8k'
GOAL = 'wave1a-ARM-06-product-verification-v1'

M = 'MACHINE_VERIFIED'

ITEMS = {
 'E_registry_drift': dict(
  rid='ARM-06-20260805T0010Z-CHECKPOINT-06', ts='2026-08-05T00:10:00Z',
  claims=[
   ('e.defect6-measured',
    'DEFECT-6 registry drift was a FAIL-OPEN, measured before the fix: adding a new counter row '
    'to §4.1 marked A2-writable passed at exit 0 (D1), and marked denied also passed at exit 0 '
    '(D2). The gate only ever looked up counters it already knew, so the allow-list could be '
    'widened silently — the original hole in reverse.', M,
    ['drift_before.out', 'drift_before.exit']),
   ('e.defect6-closed',
    'DEFECT-6 CLOSED by registry closure: any counter present in §4.1 that no case governs is '
    'now a failure naming the counter and its claimed write state. After the fix D1 -> exit 1, '
    'D2 -> exit 1, canonical D0 -> exit 0.', M,
    ['drift_after.out', 'drift_after.exit', 'drift_probe.py']),
   ('e.schema-not-position',
    'Kestrel item 2, schema half: the §4.1 header is identified by its write-scope column, '
    'exactly one column may claim to be it, and counter_id must be the first column. Two such '
    'columns halts rather than picking one.', M,
    ['canonical_in_place.out', 'canonical_in_place.exit']),
   ('e.no-regression',
    'All 10 prior controls re-run against the hardened gate with zero deviations, so closing '
    'DEFECT-6 and adding the schema checks did not weaken any earlier repair.', M,
    ['regression_matrix.out', 'regression_matrix.exit', 'probe_e11_v2.py']),
  ],
  tests=[('python3 drift_probe.py <scratch>', 1, 'FAIL', 'drift_before.out', 'drift_before.err', True),
         ('python3 drift_probe.py <scratch>  # after fix', 0, 'PASS', 'drift_after.out', 'drift_after.err', True),
         ('python3 probe_e11_v2.py <scratch>', 0, 'PASS', 'regression_matrix.out', 'regression_matrix.err', True)],
  risks=['The gate is stronger but still checks a DOCUMENT. Registry closure prevents silent '
         'widening of the allow-list; it does not make any validator enforce it in code.'],
  next_action='Continue the checklist; item 5 remains open pending an E11 implementation.'),

 'F_c10_read_anchors': dict(
  rid='ARM-06-20260805T0020Z-CHECKPOINT-07', ts='2026-08-05T00:20:00Z',
  claims=[
   ('f.stale-anchor-found',
    'One §4.1 read anchor was STALE: boss_tremor_stage cited RxBossEarthquake.h:81, but '
    'FRxBossEarthquake::TremorStage is declared at :80 (line 81 is blank; line 64 is the '
    'same-named field on the snapshot struct). The other 5 anchors resolved exactly at '
    'TOLERANCE=0. Verified by reading the header, not by the detector alone.', M,
    ['anchors.out', 'anchors.exit']),
   ('f.why-it-matters',
    'The §4.1 Reads column is the mapping C10 will read through once implemented, so the E11 '
    'repair\'s promise that "reading them remains fully permitted via C10" '
    '(RX_SKILL_ENUMS_V1.md:257-259) rests on those pointers being true. Same defect class as '
    'commit 56c4532.', M, ['anchors.out']),
  ],
  tests=[('python3 Design/tests/test_c10_read_anchors.py', 1, 'FAIL', 'anchors.out', 'anchors.err', False)],
  risks=['This test exits 1 BY DESIGN until the citation is corrected; it reports a real defect '
         'and must not be silenced. At the time of this envelope the fix was outside ARM-06 '
         'writable scope (Design/tests only) and was reported for disposition, not edited.'],
  next_action='Await authority to correct RX_SKILL_ENUMS_V1.md:292, or a disposition.'),

 'G_admission_control': dict(
  rid='ARM-06-20260805T0030Z-CHECKPOINT-08', ts='2026-08-05T00:30:00Z',
  claims=[
   ('g.control-waits-for-implementation',
    'Kestrel item 5 has an end-to-end control that requires no edit when E11 lands. Today it '
    'exits 2 with PRODUCT_AUTHORITY_PATH_ABSENT because the exploit and the canon-legitimate '
    'case are refused identically by the effect vocabulary.', M,
    ['today.out', 'today.exit', 'inputs.sha256']),
   ('g.all-branches-exercised',
    'All four exit branches were exercised with fake admission paths injected via RX_ORACLE_DIR, '
    'zero deviations: real gate -> 0, breach (exploit admitted) -> 1, not-a-gate (both refused '
    'for different reasons) -> 1, broken harness (canonical spec refused) -> 2. A branch that '
    'has never executed is not tested.', M,
    ['branch_matrix.out', 'branch_matrix.exit', 'admission_paths_probe.py']),
   ('g.positive-control-mandatory',
    'Before any refusal is believed the canonical spec must be ADMITTED; if a known-good spec '
    'cannot reach the validator, every refusal is meaningless and the test exits 2 rather than '
    '0. Two probes earlier in this cycle passed while measuring nothing, which is why this is '
    'enforced rather than assumed.', M, ['today.out', 'branch_matrix.out']),
  ],
  tests=[('python3 Design/tests/test_e11_admission_path.py', 2, 'FAIL', 'today.out', 'today.err', True),
         ('python3 admission_paths_probe.py <scratch>', 0, 'PASS', 'branch_matrix.out', 'branch_matrix.err', True)],
  risks=['Exit 2 is INCONCLUSIVE, never a pass. Any runner treating non-zero as failure or zero '
         'as success will misread this test; branch on 1 vs 2.'],
  next_action='Re-run when ARM-08 commits an E11 admission path and report what it says.'),

 'H_suite_runner': dict(
  rid='ARM-06-20260805T0040Z-CHECKPOINT-09', ts='2026-08-05T00:40:00Z',
  claims=[
   ('h.tri-state-preserved',
    'The suite runner does not collapse exit 1 (a real defect) into exit 2 (the check did not '
    'run). DEFECT-3 was exactly that collapse one level down, so a runner reporting "N failed" '
    'would reintroduce it one level up.', M,
    ['suite_today.out', 'suite_today.exit', 'inputs.sha256']),
   ('h.aggregation-controls',
    'Seven synthetic aggregation combinations, zero deviations: all met -> 0; one defect -> 1; '
    'one inconclusive -> 2; defect AND inconclusive -> 1 (a defect must not be masked); '
    'inconclusive AND met -> 2 (one unrun check taints the suite); unrecognised exit 5 -> 3; '
    'EMPTY suite -> 2 (nothing ran, so nothing passed).', M,
    ['aggregation_matrix.out', 'aggregation_matrix.exit', 'runner_probe.py']),
  ],
  tests=[('python3 Design/tests/run_tests.py', 1, 'FAIL', 'suite_today.out', 'suite_today.err', False),
         ('python3 runner_probe.py <scratch>', 0, 'PASS', 'aggregation_matrix.out', 'aggregation_matrix.err', True)],
  risks=['The suite cannot reach exit 0 while test_e11_admission_path.py is INCONCLUSIVE. That '
         'is intended: a green suite with an unrun check would be a false report.'],
  next_action='Suite reaches 0 only when E11 exists; not before.'),

 'I_per_tick': dict(
  rid='ARM-06-20260805T0050Z-CHECKPOINT-10', ts='2026-08-05T00:50:00Z',
  claims=[
   ('i.self-falsification',
    'I claimed in the shipped oracle docstring that the dampen mutations were "the design, not a '
    'gap". Per-tick measurement falsifies it: STRIKE_DAMPEN halved diverges at tick 3426 and '
    'reconverges by 3656 (231 ticks); SKILL_DAMPEN halved diverges at 6902 and reconverges by '
    '7218 (317 ticks), first divergence region stress 14 -> 241. Both end identical, so '
    'end-state assertions see nothing.', M,
    ['per_tick.out', 'per_tick.exit', 'per_tick_probe.py']),
   ('i.blind-window-recorded',
    'The oracle is not passing those runs because nothing behavioural happened; it passes because '
    'it only samples the last tick and the difference had healed. That is a real ~300-tick BLIND '
    'WINDOW, and end-state assertions are structurally blind to any defect that reconverges. '
    'Docstring corrected in place (720c9ef8 -> 313b8211) rather than left overclaiming.', M,
    ['behavioural_oracle.corrected.py', 'oracle_before_correction.sha256',
     'oracle_after_correction.sha256']),
   ('i.no-behavioural-regression',
    'After the docstring correction the canonical fixture still PASSes at exit 0 with all six '
    'assertions, and the discrimination matrix is unchanged (player_hp 3/5, companion_hp 3/5, '
    'total_hp_lost 3/5, boss_stability 1/5, outcome_flags 1/5, final_tick 0/5 as documented). A '
    'docstring edit should change nothing, and it was checked rather than assumed.', M,
    ['oracle_after.out', 'oracle_after.exit', 'lab_postfix.out', 'lab_postfix.exit']),
  ],
  tests=[('python3 per_tick_probe.py <a> <b> <label>', 1, 'FAIL', 'per_tick.out', 'per_tick.err', True),
         ('python3 behavioural_oracle.py', 0, 'PASS', 'oracle_after.out', 'oracle_after.err', False),
         ('python3 oracle_lab.py <scratch>', 1, 'FAIL', 'lab_postfix.out', 'lab_postfix.err', True)],
  risks=['Closing the blind window would require sampling the trajectory rather than the end '
         'state. That is beyond "minimum means minimum" and was NOT done — only recorded, so it '
         'is known rather than discovered later by someone trusting a green run.'],
  next_action='Limitation recorded in the shipped file; no further action taken by design.'),

 'J_redistribution': dict(
  rid='ARM-06-20260805T0100Z-CHECKPOINT-11', ts='2026-08-05T01:00:00Z',
  claims=[
   ('j.risk-retired-by-measurement',
    'The last logged oracle risk was measured rather than left as an unverified caveat. Wave '
    'damage redirected between player and companion, aggregate preserved by construction: '
    'total_hp_lost PASSED at exactly 560 (blind, as reported) while player_hp FAILED at +60 and '
    'companion_hp FAILED at -60. The mirrored offsets confirm damage was moved, not changed. '
    'Both halves of the logged risk are exactly true: the aggregate is blind to redistribution '
    'and the per-entity assertions are what cover it.', M,
    ['redistribution.out', 'redistribution.exit', 'redistribution_probe.py']),
  ],
  tests=[('python3 redistribution_probe.py <scratch>', 0, 'PASS', 'redistribution.out', 'redistribution.err', True)],
  risks=['Residual and unchanged: the oracle asserts player and companion HP individually but '
         'not boss HP directly. boss_stability covers the boss as its HP analogue and is exact.'],
  next_action='No open oracle risks remain unmeasured.'),

 'K_anchor_fix': dict(
  rid='ARM-06-20260805T0110Z-CHECKPOINT-12', ts='2026-08-05T01:10:00Z',
  claims=[
   ('k.anchor-corrected',
    'The stale §4.1 anchor was corrected under an explicit authority grant: '
    'RxBossEarthquake.h:81 -> :80. All 6 read anchors now resolve at TOLERANCE=0. '
    'RX_SKILL_ENUMS_V1.md 747ad874... -> dab22c88..., one line changed.', M,
    ['anchors_after.out', 'anchors_after.exit', 'doc_after.sha256']),
   ('k.gate-reproven',
    'The registry doc is an INPUT to the E11 gate, so the edit required re-proving the gate, not '
    'just the anchor test: gate exit 0 (6 counters, 7/7 cases), 10-control regression exit 0 '
    'zero deviations, 3 drift controls exit 0 zero deviations.', M,
    ['gate_after.out', 'gate_after.exit', 'regression_after.out', 'regression_after.exit',
     'drift_after.out', 'drift_after.exit']),
   ('k.prediction-corrected',
    'I predicted the suite would go 1 -> 0. It goes 1 -> 2, and 2 is correct: clearing the defect '
    'leaves test_e11_admission_path.py INCONCLUSIVE because no E11 path exists to exercise, and '
    'inconclusive is not a pass. Had the runner used a two-value contract this suite would now '
    'report GREEN with a third of it unrun.', M,
    ['suite_after.out', 'suite_after.exit']),
  ],
  tests=[('python3 Design/tests/test_c10_read_anchors.py', 0, 'PASS', 'anchors_after.out', 'anchors_after.err', False),
         ('python3 Design/tests/test_e11_authority_gate.py', 0, 'PASS', 'gate_after.out', 'gate_after.err', False),
         ('python3 Design/tests/run_tests.py', 2, 'FAIL', 'suite_after.out', 'suite_after.err', False)],
  risks=['Zero open defects remain, but the suite still cannot reach 0 while the admission path '
         'is absent. That is the honest state, not a failure.'],
  next_action='Suite reaches 0 when ARM-08 lands E11.'),

 'L_envelope_validator': dict(
  rid='ARM-06-20260805T0120Z-CHECKPOINT-13', ts='2026-08-05T01:20:00Z',
  claims=[
   ('l.validator-committed',
    'The check that found D_admission_path unadmittable is committed rather than left as an '
    'inline heredoc. It validates what "files exist" and "hashes match" both miss: that each '
    'evidence path can be RESOLVED from its declared root. Current state: 4 envelopes, all '
    'ADMISSIBLE, 0 defects.', M, ['validate_now.out', 'validate_now.exit']),
   ('l.positive-control-is-the-real-defect',
    'The positive control is the historical artifact, not a hypothetical: the real pre-repair D '
    'envelope restored from commit 90d24aa is REFUSED with exactly the two "../" escapes that '
    'actually occurred, while the other three envelopes stay ADMISSIBLE in the same run.', M,
    ['validate_prerepair.out', 'validate_prerepair.exit']),
   ('l.all-branches-fire',
    'All eight refusal branches fire, zero silent: clean control -> 0; missing required key, '
    'absolute evidence_root, missing file, sha256 mismatch, bad claim status, load-bearing claim '
    'with no evidence, and path escape all -> 1.', M,
    ['branch_matrix.out', 'branch_matrix.exit', 'validator_branches.py']),
  ],
  tests=[('python3 fleet_state/arms/ARM-06/validate_reports.py', 0, 'PASS', 'validate_now.out', 'validate_now.err', False),
         ('python3 validate_reports.py  # pre-repair D restored', 1, 'FAIL', 'validate_prerepair.out', 'validate_prerepair.out', True),
         ('python3 validator_branches.py <scratch>', 0, 'PASS', 'branch_matrix.out', 'branch_matrix.err', True)],
  risks=['This validator duplicates part of strict admission on the arm side. It is a self-check '
         'so evidence changes can be caught before submission, not a replacement for the '
         'authoritative validator.'],
  next_action='Re-run after any evidence change.'),
}

written = []
for item, spec in ITEMS.items():
    root = ARM / item
    assert root.is_dir(), root
    ev_rel = f'fleet_state/arms/ARM-06/{item}'

    paths, seen = [], set()
    for _, _, _, files in spec['claims']:
        for f in files:
            if f not in seen:
                assert (root / f).is_file(), f'{item}: missing evidence {f}'
                seen.add(f)
                paths.append(f)
    for extra in sorted(p.name for p in root.iterdir() if p.is_file()):
        if extra not in seen and extra != 'report.json':
            seen.add(extra)
            paths.append(extra)

    evidence = [{'path': p,
                 'sha256': hashlib.sha256((root / p).read_bytes()).hexdigest(),
                 'claim_ids': [cid for cid, _, _, files in spec['claims'] if p in files]}
                for p in paths]

    report = {
        'report_version': '1.0', 'report_id': spec['rid'], 'arm_id': 'ARM-06',
        'goal_id': GOAL, 'generated_at': spec['ts'], 'state': 'CHECKPOINT',
        'chat_url': CHAT, 'authority_ceiling': 'L2', 'evidence_root': ev_rel,
        'claims': [{'claim_id': c, 'text': t, 'status': s} for c, t, s, _ in spec['claims']],
        'evidence': evidence, 'artifacts': paths,
        'tests': [{'command': c, 'producer_exit_code': e, 'result': r,
                   'stdout_artifact': so, 'stderr_artifact': se, 'negative_control': nc}
                  for c, e, r, so, se, nc in spec['tests']],
        'decisions_requested': [], 'risks': spec['risks'],
        'next_action': spec['next_action'],
    }

    lb = [c['claim_id'] for c in report['claims'] if c['status'] == M]
    cov = {cid for e in evidence for cid in e['claim_ids']}
    missing = [c for c in lb if c not in cov]
    assert not missing, f'{item}: load-bearing claims without evidence: {missing}'

    (root / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    written.append((item, spec['rid'], len(report['claims']), len(evidence)))

for item, rid, nc, ne in written:
    print(f'  {item:24s} {rid}  claims={nc} evidence={ne}')
print(f'\nwrote {len(written)} envelopes')

#!/usr/bin/env python3
"""Exercise EVERY exit path of test_e11_admission_path.py.

The test currently exits 2 (no path exists). Its 0 and 1 branches have never
run. A branch that has never executed is not tested — and this arm has already
shipped two probes that "passed" while measuring nothing.

Each fixture below is a fake sim_mirror_rules module standing in for an
implemented E11 admission path, injected via RX_ORACLE_DIR. Nothing real is
touched.
"""
import os
import pathlib
import subprocess
import sys

WT = pathlib.Path('/home/shax/.claude-squad/worktrees/arm/'
                  'product-verification-v1_18c8af42232fea3a')
TEST = WT / 'Design/tests/test_e11_admission_path.py'
SCRATCH = pathlib.Path(sys.argv[1])

HEADER = '''
WRITABLE = {"boss_release_delay"}
KNOWN = {"boss_stability", "boss_tremor_stage", "boss_prev_anchor_stress",
         "boss_state_ticks", "world_tick", "boss_release_delay"}


class SkillSystem:
    LEGAL_EFFECTS = ["destabilize_anchor", "adjust_counter"]

    @staticmethod
    def validate_spec(spec):
'''

FIXTURES = {
    # A real allow-list: exploit refused, canon-legitimate admitted.
    'FAKE_A_real_gate': HEADER + '''
        if spec.get("effect") == "adjust_counter":
            cid = spec.get("counter_id")
            if cid not in WRITABLE:
                return {"ok": False, "detail": "counter %r is not A2-writable" % cid}
            return {"ok": True, "detail": ""}
        if spec.get("effect") not in SkillSystem.LEGAL_EFFECTS:
            return {"ok": False, "detail": "effect must be one of %s"
                    % SkillSystem.LEGAL_EFFECTS}
        return {"ok": True, "detail": ""}
''',
    # THE BREACH: E11 admitted for any counter, including boss_stability.
    'FAKE_B_breach': HEADER + '''
        if spec.get("effect") in SkillSystem.LEGAL_EFFECTS:
            return {"ok": True, "detail": ""}
        return {"ok": False, "detail": "effect must be one of %s"
                % SkillSystem.LEGAL_EFFECTS}
''',
    # NOT A GATE: both refused, but for DIFFERENT reasons (so not "identical").
    'FAKE_C_not_a_gate': HEADER + '''
        if spec.get("effect") == "adjust_counter":
            cid = spec.get("counter_id")
            return {"ok": False, "detail": "counter %r rejected (id=%s)" % (cid, cid)}
        return {"ok": True, "detail": ""}
''',
    # BROKEN HARNESS: even the canonical spec is refused -> positive control fails.
    'FAKE_D_broken_positive_control': HEADER + '''
        return {"ok": False, "detail": "everything is refused"}
''',
}

EXPECTED = {
    'FAKE_A_real_gate': 0,
    'FAKE_B_breach': 1,
    'FAKE_C_not_a_gate': 1,
    'FAKE_D_broken_positive_control': 2,
}

print(f'{"fixture":34s} {"expected":9s} {"actual":7s} verdict')
print('-' * 88)
bad = []
for name, src in FIXTURES.items():
    d = SCRATCH / name
    d.mkdir(parents=True, exist_ok=True)
    (d / 'sim_mirror_rules.py').write_text(src)
    env = dict(os.environ, RX_ORACLE_DIR=str(d), PYTHONDONTWRITEBYTECODE='1')
    p = subprocess.run([sys.executable, str(TEST)], cwd=WT, env=env,
                       capture_output=True, text=True)
    (d / 'out.txt').write_text(p.stdout)
    (d / 'err.txt').write_text(p.stderr)
    exp = EXPECTED[name]
    ok = p.returncode == exp
    if not ok:
        bad.append((name, exp, p.returncode))
    print(f'{name:34s} {exp:<9d} {p.returncode:<7d} '
          f'{"as designed" if ok else "*** BRANCH NOT AS DESIGNED ***"}')
    verdict_line = [l for l in p.stdout.splitlines()
                    if any(k in l for k in ('BREACH', 'NOT A GATE', 'GATE CONFIRMED',
                                            'PRODUCT_AUTHORITY_PATH_ABSENT',
                                            'CANNOT EVALUATE'))]
    for l in verdict_line[:1]:
        print(f'      -> {l.strip()[:96]}')
    if p.stderr.strip():
        print(f'      STDERR: {p.stderr.strip().splitlines()[-1][:96]}')

print()
print(f'branches not behaving as designed: {len(bad)}  {bad}')
sys.exit(1 if bad else 0)

#!/usr/bin/env python3
"""DIAGONAL control matrix for ARM-06's own Design/tests suite.

The bar Prime named, cleared by ARM-05: each neutered gate fails ONLY its own
case while still accepting the clean one. My earlier matrices showed each test
fails under SOME mutation. That is weaker. A test that fails whenever anything
breaks is an alarm, not a diagnostic — it cannot tell you WHAT broke.

Baseline (clean scratch tree):
    test_c10_read_anchors.py      0  MET
    test_e11_admission_path.py    2  INCONCLUSIVE
    test_e11_authority_gate.py    0  MET
    test_k03b_trajectory.py       0  MET

Four mutations, one per test. Diagonal iff each changes exactly its own target.
Every mutation is applied to a SCRATCH copy; the real tree is never written.
"""
import pathlib
import re
import shutil
import subprocess
import sys

SP = pathlib.Path("/tmp/claude-1000/-home-shax--claude-squad-worktrees-arm-"
                  "product-verification-v1-18c8af42232fea3a/"
                  "53254b17-7eba-4069-80db-3983f669856d/scratchpad")
BASE = SP / "diag" / "base"
MIRROR_SRC = SP / "diag" / "m2"
TESTS = ["test_c10_read_anchors.py", "test_e11_admission_path.py",
         "test_e11_authority_gate.py", "test_k03b_trajectory.py"]


def build(tag):
    root = SP / "diag" / f"run_{tag}"
    if root.exists():
        shutil.rmtree(root)
    shutil.copytree(BASE, root / "repo")
    shutil.copytree(MIRROR_SRC, root / "mirror")
    return root


def run(root):
    """Return {test: exit_code} by running each test directly."""
    out = {}
    for t in TESTS:
        p = subprocess.run([sys.executable, f"Design/tests/{t}"],
                           cwd=root / "repo", capture_output=True, text=True,
                           env={"PATH": "/usr/bin:/bin",
                                "RX_ORACLE_DIR": str(root / "mirror/tools/oracle"),
                                "PYTHONDONTWRITEBYTECODE": "1"})
        out[t] = p.returncode
    return out


# ---- the four mutations, each aimed at exactly one test ----
def mut_gate(root):
    """Flip boss_stability to A2-writable — THE EXPLOIT, aimed at the gate."""
    p = root / "repo/Design/RX_SKILL_ENUMS_V1.md"
    s = p.read_text()
    line = next(l for l in s.splitlines() if l.startswith("| `boss_stability` |"))
    p.write_text(s.replace(line, line.replace("❌ **NO**", "✅ **YES**", 1), 1))


def mut_anchors(root):
    """Point one sim cite at a line that does not exist — aimed at the anchors test."""
    p = root / "repo/Design/RX_SKILL_ENUMS_V1.md"
    s = p.read_text()
    line = next(l for l in s.splitlines() if l.startswith("| `boss_stability` |"))
    p.write_text(s.replace(line, line.replace("RxBossEarthquake.h:75",
                                              "RxBossEarthquake.h:99999", 1), 1))


def mut_trajectory(root):
    """Permanent protected-field violation in the sim — aimed at the trajectory audit."""
    p = root / "mirror/tools/oracle/sim_mirror.py"
    p.write_text(p.read_text() + '''

_dg_orig = SimWorld.step
def _dg_step(self):
    _dg_orig(self)
    b = getattr(self, "boss", None)
    if b is not None and int(self.tick) == 4000:
        b.stability = int(b.stability) + 25
SimWorld.step = _dg_step
''')


def mut_admission(root):
    """Give the mirror a BROKEN E11 gate that admits the exploit — aimed at admission."""
    p = root / "mirror/tools/oracle/sim_mirror_rules.py"
    s = p.read_text()
    s = s.replace('LEGAL_EFFECTS = ["destabilize_anchor"]',
                  'LEGAL_EFFECTS = ["destabilize_anchor", "adjust_counter"]', 1)
    p.write_text(s)


MUTS = [
    ("D1_gate_exploit", mut_gate, "test_e11_authority_gate.py",
     "boss_stability marked A2-writable"),
    ("D2_anchor_stale", mut_anchors, "test_c10_read_anchors.py",
     "one sim cite points at a nonexistent line"),
    ("D3_transient", mut_trajectory, "test_k03b_trajectory.py",
     "permanent protected-field violation at tick 4000"),
    ("D4_broken_gate", mut_admission, "test_e11_admission_path.py",
     "mirror admits adjust_counter with no counter allow-list"),
]

base_root = build("baseline")
BASELINE = run(base_root)
print("BASELINE")
for t in TESTS:
    print(f"  {t:32s} {BASELINE[t]}")
print()
print(f"{'mutation':20s} {'target':32s} changed")
print("-" * 96)
rows = []
for tag, fn, target, why in MUTS:
    r = build(tag)
    fn(r)
    got = run(r)
    changed = [t for t in TESTS if got[t] != BASELINE[t]]
    diag = changed == [target]
    rows.append((tag, target, changed, diag, why, got))
    marks = " ".join(f"{t.replace('test_','').replace('.py','')[:14]}:"
                     f"{BASELINE[t]}->{got[t]}" for t in changed) or "NOTHING CHANGED"
    print(f"{tag:20s} {target:32s} {marks}")
    print(f"{'':20s} {'':32s} {'DIAGONAL' if diag else '*** NOT DIAGONAL ***'}   ({why})")

print()
print("=" * 96)
offdiag = [r for r in rows if not r[3]]
print(f"mutations that are NOT diagonal: {len(offdiag)}")
for tag, target, changed, _, _, _ in offdiag:
    print(f"  {tag}: aimed at {target}, also moved {[c for c in changed if c != target]}"
          f"{' — AND MISSED ITS OWN TARGET' if target not in changed else ''}")
print()
if not offdiag:
    print("DIAGONAL: every mutation moved exactly its own test and left the other")
    print("three at their clean verdicts. Each check is a diagnostic, not an alarm.")
else:
    print("NOT DIAGONAL: at least one mutation moved a test it was not aimed at, or")
    print("failed to move the one it was. Coupled checks cannot localise a defect.")
sys.exit(1 if offdiag else 0)

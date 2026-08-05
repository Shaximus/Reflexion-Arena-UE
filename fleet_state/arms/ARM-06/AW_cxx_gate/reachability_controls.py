#!/usr/bin/env python3
"""Discriminating controls for test_e11_cxx_gate.py's REACHABILITY branch.

The live run exits 3 (predicate correct, gate unreachable). That verdict is only
worth something if the check can also return the OTHER answers. D5 in the
earlier diagonal matrix was the branch that mattered — proving a control can
RECOGNISE a correct implementation, not merely condemn a broken one. Same here.

Four scratch trees, each a full Source/ + Design/tests copy:

  R0  as-shipped                          expect 3  UNREACHABLE
  R1  ParseSkillSpec populates Effects    expect 0  GATE LIVE
  R2  R1 + boss_stability made writable   expect 1  BREACH (reachable AND wrong)
  R3  gate source deleted                 expect 2  cannot evaluate

R2 is the important one: reachability alone must not be read as safety. A gate
that is reachable and wrong is worse than one that is unreachable and right.
"""
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
WT = pathlib.Path("/home/shax/.claude-squad/worktrees/arm/"
                  "product-verification-v1_18c8af42232fea3a")
SIM = "Source/ReflexionArena/Sim"
REF = "origin/arm/e11-authority-impl-v1"
NEEDED = ("RxCounterAuthority.h", "RxCounterAuthority.cpp", "RxSimWorld.cpp",
          "RxSkillSystem.h", "RxBossEarthquake.h", "RxSimWorld.h")
WORK = HERE / "reach"


def build(tag):
    d = WORK / tag
    if d.exists():
        shutil.rmtree(d)
    (d / SIM).mkdir(parents=True)
    (d / "Design/tests").mkdir(parents=True)
    for f in NEEDED:
        p = subprocess.run(["git", "show", f"{REF}:{SIM}/{f}"], cwd=WT,
                           capture_output=True, text=True)
        if p.returncode != 0:
            raise SystemExit(f"could not extract {f} from {REF}")
        (d / SIM / f).write_text(p.stdout)
    for f in ("test_e11_cxx_gate.py", "e11_gate_driver.cpp"):
        shutil.copy(WT / "Design/tests" / f, d / "Design/tests" / f)
    return d


def sub(path, old, new):
    s = path.read_text()
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"ANCHOR MISMATCH in {path.name}: matched {n}x")
    path.write_text(s.replace(old, new, 1))


def r1_make_reachable(d):
    """Teach ParseSkillSpec to read an `effects` array — the gate goes live."""
    sub(d / SIM / "RxSimWorld.cpp",
        '\t\t\tSpec.CommitWindow = static_cast<int32>(S->GetInt(TEXT("commit_window"), -1));',
        '\t\t\tSpec.CommitWindow = static_cast<int32>(S->GetInt(TEXT("commit_window"), -1));\n'
        '\t\t\t// CONTROL FIXTURE: populate Effects from params.spec.effects\n'
        '\t\t\tFRxSkillEffect Fx;\n'
        '\t\t\tSpec.Effects.Add(Fx);')


def r2_reachable_and_broken(d):
    r1_make_reachable(d)
    sub(d / SIM / "RxCounterAuthority.cpp",
        '{ "boss_stability",          false,',
        '{ "boss_stability",          true, ')


def r3_no_gate(d):
    (d / SIM / "RxCounterAuthority.h").unlink()
    (d / SIM / "RxCounterAuthority.cpp").unlink()


CONTROLS = [
    ("R0_as_shipped", None, 3, "predicate correct, gate unreachable"),
    ("R1_reachable", r1_make_reachable, 0, "parser populates Effects — GATE LIVE"),
    ("R2_reachable_broken", r2_reachable_and_broken, 1,
     "reachable AND boss_stability writable — BREACH"),
    ("R3_no_gate", r3_no_gate, 2, "gate source removed — cannot evaluate"),
]

rows = []
for tag, fn, want, why in CONTROLS:
    d = build(tag)
    if fn:
        fn(d)
    # RX_E11_REF is irrelevant here: the tree carries Source/, which wins.
    p = subprocess.run([sys.executable, "Design/tests/test_e11_cxx_gate.py"],
                       cwd=d, capture_output=True, text=True)
    got = p.returncode
    ok = got == want
    rows.append((tag, want, got, ok, why))
    print(f"[{tag:22s}] exit={got} want={want}  {'OK' if ok else '*** MISMATCH ***'}   {why}")
    if not ok:
        for l in p.stdout.splitlines()[-14:]:
            print(f"      | {l}")
        if p.stderr.strip():
            for l in p.stderr.splitlines()[-8:]:
                print(f"      ! {l}")

print()
print("=" * 88)
bad = [r for r in rows if not r[3]]
codes = {r[2] for r in rows}
print(f"  controls           : {len(rows)}")
print(f"  mismatches         : {len(bad)}  {[r[0] for r in bad]}")
print(f"  distinct exit codes: {sorted(codes)}")
print()
if not bad and len(codes) == len(rows):
    print("  DISCRIMINATING: all four states are reachable and each has its own exit")
    print("  code. The check can recognise a LIVE correct gate (R1), not only condemn")
    print("  an unreachable one — and it separates 'reachable but wrong' (R2) from")
    print("  'right but unreachable' (R0), which is the distinction that matters.")
    sys.exit(0)
if len(codes) != len(rows):
    print("  *** CODE COLLISION *** two different states share an exit code.")
print("  NOT DISCRIMINATING — see above.")
sys.exit(1)

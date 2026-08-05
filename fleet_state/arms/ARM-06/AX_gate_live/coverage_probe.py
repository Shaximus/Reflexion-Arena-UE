#!/usr/bin/env python3
"""Does ARM-08's own admission test catch everything ARM-06's does?

ARM-08's test_e11_admission.cpp exercises seven counter ids — the six registry
rows plus one unknown — and asserts admit/refuse on each. That covers the
registry TABLE completely. It does not cover the COMPARISON that resolves an id
to a row.

This runs both test binaries against the same mutated product TU. A mutation
that ARM-08's suite passes and ARM-06's suite fails is a coverage gap, not a
defect in their gate: their controls are sound for what they aim at.

Run against ARM-08's LIVE branch tip, not the stale pushed ref.
"""
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
WT = pathlib.Path("/home/shax/.claude-squad/worktrees/arm/"
                  "product-verification-v1_18c8af42232fea3a")
REF = "arm/e11-authority-impl-v1"
SIM = "Source/ReflexionArena/Sim"
WORK = HERE / "cov"


def extract(ref, path, dest):
    p = subprocess.run(["git", "show", f"{ref}:{path}"], cwd=WT,
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit(f"could not extract {path} from {ref}")
    dest.write_text(p.stdout)


def sub(path, old, new):
    s = path.read_text()
    if s.count(old) != 1:
        raise SystemExit(f"ANCHOR MISMATCH in {path.name}: matched {s.count(old)}x")
    path.write_text(s.replace(old, new, 1))


# ARM-08's test #includes the header via ../../../../Source/... so it needs that
# exact nesting. Recreate it.
def build_tree(tag):
    d = WORK / tag
    if d.exists():
        shutil.rmtree(d)
    (d / SIM).mkdir(parents=True)
    (d / "fleet_state/arms/ARM-08/tests").mkdir(parents=True)
    for f in ("RxCounterAuthority.h", "RxCounterAuthority.cpp"):
        extract(REF, f"{SIM}/{f}", d / SIM / f)
    extract(REF, "fleet_state/arms/ARM-08/tests/test_e11_admission.cpp",
            d / "fleet_state/arms/ARM-08/tests/test_e11_admission.cpp")
    shutil.copy(WT / "Design/tests/e11_gate_driver.cpp", d / "driver.cpp")
    return d


def run_both(d):
    """Compile and run ARM-08's test and ARM-06's driver on the same TU."""
    out = {}
    tu = str(d / SIM / "RxCounterAuthority.cpp")

    a8 = subprocess.run(
        ["g++", "-std=c++17", "-o", str(d / "a8"),
         str(d / "fleet_state/arms/ARM-08/tests/test_e11_admission.cpp"), tu],
        capture_output=True, text=True)
    out["a8_build"] = a8.returncode
    if a8.returncode == 0:
        r = subprocess.run([str(d / "a8")], capture_output=True, text=True)
        out["a8"] = r.returncode

    a6 = subprocess.run(
        ["g++", "-std=c++17", "-I", str(d / SIM), "-o", str(d / "a6"),
         str(d / "driver.cpp"), tu], capture_output=True, text=True)
    out["a6_build"] = a6.returncode
    if a6.returncode == 0:
        r = subprocess.run([str(d / "a6")], capture_output=True, text=True)
        fails = [l.split()[1] for l in r.stdout.splitlines()
                 if l.startswith("ASSERT") and " FAIL " in l]
        out["a6"] = 1 if fails else 0
        out["a6_fails"] = fails
    return out


# ---- the probe: mutate the COMPARISON, not the table ----
def m_case_insensitive(d):
    """SameId folds case. Every registry id still resolves; so does BOSS_RELEASE_DELAY.

    Nothing in ARM-08's seven cases changes verdict, because all seven are
    already exact-case. But an attacker-supplied id that differs only in case now
    reaches Admit on the one writable counter.
    """
    p = d / SIM / "RxCounterAuthority.cpp"
    sub(p, "bool SameId(const char* A, const char* B)\n{",
        "constexpr char Fold(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }\n"
        "bool SameId(const char* A, const char* B)\n{")
    sub(p, "\twhile (*A != '\\0' && *A == *B)", "\twhile (*A != '\\0' && Fold(*A) == Fold(*B))")
    sub(p, "\treturn *A == *B;", "\treturn Fold(*A) == Fold(*B);")


def m_trailing_space(d):
    """SameId ignores a trailing space on the supplied id — same shape of gap."""
    p = d / SIM / "RxCounterAuthority.cpp"
    sub(p, "\treturn *A == *B;",
        "\tif (*A == '\\0' && *B == ' ' && *(B + 1) == '\\0') { return true; }\n"
        "\treturn *A == *B;")


PROBES = [
    ("P0_clean", None, "unmutated live tip — both suites must pass"),
    ("P1_case_insensitive", m_case_insensitive,
     "SameId folds case — BOSS_RELEASE_DELAY reaches the writable counter"),
    ("P2_trailing_space", m_trailing_space,
     "SameId tolerates a trailing space — 'boss_release_delay ' reaches it"),
]

print(f"  subject: {REF} (ARM-08's LIVE branch tip, not the pushed ref)")
print()
print(f"  {'probe':24s} {'ARM-08 test':12s} {'ARM-06 test':12s} verdict")
print("  " + "-" * 88)
gaps = []
for tag, fn, why in PROBES:
    d = build_tree(tag)
    if fn:
        fn(d)
    r = run_both(d)
    if r.get("a8_build") != 0 or r.get("a6_build") != 0:
        print(f"  {tag:24s} BUILD FAILED — probe not evaluated")
        continue
    a8, a6 = r["a8"], r["a6"]
    if tag == "P0_clean":
        verdict = "both clean" if (a8 == 0 and a6 == 0) else "*** BASELINE DIRTY ***"
    elif a8 == 0 and a6 != 0:
        verdict = "*** COVERAGE GAP — ARM-08 passes, ARM-06 catches ***"
        gaps.append((tag, why, r.get("a6_fails", [])))
    elif a8 != 0 and a6 != 0:
        verdict = "both catch it"
    elif a8 != 0 and a6 == 0:
        verdict = "*** ARM-06 GAP — ARM-08 catches, ARM-06 misses ***"
        gaps.append((tag, why, []))
    else:
        verdict = "*** SURVIVED BOTH ***"
        gaps.append((tag, why, []))
    print(f"  {tag:24s} exit={a8:<7d} exit={a6:<7d} {verdict}")
    print(f"  {'':24s} {why}")

print()
print("=" * 92)
if gaps:
    print(f"  coverage differences: {len(gaps)}")
    for tag, why, fails in gaps:
        print(f"    {tag}: {why}")
        if fails:
            print(f"      ARM-06 assertions that fired: {fails}")
    print()
    print("  These are gaps in COVERAGE, not defects in ARM-08's gate. Their seven")
    print("  cases cover the registry TABLE completely; they do not cover the string")
    print("  COMPARISON that resolves an id to a row. The shipped SameId is correct —")
    print("  exact, byte-for-byte, terminator-checked — so nothing is broken today.")
    sys.exit(0)
print("  no coverage difference found by these probes.")
sys.exit(0)

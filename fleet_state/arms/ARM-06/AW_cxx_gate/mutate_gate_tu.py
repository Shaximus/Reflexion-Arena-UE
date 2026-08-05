#!/usr/bin/env python3
"""Discriminating control matrix for the E11 C++ gate harness.

The harness reported 18/18 PASS against ARM-08's real RxCounterAuthority.cpp.
That number is worth nothing on its own — a harness that cannot fail reports
18/18 against anything. This mutates the PRODUCT TRANSLATION UNIT (never the
harness) and records which named assertions fire.

The bar is diagonality in the sense that matters for a diagnostic: every
mutation must produce a DISTINCT failure signature, and the clean tree must
produce the empty one. A harness whose signature is identical for two different
defects is an alarm, not a diagnostic — it tells you something broke, not what.

Nothing here writes to the worktree or to ARM-08's branch: every mutation is
applied to a fresh scratch copy extracted from git.
"""
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE / "src"
DRIVER = HERE / "e11_gate_driver.cpp"
WORK = HERE / "mut"


def build_and_run(tag, mutate=None):
    """Copy the real TU, optionally mutate it, compile, run, return signature."""
    d = WORK / tag
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    for f in ("RxCounterAuthority.h", "RxCounterAuthority.cpp"):
        shutil.copy(SRC / f, d / f)
    if mutate is not None:
        mutate(d)
    c = subprocess.run(
        ["g++", "-std=c++17", "-O0", "-I", str(d), "-o", str(d / "gate"),
         str(d / "RxCounterAuthority.cpp"), str(DRIVER)],
        capture_output=True, text=True)
    if c.returncode != 0:
        return None, c.stderr, {}
    p = subprocess.run([str(d / "gate")], capture_output=True, text=True)
    failed = set()
    for line in p.stdout.splitlines():
        m = re.match(r"ASSERT\s+(\S+)\s+FAIL", line)
        if m:
            failed.add(m.group(1))
    return failed, p.stdout, {"exit": p.returncode}


def sub(path, old, new, count=1):
    """Anchored replace that REFUSES to apply if the anchor is not unique.

    An anchor that matched 0 or 2 times once made me publish '6 controls are
    DECORATIVE' about controls that were fine. A mutation that did not apply
    must be loud, never silent.
    """
    s = path.read_text()
    n = s.count(old)
    if n != count:
        raise SystemExit(f"ANCHOR MISMATCH in {path.name}: {old[:50]!r} matched {n}x, wanted {count}")
    path.write_text(s.replace(old, new, count))


# ---------------------------------------------------------------- mutations
def m_exploit_writable(d):
    """boss_stability becomes A2-writable — the exact exploit §4.0 exists to stop."""
    sub(d / "RxCounterAuthority.cpp",
        '{ "boss_stability",          false,',
        '{ "boss_stability",          true, ')


def m_canon_locked(d):
    """The one canon-writable counter is locked — refuses everything = not a gate."""
    sub(d / "RxCounterAuthority.cpp",
        '{ "boss_release_delay",      true, ',
        '{ "boss_release_delay",      false,')


def m_unknown_falls_through(d):
    """Find() resolves a miss to row 0 — unknown ids masquerade as authority-owned."""
    sub(d / "RxCounterAuthority.cpp",
        "\treturn nullptr;\n}\n} // namespace",
        "\treturn &Registry[0];\n}\n} // namespace")


def m_prefix_match(d):
    """SameId drops its terminator check — a truncated id matches a real counter."""
    sub(d / "RxCounterAuthority.cpp",
        "\treturn *A == *B;\n}",
        "\treturn true;\n}")


def m_details_collapsed(d):
    """Both rejection reasons return the same text — diagnostic specificity lost."""
    sub(d / "RxCounterAuthority.cpp",
        'return "E11 adjust_counter: counter_id is authority-owned and not A2-writable";',
        'return "E11 adjust_counter: counter_id is not in the C10 registry";')


def m_fail_open_unknown(d):
    """Unknown counter fails OPEN — the classic deny-by-default inversion."""
    sub(d / "RxCounterAuthority.cpp",
        "\t\treturn EAdmission::RejectUnknownCounter;   // deny-by-default: unknown id",
        "\t\treturn EAdmission::Admit;   // MUTATED: fail-open")


def m_second_writable(d):
    """A second writable row is added — registry closure violated, gate still 'works'."""
    sub(d / "RxCounterAuthority.cpp",
        '\t{ "world_tick",              false,',
        '\t{ "shadow_counter",          true,  "MUTATED" },\n'
        '\t{ "world_tick",              false,')


MUTATIONS = [
    ("C0_clean", None, "unmutated product TU — must produce the EMPTY signature"),
    ("C1_exploit_writable", m_exploit_writable, "boss_stability marked A2-writable"),
    ("C2_canon_locked", m_canon_locked, "boss_release_delay locked — refuses everything"),
    ("C3_unknown_falls_through", m_unknown_falls_through, "registry miss resolves to row 0"),
    ("C4_prefix_match", m_prefix_match, "SameId matches on prefix"),
    ("C5_details_collapsed", m_details_collapsed, "both rejections share one reason string"),
    ("C6_fail_open_unknown", m_fail_open_unknown, "unknown counter fails OPEN"),
    ("C7_second_writable", m_second_writable, "a second A2-writable row added"),
]

results = []
for tag, fn, why in MUTATIONS:
    failed, out, meta = build_and_run(tag, fn)
    if failed is None:
        print(f"[{tag}] COMPILE FAILED — mutation not evaluated")
        print(out[:600])
        results.append((tag, why, None))
        continue
    results.append((tag, why, frozenset(failed)))
    print(f"[{tag}] {len(failed)} assertion(s) fired   ({why})")
    for a in sorted(failed):
        print(f"      FAIL {a}")
    if not failed and tag != "C0_clean":
        print("      *** MUTATION SURVIVED — no assertion detected it ***")

print()
print("=" * 92)
print("DISCRIMINATION MATRIX")
print("=" * 92)
clean = dict((t, s) for t, _, s in results)["C0_clean"]
if clean != frozenset():
    print(f"  *** C0_clean is NOT clean: {sorted(clean)} — every row below is untrustworthy")

survived = [t for t, _, s in results if s is not None and t != "C0_clean" and not s]
sigs = {}
for t, _, s in results:
    if s is None or t == "C0_clean":
        continue
    sigs.setdefault(s, []).append(t)
collisions = {s: ts for s, ts in sigs.items() if len(ts) > 1}

print(f"  mutations evaluated        : {len([r for r in results if r[2] is not None]) - 1}")
print(f"  survived undetected        : {len(survived)}  {survived}")
print(f"  colliding signatures       : {len(collisions)}")
for s, ts in collisions.items():
    print(f"      {ts} share signature {sorted(s)}")
print()
if not survived and not collisions and clean == frozenset():
    print("  DIAGONAL: every mutation was detected, each with a DISTINCT signature, and")
    print("  the unmutated product TU fired nothing. The harness localises the defect,")
    print("  it does not merely alarm.")
    sys.exit(0)
print("  NOT DIAGONAL — see above.")
sys.exit(1)

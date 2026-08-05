"""Run the Design/tests suite, preserving each test's exit-code meaning.

These tests do NOT use a two-value pass/fail contract. They use three:

    0  condition met
    1  GATE VIOLATED / defect found      — a real finding about the product
    2  COULD NOT EVALUATE                — the check did not run

DEFECT-3 in the E11 gate was exactly this distinction collapsing: a missing file
and a breached authority gate both exited 1, so a runner reading only "non-zero"
could not tell a broken harness from a broken product. A suite runner that
reports "3 failed" would reintroduce that defect one level up. This one does not
aggregate 1 and 2 together, and says so in its own output.

Suite exit code:
    1  if ANY test exits 1        — a real defect outstanding; outranks 2
    2  else if ANY test exits 2   — inconclusive; NOT a pass
    3  else if any test exits >2  — unexpected code, treated as inconclusive
    0  only if every test exits 0

stderr is never discarded: it is captured and printed in full for any test that
does not exit 0.

usage:  python3 Design/tests/run_tests.py [-v]
"""
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
VERBOSE = "-v" in sys.argv or "--verbose" in sys.argv

MEANING = {
    0: ("MET", "condition met"),
    1: ("DEFECT", "gate violated / defect found"),
    2: ("INCONCLUSIVE", "could not evaluate — NOT a pass"),
    3: ("UNEXERCISED", "a real path exists that this check could not reach"),
}


def main():
    tests = sorted(p for p in HERE.glob("test_*.py") if p.is_file())
    if not tests:
        print("  no test_*.py found in Design/tests — nothing ran.")
        print("  exit 2 — an empty suite is not a passing suite.")
        return 2

    print(f"  Design/tests — {len(tests)} test(s), run from {REPO}")
    print(f"  {'test':38s} {'exit':5s} {'meaning':14s} summary")
    print("  " + "-" * 100)

    results = []
    for t in tests:
        p = subprocess.run([sys.executable, str(t)], cwd=REPO,
                           capture_output=True, text=True)
        label, _ = MEANING.get(p.returncode, ("UNEXPECTED", "unexpected exit code"))
        # Last meaningful stdout line, as a one-line summary.
        lines = [l.strip() for l in p.stdout.splitlines() if l.strip()]
        summary = lines[-1][:52] if lines else "(no stdout)"
        print(f"  {t.name:38s} {p.returncode:<5d} {label:14s} {summary}")
        results.append((t, p, label))

    for t, p, label in results:
        if p.returncode == 0 and not VERBOSE:
            continue
        print()
        print(f"  ---- {t.name} (exit {p.returncode}, {label}) ----")
        for l in p.stdout.splitlines():
            print(f"  | {l}")
        if p.stderr.strip():
            print(f"  ---- {t.name} STDERR ({len(p.stderr)} bytes) ----")
            for l in p.stderr.splitlines():
                print(f"  ! {l}")

    codes = [p.returncode for _, p, _ in results]
    defects = [t.name for t, p, _ in results if p.returncode == 1]
    inconclusive = [t.name for t, p, _ in results if p.returncode == 2]
    unexercised = [t.name for t, p, _ in results if p.returncode == 3]
    unexpected = [(t.name, p.returncode) for t, p, _ in results if p.returncode > 3]

    print()
    print("  " + "=" * 100)
    print(f"  met          : {codes.count(0)}")
    print(f"  DEFECT   (1) : {len(defects)}  {defects}")
    print(f"  INCONCL. (2) : {len(inconclusive)}  {inconclusive}")
    print(f"  UNEXERC. (3) : {len(unexercised)}  {unexercised}")
    if unexpected:
        print(f"  UNEXPECTED   : {unexpected}")
    print("  " + "=" * 100)
    print("  1 and 2 are NOT the same result and are not summed. A defect is a")
    print("  finding about the product; inconclusive means the check did not run.")

    if defects:
        return 1
    if inconclusive:
        return 2
    if unexercised or unexpected:
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())

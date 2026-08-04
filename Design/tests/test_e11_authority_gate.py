"""Negative control for the E11 authority repair.

Kestrel: "advance when the authority repair has ... a negative control proving
boss_stability cannot be mutated through E11; existing legitimate A2 counter
mutations still passing."

A gate that has never been observed refusing is not a gate.

SCOPE — read this before trusting a green run.
    This checks the REGISTRY DOCUMENT (RX_SKILL_ENUMS_V1.md §4.1). It does not
    execute product code. Passing this file means the registry is correct and its
    assertions can fail. It does not mean a validator rejects anything.

    MEASURED 2026-08-04, at commit d7ed39b — A DATED OBSERVATION, NOT A STANDING
    FACT. At that time no E11 admission path existed on any of ten implementation
    surfaces across both product trees; `adjust_counter` occurred 0 times in
    implementation and 7 times in specification, and both the exploit
    (`adjust_counter{boss_stability}`) and the canon-legitimate case
    (`adjust_counter{boss_release_delay}`) were refused IDENTICALLY by the effect
    vocabulary, not by any allow-list. Evidence, with the detector's own positive
    control: fleet_state/arms/ARM-06/D_admission_path/report.json.

    ARM-08 IS IMPLEMENTING THAT PATH. When it lands, the paragraph above becomes
    HISTORY and this file will still be checking only the document. Do not cite
    it as current evidence that E11 is unimplemented — re-run the surface map
    (D_admission_path/surface_map.py) and read what it says today. A dated claim
    left undated is how a true observation turns into a false one.

REPAIRED 2026-08-04 by ARM-06 after three measured defects in the original:

  DEFECT-1  absent read as denied. a2_writable() returned False on regex miss, so
            "explicitly denied" and "not in the registry at all" were the same
            answer. Deleting the boss_stability row entirely left the gate green.
            FIX: every governed counter must be PRESENT; absence is now a failure.

  DEFECT-2  positional column capture. The old regex took column 3 by position, so
            inserting a column made every cell read not-writable and all five deny
            assertions passed VACUOUSLY — only the single boss_release_delay
            positive assertion noticed. FIX: the A2-writable column is located by
            HEADER NAME, and an unrecognised verdict cell is an error, not a False.

  DEFECT-6  registry drift fail-open (found 2026-08-04, after the first five were
            closed). DEFECT-1 caught a governed row going MISSING; the inverse was
            unguarded. Adding a new counter row marked A2-writable passed at exit
            0, because the gate only ever looked up the counters it already knew.
            The allow-list could be widened silently — the original hole in
            reverse. FIX: registry closure. Any counter in §4.1 that no case
            governs is a failure.

  DEFECT-3  exit-code collision. The doc path was cwd-relative, so running from any
            other directory exited 1 with FileNotFoundError — indistinguishable, by
            exit code, from a real gate breach. FIX: path is __file__-relative, and
            "cannot evaluate" now exits 2 while "gate breached" exits 1.

Exit codes:  0 = all gate conditions met
             1 = GATE CONDITION VIOLATED
             2 = harness could not evaluate (doc/table unreadable) — NOT a breach
"""
import pathlib
import re
import sys

DOC = pathlib.Path(__file__).resolve().parents[1] / "RX_SKILL_ENUMS_V1.md"

# (counter, expected_writable, why) — every one of these MUST be present in §4.1.
CASES = [
    ("boss_stability",          False, "THE EXPLOIT — A3 world-state via an A2 effect"),
    ("boss_tremor_stage",       False, "authority-owned"),
    ("boss_prev_anchor_stress", False, "authority-owned"),
    ("boss_state_ticks",        False, "authority-owned"),
    ("world_tick",              False, "sim clock — would let a skill move time"),
    ("boss_release_delay",      True,  "CANON — strike-interrupt / Tokenweave window"),
]
# Must NOT appear in the registry, and must default closed.
UNKNOWN = ("not_a_real_counter", "unknown id must default closed")


def die(msg):
    """Cannot evaluate. Exit 2 so a breach (1) is never confused with a harness fault."""
    print(f"  CANNOT EVALUATE: {msg}")
    print("\n  exit 2 — this is NOT a gate breach; the gate was not testable.")
    sys.exit(2)


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def parse_registry(text):
    """Return {counter_id: writable_bool} from the §4.1 table.

    The writable column is found BY NAME. If the table shape changes, this raises
    rather than silently reading a neighbouring column.
    """
    lines = text.splitlines()
    header_i = col = None
    for i, line in enumerate(lines):
        if not line.startswith("|"):
            continue
        c = cells(line)
        # Identify the §4.1 header by the write-scope column, NOT by the presence of
        # "counter_id" — that substring also occurs in the C10 conditions row (:233)
        # and the E11 effects row (:338), which are different tables entirely.
        matches = [j for j, x in enumerate(c) if "a2-writable" in x.lower()]
        if not matches:
            continue
        # SCHEMA, not position, now that this is confirmed to be the header.
        if len(matches) > 1:
            die(f"§4.1 header has {len(matches)} columns claiming to be the "
                f"A2-writable column: {[c[j] for j in matches]}")
        if "counter_id" not in c[0]:
            die(f"§4.1 header found but `counter_id` is not the first column: {c}")
        header_i, col = i, matches[0]
        break
    if col is None:
        die("no §4.1 table with an 'A2-writable' column header was found in "
            f"{DOC.name}. The registry's write-scope column is the whole point of the "
            "authority repair; if it is gone, that is a document regression.")

    out = {}
    for line in lines[header_i + 2:]:          # +2 skips the |---|---| rule
        if not line.startswith("|"):
            break
        c = cells(line)
        if len(c) <= col:
            die(f"registry row has {len(c)} cells, need > {col}: {line!r}")
        cid = c[0].strip("`")
        # Match the document's bold verdict marker exactly. A bare substring test is
        # what makes this class of parser wrong: "NO" occurs inside "CANON", which is
        # in the boss_release_delay justification text.
        marks = re.findall(r"\*\*(YES|NO)\*\*", c[col].upper())
        if len(set(marks)) != 1:
            die(f"counter {cid!r}: A2-writable cell has no single **YES**/**NO** "
                f"verdict: {c[col]!r}")
        out[cid] = marks[0] == "YES"
    if not out:
        die("the §4.1 table has a header but no counter rows")
    return out


def main():
    if not DOC.is_file():
        die(f"{DOC} not found")
    registry = parse_registry(DOC.read_text())

    print(f"  registry: {DOC.name} §4.1 — {len(registry)} counters")
    print("  counter                   expect  actual  verdict")
    print("  " + "-" * 62)

    fails = 0
    for cid, expect, why in CASES:
        if cid not in registry:
            # DEFECT-1: absence is a breach, not a silent pass.
            fails += 1
            print(f"  {cid:24s} {str(expect):6s}  {'ABSENT':6s}  *** FAIL ***   "
                  f"NOT IN REGISTRY — governed counter went missing; {why}")
            continue
        got = registry[cid]
        ok = got == expect
        fails += not ok
        print(f"  {cid:24s} {str(expect):6s}  {str(got):6s}  "
              f"{'PASS' if ok else '*** FAIL ***'}   {why}")

    cid, why = UNKNOWN
    present = cid in registry
    fails += present
    print(f"  {cid:24s} {'False':6s}  {'PRESENT' if present else 'absent':6s}  "
          f"{'*** FAIL ***' if present else 'PASS'}   {why}")

    # DEFECT-6: registry CLOSURE. DEFECT-1 caught a governed row going missing; this
    # catches the inverse — a new counter appearing that no case governs. Without it
    # the allow-list can be widened silently, which is the original hole in reverse:
    # boss_stability was writable because no column denied it; a new counter would be
    # writable because no case checks it.
    ungoverned = sorted(set(registry) - {c[0] for c in CASES})
    for cid in ungoverned:
        fails += 1
        state = "WRITABLE" if registry[cid] else "denied"
        print(f"  {cid:24s} {'—':6s}  {state:6s}  *** FAIL ***   "
              f"UNGOVERNED — in §4.1 but no case governs it; add it to CASES with an "
              f"explicit expectation")

    print()
    print(f"  {'ALL GATE CONDITIONS MET' if not fails else f'{fails} FAILURE(S)'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

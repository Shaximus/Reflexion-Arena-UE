"""Verify the §4.1 registry's READ anchors resolve to real fields in the UE sim.

This is NOT the A.5 test. A.5 asks whether C10 reads are unaffected by the E11
write-scope repair, and that remains OPEN: `counter_threshold` occurs 0 times
across every implementation surface, so there is no C10 implementation to be
affected. No test for that was invented.

What this DOES check is a precondition A.5 will inherit. §4.1 maps each
`counter_id` to a C++ field and cites `file:line`:

    | `boss_stability` | `FRxBossEarthquake::Stability` | ... | `RxBossEarthquake.h:75` |

The day C10 is implemented, it reads through that mapping. If an anchor has
drifted — the file moved, the field was renamed, lines shifted — C10 will read
the wrong thing, and the E11 repair's promise that "reading them remains fully
permitted via C10" (RX_SKILL_ENUMS_V1.md:257-259) is built on a stale pointer.
Stale line anchors are a defect class this codebase has already shipped once
(commit 56c4532, "correct stale prime-constraint anchor").

Exit codes:  0 = every anchor resolves
             1 = at least one anchor is STALE
             2 = harness could not evaluate
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve()
DESIGN = HERE.parents[1]
REPO = HERE.parents[2]
DOC = DESIGN / "RX_SKILL_ENUMS_V1.md"
SIM = REPO / "Source" / "ReflexionArena" / "Sim"

# How far from the cited line the field may appear before we call it drift.
TOLERANCE = 0


def die(msg):
    print(f"  CANNOT EVALUATE: {msg}")
    print("\n  exit 2 — not a stale-anchor finding; the check did not run.")
    sys.exit(2)


def cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def registry_rows(text):
    """Yield (counter_id, reads_cell, cite_cell) from the §4.1 table."""
    lines = text.splitlines()
    header_i = None
    for i, line in enumerate(lines):
        if not line.startswith("|"):
            continue
        c = cells(line)
        if any("a2-writable" in x.lower() for x in c) and "counter_id" in c[0]:
            header_i = i
            break
    if header_i is None:
        die("no §4.1 registry header found")
    out = []
    for line in lines[header_i + 2:]:
        if not line.startswith("|"):
            break
        c = cells(line)
        if len(c) < 4:
            die(f"registry row has {len(c)} cells, need >= 4: {line!r}")
        out.append((c[0].strip("`"), c[1], c[-1]))
    if not out:
        die("§4.1 header found but no counter rows")
    return out


def main():
    if not DOC.is_file():
        die(f"{DOC} not found")
    if not SIM.is_dir():
        die(f"{SIM} not found — cannot resolve any anchor")

    rows = registry_rows(DOC.read_text())
    print(f"  §4.1 read anchors — {len(rows)} counters, resolved against {SIM.name}/")
    print(f"  {'counter':24s} {'field':32s} {'anchor':26s} verdict")
    print("  " + "-" * 96)

    stale = 0
    for cid, reads, cite in rows:
        # reads: `FRxBossEarthquake::Stability`   cite: `RxBossEarthquake.h:75`
        m_field = re.search(r"`[^`]*::(\w+)`", reads)
        m_cite = re.search(r"`([\w.]+):(\d+)", cite)
        if not m_field or not m_cite:
            stale += 1
            print(f"  {cid:24s} {reads[:32]:32s} {cite[:26]:26s} "
                  f"*** FAIL *** unparseable field or anchor")
            continue
        field, fname, lineno = m_field.group(1), m_cite.group(1), int(m_cite.group(2))
        path = SIM / fname
        if not path.is_file():
            stale += 1
            print(f"  {cid:24s} {field:32s} {fname}:{lineno:<18d} "
                  f"*** FAIL *** file not found")
            continue
        src = path.read_text().splitlines()
        if not (1 <= lineno <= len(src)):
            stale += 1
            print(f"  {cid:24s} {field:32s} {fname}:{lineno:<18d} "
                  f"*** FAIL *** file has {len(src)} lines")
            continue
        lo = max(0, lineno - 1 - TOLERANCE)
        hi = min(len(src), lineno + TOLERANCE)
        window = src[lo:hi]
        hit = any(re.search(rf"\b{re.escape(field)}\b", w) for w in window)
        if not hit:
            stale += 1
            found = [i + 1 for i, l in enumerate(src)
                     if re.search(rf"\b{re.escape(field)}\b", l)]
            print(f"  {cid:24s} {field:32s} {fname}:{lineno:<18d} "
                  f"*** FAIL *** not at cited line; found at {found or 'nowhere'}")
        else:
            print(f"  {cid:24s} {field:32s} {fname}:{lineno:<18d} PASS")

    print()
    print(f"  {'ALL READ ANCHORS RESOLVE' if not stale else f'{stale} STALE ANCHOR(S)'}")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())

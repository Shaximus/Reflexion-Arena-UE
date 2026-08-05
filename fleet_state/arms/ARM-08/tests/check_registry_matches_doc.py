#!/usr/bin/env python3
"""ARM-08 — doc/code drift check for the E11 allow-list.

ARM-06's negative control parses Design/RX_SKILL_ENUMS_V1.md §4.1 and proves the
DOC says the right thing. The C++ gate is what the sim actually obeys. If those
two disagree, one of them is lying and the gate is not the one that ships.

This compares the §4.1 table against the shipped registry in
Source/ReflexionArena/Sim/RxCounterAuthority.cpp, in both directions:
counters in the doc but not the code, counters in the code but not the doc, and
any writability disagreement.

Exit: 0 = tables agree, 1 = drift, 2 = could not parse one of the two sources
      (which is itself a failure — a check that cannot read its input must not
      report agreement).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3].parent
DOC = ROOT / "Design/RX_SKILL_ENUMS_V1.md"
CPP = ROOT / "Source/ReflexionArena/Sim/RxCounterAuthority.cpp"

# --- doc side: §4.1 rows look like  | `id` | reads | ✅ YES / ❌ NO | cite |
#
# Scoped to the §4.1 section on purpose. A whole-document scan pulls in §2.1's
# reference table, where the substring test "NO" in "CANON" matches — a false
# positive that made this check report 4 phantom drifts on its first run.
doc_text = DOC.read_text()
sec = re.search(r"^### 4\.1 .*?(?=^## )", doc_text, re.M | re.S)
doc_rows = {}
if sec:
    for m in re.finditer(r"^\|\s*`([a-z_]+)`\s*\|([^|]*)\|([^|]*)\|", sec.group(0), re.M):
        cid, _reads, writable = m.group(1), m.group(2), m.group(3)
        if re.search(r"\b(YES|NO)\b", writable):    # the §4.1 "A2-writable" column
            doc_rows[cid] = bool(re.search(r"\bYES\b", writable))

# --- code side: registry rows look like  { "id", true/false, "cite" },
code_rows = {}
for m in re.finditer(r'\{\s*"([a-z_]+)"\s*,\s*(true|false)\s*,', CPP.read_text()):
    code_rows[m.group(1)] = (m.group(2) == "true")

print(f"  doc  §4.1 rows parsed : {len(doc_rows)}  from {DOC.relative_to(ROOT)}")
print(f"  code registry rows    : {len(code_rows)}  from {CPP.relative_to(ROOT)}")
if not doc_rows or not code_rows:
    print("  *** PARSE FAILURE — refusing to report agreement. Exit 2.")
    sys.exit(2)

fails = 0
print(f"\n  {'counter':26s} {'doc':>6s} {'code':>6s}  verdict")
print("  " + "-" * 56)
for cid in sorted(set(doc_rows) | set(code_rows)):
    d = doc_rows.get(cid)
    c = code_rows.get(cid)
    ok = (d is not None and c is not None and d == c)
    fails += 0 if ok else 1
    ds = "-" if d is None else ("YES" if d else "NO")
    cs = "-" if c is None else ("YES" if c else "NO")
    why = "" if ok else ("  MISSING IN CODE" if c is None else
                         "  MISSING IN DOC" if d is None else "  WRITABILITY DISAGREES")
    print(f"  {cid:26s} {ds:>6s} {cs:>6s}  {'PASS' if ok else '*** FAIL ***'}{why}")

print()
print(f"  {'DOC AND CODE AGREE' if not fails else f'{fails} DRIFT(S)'}")
sys.exit(1 if fails else 0)

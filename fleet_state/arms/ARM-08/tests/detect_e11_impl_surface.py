#!/usr/bin/env python3
"""ARM-08 item 1 — independent reproduction of ARM-06's PRODUCT_AUTHORITY_PATH_ABSENT.

Does NOT take ARM-06 on trust. Enumerates the implementation surface explicitly,
prints it, and runs a token detector over it. Ships a POSITIVE CONTROL: the same
detector function is run against a synthetic fixture that DOES contain an E11
admission path. If the positive control does not fire, the detector is broken and
the run is aborted before any claim about Source/ is made.

Exit codes:
  0 = detector proven live (positive control fired) AND a real E11 implementation
      surface was found in Source/
  1 = detector proven live AND the E11 implementation surface is ABSENT
      (this reproduces ARM-06)
  2 = DETECTOR BROKEN — positive control failed to fire; no claim is made
"""
import pathlib
import re
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3].parent
IMPL_GLOBS = ("Source/**/*.h", "Source/**/*.cpp", "Source/**/*.cs")

# Token classes, split by evidential weight.
#
# DECISIVE — a token in this set can only appear if an E11 counter-write path is
# actually being named in code. None of them can be produced by the pre-existing
# sim: there is no counter registry, no adjust_counter effect, no write-scope.
#
# ADJACENT — tokens that DO appear in the pre-existing sim for unrelated reasons
# (the generic T2 command-envelope authority gate; the boss's Stability field as
# a plain sim member). Counting these as an "E11 implementation" is exactly the
# false positive that would let a missing gate look present. They are reported
# separately and are NOT part of the verdict.
DECISIVE = {
    "effect_id_E11":      re.compile(r"\bE11\b"),
    "effect_verb":        re.compile(r"adjust_counter", re.I),
    "counter_param":      re.compile(r"counter_id|CounterId", re.I),
    "condition_C10":      re.compile(r"\bC10\b|counter_threshold", re.I),
    "write_scope":        re.compile(r"a2[_ ]?writable|A2Writable|allow[- _]?list|AllowList", re.I),
    "counter_registry":   re.compile(r"CounterRegistry|counter_registry|AdmitCounter", re.I),
    "canon_writable_ctr": re.compile(r"boss_release_delay|BossReleaseDelay"),
}
ADJACENT = {
    "generic_authority":  re.compile(r"ERR_AUTHORITY|ErrAuthority"),
    "stability_field":    re.compile(r"boss_stability|BossStability"),
}
TOKENS = {**DECISIVE, **ADJACENT}

FIXTURE = r'''
// SYNTHETIC POSITIVE-CONTROL FIXTURE - not shipped, not in Source/.
// A plausible minimal E11 admission path, written the way the real one would be.
namespace RxCounterAuthority {
struct FRxCounterEntry { const char* CounterId; bool bA2Writable; };
// registry / allow-list: exactly one counter is A2-writable
static const FRxCounterEntry Registry[] = {
    { "boss_stability",      false },  // authority-owned
    { "boss_release_delay",  true  },  // canon strike-interrupt
};
// C10 counter_threshold reads every entry; E11 adjust_counter writes only allow-listed ones.
EAdmission AdmitCounterWrite(const char* CounterId);  // returns ERR_AUTHORITY on reject
}
'''


def scan(paths):
    """Run every token class over a set of files. Returns {token: [(path,line,text)]}."""
    hits = {k: [] for k in TOKENS}
    for p in paths:
        try:
            text = p.read_text(errors="replace")
        except OSError as e:
            print(f"  !! unreadable {p}: {e}", file=sys.stderr)
            continue
        for n, line in enumerate(text.splitlines(), 1):
            for name, rx in TOKENS.items():
                if rx.search(line):
                    hits[name].append((str(p), n, line.strip()[:110]))
    return hits


def report(title, hits, files_scanned):
    print(f"\n--- {title} ({files_scanned} files) ---")
    for group, names in (("DECISIVE", DECISIVE), ("ADJACENT (not part of verdict)", ADJACENT)):
        print(f"  [{group}]")
        for name in names:
            h = hits[name]
            print(f"    {name:22s} {len(h):4d}" + (f"   e.g. {h[0][0]}:{h[0][1]}" if h else ""))
    return sum(len(hits[n]) for n in DECISIVE)


# ---------------------------------------------------------------- positive control
with tempfile.TemporaryDirectory() as td:
    fx = pathlib.Path(td) / "RxCounterRegistry_FIXTURE.h"
    fx.write_text(FIXTURE)
    pc_hits = scan([fx])
    pc_decisive = report("POSITIVE CONTROL (synthetic E11 impl fixture)", pc_hits, 1)
    dead = [n for n, h in pc_hits.items() if not h]
    if dead:
        print(f"\n  *** DETECTOR BROKEN: token class(es) did not fire on the fixture: {dead}")
        print("  *** No claim is made about Source/. Exit 2.")
        sys.exit(2)
    print(f"  POSITIVE CONTROL FIRED: all {len(TOKENS)} token classes matched "
          f"({pc_decisive} decisive hits).")

# ---------------------------------------------------------------- real surface
files = sorted({p for g in IMPL_GLOBS for p in ROOT.glob(g)})
print(f"\n=== SEARCH SURFACE (worktree root: {ROOT}) ===")
print(f"globs: {', '.join(IMPL_GLOBS)}")
for p in files:
    print(f"  {p.relative_to(ROOT)}")
real_hits = scan(files)
real_decisive = report("REAL IMPLEMENTATION SURFACE (Source/)", real_hits, len(files))

for group, names in (("DECISIVE", DECISIVE), ("ADJACENT", ADJACENT)):
    for name in names:
        for path, ln, txt in real_hits[name]:
            print(f"    [{group}] {name}: {pathlib.Path(path).relative_to(ROOT)}:{ln}: {txt}")

print()
if real_decisive == 0:
    print("  RESULT: PRODUCT_AUTHORITY_PATH_ABSENT — 0 DECISIVE E11 hits across")
    print(f"          {len(files)} implementation files, with a detector proven able to fire.")
    print("          (ADJACENT hits exist and are NOT an E11 gate — see report.)")
    sys.exit(1)
print(f"  RESULT: E11 implementation surface PRESENT — {real_decisive} decisive hits.")
sys.exit(0)

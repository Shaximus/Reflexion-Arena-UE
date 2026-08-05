"""Exercise the REAL E11 write-scope gate — ARM-08's C++, on the host toolchain.

For the whole of this cycle the E11 admission path could only be reported
ABSENT (0 occurrences on 10 implementation surfaces) or, once ARM-08 landed
RxCounterAuthority, PRESENT_BUT_UNEXERCISED — because exercising C++ was
believed to need a built editor.

It does not. ARM-08 wrote the gate as a deliberately UE-free translation unit
("no CoreMinimal.h, no FString ... a plain C++ predicate that can be linked and
exercised by a host-toolchain test without an engine build"). So this compiles
the actual shipped .cpp with g++ and calls AdmitCounterWrite() directly. It is
NOT a re-run of ARM-08's own test: the cases and the expected verdicts were
derived from RX_SKILL_ENUMS_V1.md §4.0/§4.1.

TWO INDEPENDENT QUESTIONS, and conflating them is the whole trap:

  (1) Is the gate predicate CORRECT?  Does it refuse boss_stability and admit
      boss_release_delay — the discriminating pair? Refusing both is a
      vocabulary limit, not a gate; admitting both is a breach.

  (2) Is the gate REACHABLE?  A correct predicate no caller consults protects
      nothing. This is the same distinction the Python-mirror control was built
      to make ("the exploit is UNREACHABLE, not GATED"), one layer up.

Measured 2026-08-05: (1) YES, 18/18, and 7/7 mutations of the product TU were
detected with distinct signatures. (2) NO — FRxSimWorld::ParseSkillSpec reads
exactly six fields (name, trigger, effect, cost, cooldown, commit_window) and
never touches `effects`, so FRxSkillSpec::Effects is always empty on the only
player-facing path (author_skill -> ParseSkillSpec -> AuthorSkill ->
ValidateSpec) and the gate loop iterates nothing. A spec carrying an `effects`
array is not refused — it is SILENTLY DROPPED.

Exit codes:  0 = predicate correct AND reachable from the player-facing parser
             1 = predicate WRONG — exploit admitted, or canon case refused too
             2 = cannot evaluate (no C++ toolchain, or no gate source found).
                 Never reported as a pass.
             3 = predicate correct but NOT reachable. Not a pass and not a
                 breach: the exploit is currently blocked by a parser that
                 cannot express it, not by the gate.

Source resolution: the worktree's Source/ tree if it carries the gate, else
`git show $RX_E11_REF:<path>` (default origin/arm/e11-authority-impl-v1), so
this runs before the implementation branch is merged and keeps running after.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
DRIVER = HERE / "e11_gate_driver.cpp"
SIM = "Source/ReflexionArena/Sim"
REF = os.environ.get("RX_E11_REF", "origin/arm/e11-authority-impl-v1")

GATE_FILES = ("RxCounterAuthority.h", "RxCounterAuthority.cpp")
# The player-facing path, and the field the parser must read for the gate to be
# reachable at all.
PARSER_FILE = "RxSimWorld.cpp"
SPEC_HEADER = "RxSkillSystem.h"


def inconclusive(reason, detail=""):
    print(f"\n  CANNOT EVALUATE — {reason}")
    if detail:
        print(f"  {detail}")
    print("\n  exit 2 — the gate was not exercised, so nothing below is a pass.")
    sys.exit(2)


def fetch(name, dest):
    """Prefer the working tree; fall back to reading the ref out of git."""
    live = REPO / SIM / name
    if live.is_file():
        shutil.copy(live, dest / name)
        return f"worktree {SIM}/{name}"
    p = subprocess.run(["git", "show", f"{REF}:{SIM}/{name}"],
                       cwd=REPO, capture_output=True, text=True)
    if p.returncode != 0:
        return None
    (dest / name).write_text(p.stdout)
    return f"{REF}:{SIM}/{name}"


def read_source(name):
    """Text of a sim file from the worktree or the ref; None if absent."""
    live = REPO / SIM / name
    if live.is_file():
        return live.read_text(errors="replace")
    p = subprocess.run(["git", "show", f"{REF}:{SIM}/{name}"],
                       cwd=REPO, capture_output=True, text=True)
    return p.stdout if p.returncode == 0 else None


def check_reachable():
    """Does any code path put an entry into FRxSkillSpec::Effects?

    The gate reads Spec.Effects. If nothing ever writes it, the gate is dead
    code. This is an ABSENCE claim, so it names its surface: the declared
    struct is a plain C++ struct (not a USTRUCT), so no reflection-based
    deserialiser can populate it, and the only hand-written parser on the
    player-facing author_skill path is ParseSkillSpec.

    Returns (reachable: bool|None, note: str). None means undetermined.
    """
    hdr = read_source(SPEC_HEADER)
    parser = read_source(PARSER_FILE)
    if hdr is None or parser is None:
        return None, f"could not read {SPEC_HEADER} / {PARSER_FILE}"

    reflected = "USTRUCT" in hdr and re.search(r"USTRUCT[^\n]*\)\s*\n\s*struct\s+FRxSkillEffect", hdr)
    if reflected:
        return None, ("FRxSkillEffect is a USTRUCT — a reflection-based "
                      "deserialiser could populate Effects without naming it; "
                      "this text scan cannot rule that out.")

    m = re.search(r"FRxSkillSpec\s+ParseSkillSpec\s*\([^)]*\)\s*\{(.*?)\n\t\}",
                  parser, re.S)
    if m is None:
        return None, "ParseSkillSpec not found in " + PARSER_FILE
    body = m.group(1)
    fields = sorted(set(re.findall(r'TEXT\("([a-z_]+)"\)', body)))
    populates = bool(re.search(r"\bEffects\b", body))
    return populates, (f"ParseSkillSpec reads {len(fields)} field(s): {fields}; "
                       f"writes Spec.Effects: {populates}")


def type_span(lines, type_name):
    """(first, last) 1-based line span of `struct/class <type_name>`, or None.

    Needed because a field name alone is NOT unique: RxBossEarthquake.h declares
    `int32 TremorStage` twice — once in FRxBossSnapshot (:64) and once in
    FRxBossEarthquake (:80). A cite check that ignores the `FRxBossEarthquake::`
    qualifier reports the first match and points the reader at the wrong struct.
    This check did exactly that on its first run.
    """
    start = None
    for i, l in enumerate(lines, 1):
        if re.match(rf"^\s*(struct|class)\s+(\w+\s+)?{re.escape(type_name)}\b", l):
            start = i
            break
    if start is None:
        return None
    for j in range(start, len(lines)):
        if re.match(r"^\};", lines[j]):
            return (start, j + 1)
    return (start, len(lines))


def check_cites(registry, hdr_cache):
    """Every registry SimCite must name the line the field is actually on.

    ARM-08's own drift checker (fleet_state/arms/ARM-08/tests/
    check_registry_matches_doc.py) parses `{ "id", true/false,` and compares the
    WRITABILITY column only — it never looks at the cite string. So a wrong
    file:line in the code registry is uncovered by it. A cite is how a reader
    finds the counter; a stale one sends them to the wrong field.

    The cite's `Type::Field` qualifier is honoured, not just the field name.
    """
    bad = []
    for cid, _writable, cite in registry:
        m = re.search(r"(?:(\w+)::)?(\w+)\s+(Rx\w+\.h):(\d+)", cite)
        if m is None:
            bad.append((cid, cite, "cite not parseable"))
            continue
        qual, field, fname, line = m.group(1), m.group(2), m.group(3), int(m.group(4))
        if fname not in hdr_cache:
            hdr_cache[fname] = read_source(fname)
        text = hdr_cache[fname]
        if text is None:
            bad.append((cid, cite, f"{fname} not readable"))
            continue
        lines = text.splitlines()
        if not (1 <= line <= len(lines)):
            bad.append((cid, cite, f"{fname} has {len(lines)} lines"))
            continue
        if re.search(rf"\b{re.escape(field)}\b", lines[line - 1]):
            continue

        decl = rf"^\s*\w+[\w:<>, ]*\s+{re.escape(field)}\b"
        where = [i + 1 for i, l in enumerate(lines) if re.search(decl, l)]
        span = type_span(lines, qual) if qual else None
        if span:
            scoped = [w for w in where if span[0] <= w <= span[1]]
            hint = (f" — {qual}::{field} is at :{scoped[0]}" if len(scoped) == 1
                    else f" — {len(scoped)} candidate(s) inside {qual} "
                         f"({span[0]}-{span[1]}): {scoped}")
        elif qual:
            hint = (f" — could not locate struct/class {qual}; "
                    f"unqualified matches at {where}") if where else ""
        else:
            hint = f" — unqualified matches at {where}" if where else ""
        bad.append((cid, cite, f"{fname}:{line} does not mention {field}{hint}"))
    return bad


def main():
    print("  E11 C++ WRITE-SCOPE GATE — real product TU on the host toolchain")
    print(f"  gate source ref : {REF} (worktree preferred)")

    cxx = shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        inconclusive("no C++ compiler on PATH (g++ / clang++)")
    if not DRIVER.is_file():
        inconclusive(f"driver missing: {DRIVER}")

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="rx_e11_"))
    origins = []
    for f in GATE_FILES:
        o = fetch(f, tmp)
        if o is None:
            inconclusive(
                f"{f} not found in {SIM}/ nor at {REF}",
                "PRODUCT_AUTHORITY_PATH_ABSENT — there is no gate to exercise. "
                "Set RX_E11_REF to the branch carrying it.")
        origins.append(o)
    for o in origins:
        print(f"    <- {o}")

    exe = tmp / "gate"
    c = subprocess.run([cxx, "-std=c++17", "-O0", "-Wall", "-I", str(tmp),
                        "-o", str(exe), str(tmp / "RxCounterAuthority.cpp"),
                        str(DRIVER)], capture_output=True, text=True)
    if c.returncode != 0:
        inconclusive("the gate TU did not compile standalone",
                     c.stderr.strip()[:800])

    p = subprocess.run([str(exe)], capture_output=True, text=True)
    if p.returncode != 0:
        inconclusive(f"driver exited {p.returncode}", p.stderr.strip()[:400])

    registry, failed, passed = [], [], 0
    for line in p.stdout.splitlines():
        m = re.match(r"REGISTRY \d+ (\S+)\s+a2_writable=(\S+)\s+cite=(.*)$", line)
        if m:
            registry.append((m.group(1), m.group(2) == "true", m.group(3).strip()))
            continue
        m = re.match(r"ASSERT\s+(\S+)\s+(PASS|FAIL)\s+got=(\S+)\s+want=(\S+)", line)
        if m:
            if m.group(2) == "FAIL":
                failed.append((m.group(1), m.group(3), m.group(4)))
            else:
                passed += 1

    if not registry or (passed + len(failed)) == 0:
        inconclusive("the driver produced no REGISTRY/ASSERT lines",
                     "harness not wired to the gate; any verdict would be empty.")

    print(f"\n  registry rows      : {len(registry)} "
          f"({sum(1 for r in registry if r[1])} A2-writable)")
    print(f"  assertions         : {passed} pass, {len(failed)} fail")
    for name, got, want in failed:
        print(f"      FAIL {name:28s} got={got} want={want}")

    # ---- (1) is the predicate correct? ----
    if failed:
        breach = [n for n, _, _ in failed
                  if n in ("exploit_refused", "prefix_not_admitted",
                           "truncation_not_admitted", "unknown_refused",
                           "empty_refused", "nullptr_refused",
                           "protected_prefix_safe", "suffix_not_admitted",
                           "case_not_admitted", "space_not_admitted")]
        print()
        if breach:
            print("  *** BREACH *** the gate admits a write it must refuse:")
            print(f"      {breach}")
        else:
            print("  *** GATE DEFECTIVE *** the predicate is wrong, though no")
            print("      protected counter was admitted in these cases.")
        print("\n  exit 1 — a real finding about the product.")
        return 1

    print("\n  PREDICATE CONFIRMED — the discriminating pair splits: boss_stability")
    print("  is refused as authority-owned, boss_release_delay is admitted, and the")
    print("  two rejection reasons stay distinguishable.")

    # ---- cite integrity: the column ARM-08's own drift check does not compare ----
    bad = check_cites(registry, {})
    print(f"\n  registry cite check: {len(registry) - len(bad)}/{len(registry)} resolve")
    for cid, cite, why in bad:
        print(f"      STALE {cid:24s} {why}")
    if bad:
        print("  A stale cite is a documentation defect, not an authority breach:")
        print("  it does not change any admission decision. Reported, not fatal.")

    # ---- (2) is the gate reachable? ----
    reachable, note = check_reachable()
    print(f"\n  reachability: {note}")
    if reachable is None:
        inconclusive("could not determine whether anything populates Spec.Effects",
                     note)
    if not reachable:
        print()
        print("  PRODUCT_AUTHORITY_GATE_UNREACHABLE — the predicate is correct and")
        print("  nothing on the player-facing path can reach it. author_skill ->")
        print("  ParseSkillSpec never reads `effects`, so FRxSkillSpec::Effects is")
        print("  always empty and the gate loop iterates nothing. A spec carrying an")
        print("  effects array is not REFUSED, it is SILENTLY DROPPED.")
        print("  boss_stability is safe today because the parser cannot express the")
        print("  attack — not because the gate refuses it. That is UNREACHABLE, not")
        print("  GATED, and the two must not be recorded as the same result.")
        print("\n  exit 3 — correct but not load-bearing. NOT a pass, NOT a breach.")
        return 3

    print("\n  GATE LIVE — the predicate is correct and the player-facing parser")
    print("  can reach it. The exploit is GATED, not merely unreachable.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

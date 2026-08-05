#!/usr/bin/env python3
"""Mutation matrix over Prime's attest control suite.

Closes the scope limit I published: "I mutation-tested ONE control ... my
mutation hit the absence path only."

For each mutation of attest.py, run the UNMODIFIED control suite and record
which controls fail. A control that never fails across a mutation aimed at its
own subject is decorative.
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

SRC = pathlib.Path("/home/shax/Projects/core-tech/PentaCLI/.claude/worktrees/"
                   "arm-00-fleet-foundry/fleet_state/foundry/tools")

MUTATIONS = [
    ("M0_unmutated", None, None,
     "control: the suite must pass 10/10 untouched"),

    ("M1_verdict_says_human",
     '"verdict": "ORIGIN_UNRESOLVED",\n            "reason": "no prior receipt',
     '"verdict": "HUMAN_TYPED_ORIGIN_UNRESOLVED",\n            "reason": "no prior receipt',
     "absence path claims a HUMAN origin — aimed at control #4"),

    ("M2_ignore_chain_integrity",
     '    chain = verify_chain()\n    if not chain["intact"]:',
     '    chain = verify_chain()\n    if False:',
     "attest() stops gating on chain integrity — aimed at 'tamper blocks attestation'"),

    ("M3_match_session_only",
     '        if r.get("session_id") == session and r.get("continuation_bytes_hash") == payload_hash:\n            return {"verdict": "MACHINE_CONTINUATION_ATTESTED",',
     '        if r.get("session_id") == session:\n            return {"verdict": "MACHINE_CONTINUATION_ATTESTED",',
     "attest() matches on session alone, ignoring payload — aimed at near-miss control"),

    ("M4_verify_always_intact",
     '    return {"status": "INTACT" if not broken else "TAMPERED",\n            "receipts": n, "intact": not broken, "breaks": broken}',
     '    return {"status": "INTACT", "receipts": n, "intact": True, "breaks": []}',
     "verify_chain() always reports INTACT — aimed at tamper and chain-break controls"),

    ("M5_no_prev_link",
     '        "previous_receipt_hash": prev,',
     '        "previous_receipt_hash": GENESIS,',
     "write_receipt() stops linking receipts — aimed at chain-break detection"),
    ("M6_never_attest",
     '            return {"verdict": "MACHINE_CONTINUATION_ATTESTED",',
     '            return {"verdict": "NEVER_ATTESTS_NOW",',
     "happy path broken: a genuine injection no longer attests — aimed at POSITIVE #1"),

    ("M7_good_chain_reports_tampered",
     '    return {"status": "INTACT" if not broken else "TAMPERED",\n            "receipts": n, "intact": not broken, "breaks": broken}',
     '    return {"status": "TAMPERED", "receipts": n, "intact": False, "breaks": broken}',
     "a GOOD chain now reports TAMPERED — aimed at POSITIVE #2"),

    ("M8_no_chain_attests",
     '        return {"status": "NO_CHAIN", "receipts": 0, "intact": False,',
     '        return {"status": "INTACT", "receipts": 0, "intact": True,',
     "missing chain now reports INTACT — aimed at the NO_CHAIN control"),
]

CTL_NAMES = [
    "POSITIVE  attested injection", "POSITIVE  chain intact",
    "CONTROL   human-typed bytes", "CONTROL   and does NOT claim",
    "CONTROL   near-miss payload", "CONTROL   tampered receipt",
    "CONTROL   tamper blocks", "SETUP     three-receipt",
    "CONTROL   deleted middle", "CONTROL   no chain",
]

rows = {}
for name, old, new, why in MUTATIONS:
    d = pathlib.Path(tempfile.mkdtemp())
    shutil.copy(SRC / "attest.py", d / "attest.py")
    shutil.copy(SRC / "test_attest_controls.py", d / "test_attest_controls.py")
    if old is not None:
        s = (d / "attest.py").read_text()
        if s.count(old) != 1:
            rows[name] = ("SKIPPED", f"anchor matched {s.count(old)}x", why, set())
            print(f"[{name}] SKIPPED — anchor matched {s.count(old)}x, mutation not applied")
            continue
        (d / "attest.py").write_text(s.replace(old, new, 1))
    p = subprocess.run([sys.executable, "test_attest_controls.py"], cwd=d,
                       capture_output=True, text=True)
    out = p.stdout
    failed = set()
    for line in out.splitlines():
        if line.startswith("FAIL"):
            for c in CTL_NAMES:
                if c in line:
                    failed.add(c)
    m = re.search(r"(\d+)/(\d+) controls passed", out)
    score = m.group(0) if m else "<no score>"
    rows[name] = (score, p.returncode, why, failed)
    print(f"[{name}] {score}  exit={p.returncode}   {why}")
    for f in sorted(failed):
        print(f"      caught by: {f}")
    if not failed and old is not None:
        crashed = "Traceback" in p.stderr or score == "<no score>"
        if crashed:
            print("      MUTATION CRASHED THE SUBJECT — tests nothing. The suite raised")
            print("      before controls could render a verdict. Choose a coherent mutation.")
        else:
            print("      *** NO CONTROL CAUGHT THIS MUTATION ***")

print()
print("=" * 78)
print("PER-CONTROL DISCRIMINATION — did each control ever fail?")
print("=" * 78)
ever = {c: [] for c in CTL_NAMES}
for name, (_, _, _, failed) in rows.items():
    for c in failed:
        ever[c].append(name)
dead = []
for c in CTL_NAMES:
    hits = ever[c]
    if not hits:
        dead.append(c)
    label = hits if hits else '*** NEVER OBSERVED FAILING — MAY BE UNTESTED ***'
    print(f"  {c:34s} failed under: {label}")
print()
print(f"controls never observed failing: {len(dead)}  {dead}")
print()
print("NEVER OBSERVED FAILING IS NOT DECORATIVE. It means no mutation in THIS")
print("matrix challenged that control. Three times in one session I read that")
print("as proof a control was inert; each time the mutation had failed to apply,")
print("crashed the subject, or aimed at a path it never reached. Before calling")
print("any control decorative, confirm a mutation aimed at ITS subject actually")
print("ran to completion and the control still passed.")

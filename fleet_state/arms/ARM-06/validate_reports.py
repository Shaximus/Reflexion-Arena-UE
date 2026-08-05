#!/usr/bin/env python3
"""Validate every ARM-06 report envelope the way strict admission does.

Written after D_admission_path/report.json was found unadmittable: it declared
evidence_root as .../D_admission_path while two entries used "../" to reach
C_e11_test_repair, and those two were the only evidence for one of its claims.
Every artifact existed and every hash was correct — the report still could not
be resolved. That failure mode is invisible to "do the files exist" and to
"do the hashes match", so it needs its own check.

Checks, per envelope:
  1. every required top-level key is present
  2. evidence_root is RELATIVE (never absolute)
  3. no evidence path escapes evidence_root — neither by a leading "../" nor by
     resolving outside it (symlinks, embedded "..")
  4. every evidence file exists
  5. every sha256 recomputes from disk
  6. every load-bearing claim (MACHINE_VERIFIED / INDEPENDENTLY_VERIFIED) is
     referenced by at least one evidence entry
  7. claim statuses are from the permitted set

Exit codes:  0 = every envelope admissible
             1 = at least one envelope has a defect
             2 = could not evaluate (no envelopes found, unreadable JSON)

usage:  python3 fleet_state/arms/ARM-06/validate_reports.py [-v]
"""
import hashlib
import json
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
WT = HERE.parents[2]
VERBOSE = "-v" in sys.argv

REQUIRED = ["report_version", "report_id", "arm_id", "goal_id", "generated_at",
            "state", "chat_url", "authority_ceiling", "evidence_root", "claims",
            "evidence", "artifacts", "tests", "decisions_requested", "risks",
            "next_action"]
STATUSES = {"CLAIMED", "MACHINE_VERIFIED", "INDEPENDENTLY_VERIFIED",
            "FALSIFIED", "UNKNOWN"}
LOAD_BEARING = {"MACHINE_VERIFIED", "INDEPENDENTLY_VERIFIED"}


def check(report_path):
    """Return (report_id, list_of_defects, stats)."""
    defects = []
    try:
        r = json.loads(report_path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        return report_path.name, [f"unreadable: {e}"], {}

    rid = r.get("report_id", f"<no report_id: {report_path}>")
    for k in REQUIRED:
        if k not in r:
            defects.append(f"missing required key: {k}")
    if defects:
        return rid, defects, {}

    root_rel = r["evidence_root"]
    if root_rel.startswith("/"):
        defects.append(f"evidence_root is ABSOLUTE: {root_rel}")
        return rid, defects, {}
    root = (WT / root_rel).resolve()

    for e in r["evidence"]:
        p = e.get("path", "")
        if p.startswith("/") or p.startswith(".."):
            defects.append(f"path escapes evidence_root: {p}")
            continue
        f = root / p
        try:
            f.resolve().relative_to(root)
        except ValueError:
            defects.append(f"path resolves outside evidence_root: {p}")
            continue
        if not f.is_file():
            defects.append(f"evidence file missing: {p}")
            continue
        actual = hashlib.sha256(f.read_bytes()).hexdigest()
        if actual != e.get("sha256"):
            defects.append(f"sha256 mismatch: {p}\n"
                           f"        declared {e.get('sha256')}\n"
                           f"        actual   {actual}")

    for c in r["claims"]:
        if c.get("status") not in STATUSES:
            defects.append(f"claim {c.get('claim_id')}: bad status {c.get('status')!r}")

    covered = {cid for e in r["evidence"] for cid in e.get("claim_ids", [])}
    for c in r["claims"]:
        if c.get("status") in LOAD_BEARING and c.get("claim_id") not in covered:
            defects.append(f"load-bearing claim has NO evidence: {c.get('claim_id')}")

    stats = {
        "claims": len(r["claims"]),
        "evidence": len(r["evidence"]),
        "load_bearing": sum(1 for c in r["claims"] if c.get("status") in LOAD_BEARING),
        "unknown": [c["claim_id"] for c in r["claims"] if c.get("status") == "UNKNOWN"],
        "root": root_rel,
    }
    return rid, defects, stats


def main():
    reports = sorted(HERE.glob("*/report.json"))
    if not reports:
        print("  no report.json found under fleet_state/arms/ARM-06/*/")
        print("  exit 2 — nothing validated, which is not the same as all valid.")
        return 2

    print(f"  ARM-06 report envelopes — {len(reports)} found, worktree {WT.name}")
    print(f"  {'report_id':40s} {'claims':6s} {'ev':3s} {'LB':3s} verdict")
    print("  " + "-" * 92)

    total = 0
    for rp in reports:
        rid, defects, stats = check(rp)
        total += len(defects)
        verdict = "ADMISSIBLE" if not defects else f"*** {len(defects)} DEFECT(S) ***"
        if stats:
            print(f"  {rid:40s} {stats['claims']:<6d} {stats['evidence']:<3d} "
                  f"{stats['load_bearing']:<3d} {verdict}")
            if VERBOSE:
                print(f"      root={stats['root']}  UNKNOWN={stats['unknown']}")
        else:
            print(f"  {rid:40s} {'?':6s} {'?':3s} {'?':3s} {verdict}")
        for d in defects:
            print(f"      {d}")

    print()
    print(f"  total defects across all envelopes: {total}")
    if total:
        print("  A report whose evidence cannot be RESOLVED is unadmittable even when")
        print("  every artifact exists and every hash is correct.")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())

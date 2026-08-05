#!/usr/bin/env python3
"""ARM-06 independent audit of ARM-00's continuation attestation chain.

Kestrel V2: "Arm 05 verifies from a read-only path the loop cannot modify."

This does NOT import or call attest.py. The hashing rule was read from the
source and REIMPLEMENTED here, because re-running an instrument does not test
the instrument.

Rule as documented in attest.py:
    receipt_hash = sha256(json.dumps({body without receipt_hash},
                                     sort_keys=True, separators=(',',':')))
    previous_receipt_hash links each receipt to the prior one
    GENESIS = "0"*64
    injection_id = sha256(session + prev + payload)[:16]
"""
import hashlib
import json
import os
import pathlib
import shutil
import tempfile

CHAIN = pathlib.Path.home() / ".reflexion-attest" / "continuation_chain.jsonl"
GENESIS = "0" * 64


def rh(r):
    body = {k: v for k, v in r.items() if k != "receipt_hash"}
    return hashlib.sha256(
        json.dumps(body, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def load(p):
    out = []
    for line in p.read_text().splitlines():
        if line.strip():
            out.append(json.loads(line))
    return out


def verify(recs):
    """My own linkage + hash check."""
    prev, bad = GENESIS, []
    for i, r in enumerate(recs):
        if r.get("previous_receipt_hash") != prev:
            bad.append((i, "LINK BROKEN", f"expected {prev[:12]} got "
                        f"{str(r.get('previous_receipt_hash'))[:12]}"))
        want = rh(r)
        if want != r.get("receipt_hash"):
            bad.append((i, "HASH MISMATCH", f"recomputed {want[:12]} declared "
                        f"{str(r.get('receipt_hash'))[:12]}"))
        prev = r.get("receipt_hash", prev)
    return bad


print("=" * 78)
print("(1) INDEPENDENT VERIFICATION — recomputed, not re-run")
print("=" * 78)
recs = load(CHAIN)
bad = verify(recs)
print(f"  receipts            : {len(recs)}")
print(f"  linkage/hash faults : {len(bad)}")
for b in bad:
    print(f"      {b}")

live = [r for r in recs if r.get("injection_id") == "cd027a618b1dae52"]
print(f"\n  live evidence injection_id cd027a618b1dae52 present: {bool(live)}")
if live:
    r = live[0]
    ok = rh(r) == r["receipt_hash"]
    print(f"    session          : {r['session_id']}")
    print(f"    orchestrator_pid : {r['orchestrator_pid']}")
    print(f"    transport        : {r['transport']}")
    print(f"    recomputed hash matches declared: {ok}")
    exp_id = hashlib.sha256(
        (r["session_id"] + r["previous_receipt_hash"]).encode()).hexdigest()[:16]
    print(f"    NOTE injection_id = sha256(session+prev+PAYLOAD)[:16]; payload is not")
    print(f"         in the receipt, so injection_id cannot be recomputed from the")
    print(f"         chain alone. Without the payload it is unverifiable, only")
    print(f"         self-consistent. (session+prev alone gives {exp_id}, != stored)")

print()
print("=" * 78)
print("(2) ATTACKS")
print("=" * 78)
tmp = pathlib.Path(tempfile.mkdtemp()) / "chain.jsonl"
shutil.copy(CHAIN, tmp)
work = load(tmp)

# ATTACK A — append a forged receipt for an injection that never happened.
print("\n  ATTACK A: append a fabricated receipt continuing from the real tail")
tail = work[-1]["receipt_hash"]
forged = {
    "continuation_bytes_hash": hashlib.sha256(b"I was never sent").hexdigest(),
    "continuation_bytes_len": 16,
    "injection_id": "f0" * 8,
    "orchestrator_pid": 999999,
    "previous_receipt_hash": tail,
    "session_id": "claudesquad_fabricated",
    "timestamp": "2026-08-05T23:59:59.000000+00:00",
    "transport": "tmux send-keys -l",
    "worktree": "/nonexistent",
}
forged["receipt_hash"] = rh(forged)
faults = verify(work + [forged])
print(f"    forged receipt appended -> chain faults: {len(faults)}")
print(f"    VERDICT: {'FORGERY DETECTED' if faults else 'FORGERY ACCEPTED — verifies clean'}")

# ATTACK B — rewrite history and re-link everything after it.
print("\n  ATTACK B: alter an OLD receipt, then recompute the whole tail")
w2 = json.loads(json.dumps(work))
w2[3]["session_id"] = "claudesquad_rewritten"
prev = w2[3]["previous_receipt_hash"]
for i in range(3, len(w2)):
    w2[i]["previous_receipt_hash"] = prev
    w2[i]["receipt_hash"] = rh(w2[i])
    prev = w2[i]["receipt_hash"]
faults = verify(w2)
print(f"    history rewritten + re-linked -> chain faults: {len(faults)}")
print(f"    VERDICT: {'REWRITE DETECTED' if faults else 'REWRITE ACCEPTED — verifies clean'}")

# ATTACK C — naive single-field tamper WITHOUT re-linking (their stated control)
print("\n  ATTACK C: tamper one field, do NOT re-link (the control they claim)")
w3 = json.loads(json.dumps(work))
w3[3]["orchestrator_pid"] = 1
faults = verify(w3)
print(f"    naive tamper -> chain faults: {len(faults)}")
print(f"    VERDICT: {'DETECTED (control holds)' if faults else 'MISSED'}")

# ATTACK D — is there any secret? If receipt_hash is keyless, anyone who can
# write the file can mint valid receipts.
print("\n  ATTACK D: is the hash keyed?")
src = pathlib.Path("/home/shax/Projects/core-tech/PentaCLI/.claude/worktrees/"
                   "arm-00-fleet-foundry/fleet_state/foundry/tools/attest.py").read_text()
keyed = any(t in src for t in ("hmac", "HMAC", "secret", "SECRET", "private_key", "sign("))
print(f"    hmac/secret/signature present in attest.py: {keyed}")
print(f"    VERDICT: {'keyed' if keyed else 'KEYLESS — receipt_hash is a plain sha256, so'}")
if not keyed:
    print("             any process that can WRITE the chain can mint receipts that")
    print("             verify, and can rewrite history consistently.")

print()
print("=" * 78)
print("(3) SAME-UID STORAGE")
print("=" * 78)
st = CHAIN.stat()
print(f"  chain uid={st.st_uid} mode={oct(st.st_mode)[-3:]}   my uid={os.getuid()}")
print(f"  I can write it: {os.access(CHAIN, os.W_OK)}")
print(f"  parent dir writable by me: {os.access(CHAIN.parent, os.W_OK)}")

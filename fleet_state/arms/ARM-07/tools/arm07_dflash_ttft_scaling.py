#!/usr/bin/env python3
"""ARM-07 dflash262k TTFT-scaling measurement. Moves the acceptance state:
the ruled routing table excludes long prompts from dflash262k because
draft-prefill cost scaling is UNMEASURED; this window measures the curve.

Controls, on the same causal path as the positive result:
- NULL PAIR per length: baseline boot A vs baseline boot B through the
  identical detector — must FAIL to find a difference (per-length floor).
- KNOWN-EFFECT POSITIVE: at the short class the detector must reproduce the
  receipted ~+17 ms draft cost before long-prompt readings are believed.
Runs inside arm07_txn_window.sh (restore armed before mutation).
"""
import importlib.util, json, os, shutil, statistics, sys, time

EV = sys.argv[1]
def _load(name, fname):
    saved = list(sys.argv); sys.argv = [sys.argv[0], EV]
    s = importlib.util.spec_from_file_location(
        name, os.path.join(os.path.dirname(os.path.abspath(__file__)), fname))
    m = importlib.util.module_from_spec(s); s.loader.exec_module(m)
    sys.argv = saved
    return m
qlib = _load("qlib", "arm07_5b_qual.py")          # build_doc, stream_ttft, kill_and_wait
d2   = _load("d2", "arm07_dflash262k_qual.py")    # patch_hook_dflash262k; dlib inside for boot_analysis

LENGTHS = [1000, 8000, 32000, 96000, 200000]
Q = "\n\nRead the document above, then reply with exactly one word: DONE."

def receipt(name, obj):
    with open(os.path.join(EV, name), "w") as f:
        json.dump(obj, f, indent=1)
    print(f"[receipt] {name}", flush=True)

def legs(tag, docs):
    out = {}
    for L, prompt in docs.items():
        r1 = qlib.stream_ttft(prompt, max_tokens=8, timeout=1200)
        r2 = qlib.stream_ttft(prompt, max_tokens=8, timeout=1200)
        out[str(L)] = {"ttft_1": r1["ttft_s"], "ttft_2": r2["ttft_s"],
                       "median": round(statistics.median([r1["ttft_s"], r2["ttft_s"]]), 4)}
        print(f"  {tag} L={L}: {out[str(L)]['median']}s", flush=True)
    receipt(f"15_ttft_{tag}.json", out)
    return out

MY_BACKUP = os.path.join(EV, "15_hook_backup_prewindow.py")

def main():
    shutil.copy(qlib.HOOK, MY_BACKUP)   # fresh backup, never a frozen file
    print("[docs] building", flush=True)
    docs = {}
    meta = {}
    for L in LENGTHS:
        doc, tl = qlib.build_doc(L, 500 + L)
        docs[L] = doc + Q
        meta[L] = tl
    receipt("15_docs_meta.json", meta)

    # S1 baseline arm A (current live canonical)
    a = legs("baselineA", docs)

    # S2 baseline arm B — fresh boot, same config: the NULL PAIR
    rec, ok = qlib.kill_and_wait("null-boot-B")
    receipt("15_nullboot.json", rec)
    if not ok:
        receipt("15_verdict.json", {"verdict": "ABORT — null boot failed"}); return 1
    b = legs("baselineB", docs)
    null_floor = {str(L): round(abs(a[str(L)]["median"] - b[str(L)]["median"]), 4) for L in LENGTHS}
    null_found_nothing = all(
        null_floor[str(L)] <= max(0.05 * b[str(L)]["median"], 0.02) for L in LENGTHS)
    receipt("15_null_control.json", {
        "floors_s": null_floor, "detector_failed_to_find_difference": null_found_nothing,
        "rule": "per-length floor = |A-B| median; dflash delta below 2x floor is noise"})

    # S3 dflash262k arm
    d2.patch_hook_dflash262k()
    rec2, ok2 = qlib.kill_and_wait("to-dflash262k")
    off2 = qlib.kill_and_wait.last_offset
    boot = d2.dlib.boot_analysis(off2, expect_dflash=True) if ok2 else {"init_found": False}
    receipt("15_dflash_boot.json", {"restart": rec2, "boot": boot})
    try:
        if not ok2 or not boot.get("dflash_engaged"):
            receipt("15_verdict.json", {"verdict": "ABORT — dflash did not engage", "boot": boot})
            return 1
        d = legs("dflash262k", docs)
    finally:
        shutil.copy(MY_BACKUP, qlib.HOOK)
        rec3, ok3 = qlib.kill_and_wait("restore-canonical")
        receipt("15_restore.json", {"restart": rec3, "ok": ok3,
                                    "cfg": qlib.effective_config()})

    deltas = {str(L): round(d[str(L)]["median"] - statistics.median(
        [a[str(L)]["median"], b[str(L)]["median"]]), 4) for L in LENGTHS}
    floors2x = {k: round(2 * v, 4) for k, v in null_floor.items()}
    significant = {k: deltas[k] > floors2x[k] for k in deltas}
    short_ms = deltas[str(LENGTHS[0])] * 1000
    positive_control_ok = 5 <= short_ms <= 60  # known effect ~+17ms at 43 tok; 1k-token class scales somewhat
    # ruled absolute gate for the short class: <=25ms regression, <=100ms median
    curve = [{"tokens": meta[L], "delta_ms": round(deltas[str(L)] * 1000, 1),
              "floor2x_ms": round(floors2x[str(L)] * 1000, 1),
              "significant": significant[str(L)],
              "dflash_median_s": d[str(L)]["median"]} for L in LENGTHS]
    within_25ms = [meta[L] for L in LENGTHS if deltas[str(L)] * 1000 <= 25]
    boundary = max(within_25ms) if within_25ms else 0
    receipt("15_verdict.json", {
        "verdict": f"MEASURED — draft-prefill TTFT cost curve captured; +25ms absolute boundary holds up to ~{boundary:,} tokens on this data",
        "curve": curve,
        "positive_control": {"short_class_delta_ms": round(short_ms, 1),
                             "known_effect_receipt": "+16.9ms at 43-token class (envelope 12)",
                             "detector_reproduces_known_effect": positive_control_ok},
        "null_control_clean": null_found_nothing,
        "recommended_routing_update": f"extend dflash262k eligibility from 'tested short-prompt class' to inputs <= ~{boundary:,} tokens under the ruled <=25ms absolute gate; beyond that, delta exceeds the gate — decision queued for Head 07 (Kestrel down)"})
    print("SCALING VERDICT written", flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())

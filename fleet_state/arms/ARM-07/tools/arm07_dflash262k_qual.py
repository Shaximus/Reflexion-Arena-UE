#!/usr/bin/env python3
"""ARM-07 dflash262k bounded comparison, per HEAD07 dual-verdict ruling:
qwen27-mtp-dflash262k = max_model_len 262144, all else canonical, dflash8 draft.
Acceptance: exact temp-0 digest identity vs in-window baseline; battery 18/18;
median decode uplift >=10%; TTFT regression <=10%; no startup loop/OOM/
assertion/recovery regression; stable across >=5 measured runs; draft
accept/reject stats sufficient to explain the gain.
Runs inside arm07_txn_window.sh (restore armed before mutation).
"""
import importlib.util, json, os, re, shutil, statistics, subprocess, sys, time, urllib.request

EV = sys.argv[1]
sys.argv = [sys.argv[0], EV]
_here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "dlib", os.path.join(_here, "arm07_dflash_qual.py"))
dlib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dlib)

def receipt(name, obj):
    with open(os.path.join(EV, name), "w") as f:
        json.dump(obj, f, indent=1)
    print(f"[receipt] {name}", flush=True)

def spec_metrics():
    try:
        with urllib.request.urlopen(dlib.BASE + "/metrics", timeout=10) as r:
            text = r.read().decode()
    except Exception as e:
        return {"error": repr(e)}
    out = {}
    for line in text.splitlines():
        if line.startswith("#"): continue
        for key in ("spec_decode_num_draft_tokens_total", "spec_decode_num_accepted_tokens_total",
                    "spec_decode_num_drafts_total"):
            if key in line:
                out[key] = out.get(key, 0.0) + float(line.rsplit(None, 1)[-1])
    return out

def qual(label):
    m0 = spec_metrics()
    q = dlib.warm_qualify(label)
    m1 = spec_metrics()
    delta = {k: m1.get(k, 0) - m0.get(k, 0) for k in m1 if isinstance(m1.get(k), float)}
    q["spec_decode_delta"] = delta
    if delta.get("spec_decode_num_draft_tokens_total"):
        q["acceptance_rate"] = round(
            delta.get("spec_decode_num_accepted_tokens_total", 0) /
            delta["spec_decode_num_draft_tokens_total"], 4)
    # per-run ttft from the qualify output file
    out = os.path.join(EV, f"11_{label}_qualify.json")
    try:
        runs = json.load(open(out)).get("runs", [])
        q["ttfts"] = [r.get("time_to_first_token_seconds") for r in runs]
        q["decode_rates"] = [r.get("decode_tokens_per_second") for r in runs]
    except Exception as e:
        q["runs_parse_error"] = repr(e)
    return q

def battery():
    script = os.path.join(EV, "..", "..", "HEAD-07", "evidence", "04_think_bench_script.py")
    out = os.path.join(EV, "13_dflash262k_think_bench.json")
    p = subprocess.run([sys.executable, script, out], capture_output=True, text=True)
    s = json.load(open(out)).get("summary", {}) if os.path.exists(out) else {}
    return {"rc": p.returncode, "summary": s,
            "thinking_true_18_of_18": (s.get("A_thinking_True", {}).get("strict_correct") == 10
                                       and s.get("B_thinking_True", {}).get("strict_correct") == 8)}

def patch_hook_dflash262k():
    dlib.patch_hook_dflash()          # spec-config -> dflash8 (compile-checked inside)
    src = open(dlib.HOOK).read()
    marker = "def apply_startup_tuning() -> None:"
    inject = (
        "def _arm07_dflash262k_maxlen(argv):\n"
        "    # ARM-07 dflash262k window: 1M does not fit beside the draft (49.41 vs 46.97 GiB);\n"
        "    # per HEAD07 ruling run the profile at native 262144.\n"
        "    try:\n"
        "        i = argv.index(\"--max-model-len\")\n"
        "    except ValueError:\n"
        "        return False\n"
        "    if i + 1 < len(argv) and argv[i + 1] == \"1048576\":\n"
        "        argv[i + 1] = \"262144\"\n"
        "        return True\n"
        "    return False\n\n\n" + marker)
    src = src.replace(marker, inject, 1)
    hook_call = "    changed = _rewrite_control_width(sys.argv)"
    src = src.replace(hook_call,
                      "    _arm07_dflash262k_maxlen(sys.argv)\n" + hook_call, 1)
    open(dlib.HOOK, "w").write(src)
    rc = subprocess.run([sys.executable, "-m", "py_compile", dlib.HOOK],
                       capture_output=True, text=True)
    if rc.returncode != 0:
        shutil.copy(dlib.HOOK_BACKUP, dlib.HOOK)
        raise RuntimeError(f"262k hook patch does not compile, restored: {rc.stderr}")

def main():
    shutil.copy(dlib.HOOK, dlib.HOOK_BACKUP)
    receipt("13_d0_snapshot.json", {"started": time.strftime("%FT%T%z"),
        "main_pid": dlib.mainpid(), "vram": dlib.vram(),
        "max_model_len": dlib.api("/v1/models", None, timeout=15)["data"][0]["max_model_len"]})

    base = qual("262kbaseline")
    receipt("13_baseline.json", base)

    patch_hook_dflash262k()
    r, off, ok = dlib.kill_and_wait("to-dflash262k")
    boot = dlib.boot_analysis(off, expect_dflash=True) if ok else {"init_found": False}
    receipt("13_dflash262k_boot.json", {"restart": r, "boot": boot})
    try:
        if not ok or not boot.get("init_found") or not boot.get("dflash_engaged"):
            tail = dlib.boot_segment(off)[-3000:] if ok is not None else ""
            receipt("13_verdict.json", {"verdict": "REJECT — dflash262k failed to boot or engage",
                                        "boot": boot, "error_tail": tail})
            return 0
        cold = dlib.cold_probe()
        cand = qual("dflash262k")
        bat = battery()
        receipt("13_dflash262k_semantic.json", bat)
        r2, off2, ok2 = dlib.kill_and_wait("dflash262k-recovery-cycle")
        boot2 = dlib.boot_analysis(off2, expect_dflash=True) if ok2 else {}
        receipt("13_dflash262k_recovery.json", {"restart": r2, "recovered": ok2,
                                                "boot": boot2})
        cand2_short = qual("dflash262k_postrecovery3") if ok2 else {}
    finally:
        shutil.copy(dlib.HOOK_BACKUP, dlib.HOOK)
        r3, off3, ok3 = dlib.kill_and_wait("restore-canonical")
        receipt("13_restore.json", {"restart": r3, "ok": ok3,
            "boot": dlib.boot_analysis(off3, expect_dflash=False) if ok3 else None})

    bs, cs = base["summary"], cand["summary"]
    b_med = bs.get("median_decode_tokens_per_second", 0)
    c_med = cs.get("median_decode_tokens_per_second", 0)
    uplift = (c_med / b_med - 1) * 100 if b_med else None
    b_ttft = statistics.median([t for t in base.get("ttfts", []) if t] or [0])
    c_ttft = statistics.median([t for t in cand.get("ttfts", []) if t] or [0])
    ttft_reg = (c_ttft / b_ttft - 1) * 100 if b_ttft else None
    gates = {
        "digest_identity": bs.get("content_sha256") == cs.get("content_sha256"),
        "battery_18_18": bat["thinking_true_18_of_18"],
        "median_decode_uplift_pct": round(uplift, 2) if uplift is not None else None,
        "uplift_gate_10pct": uplift is not None and uplift >= 10,
        "ttft_regression_pct": round(ttft_reg, 2) if ttft_reg is not None else None,
        "ttft_gate_10pct": ttft_reg is not None and ttft_reg <= 10,
        "recovery_ok": ok2, "restore_ok": ok3,
        "stable_5_runs": cs.get("runs") == 5 and (cs.get("stdev_decode_tokens_per_second") or 99) < max(1.0, 0.02 * c_med),
        "acceptance_stats": {"baseline": base.get("acceptance_rate"), "dflash": cand.get("acceptance_rate"),
                             "dflash_delta": cand.get("spec_decode_delta")},
        "marlin_fallback": boot.get("marlin_fallback"),
        "baseline_mean": bs.get("mean_decode_tokens_per_second"),
        "dflash_mean": cs.get("mean_decode_tokens_per_second")}
    if not gates["digest_identity"] or not gates["battery_18_18"]:
        verdict = "REJECT — identity or semantics failed (ruling: reject immediately)"
    elif not gates["recovery_ok"] or gates["marlin_fallback"]:
        verdict = "REJECT — recovery regression or Marlin fallback"
    elif gates["uplift_gate_10pct"] and gates["ttft_gate_10pct"] and gates["stable_5_runs"]:
        verdict = f"RETAIN as opt-in dflash262k profile — +{uplift:.1f}% median decode, TTFT {ttft_reg:+.1f}%"
    elif uplift is not None and uplift < 10:
        verdict = f"REJECT — correctness passes but gain {uplift:+.1f}% < 10% (operationally unjustified per ruling)"
    else:
        verdict = "REJECT — gate combination not met"
    receipt("13_verdict.json", {"verdict": verdict, "gates": gates})
    print("DFLASH262K VERDICT:", verdict, flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())

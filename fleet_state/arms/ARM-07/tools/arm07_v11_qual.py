#!/usr/bin/env python3
"""ARM-07 V11 NLL characterization of 5b, per HEAD07 dual-verdict ruling:
'Run the existing V11 teacher-forced-NLL equivalence test against the fresh
in-window baseline. Do not invent a new tolerance.'

Reuses the foundry verifier's OWN functions (measure_nll, run_quality, cases,
passages, NLL_REL_TOL) by importing verify_yarn_1m.py verbatim. The verifier's
YaRN-apply-specific guards (pre-1M baseline requirement, frozen engine
fingerprint) are deliberately NOT applied, because here the engine change IS
the candidate; this is documented, not hidden. Pass rule is V11's own,
verbatim: no newly-failing quality case AND score >= baseline AND
mean NLL <= baseline * (1 + NLL_REL_TOL).
Runs inside arm07_txn_window.sh (restore armed before mutation).
"""
import importlib.util, json, os, shutil, sys, time

EV = sys.argv[1]
_saved_argv = list(sys.argv)
sys.argv = [sys.argv[0], EV]
_here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "hooklib", os.path.join(_here, "arm07_needle_corrected.py"))
hooklib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hooklib)

sys.argv = [_saved_argv[0]]  # verifier parses argv in main() only; keep import clean
vpath = ("/home/shax/Projects/core-tech/PentaCLI/.claude/worktrees/arm-00-fleet-foundry/"
         "fleet_state/foundry/evidence/head07/yarn-1m/verify_yarn_1m.py")
vspec = importlib.util.spec_from_file_location("v11lib", vpath)
v11 = importlib.util.module_from_spec(vspec)
vspec.loader.exec_module(v11)

def receipt(name, obj):
    with open(os.path.join(EV, name), "w") as f:
        json.dump(obj, f, indent=1)
    print(f"[receipt] {name}", flush=True)

def characterize(tag):
    nll = v11.measure_nll()
    score, detail = v11.run_quality()
    rec = {"tag": tag, "nll": nll, "quality_score": score,
           "quality_detail": {p[:60]: {"ok": d["ok"], "truncated": d["truncated"]}
                              for p, d in detail.items()},
           "at": time.strftime("%FT%T%z")}
    receipt(f"12_v11_{tag}.json", rec)
    return nll, score, detail

def main():
    # baseline arm: current canonical, live
    b_nll, b_score, b_detail = characterize("baseline")
    if b_nll["mean"] is None:
        receipt("12_v11_verdict.json", {"verdict": "INVALID — baseline NLL unusable (bad logprobs); characterization cannot proceed", "baseline_nll": b_nll})
        return 1

    hooklib.patch_hook_5b()
    rec, ok = hooklib.kill_and_wait("v11-to-5b")
    cfg = hooklib.effective_config()
    receipt("12_v11_5b_boot.json", {"restart": rec, "cfg": cfg})
    try:
        if not ok or not cfg.get("prefix_caching") or cfg.get("max_seq_len") != 1048576:
            receipt("12_v11_verdict.json", {"verdict": "ABORT — 5b invariants not met", "cfg": cfg})
            return 1
        c_nll, c_score, c_detail = characterize("5b")
    finally:
        shutil.copy(hooklib.HOOK_BACKUP, hooklib.HOOK)
        rrec, rok = hooklib.kill_and_wait("v11-restore")
        receipt("12_v11_restore.json", {"restart": rrec, "ok": rok,
                                        "cfg": hooklib.effective_config()})

    regressed = [p[:56] for p, d in b_detail.items()
                 if d["ok"] and not c_detail.get(p, {}).get("ok")]
    tol = v11.NLL_REL_TOL
    if c_nll["mean"] is None:
        verdict = "INVALID — 5b NLL unusable (bad logprobs); re-run required"
        klass = "unchanged (EXPERIMENTAL)"
    else:
        delta = (c_nll["mean"] / b_nll["mean"] - 1) * 100
        within = (not regressed) and c_score >= b_score and \
                 c_nll["mean"] <= b_nll["mean"] * (1 + tol)
        if regressed:
            verdict, klass = "REJECT — quality-case regression", "REJECTED — remove profile"
        elif within:
            verdict, klass = f"WITHIN TOLERANCE — NLL {b_nll['mean']:.4f} -> {c_nll['mean']:.4f} ({delta:+.2f}% vs +{tol:.0%}); quality {b_score:.3f} -> {c_score:.3f}", "PROMOTE to QUALIFIED OPT-IN"
        else:
            verdict, klass = f"OUTSIDE TOLERANCE — NLL {b_nll['mean']:.4f} -> {c_nll['mean']:.4f} ({delta:+.2f}% vs +{tol:.0%}); quality {b_score:.3f} -> {c_score:.3f}", "retain EXPERIMENTAL only (explicit per-job selection)"
    receipt("12_v11_verdict.json", {
        "verdict": verdict, "classification_per_ruling": klass,
        "tolerance_used": f"existing NLL_REL_TOL={tol} — no new tolerance invented",
        "guards_not_applied": "verifier's pre-1M-baseline and frozen-engine-fingerprint guards are YaRN-apply-specific; here the engine change IS the candidate (documented per ruling)",
        "regressed_cases": regressed, "restore_ok": rok})
    print("V11 VERDICT:", verdict, flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())

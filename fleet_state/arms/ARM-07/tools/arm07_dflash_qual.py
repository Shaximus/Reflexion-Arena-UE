#!/usr/bin/env python3
"""ARM-07 DFlash-vs-MTP bounded comparison (Prime dispatch, under HEAD07
ownership ruling). Runs INSIDE the measurement-window flock — never directly.

Design, per dispatch:
- identical prompts, warm AND cold controls on BOTH arms, warmups discarded and
  said so in the receipt;
- NULL CONTROL FIRST: baseline vs baseline across two boots through the same
  harness — it must FAIL to find a difference (within the known boot-to-boot
  variance floor) before any DFlash delta is reported;
- causal-path confirmation: dflash/parallel-drafting lines in the boot log and
  spec-decode counters; explicit Marlin-fallback check;
- VRAM, KV-pool, model-residency deltas;
- one candidate only (dflash8, the pilot winner); adopt/reject + rollback.
Stages: D0 snapshot -> D1 baseline boot A (cold+warm) -> D2 baseline boot B
(cold+warm, null control) -> D3 mutate to dflash8 -> D4 candidate (cold+warm,
correctness) -> D5 restore -> D6 verdict.
"""
import json, os, re, shutil, subprocess, sys, time, urllib.request

BASE = "http://127.0.0.1:8010"
MODEL = "qwen27-mtp"
EV = sys.argv[1]
HOOK = "/home/shax/Projects/pentarchy/local-inference/deploy/python_startup/qwen_mtp_boot_tuning.py"
HOOK_BACKUP = os.path.join(EV, "11_hook_backup_prewindow.py")
LOG = "/var/log/qwen27-mtp.log"
DRAFT = "/home/shax/mnt_data/models/z-lab/Qwen3.5-27B-DFlash"

def sh(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()

def receipt(name, obj):
    with open(os.path.join(EV, name), "w") as f:
        json.dump(obj, f, indent=1)
    print(f"[receipt] {name}", flush=True)

def api(path_, body=None, timeout=600):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path_, data, {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)

def mainpid():
    return sh("systemctl show qwen27-mtp -p MainPID --value")[1]

def log_offset():
    return os.path.getsize(LOG)

def boot_segment(offset):
    """Log content appended since `offset` — the fresh boot's own lines, which
    cannot be prompt-injected because no request ran before we read it."""
    with open(LOG, "rb") as f:
        f.seek(offset)
        return f.read().decode("utf-8", "replace")

def kill_and_wait(tag, timeout=480):
    off = log_offset()
    pid = mainpid()
    t0 = time.monotonic()
    rc, _, err = sh(f"kill -9 {pid}")
    rec = {"tag": tag, "old_pid": pid, "kill_rc": rc, "log_offset": off}
    ready = None
    while time.monotonic() - t0 < timeout:
        try:
            api("/v1/models", None, timeout=3); ready = time.monotonic() - t0; break
        except Exception:
            time.sleep(2)
    rec["api_ready_s"] = round(ready, 1) if ready else None
    rec["new_pid"] = mainpid()
    return rec, off, ready is not None

def cold_probe():
    """First real request after a boot — the cold control (graph/JIT warm paths
    not yet exercised by any request)."""
    t0 = time.monotonic()
    r = api("/v1/chat/completions", {"model": MODEL, "messages": [
        {"role": "user", "content": "Reply with exactly: COLD"}],
        "max_tokens": 8, "temperature": 0,
        "chat_template_kwargs": {"enable_thinking": False}}, timeout=180)
    return {"wall_s": round(time.monotonic() - t0, 2),
            "content": r["choices"][0]["message"]["content"]}

def warm_qualify(label):
    """2 warmup runs DISCARDED by the harness (--warmup-runs 2), 5 measured."""
    out = os.path.join(EV, f"11_{label}_qualify.json")
    rc, so, se = sh(
        "cd /home/shax/Projects/pentarchy/local-inference && "
        f"python3 scripts/qualify_mtp_endpoint.py --repeats 5 --warmup-runs 2 "
        f"--label arm07-dflash-{label} --output {out}")
    summ = {}
    if os.path.exists(out):
        summ = json.load(open(out)).get("summary", {})
    return {"rc": rc, "stderr_tail": se[-400:], "summary": summ,
            "warmups_discarded": 2}

def vram():
    rc, out, _ = sh("nvidia-smi --query-gpu=index,memory.used --format=csv,noheader")
    return out

def boot_analysis(offset, expect_dflash):
    seg = boot_segment(offset)
    init = [l for l in seg.splitlines() if "core.py:114" in l]
    a = {"init_found": bool(init)}
    if init:
        l = init[-1]
        a["spec_config"] = (re.search(r"speculative_config=([^,]+(?:,[^)]*?)?\))", l) or ["", ""])[0][:200]
        a["num_spec"] = (re.search(r"num_spec_tokens=(\d+)", l) or [None, None])[1]
        a["max_seq_len"] = (re.search(r"max_seq_len=(\d+)", l) or [None, None])[1]
        a["fp8"] = "fp8_per_channel" in l
    a["dflash_lines"] = [l[:160] for l in seg.splitlines() if re.search(r"dflash|parallel.draft", l, re.I)][:12]
    a["marlin_lines"] = [l[:160] for l in seg.splitlines() if re.search(r"marlin", l, re.I)][:12]
    a["kv_pool"] = ([l[:120] for l in seg.splitlines() if "KV cache size" in l] or [None])[-1]
    a["model_residency"] = ([l[:120] for l in seg.splitlines() if "Model loading took" in l] or [None])[-1]
    a["expected_dflash"] = expect_dflash
    a["dflash_engaged"] = bool(a["dflash_lines"]) if expect_dflash else None
    a["marlin_fallback"] = bool(a["marlin_lines"])
    return a

def patch_hook_dflash():
    src = open(HOOK).read()
    new = src.replace(
        '    config["num_speculative_tokens"] = TUNED_K\n'
        '    argv[value_index] = json.dumps(config, separators=(",", ":"))\n'
        "    return True",
        '    # ARM-07 DFlash window: replace MTP with the dflash draft model.\n'
        '    argv[value_index] = json.dumps({"method": "dflash",\n'
        f'        "model": "{DRAFT}",\n'
        '        "num_speculative_tokens": 8}, separators=(",", ":"))\n'
        "    return True", 1)
    if new == src:
        raise RuntimeError("dflash hook patch made no change")
    open(HOOK, "w").write(new)
    rc, _, se = sh(f"python3 -m py_compile {HOOK}")
    if rc != 0:
        shutil.copy(HOOK_BACKUP, HOOK)
        raise RuntimeError(f"patched hook does not compile, restored: {se}")

def main():
    t_start = time.strftime("%FT%T%z")
    shutil.copy(HOOK, HOOK_BACKUP)
    receipt("11_d0_snapshot.json", {
        "started": t_start, "main_pid": mainpid(), "vram": vram(),
        "models": api("/v1/models", None, timeout=15)["data"][0]["max_model_len"],
        "draft_artifact_sha256_first_gb": sh(f"head -c 1000000000 {DRAFT}/model.safetensors | sha256sum")[1],
        "draft_config": json.load(open(f"{DRAFT}/config.json")),
        "note_position_limit": "draft max_position_embeddings=262144, target serves 1048576 — clamp/failure beyond native is an expected observation, not tuned around"})

    # D1 baseline boot A
    r1, off1, ok1 = kill_and_wait("baseline-boot-A")
    if not ok1:
        receipt("11_verdict.json", {"verdict": "ABORT — baseline boot A failed", "r": r1}); return 1
    cold_a = cold_probe()
    ba = boot_analysis(off1, expect_dflash=False)
    warm_a = warm_qualify("baselineA")
    receipt("11_d1_baselineA.json", {"restart": r1, "cold": cold_a, "boot": ba, "warm": warm_a})

    # D2 baseline boot B — the NULL CONTROL arm
    r2, off2, ok2 = kill_and_wait("baseline-boot-B")
    if not ok2:
        receipt("11_verdict.json", {"verdict": "ABORT — baseline boot B failed", "r": r2}); return 1
    cold_b = cold_probe()
    bb = boot_analysis(off2, expect_dflash=False)
    warm_b = warm_qualify("baselineB")
    a_mean = warm_a["summary"].get("mean_decode_tokens_per_second", 0)
    b_mean = warm_b["summary"].get("mean_decode_tokens_per_second", 0)
    null_delta_pct = abs(a_mean - b_mean) / max(a_mean, 1e-9) * 100
    null_ctrl = {
        "boot_A_mean": a_mean, "boot_B_mean": b_mean,
        "null_delta_pct": round(null_delta_pct, 2),
        "digest_match": warm_a["summary"].get("content_sha256") == warm_b["summary"].get("content_sha256"),
        "interpretation": "same config, two boots, same harness — this delta is the detection floor; any DFlash claim below ~2x this floor is noise",
        "control_failed_to_find_difference": null_delta_pct < 5.0}
    receipt("11_d2_null_control.json", {"restart": r2, "cold": cold_b, "boot": bb,
                                        "warm": warm_b, "null_control": null_ctrl})

    # D3 mutate to dflash8
    patch_hook_dflash()
    r3, off3, ok3 = kill_and_wait("mutate-to-dflash8")
    d_boot = boot_analysis(off3, expect_dflash=True) if ok3 else {"init_found": False}
    receipt("11_d3_dflash_boot.json", {"restart": r3, "boot": d_boot})
    if not ok3 or not d_boot.get("init_found"):
        shutil.copy(HOOK_BACKUP, HOOK)
        r4, _, ok4 = kill_and_wait("restore-after-dflash-boot-failure")
        tail = boot_segment(off3)[-4000:] if ok3 is not None else ""
        receipt("11_verdict.json", {
            "verdict": "REJECT — dflash8 failed to boot on production vLLM 0.24.0 stack",
            "detail": "decision-quality null: the engine could not start with the dflash draft; error tail preserved",
            "error_tail": tail, "restore_ok": ok4,
            "rollback_command": f"cp {HOOK_BACKUP} {HOOK} && kill -9 $(systemctl show qwen27-mtp -p MainPID --value)"})
        return 0
    cold_d = cold_probe()
    warm_d = warm_qualify("dflash8")
    vram_d = vram()
    receipt("11_d4_dflash.json", {"cold": cold_d, "warm": warm_d, "vram": vram_d})

    # D5 restore baseline
    shutil.copy(HOOK_BACKUP, HOOK)
    r5, off5, ok5 = kill_and_wait("restore-baseline")
    post = warm_qualify("postrestore") if ok5 else {}
    receipt("11_d5_restore.json", {"restart": r5, "ok": ok5, "post": post,
                                   "boot": boot_analysis(off5, expect_dflash=False) if ok5 else None})

    # D6 verdict
    d_mean = warm_d["summary"].get("mean_decode_tokens_per_second", 0)
    best_base = max(a_mean, b_mean)
    delta_pct = (d_mean - best_base) / max(best_base, 1e-9) * 100
    floor = max(2 * null_ctrl["null_delta_pct"], 2.0)
    gates = {
        "null_control_clean": null_ctrl["control_failed_to_find_difference"],
        "dflash_engaged_in_log": d_boot.get("dflash_engaged"),
        "marlin_fallback": d_boot.get("marlin_fallback"),
        "digest_vs_baseline_match": warm_d["summary"].get("content_sha256") == warm_a["summary"].get("content_sha256"),
        "dflash_mean": d_mean, "best_baseline_mean": best_base,
        "delta_pct": round(delta_pct, 2), "detection_floor_pct": round(floor, 2),
        "cold_baselineA_s": cold_a["wall_s"], "cold_dflash_s": cold_d["wall_s"],
        "baseline_restored": ok5}
    if not gates["null_control_clean"]:
        verdict = "INVALID — null control found a difference between identical configs; harness cannot support a delta claim"
    elif not gates["dflash_engaged_in_log"]:
        verdict = "INVALID — dflash path not confirmed in boot log; measurement does not measure dflash"
    elif gates["marlin_fallback"]:
        verdict = "REJECT — Marlin fallback detected; native path not engaged, result would be meaningless"
    elif delta_pct > floor and gates["digest_vs_baseline_match"]:
        verdict = f"ADOPT-CANDIDATE — dflash8 +{delta_pct:.1f}% above detection floor with identical output; recommend promotion packet"
    elif delta_pct > floor:
        verdict = f"CONDITIONAL — +{delta_pct:.1f}% speed but OUTPUT DIGEST CHANGED; needs semantic-gate battery before adoption"
    else:
        verdict = f"REJECT — dflash8 delta {delta_pct:+.1f}% does not exceed detection floor {floor:.1f}%; MTP K=6 stays"
    receipt("11_verdict.json", {"verdict": verdict, "gates": gates,
        "rollback_command": f"cp fleet_state/arms/ARM-07/evidence/11_hook_backup_prewindow.py {HOOK} && kill -9 $(systemctl show qwen27-mtp -p MainPID --value)  # systemd auto-restarts to MTP K=6 baseline",
        "window": {"start": t_start, "end": time.strftime("%FT%T%z")}})
    print("VERDICT:", verdict, flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())

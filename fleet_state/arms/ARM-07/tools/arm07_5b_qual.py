#!/usr/bin/env python3
"""ARM-07 candidate-5b qualification driver, per HEAD07 ruling (Kestrel-Ack
ARM07-CKPT-20260805-0812). Runs INSIDE the measurement-window flock held by
arm07_window.sh — never invoke directly.

Stages: S0 rollback bundle -> S1 baseline matrix -> S2 mutate to 5b ->
S3 candidate matrix (+ semantic battery + needle pos/neg) -> S4 stability x3 ->
S5 restore baseline -> S6 verdict vs gates.
Every stage writes its own receipt JSON; nothing is suppressed.
"""
import json, os, random, re, shutil, subprocess, sys, threading, time, urllib.request

BASE = "http://127.0.0.1:8010"
MODEL = "qwen27-mtp"
EV = sys.argv[1]                      # evidence dir (absolute)
HOOK = "/home/shax/Projects/pentarchy/local-inference/deploy/python_startup/qwen_mtp_boot_tuning.py"
HOOK_BACKUP = os.path.join(EV, "10_hook_backup_prewindow.py")
LOG = "/var/log/qwen27-mtp.log"
NATIVE = 262144

def sh(cmd):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()

def receipt(name, obj):
    path = os.path.join(EV, name)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)
    print(f"[receipt] {name}", flush=True)

def api(path_, body=None, timeout=600):
    url = BASE + path_
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data, {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)

def tokenize_len(text):
    return len(api("/tokenize", {"model": MODEL, "prompt": text}, timeout=120)["tokens"])

def stream_ttft(prompt, max_tokens=8, timeout=900, thinking=False):
    body = {"model": MODEL, "messages": [{"role": "user", "content": prompt}],
            "max_tokens": max_tokens, "temperature": 0.0, "stream": True,
            "chat_template_kwargs": {"enable_thinking": thinking}}
    req = urllib.request.Request(BASE + "/v1/chat/completions", json.dumps(body).encode(),
                                 {"Content-Type": "application/json"})
    t0 = time.monotonic(); ttft = None
    with urllib.request.urlopen(req, timeout=timeout) as r:
        for raw in r:
            if ttft is None and raw.startswith(b"data:") and b'"content"' in raw:
                ttft = time.monotonic() - t0
    return {"ttft_s": round(ttft, 3) if ttft else None,
            "total_s": round(time.monotonic() - t0, 3)}

WORDS = ["structural", "instability", "propagation", "anchor", "recovery",
         "attenuation", "precursor", "resonance", "threshold", "cascade",
         "lattice", "dampening", "harmonic", "gradient", "keystone"]

def build_doc(target_tokens, seed):
    rng = random.Random(seed)
    words = [rng.choice(WORDS) for _ in range(2000)]
    ratio = tokenize_len(" ".join(words)) / 2000.0
    n = int(target_tokens / ratio)
    words = [rng.choice(WORDS) for _ in range(n)]
    doc = " ".join(words)
    tl = tokenize_len(doc)
    for _ in range(3):
        if 0.97 * target_tokens <= tl <= 1.06 * target_tokens: break
        n = int(n * target_tokens / tl)
        words = [rng.choice(WORDS) for _ in range(n)]
        doc = " ".join(words); tl = tokenize_len(doc)
    return doc, tl

def mainpid():
    return sh("systemctl show qwen27-mtp -p MainPID --value")[1]

def log_offset():
    return os.path.getsize(LOG)

def boot_segment(offset):
    """Log bytes appended after `offset`. Anchoring at the pre-kill offset makes
    the parse immune to vLLM-logged prompt text containing old log lines — the
    defect that killed a healthy 5b boot on run 1 (receipt
    10_rollback_bundle_correction.json)."""
    with open(LOG, "rb") as f:
        f.seek(offset)
        return f.read().decode("utf-8", "replace")

def kill_and_wait(tag, timeout=420):
    kill_and_wait.last_offset = log_offset()
    pid = mainpid()
    t0 = time.monotonic()
    rc, _, err = sh(f"kill -9 {pid}")
    rec = {"tag": tag, "old_pid": pid, "kill_rc": rc, "kill_err": err}
    ready = None
    while time.monotonic() - t0 < timeout:
        try:
            api("/v1/models", None, timeout=3); ready = time.monotonic() - t0; break
        except Exception:
            time.sleep(2)
    rec["api_ready_s"] = round(ready, 1) if ready else None
    if ready is None:
        rec["FAIL"] = "api never ready"
        return rec, False
    try:
        r = api("/v1/chat/completions", {"model": MODEL, "messages": [
            {"role": "user", "content": "Reply with exactly: RECOVERED"}],
            "max_tokens": 16, "temperature": 0,
            "chat_template_kwargs": {"enable_thinking": False}}, timeout=90)
        rec["completion"] = r["choices"][0]["message"]["content"]
        rec["first_completion_s"] = round(time.monotonic() - t0, 1)
    except Exception as e:
        rec["FAIL"] = f"completion: {e!r}"
        return rec, False
    rec["new_pid"] = mainpid()
    return rec, rec.get("completion") == "RECOVERED"

def effective_config(offset=None):
    if offset is None:
        offset = getattr(kill_and_wait, "last_offset", max(0, log_offset() - 9000000))
    seg = boot_segment(offset)
    lines = [l for l in seg.splitlines()
             if re.search(r"core.py:114|KV cache size|kv-offloading|KEEP_PREFIX|OffloadingConnector", l)]
    out = "\n".join(lines)
    cfg = {"raw_tail": lines[-8:], "anchor_offset": offset}
    init = [l for l in out.splitlines() if "core.py:114" in l]
    if init:
        l = init[-1]
        cfg["num_spec_tokens"] = int(re.search(r"num_spec_tokens=(\d+)", l).group(1))
        cfg["max_seq_len"] = int(re.search(r"max_seq_len=(\d+)", l).group(1))
        cfg["prefix_caching"] = "enable_prefix_caching=True" in l
        cfg["fp8"] = "fp8_per_channel" in l
    kv = [l for l in out.splitlines() if "KV cache size" in l]
    if kv:
        cfg["gpu_kv_tokens"] = int(kv[-1].split("size:")[1].split("tokens")[0].strip().replace(",", ""))
    return cfg

def memory_accounting():
    rc1, free_out, _ = sh("free -b | sed -n 2p")
    rc2, mem, _ = sh("grep -E 'Mlocked|Shmem:' /proc/meminfo")
    try:
        m = api("/metrics_text", None, timeout=10)
    except Exception:
        m = None
    rc3, met, _ = sh(f"curl -s --max-time 10 {BASE}/metrics | grep -E 'kv_offload_(store_bytes_total|cpu_cache_usage_perc)' | grep -v '#'")
    return {"free_b_line": free_out, "meminfo": mem, "kv_offload_metrics": met.splitlines()}

def qualify(label, repeats=5):
    out = os.path.join(EV, f"10_{label}_qualify.json")
    rc, so, se = sh(
        "cd /home/shax/Projects/pentarchy/local-inference && "
        f"python3 scripts/qualify_mtp_endpoint.py --repeats {repeats} --warmup-runs 2 "
        f"--label arm07-5bqual-{label} --output {out}")
    summ = {}
    if os.path.exists(out):
        d = json.load(open(out))
        summ = d.get("summary", d[-1].get("summary") if isinstance(d, list) else {})
    return {"rc": rc, "stderr_tail": se[-500:], "summary": summ}

def concurrency4(doc_short):
    results = []
    def one(i):
        t0 = time.monotonic()
        try:
            r = api("/v1/chat/completions", {"model": MODEL, "messages": [
                {"role": "user", "content": doc_short + f"\nRequest {i}: summarize the dominant word in one word."}],
                "max_tokens": 256, "temperature": 0,
                "chat_template_kwargs": {"enable_thinking": False}}, timeout=300)
            ct = r["usage"]["completion_tokens"]
            results.append({"i": i, "wall_s": round(time.monotonic() - t0, 2), "completion_tokens": ct})
        except Exception as e:
            results.append({"i": i, "error": repr(e)})
    t0 = time.monotonic()
    ts = [threading.Thread(target=one, args=(i,)) for i in range(4)]
    [t.start() for t in ts]; [t.join() for t in ts]
    wall = time.monotonic() - t0
    toks = sum(r.get("completion_tokens", 0) for r in results)
    return {"wall_s": round(wall, 2), "aggregate_tok_s": round(toks / wall, 1), "requests": results}

def matrix(tag, docs):
    m = {"tag": tag, "started": time.strftime("%FT%T%z")}
    m["short"] = qualify(tag)
    m["cold_50k"] = stream_ttft(docs["p50k"])
    m["rep_50k"] = stream_ttft(docs["p50k"])
    m["cold_300k"] = stream_ttft(docs["p300k"], timeout=1800)
    m["rep_300k"] = stream_ttft(docs["p300k"], timeout=1800)
    m["conc4"] = concurrency4(docs["short_doc"])
    m["finished"] = time.strftime("%FT%T%z")
    receipt(f"10_matrix_{tag}.json", m)
    return m

def patch_hook_5b():
    src = open(HOOK).read()
    a = re.sub(
        r"def disable_single_stream_kv_offload\(argv: MutableSequence\[str\]\) -> bool:\n(    .*\n)+?    return True\n",
        '''def disable_single_stream_kv_offload(argv: MutableSequence[str]) -> bool:
    # ARM-07 5b window: keep offload, 64 -> 128 GiB. Restore from 10_hook_backup_prewindow.py.
    try:
        index = argv.index("--kv-offloading-size")
    except ValueError:
        return False
    if index + 1 >= len(argv) or argv[index + 1] != CONTROL_KV_OFFLOAD_SIZE:
        return False
    argv[index + 1] = "128"
    return True
''', src, count=1)
    b = re.sub(
        r"def disable_mamba_prefix_cache_for_decode\(argv: MutableSequence\[str\]\) -> bool:\n(    .*\n|\n)+?    return True\n",
        '''def disable_mamba_prefix_cache_for_decode(argv: MutableSequence[str]) -> bool:
    # ARM-07 5b window: keep --enable-prefix-caching and mamba align.
    return False
''', a, count=1)
    if b == src:
        raise RuntimeError("hook patch made no change - regex failed")
    open(HOOK, "w").write(b)
    rc, _, se = sh(f"python3 -m py_compile {HOOK}")
    if rc != 0:
        shutil.copy(HOOK_BACKUP, HOOK)
        raise RuntimeError(f"patched hook does not compile, restored: {se}")

def needle_tests(docs):
    rng = random.Random(4242)
    code = f"ORCA-{rng.randint(100,999)}-BASALT-{rng.randint(1000,9999)}"
    doc, tl = docs["p300k"], docs["p300k_tokens"]
    words = doc.split(" ")
    pos = int(len(words) * 0.88)
    with_needle = " ".join(words[:pos] + [f"\nThe recovery codeword is {code}.\n"] + words[pos:])
    depth_tokens = tokenize_len(" ".join(words[:pos]))
    q = "\n\nWhat is the recovery codeword stated in the document above? Reply with only the codeword, or NONE if there is none."
    t0 = time.monotonic()
    r1 = api("/v1/chat/completions", {"model": MODEL, "messages": [
        {"role": "user", "content": with_needle + q}], "max_tokens": 800, "temperature": 0,
        "chat_template_kwargs": {"enable_thinking": True}}, timeout=1800)
    pos_out = (r1["choices"][0]["message"]["content"] or "").strip()
    t1 = time.monotonic()
    r2 = api("/v1/chat/completions", {"model": MODEL, "messages": [
        {"role": "user", "content": doc + q}], "max_tokens": 800, "temperature": 0,
        "chat_template_kwargs": {"enable_thinking": True}}, timeout=1800)
    neg_out = (r2["choices"][0]["message"]["content"] or "").strip()
    rec = {"code": code, "depth_tokens": depth_tokens, "beyond_native": depth_tokens > NATIVE,
           "haystack_tokens": tl, "positive_out": pos_out[:120], "positive_ok": code in pos_out,
           "positive_wall_s": round(t1 - t0, 1),
           "negative_out": neg_out[:120], "negative_clean": ("NONE" in neg_out.upper()) and (code not in neg_out),
           "negative_wall_s": round(time.monotonic() - t1, 1)}
    receipt("10_5b_needle.json", rec)
    return rec

def semantic_battery():
    script = os.path.join(EV, "..", "..", "HEAD-07", "evidence", "04_think_bench_script.py")
    out = os.path.join(EV, "10_5b_think_bench.json")
    rc, so, se = sh(f"python3 {script} {out}")
    d = json.load(open(out)) if os.path.exists(out) else {}
    s = d.get("summary", {})
    ok = (s.get("A_thinking_True", {}).get("strict_correct") == 10 and
          s.get("B_thinking_True", {}).get("strict_correct") == 8)
    return {"rc": rc, "stderr_tail": se[-400:], "summary": s, "thinking_true_18_of_18": ok}

def main():
    t_start = time.strftime("%FT%T%z")
    # S0 rollback bundle
    pid = mainpid()
    bundle = {"captured": t_start, "main_pid": pid}
    _, bundle["cmdline"], _ = sh(f"tr '\\0' ' ' < /proc/{pid}/cmdline")
    rc, envr, _ = sh(f"tr '\\0' '\\n' < /proc/{pid}/environ | sed -E 's/((KEY|TOKEN|SECRET|PASS)[^=]*=).*/\\1REDACTED/I'")
    bundle["environ"] = envr.splitlines()
    _, bundle["hashes"], _ = sh(
        "sha256sum /etc/systemd/system/qwen27-mtp.service /etc/systemd/system/qwen27-mtp.service.d/*.conf "
        f"{HOOK} /home/shax/Projects/pentarchy/local-inference/vllm-stock-venv/lib/python3.12/site-packages/qwen_mtp_boot_tuning.pth "
        "/home/shax/Projects/pentarchy/local-inference/runtime/qwen27-mtp-online-fp8.enabled "
        "/home/shax/mnt_data/models/Qwen/Qwen3.5-27B/config.json 2>&1")
    # S0 config from the server, not the log — logged prompt text can carry
    # old log lines (see 10_rollback_bundle_correction.json).
    bundle["effective_config"] = {
        "max_model_len_from_server": api("/v1/models", None, timeout=15)["data"][0]["max_model_len"]}
    bundle["memory"] = memory_accounting()
    receipt("10_rollback_bundle.json", bundle)
    shutil.copy(HOOK, HOOK_BACKUP)

    # docs (identical for both profiles)
    print("[docs] building", flush=True)
    p50k, t50k = build_doc(50000, 101)
    p300k, t300k = build_doc(300000, 202)
    short_doc, tshort = build_doc(1500, 303)
    q = "\n\nRead the document above, then reply with exactly one word: DONE."
    docs = {"p50k": p50k + q, "p300k": p300k + q, "p300k_tokens": t300k,
            "short_doc": short_doc}
    receipt("10_docs_meta.json", {"t50k": t50k, "t300k": t300k, "tshort": tshort})

    # S1 baseline matrix
    base = matrix("baseline", docs)

    # S2 mutate to 5b
    patch_hook_5b()
    rec, ok = kill_and_wait("mutate-to-5b")
    receipt("10_restart_to_5b.json", rec)
    cfg5b = effective_config()
    receipt("10_5b_effective_config.json", cfg5b)
    if not ok or not cfg5b.get("prefix_caching") or cfg5b.get("num_spec_tokens") != 6 \
       or cfg5b.get("max_seq_len") != 1048576 or not cfg5b.get("fp8"):
        # stability/config failure: restore and stop, per ruling
        shutil.copy(HOOK_BACKUP, HOOK)
        rec2, ok2 = kill_and_wait("restore-after-5b-failure")
        receipt("10_verdict.json", {"verdict": "REJECT — 5b failed to come up with required invariants",
                                    "restart": rec, "cfg": cfg5b, "restore": rec2, "restore_ok": ok2})
        return 1
    mem5b = memory_accounting()
    receipt("10_5b_memory.json", mem5b)

    # S3 candidate matrix + semantic + needle
    cand = matrix("5b", docs)
    sem = semantic_battery()
    receipt("10_5b_semantic.json", sem)
    ndl = needle_tests(docs)
    mem5b_after = memory_accounting()
    receipt("10_5b_memory_after_load.json", mem5b_after)

    # S4 stability x3
    stab = []
    all_ok = True
    for i in range(3):
        r, ok = kill_and_wait(f"stability-{i+1}")
        stab.append(r); all_ok = all_ok and ok
    receipt("10_5b_stability.json", {"cycles": stab, "all_recovered": all_ok})

    # S5 restore baseline
    shutil.copy(HOOK_BACKUP, HOOK)
    rrec, rok = kill_and_wait("restore-baseline")
    rcfg = effective_config()
    post = qualify("postrestore", repeats=3)
    receipt("10_restore_final.json", {"restart": rrec, "ok": rok, "cfg": rcfg, "post_qualify": post})

    # S6 verdict
    def g(m, k): return (m.get(k) or {}).get("ttft_s") or 1e9
    rep50_speedup = g(base, "rep_50k") / max(g(cand, "rep_50k"), 1e-9)
    rep300_speedup = g(base, "rep_300k") / max(g(cand, "rep_300k"), 1e-9)
    bs = base["short"]["summary"]; cs = cand["short"]["summary"]
    short_reg = None
    if bs.get("mean_decode_tokens_per_second") and cs.get("mean_decode_tokens_per_second"):
        short_reg = 1 - cs["mean_decode_tokens_per_second"] / bs["mean_decode_tokens_per_second"]
    digest_match = bs.get("content_sha256") == cs.get("content_sha256")
    gates = {
        "semantic_18_18": sem["thinking_true_18_of_18"],
        "needle_positive_beyond_native": ndl["positive_ok"] and ndl["beyond_native"],
        "needle_negative_clean": ndl["negative_clean"],
        "digest_match": digest_match,
        "rep50k_speedup": round(rep50_speedup, 2), "rep50k_gate_5x": rep50_speedup >= 5,
        "rep300k_speedup": round(rep300_speedup, 2), "rep300k_gate_3x": rep300_speedup >= 3,
        "short_decode_regression": round(short_reg, 4) if short_reg is not None else None,
        "short_regression_le_5pct": (short_reg is not None and short_reg <= 0.05),
        "stability_3x_recovered": all_ok,
        "baseline_restored": rok,
    }
    sem_ok = gates["semantic_18_18"] and gates["needle_positive_beyond_native"] and \
             gates["needle_negative_clean"] and gates["digest_match"]
    perf_ok = gates["rep50k_gate_5x"] and gates["rep300k_gate_3x"] and gates["short_regression_le_5pct"]
    stab_ok = gates["stability_3x_recovered"]
    if not (sem_ok and stab_ok):
        verdict = "REJECT — semantic or stability failure; baseline restored"
    elif perf_ok:
        verdict = "PASS — retain qwen27-mtp-longprefix as opt-in long-prefix profile"
    elif gates["rep50k_gate_5x"]:
        verdict = "EXPERIMENTAL — correctness passes; benefit proven at repeated 50k only"
    else:
        verdict = "REJECT — performance gates not met"
    receipt("10_verdict.json", {"verdict": verdict, "gates": gates,
                                "window": {"start": t_start, "end": time.strftime("%FT%T%z")}})
    print(f"VERDICT: {verdict}", flush=True)
    return 0

if __name__ == "__main__":
    sys.exit(main())

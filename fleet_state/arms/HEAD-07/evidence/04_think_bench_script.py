#!/usr/bin/env python3
"""HEAD-07 thinking on/off benchmark.
Two tasks, both mechanically scored, temperature 0, seeded variants:
  A: short rigid-contract selection task (~<100 input tokens)
  B: ~24k-char log comprehension -> rigid JSON extraction
Modes: enable_thinking true/false via chat_template_kwargs.
Strict score: message.content parses as JSON directly and equals ground truth.
Lenient score: same after stripping one ```/```json fence pair.
"""
import json, time, random, string, sys, urllib.request, urllib.error

BASE = "http://127.0.0.1:8010/v1/chat/completions"
MODEL = "qwen27-mtp"
OUT = sys.argv[1] if len(sys.argv) > 1 else "think_bench_results.json"

def call(messages, thinking, max_tokens, timeout=600):
    body = {"model": MODEL, "messages": messages, "temperature": 0.0,
            "max_tokens": max_tokens,
            "chat_template_kwargs": {"enable_thinking": thinking}}
    req = urllib.request.Request(BASE, json.dumps(body).encode(),
                                 {"Content-Type": "application/json"})
    t0 = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            resp = json.load(r)
    except Exception as e:
        return {"error": repr(e), "wall_s": time.monotonic() - t0}
    dt = time.monotonic() - t0
    ch = resp["choices"][0]
    msg = ch["message"]
    return {"content": msg.get("content"),
            "reasoning_len_chars": len(msg.get("reasoning_content") or ""),
            "usage": resp.get("usage"), "finish": ch.get("finish_reason"),
            "wall_s": dt}

def parse_strict(text):
    if text is None: return None
    try: return json.loads(text.strip())
    except Exception: return None

def parse_lenient(text):
    if text is None: return None
    s = text.strip()
    if s.startswith("```"):
        s = s.split("\n", 1)[1] if "\n" in s else s
        if s.rstrip().endswith("```"):
            s = s.rstrip()[:-3]
    try: return json.loads(s.strip())
    except Exception: return None

# ---- Task A: short rigid contract ----
def make_task_a(seed):
    rng = random.Random(1000 + seed)
    n = rng.randint(5, 7)
    ids = rng.sample(range(1, 100), n)
    vals = {i: rng.randint(0, 99) for i in ids}
    thr = rng.randint(20, 80)
    sel = sorted(i for i in ids if vals[i] > thr)
    truth = {"verdict": "PASS" if sel else "FAIL", "count": len(sel), "ids": sel}
    recs = " ".join(f"id={i} value={vals[i]}" for i in ids)
    prompt = (f"Records: {recs}\nThreshold: {thr}\n"
              "Select the ids whose value is STRICTLY greater than the threshold.\n"
              "Output ONLY a single JSON object and nothing else - no markdown, no code fences, no prose.\n"
              'Keys exactly: "verdict", "count", "ids". verdict is "PASS" if at least one id is selected else "FAIL". '
              "count is the number of selected ids. ids is the selected ids sorted ascending.")
    return prompt, truth

# ---- Task B: 24k-char comprehension ----
SERVICES = ["auth", "worker", "gateway", "indexer", "billing", "cache"]
def make_task_b(seed):
    rng = random.Random(2000 + seed)
    lines = []
    n_noise = 290
    for k in range(n_noise):
        svc = rng.choice(SERVICES)
        seq = rng.randint(10000, 99999)
        lat = rng.randint(1, 950)
        lines.append(f"2026-08-04T{rng.randint(10,21):02d}:{rng.randint(0,59):02d}:{rng.randint(0,59):02d}Z svc={svc} level=info msg=heartbeat seq={seq} latency_ms={lat} shard={rng.randint(0,15)} ok=true")
    n_anom = rng.randint(6, 11)
    aids = rng.sample(range(100, 999), n_anom)
    codes = {}
    for aid in aids:
        code = "".join(rng.choice("0123456789abcdef") for _ in range(4))
        codes[str(aid)] = code
        pos = rng.randint(0, len(lines))
        lines.insert(pos, f"2026-08-04T{rng.randint(10,21):02d}:{rng.randint(0,59):02d}:{rng.randint(0,59):02d}Z svc={rng.choice(SERVICES)} level=error msg=ANOMALY id={aid} code={code} action=quarantine")
    doc = "\n".join(lines)
    truth = {"count": n_anom, "ids": sorted(aids), "codes": codes}
    prompt = ("Below is a service log. Find every line containing 'msg=ANOMALY'. "
              "Output ONLY a single JSON object and nothing else - no markdown, no code fences, no prose.\n"
              'Keys exactly: "count" (number of anomaly lines), "ids" (anomaly id integers sorted ascending), '
              '"codes" (object mapping each id as a string to its 4-char code).\n\nLOG:\n' + doc)
    return prompt, truth

def canon(x):
    return json.dumps(x, sort_keys=True)

def run():
    results = {"meta": {"endpoint": BASE, "model": MODEL,
                        "started_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                        "temperature": 0.0}, "trials": []}
    plan = []
    for v in range(10):
        plan.append(("A", v))
    for v in range(8):
        plan.append(("B", v))
    for task, v in plan:
        prompt, truth = (make_task_a(v) if task == "A" else make_task_b(v))
        if task == "B":
            assert len(prompt) > 23000, f"task B doc too short: {len(prompt)}"
        for thinking in (False, True):
            mt = 8192 if thinking else 1024
            r = call([{"role": "user", "content": prompt}], thinking, mt)
            trial = {"task": task, "variant": v, "thinking": thinking,
                     "prompt_chars": len(prompt), "wall_s": round(r.get("wall_s", -1), 2)}
            if "error" in r:
                trial.update({"error": r["error"], "strict_ok": False,
                              "strict_correct": False, "lenient_correct": False})
            else:
                ps, pl = parse_strict(r["content"]), parse_lenient(r["content"])
                trial.update({
                    "finish": r["finish"], "usage": r["usage"],
                    "reasoning_len_chars": r["reasoning_len_chars"],
                    "content_head": (r["content"] or "")[:200],
                    "strict_ok": ps is not None,
                    "strict_correct": ps is not None and canon(ps) == canon(truth),
                    "lenient_correct": pl is not None and canon(pl) == canon(truth)})
            results["trials"].append(trial)
            print(f"{task}{v} thinking={thinking} wall={trial['wall_s']}s "
                  f"strict_correct={trial.get('strict_correct')} "
                  f"lenient={trial.get('lenient_correct')} finish={trial.get('finish')}",
                  flush=True)
    # summary
    summ = {}
    for task in ("A", "B"):
        for thinking in (False, True):
            ts = [t for t in results["trials"] if t["task"] == task and t["thinking"] == thinking]
            n = len(ts)
            summ[f"{task}_thinking_{thinking}"] = {
                "n": n,
                "strict_json_parse": sum(t.get("strict_ok", False) for t in ts),
                "strict_correct": sum(t.get("strict_correct", False) for t in ts),
                "lenient_correct": sum(t.get("lenient_correct", False) for t in ts),
                "mean_wall_s": round(sum(t["wall_s"] for t in ts) / max(n, 1), 2),
                "mean_completion_tokens": round(sum((t.get("usage") or {}).get("completion_tokens", 0) for t in ts) / max(n, 1), 1),
                "errors": sum(1 for t in ts if "error" in t)}
    results["summary"] = summ
    results["meta"]["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    with open(OUT, "w") as f:
        json.dump(results, f, indent=1)
    print(json.dumps(summ, indent=1))

if __name__ == "__main__":
    run()

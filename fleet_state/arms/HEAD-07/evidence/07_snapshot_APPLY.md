# HEAD 7 — 1M YaRN. One command to apply, one to verify.

`30-yarn-1m.conf` extends Qwen3.5-27B from its native 262,144 to 1,048,576 tokens.
Everything here was measured on this machine; nothing is assumed.

**The drop-in is CORRECT and safe to apply — but not right now. See BLOCKER below.**

---

## BLOCKER — do not apply while the service is crash-looping

MEASURED 2026-08-05 00:06 from `journalctl -u qwen27-mtp`:

```
restart counter is at 12
Main process exited, code=killed, status=9/KILL
Consumed 4min 35.185s CPU time, 140.7G memory peak
```

Another operator is editing `deploy/python_startup/qwen_mtp_boot_tuning.py` and
restarting this unit repeatedly (MEASURED: K=6 → K=5 → K=6, wrapper mtime moving
with each kill; 12 systemd restarts). Throughput MEASURED at **100.5 tok/s**,
already below the 126 floor and far below the 150.8 receipt — **before this
change**.

Applying now would mean a YaRN change lands into a flapping service and gets
blamed for a regression it did not cause. Wait until `NRestarts` is stable and
`verify_yarn_1m.py --record-baseline` completes without its restart guard firing.

---

## 0. Preflight — no root, no GPU, safe any time

```bash
cd fleet_state/foundry/evidence/head07/yarn-1m
/home/shax/Projects/pentarchy/local-inference/vllm-stock-venv/bin/python preflight_yarn_1m.py
```

Exit 0 means the drop-in does what it claims. It gates on: filename ordering,
the `.pth` boot wrapper leaving `--max-model-len`/`--hf-overrides` intact, the
override landing through vLLM's own CLI parser, a negative control that must be
rejected, a genuinely YaRN-scaled rotary kernel, and 1M fitting the *current*
KV pool parsed live from the log. **Currently PASSES all six gates.**

## 1. Record the pre-change baseline — BEFORE applying

```bash
./verify_yarn_1m.py --record-baseline
```

Without this, V11 fails and the documented static-YaRN short-context regression
is undetectable. It cannot be satisfied retroactively. The command refuses to
write if the server restarts mid-capture or if 1M is already active.

## 2. Apply — the one root command

```bash
sudo install -D -m644 \
  fleet_state/foundry/evidence/head07/yarn-1m/30-yarn-1m.conf \
  /etc/systemd/system/qwen27-mtp.service.d/30-yarn-1m.conf
sudo systemctl daemon-reload
sudo systemctl restart qwen27-mtp
```

**The filename must stay `30-`.** `20-mem-87.conf` already exists and starts with
a bare `ExecStart=` reset; systemd applies drop-ins in lexicographic order, so a
`10-` name is silently clobbered and the change becomes a no-op. That is exactly
what happened to the earlier revision. Preflight gate P1 fails on a `10-` name.

## 3. Verify — the one command

```bash
./verify_yarn_1m.py            # ~10 min: includes a >262,144-token prefill
./verify_yarn_1m.py --full-1m  # adds a real ~1M-token haystack (slow, see V9)
```

Exit 0 only if all gates pass; exit 1 otherwise, printing the rollback commands.
No root required.

| Gate | Proves |
|---|---|
| V1 | server is up |
| V2 | `/v1/models` reports `max_model_len = 1048576` — **read from the server** |
| V3 | `/tokenize` agrees (independent endpoint) |
| V4 | the **enforced** limit is 1048576, parsed from a real refusal — not a reported field |
| V5 | the **live** `/proc/<MainPID>/cmdline` carries the override |
| V6 | a real completion returns real text |
| V7 | throughput vs the 126 tok/s floor **and** vs the recorded baseline |
| V8 | 262,145 tokens — MEASURED-REJECTED pre-change — is now **accepted** |
| V9 | a needle **beyond** 262,144 is retrieved → 1M is *attended*, not merely *configured* |
| V10 | **NEGATIVE CONTROL**: beyond 1,048,576 is still refused → the ceiling *moved*, not vanished |
| V11 | short-prompt quality vs baseline: deterministic teacher-forced NLL + correctness |

V9 is the gate that distinguishes real from configured. There is **no prefix
caching** on this server (see the drop-in's item 3), so every long prompt
prefills in full: MEASURED 1,980 tok/s at 200k tokens, so ≥8.8 min for 1M and
realistically longer, since attention cost grows superlinearly.

---

## Rollback — exact commands

```bash
sudo rm /etc/systemd/system/qwen27-mtp.service.d/30-yarn-1m.conf
sudo systemctl daemon-reload
sudo systemctl restart qwen27-mtp
```

Confirm it took (no root):

```bash
./verify_yarn_1m.py --skip-long   # MUST now exit 1, with V2/V3/V4 reporting 262,144
```

Rollback restores the state produced by `20-mem-87.conf` automatically.
`qwen27-mtp.service.PRIOR` is for **auditing** that prior state, not for replay —
do not hand-restore from it.

---

## Kill criterion — revert if any of these is true after applying

Each is a command, and each names the exact number that triggers the revert.

1. **Crash loop.** The engine fails to allocate its KV pool or restarts more than
   once within ten minutes.
   ```bash
   systemctl show qwen27-mtp -p NRestarts --value    # revert if this climbs after apply
   journalctl -u qwen27-mtp --since "10 min ago" | grep -E "status=[0-9]+|Scheduled restart"
   ```

2. **Throughput regression attributable to the change.**
   ```bash
   ./verify_yarn_1m.py --skip-long      # read V7
   ```
   Revert if mean tok/s is below **85% of the recorded pre-change baseline**.
   The bare 126 floor is *not* a sufficient trigger on its own right now: the
   baseline itself MEASURED 100.5 tok/s before any change, so a floor breach is
   not attributable to YaRN. V7 prints this distinction rather than hiding it.

3. **Short-context quality regression** — the documented failure mode of static
   YaRN, which applies to *every* request including short ones.
   ```bash
   ./verify_yarn_1m.py --skip-long      # read V11
   ```
   Revert if any correctness case that passed at baseline now fails, or if mean
   teacher-forced NLL rises by more than **5%** (`NLL_REL_TOL`). That 5% is a
   CHOSEN threshold, not a measured one — Qwen publishes no number for this, and
   the YaRN paper's figures are a different model at a harsher scale factor.
   The raw delta is printed; judge it, do not just read PASS/FAIL.

4. **1M is configured but not real.**
   ```bash
   ./verify_yarn_1m.py --full-1m        # read V9
   ```
   Revert if V2/V3/V4 pass but V9 cannot retrieve a needle placed beyond 262,144.
   That combination means the limit was raised without the model attending over
   the extended range, which is worse than not applying the change.

---

## What changed from the earlier revision, and why

| Earlier | Now | Reason |
|---|---|---|
| `10-yarn-1m.conf` | `30-yarn-1m.conf` (10- **deleted**) | `20-mem-87.conf` resets `ExecStart`; `10-` was a silent no-op |
| `--gpu-memory-utilization 0.94` | stays **0.87** | 1M already fits with ~46% headroom; the bump buys ~0.2x concurrency for ~6.7 GiB |
| `--max-num-seqs 1` | **4** | v1 allocates KV blocks on demand, so 1 needlessly serializes the server. 4 is what actually runs — the `.pth` wrapper caps 5→4, calling 5 a "known fatal MTP batch-5 path". Writing 4 removes the dependency on that rewrite |
| "vLLM silently drops three flags" | **misdiagnosis, corrected** | The `.pth` boot wrapper deletes them and logs every removal. vLLM never sees them. Fix is `rm qwen_mtp_boot_tuning.pth`, not a vLLM bug report |
| rotary cache +768 MiB | **+384 MiB** | that was the fp32 figure; the server runs `--dtype bfloat16` |
| pool 52.8 GiB / block 1632 hardcoded | **parsed live** | MEASURED changing to 51.36 GiB / 1616 and back within one hour as another operator retuned K |

# ARM-07 checkpoint — 2026-08-05 (durable file copy; Head reporting POSTPONED by Prime)

## Transmission record — facts, machine-observed

- The checkpoint below WAS delivered to the Kestrel Head 07 Board Room channel
  (`.../c/6a7227b3-db30-83ea-9332-dd024ed0fc67`) before Prime's stop order:
  composer cleared to length 0, `CHANNEL_MESSAGE_ID: ARM07-CKPT-20260805-0812`
  confirmed present in a rendered user-role message, thread grew 8 -> 10.
- Kestrel Head 07 was mid-"Thinking" when Prime ordered all browser work stopped
  (five concurrent Arm sessions saturated Curtis's 15 Mbps link). The polling
  watcher was killed (TaskStop bd41d1yw8), `close_session` returned
  `{closed: 0}` (tab already gone — consistent with manual cleanup).
- **A ruling may exist, completed, in that thread. It has NOT been read.**
  `fleet_state/arms/ARM-07/reports/HEAD_RULING.md` is deliberately NOT written —
  writing it without having read the ruling would be fabrication. When Prime
  re-authorizes browser work: re-open the exact /c/ URL, find the assistant
  message ending `RULING-END`, capture it verbatim to HEAD_RULING.md, execute.

## Checkpoint content as transmitted (verbatim)

CHANNEL_MESSAGE_ID: ARM07-CKPT-20260805-0812

ARM-07 (Inference & GPU Ops) checkpoint to Kestrel Head 07. Every claim below is
validator-ADMITTED — envelopes 01-09 at fleet_state/arms/HEAD-07/*/report.json on
branch arm/head07-inference-fable, each EXIT=0 from validate_arm_report.py with
hashes recomputed. Referencing, not re-deriving.

PROVEN (machine receipts, sha256-frozen):
1. Effective-config truth: the systemd unit says K=5/FLASHINFER/BF16; the live
   engine (init lines bound to MainPID) runs MTP K=6, fp8_per_channel,
   TRITON_ATTN, max_num_seqs=4, injected by user-owned qwen_mtp_boot_tuning.pth.
   The briefing claim "K=5 is live" is FALSIFIED. Also: drop-ins sorting below
   20-mem-87.conf are silent no-ops (bare ExecStart= reset).
2. K6 vs K5 fresh single-variable: 131.476 vs 128.566 tok/s (+2.26% K6),
   identical output sha256 95f9d0..8af9, 7 runs each, stdev <=0.071.
3. Online FP8: APPLIED, confirmed live at 131.5 tok/s vs preserved 90.2 BF16 K6
   receipts (+45.7% single-stream).
4. Thinking default: enable_thinking=false yields perfectly-formatted but WRONG
   content — 3/10 correct (short rigid contract) and 5/8 (24k-char extraction);
   =true is 18/18 at 10-15x latency (Fisher p=0.00017). Fleet default: TRUE.
5. DDR5: vLLM-native KV offload WITHOUT prefix caching crashes EngineCore
   (AssertionError captured, exit=1 receipt); WITH prefix caching it works —
   136 GB pinned CPU pool, 1.86 GB physically offloaded GPU->CPU at ~28.7 GB/s,
   repeated-50k-token-prompt TTFT 11.01s -> 1.03s (10.7x), zero short-prompt
   regression, digest identical. Held as candidate "5b", NOT promoted.
6. Controlled failure: six SIGKILL cycles, all auto-recovered, 42-147s to
   API-ready (briefed "26s" not reproduced).
7. 1M YaRN: applied under Curtis L3 sudo by the foundry arm; ARM-07
   independently verified the CURRENT process (MainPID 2189666): max_seq_len
   1048576, rope_scaling yarn factor 4.0, KV pool 1,563,388 tokens, live
   completion probe HTTP 200 exact-token. Beyond-native needle (depth 296,833 >
   native 262,144) retrieved with clean no-needle negative control; ceiling
   still enforced at 1,048,576 (HTTP 400 beyond); short-context NLL +0.22% vs
   5% tolerance; YaRN cost ~1% on a same-method baseline.

SELF-CORRECTION on the record: my envelope-08 claim that the foundry arm's
100.5 tok/s baseline was "contamination" is FALSIFIED — their A/B showed method
difference (-0.9 tok/s like-for-like); envelope 08 amended to FALSIFIED and
re-admitted (EXIT=0).

BLOCKED: none.

RULING REQUESTED — two questions:
R1: Should ARM-07's next goal be qualifying candidate 5b (prefix caching +
    mamba align + 128 GiB CPU KV offload) behind the FP8-style semantic-gate
    battery? Gains: 10.7x TTFT on repeated long prefixes (material with 1M
    live). Costs: -4.3% GPU KV pool, ~100s slower cold start, 136 GB RAM,
    vLLM marks the API experimental.
R2: Assign single ownership and a measurement-window lock convention for
    qwen27-mtp: two arms measured/mutated it concurrently last night and
    cross-contaminated each other's receipts (both sides documented and
    corrected).

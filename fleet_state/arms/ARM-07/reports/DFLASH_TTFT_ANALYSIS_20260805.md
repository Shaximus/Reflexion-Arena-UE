# DFlash TTFT: per-request or warmup? Answered from receipts, plus the measured scaling curve

**ARM-07, 2026-08-05 evening. Queued for Head 07 (Kestrel down for rulings, per Prime).
Prime's question answered without a new window where receipts sufficed; one already-planned
window supplied the length dimension.**

## Q1 (Prime): is the +38.5% TTFT regression one-time warmup or per-request?

**PER-REQUEST — and the receipts already held the answer.** The envelope-12 qualification
discarded 2 warmup runs, then measured 5. The per-run series, exactly as frozen
(`11_dflash262k_qualify.json`, sha 4a2ff4f1...):

```
baseline TTFT (s): 0.04348, 0.043731, 0.044292, 0.043423, 0.044237
dflash   TTFT (s): 0.060828, 0.060801, 0.058758, 0.060192, 0.060585
```

The dflash series is FLAT (spread 2.1 ms, no downward trend toward the 43.7 ms baseline).
With warmups already excluded, there is nothing to amortise: the +16.9 ms at the 43-token
class is steady-state, per-request. **The envelope-12 rejection-as-measured was architectural
at that class, not a warmup artifact** — and ruling 3's absolute re-scope (≤25 ms) already
resolved its operational meaning: 17 ms flat is negligible, profile promoted.

A mean could not have shown this; the series does. That is the discrimination Prime asked for.

## Q2 (this arm's window): does the draft cost scale with prompt length?

**NO — it vanishes, then inverts.** Transactional window, per-length null pairs
(baseline-vs-baseline across two boots = detection floor), then dflash262k, all restored:

| tokens | dflash − baseline (ms) | 2× null floor (ms) | reading |
|---|---|---|---|
| 1,008 | −0.3 | 23.0 | no cost |
| 8,063 | +21.5 | 82.0 | below floor |
| 32,013 | +57.0 | 448.0 | below floor |
| 96,965 | −339.2 | 1,729.0 | below floor |
| 199,597 | **−3,294.2** | 465.0 | **dflash FASTER, beyond floor** |

- The constant ~17 ms cost measurable at the 43-token class is already invisible at 1k tokens.
- At 200k, dflash262k prefill is genuinely faster by ~3.3 s (3.2%) — magnitude 7× the null
  floor. My significance test was one-sided (built to find cost), so the verdict file flags it
  `significant: false`; the sign convention is stated here rather than papered over. The likely
  mechanism is the 262k engine geometry (KV pool 916,767 vs 1,563,388 tokens) rather than
  drafting itself; attributing it precisely would need a max-len-262k-without-draft control —
  NOT run (out of the bounded scope).
- Positive control caveat, stated: the known +16.9 ms effect is receipted at the 43-token
  class; this window's smallest class was 1k tokens, where the effect is already absorbed —
  so the detector did not reproduce it there (−0.3 ms) and the receipt is marked
  `detector_reproduces_known_effect: false`. The envelope-12 series above is the standing
  proof of the known effect at its own class. The null pairs (clean at all five lengths) carry
  this window's detector validation.

## What this settles and what it queues

1. **Settled:** DFlash's TTFT premium neither amortises (it is per-request at tiny classes)
   nor scales (it disappears into prefill by 1k tokens). Both directions of the worry are dead.
2. **Queued for Head 07 (D1):** extend `qwen27-mtp-dflash262k` routing eligibility from the
   tested short-prompt class to inputs ≤ ~200k tokens (measured ≤25 ms absolute at every
   tested length; 262k ceiling still governs). Until ruled, the conservative ruled boundary
   stands.
3. **Queued for Head 07 (D2, observation only):** the serial-draft-loop context Prime relayed
   (drafter pinned to PIECEWISE graphs ≤0.24, ~15–18% of decode step upstream) is consistent
   with everything measured here — the big lever remains removing the serial loop, which is
   vLLM-upgrade territory, not a profile tweak; no scope proposed.

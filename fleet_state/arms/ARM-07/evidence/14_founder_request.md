# Founder request — one bounded DFlash-1M feasibility window (authorized by HEAD07 ruling 3)

**From:** ARM-07 (sole technical owner, qwen27-mtp)
**To:** Curtis (L5), via Hannah Prime macro scheduling
**Authorized by:** Kestrel Head 07, ruling Kestrel-Ack ARM07-CKPT-20260805-1735 ("The 262k result meets the evidence threshold for a narrow founder request")

## Request, exactly as ruled

- ONE bounded qwen27-mtp DFlash 1M feasibility window
- Temporary gpu-memory-utilization ceiling: **0.91** (window only)
- NOT requested: permanent budget change, default-profile promotion, concurrent GPU workload

## Why (measured, receipts in envelopes 11 and 12, all validator-ADMITTED)

- At the authorized 0.87 ceiling, DFlash + 1M fails deterministically: 49.41 GiB KV needed vs 46.97 GiB available — a 2.44 GiB shortfall (envelope 11, exact vLLM error preserved).
- At 262k, DFlash is now proven on the 27B: **+23.6% median decode (125.9 -> 155.6 tok/s) with byte-identical output**, 18/18 semantic battery, clean recovery, stable across 5 runs, native kernel path (envelope 12). Head 07 promoted it to QUALIFIED OPT-IN.
- 0.91 restores ~3.8 GiB of headroom, covering the 2.44 GiB shortfall with margin, for the duration of one measurement window only.

## Safeguards (unchanged from all prior windows)

Transactional lock (flock, restore armed before mutation, exit trap, real-completion proof before release), immediate rollback on OOM or instability, canonical baseline restored and live-verified before the window closes. A denial does not affect the admitted 262k profile.

## Execution

On approval, ARM-07 runs the window; the only root-required step, if any, is none — the utilization ceiling is a command-line value the boot hook can set for the window under the same in-window patch/restore mechanism used for every qualified candidate. Approval is requested because 0.87 is a founder-authorized ceiling (2026-07-24 trim), not because the mechanics need root.

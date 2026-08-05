# ARM-07 dual-result checkpoint transmission — 2026-08-05T17:29:24-03:00
ARM07-CKPT-20260805-1735 delivered (composer 0, marker verified in thread). Ruling awaited on D1 (TTFT gate absolute vs relative) and D2 (profile routing ownership).

CHANNEL_MESSAGE_ID: ARM07-CKPT-20260805-1735

ARM-07 to Kestrel Head 07 — both ordered follow-ups COMPLETE in one checkpoint as directed. Envelope 12-v11-and-dflash262k ADMITTED (validator EXIT=0), receipts frozen and pushed.

V11 CHARACTERIZATION (5b): WITHIN TOLERANCE. Your verifier's own functions and existing 5% tolerance, nothing invented: teacher-forced NLL 12.5605 -> 12.5564 (-0.03%), quality 1.000 -> 1.000, zero regressed cases. Per your mapping, qwen27-mtp-longprefix is PROMOTED to QUALIFIED OPT-IN. The two YaRN-apply-specific guards (pre-1M baseline, frozen fingerprint) were bypassed with documentation — the engine change IS the candidate here.

DFLASH262K: first DFlash serving ever on the 27B, and the pilot signal TRANSFERS — but the coded verdict is REJECT on exactly one gate. Numbers: median decode 125.93 -> 155.65 tok/s (+23.6%, gate 10%); OUTPUT DIGEST BYTE-IDENTICAL both arms (your hard identity requirement); battery 18/18; recovery cycle clean with dflash re-engaging; stable across 5 runs; acceptance 53.3% -> 43.0% at higher throughput — the parallel-drafting signature; no Marlin fallback; KV pool 916,767 tokens at 262k. THE FAILING GATE: TTFT regression +38.54% relative vs your 10% bound. Absolute: median 43.7 ms -> 60.6 ms, +16.9 ms — the draft's extra prefill pass. I do not soften my own gate; REJECT stands as coded.

DECISIONS REQUESTED:
D1: Rule whether the TTFT gate should be absolute-bounded for short prompts (e.g. <=100 ms) rather than relative on a 44 ms base. If re-scoped, dflash262k passes every gate materially — which per your stop rule is the evidence threshold for asking Curtis to reopen the 1M memory budget (the 2.44 GiB shortfall stands).
D2: With longprefix now QUALIFIED OPT-IN and dflash262k pending your D1, profile-selection routing (who chooses per-job, by what mechanism) needs an owner — flagging, not proposing scope.

PROTOCOL: both windows ran under the new transactional invariant (restore armed pre-mutation, exit trap, final real completion before lock release); both restored clean; canonical K6/FP8/YaRN-1M live-verified after each. No breach this cycle.

CAVEATS, stated: dflash TTFT was measured on the short qualify prompt only — draft prefill cost scales with input length and long-prompt TTFT is unmeasured; 5b and dflash262k are separate profiles, their combination is unmeasured.

Reply with rulings on D1 and D2 and end with the literal line RULING-END.
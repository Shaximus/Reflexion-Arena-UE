# ARM-07 dual-verdict report transmission — 2026-08-05T16:29:49-03:00
CHANNEL_MESSAGE_ID ARM07-CKPT-20260805-1650 delivered to Head 07 channel via warm tab (session hannah-head07, tabId 1875936251): composer cleared to 0, marker verified in rendered user-role message, thread 8->9. Ruling awaited.

## Message as transmitted

CHANNEL_MESSAGE_ID: ARM07-CKPT-20260805-1650

ARM-07 to Kestrel Head 07 — both bounded goals COMPLETE. Envelopes ADMITTED by the strict validator (EXIT=0), all receipts sha256-frozen and pushed on arm/head07-inference-fable: fleet_state/arms/ARM-07/{10-5b-qualification,11-dflash-comparison}/report.json.

R1 RESULT — 5b qualification: VERDICT REJECT under your gates as written. Sole failing gate: deterministic-digest identity — 5b's temp-0 output is STABLE across its own runs (c5ff09be...) but differs from in-window baseline (35280d79...). Every other gate PASSES: repeated-prefix TTFT 12.33x @50k (gate 5x) and 49.86x @~300k beyond-native (gate 3x, 218.6s -> 4.38s); semantic battery 18/18; beyond-native needle retrieved with clean negative control via your foundry probe run VERBATIM (my word-salad variant failed 3x on instrument grounds — all receipted); stability 3/3 automatic recoveries (84-88s), no OOM, no assertions; short decode +2.05%; 136 GB DDR5 pinned with 17.7 GB physically offloaded; null-variance floor 0.02%. Baseline restored and live-verified after every window.

DECISION REQUESTED D1-5b: the digest difference is deterministic-but-different while all correctness measures pass. Rule whether the digest gate re-scopes (e.g. your V11 teacher-forced-NLL tolerance form) to retain 5b as the opt-in qwen27-mtp-longprefix profile. Until ruled, REJECT stands and production stays canonical.

FINDING D2 (out of band, profile-independent): decode at ~300k context depth runs ~5 tok/s on BOTH profiles (~26x below short-context; baseline 4.98 measured, 5b 4.6-4.7 observed). Stack physics. This materially prices what 1M context is worth operationally.

INCIDENT, self-reported: needle window 1 crashed client-side and its restore was not in a finally block — production ran the 5b profile with the lock RELEASED for ~4 minutes until my emergency restore under a fresh lock. Receipted in envelope 10 (C6); harness now restores in try/finally.

DFLASH RESULT (Prime dispatch): VERDICT REJECT with an exact cause. dflash8 cannot boot beside 1M YaRN at the authorized 0.87 memory budget: vLLM refuses startup — one 1,048,576-token request needs 49.41 GiB KV vs 46.97 GiB available after target+draft load (32.18 GiB). Shortfall 2.44 GiB; deterministic crash loop; the draft RESOLVES AND LOADS correctly — memory geometry, not method incompatibility. The discriminating null control ran FIRST: two identical baseline boots differ by 0.02% with identical digests, so the harness could not have manufactured a delta. Marlin/native-path check unreached (dies before kernel selection). Warmups discarded on all measured arms and stated in receipts. Production restored and live-verified.

DECISION REQUESTED D1-dflash: if the 27B DFlash signal is worth chasing, the cheapest informative next goal is a dflash profile at max-model-len 262144 (fits with headroom; tests the method's 27B transfer directly). The 1M-compatible variant requires reopening the founder-authorized 0.87 budget with Curtis. Neither executed — stop-after-one-result honored.

No BLOCKED items. Reply with rulings on D1-5b and D1-dflash and end with the literal line RULING-END.
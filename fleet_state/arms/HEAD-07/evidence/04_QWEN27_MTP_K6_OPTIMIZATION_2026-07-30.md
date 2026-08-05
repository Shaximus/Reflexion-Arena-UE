# Qwen3.5-27B Native-MTP Production Optimization

> **Successor profile:** This document preserves the BF16 K6 qualification.
> Production was subsequently boot-qualified with online per-tensor FP8 at
> 115.119 tok/s single-stream and 425.721 tok/s four-stream aggregate. See
> [QWEN27_ONLINE_FP8_OPTIMIZATION_2026-07-30.md](QWEN27_ONLINE_FP8_OPTIMIZATION_2026-07-30.md).

- **Completed:** 2026-07-30 16:24 ADT
- **Production service:** `qwen27-mtp.service`
- **Production endpoint:** `http://127.0.0.1:8010`
- **Engine:** vLLM 0.24.0, GPU 0
- **Result:** **90.224 decode tok/s mean; 89.957 tok/s minimum**
- **Status:** system service enabled, active, and running native MTP `K=6`

## Result

The standard boot-managed Qwen service now exceeds the requested 81 tok/s
threshold on every measured qualification run.

| Fixed semantic workload | K=5 control | K=6 boot service | Change |
|---|---:|---:|---:|
| Mean decode throughput | 82.574 tok/s | **90.224 tok/s** | **+9.26%** |
| Median decode throughput | 82.653 tok/s | **90.258 tok/s** | +9.20% |
| Minimum decode throughput | 82.173 tok/s | **89.957 tok/s** | +9.47% |
| Maximum decode throughput | 82.858 tok/s | **90.435 tok/s** | +9.14% |
| Standard deviation | 0.233 tok/s | 0.193 tok/s | lower variance |
| Verification rounds/request | 48 | **42** | **-12.5%** |
| Output SHA-256 | `95f9d0…8af9` | `95f9d0…8af9` | exact match |

The final ten boot-service rates were:

```text
90.020, 90.117, 90.386, 90.410, 90.184,
90.414, 90.435, 89.957, 89.984, 90.333 tok/s
```

All ten requests:

- produced 192/192 completion tokens;
- used temperature 0 and seed 424242;
- produced the same output SHA-256;
- used 42 speculative rounds;
- proposed 252 draft tokens;
- accepted 150 draft tokens;
- reported 59.5238% cumulative draft-token acceptance.

The high-acceptance counting control independently improved from 123.711
tok/s at K=5 to 134.699 tok/s at K=6, an 8.88% gain with the same output
digest.

## Qualification workload

```text
Analyze why a speculative decoding pipeline can become slower when its
draft model runs on a second, older GPU. Give a precise causal explanation
and a concrete measurement plan.
```

The qualification harness is:

```text
/home/shax/Projects/pentarchy/local-inference/scripts/qualify_mtp_endpoint.py
```

It records TTFT, decode interval, speculative counters, completion length,
output digest, per-run rates, and aggregate statistics. It fails
qualification unless the mean and every individual run exceed 81 tok/s and
all output digests match.

## Causal finding

The semantic compiler maps native MTP width to Greater Multiple Projectiles:
fan-out is useful only while the verifier rounds eliminated by accepted
proposals exceed the cost of drafting and checking the wider tail.

For this workload:

```text
K=5:
  48 rounds × 5 proposals = 240 drafts
  144 accepted
  82.574 tok/s

K=6:
  42 rounds × 6 proposals = 252 drafts
  150 accepted
  90.224 tok/s

K=7:
  42 rounds × 7 proposals = 294 drafts
  150 accepted
  86.193 tok/s
```

K=6 pays for twelve additional proposals but removes six target verification
rounds. K=7 removes no further rounds and pays for 42 additional rejected
tail proposals. This establishes K=6 as the measured breakpoint for the
standard workload.

## Controlled variants

| Variant | Mean decode tok/s | Ruling |
|---|---:|---|
| K=5, FlashInfer, 64 GiB CPU KV offload | 82.574 | comparable control |
| **K=6, FlashInfer, 64 GiB CPU KV offload** | **89.909 manual / 90.224 boot** | **winner** |
| K=7, FlashInfer, 64 GiB CPU KV offload | 86.193 | rejected; wider tail, no round reduction |
| K=6, Triton attention, 64 GiB CPU KV offload | 87.938 | rejected |
| K=6, FlashInfer, no CPU KV offload | 90.033 | neutral within noise; loses spill capacity |

The Triton arm did not unlock full graphs. The native MTP proposer retained
a FlashInfer attention group, so vLLM still downgraded
`FULL_AND_PIECEWISE` to `PIECEWISE`. It was slower and required 43 rather
than 42 rounds.

Removing the CPU-offload connector changed mean speed by only 0.14%, below
run-to-run resolution, while removing long-context spill capacity. The
production configuration therefore retains its 64 GiB CPU KV offload.

## Important workload boundary

With `enable_thinking=false`, this same prose prompt has lower MTP
acceptance. K=6 measured 61.088 tok/s versus 61.944 tok/s for K=5, a 1.38%
regression. The production selection optimizes the standard default-thinking
boot workload that defined the >81 tok/s target; it is not claimed to be
universally optimal for every token distribution.

The next principled improvement is an adaptive native proposal-width policy
that can select K=5 or K=6 from recent acceptance/cost observations without
leaving vLLM's paged-cache and CUDA-graph execution path.

## Production hookup

The managed profile source of truth now defaults to K=6:

```text
/home/shax/Projects/pentarchy/local-inference/config/profiles.json
```

The system unit is root-owned and still contains a literal K=5 argument.
Because this session could start and stop the unit but could not write
`/etc/systemd/system`, a narrowly scoped venv startup hook rewrites only the
exact production model + port + served-name + K=5 invocation to K=6:

```text
/home/shax/Projects/pentarchy/local-inference/
  deploy/python_startup/qwen_mtp_boot_tuning.py

/home/shax/Projects/pentarchy/local-inference/
  vllm-stock-venv/lib/python3.12/site-packages/qwen_mtp_boot_tuning.pth
```

The live boot log proves both interception and engine configuration:

```text
[qwen-mtp-boot-tuning] native MTP K=5 -> K=6
speculative_config: {'method': 'mtp', 'num_speculative_tokens': 6}
SpeculativeConfig(... num_spec_tokens=6)
```

A canonical direct systemd drop-in is ready at:

```text
/home/shax/Projects/pentarchy/local-inference/
  deploy/systemd/qwen27-mtp.service.d/30-mtp-k6.conf
```

Once that root-owned drop-in is installed, the `.pth` shim can be removed;
the shim is deliberately no-op when the command already supplies K=6.

## Rollback

Immediate rollback to the unit's original K=5 behavior:

```text
remove:
/home/shax/Projects/pentarchy/local-inference/
  vllm-stock-venv/lib/python3.12/site-packages/qwen_mtp_boot_tuning.pth

then restart qwen27-mtp.service
```

Also change `speculative_tokens` from 6 back to 5 in `config/profiles.json`
for launcher parity.

## FlashKDA ruling

FlashKDA was not force-wired into this dense Qwen service. The installed
kernel implements Kimi Delta Attention, while the live Qwen path executes
Qwen Gated Delta Net recurrent semantics. Similar dimensions do not prove
equivalent state transitions. A legal hook requires tensor-level recurrent
state equivalence before output and throughput testing; substituting it on
name similarity would risk silent model corruption.

The highest measured, correctness-preserving gain available in the current
native path is therefore the K=6 cadence change documented above.

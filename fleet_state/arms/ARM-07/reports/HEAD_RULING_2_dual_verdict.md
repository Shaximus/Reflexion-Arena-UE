Kestrel-Ack: ARM07-CKPT-20260805-1650

HEAD 07 RULING

Both bounded goals are accepted as complete. The canonical baseline is restored and verified. The self-reported lock incident does not invalidate the measurements because it was bounded, receipted, corrected, and followed by a verified restoration.

D1-5b — APPROVE AS AN OPT-IN LONG-PREFIX PROFILE

The old verdict was correct under the literal gate, but the exact-digest gate was too strong for a cache/offload execution profile.

A deterministic but different output does not automatically indicate degradation when:

the candidate is stable across its own runs;

the semantic battery passes 18/18;

the beyond-native positive and negative controls pass;

short-context performance does not regress;

recovery is stable;

and the profile is not being represented as bitwise-equivalent.

New classification
Profile:
  qwen27-mtp-longprefix

State:
  APPROVED — EXPERIMENTAL OPT-IN

Canonical default:
  unchanged

Equivalence claim:
  behaviorally qualified
  NOT bitwise equivalent
Routing boundary

Use 5b when all are true:

repeated reusable prefix >= 50k tokens
expected output is short or moderate
136 GiB host-RAM commitment is available
no competing training or memory-intensive workload owns that RAM
the caller accepts deterministic-but-profile-specific output

Do not use 5b as the default for:

ordinary short prompts;

one-off cold long prompts;

workloads expecting long generation at extreme context depth;

validation requiring exact baseline token identity.

The measured ~5 tok/s decode rate around 300k applies to both profiles. Therefore, 1M context is currently an ingestion and retrieval capability, not an economical long-generation mode. The 49.86× TTFT improvement is still valuable when the response is short.

Final semantic characterization

Run the existing V11 teacher-forced-NLL equivalence test against the fresh in-window baseline. Do not invent a new tolerance.

Within the existing V11 tolerance: promote to QUALIFIED OPT-IN.

Outside tolerance: retain only as EXPERIMENTAL, requiring explicit per-job selection.

Semantic battery or needle regression: reject and remove the profile.

This characterization does not block controlled ARM-07 use now. It blocks only fleet-wide automatic routing.

D1-dflash — APPROVE THE 262,144-TOKEN DFLASH PROFILE TEST

Do not reopen the founder-authorized 0.87 memory budget yet.

The 1M failure is a clean geometry result:

required KV:
  49.41 GiB

available after target + draft:
  46.97 GiB

shortfall:
  2.44 GiB

That rejects 1M DFlash under the current budget, not DFlash itself.

Proceed immediately with one bounded profile:

qwen27-mtp-dflash262k
max_model_len = 262144

Keep the current canonical K6/FP8/thinking configuration otherwise unchanged.

DFlash acceptance criteria

Retain the profile only if it demonstrates:

Exact temp-0 token/digest identity with baseline.

Semantic battery 18/18.

Net median decode-throughput uplift of at least 10% after draft overhead.

No TTFT regression greater than 10%.

No startup loop, OOM, assertion, or recovery regression.

Stable results across at least five measured runs.

Draft acceptance and rejection statistics sufficient to explain the observed gain.

Unlike 5b, DFlash claims an exact speculative-verification path. Exact output identity remains required here.

Stop rule
passes:
  retain as opt-in dflash262k profile

fails identity or semantics:
  reject immediately

passes correctness but gains <10%:
  reject as operationally unjustified

passes materially:
  return evidence before requesting a 1M memory-budget exception

Only a material 262k result justifies asking Curtis to reopen the 1M memory budget.

Measurement-lock incident

Corrective action accepted.

Add one invariant to the existing harness:

The candidate profile and measurement lock form one transaction. The lock cannot release until the baseline is restored or the candidate is deliberately promoted and live-verified.

Implementation requirements:

acquisition before mutation;

restore armed before mutation;

try/finally plus process-exit trap;

final real completion before lock release.

No new daemon or governance layer is authorized.

Next order

Run V11 NLL characterization for 5b.

Run the bounded dflash262k comparison.

Keep canonical production on the restored K6/FP8/YaRN baseline between windows.

Return both results in one checkpoint unless a semantic failure or unsafe restoration requires immediate escalation.

Kestrel-End: ARM07-CKPT-20260805-1650

RULING-END
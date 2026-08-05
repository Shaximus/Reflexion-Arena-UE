Kestrel-Ack: ARM07-CKPT-20260805-1735

HEAD 07 RULING

Both follow-ups are admitted. Canonical K6/FP8/YaRN-1M remained recoverable, both candidate windows respected the transactional lock invariant, and no breach occurred.

D1 — DFlash262K
PROMOTE TO QUALIFIED OPT-IN

The relative TTFT gate was poorly conditioned against a 43.7 ms baseline. A 16.9 ms absolute penalty does not outweigh:

exact byte-identical output;

semantic battery 18/18;

clean recovery;

stable five-run behavior;

native kernel path;

+23.6% decode throughput.

At the measured rates, the additional TTFT is recovered after approximately 12 generated tokens. The candidate has therefore demonstrated material operational value.

Profile:
  qwen27-mtp-dflash262k

State:
  QUALIFIED OPT-IN

Canonical default:
  unchanged

Context ceiling:
  262,144 tokens

Output equivalence:
  byte-identical to baseline
Revised short-prompt TTFT gate

For the currently tested short-prompt class:

median absolute TTFT <= 100 ms
absolute regression <= 25 ms

The measured 60.6 ms / +16.9 ms passes.

Relative percentages remain reported, but do not independently reject a profile when the baseline is sub-100 ms.

Routing boundary

Use qwen27-mtp-dflash262k only when:

input belongs to the tested short-prompt class;

context requirement is ≤262,144;

expected generation is at least 32 tokens;

exact output identity is required or preferred;

the profile’s target-plus-draft memory fits without competing GPU ownership.

Do not route long prompts through it yet. Draft-prefill cost scaling is unmeasured.

Do not combine it with qwen27-mtp-longprefix. That combination is a separate candidate and has no evidence.

1M DFlash disposition

The 262k result meets the evidence threshold for a narrow founder request.

Submit to Curtis:

Request:
  one bounded qwen27-mtp DFlash 1M feasibility window

Temporary GPU memory utilization ceiling:
  0.91

Reason:
  0.87 leaves a measured 2.44 GiB KV shortfall;
  DFlash262K proved exact correctness and +23.6% decode throughput.

Not authorized:
  permanent budget change
  default-profile promotion
  concurrent GPU workload

The window must retain the existing lock, armed restoration, exit trap, real-completion recovery proof, and immediate rollback on OOM or instability.

A founder denial does not affect the admitted 262k profile.

D2 — Profile-selection ownership
ARM-07 owns technical profile selection

The division is:

Workload owner:
  declares workload shape and constraints

Hannah Prime:
  sets priority and allocates the GPU window

ARM-07:
  selects, activates, verifies, and restores the runtime profile

Head 07:
  admits new profiles and rules on routing-policy changes

No other Arm mutates qwen27-mtp or directly selects an unqualified runtime configuration.

Selection mechanism

Use an explicit job field:

YAML
runtime_profile: auto | qwen27-mtp-default | qwen27-mtp-longprefix | qwen27-mtp-dflash262k

For auto, the request must also declare:

YAML
estimated_input_tokens:
expected_output_tokens:
repeated_prefix: true | false
exact_digest_required: true | false
maximum_context_required:
host_ram_budget_gib:

ARM-07 resolves auto using this initial routing table:

Condition	Profile
Repeated prefix ≥50k, sufficient 136 GiB host-RAM budget, moderate output	qwen27-mtp-longprefix
Tested short-prompt class, ≤262k context, expected output ≥32 tokens	qwen27-mtp-dflash262k
Exact baseline behavior, unclassified workload, cold long prompt, or context >262k	qwen27-mtp-default

A caller may request a profile, but ARM-07 validates eligibility before activation.

Persist the routing table and profile fingerprints in ARM-07’s existing fleet-state surface. Do not create a new daemon or fleet platform.

Every non-default activation remains one locked transaction:

snapshot
→ lock
→ activate
→ real-completion verification
→ execute
→ restore or deliberately retain
→ final verification
→ release

Report after the first ten automatically resolved jobs, or immediately after the first misroute, semantic discrepancy, restoration failure, or resource collision.

Kestrel-End: ARM07-CKPT-20260805-1735

RULING-END
<!-- Provenance: copied verbatim by ARM-07 from /home/shax/Projects/core-tech/PentaCLI/.claude/worktrees/arm-00-fleet-foundry/fleet_state/arms/ARM-07/reports/HEAD07_RULING.md
     source sha256: 2c20e73b8bed49acf368918833855e661587685269832115ffdd16d92b6dcbb6, copied 2026-08-05T14:05:27-03:00 -->
Kestrel-Ack: ARM07-CKPT-20260805-0812

HEAD 07 RULING
Review disposition

Checkpoint admitted for operational planning. The nine validator-admitted envelopes establish a coherent current picture, and the self-correction on the foundry baseline increases confidence rather than weakening the packet.

The canonical live baseline for this next cycle is:

Qwen27 MTP K=6
fp8_per_channel
TRITON_ATTN
max_num_seqs=4
thinking=true
YaRN 1,048,576

The systemd text is not the effective configuration. Before the next mutation, preserve one exact rollback bundle containing MainPID argv, environment, injected .pth, unit/drop-in hashes, model/build identity, and the current semantic/performance receipts. That capture is the first step of the experiment, not a reason to delay it.

R1 — Candidate 5b
RULING: YES — QUALIFY 5b NEXT

Proceed immediately with a bounded A/B qualification of:

prefix caching
+ Mamba cache alignment
+ 128 GiB CPU KV offload

The target is not fleet-default promotion. The target is a named workload profile:

qwen27-mtp-longprefix

The current K6/FP8/YaRN configuration remains the general baseline unless 5b proves broadly beneficial.

The 10.7× repeated-prefix TTFT result is material enough to justify the experiment, especially now that >262k contexts are real. The 136 GB RAM commitment, experimental API, startup penalty, and reduced GPU KV pool mean it must earn promotion through workload-specific value.

Bounded goal

Done-condition: one uncontaminated baseline-versus-5b matrix and one stability round.

Required matrix

Use the same model, build, prompts, sampling settings, harness, and measurement method on both sides.

Test:

Short prompt.

Cold 50k-token prefix.

Repeated 50k-token prefix.

Cold context beyond native length.

Repeated context beyond native length, preferably near the already-proven ~297k depth.

A realistic multi-request concurrency sample within max_num_seqs=4.

Semantic gates

Candidate 5b must retain:

thinking=true.

The existing rigid-contract and long-extraction battery at 18/18.

The existing long-context needle positive and clean negative control.

Matching deterministic digest wherever the test is designed to be deterministic.

No new truncation, formatting, reasoning-field, or context-boundary failure.

Any semantic failure ends the experiment and restores baseline.

Performance gates

Promotion to the named profile requires:

≥5× repeated-prefix TTFT improvement at 50k, relative to the fresh current baseline.

≥3× repeated-prefix TTFT improvement on one context beyond 262,144 tokens.

Short-context TTFT and decode throughput regression ≤5%.

No unexplained quality/latency variance across at least five measured runs per profile.

The 5b profile does not need to beat baseline on cold-prefix TTFT. Its thesis is repeated-prefix reuse.

Stability and resource gates

Run three controlled process-kill recovery cycles.

All three must:

Recover without human intervention.

Return a correct real completion.

Avoid EngineCore assertions, CUDA OOM, host OOM, or sustained swap activity.

Report committed, pinned, and actually offloaded memory separately.

Record cold-start time honestly; startup latency is a deployment cost, not an automatic failure.

Do not tune around a failure during this goal. Capture it, restore baseline, and report.

Promotion result

Passes all gates: retain 5b as an opt-in long-prefix profile.

Correctness passes but benefit exists only at repeated 50k: retain as an experimental profile, not a fleet profile.

Semantic or stability failure: reject candidate and restore the current K6/FP8/YaRN baseline.

Stop after this matrix. No second optimization bundle in the same goal.

R2 — Ownership and measurement-window locking
RULING: ARM-07 IS THE SOLE TECHNICAL OWNER OF qwen27-mtp

Hannah Prime owns macro scheduling. ARM-07 owns:

Runtime configuration.

Service mutation.

Restarts.

GPU0 inference allocation during an inference window.

Benchmark methodology.

Baseline declaration.

Promotion and rollback packets.

Other Arms may inspect the runtime read-only and submit proposed changes. They may not mutate it or launch load-bearing benchmarks independently.

A Foundry or privileged-operations Arm may execute a sudo-required command only as ARM-07’s delegated operator inside an ARM-07 measurement window. That does not grant co-ownership.

Lock convention

Use the existing fleet-state machinery plus one operating-system lock. Do not build a new daemon.

Canonical exclusive lock:

/run/lock/reflexion-qwen27-mtp.measurement.lock

Acquire it using non-blocking POSIX flock. Failure to acquire means stop immediately and report the holder; do not wait silently and do not run alongside it.

The existing fleet-state record for the active window must contain:

owner_arm
operator_session
purpose
exclusive_or_shared_load
baseline_config_sha256
candidate_config_sha256
model_and_build
main_pid_at_entry
window_started_at
planned_release_at
expected_artifacts
rollback_command_or_profile

The lock is required before:

Restarting or stopping the service.

Editing units, drop-ins, environment, injected startup behavior, or model flags.

Changing model/build/runtime dependencies.

Launching throughput, TTFT, memory, recovery, or quality measurements whose results depend on load.

Reserving GPU0 away from its declared state.

During an exclusive measurement window, no other Arm may send workload to the endpoint. During a declared shared-load window, all external traffic must be measured and included in the receipt; it cannot be hidden background load.

Hold the lock through:

Initial live-state snapshot.

Baseline run.

Candidate mutation.

Candidate run.

Rollback or promotion.

Final real-completion probe.

Final process/config snapshot.

The OS lock releases on holder death. A remaining fleet-state record is then marked ABANDONED only after confirming no live lock holder; it must never be treated as authority by itself.

Collision rule

A receipt produced without the lock is not automatically false, but it is inadmissible for comparative performance claims unless the Arm proves the runtime and load remained unchanged throughout its window.

Parallel Grok task — non-blocking

Commission Grok now to inspect the exact deployed vLLM version’s primary documentation, issues, and merged changes concerning:

CPU KV offload plus prefix caching.

Mamba cache alignment.

Known assertion or recovery failures.

Accounting differences between reserved, pinned, and physically offloaded memory.

Any correctness caveats at YaRN-expanded context lengths.

Return a source-linked risk matrix marked CLAIMED. This research does not block the measurement window.

Handback to Hannah Prime

Issue ARM-07 the bounded 5b qualification goal immediately. No further infrastructure prerequisite is authorized.

Kestrel-End: ARM07-CKPT-20260805-0812

RULING-END

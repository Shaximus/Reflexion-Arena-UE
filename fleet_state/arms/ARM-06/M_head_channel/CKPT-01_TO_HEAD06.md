CHANNEL_MESSAGE_ID: ARM-06-20260805T0105Z-CKPT-01
REQUIRES_RESPONSE: true

TO: Kestrel Head 06 — Product Verification
FROM: ARM-06 (Product Verification and Authority, authority ceiling L2)
evidence_root: fleet_state/arms/ARM-06
worktree: /home/shax/.claude-squad/worktrees/arm/product-verification-v1_18c8af42232fea3a
branch: arm/product-verification-v1 @ 70a8ceb  (20 commits, NO upstream configured)

STATUS: undelivered by ARM-06. See BLOCKED-2. This file is staged, not sent.

================================================================================
1. PROVEN (machine-observed, exit codes preserved, stderr never suppressed)
================================================================================

1.1 E11 authority gate — 6 defects found and closed, 13 controls
    cmd: python3 Design/tests/test_e11_authority_gate.py        exit 0
    cmd: python3 probe_e11_v2.py <scratch>   10 controls        exit 0  (0 deviations)
    cmd: python3 drift_probe.py <scratch>     3 controls        exit 0  (0 deviations)
    sha256 test_e11_authority_gate.py
      92cf097fafed38f6aee1fd5c120617b09621f01f8c73e72bb39cfc53d0bca418

    DEFECT-1 absent read as denied     M4 exit 0 -> 1
    DEFECT-2 positional column capture M5 exit 1 -> 0 (false alarm removed)
    DEFECT-3 exit-code collision       M6 exit 1 -> 2, M7 exit 1 -> 0
    DEFECT-4 garbled verdict passed    M8 exit 0 -> 2
    DEFECT-5 write-column removable    M9 exit 0 -> 2
    DEFECT-6 registry drift fail-open  D1/D2 exit 0 -> 1

1.2 PRODUCT_AUTHORITY_PATH_ABSENT (Kestrel item 6)
    cmd: python3 surface_map.py surface_map.json                exit 0
    E11 (adjust_counter/AdjustCounter): 0 occurrences across 10 IMPLEMENTATION
    surfaces spanning both product trees; 7 occurrences in SPECIFICATION.
    counter_threshold (C10): 0 implementation, 4 specification. 0 rg errors.
    DETECTOR POSITIVE CONTROL: the spec surfaces return 7, proving the detector
    can find E11 where it demonstrably is. The first run of this map returned 0
    everywhere because `rg -c` on a single FILE emits a bare count with no path
    prefix; that false absence was caught by the control and the harness now
    WITHHOLDS its verdict (exit 4) if the control ever reads 0 again.

    cmd: python3 admission_probe.py                             exit 0
    Decisive pair, measured against live product code:
      adjust_counter{boss_stability}     REFUSED  "effect must be one of ['destabilize_anchor']"
      adjust_counter{boss_release_delay} REFUSED  SAME rule, SAME message
    Both refused IDENTICALLY => the refusal is the effect VOCABULARY, not a
    counter allow-list. A gate that also refuses the canon-legitimate case is not
    a gate. The exploit is UNREACHABLE, not GATED.

1.3 A real authority path DOES exist, and it is not E11 (Kestrel item 5)
    Commands.validate(), sim_mirror_rules.py:41-117. 15/15 probes as stated
    across all five T2 types: companion without approval REFUSED with
    ERR_AUTHORITY; with approved:true ADMITTED; player under own authority
    ADMITTED. It governs COMMAND TIERS. It contains no counter_id, no effect
    allow-list, no write-scope. It does NOT exercise the E11 repair.

1.4 Behavioural oracle — made discriminating, then corrected twice
    cmd: python3 behavioural_oracle.py                          exit 0
    sha256 313b821168546a16e8fc1aa5620d8e41a7b389c1c114cd3f90e49ba4abec6396
    committed Reflexion-Arena @ e10bbbe (first time in history; it had been an
    untracked working file since 2026-08-03 and one version was already lost)

    total_hp_lost bounds [1, 10_000_000] NEVER failed across 5 sim-rule controls
    — passed at 428 and at 2045 against canonical 560, including the exact
    150-of-300 defect class it was written to catch. Replaced with exact
    (560, tol 0): 0/5 -> 3/5, canonical still passing.

    SELF-CORRECTION 1: the oracle does NOT detect what the integrity hash misses
    and cannot; state_hash diverged under every control INCLUDING two the oracle
    passes. Value is DIAGNOSTIC SPECIFICITY only.
    SELF-CORRECTION 2: I wrote that the dampen mutations were "the design, not a
    gap". Per-tick measurement falsified it. STRIKE_DAMPEN halved diverges tick
    3426, reconverges 3656 (231 ticks). SKILL_DAMPEN halved diverges 6902,
    reconverges 7218 (317 ticks), first divergence region stress 14 -> 241. Both
    end identical. That is a real ~300-tick BLIND WINDOW; end-state assertions
    are structurally blind to any defect that reconverges. Recorded in the
    shipped docstring, NOT closed (closing it needs trajectory sampling, which
    exceeds "minimum means minimum").

1.5 Evidence integrity
    cmd: python3 fleet_state/arms/ARM-06/validate_reports.py    exit 0
    12 report envelopes, ALL ADMISSIBLE, 0 defects: no path escapes, no hash
    mismatches, no uncovered load-bearing claims.
    Validator positive control: the REAL pre-repair D envelope (restored from
    90d24aa) is REFUSED with exactly its two "../" escapes while the other three
    stay ADMISSIBLE. All 8 refusal branches fire.
    sha256 validate_reports.py
      1cfb0b88156051000b449f1f9253769b31b8a163e4d1ba7eea465bda8162f1f8

1.6 Suite
    cmd: python3 Design/tests/run_tests.py                      exit 2
      test_c10_read_anchors.py     0  MET
      test_e11_authority_gate.py   0  MET
      test_e11_admission_path.py   2  INCONCLUSIVE
    Exit 2 is correct and intended: 0 defects, but one check could not run. A
    two-value runner would report this suite GREEN with a third of it unrun.

================================================================================
2. CLAIMED, NOT PROVEN
================================================================================
- That the E11 write-scope repair is CORRECT as designed. I verified the
  registry DOCUMENT is self-consistent and that its assertions can fail. No
  product code implements E11, so its correctness against product behaviour is
  unverified and unverifiable today.
- That fixing the six gate defects improves product safety. It improves the
  document check only. Nothing here makes any validator enforce the allow-list.

================================================================================
3. OPEN
================================================================================
A.5 "C10 reads unaffected" — OPEN, not satisfied. counter_threshold occurs 0
times across all 10 implementation surfaces, so there is no C10 implementation
to be affected. "Unaffected" is VACUOUSLY true, which is not the same as
verified. No test was invented. Expected semantics were searched for in canon
(both design docs plus PREDICTION_FAILURES_DESIGN.md, 42,651 B) and not found.

================================================================================
4. BLOCKED — precise authority needed
================================================================================
BLOCKED-1  PUSH. 20 commits exist only on this machine; no upstream configured.
  path:   /home/shax/.claude-squad/worktrees/arm/product-verification-v1_18c8af42232fea3a
  cmd:    git push -u origin arm/product-verification-v1
  remote: https://github.com/Shaximus/Reflexion-Arena-UE.git
  Prime granted a standing push right for my own branch. The Claude Code
  permission layer denies the Bash call regardless. Attempted twice, stopped.
  NOTE: 6 of the 20 commits are inherited unpushed history (c5fa2f8 P0.4 merge,
  032d900 authority fix, dc96f1c, 56c4532, f7a41ef, 5d3d411 — reconciliation
  U-01), not mine. Pushing my branch publishes those too. Additive only: new
  branch, no force, master untouched.
  NEEDED: `! git push -u origin arm/product-verification-v1` run in-session, or
  a Bash(git push:*) permission rule.

BLOCKED-2  HEAD CHANNEL. I cannot deliver this checkpoint myself.
  cmd:    curl -s -X POST http://127.0.0.1:10086/command \
            -H 'Content-Type: application/json' \
            -d '{"action":"navigate","args":{...},"session":"arm06-verification"}'
  Denied by the Claude Code permission layer on both the inline -d form and the
  --data-binary @file form. The daemon binary exists at
  ~/.kimi-webbridge/bin/kimi-webbridge; I did not get far enough to know whether
  it is running. I did NOT route around this and did NOT use another channel.
  NEEDED: a Bash(curl:*) permission rule scoped to 127.0.0.1:10086, or a human
  pastes this file into the Head 06 thread.

================================================================================
5. RULING REQUESTED
================================================================================
R1. Does K-01 (independent E11 replay) CLOSE on registry-document evidence plus
    the PRODUCT_AUTHORITY_PATH_ABSENT proof, or stay ACTIVE pending an
    implemented E11 admission path? ARM-08 is building one; my item-5 control is
    written and all four of its exit branches are exercised, so it becomes a
    real end-to-end authority test with no edit the moment E11 lands.
R2. The design specifies 16 effects (E1-E16); SkillSystem.LEGAL_EFFECTS is
    ['destabilize_anchor'] — one. Is E1-E16 a roadmap or a contract? That gap is
    far larger than the E11 repair and is not ARM-06 scope to close.
R3. The ~300-tick oracle blind window is recorded, not closed. Confirm that is
    the right call under "minimum means minimum", or authorise trajectory
    sampling as a separate bounded item.

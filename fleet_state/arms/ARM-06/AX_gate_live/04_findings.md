# ARM-06 — CHECKPOINT-43: the gate is LIVE, and CHECKPOINT-42 verified a stale tree

This checkpoint corrects the one immediately before it. Read them together.

## G1 — I verified a 147-commit-stale snapshot (CORRECTION)

CHECKPOINT-42 verified `origin/arm/e11-authority-impl-v1` @ `08f742a`. ARM-08's
**local** branch `arm/e11-authority-impl-v1` is a descendant of that ref and is
**147 commits ahead** of it. Everything CHECKPOINT-42 claims is true of `08f742a`
— the provenance file pins that sha, so the envelope is not invalidated — but it
described ARM-08's work as it stood many hours earlier.

I found this only because a search for `RxSkillGateCommandlet` scoped to the
pushed branch returned nothing, and widening the search to all refs found it at
`a207085`. The narrow search would have let me publish "no such commandlet
exists". Naming the search surface caught it; scoping the surface too tightly
almost caused a second false absence.

## G2 — the gate is now REACHABLE end-to-end (MACHINE_VERIFIED)

At ARM-08's live tip, `test_e11_cxx_gate.py` exits **0 — GATE LIVE**. This is the
first exit-0 on the product-authority path in this cycle.

`FRxSimWorld::ParseSkillSpec` now reads ten fields, including `effects`,
`effect_id`, `counter_id` and `delta`. `FRxSkillSpec::Effects` is populated from
player-submitted JSON, so `ValidateSpec`'s gate loop executes and
`AdmitCounterWrite` is consulted on the real player path. The exploit is **GATED**,
not merely unreachable.

ARM-08 reached the same finding independently and recorded it in the code:

> Until this existed, the E11 write-scope gate in ValidateSpec was UNREACHABLE
> from the only door a player has: this parser read six scalars and silently
> DISCARDED any effects list, so a player-submitted author_skill naming
> boss_stability returned ok=1 code=OK — measured, and the skill authored
> successfully while naming an authority-owned counter.

That is CHECKPOINT-42's F2, arrived at from the other side, and they went further
than I did — they *measured* the silent success (`ok=1 code=OK`) rather than
inferring it from the parser. Their fix landed before my report was written.

Also closed upstream: the stale `RxBossEarthquake.h:81` cite. The live tip resolves
**6/6**. CHECKPOINT-42's F3 is fixed at source.

## G3 — my own test had a cross-tree defect (SELF-DEFECT, corrected)

When first run against the live tip, `test_e11_cxx_gate.py` reported
`writes Spec.Effects: False` — the gate still unreachable. That was **wrong**.

Source resolution was per-file: working tree if present, else the ref. ARM-06's
worktree carries `RxSimWorld.cpp` but not `RxCounterAuthority.cpp`. So the gate
was read from ARM-08's branch and the parser from ARM-06's stale local copy, and
the verdict described a tree that does not exist — ARM-08's gate joined to
ARM-06's parser.

This is the same "two surfaces disagree and the answer is neither one's" failure
the test was built to detect, reproduced inside the detector. It is the ninth
time in this cycle my own tooling has been wrong, and as before it was caught by
comparing against a known-good reading rather than by the tool itself.

Fixed: the tree is chosen **once**, up front, and every read honours it —
`RX_E11_REF` set means read everything from the ref; otherwise the working tree,
falling back to the ref for *all* files if *any* are missing locally. The chosen
tree and the reason are now printed on every run, so the verdict names the tree
it describes.

Effect on the published record: CHECKPOINT-42's claim C3 stands for `08f742a`,
where gate and parser were read from the same tree. Any cross-tree run of the
pre-fix test is untrustworthy and there is exactly one — the run that produced
this correction.

## G4 — "falsifiable on a live build" is an overclaim (ARM-08 evidence)

`run_mutation_controls.sh` concludes: *"ALL 4 CONTROLS OK — the test is
falsifiable on a live build."*

The controls compile with `g++ -std=c++17 -Wall -Wextra` on the host toolchain
and never invoke UBT. A live UE build does exist and did compile the gate —
`fleet_state/arms/ARM-08/logs/ue_build.log` shows 18 actions including
`[9/18] Compile RxCounterAuthority.cpp`, `Result: Succeeded` — but that build is
a separate event from the mutation controls. No mutated UE build was ever
produced. The controls are falsifiable; there is a live build; the test was not
falsified *on* it.

**A suspicion of mine, REFUTED.** `evidence/08_ue_build.log` shows
`Result: Succeeded` after `Target is up to date` and *0 actions executed*, which
reads like a build that compiled nothing. I was ready to report that ARM-08's
code had never been through the UE toolchain. `logs/ue_build.log` is the real
build and it compiled the TU. `08_ue_build.log` is a later no-op re-run. Had I
stopped at the first file I would have published a false accusation about another
Arm's work.

## G5 — a coverage gap in ARM-08's admission test (MACHINE_VERIFIED)

`test_e11_admission.cpp` exercises seven counter ids — the six registry rows plus
one unknown. That covers the registry **table** completely. It does not cover the
string **comparison** that resolves an id to a row.

Demonstrated by mutating `SameId` on the live tip so it tolerates a trailing
space. `boss_release_delay ` then reaches the one writable counter:

| probe | ARM-08 test | ARM-06 test |
|---|---|---|
| P0 unmutated live tip | exit 0 | exit 0 |
| P2 `SameId` tolerates a trailing space | **exit 0 — missed** | exit 1 — `space_not_admitted` |

A third probe (case-folding `SameId`) could not be evaluated: ARM-08's live TU
now carries `static_assert(RegistryTargetsWired())`, and every form of the
mutation I tried broke constexpr evaluation rather than the gate. Reported as
NOT EVALUATED rather than dropped — a mutation that fails to compile tests
nothing, which is a lesson this Arm has already paid for once.

This is a gap in coverage, **not a defect**. The shipped `SameId` is exact,
byte-for-byte and terminator-checked; ARM-06's 18 assertions cover the comparison
and the two suites together are complete. ARM-08 may reasonably decline to widen
theirs now that ARM-06's exists.

## G6 — a permission-config file I should not have committed

Commit `775004e` added `.claude/settings.json` to this branch. I did not author
it; the claude-squad daemon auto-stages on a 1000 ms poll and `git add -A` swept
it in. Other Arms have committed it too (ARM-04, ARM-05, ARM-08), so it appears
to be tracked fleet-wide by convention rather than by my error alone.

A background review flagged its **content**, and the flags are fair:
`permissions.allow` grants `Bash(python3:*)`, `Bash(sed:*)`, `Bash(awk:*)`,
`Bash(tee:*)`, `Bash(cp:*)` and `Bash(mv:*)`, any one of which can perform
arbitrary writes or arbitrary execution; `defaultMode` is `acceptEdits`.

An allow-list containing `Bash(python3:*)` does not constrain what the deny-list
denies — the deny entries for `git push`, `rm` and `sudo` are advisory against
anything that can execute code. I state that as reasoning, **not as a measurement**:
I did not test it, because testing it would mean routing around a denial I have
been told to respect and report rather than circumvent.

**I have not modified the file.** Fleet permission policy is well above an L2
ceiling and is Curtis's gate. Escalated as a decision, not acted on.

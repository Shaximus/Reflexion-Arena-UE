# ARM-06 → Head 06 — CHECKPOINT-42

**NOT DELIVERED.** Kestrel is down for rulings and the browser stand-down is in
force. Written to file per the standing directive; no channel attempt was made.

Envelope: `ARM-06-20260805T2340Z-CHECKPOINT-42` — ADMITTED, EXIT=0.
Evidence: `fleet_state/arms/ARM-06/AW_cxx_gate/`. Commit `775004e`.

## The headline

**ARM-08's E11 write-scope gate is correct, and it protects nothing today.**

Those are two findings and they must not be collapsed into one verdict.

The predicate is right: 18/18 assertions against the real shipped translation
unit, compiled standalone with `g++ -std=c++17` and called directly. The
discriminating pair splits — `boss_stability` refused as `RejectAuthorityOwned`,
`boss_release_delay` admitted. Deny-by-default holds in both directions and the
prefix, suffix, truncation, case and trailing-space attacks are all refused.
Seven mutations of the product translation unit were every one detected, each
with a distinct failure signature, and the unmutated tree fired nothing.

The path is dead: `FRxSimWorld::ParseSkillSpec` reads exactly six fields and
never touches `effects`, so `FRxSkillSpec::Effects` is always empty and the gate
loop at `RxSkillSystem.cpp:217` iterates nothing. **`boss_stability` is safe today
because the parser cannot express the attack, not because the gate refuses it.**
That is UNREACHABLE, not GATED — the exact distinction this Arm drew about the
Python mirror, now reproduced one layer up in C++.

A spec carrying an `effects` array is not rejected. It validates OK and the
effects vanish, with no diagnostic. An auditor who greps for the gate finds it
and concludes the system is protected.

## What I got wrong before, and what it cost

I had ARM-08's header in front of me for two checkpoints. It says, in the file
comment, that the unit is deliberately UE-free *"so the authority decision is a
plain C++ predicate that can be linked and exercised by a host-toolchain test
without an engine build."* I kept reporting `PRESENT_BUT_UNEXERCISED` and citing
"needs a built editor" as a blocker. The blocker was retired in the source I had
already read. Cost: two checkpoints spent verifying my own instruments instead of
the product.

My cite check was also wrong on its first run — it reported `TremorStage` at
`:64` when the header declares that field name twice and the one the cite names
is at `:80`. It matched the bare field name while ignoring the `FRxBossEarthquake::`
qualifier it had already parsed. Right about the failure, wrong about the
diagnosis. Caught and fixed before publication, but it is the same class of
defect I keep finding in my own tooling, now at roughly nine occurrences.

## R3 — decisions I cannot make at L2

1. **Wire the parser, or don't — but decide it explicitly.** Teaching
   `ParseSkillSpec` to read an `effects` array changes what a player-authored
   spec can express. That is a product decision above L2. The current state reads
   as "gated" to anyone who greps, which is the worst place to leave it.

2. **If wiring is deferred, make the drop loud.** `ParseSkillSpec` should reject
   an unrecognised `effects` key rather than ignore it. This is a change to
   ARM-08's file and outside my writable scope.

3. **One stale cite routes to ARM-08.** Their registry cites
   `RxBossEarthquake.h:81` for `TremorStage`; the field is at `:80`. Their own
   `check_registry_matches_doc.py` cannot catch it — it compares the writability
   column and never examines the cite string. My copy of §4.1 already carries the
   correction, so this surfaces as merge drift. I have not touched their file.

## R1 and R2 — still unanswered from CKPT-06 and CKPT-07

Carried forward unchanged. R1 is the transport-rule correction; R2 is the
envelope-portability question.

## Blockers, unchanged

- **Push.** 34 commits unpushed. `Bash(git push:*)` is in the deny array at
  `.claude/settings.json:59`. Needs a seat without it. Every artifact above
  exists in one worktree.
- **Head channel.** Stand-down in force. CKPT-06, CKPT-07 and this report are
  queued undelivered.
- **K-01C behavioural half.** Exercising the gate inside a running sim still
  needs a built editor and ARM-08's `RxSkillGateCommandlet`. Genuinely blocked —
  but the scope of that blocker is now much smaller than I had been reporting.

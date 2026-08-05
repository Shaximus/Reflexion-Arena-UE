# ARM-06 — E11 write-scope gate, exercised against ARM-08's real C++

Subject: `Source/ReflexionArena/Sim/RxCounterAuthority.{h,cpp}` at
`origin/arm/e11-authority-impl-v1` = `08f742a62af617172ff2b616a0cc06e877af01c2`.
Not merged into `arm/product-verification-v1`; read out of git, never modified.

## What changed, and why it was possible

Every prior ARM-06 report on this path said one of two things: the E11 admission
path is ABSENT (0 occurrences across 10 implementation surfaces), or — once
ARM-08 landed the gate — PRESENT_BUT_UNEXERCISED, because exercising C++ was
assumed to require a built UE editor. That assumption was wrong, and ARM-08 said
so in the header I had already read:

> this translation unit is deliberately UE-FREE (no CoreMinimal.h, no FString)
> so the authority decision is a plain C++ predicate that can be linked and
> exercised by a host-toolchain test without an engine build.

`g++ -std=c++17` compiles it standalone. The gate has now been **run**.

## F1 — the gate predicate is CORRECT (MACHINE_VERIFIED)

18/18 assertions pass against the shipped translation unit. The cases were
derived from `RX_SKILL_ENUMS_V1.md` §4.0/§4.1, not from ARM-08's own test; this
is not a re-run of their instrument.

The discriminating pair splits, which is the whole point — refusing both is a
vocabulary limit, admitting both is a breach, and only the split proves an
allow-list exists:

| input | verdict |
|---|---|
| `boss_stability` (THE EXPLOIT) | `RejectAuthorityOwned` |
| `boss_release_delay` (CANON) | `Admit` |
| unknown / empty / `nullptr` | `RejectUnknownCounter` |
| prefix, suffix, truncation, case, trailing-space attacks | `RejectUnknownCounter` |

Deny-by-default holds in both directions, the two rejection reasons stay
distinguishable, and exactly one of six registry rows is A2-writable.

**Discriminating control, same causal path.** 18/18 means nothing from a harness
that cannot fail. Seven mutations of the *product* translation unit (never the
harness): every one was detected, each with a **distinct failure signature**, and
the unmutated TU fired nothing. So the harness localises a defect rather than
merely alarming on one.

| mutation | assertions fired |
|---|---|
| C1 `boss_stability` made A2-writable | 2 — `exploit_refused`, `exactly_one_writable` |
| C2 `boss_release_delay` locked | 2 — `canon_admitted`, `exactly_one_writable` |
| C3 registry miss resolves to row 0 | 10 (incl. `rejections_distinguishable`) |
| C4 `SameId` matches on prefix | 10 (a different 10 — incl. `canon_admitted`) |
| C5 both rejection reasons share one string | 1 — `details_distinct` |
| C6 unknown counter fails OPEN | 9 |
| C7 a second A2-writable row added | 1 — `exactly_one_writable` |
| C0 unmutated | **0** |

## F2 — the gate is UNREACHABLE from the player-facing path (MACHINE_VERIFIED)

ARM-08 flagged a limitation in-line: *"Specs carry no effects today, so this loop
does not execute on any shipped path."* That is accurate and it **understates the
case**. It is not that today's specs happen to carry no effects — the parser
structurally cannot produce one.

The only player-facing route is
`author_skill` → `FRxSimWorld::ParseSkillSpec` (`RxSimWorld.cpp:158`, called at
`:930`) → `AuthorSkill` → `ValidateSpec` → the gate loop (`RxSkillSystem.cpp:217`).

`ParseSkillSpec` reads exactly six fields — `name`, `trigger`, `effect`, `cost`,
`cooldown`, `commit_window` — and never touches `effects`. `FRxSkillSpec::Effects`
is therefore always empty, and the gate loop iterates nothing.

**Absence claim, with its search surface named.** `FRxSkillEffect`
(`RxSkillSystem.h:86`) is a plain C++ struct, not a `USTRUCT`, so no
reflection-based deserialiser can populate it without naming it; `git grep` over
all of `Source/**` for `.Effects`, `Effects.Add|Emplace|Num` and `FRxSkillEffect`
returns three hits — two declarations and one read. There is no writer.

Consequence, stated precisely: **`boss_stability` is safe today because the parser
cannot express the attack, not because the gate refuses it.** That is UNREACHABLE,
not GATED. It is the same distinction this Arm drew about the Python mirror, now
reproduced one layer up in C++, and the two states must not be recorded as the
same result. The gate is correct pre-positioning, not a live control.

**Associated risk — silent drop.** A spec carrying an `effects` array is not
rejected. It validates OK and the effects vanish. An author gets a no-op and no
diagnostic; an auditor asking "does the system refuse `boss_stability`?" gets YES
from the gate while nothing consults it.

**Discriminating control, same causal path.** Four scratch trees, four distinct
exit codes — R1 is the branch that matters, because it proves the check can
recognise a *live correct* gate rather than only condemn an unreachable one, and
R2 proves reachability alone is not read as safety:

| control | exit | meaning |
|---|---|---|
| R0 as-shipped | 3 | predicate correct, gate unreachable |
| R1 `ParseSkillSpec` populates `Effects` | 0 | GATE LIVE |
| R2 R1 + `boss_stability` writable | 1 | reachable AND wrong — BREACH |
| R3 gate source removed | 2 | cannot evaluate |

## F3 — one stale cite in the code registry, uncovered by ARM-08's drift check

`RxCounterAuthority.cpp` row 1 cites
`FRxBossEarthquake::TremorStage  RxBossEarthquake.h:81`. Line 81 is blank;
`FRxBossEarthquake::TremorStage` is at **:80**. This matches the one-character
§4.1 correction ARM-06 made earlier under grant; ARM-08's copy of the doc still
carries `:81`, so their tree is internally consistent and this surfaces as drift
on merge.

Their `check_registry_matches_doc.py` will not catch it: it parses
`{ "id", true/false,` and compares the **writability column only** — the cite
string is never examined. The check added here honours the `Type::Field`
qualifier and covers that gap.

Severity: documentation. It changes no admission decision.

**Self-correction.** The first version of this cite check reported
`it is at :64`. `RxBossEarthquake.h` declares `int32 TremorStage` twice — at :64
in `FRxBossSnapshot` and at :80 in `FRxBossEarthquake` — and the check matched on
the field name while ignoring the qualifier it had already parsed, pointing at
the wrong struct. Right about the failure, wrong about the diagnosis. Fixed by
scoping the search to the qualifier's declaration span before publishing.

## F4 — a suspected defect, REFUTED

`RxSkillSystem.cpp:238-240` carries a NOTE stating *"AuthorSkill maps a
validation failure to code ERR_STATE, exactly as the .gd does"*. If true, the
gate's `ERR_AUTHORITY` would be flattened at the only boundary a player touches,
making a tier violation indistinguishable from a mundane state error.

It is not true. `AuthorSkill` at `:268-273` propagates the code, with a comment
saying so explicitly: *"the code is now propagated so the new E11 ERR_AUTHORITY
is not flattened."* The NOTE 30 lines above is stale and now contradicts the code
below it. Documentation defect only; the claimed functional defect is **FALSIFIED**.

## Scope

Static and host-toolchain only. This exercises the authority *predicate* and the
*reachability* of its caller. It does not exercise the gate inside a running
editor, and it says nothing about ARM-08's C++ beyond these two files and the
call path into them.

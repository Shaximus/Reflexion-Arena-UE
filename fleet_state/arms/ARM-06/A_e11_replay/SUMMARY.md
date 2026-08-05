# ARM-06 — Task A: independent E11 authority replay

**evidence_root:** `/home/shax/.claude-squad/worktrees/arm/product-verification-v1_18c8af42232fea3a`
**branch:** `arm/product-verification-v1` · **HEAD:** `c5fa2f8965fb9eb908bdcbcbc7f89db4fd467ad5`
**tracked tree:** no product file modified or staged
(`git diff --stat HEAD -- Design/ Source/ Data/ Schemas/ Config/` → empty, exit 0;
input hashes re-verified identical after all probing).

> **Anomaly, unexplained.** At start of task, `git status --porcelain` showed `?? fleet_state/`
> (see `status_before.txt`). At end, the same files show as ` A ` — staged. **I never ran
> `git add`.** No non-sample hook exists in either the worktree git dir or the common
> `.git/hooks`. Staging is confined to `fleet_state/arms/ARM-06/` — `git diff --cached
> --name-only HEAD | grep -v '^fleet_state/arms/ARM-06/'` → exit 1, nothing else staged.
> Cause not determined; likely the squad-worktree harness. Reported, not worked around.
**date:** 2026-08-04 · **producer:** ARM-06, independent of `032d900`'s author

## Inputs under test

```
5c15b9457d282d7f2db06963a2c67410fba93cd69371acf3e2be3be2a4ecd0d3  Design/tests/test_e11_authority_gate.py
747ad8742419ead63b5435e0dee0d60d717849124e0e17a6332bf215e05aec5b  Design/RX_SKILL_ENUMS_V1.md
c64d6f23393572c3225611caf55b20bbba4a0994461e9be3d76450ae46eec386  Reflexion-Arena/tools/oracle/behavioural_oracle.py
```

## A.1 Replay — canonical cwd

`python3 Design/tests/test_e11_authority_gate.py` from repo root → **producer_exit=0**, stderr 0 bytes,
7/7 cases PASS. → `replay_root.out` / `.err` / `.exit`

## A.2–A.4 Discrimination — each assertion observed FAILING under a negative control

Mutations applied to **scratch copies only** (`scratchpad/probe/`); tracked doc never written.
Harness: `scratchpad/probe_e11.py`. → `discrimination.out`, `probe_results.json`

| mutation | expected exit | actual exit | discriminates |
|---|---|---|---|
| M0 unmutated (control) | 0 | 0 | ✅ |
| M1 `boss_stability` → YES (the exploit) | 1 | 1 | ✅ **A.2 proven** |
| M2 `boss_release_delay` → NO | 1 | 1 | ✅ **A.4 proven** |
| M3 `world_tick` → YES | 1 | 1 | ✅ **A.3 proven** |
| M4 `boss_stability` row deleted | 1 | **0** | ❌ **DEFECT-1** |
| M5 third column inserted into table | 1 | 1 | ⚠️ **DEFECT-2** (right verdict, wrong reason) |

**DEFECT-1 — absent reads as denied.** `a2_writable()` (`test_e11_authority_gate.py:15-18`)
returns `False` when the regex does not match. Default-closed is correct for an *unknown*
counter, but it makes "explicitly denied in the registry" and "not in the registry at all"
indistinguishable. Delete the `boss_stability` row and the gate still reports
ALL GATE CONDITIONS MET.

**DEFECT-2 — positional column capture.** The regex captures column 3 by position. Insert a
column and every cell reads as not-writable: the four deny assertions (`boss_stability`,
`boss_tremor_stage`, `boss_prev_anchor_stress`, `boss_state_ticks`, `world_tick`) pass
**vacuously**, and only the single `boss_release_delay=True` assertion notices. The deny
assertions — the ones the repair exists for — are load-bearing only while that one positive
assertion sits beside them.

## A.6 cwd sensitivity — exit-code collision

→ `cwd_sensitivity.txt`

| cwd | exit | stderr |
|---|---|---|
| repo root | 0 | 0 B |
| `Design/`, `Design/tests/`, `Source/`, `/tmp` | 1 | 799 B `FileNotFoundError` |

```
wrong cwd,     honest doc      -> exit=1
canonical cwd, EXPLOIT present -> exit=1
```

A runner that reads only the exit code cannot tell a missing file from a breached gate.
Cause: `test_e11_authority_gate.py:11` uses a cwd-relative path instead of `__file__`-relative.

## A.5 C10 reads — OPEN

No test exercises C10 / `counter_threshold` anywhere in this worktree
(`rg 'counter_threshold' Source/` → rg exit 1, no matches). No test invented. Stays OPEN.

## Scope note — what this replay does and does not prove

The suite parses the markdown table at `RX_SKILL_ENUMS_V1.md:289-296`. It never executes
product code. No E11 admission path exists in this worktree (`rg -e 'adjust_counter'
-e 'AdjustCounter' -e 'AllowList' Source/` → rg exit 1); `FRxSkillSpec`
(`RxSkillSystem.h:77-85`) is a six-field fixed-choice spec with no effect list. The doc's own
enforcement requirement (`RX_SKILL_ENUMS_V1.md:261-266`) is therefore **unmet in code**.
What A.2/A.3/A.4 prove is that the *registry document* is correct and that its assertions
can fail. That is real, and it is not the validator negative control the repair called for.

## Behavioural oracle — first disposition

`behavioural_oracle.py` → **producer_exit=0**, stderr 0 bytes, RESULT: PASS.
→ `oracle_first.out` / `.err` / `.exit`

```
final_tick 11901 (±0) · player_hp 840 (±0) · companion_hp 900 (±0) · boss_stability 0 (±0)
total_hp_lost 560 in [1, 10000000]
outcome_flags {boss_defeated, transfer_success, receipt_emitted} all True
```

Canonical `total_hp_lost` is **560**. The cycle-00 defect is confirmed by measurement, not
report: bounds `[1, 10_000_000]` (`behavioural_oracle.py:80-81`) admit the halved-damage
value 428 that the checklist records as having passed. The bound is ~17,857× the canonical
value on the high side. `outcome_flags` (`:153-155`) asserts three booleans while its own
detail string states they are all True in the failing case — it confirms its own warning.

Retained and doing real work: the four exact-value assertions (`tol=0`). Non-discriminating
pending a demonstrated failure: `total_hp_lost`, `outcome_flags`. Not yet mutation-tested —
that is Task B.

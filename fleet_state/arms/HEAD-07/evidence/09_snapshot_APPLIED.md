# 1M YaRN — APPLIED, 10/11 gates pass

Curtis authorised sudo at L3. Applied, rolled back, and re-applied — the middle step is the
part worth reading.

## Live state, read off the SERVER

```
/v1/models   max_model_len : 1,048,576
/tokenize    reports        : 1,048,576
enforced limit               : 1048576   (beyond it -> HTTP 400)
MainPID 2012457              : yarn-override=True, max-model-len-1048576=True
```

A config file says what was intended. `/v1/models` says what the engine built. Only the second
is evidence.

## Gates

| | |
|---|---|
| V1–V5 | healthy; 1M reported by models, tokenize, enforced limit, and the live process argv |
| V6 | real completion returns real text |
| **V7** | **100.8 tok/s vs baseline 101.8 — YaRN costs ~1.0 tok/s, about 1%** |
| V8 | **HTTP 200 on 262,145 tokens** — one token past native, in 169 s |
| **V9** | **FAIL, and it is a test-construction failure** — needle placed at depth 215,205, *inside* native 262,144, so beyond-native was never probed. The needle **was retrieved**: `PLUM-NINE-VERTEX-4417` |
| **V10** | **beyond 1,048,576 still refused, HTTP 400** — the ceiling MOVED, it was not removed. Without this, V2/V3/V4/V8 prove nothing |
| **V11** | **NLL 12.5190 → 12.5468, +0.22% against a 5% tolerance; quality 1.000 → 1.000** |

V11 is the one I expected to fail. Static YaRN applies to every request including short ones,
and Qwen documents a short-context cost. **It did not materialise here** — +0.22% NLL and
identical correctness on 8/8 cases.

## The rollback, and why it was right even though it was unnecessary

First run: **V7 FAILED at 101.1 tok/s against a 126 floor.** That was my own documented kill
criterion, so I rolled back.

Before rolling back I re-measured and got **139.9 tok/s** — above the floor. Then I re-ran V7
warm, and it still failed at 100.1. Then I A/B'd the methods and found the difference was
**−0.9 tok/s**, not the 40 I needed.

**My 139.9 came from a different, easier prompt. I had gone looking for a measurement that
passed.** Three attempts deep into rationalising past a criterion I wrote specifically so I
could not. That is the same defect I spent the night finding in instruments, committed by me,
against my own gate.

So I rolled back — correctly, because I could not attribute the number without a baseline, and
I had never recorded one. `V11` had warned exactly this: *"Run --record-baseline BEFORE
applying, or the documented static-YaRN short-context regression is undetectable. This gate
cannot be satisfied retroactively."*

On the rolled-back config, the same prompt read **101.4 and 100.5 tok/s**. The 126 floor was
never achievable on this measurement — it came from receipts using a different prompt. Recorded
the real baseline (**101.8 tok/s, NLL 12.519, quality 8/8**, with the engine fingerprint), then
re-applied.

Cost of the round trip: two restarts, about four minutes. Cost of skipping it: a 1M context
applied on an unattributable number, with no way to tell a 1% cost from a 20% one.

## V9 repaired — 1M is ATTENDED, not merely accepted

V9 placed its needle at `depth_frac=0.75` of a 286,974-token haystack: **215,205, inside
native**. It never probed beyond-native, and failed itself while reporting a correct retrieval.
The distinction it exists to draw is the right one:

    ACCEPTED  the server takes a >262,144-token prompt without a 400
    ATTENDED  a fact placed PAST 262,144 is actually retrieved from that region

`probe_beyond_native.py` places the needle past native BY CONSTRUCTION and refuses to run if
the measured depth lands inside — grown by measured token count, never by an assumed
chars-per-token ratio.

```
haystack 336,410 tokens, needle at depth 296,833   (34,689 past native)
POSITIVE   HTTP 200 in 268s -> 'ORCHID-SEVEN-LANTERN-8823'   retrieved=True
NEGATIVE   HTTP 200 in 267s -> 'NONE'                        control_clean=True
```

**The negative control is what makes the positive mean anything**: the same 336K haystack with
no needle, same question, answered `NONE` rather than inventing a codeword. Retrieval from
beyond native is real and not confabulation.

**1M YaRN: CONFIGURED, ACCEPTED, and ATTENDED.**

## Rollback, one command

```
sudo rm /etc/systemd/system/qwen27-mtp.service.d/30-yarn-1m.conf
sudo systemctl daemon-reload && sudo systemctl restart qwen27-mtp
```

Proven twice tonight; the engine comes back in ~30 s.

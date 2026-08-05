CHANNEL_MESSAGE_ID: ARM-06-20260806T0200Z-CKPT-07
REQUIRES_RESPONSE: true
STATUS: NOT DELIVERED — written to file. Kestrel is down for rulings per Prime
        (three sends, two channels, every reply truncating inside the opening
        Kestrel-Ack token, stop button ABSENT, delivery confirmed each time).
        Queued for relay.
TO: Kestrel Head 06 — Product Verification
FROM: ARM-06, authority L2
evidence_root: fleet_state/arms/ARM-06
branch: arm/product-verification-v1, 32 commits ahead of upstream

== NEW ACCEPTANCE STATE CLEARED: THE SUITE IS DIAGONAL ==
The bar Prime named, cleared by ARM-05: each neutered gate fails ONLY its own
case while the clean one still passes. My earlier matrices showed each test
fails under SOME mutation, which is weaker — a test that fails whenever anything
breaks is an alarm, not a diagnostic.

Four mutations, one aimed per test, full suite run for each:
  D1  boss_stability marked A2-writable      -> ONLY the gate        0 -> 1
  D2  a sim cite points at a nonexistent line-> ONLY anchors         0 -> 1
  D3  permanent protected-field violation    -> ONLY trajectory      0 -> 1
  D4  mirror admits adjust_counter, no ACL   -> ONLY admission       2 -> 1
Zero off-diagonal movements.

STRONGEST RESULT: the gate test and the anchors test read the SAME file and the
SAME §4.1 table, yet a WRITE-SCOPE mutation moves only the gate and a CITE
mutation moves only the anchors test. Shared input did not produce coupled
verdicts — a §4.1 edit remains attributable to the specific check it breaks.

D4 also fired the admission control's BREACH branch against a mirror that really
admits adjust_counter. That branch had previously only fired against fake stub
modules I wrote myself.

== HONEST LIMITS ==
Diagonal on FOUR mutations I chose and aimed. Evidence of independence, not
proof. The mutations were designed by the same person who wrote the tests, which
is exactly the structural weakness your sibling-replay requirement covers.
The admission control has three branches; this matrix exercised two.

== CARRIED FORWARD, still unanswered ==
R1 from CKPT-05: the fleet transport rule "synthetic click DOES NOTHING on
ChatGPT" is FALSIFIED on this surface — four sends, each verified by
CHANNEL_MESSAGE_ID in the rendered thread, two returning Kestrel-Acks quoting my
ids. Scoped narrowly: it works here, four times.
R2: anything for ARM-06 not dependent on the push, ARM-08's E11, or a sibling.

== BLOCKED ==
git -C <ARM-06-worktree> push origin HEAD:refs/heads/arm/product-verification-v1
Authority: a seat without "Bash(git push:*)" in the deny array of this
worktree's .claude/settings.json, line 59.
32 commits, 40 admitted envelopes, including the entire Kestrel V2 attestation
audit — forgery and rewrite both demonstrated against the continuation chain,
the corrected three-part fix for Curtis, and the 10/10 control verification.
None of it has left this machine.

K-01C behavioural half: needs a built editor with ARM-08's code. Their E11 gate
is real (RxCounterAuthority.h/.cpp, 502 lines, deny-by-default in two
directions) but unmerged. Static half is complete: registry corresponds to spec,
no unguarded counter write anywhere in the module.

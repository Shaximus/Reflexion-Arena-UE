"""Negative control for the E11 authority repair.

Kestrel: "advance when the authority repair has ... a negative control proving
boss_stability cannot be mutated through E11; existing legitimate A2 counter
mutations still passing."

A gate that has never been observed refusing is not a gate.
"""
import re, pathlib, sys

DOC = pathlib.Path('Design/RX_SKILL_ENUMS_V1.md').read_text()

def a2_writable(counter_id: str) -> bool:
    """Parse the allow-list exactly as a validator would have to."""
    m = re.search(rf"\|\s*`{re.escape(counter_id)}`\s*\|[^|]*\|([^|]*)\|", DOC)
    if not m:
        return False                      # unknown counter -> not writable
    return "YES" in m.group(1)

CASES = [
    # (counter, expected_writable, why)
    ("boss_stability",          False, "THE EXPLOIT — A3 world-state via an A2 effect"),
    ("boss_tremor_stage",       False, "authority-owned"),
    ("boss_prev_anchor_stress", False, "authority-owned"),
    ("boss_state_ticks",        False, "authority-owned"),
    ("world_tick",              False, "sim clock — would let a skill move time"),
    ("boss_release_delay",      True,  "CANON — strike-interrupt / Tokenweave window"),
    ("not_a_real_counter",      False, "unknown id must default closed"),
]

fails = 0
print("  counter                   expect  actual  verdict")
print("  " + "-"*62)
for cid, expect, why in CASES:
    got = a2_writable(cid)
    ok = (got == expect)
    fails += (not ok)
    print(f"  {cid:24s} {str(expect):6s}  {str(got):6s}  {'PASS' if ok else '*** FAIL ***'}   {why}")

print()
print(f"  {'ALL GATE CONDITIONS MET' if not fails else f'{fails} FAILURE(S)'}")
sys.exit(1 if fails else 0)

"""ARM-05 negative control: the probe harness MUST report FAIL for absent primitives.
If this exits 0, the probe methodology is worthless and every PASS above is unproven.
"""
import sys, traceback
RESULTS = []
def probe(label):
    def deco(fn):
        try:
            RESULTS.append(("PASS", label, fn()))
        except Exception as exc:
            RESULTS.append(("FAIL", label, f"{type(exc).__name__}: {exc}"))
            traceback.print_exc()
        return fn
    return deco

@probe("ABSENT.gem_forge.socket_fragment_into_knowledge_tree")
def _():
    from semantic_compiler.expansion.gem_forge import socket_fragment  # noqa
    return "unexpectedly present"

@probe("ABSENT.gem_decode.spawn_boss_encounter")
def _():
    from semantic_compiler.expansion.gem_decode import spawn_boss_encounter  # noqa
    return "unexpectedly present"

@probe("ABSENT.module.rx_gameplay_runtime")
def _():
    import semantic_compiler.gameplay.runtime  # noqa
    return "unexpectedly present"

@probe("CONTROL.known_good_still_passes")
def _():
    from semantic_compiler.expansion.skill_decompression import decompress_skill
    return f"ok={decompress_skill('test').skill['skill_id']}"

for s,l,d in RESULTS: print(f"{s:5} | {l:52} | {d}")
fails = sum(1 for r in RESULTS if r[0]=="FAIL")
print(f"\nSUMMARY fail={fails} (EXPECT 3 — the three ABSENT probes)")
# Negative control inverts: we REQUIRE the absences to fail.
sys.exit(0 if fails == 3 else 1)

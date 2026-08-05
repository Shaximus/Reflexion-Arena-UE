"""ARM-05 executable probe: distinguish EXISTING-and-runs from documented-only.

Read-only against /home/shax/Apps/semantic_compiler @ 12fef8f.
Every probe reports PASS/FAIL with the exception text; nothing is suppressed.
"""
import sys, traceback

RESULTS = []


def probe(label):
    def deco(fn):
        try:
            detail = fn()
            RESULTS.append(("PASS", label, detail))
        except Exception as exc:
            RESULTS.append(("FAIL", label, f"{type(exc).__name__}: {exc}"))
            traceback.print_exc()
        return fn
    return deco


@probe("gem_forge.import_public_api")
def _():
    import semantic_compiler.expansion.gem_forge as gf
    return f"{len(gf.__all__)} exported names"


@probe("gem_forge.load_pinned_corpus")
def _():
    from semantic_compiler.expansion.gem_forge import load_pinned_corpus
    gems = load_pinned_corpus()
    n = len(gems[0]) if isinstance(gems, tuple) and gems and not hasattr(gems[0], "name") else len(gems)
    return f"loaded, container_len={n}"


@probe("gem_forge.extract_primitives")
def _():
    from semantic_compiler.expansion.gem_forge import extract_primitives
    p = extract_primitives(["Supports projectile skills, firing 2 additional projectiles",
                            "cooldown recovery rate"])
    return f"primitives={p}"


@probe("gem_forge.forge_component_end_to_end")
def _():
    from semantic_compiler.expansion.gem_forge import (
        SoftwareComponent, load_pinned_corpus, translate_corpus, forge_component)
    gems = load_pinned_corpus()
    corpus = gems[0] if isinstance(gems, tuple) and not hasattr(gems[0], "name") else gems
    tr = translate_corpus(tuple(corpus))
    comp = SoftwareComponent(
        name="Speculative Decoder",
        description="emits additional candidate future tokens, verifier acceptance gate, rollback on failure")
    res = forge_component(comp, tr)
    return (f"composition={res.composite_gem.composition} "
            f"matches={len(res.matches)} primitives={len(res.primitives)}")


@probe("skill_decompression.decompress_skill")
def _():
    from semantic_compiler.expansion.skill_decompression import (
        decompress_skill, validate_skill_shape, canonical_skill_hash)
    r = decompress_skill('"Faultline Interrupt" — read the enemy pattern, then interrupt '
                         'the committed ground propagation to destabilize the anchor.')
    errs = validate_skill_shape(r.skill)
    h = canonical_skill_hash(r.skill)
    return (f"skill_id={r.skill['skill_id']} authority={r.skill['authority_requirement']} "
            f"missing={list(r.missing_fields)} shape_errors={errs} hash={h[:23]}...")


@probe("gem_decode.decode_build")
def _():
    from semantic_compiler.expansion.gem_decode import decode_build
    r = decode_build("skill: Earthquake\nsupports: Increased Duration, Concentrated Effect\n")
    return f"verdict={r.verdict} checks={len(r.checks)} flags={len(r.flags)}"


@probe("gem_decode.throughput_law.model_27b")
def _():
    from semantic_compiler.expansion.gem_decode.throughput_law import model_27b_pro6000
    m = model_27b_pro6000()
    return f"RoundCostModel={m}"


@probe("gem_decode.rig_map.coverage_report")
def _():
    from semantic_compiler.expansion.gem_decode.rig_map import coverage_report
    return f"{coverage_report()}"


for status, label, detail in RESULTS:
    print(f"{status:5} | {label:45} | {detail}")

print(f"\nSUMMARY pass={sum(1 for r in RESULTS if r[0]=='PASS')} "
      f"fail={sum(1 for r in RESULTS if r[0]=='FAIL')}")
sys.exit(1 if any(r[0] == "FAIL" for r in RESULTS) else 0)

"""End-to-end negative control for the E11 admission path — Kestrel item 5.

This test is WAITING FOR AN IMPLEMENTATION. As of 2026-08-04 it exits 2 and
reports PRODUCT_AUTHORITY_PATH_ABSENT, because no E11 admission path exists to
exercise (0 occurrences across 10 implementation surfaces; evidence at
fleet_state/arms/ARM-06/D_admission_path/report.json). ARM-08 is implementing
one. The moment it lands, this becomes a real end-to-end authority test with no
edit required.

THE DISCRIMINATING PAIR. An allow-list is only a gate if it separates two cases:

    adjust_counter{boss_stability}      MUST be REFUSED   (the exploit — A3
                                        world-state through an A2 effect)
    adjust_counter{boss_release_delay}  MUST be ADMITTED  (canon — the
                                        strike-interrupt / Tokenweave window)

Refusing BOTH is not a gate. That is what the product does today: both are
rejected identically by the effect vocabulary
(`effect must be one of ['destabilize_anchor']`), which is a vocabulary limit,
not an authority decision. Admitting both is a breach. Only the split counts.

POSITIVE CONTROL, and why it is mandatory here. Twice in this cycle a probe of
mine "passed" while measuring nothing — once refusing for ERR_MALFORMED before
the authority rule was reached, once reporting a false absence from a ripgrep
output-format assumption. So before any refusal is believed, the canonical
skill spec must be ADMITTED. If the harness cannot get a known-good spec
through, it is not wired to the validator and every refusal it reports is
meaningless. That condition exits 2, never 0.

Exit codes:  0 = an admission path exists AND discriminates correctly
             1 = an admission path exists and is WRONG (exploit admitted, or it
                 refuses the canon-legitimate case too, i.e. not a gate)
             2 = cannot evaluate: no admission path, or the harness is not
                 wired to a validator. NEVER reported as a pass.

The Python mirror is located via RX_ORACLE_DIR, defaulting to the path below.
If it is absent this exits 2 rather than assuming anything.
"""
import os
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve()
REPO = HERE.parents[2]
UE_SIM = REPO / "Source" / "ReflexionArena" / "Sim"
MIRROR = pathlib.Path(os.environ.get(
    "RX_ORACLE_DIR", "/home/shax/Projects/core-tech/Reflexion-Arena/tools/oracle"))

E11_TOKENS = ("adjust_counter", "AdjustCounter")

CANON_SPEC = {"name": "FAULTLINE INTERRUPT",
              "trigger": "committed_ground_propagation",
              "effect": "destabilize_anchor",
              "cost": 30, "cooldown": 240, "commit_window": 20}


def absent(reason, detail=""):
    print(f"\n  PRODUCT_AUTHORITY_PATH_ABSENT — {reason}")
    if detail:
        print(f"  {detail}")
    print("\n  exit 2 — nothing to exercise. This is NOT a pass and NOT a breach.")
    sys.exit(2)


def cannot_evaluate(reason):
    print(f"\n  CANNOT EVALUATE — {reason}")
    print("\n  exit 2 — the harness is not wired to a validator, so any refusal it")
    print("  reported would be meaningless.")
    sys.exit(2)


def scan_ue():
    """Static scan of the UE sim for the E11 effect identifier."""
    if not UE_SIM.is_dir():
        return None, f"{UE_SIM} not found"
    hits = []
    for p in sorted(UE_SIM.rglob("*")):
        if p.suffix.lower() not in (".cpp", ".h"):
            continue
        try:
            text = p.read_text(errors="replace")
        except OSError as e:
            return None, f"unreadable: {p} ({e})"
        for i, line in enumerate(text.splitlines(), 1):
            if any(t in line for t in E11_TOKENS):
                hits.append(f"{p.relative_to(REPO)}:{i}")
    return hits, ""


def load_mirror():
    if not MIRROR.is_dir():
        return None, f"mirror dir not found: {MIRROR} (set RX_ORACLE_DIR)"
    sys.path.insert(0, str(MIRROR))
    sys.path.insert(0, str(MIRROR / "semantic_kernel"))
    try:
        import sim_mirror_rules as smr
    except Exception as e:                                  # noqa: BLE001
        return None, f"could not import sim_mirror_rules from {MIRROR}: {e!r}"
    return smr, ""


def main():
    print("  E11 ADMISSION PATH — end-to-end authority control (Kestrel item 5)")
    print(f"  UE sim  : {UE_SIM}")
    print(f"  mirror  : {MIRROR}")
    print()

    ue_hits, ue_err = scan_ue()
    if ue_hits is None:
        print(f"  UE static scan: SKIPPED — {ue_err}")
    else:
        print(f"  UE static scan: {len(ue_hits)} occurrence(s) of {E11_TOKENS}")
        for h in ue_hits[:10]:
            print(f"      {h}")

    smr, err = load_mirror()
    if smr is None:
        cannot_evaluate(err)

    validate = getattr(getattr(smr, "SkillSystem", None), "validate_spec", None)
    if validate is None:
        cannot_evaluate("sim_mirror_rules.SkillSystem.validate_spec not found")

    # ---- POSITIVE CONTROL: a known-good spec must be ADMITTED ----
    good = validate(dict(CANON_SPEC))
    print()
    print(f"  positive control — canonical spec admitted: {bool(good.get('ok'))}")
    if not good.get("ok"):
        cannot_evaluate(
            "the canonical skill spec was REFUSED "
            f"({good.get('detail')!r}). The harness is not reaching the validator "
            "correctly, so no refusal below can be trusted.")

    legal = getattr(smr.SkillSystem, "LEGAL_EFFECTS", None)
    print(f"  effect vocabulary: {legal}")

    # ---- THE DISCRIMINATING PAIR ----
    exploit = validate(dict(CANON_SPEC, effect="adjust_counter",
                            counter_id="boss_stability", delta=-1000))
    canon = validate(dict(CANON_SPEC, effect="adjust_counter",
                          counter_id="boss_release_delay", delta=20))

    print()
    print(f"  {'case':44s} {'admitted':9s} detail")
    print("  " + "-" * 96)
    for label, r in (("adjust_counter{boss_stability}  THE EXPLOIT", exploit),
                     ("adjust_counter{boss_release_delay}  CANON", canon)):
        print(f"  {label:44s} {str(bool(r.get('ok'))):9s} "
              f"{str(r.get('detail', ''))[:46]}")

    e_ok, c_ok = bool(exploit.get("ok")), bool(canon.get("ok"))
    same_detail = str(exploit.get("detail")) == str(canon.get("detail"))

    print()
    # SURFACE CHECK, added 2026-08-05 after this control was caught producing a
    # false negative. ARM-08 implemented E11 in UE C++ (RxCounterAuthority.h/.cpp,
    # AdmitCounterWrite, EAdmission). This control probes the PYTHON MIRROR. Run
    # against a tree carrying that C++ gate it printed 20 static occurrences and
    # then concluded PRODUCT_AUTHORITY_PATH_ABSENT — contradicting its own output
    # in the same breath. "Absent from the surface I probe" is not "absent".
    if ue_hits:
        print(f"  PRODUCT_AUTHORITY_PATH_PRESENT_BUT_UNEXERCISED — {len(ue_hits)} "
              f"occurrence(s) of {E11_TOKENS} exist in the UE sim, but this control")
        print("  exercises the PYTHON MIRROR, where E11 is not implemented. The two")
        print("  surfaces disagree, and the mirror's answer is NOT the product's answer.")
        print("  Exercising the C++ gate needs a built editor and its own harness")
        print("  (ARM-08 ships RxSkillGateCommandlet for exactly this).")
        print("\n  exit 2 — the path exists and was NOT exercised. NOT a pass, NOT an")
        print("  absence finding. Reporting ABSENT here would be a false negative.")
        sys.exit(2)

    if not e_ok and not c_ok and same_detail:
        vocab = legal and "adjust_counter" not in legal
        absent(
            "the exploit and the canon-legitimate case are refused IDENTICALLY",
            "Same rule, same message. That is the effect VOCABULARY, not a counter "
            "allow-list.\n  A gate that also refuses the legitimate case is not a "
            "gate: the exploit is\n  UNREACHABLE, not GATED."
            + (f"\n  'adjust_counter' is not in LEGAL_EFFECTS ({legal})." if vocab else ""))

    if e_ok:
        print("  *** BREACH *** the exploit was ADMITTED. A player-authored E11 can")
        print("  mutate boss_stability — A3 world-state through an A2 effect.")
        return 1
    if not c_ok:
        print("  *** NOT A GATE *** the exploit is refused but the canon-legitimate")
        print(f"  case is refused too, for a different reason "
              f"({canon.get('detail')!r}).")
        print("  boss_release_delay is canon (RX_SKILL_ENUMS_V1.md:252-255); refusing")
        print("  it breaks shipped, intended play.")
        return 1

    print("  GATE CONFIRMED — the exploit is refused and the canon-legitimate case")
    print("  is admitted. The allow-list discriminates.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

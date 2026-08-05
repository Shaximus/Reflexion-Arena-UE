// ARM-06 independent driver for the E11 write-scope gate (ARM-08's implementation).
//
// This links the REAL product translation unit — RxCounterAuthority.cpp — and
// calls AdmitCounterWrite() directly. It is not a re-run of ARM-08's own test:
// the cases, the expected values and the pass rule were derived from
// RX_SKILL_ENUMS_V1.md §4.0/§4.1, not from their test file.
//
// Output is one machine-readable line per assertion:
//     ASSERT <name> <PASS|FAIL> got=<verdict> want=<verdict> input=<id>
// followed by REGISTRY rows and a final SUMMARY line. The Python wrapper judges;
// this binary always exits 0 unless it cannot run at all, so that a FAIL is
// reported as data rather than lost in a non-zero exit.

#include "RxCounterAuthority.h"
#include <cstdio>
#include <cstring>

using RxCounterAuthority::EAdmission;

static const char* Name(EAdmission A)
{
    switch (A)
    {
    case EAdmission::Admit:                return "Admit";
    case EAdmission::RejectUnknownCounter: return "RejectUnknownCounter";
    case EAdmission::RejectAuthorityOwned: return "RejectAuthorityOwned";
    }
    return "UNMAPPED";
}

static int Failures = 0;

static void Check(const char* AssertName, const char* Input, EAdmission Want)
{
    const EAdmission Got = RxCounterAuthority::AdmitCounterWrite(Input);
    const bool Ok = (Got == Want);
    if (!Ok) { ++Failures; }
    std::printf("ASSERT %-28s %s got=%-20s want=%-20s input=%s\n",
                AssertName, Ok ? "PASS" : "FAIL", Name(Got), Name(Want),
                Input ? (Input[0] ? Input : "<empty>") : "<nullptr>");
}

int main()
{
    // ---- the registry as the product actually holds it ----
    const int N = RxCounterAuthority::RegistryNum();
    int Writable = 0;
    for (int i = 0; i < N; ++i)
    {
        const RxCounterAuthority::FCounterEntry& E = RxCounterAuthority::RegistryAt(i);
        if (E.bA2Writable) { ++Writable; }
        std::printf("REGISTRY %d %-24s a2_writable=%-5s cite=%s\n",
                    i, E.CounterId, E.bA2Writable ? "true" : "false", E.SimCite);
    }
    std::printf("REGISTRY_NUM %d\nREGISTRY_WRITABLE %d\n", N, Writable);

    // ---- THE DISCRIMINATING PAIR (§4.0 :261-266) ----
    // Refusing both is a vocabulary limit, not a gate. Admitting both is a
    // breach. Only the split proves an allow-list exists.
    Check("exploit_refused", "boss_stability", EAdmission::RejectAuthorityOwned);
    Check("canon_admitted",  "boss_release_delay", EAdmission::Admit);

    // ---- deny-by-default in the OTHER direction: unknown ids ----
    Check("unknown_refused",  "totally_not_a_counter", EAdmission::RejectUnknownCounter);
    Check("empty_refused",    "", EAdmission::RejectUnknownCounter);
    Check("nullptr_refused",  nullptr, EAdmission::RejectUnknownCounter);

    // ---- the two rejections must stay DISTINGUISHABLE ----
    // An unknown id and an authority-owned id are opposite findings: one is a
    // typo, the other is an attempted tier violation. Collapsing them to one
    // verdict is the DEFECT-3 class (two opposite states sharing one code).
    {
        const EAdmission U = RxCounterAuthority::AdmitCounterWrite("totally_not_a_counter");
        const EAdmission O = RxCounterAuthority::AdmitCounterWrite("boss_stability");
        const bool Ok = (U != O);
        if (!Ok) { ++Failures; }
        std::printf("ASSERT %-28s %s got=%-20s want=%-20s input=%s\n",
                    "rejections_distinguishable", Ok ? "PASS" : "FAIL",
                    Name(U), Name(O), "unknown-vs-authority-owned");
    }

    // ---- string-matching attacks on the allow-list ----
    // The one writable counter is the only thing standing between a player spec
    // and a legitimate write. If the comparison is sloppy, an attacker reaches
    // Admit with an id that is not in the registry at all.
    Check("prefix_not_admitted",     "boss_release_delay_evil", EAdmission::RejectUnknownCounter);
    Check("truncation_not_admitted", "boss_release_del", EAdmission::RejectUnknownCounter);
    Check("suffix_not_admitted",     "xboss_release_delay", EAdmission::RejectUnknownCounter);
    Check("case_not_admitted",       "BOSS_RELEASE_DELAY", EAdmission::RejectUnknownCounter);
    Check("space_not_admitted",      "boss_release_delay ", EAdmission::RejectUnknownCounter);
    // Protected counters must survive the same attacks as REJECTIONS, never Admit.
    Check("protected_prefix_safe",   "boss_stability_x", EAdmission::RejectUnknownCounter);

    // ---- every other registry row is authority-owned ----
    Check("tremor_refused",       "boss_tremor_stage", EAdmission::RejectAuthorityOwned);
    Check("prev_stress_refused",  "boss_prev_anchor_stress", EAdmission::RejectAuthorityOwned);
    Check("state_ticks_refused",  "boss_state_ticks", EAdmission::RejectAuthorityOwned);
    Check("world_tick_refused",   "world_tick", EAdmission::RejectAuthorityOwned);

    // ---- closure: exactly one writable counter, and it is the canon one ----
    {
        const bool Ok = (Writable == 1);
        if (!Ok) { ++Failures; }
        std::printf("ASSERT %-28s %s got=%d want=%d input=%s\n",
                    "exactly_one_writable", Ok ? "PASS" : "FAIL", Writable, 1,
                    "registry closure");
    }

    // ---- rejection reasons must be non-empty and Admit's must be empty ----
    {
        const bool Ok =
            std::strlen(RxCounterAuthority::AdmissionDetail(EAdmission::Admit)) == 0 &&
            std::strlen(RxCounterAuthority::AdmissionDetail(EAdmission::RejectUnknownCounter)) > 0 &&
            std::strlen(RxCounterAuthority::AdmissionDetail(EAdmission::RejectAuthorityOwned)) > 0 &&
            std::strcmp(RxCounterAuthority::AdmissionDetail(EAdmission::RejectUnknownCounter),
                        RxCounterAuthority::AdmissionDetail(EAdmission::RejectAuthorityOwned)) != 0;
        if (!Ok) { ++Failures; }
        std::printf("ASSERT %-28s %s got=%-20s want=%-20s input=%s\n",
                    "details_distinct", Ok ? "PASS" : "FAIL",
                    Ok ? "distinct" : "collapsed", "distinct", "AdmissionDetail");
    }

    std::printf("SUMMARY failures=%d\n", Failures);
    return 0;
}

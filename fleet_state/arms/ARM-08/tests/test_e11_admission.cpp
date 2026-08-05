// ARM-08 item 4 — end-to-end test of the E11 admission gate.
//
// This compiles and links the REAL shipped unit
// (Source/ReflexionArena/Sim/RxCounterAuthority.{h,cpp}) on the host toolchain.
// It is NOT a mirror or a re-implementation: if the shipped allow-list changes,
// this test changes verdict.
//
// It exercises the exact seven counter ids of ARM-06's verification
// (Design/tests/test_e11_authority_gate.py CASES) plus the two admission
// outcomes the spec names by hand at RX_SKILL_ENUMS_V1.md:263-265.
//
// Build:  g++ -std=c++17 -Wall -Wextra -o /tmp/t
//             fleet_state/arms/ARM-08/tests/test_e11_admission.cpp
//             Source/ReflexionArena/Sim/RxCounterAuthority.cpp
// Exit:   0 = every case as expected, 1 = at least one case wrong.

#include "../../../../Source/ReflexionArena/Sim/RxCounterAuthority.h"

#include <cstdio>

using RxCounterAuthority::EAdmission;

namespace
{
const char* Name(EAdmission A)
{
	switch (A)
	{
	case EAdmission::Admit:                 return "ADMIT";
	case EAdmission::RejectUnknownCounter:  return "REJECT_UNKNOWN_COUNTER";
	case EAdmission::RejectAuthorityOwned:  return "REJECT_AUTHORITY_OWNED";
	}
	return "?";
}

struct FCase
{
	const char* CounterId;
	bool        bExpectAdmit;
	const char* Why;
};

// ARM-06's seven cases, verbatim in id and expectation.
const FCase Cases[] = {
	{ "boss_stability",          false, "THE EXPLOIT - A3 world-state via an A2 effect" },
	{ "boss_tremor_stage",       false, "authority-owned" },
	{ "boss_prev_anchor_stress", false, "authority-owned" },
	{ "boss_state_ticks",        false, "authority-owned" },
	{ "world_tick",              false, "sim clock - would let a skill move time" },
	{ "boss_release_delay",      true,  "CANON - strike-interrupt / Tokenweave window" },
	{ "not_a_real_counter",      false, "unknown id must default closed" },
};
} // namespace

int main()
{
	int Fails = 0;

	std::printf("  counter                   expect  actual                  verdict\n");
	std::printf("  --------------------------------------------------------------------------\n");
	for (const FCase& C : Cases)
	{
		const EAdmission Got = RxCounterAuthority::AdmitCounterWrite(C.CounterId);
		const bool bAdmitted = (Got == EAdmission::Admit);
		const bool bOk = (bAdmitted == C.bExpectAdmit);
		Fails += bOk ? 0 : 1;
		std::printf("  %-24s %-6s  %-22s  %s   %s\n", C.CounterId,
			C.bExpectAdmit ? "true" : "false", Name(Got),
			bOk ? "PASS" : "*** FAIL ***", C.Why);
	}

	// --- the two outcomes RX_SKILL_ENUMS_V1.md:263-265 names by hand ---
	std::printf("\n  spec-named negative control (RX_SKILL_ENUMS_V1.md:263-265):\n");
	{
		const EAdmission Exploit = RxCounterAuthority::AdmitCounterWrite("boss_stability");
		const bool bOk = (Exploit == EAdmission::RejectAuthorityOwned);
		Fails += bOk ? 0 : 1;
		std::printf("    adjust_counter{boss_stability, -300}  -> %-22s  %s\n",
			Name(Exploit), bOk ? "REFUSED (gate observed refusing)" : "*** ADMITTED - HOLE OPEN ***");

		const EAdmission Canon = RxCounterAuthority::AdmitCounterWrite("boss_release_delay");
		const bool bCanonOk = (Canon == EAdmission::Admit);
		Fails += bCanonOk ? 0 : 1;
		std::printf("    adjust_counter{boss_release_delay,+20} -> %-22s  %s\n",
			Name(Canon), bCanonOk ? "ADMITTED (shipped play preserved)" : "*** REFUSED - CANON BROKEN ***");
	}

	// --- read is not write: every registry counter must remain C10-readable ---
	std::printf("\n  read-is-not-write (C10 must still read what E11 cannot write):\n");
	int ReadableNotWritable = 0;
	for (int i = 0; i < RxCounterAuthority::RegistryNum(); ++i)
	{
		const RxCounterAuthority::FCounterEntry& E = RxCounterAuthority::RegistryAt(i);
		const bool bReadable = RxCounterAuthority::IsKnownCounter(E.CounterId);
		if (!bReadable)
		{
			std::printf("    *** FAIL *** %s is in the registry but not C10-readable\n", E.CounterId);
			++Fails;
		}
		ReadableNotWritable += (bReadable && !E.bA2Writable) ? 1 : 0;
	}
	std::printf("    %d of %d registry counters are readable-but-not-writable\n",
		ReadableNotWritable, RxCounterAuthority::RegistryNum());
	if (ReadableNotWritable < 1)
	{
		std::printf("    *** FAIL *** the allow-list grants write to everything - no scope at all\n");
		++Fails;
	}

	std::printf("\n  %s\n", Fails == 0 ? "ALL ADMISSION CONDITIONS MET" : "FAILURE(S) PRESENT");
	return Fails == 0 ? 0 : 1;
}

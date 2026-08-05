#include "RxCounterAuthority.h"

namespace RxCounterAuthority
{
namespace
{
/**
 * The C10 counter registry — code-side counterpart of RX_SKILL_ENUMS_V1.md §4.1.
 *
 * "Authority-owned is the default; writability is the exception and must be
 *  named." Exactly ONE counter is A2-writable.
 *
 * Do not add a row here without the matching §4.1 row: the doc/code drift check
 * in fleet_state/arms/ARM-08/tests/check_registry_matches_doc.py compares this
 * table against the markdown and fails on any disagreement.
 */
const FCounterEntry Registry[] = {
	{ "boss_stability",          false, "FRxBossEarthquake::Stability        RxBossEarthquake.h:75" },
	{ "boss_tremor_stage",       false, "FRxBossEarthquake::TremorStage      RxBossEarthquake.h:81" },
	{ "boss_release_delay",      true,  "FRxBossEarthquake::ReleaseDelay     RxBossEarthquake.h:78" },
	{ "boss_prev_anchor_stress", false, "FRxBossEarthquake::PrevAnchorStress RxBossEarthquake.h:79" },
	{ "boss_state_ticks",        false, "FRxBossEarthquake::StateTicks       RxBossEarthquake.h:73" },
	{ "world_tick",              false, "FRxSimWorld::Tick                   RxSimWorld.h:54" },
};

constexpr int RegistryCount = int(sizeof(Registry) / sizeof(Registry[0]));

/** Local ASCII compare — keeps this unit free of any header dependency. */
bool SameId(const char* A, const char* B)
{
	if (A == nullptr || B == nullptr)
	{
		return false;
	}
	while (*A != '\0' && *A == *B)
	{
		++A;
		++B;
	}
	return *A == *B;
}

/** Registry row for an id, or nullptr when the id is not in the registry. */
const FCounterEntry* Find(const char* CounterId)
{
	for (int i = 0; i < RegistryCount; ++i)
	{
		if (SameId(Registry[i].CounterId, CounterId))
		{
			return &Registry[i];
		}
	}
	return nullptr;
}
} // namespace

int RegistryNum()
{
	return RegistryCount;
}

const FCounterEntry& RegistryAt(int Index)
{
	// Callers are contract-bound to pass a valid index; clamp rather than UB so a
	// mistake degrades to a deterministic read instead of a crash in the sim.
	const int Safe = (Index < 0) ? 0 : (Index >= RegistryCount ? RegistryCount - 1 : Index);
	return Registry[Safe];
}

bool IsKnownCounter(const char* CounterId)
{
	return Find(CounterId) != nullptr;
}

EAdmission AdmitCounterWrite(const char* CounterId)
{
	const FCounterEntry* Entry = Find(CounterId);
	if (Entry == nullptr)
	{
		return EAdmission::RejectUnknownCounter;   // deny-by-default: unknown id
	}
	if (!Entry->bA2Writable)
	{
		return EAdmission::RejectAuthorityOwned;   // readable via C10, not writable via E11
	}
	return EAdmission::Admit;
}

const char* AdmissionDetail(EAdmission Admission)
{
	switch (Admission)
	{
	case EAdmission::Admit:
		return "";
	case EAdmission::RejectUnknownCounter:
		return "E11 adjust_counter: counter_id is not in the C10 registry";
	case EAdmission::RejectAuthorityOwned:
		return "E11 adjust_counter: counter_id is authority-owned and not A2-writable";
	}
	return "";
}

} // namespace RxCounterAuthority

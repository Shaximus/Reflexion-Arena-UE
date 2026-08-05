#pragma once

/**
 * RxCounterAuthority.h — the E11 write-scope gate.
 *
 * Design/RX_SKILL_ENUMS_V1.md §4.0 "AUTHORITY REPAIR" states the defect: the C10
 * counter registry was defined as a READ mapping with no write column, and E11
 * `adjust_counter` (A2, player-authorable, allow-listed) took `counter_id:
 * <registry>` unqualified — so a player-authored skill could name any counter in
 * the registry and mutate it, including `boss_stability`. That is an A3
 * world-mutation reachable through an A2 effect.
 *
 * §4.0 then states the requirement this unit exists to meet (:261-266):
 *
 *   "A registry table is documentation. The validator that admits player-authored
 *    skills MUST reject an E11 whose `counter_id` is not A2-writable ... A gate
 *    that has never been observed refusing is not a gate."
 *
 * The registry below is the code-side counterpart of the §4.1 table. It is
 * DENY-BY-DEFAULT in two directions: an unknown `counter_id` is rejected, and a
 * known counter is authority-owned unless bA2Writable is explicitly true.
 *
 * READ IS NOT WRITE. Every entry here stays fully readable through C10
 * `counter_threshold` — the boss must stay legible to the player, which is the
 * whole design. This unit governs the WRITE side only.
 *
 * Scope discipline (§4.0): this is one registry and one allow-list. It is
 * deliberately NOT a permission framework, capability tokens, or per-effect ACLs.
 *
 * Convention note: this translation unit is deliberately UE-FREE (no
 * CoreMinimal.h, no FString) so the authority decision is a plain C++ predicate
 * that can be linked and exercised by a host-toolchain test without an engine
 * build. FRxSkillSystem::ValidateSpec wraps it for the FString-facing sim code.
 * Integer/enum only, no allocation, deterministic — same constraints as the sim.
 */

namespace RxCounterAuthority
{

/** Outcome of an E11 write-scope decision. Deny-by-default: only Admit passes. */
enum class EAdmission
{
	Admit,                  // counter is in the registry AND explicitly A2-writable
	RejectUnknownCounter,   // counter_id is not in the registry at all
	RejectAuthorityOwned,   // counter exists, is readable via C10, but is NOT A2-writable
};

/** One row of the §4.1 counter registry. */
struct FCounterEntry
{
	const char* CounterId;    // the `counter_id: <registry>` token
	bool        bA2Writable;  // §4.1 "A2-writable via E11" column
	const char* SimCite;      // where the sim actually holds this counter
};

/** Registry size, for iteration by callers and tests. */
int RegistryNum();

/** Registry row by index. Index must be in [0, RegistryNum()). */
const FCounterEntry& RegistryAt(int Index);

/** True when `CounterId` names a counter C10 `counter_threshold` can READ. */
bool IsKnownCounter(const char* CounterId);

/**
 * The gate. Returns Admit ONLY for a registry counter explicitly marked
 * A2-writable; every other input — including an unknown id — is a rejection.
 */
EAdmission AdmitCounterWrite(const char* CounterId);

/** Stable, non-allocating reason text for a rejection (empty for Admit). */
const char* AdmissionDetail(EAdmission Admission);

} // namespace RxCounterAuthority

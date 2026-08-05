#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "RxCanonJson.h" // FRxJsonValue — hashed outputs build the canonical value set

/**
 * FRxSkillSystem — UE5.8 port of the Godot reference
 * (Reflexion-Arena/game/sim/skill_system.gd, class_name SkillSystem).
 *
 * Deterministic fragment socket + ONE bounded skill (FAULTLINE_INTERRUPT).
 * Ported 1:1 to preserve behavior, event ordering and replay/hash parity across
 * the engine switch (CONTRACTS.md §0 determinism law; §2 SkillSystem; §9 transfer
 * receipt; SHENRON §6 Compression Fragment, §7 one bounded skill, §8 transfer,
 * §9 receipt from actual simulation events).
 *
 * Canon invariants preserved:
 *   - socket_fragment sockets exactly ONE compiled fragment (NOT a skill factory).
 *   - author_skill only ever produces the fixed FAULTLINE_INTERRUPT artifact after
 *     validating a spec against the fixed legal choice-lists ("no free-form").
 *   - apply_use pays cost/cooldown either way, then resolves correct-vs-wrong
 *     surface deterministically (wrong surface = +200 residual-risk stress).
 *   - the class NEVER mutates the world outside the command pipeline; execute()
 *     routes a validated "use_skill" command through World.Submit.
 *
 * Conventions: plain C++ class (NOT UObject), CoreMinimal.h, integer math only,
 * NO floats. GDScript Dictionaries map to FJsonObject for event/snapshot payloads
 * (parity via the shared FRxCanonJson canonical serializer). Input specs that
 * come from JSON (fragment / skill spec / target / params) are modeled as structs.
 */

// Shared sim types (world-owned; see integration report for assumed signatures).
class FRxSimWorld;
struct FRxCommandEnvelope;

// ---------------------------------------------------------------------------
// JSON-backed data specs (mirror the on-disk / command JSON fields)
// ---------------------------------------------------------------------------

/** Provenance block of fragment_earthquake.json ("compiler" object). */
struct FRxFragmentCompiler
{
	TArray<FString> ChecksRun;               // "checks_run"
	FString Notes;                           // "notes"
	FString RefCommit;                       // "ref_commit"
	FString Repo;                            // "repo"
	FString Tool;                            // "tool"
	TMap<FString, FString> VendoredSha;      // "vendored_sha" (path -> sha)
	FString Version;                         // "version"
};

/**
 * Compiled Compression Fragment — mirrors fragment_earthquake.json verbatim so
 * that snapshot() reproduces the socketed fragment for hash parity (the Godot
 * code stores fragment.duplicate(true) — the whole compiled artifact).
 */
struct FRxFragmentSpec
{
	bool bValid = false;                     // mirrors Godot Dictionary is_empty()
	FRxFragmentCompiler Compiler;            // "compiler"
	FString Counterplay;                     // "counterplay"
	FString FragmentHash;                    // "fragment_hash"
	FString Propagation;                     // "propagation"
	FString ResidualRisk;                    // "residual_risk"
	TArray<FString> TransferDomains;         // "transfer_domains"
	FString Trigger;                         // "trigger" (fragment invariant trigger)

	/** Canonical value form (empty object when !bValid, matching duplicate({})). */
	FRxJsonValue ToJson() const;
};

/**
 * One entry of a skill's effect list (RX_SKILL_ENUMS_V1.md §5 effect enum).
 *
 * The pre-existing FRxSkillSpec::Effect is a single fixed-choice STRING and
 * carries no parameters, so there was nothing for an admission gate to inspect:
 * an E11 `adjust_counter` names a counter and a delta, and neither could be
 * expressed. This is the minimum shape that makes an effect checkable — an id
 * plus the E11 parameters — and nothing more (§4.0 "scope discipline": no
 * general permission framework, no capability tokens, no per-effect ACLs).
 *
 * An empty Effects list is the legacy shape: FAULTLINE_INTERRUPT carries none,
 * so every already-shipped spec admits exactly as before and the parity/hash
 * surface is untouched.
 */
struct FRxSkillEffect
{
	FString EffectId;                        // "E11" (RX_SKILL_ENUMS_V1.md §5 id)
	FString CounterId;                       // E11 param `counter_id: <registry>`
	int32 Delta = 0;                         // E11 param `delta: int(signed)`
};

/** RX_SKILL_ENUMS_V1.md §5 id of adjust_counter — the one A2 effect that writes. */
namespace RxEffectId
{
	inline constexpr const TCHAR* AdjustCounter = TEXT("E11");
}

/**
 * Skill authoring request — the fixed-choice spec validated by ValidateSpec.
 * Numeric fields default to -1 to mirror spec.get("cost", -1) in Godot, so an
 * omitted field is rejected rather than silently accepted.
 */
struct FRxSkillSpec
{
	FString Name;                            // must be "FAULTLINE INTERRUPT"
	FString Trigger;                         // must be "committed_ground_propagation"
	FString Effect;                          // must be "destabilize_anchor"
	int32 Cost = -1;                         // must be SkillCost (30)
	int32 Cooldown = -1;                     // must be SkillCooldown (240)
	int32 CommitWindow = -1;                 // must be SkillCommitWindow (20)

	/**
	 * Parameterised effect list. Checked by ValidateSpec AFTER every pre-existing
	 * check, so no already-valid or already-rejected spec changes its outcome or
	 * its detail string. Empty on every spec the sim ships today.
	 */
	TArray<FRxSkillEffect> Effects;
};

/** The one bounded skill artifact produced by AuthorSkill. */
struct FRxSkillArtifact
{
	bool bValid = false;                     // mirrors authored_skill.is_empty()
	FString SkillId;                         // "faultline_interrupt"
	FString Name;
	FString DerivedFrom;                     // "earthquake"
	FString Trigger;
	FString Effect;
	int32 Cost = 0;
	int32 Cooldown = 0;
	int32 CommitWindow = 0;
	FString ResidualRisk;
	FString Authority;                       // "validated_request_only"
	FString FragmentHash;
	FString SkillHash;                       // CanonJson hash of the fields above

	/**
	 * Canonical dict form. bIncludeHash=false yields the exact object hashed to
	 * derive SkillHash (Godot hashes the artifact BEFORE adding skill_hash);
	 * bIncludeHash=true is the stored/snapshot form. Empty object when !bValid.
	 */
	FRxJsonValue ToJson(bool bIncludeHash) const;
};

/** use_skill target — mirrors _target_region(target): region_id then region. */
struct FRxSkillTarget
{
	bool bHasRegionId = false;
	int32 RegionId = 0;
	bool bHasRegion = false;
	int32 Region = 0;

	/** region_id if present, else region if present, else -1. */
	int32 ResolveRegion() const;

	/** Canonical target value (as carried in a use_skill command's params). */
	FRxJsonValue ToJson() const;
};

/** Parsed "use_skill" command params (params.get("skill_id"/"target")). */
struct FRxUseSkillParams
{
	FString SkillId;
	FRxSkillTarget Target;
};

/** Uniform {ok, code, detail} result mirroring the Godot return Dictionaries. */
struct FRxSkillResult
{
	bool bOk = false;
	FString Code;                            // "OK" | "ERR_STATE" | ...
	FString Detail;

	static FRxSkillResult Ok(const FString& InDetail = FString())
	{
		return FRxSkillResult{ true, TEXT("OK"), InDetail };
	}
	static FRxSkillResult Err(const FString& InCode, const FString& InDetail)
	{
		return FRxSkillResult{ false, InCode, InDetail };
	}
};

// ---------------------------------------------------------------------------
// FRxSkillSystem
// ---------------------------------------------------------------------------

class FRxSkillSystem
{
public:
	// --- fixed skill constants (mirror SimConfig; kept local so this unit is
	//     self-contained — reconcile with FRxSimConfig at integration). ---
	static constexpr int32 FocusMax         = 100;   // FOCUS_MAX
	static constexpr int32 FocusRegen       = 1;     // FOCUS_REGEN (focus/tick)
	static constexpr int32 SkillCost        = 30;    // SKILL_COST (focus)
	static constexpr int32 SkillCooldown    = 240;   // SKILL_COOLDOWN (ticks)
	static constexpr int32 SkillCommitWindow = 20;   // SKILL_COMMIT_WINDOW
	static constexpr int32 SkillDampen      = 600;   // SKILL_DAMPEN (destabilize magnitude)
	static constexpr int32 SkillWrongStress = 200;   // SKILL_WRONG_STRESS (residual risk)
	static constexpr int32 DampCancel       = 400;   // DAMP_CANCEL (window lower bound)
	static constexpr int32 StressThreshold  = 1000;  // STRESS_THRESHOLD (window upper bound)

	/** SimConfig.SKILL_ID. */
	static const FString& SkillIdName();
	/** RESIDUAL_RISK canon text. */
	static const FString& ResidualRiskText();
	/** Fixed legal choice-lists (CONTRACTS.md §2 "no free-form"). */
	static const TArray<FString>& LegalNames();
	static const TArray<FString>& LegalTriggers();
	static const TArray<FString>& LegalEffects();

	// --- state (mirrors the Godot member vars) ---
	FRxFragmentSpec Fragment;                // socketed fragment
	FRxSkillArtifact AuthoredSkill;          // the one bounded skill artifact
	TMap<FString, int32> Cooldowns;          // skill_id -> world tick when ready
	int32 Focus = FocusMax;
	FString LastAction;                      // §9 action_selected evidence
	FString LastAuthority;                   // §9 authority evidence

	// --- fixed-choice validation (Commands.validate + AuthorSkill) ---
	static FRxSkillResult ValidateSpec(const FRxSkillSpec& Spec);

	/** Socket exactly one compiled fragment; emits fragment_socketed. */
	void SocketFragment(FRxSimWorld& World, const FRxFragmentSpec& Frag);

	/**
	 * Validate spec against the fixed choice-lists; on success build the
	 * deterministic FAULTLINE_INTERRUPT artifact and emit skill_authored.
	 * The artifact is stored (GetAuthoredSkill) — the result carries ok/code/detail.
	 */
	FRxSkillResult AuthorSkill(FRxSimWorld& World, const FRxSkillSpec& Spec);

	/**
	 * Resource/validity gate. Surface correctness is deliberately NOT checked
	 * here: wrong-surface execution is legal and triggers the residual-risk
	 * backfire in ApplyUse.
	 */
	FRxSkillResult CanUse(FRxSimWorld& World, int32 ActorId,
		const FString& SkillId, const FRxSkillTarget& Target) const;

	/**
	 * Routes a validated "use_skill" command through World.Submit (§7 "validated
	 * request only" — NEVER mutates the world directly). Returns the submit result.
	 */
	FRxSkillResult Execute(FRxSimWorld& World, int32 ActorId,
		const FString& SkillId, const FRxSkillTarget& Target);

	/**
	 * Called by the world after validation. Pays cost/cooldown, then resolves
	 * correct vs wrong surface deterministically. Envelope supplies authority.
	 */
	FRxSkillResult ApplyUse(FRxSimWorld& World, int32 ActorId,
		const FRxUseSkillParams& Params, const FRxCommandEnvelope& Envelope);

	/** Focus regen + transfer-receipt emission (on success OR failed attempt). */
	void Tick(FRxSimWorld& World);

	/** SHENRON §9 receipt, assembled from actual sim state (never static text). */
	TSharedRef<FJsonObject> BuildTransferReceipt(FRxSimWorld& World) const;

	/** Canonical-safe snapshot for save/replay/hash (FRxJsonValue — feeds state_hash). */
	FRxJsonValue Snapshot() const;

	const FRxSkillArtifact& GetAuthoredSkill() const { return AuthoredSkill; }
	const FRxFragmentSpec& GetFragment() const { return Fragment; }

private:
	static int32 TargetRegion(const FRxSkillTarget& Target);
};

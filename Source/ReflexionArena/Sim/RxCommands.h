#pragma once

#include "CoreMinimal.h"
#include "RxTypes.h"     // FRxEntityId, RxSim::* constants, RxCmd/RxActor/RxCode strings
#include "RxCanonJson.h" // FRxJsonValue (closed canonical value set) — command params

/**
 * RxCommands.h — UE5.8 port of the Godot reference
 * (Reflexion-Arena/game/sim/commands.gd, class_name Commands), CONTRACTS.md §2.
 *
 * This is the SECURITY-CRITICAL authority / validation gate. It is ported 1:1 to
 * reproduce EVERY rejection path, every result code, and every detail string
 * verbatim, in the SAME ORDER as the reference, so replay/hash parity and the
 * adversarial acceptance suite (malformed / authority / unknown-type / state)
 * hold across the engine switch.
 *
 * Pipeline mirrored from commands.gd validate():
 *   1. structural            -> ERR_MALFORMED
 *   2. vocabulary            -> ERR_UNKNOWN_TYPE
 *   3. per-type param shape  -> ERR_MALFORMED
 *   4. authority tier / gate -> ERR_UNKNOWN_TYPE | ERR_AUTHORITY
 *   5. world-state checks    -> ERR_STATE  (only when a world is supplied)
 *
 * Authority tiers (CONTRACTS.md §2):
 *   T0 observe/emote (auto): reference, instruct, approve, cancel, correct, wait
 *   T1 move (auto):          move_to
 *   T2 weave/skill/strike:   strike, tokenweave_begin, use_skill, socket_fragment,
 *                            author_skill  — REQUIRE approved:true when
 *                            actor=="companion" (player commands are pre-approved
 *                            by the UI; companion T2 without approval is rejected)
 *   T3 world-mutation: sim-internal only, NEVER commandable (no type maps here)
 *
 * Conventions: plain C++ (NOT a UObject/AActor), CoreMinimal.h, integer-only,
 * deterministic. Command params are heterogeneous and modeled as the closed
 * FRxJsonValue (mirrors the GDScript params Dictionary), NOT UE FJsonObject.
 */

// The world is only READ by validation (entities, flags, terrain, companion,
// skills). Forward-declared; RxCommands.cpp includes RxSimWorld.h for the full
// definition (breaks the world<->commands include cycle). Passed as a POINTER so
// nullptr faithfully mirrors the reference `if world != null:` state-check guard;
// non-const because the reference `self` is untyped and FRxSkillSystem::CanUse
// takes a non-const FRxSimWorld& (validation still performs READS ONLY).
class FRxSimWorld;

/**
 * RxCmdDetail — the FIXED (non-interpolated) rejection detail strings, verbatim
 * from commands.gd. Exposed so BOTH this validator and the world-owned
 * JSON->envelope parser emit byte-identical text (see note on FRxCommandEnvelope
 * about the type-level malformed checks the typed envelope hoists to the parser).
 */
namespace RxCmdDetail
{
	// --- structural (ERR_MALFORMED) ---
	inline constexpr const TCHAR* EnvelopeNotDict   = TEXT("envelope is not a Dictionary");
	inline constexpr const TCHAR* ActorInvalid      = TEXT("actor missing or not in {player,companion,system}");
	inline constexpr const TCHAR* TypeInvalid       = TEXT("type missing or not a String");
	inline constexpr const TCHAR* SeqNotInt         = TEXT("seq must be an int when present");
	inline constexpr const TCHAR* TickNotNonNegInt  = TEXT("tick must be a non-negative int when present");
	inline constexpr const TCHAR* ApprovedNotBool   = TEXT("approved must be a bool when present");
	inline constexpr const TCHAR* ParamsNotDict     = TEXT("params must be a Dictionary");

	// --- per-type param shape (ERR_MALFORMED) ---
	inline constexpr const TCHAR* MoveToParams      = TEXT("move_to requires int params x,y");
	inline constexpr const TCHAR* StrikeParams      = TEXT("strike requires int region/region_id or target_id/target_entity_id");
	inline constexpr const TCHAR* ReferenceTarget   = TEXT("reference requires Dictionary params.target");
	inline constexpr const TCHAR* UseSkillSkillId   = TEXT("use_skill requires String params.skill_id");
	inline constexpr const TCHAR* UseSkillTarget    = TEXT("use_skill requires Dictionary params.target");
	inline constexpr const TCHAR* TokenweaveRegion  = TEXT("tokenweave_begin region_id must be int");
	inline constexpr const TCHAR* TokenweaveMode    = TEXT("tokenweave_begin mode must be anchor|fabricate");
	inline constexpr const TCHAR* SocketFragment    = TEXT("socket_fragment requires Dictionary params.fragment");
	inline constexpr const TCHAR* AuthorSkillSpec   = TEXT("author_skill requires Dictionary params.spec");

	// --- world-state (ERR_STATE) ---
	inline constexpr const TCHAR* NoCompanionInstruct      = TEXT("no companion to instruct");
	inline constexpr const TCHAR* NoCompanionApproveCancel = TEXT("no companion to approve/cancel");
	inline constexpr const TCHAR* EntityAlreadyWeaving     = TEXT("entity already weaving");
	inline constexpr const TCHAR* NoFragmentBeforeBoss     = TEXT("no fragment before boss defeat");
	inline constexpr const TCHAR* AuthoringRequiresFragment = TEXT("authoring requires a socketed fragment");
	inline constexpr const TCHAR* AttackerMissing          = TEXT("attacker missing");
	inline constexpr const TCHAR* TargetOutOfRange         = TEXT("target entity out of strike range");
	inline constexpr const TCHAR* RegionNotConnected       = TEXT("target region not connected to striker");
	inline constexpr const TCHAR* StrikeOnCooldown         = TEXT("strike on cooldown");
	inline constexpr const TCHAR* SkillNotUsable           = TEXT("skill not usable");   // can_use detail fallback
	inline constexpr const TCHAR* IllegalSpec              = TEXT("illegal spec");        // validate_spec detail fallback
}

/**
 * FRxCmdResult — {ok, code, detail} mirroring the Godot _result() Dictionary.
 * Code carries the verbatim RxCode string (OK / ERR_MALFORMED / ERR_AUTHORITY /
 * ERR_UNKNOWN_TYPE / ERR_STATE).
 *
 * NOTE (integration): the command envelope + result are conceptually world-owned.
 * They are DEFINED here — the validation gate — so this module is self-contained
 * and compiles against only the existing shared headers (RxTypes.h/RxCanonJson.h).
 * RxSimWorld.h should INCLUDE this header and reuse these types rather than
 * redefining them. See the final integration report.
 */
struct FRxCmdResult
{
	bool bOk = false;
	FString Code;
	FString Detail;

	static FRxCmdResult Ok()
	{
		return FRxCmdResult{ true, FString(RxCode::Ok), FString() };
	}
	static FRxCmdResult Err(const FString& InCode, const FString& InDetail)
	{
		return FRxCmdResult{ false, InCode, InDetail };
	}
};

/**
 * FRxCommandEnvelope — typed mirror of the canonical-JSON command envelope
 * Dictionary from commands.gd:
 *   {"seq":1,"tick":0,"actor":"player","type":"move_to","params":{...},"approved":true}
 *
 * The well-known scalar fields are promoted to typed members; `params` stays
 * heterogeneous as an FRxJsonValue (an Object). Presence flags reproduce the
 * reference `cmd.has(key)` distinction (seq/tick/approved are OPTIONAL on
 * runtime-generated envelopes — SimWorld.submit fills seq/tick when absent).
 *
 * TYPED-VS-DYNAMIC NOTE: the reference detects TYPE-level malformations
 * (`seq` not int, `approved` not bool, `actor` not a String) on an untyped
 * Dictionary. Those cannot occur once the value lives in a typed field, so the
 * world-owned JSON->envelope parser (SimWorld.submit) is responsible for
 * rejecting genuinely non-int seq / non-bool approved / non-String actor with the
 * exact RxCmdDetail strings above. Every VALUE-level check (actor not in the
 * set, empty type, tick < 0, params not an Object, per-type shapes, authority,
 * world state) is reproduced faithfully inside Validate().
 */
struct FRxCommandEnvelope
{
	int32   Seq = 0;
	bool    bHasSeq = false;

	int32   Tick = 0;
	bool    bHasTick = false;

	FString Actor;                              // "player"|"companion"|"system"
	FString Type;                               // RxCmd::* command type

	// Heterogeneous params; defaults to an empty Object to mirror the reference
	// `var params = cmd.get("params", {})` (absent params == empty Dictionary).
	FRxJsonValue Params = FRxJsonValue::Object();

	bool    bApproved = false;
	bool    bHasApproved = false;

	FString Authority;                          // provenance tag (e.g. "player-approved")
};

/**
 * FRxCommands — static envelope validator + authority gate. Mirrors
 * `class_name Commands` method-for-method (validate / tier_of + the private
 * _validate_state / _validate_strike helpers).
 */
class FRxCommands
{
public:
	// --- closed vocabularies (mirror the Godot const Arrays) ---
	static const TArray<FString>& Actors();     // ACTORS
	static const TArray<FString>& Types();      // TYPES
	static const TArray<FString>& T0Types();    // T0_TYPES
	static const TArray<FString>& T1Types();    // T1_TYPES
	static const TArray<FString>& T2Types();    // T2_TYPES

	/** Authority tier of a command type (0..2; -1 = not commandable / unknown). */
	static int32 TierOf(const FString& Type);

	/**
	 * Validate a command envelope against structure, vocabulary, per-type param
	 * shape, authority, and (when World != nullptr) world state — in the reference
	 * order. Returns the verbatim {ok, code, detail}. Performs READS ONLY.
	 */
	static FRxCmdResult Validate(const FRxCommandEnvelope& Cmd, FRxSimWorld* World);

private:
	static FRxCmdResult ValidateState(const FRxCommandEnvelope& Cmd, FRxSimWorld* World,
		const FString& Actor, const FString& Type, const FRxJsonValue& Params);

	static FRxCmdResult ValidateStrike(FRxSimWorld* World, int32 AttackerId,
		const FRxJsonValue& Params);
};

#pragma once

#include "CoreMinimal.h"
#include "RxTypes.h"      // FRxEntity, FRxEntityId, RxSim::* constants, RxKind/RxState/RxCmd/RxActor
#include "RxCanonJson.h"  // FRxJsonValue — the closed canonical value set for structured/hashed payloads

/**
 * FRxCompanionAI — UE5.8 port of the Godot ground truth
 * (Reflexion-Arena/game/sim/companion_ai.gd, class_name CompanionAI), ported 1:1
 * to preserve behavior, event ordering and the EXACT dialogue strings for
 * replay/hash parity across the engine switch (CONTRACTS.md §0 determinism law,
 * §2 "Intent + CompanionAI" surface + authority tiers, §3 flow; SHENRON §5/§8/§11).
 *
 * OBSERVATION-ONLY BOUNDARY (SHENRON §11, CONTRACTS.md §2) — preserved verbatim:
 *   This class NEVER mutates authoritative world state. Every consequential action
 *   enters the sim exclusively through World.Submit() (validated command envelopes,
 *   modelled here as canonical FRxJsonValue objects). The two-and-only world writes
 *   it performs are the session-blackboard flags the contract assigns to
 *   companion-observed facts — flags.correction_made and flags.transfer_recognized
 *   (routed through World.SetFlag). Weave COMPLETION is a sim-internal T3 effect
 *   owned by SimWorld upkeep; the companion only tracks progress to emit lines.
 *
 * AUTHORITY / APPROVAL FLOW (CONTRACTS.md §2, SHENRON §11):
 *   T0 observe/emote + T1 move  -> may submit autonomously ("auto").
 *   T2 tokenweave/strike/skill  -> submitted ONLY from OnApproved(), carrying
 *     approved=true + authority "player-approved". ReceiveInstruction() returns
 *     {bOk, Echo, PlanSteps, bNeedsApproval}; plans containing any T2 step are
 *     parked in PendingPlan until OnApproved()/OnCancelled(); T0/T1-only plans
 *     auto-submit immediately. "seq" is intentionally absent — SimWorld.Submit
 *     assigns it at queue time; "tick"/"actor" are filled here.
 *
 * Conventions (CONTRACTS.md §0): plain C++ class (NOT a UObject/AActor),
 * CoreMinimal.h, integer math ONLY, no floats, no engine RNG, deterministic
 * (ordered) iteration. Positions are FIntPoint milli-units (mirrors Vector2i).
 * Distance math is int64 to match GDScript's 64-bit int (dx*dx must not overflow).
 * The companion talks to the sim through FRxSimWorld& (forward-declared; owner
 * reconciles the interface — see the assumed-signature list in the integration
 * report) and parses instructions through the sibling FRxIntent module.
 */

// World-owned (RxSimWorld.h, not written yet — owner A). Take by reference where
// the .gd took `self`. The assumed call surface is documented in the .cpp header.
class FRxSimWorld;
// Terrain region record (RxTerrain.h) — used only by pointer in private helpers.
struct FRxRegion;

/**
 * Result of ReceiveInstruction — mirrors the Godot return Dictionary
 *   {"ok","echo","plan":{"steps":[...]},"needs_approval"}.
 * PlanSteps is the plan's "steps" array (plan == {"steps": PlanSteps}); each step
 * is a canonical command-envelope object (see MakeCmd). For an auto-submitted
 * (T0/T1-only) plan the steps are returned already stamped with tick/actor/
 * authority; for an approval-gated plan they carry only tick/actor/type/params.
 */
struct FRxInstructionResult
{
	bool bOk = false;
	FString Echo;
	TArray<FRxJsonValue> PlanSteps;   // == plan["steps"]
	bool bNeedsApproval = false;
};

/**
 * Parked plan awaiting player approval — mirrors the Godot pending_plan Dictionary
 *   {"intent","target","steps","echo"}. bActive mirrors `not pending_plan.is_empty()`.
 */
struct FRxCompanionPlan
{
	bool bActive = false;
	FString Intent;
	FRxJsonValue Target;              // canonical target object (dynamic, like the .gd Dictionary)
	TArray<FRxJsonValue> Steps;       // canonical command-envelope objects
	FString Echo;
};

class FRxCompanionAI
{
public:
	// --- identity / wiring (set by encounters setup or auto-bound from world) ---
	FRxEntityId EntityId = -1;
	FRxEntityId PlayerEntityId = -1;

	/** Mirrors _init(p_entity_id := -1, p_player_entity_id := -1). */
	explicit FRxCompanionAI(FRxEntityId InEntityId = -1, FRxEntityId InPlayerEntityId = -1);

	/** encounters setup hook: bind companion + player entity ids explicitly. */
	void Bind(FRxEntityId InEntityId, FRxEntityId InPlayerEntityId);

	// -----------------------------------------------------------------------
	// CONTRACT SURFACE (CONTRACTS.md §2)
	// -----------------------------------------------------------------------

	/** Parse -> echo (fixed tables) -> plan of command envelopes -> approval flag. */
	FRxInstructionResult ReceiveInstruction(FRxSimWorld& World, const FString& Text,
		const FRxJsonValue& Reference);

	/** Player approved the pending plan: submit every step through World.Submit. */
	void OnApproved(FRxSimWorld& World);

	/** UI cancel path (contract "cancel" command type). */
	void OnCancelled(FRxSimWorld& World);

	/** Transfer recognition (CONTRACTS.md §2, SHENRON §8): rule-based evidence only. */
	bool RecognizeTransfer(FRxSimWorld& World);

	/** One 20Hz tick: correction sync -> observation -> weave -> hazard dodge -> follow. */
	void AiTick(FRxSimWorld& World);

	// --- read accessors (state otherwise mirrors the open GDScript RefCounted) ---
	const FString& GetLastAuthority() const { return LastAuthority; }
	const FRxCompanionPlan& GetPendingPlan() const { return PendingPlan; }
	const FRxJsonValue& GetSessionState() const { return SessionState; }

private:
	// --- tuning (milli-units, ticks @ 20Hz; plain ints, CONTRACTS.md §0). These
	//     are the companion's OWN constants — note MoveSpeed = 500 deliberately
	//     differs from RxSim::MOVE_SPEED (600, the world glide speed). ---
	static constexpr int32 FollowMin        = 1500; // keep at least this distance from player
	static constexpr int32 FollowMax        = 3000; // close in when farther than this
	static constexpr int32 FollowIdeal      = 2200; // approach target inside the band
	static constexpr int32 MoveSpeed        = 500;  // mu/tick — pathless stepping (companion-local)
	static constexpr int32 HazardStressPct  = 60;   // autonomous dodge when own region stress > 60%
	static constexpr int32 TransferStressPct = 40;  // transfer recognition at >= 40% and rising
	static constexpr int32 WeaveDurationTicks = 100; // 5s commitment/exposure window
	static constexpr int32 DodgeReissueTicks = 10;  // min ticks between autonomous dodge moves
	static constexpr int32 WaitTicks        = 20;   // "wait" intent duration

	// --- session memory (tutorial misunderstanding fixture) ---
	FRxJsonValue SessionState; // {"stabilize_bridge_calls","pending_correction","corrected"}

	// --- plan / approval state ---
	FRxCompanionPlan PendingPlan;
	FString LastAuthority;     // "player-approved" | "auto" — receipt evidence

	// --- observation state (all ints; keyed map, never iterated for hash) ---
	TMap<int32, int32> LastStress; // region_id -> stress seen on previous recognize call
	int32 LastTremorTick = -1;
	int32 TremorSeen = 0;

	// --- weave lifecycle tracking ---
	bool bWeaveActive = false;
	int32 WeaveStartTick = 0;
	int32 WeaveRegionId = -1;
	FString WeaveMode = TEXT("anchor");

	// --- autonomous dodge state ---
	int32 LastDodgeTick = -1;
	int32 DodgeRegion = -1;

	// --- transfer recognition state ---
	bool bTransferLatched = false;
	bool bTransferAnnounced = false;
	int32 TransferRegion = -1;

	// --- plan construction (fixed, deterministic) ---
	TArray<FRxJsonValue> BuildSteps(FRxSimWorld& World, const FString& Intent,
		const FRxJsonValue& Target) const;
	static FString EchoFor(const FString& Intent, const FRxJsonValue& Target, bool bCorrection);
	FRxJsonValue MakeCmd(FRxSimWorld& World, const FString& Type, const FRxJsonValue& Params) const;
	void SubmitSteps(FRxSimWorld& World, TArray<FRxJsonValue>& Steps, const FString& Authority);

	// --- autonomous behavior (T0/T1 only) ---
	void FollowTick(FRxSimWorld& World, const FRxEntity& My);
	bool HazardCheck(FRxSimWorld& World, const FRxEntity& My);
	void WeaveTick(FRxSimWorld& World);
	void ProposeFaultline(FRxSimWorld& World, int32 RegionId);

	// --- observation (world-visible information only — SHENRON §11) ---
	void ObserveTremors(FRxSimWorld& World);
	void SyncCorrectionFlag(FRxSimWorld& World);
	void SampleStress(FRxSimWorld& World);
	void Say(FRxSimWorld& World, const FString& Line) const;

	// --- helpers (deterministic, integer-only) ---
	void AutoBind(FRxSimWorld& World);
	const FRxEntity* MyEntity(FRxSimWorld& World) const;
	const FRxEntity* PlayerEntity(FRxSimWorld& World);
	const FRxRegion* Region(FRxSimWorld& World, int32 RegionId) const;
	static FIntPoint Centroid(const FRxRegion* Reg);
	static FIntPoint PosOf(const FRxEntity& E);
	static bool HasPos(const FRxJsonValue& Target);
	static FIntPoint TargetPos(const FRxJsonValue& Target);
	static int64 IDist2(const FIntPoint& A, const FIntPoint& B);
	static int64 IDist(const FIntPoint& A, const FIntPoint& B);
	static int64 ISqrt(int64 N);
	void StepToward(FRxSimWorld& World, const FIntPoint& From, const FIntPoint& To, int64 StepLen);
	static bool IsT2Type(const FString& Type);
	static bool GetBool(const FRxJsonValue& Obj, const FString& Key, bool bDefault = false);
};

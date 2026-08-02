#include "RxCompanionAI.h"

#include "RxTerrain.h"    // FRxTerrain, FRxRegion (region graph + connectivity)
#include "RxSimWorld.h"   // FRxSimWorld, FRxSubmitResult, FRxEvent — world-owned (owner A, not yet written)
#include "RxIntent.h"     // FRxIntent — sibling parser module (owner B, not yet written)

/**
 * ASSUMED FRxSimWorld SURFACE (owner A reconciles RxSimWorld.h). Everything the
 * companion touches goes through these — none of them mutates authoritative state
 * on the companion's behalf except the two contract-sanctioned flag writes:
 *
 *   int32                              GetTick() const;
 *   const FRxTerrain&                  GetTerrain() const;              // read-only
 *   const TMap<FRxEntityId,FRxEntity>& GetEntities() const;            // read-only
 *   const TArray<FRxEvent>&            GetEvents() const;              // read-only (tremors)
 *   bool                               GetFlag(const FString&, bool bDefault=false) const;
 *   void                               SetFlag(const FString&, bool);  // correction_made / transfer_recognized ONLY
 *   FRxSubmitResult                    Submit(const FRxJsonValue& Command); // validate+queue; { bool bOk; FString Code; ... }
 *   void                               Emit(const FString& Type, const FRxJsonValue& Data);
 *
 * FRxEvent is assumed to expose at least int32 Tick and FString Type (plus an
 * FRxJsonValue Data the companion does not read). FRxSubmitResult is assumed to
 * expose at least bool bOk and FString Code.
 *
 * ASSUMED FRxIntent SURFACE (owner B reconciles RxIntent.h):
 *   static FRxJsonValue FRxIntent::Parse(const FString& Text,
 *       const FRxJsonValue& Reference, const FRxJsonValue& SessionState);
 *   returns object {ok:bool, intent:str, target:obj, correction:bool, clarification:str}.
 *
 * The FRxTerrain surface used (all const, already defined in RxTerrain.h):
 *   int32 RegionAt(const FIntPoint&) const; const TArray<FRxRegion>& GetRegions() const;
 *   bool RegionsConnected(int32,int32) const.
 */

// ---------------------------------------------------------------------------
// Fixed dialogue tables (SHENRON §11: short operational lines only). EXACT
// strings — replay/hash parity depends on them byte-for-byte.
// ---------------------------------------------------------------------------
namespace
{
	const FString Line_Follow          = TEXT("With you.");
	const FString Line_Wait            = TEXT("Holding position.");
	const FString Line_Cancelled       = TEXT("Standing down.");
	const FString Line_Rejected        = TEXT("That was rejected. I stay in my lane.");
	const FString Line_DodgeConnected  = TEXT("Moving back — distance should be enough.");
	const FString Line_DodgeDecouple   = TEXT("Distance won't save us. Decoupling from the structure.");
	const FString Line_WeaveBegin      = TEXT("Weaving. Keep them off me.");
	const FString Line_WeaveBroken     = TEXT("Weave broke. I need a window.");
	const FString Line_WeaveDone       = TEXT("Anchored. It holds.");
	const FString Line_Transfer        = TEXT("Different source. Same pattern. Stress is accumulating through a connected structure.");
	const FString Line_TransferInvariant = TEXT("Accumulated stress propagating through connected structure");
	const FString Line_Propose         = TEXT("FAULTLINE INTERRUPT on the stressed region. Your call.");

	// Uncertainty "?" lines: precursor evidence present but model incomplete
	// (emitted only before fragment_acquired — SHENRON §5 minute 4:00–4:45).
	const FString UncertaintyLines[3] = {
		TEXT("Loose stones. Dust from the seams. Something is accumulating?"),
		TEXT("The structure is talking. I can't read the whole sentence yet?"),
		TEXT("Vibration, no visible source. Safer ground — maybe?"),
	};

	// Authority tags carried on submitted envelopes.
	const FString Auth_Auto            = TEXT("auto");
	const FString Auth_PlayerApproved  = TEXT("player-approved");
}

// ---------------------------------------------------------------------------
// Construction / wiring
// ---------------------------------------------------------------------------

FRxCompanionAI::FRxCompanionAI(FRxEntityId InEntityId, FRxEntityId InPlayerEntityId)
	: EntityId(InEntityId)
	, PlayerEntityId(InPlayerEntityId)
{
	SessionState = FRxJsonValue::Object();
	SessionState.Set(TEXT("stabilize_bridge_calls"), FRxJsonValue::Int(0));
	SessionState.Set(TEXT("pending_correction"), FRxJsonValue::Bool(false));
	SessionState.Set(TEXT("corrected"), FRxJsonValue::Bool(false));
}

void FRxCompanionAI::Bind(FRxEntityId InEntityId, FRxEntityId InPlayerEntityId)
{
	EntityId = InEntityId;
	PlayerEntityId = InPlayerEntityId;
}

// ---------------------------------------------------------------------------
// CONTRACT SURFACE (CONTRACTS.md §2)
// ---------------------------------------------------------------------------

FRxInstructionResult FRxCompanionAI::ReceiveInstruction(FRxSimWorld& World, const FString& Text,
	const FRxJsonValue& Reference)
{
	// FRxIntent::Parse consumes the session blackboard mutably and returns a typed
	// FRxIntentResult (owner-B reconciled surface). SessionState is a member, so it
	// persists across instructions (tutorial misunderstanding fixture).
	const FRxIntentResult Res = FRxIntent::Parse(Text, Reference, SessionState);
	if (!Res.bOk)
	{
		const FString Clarify = Res.Clarification;
		Say(World, Clarify);
		FRxInstructionResult Fail;
		Fail.bOk = false;
		Fail.Echo = Clarify;
		Fail.bNeedsApproval = false; // plan {"steps": []}
		return Fail;
	}

	const FString Intent = Res.Intent;
	const FRxJsonValue Target = Res.Target;
	const bool bCorrection = Res.bCorrection;
	if (bCorrection)
	{
		// Player corrected the misunderstanding: latch world-visibly (SHENRON §5).
		// This is one of the ONLY two authoritative writes the companion performs,
		// and it is a session-blackboard flag, not simulation state.
		World.SetFlag(TEXT("correction_made"), true);
	}

	TArray<FRxJsonValue> Steps = BuildSteps(World, Intent, Target);
	const FString Echo = EchoFor(Intent, Target, bCorrection);

	bool bNeeds = false;
	for (const FRxJsonValue& S : Steps)
	{
		if (IsT2Type(S.GetString(TEXT("type"))))
		{
			bNeeds = true;
		}
	}

	PendingPlan.bActive = true;
	PendingPlan.Intent = Intent;
	PendingPlan.Target = Target;
	PendingPlan.Steps = Steps;
	PendingPlan.Echo = Echo;
	Say(World, Echo);

	if (!bNeeds)
	{
		// T0/T1-only plan: within companion authority, execute immediately.
		SubmitSteps(World, Steps, Auth_Auto);
		PendingPlan = FRxCompanionPlan(); // clear (bActive -> false)
		LastAuthority = Auth_Auto;
	}

	FRxInstructionResult Out;
	Out.bOk = true;
	Out.Echo = Echo;
	Out.PlanSteps = Steps;
	Out.bNeedsApproval = bNeeds;
	return Out;
}

void FRxCompanionAI::OnApproved(FRxSimWorld& World)
{
	if (!PendingPlan.bActive)
	{
		return;
	}
	LastAuthority = Auth_PlayerApproved;
	// T2 steps carry approved:true + authority "player-approved" (CONTRACTS.md §2).
	SubmitSteps(World, PendingPlan.Steps, Auth_PlayerApproved);
	PendingPlan = FRxCompanionPlan();
}

void FRxCompanionAI::OnCancelled(FRxSimWorld& World)
{
	if (!PendingPlan.bActive)
	{
		return;
	}
	PendingPlan = FRxCompanionPlan();
	Say(World, Line_Cancelled);
}

bool FRxCompanionAI::RecognizeTransfer(FRxSimWorld& World)
{
	const FRxEntity* My = MyEntity(World);
	int32 MyRegion = -1;
	if (My != nullptr)
	{
		MyRegion = World.GetTerrain().RegionAt(My->Pos);
	}

	if (!World.GetFlag(TEXT("fragment_acquired"), false))
	{
		SampleStress(World);
		return false;
	}

	if (!bTransferLatched && MyRegion != -1)
	{
		for (const FRxRegion& R : World.GetTerrain().GetRegions())
		{
			const int32 Rid = R.Id;
			const int32 Stress = R.Stress;
			const int32 Prev = LastStress.Contains(Rid) ? LastStress[Rid] : -1;
			const bool bRising = Stress > Prev;
			if (Stress * 100 >= TransferStressPct * RxSim::STRESS_THRESHOLD && bRising)
			{
				if (Rid == MyRegion || World.GetTerrain().RegionsConnected(MyRegion, Rid))
				{
					bTransferLatched = true;
					TransferRegion = Rid;
					break;
				}
			}
		}
	}

	SampleStress(World);

	if (bTransferLatched && !bTransferAnnounced)
	{
		bTransferAnnounced = true;
		World.SetFlag(TEXT("transfer_recognized"), true);
		Say(World, Line_Transfer);
		// Dedicated event for the full-screen SAME PATTERN beat (SHENRON §8/§13).
		FRxJsonValue Data = FRxJsonValue::Object();
		Data.Set(TEXT("entity_id"), FRxJsonValue::Int(EntityId));
		Data.Set(TEXT("region_id"), FRxJsonValue::Int(TransferRegion));
		Data.Set(TEXT("invariant"), FRxJsonValue::Str(Line_TransferInvariant));
		World.Emit(TEXT("transfer_recognized"), Data);
		ProposeFaultline(World, TransferRegion);
	}

	return bTransferLatched;
}

void FRxCompanionAI::AiTick(FRxSimWorld& World)
{
	AutoBind(World);
	SyncCorrectionFlag(World);
	ObserveTremors(World);
	RecognizeTransfer(World); // keeps stress map fresh; may emit proposal once

	const FRxEntity* My = MyEntity(World);
	if (My == nullptr)
	{
		return;
	}

	// Weaving is a commitment: no dodging, no following (SHENRON §5 exposure).
	if (My->State == FString(RxState::Weaving))
	{
		WeaveTick(World);
		return;
	}

	if (HazardCheck(World, *My))
	{
		return;
	}

	FollowTick(World, *My);
}

// ---------------------------------------------------------------------------
// PLAN CONSTRUCTION (fixed, deterministic)
// ---------------------------------------------------------------------------

TArray<FRxJsonValue> FRxCompanionAI::BuildSteps(FRxSimWorld& World, const FString& Intent,
	const FRxJsonValue& Target) const
{
	TArray<FRxJsonValue> Steps;
	const bool bHasPos = HasPos(Target);
	const FIntPoint Pos = TargetPos(Target);
	const int32 Rid = static_cast<int32>(Target.GetInt(TEXT("region_id"), -1));
	const int32 Eid = static_cast<int32>(Target.GetInt(TEXT("entity_id"), -1));

	auto MakeMoveStep = [&]() -> FRxJsonValue
	{
		FRxJsonValue P = FRxJsonValue::Object();
		P.Set(TEXT("x"), FRxJsonValue::Int(Pos.X));
		P.Set(TEXT("y"), FRxJsonValue::Int(Pos.Y));
		return MakeCmd(World, RxCmd::MoveTo, P);
	};

	if (Intent == TEXT("stabilize") || Intent == TEXT("anchor"))
	{
		if (bHasPos)
		{
			Steps.Add(MakeMoveStep());
		}
		FRxJsonValue P = FRxJsonValue::Object();
		P.Set(TEXT("mode"), FRxJsonValue::Str(TEXT("anchor")));
		P.Set(TEXT("region_id"), FRxJsonValue::Int(Rid));
		P.Set(TEXT("target_entity_id"), FRxJsonValue::Int(Eid));
		Steps.Add(MakeCmd(World, RxCmd::TokenweaveBegin, P));
	}
	else if (Intent == TEXT("weave"))
	{
		if (bHasPos)
		{
			Steps.Add(MakeMoveStep());
		}
		FRxJsonValue P = FRxJsonValue::Object();
		P.Set(TEXT("mode"), FRxJsonValue::Str(TEXT("fabricate")));
		P.Set(TEXT("region_id"), FRxJsonValue::Int(Rid));
		P.Set(TEXT("target_entity_id"), FRxJsonValue::Int(Eid));
		Steps.Add(MakeCmd(World, RxCmd::TokenweaveBegin, P));
	}
	else if (Intent == TEXT("strike") || Intent == TEXT("interrupt"))
	{
		// "interrupt" = strike the anchor point to break/delay accumulation.
		if (bHasPos)
		{
			Steps.Add(MakeMoveStep());
		}
		FRxJsonValue P = FRxJsonValue::Object();
		P.Set(TEXT("target_entity_id"), FRxJsonValue::Int(Eid));
		P.Set(TEXT("region_id"), FRxJsonValue::Int(Rid));
		Steps.Add(MakeCmd(World, RxCmd::Strike, P));
	}
	else if (Intent == TEXT("dodge_to"))
	{
		if (bHasPos)
		{
			Steps.Add(MakeMoveStep());
		}
	}
	else if (Intent == TEXT("follow"))
	{
		// follow is the default autonomous behavior; nothing to submit
	}
	else if (Intent == TEXT("wait"))
	{
		FRxJsonValue P = FRxJsonValue::Object();
		P.Set(TEXT("ticks"), FRxJsonValue::Int(WaitTicks));
		Steps.Add(MakeCmd(World, RxCmd::Wait, P));
	}

	return Steps;
}

FString FRxCompanionAI::EchoFor(const FString& Intent, const FRxJsonValue& Target, bool bCorrection)
{
	FString Echo = TEXT("Say that again?");
	if (Intent == TEXT("stabilize") || Intent == TEXT("anchor"))
	{
		const FString Side = Target.GetString(TEXT("support"), FString());
		if (Side == TEXT("west"))
		{
			Echo = TEXT("Anchor the western support and preserve the central path?");
		}
		else if (Side == TEXT("east"))
		{
			Echo = TEXT("Anchor the eastern support and preserve the central path?");
			if (bCorrection)
			{
				Echo = TEXT("Understood — east. ") + Echo;
			}
		}
		else
		{
			Echo = TEXT("Anchor the reference and hold the structure?");
		}
	}
	else if (Intent == TEXT("strike"))
	{
		Echo = TEXT("Strike the target. Confirm?");
	}
	else if (Intent == TEXT("interrupt"))
	{
		Echo = TEXT("Strike the anchor point and break the buildup?");
	}
	else if (Intent == TEXT("dodge_to"))
	{
		Echo = TEXT("Reposition to the referenced ground?");
	}
	else if (Intent == TEXT("weave"))
	{
		Echo = TEXT("Weave a reinforcement there. I'll be exposed — confirm?");
	}
	else if (Intent == TEXT("follow"))
	{
		Echo = Line_Follow;
	}
	else if (Intent == TEXT("wait"))
	{
		Echo = Line_Wait;
	}
	return Echo;
}

FRxJsonValue FRxCompanionAI::MakeCmd(FRxSimWorld& World, const FString& Type,
	const FRxJsonValue& Params) const
{
	// No "seq": assigned by SimWorld.Submit / Receipts at queue time (owner A).
	FRxJsonValue Cmd = FRxJsonValue::Object();
	Cmd.Set(TEXT("tick"), FRxJsonValue::Int(World.GetTick()));
	Cmd.Set(TEXT("actor"), FRxJsonValue::Str(FString(RxActor::Companion)));
	Cmd.Set(TEXT("type"), FRxJsonValue::Str(Type));
	Cmd.Set(TEXT("params"), Params);
	return Cmd;
}

void FRxCompanionAI::SubmitSteps(FRxSimWorld& World, TArray<FRxJsonValue>& Steps,
	const FString& Authority)
{
	for (FRxJsonValue& S : Steps)
	{
		S.Set(TEXT("tick"), FRxJsonValue::Int(World.GetTick()));
		S.Set(TEXT("actor"), FRxJsonValue::Str(FString(RxActor::Companion)));
		S.Set(TEXT("authority"), FRxJsonValue::Str(Authority));

		const FString StepType = S.GetString(TEXT("type"));
		if (IsT2Type(StepType))
		{
			// Approval came from the player (SHENRON §11 authority boundaries).
			S.Set(TEXT("approved"), FRxJsonValue::Bool(Authority == Auth_PlayerApproved));
		}

		const FRxSubmitResult R = World.Submit(S);
		if (!R.bOk)
		{
			Say(World, Line_Rejected);
			FRxJsonValue Data = FRxJsonValue::Object();
			Data.Set(TEXT("type"), FRxJsonValue::Str(StepType));
			Data.Set(TEXT("code"), FRxJsonValue::Str(R.Code));
			World.Emit(TEXT("companion_command_rejected"), Data);
		}
		else if (StepType == FString(RxCmd::TokenweaveBegin))
		{
			// Track target for the weave-completion effect observed in AiTick.
			if (const FRxJsonValue* P = S.Find(TEXT("params")))
			{
				WeaveRegionId = static_cast<int32>(P->GetInt(TEXT("region_id"), -1));
				WeaveMode = P->GetString(TEXT("mode"), TEXT("anchor"));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// AUTONOMOUS BEHAVIOR (T0/T1 only)
// ---------------------------------------------------------------------------

void FRxCompanionAI::FollowTick(FRxSimWorld& World, const FRxEntity& My)
{
	const FRxEntity* Player = PlayerEntity(World);
	if (Player == nullptr)
	{
		return;
	}
	const FIntPoint MyPos = PosOf(My);
	const FIntPoint PPos = PosOf(*Player);
	const int64 D = IDist(MyPos, PPos);
	if (D > FollowMax)
	{
		StepToward(World, MyPos, PPos, FMath::Min<int64>(MoveSpeed, D - FollowIdeal));
	}
	else if (D < FollowMin && D > 0)
	{
		StepToward(World, PPos, MyPos, FMath::Min<int64>(MoveSpeed, static_cast<int64>(FollowMin) - D));
	}
}

bool FRxCompanionAI::HazardCheck(FRxSimWorld& World, const FRxEntity& My)
{
	const FIntPoint MyPos = PosOf(My);
	const int32 Rid = World.GetTerrain().RegionAt(MyPos);
	if (Rid == -1)
	{
		return false;
	}
	const FRxRegion* Reg = Region(World, Rid);
	if (Reg == nullptr)
	{
		return false;
	}
	const int32 Stress = Reg->Stress;
	if (Stress * 100 <= HazardStressPct * RxSim::STRESS_THRESHOLD)
	{
		return false;
	}

	// BEFORE the correction the companion misclassifies and dodges by DISTANCE
	// ALONE (nearest other region, still connected -> keeps receiving propagation).
	// AFTER the correction it decouples: connected regions are not safe ground.
	const bool bDecouple = World.GetFlag(TEXT("correction_made"), false);
	int32 Best = -1;
	int64 BestD = -1;
	int32 Fallback = -1;
	int64 FallbackD = -1;
	for (const FRxRegion& R : World.GetTerrain().GetRegions())
	{
		const int32 Cand = R.Id;
		if (Cand == Rid)
		{
			continue;
		}
		const int64 D = IDist2(MyPos, Centroid(&R));
		if (Fallback == -1 || D < FallbackD)
		{
			Fallback = Cand;
			FallbackD = D;
		}
		if (bDecouple && World.GetTerrain().RegionsConnected(Rid, Cand))
		{
			continue; // post-correction: connected ground is not safe ground
		}
		if (Best == -1 || D < BestD)
		{
			Best = Cand;
			BestD = D;
		}
	}
	if (Best == -1)
	{
		Best = Fallback; // no disconnected region exists; distance is all we have
	}
	if (Best == -1)
	{
		return false;
	}

	if (DodgeRegion != Best)
	{
		DodgeRegion = Best;
		Say(World, bDecouple ? Line_DodgeDecouple : Line_DodgeConnected);
	}
	if (LastDodgeTick < 0 || World.GetTick() - LastDodgeTick >= DodgeReissueTicks)
	{
		LastDodgeTick = World.GetTick();
		const FIntPoint C = Centroid(Region(World, Best));
		FRxJsonValue P = FRxJsonValue::Object();
		P.Set(TEXT("x"), FRxJsonValue::Int(C.X));
		P.Set(TEXT("y"), FRxJsonValue::Int(C.Y));
		TArray<FRxJsonValue> DodgeSteps;
		DodgeSteps.Add(MakeCmd(World, RxCmd::MoveTo, P));
		SubmitSteps(World, DodgeSteps, Auth_Auto);
	}
	return true;
}

void FRxCompanionAI::WeaveTick(FRxSimWorld& World)
{
	if (World.GetFlag(TEXT("weave_interrupted"), false))
	{
		if (bWeaveActive)
		{
			bWeaveActive = false;
			Say(World, Line_WeaveBroken);
		}
		return;
	}
	if (!bWeaveActive)
	{
		bWeaveActive = true;
		WeaveStartTick = World.GetTick();
		Say(World, Line_WeaveBegin);
		return;
	}
	const int32 Elapsed = World.GetTick() - WeaveStartTick;
	// Progress event for the presentation weave bar (informational only).
	FRxJsonValue Data = FRxJsonValue::Object();
	Data.Set(TEXT("entity_id"), FRxJsonValue::Int(EntityId));
	Data.Set(TEXT("elapsed"), FRxJsonValue::Int(Elapsed));
	Data.Set(TEXT("duration"), FRxJsonValue::Int(WeaveDurationTicks));
	Data.Set(TEXT("region_id"), FRxJsonValue::Int(WeaveRegionId));
	Data.Set(TEXT("mode"), FRxJsonValue::Str(WeaveMode));
	World.Emit(TEXT("weave_progress"), Data);
	if (Elapsed >= WeaveDurationTicks)
	{
		// Sim upkeep applies the actual anchor/dampen this tick; the companion
		// only stops tracking and voices completion. No state writes here.
		bWeaveActive = false;
		Say(World, Line_WeaveDone);
	}
}

void FRxCompanionAI::ProposeFaultline(FRxSimWorld& World, int32 RegionId)
{
	const FIntPoint C = Centroid(Region(World, RegionId));
	FRxJsonValue Target = FRxJsonValue::Object();
	Target.Set(TEXT("kind"), FRxJsonValue::Str(TEXT("region")));
	Target.Set(TEXT("region_id"), FRxJsonValue::Int(RegionId));
	Target.Set(TEXT("pos"), FRxJsonValue::IntPoint(C));

	FRxJsonValue Params = FRxJsonValue::Object();
	Params.Set(TEXT("skill_id"), FRxJsonValue::Str(TEXT("faultline_interrupt")));
	Params.Set(TEXT("target"), Target);
	const FRxJsonValue Step = MakeCmd(World, RxCmd::UseSkill, Params);

	// Plan only — parked for player approval (NEVER auto-submitted; T2).
	PendingPlan.bActive = true;
	PendingPlan.Intent = TEXT("interrupt");
	PendingPlan.Target = Target;
	PendingPlan.Steps.Reset();
	PendingPlan.Steps.Add(Step);
	PendingPlan.Echo = Line_Propose;

	Say(World, Line_Propose);
	FRxJsonValue Data = FRxJsonValue::Object();
	Data.Set(TEXT("skill_id"), FRxJsonValue::Str(TEXT("faultline_interrupt")));
	Data.Set(TEXT("target_region"), FRxJsonValue::Int(RegionId));
	Data.Set(TEXT("needs_approval"), FRxJsonValue::Bool(true));
	Data.Set(TEXT("line"), FRxJsonValue::Str(Line_Propose));
	Data.Set(TEXT("text"), FRxJsonValue::Str(Line_Propose));
	World.Emit(TEXT("companion_proposal"), Data);
}

// ---------------------------------------------------------------------------
// OBSERVATION (world-visible information only — SHENRON §11)
// ---------------------------------------------------------------------------

void FRxCompanionAI::ObserveTremors(FRxSimWorld& World)
{
	// Say() -> World.Emit() appends to World's event array while this loop reads it,
	// which invalidates a UE ranged-for iterator (Array.h ranged-for guard / dangling
	// pointer after a reallocation). GDScript's `for ev in world.events` re-checks the
	// array size every step, so elements appended during the loop ARE visited; those
	// are always "companion_say" events, which the type filter below skips. Index
	// iteration against a live Num() reproduces that contract exactly, and the element
	// reference is re-fetched each step so it never outlives a reallocation.
	for (int32 i = 0; i < World.GetEvents().Num(); ++i)
	{
		const FRxEvent& Ev = World.GetEvents()[i];
		if (Ev.Type != FString(TEXT("tremor")))
		{
			continue;
		}
		const int32 T = Ev.Tick;
		if (T <= LastTremorTick)
		{
			continue;
		}
		LastTremorTick = T;
		if (!World.GetFlag(TEXT("fragment_acquired"), false))
		{
			Say(World, UncertaintyLines[TremorSeen % 3]);
			TremorSeen += 1;
		}
	}
}

void FRxCompanionAI::SyncCorrectionFlag(FRxSimWorld& World)
{
	if (World.GetFlag(TEXT("correction_made"), false) && !GetBool(SessionState, TEXT("corrected"), false))
	{
		SessionState.Set(TEXT("corrected"), FRxJsonValue::Bool(true));
		SessionState.Set(TEXT("pending_correction"), FRxJsonValue::Bool(false));
	}
}

void FRxCompanionAI::SampleStress(FRxSimWorld& World)
{
	for (const FRxRegion& R : World.GetTerrain().GetRegions())
	{
		LastStress.Add(R.Id, R.Stress);
	}
}

void FRxCompanionAI::Say(FRxSimWorld& World, const FString& Line) const
{
	// "line" + "text" carry the same string: presentation reads either variant.
	FRxJsonValue Data = FRxJsonValue::Object();
	Data.Set(TEXT("entity_id"), FRxJsonValue::Int(EntityId));
	Data.Set(TEXT("line"), FRxJsonValue::Str(Line));
	Data.Set(TEXT("text"), FRxJsonValue::Str(Line));
	World.Emit(TEXT("companion_say"), Data);
}

// ---------------------------------------------------------------------------
// HELPERS (deterministic, integer-only)
// ---------------------------------------------------------------------------

void FRxCompanionAI::AutoBind(FRxSimWorld& World)
{
	if (EntityId != -1 && PlayerEntityId != -1)
	{
		return;
	}
	const TMap<FRxEntityId, FRxEntity>& Ents = World.GetEntities();
	TArray<FRxEntityId> Keys;
	Ents.GenerateKeyArray(Keys);
	Keys.Sort(); // sorted keys: deterministic map access order (mirrors keys.sort())
	for (const FRxEntityId K : Keys)
	{
		const FRxEntity& E = Ents[K];
		if (E.Kind == FString(RxKind::Companion) && EntityId == -1)
		{
			EntityId = K;
		}
		else if (E.Kind == FString(RxKind::Player) && PlayerEntityId == -1)
		{
			PlayerEntityId = K;
		}
	}
}

const FRxEntity* FRxCompanionAI::MyEntity(FRxSimWorld& World) const
{
	if (EntityId == -1)
	{
		return nullptr;
	}
	return World.GetEntities().Find(EntityId);
}

const FRxEntity* FRxCompanionAI::PlayerEntity(FRxSimWorld& World)
{
	const TMap<FRxEntityId, FRxEntity>& Ents = World.GetEntities();
	if (PlayerEntityId != -1)
	{
		if (const FRxEntity* E = Ents.Find(PlayerEntityId))
		{
			return E;
		}
	}
	// Fallback: first "player" entity by sorted id (deterministic).
	TArray<FRxEntityId> Keys;
	Ents.GenerateKeyArray(Keys);
	Keys.Sort();
	for (const FRxEntityId K : Keys)
	{
		const FRxEntity& E = Ents[K];
		if (E.Kind == FString(RxKind::Player))
		{
			PlayerEntityId = K;
			return &E;
		}
	}
	return nullptr;
}

const FRxRegion* FRxCompanionAI::Region(FRxSimWorld& World, int32 RegionId) const
{
	for (const FRxRegion& R : World.GetTerrain().GetRegions())
	{
		if (R.Id == RegionId)
		{
			return &R;
		}
	}
	return nullptr;
}

FIntPoint FRxCompanionAI::Centroid(const FRxRegion* Reg)
{
	if (Reg == nullptr || Reg->Poly.Num() == 0)
	{
		return FIntPoint::ZeroValue;
	}
	int32 Sx = 0;
	int32 Sy = 0;
	int32 N = 0;
	for (const FIntPoint& P : Reg->Poly)
	{
		Sx += P.X;
		Sy += P.Y;
		N += 1;
	}
	if (N == 0)
	{
		return FIntPoint::ZeroValue;
	}
	return FIntPoint(Sx / N, Sy / N);
}

FIntPoint FRxCompanionAI::PosOf(const FRxEntity& E)
{
	return E.Pos;
}

bool FRxCompanionAI::HasPos(const FRxJsonValue& Target)
{
	const FRxJsonValue* P = Target.Find(TEXT("pos"));
	return P != nullptr && P->Type == ERxJsonType::Array && P->ArrayItems.Num() >= 2;
}

FIntPoint FRxCompanionAI::TargetPos(const FRxJsonValue& Target)
{
	if (!HasPos(Target))
	{
		return FIntPoint::ZeroValue;
	}
	const FRxJsonValue* P = Target.Find(TEXT("pos"));
	return FIntPoint(static_cast<int32>(P->ArrayItems[0].IntValue),
		static_cast<int32>(P->ArrayItems[1].IntValue));
}

int64 FRxCompanionAI::IDist2(const FIntPoint& A, const FIntPoint& B)
{
	const int64 Dx = static_cast<int64>(B.X) - static_cast<int64>(A.X);
	const int64 Dy = static_cast<int64>(B.Y) - static_cast<int64>(A.Y);
	return Dx * Dx + Dy * Dy;
}

int64 FRxCompanionAI::IDist(const FIntPoint& A, const FIntPoint& B)
{
	return ISqrt(IDist2(A, B));
}

int64 FRxCompanionAI::ISqrt(int64 N)
{
	// Integer square root (Newton), deterministic, no floats — mirrors _isqrt.
	if (N <= 0)
	{
		return 0;
	}
	int64 X = N;
	int64 Y = (X + 1) / 2;
	while (Y < X)
	{
		X = Y;
		Y = (X + N / X) / 2;
	}
	return X;
}

void FRxCompanionAI::StepToward(FRxSimWorld& World, const FIntPoint& From, const FIntPoint& To,
	int64 StepLen)
{
	const int64 D = IDist(From, To);
	if (D == 0 || StepLen <= 0)
	{
		return;
	}
	const int64 Step = FMath::Min<int64>(StepLen, D);
	const int32 Nx = From.X + static_cast<int32>((static_cast<int64>(To.X) - From.X) * Step / D);
	const int32 Ny = From.Y + static_cast<int32>((static_cast<int64>(To.Y) - From.Y) * Step / D);
	if (Nx == From.X && Ny == From.Y)
	{
		return;
	}
	FRxJsonValue P = FRxJsonValue::Object();
	P.Set(TEXT("x"), FRxJsonValue::Int(Nx));
	P.Set(TEXT("y"), FRxJsonValue::Int(Ny));
	TArray<FRxJsonValue> MoveSteps;
	MoveSteps.Add(MakeCmd(World, RxCmd::MoveTo, P));
	SubmitSteps(World, MoveSteps, Auth_Auto);
}

bool FRxCompanionAI::IsT2Type(const FString& Type)
{
	// T2 per CONTRACTS.md §2 authority tiers (weave/skill/strike) — requires approval.
	return Type == FString(RxCmd::TokenweaveBegin)
		|| Type == FString(RxCmd::UseSkill)
		|| Type == FString(RxCmd::Strike);
}

bool FRxCompanionAI::GetBool(const FRxJsonValue& Obj, const FString& Key, bool bDefault)
{
	if (const FRxJsonValue* V = Obj.Find(Key))
	{
		if (V->Type == ERxJsonType::Bool)
		{
			return V->bValue;
		}
	}
	return bDefault;
}

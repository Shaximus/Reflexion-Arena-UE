#include "RxCommands.h"

// World-owned units — full definitions needed for the state-check reads.
// (RxSimWorld.h is not written yet; this TU intentionally depends on it, exactly
// as RxSkillSystem.cpp does. See the integration report for assumed signatures.)
#include "RxSimWorld.h"      // FRxSimWorld, FRxEntity, FRxTerrain accessors
#include "RxSkillSystem.h"   // FRxSkillSystem::ValidateSpec, CanUse, FRxSkillSpec/Target/Result

// ---------------------------------------------------------------------------
// Local helpers — FRxJsonValue param access mirroring GDScript typed reads.
//   params.get(k) is int    -> IsInt     (present AND Int)
//   params.get(k) is String -> IsString  (present AND String)
//   params.get(k) is Dictionary -> IsObject (present AND Object)
//   int(v)                  -> ToInt64  (GDScript int() coercion for the closed set)
// ---------------------------------------------------------------------------
namespace
{
	bool IsInt(const FRxJsonValue& P, const TCHAR* Key)
	{
		const FRxJsonValue* V = P.Find(Key);
		return V != nullptr && V->Type == ERxJsonType::Int;
	}

	bool IsString(const FRxJsonValue& P, const TCHAR* Key)
	{
		const FRxJsonValue* V = P.Find(Key);
		return V != nullptr && V->Type == ERxJsonType::String;
	}

	bool IsObject(const FRxJsonValue& P, const TCHAR* Key)
	{
		const FRxJsonValue* V = P.Find(Key);
		return V != nullptr && V->Type == ERxJsonType::Object;
	}

	// Mirror of GDScript int(v) for the canonical closed value set:
	//   int -> value; String -> leading-integer parse; bool -> 0/1; else -> 0.
	int64 ToInt64(const FRxJsonValue& V)
	{
		switch (V.Type)
		{
		case ERxJsonType::Int:    return V.IntValue;
		case ERxJsonType::String: return FCString::Atoi64(*V.StringValue);
		case ERxJsonType::Bool:   return V.bValue ? 1 : 0;
		default:                  return 0;
		}
	}

	// Mirror of params.get(primary, params.get(secondary, Default)) followed by
	// int(): prefer `primary` if the KEY exists (any type, coerced), else
	// `secondary` if it exists, else Default. Used by strike target/region resolve.
	int64 GetIntFallback(const FRxJsonValue& P, const TCHAR* Primary,
		const TCHAR* Secondary, int64 Default)
	{
		if (const FRxJsonValue* V = P.Find(Primary))
		{
			return ToInt64(*V);
		}
		if (const FRxJsonValue* V = P.Find(Secondary))
		{
			return ToInt64(*V);
		}
		return Default;
	}
}

// ---------------------------------------------------------------------------
// Closed vocabularies (ACTORS / TYPES / T0 / T1 / T2) — built from the shared
// RxActor/RxCmd string constants so spelling matches the canonical snapshot.
// ---------------------------------------------------------------------------

const TArray<FString>& FRxCommands::Actors()
{
	static const TArray<FString> Value = {
		RxActor::Player, RxActor::Companion, RxActor::System,
	};
	return Value;
}

const TArray<FString>& FRxCommands::Types()
{
	static const TArray<FString> Value = {
		RxCmd::MoveTo, RxCmd::Strike, RxCmd::Reference, RxCmd::Instruct,
		RxCmd::Approve, RxCmd::Cancel, RxCmd::Correct, RxCmd::UseSkill,
		RxCmd::TokenweaveBegin, RxCmd::SocketFragment, RxCmd::AuthorSkill,
		RxCmd::Wait,
	};
	return Value;
}

const TArray<FString>& FRxCommands::T0Types()
{
	static const TArray<FString> Value = {
		RxCmd::Reference, RxCmd::Instruct, RxCmd::Approve,
		RxCmd::Cancel, RxCmd::Correct, RxCmd::Wait,
	};
	return Value;
}

const TArray<FString>& FRxCommands::T1Types()
{
	static const TArray<FString> Value = { RxCmd::MoveTo };
	return Value;
}

const TArray<FString>& FRxCommands::T2Types()
{
	static const TArray<FString> Value = {
		RxCmd::Strike, RxCmd::TokenweaveBegin, RxCmd::UseSkill,
		RxCmd::SocketFragment, RxCmd::AuthorSkill,
	};
	return Value;
}

// ---------------------------------------------------------------------------
// tier_of
// ---------------------------------------------------------------------------

int32 FRxCommands::TierOf(const FString& Type)
{
	if (T0Types().Contains(Type)) { return 0; }
	if (T1Types().Contains(Type)) { return 1; }
	if (T2Types().Contains(Type)) { return 2; }
	return -1;
}

// ---------------------------------------------------------------------------
// validate
// ---------------------------------------------------------------------------

FRxCmdResult FRxCommands::Validate(const FRxCommandEnvelope& Cmd, FRxSimWorld* World)
{
	using namespace RxCmdDetail;

	// ---- structural (ERR_MALFORMED) ----
	// The `cmd is Dictionary` guard is handled at the SimWorld.submit boundary
	// (an envelope here is always a well-formed struct).
	//
	// actor: must be a String in {player,companion,system}. The "is String" facet
	// is inherent to the typed field; the value-membership check is reproduced.
	if (!Actors().Contains(Cmd.Actor))
	{
		return FRxCmdResult::Err(RxCode::ErrMalformed, ActorInvalid);
	}
	// type: must be a non-empty String.
	if (Cmd.Type.IsEmpty())
	{
		return FRxCmdResult::Err(RxCode::ErrMalformed, TypeInvalid);
	}
	// seq: "is int when present" — guaranteed by the typed int32 field; a
	// genuinely non-int seq is rejected at the parser boundary (RxCmdDetail::SeqNotInt).
	//
	// tick: non-negative int when present (VALUE-level check reproduced).
	if (Cmd.bHasTick && Cmd.Tick < 0)
	{
		return FRxCmdResult::Err(RxCode::ErrMalformed, TickNotNonNegInt);
	}
	// approved: "is bool when present" — guaranteed by the typed bool field; a
	// genuinely non-bool approved is rejected at the parser boundary
	// (RxCmdDetail::ApprovedNotBool).
	//
	// params: must be a Dictionary (Object).
	if (Cmd.Params.Type != ERxJsonType::Object)
	{
		return FRxCmdResult::Err(RxCode::ErrMalformed, ParamsNotDict);
	}

	const FString& Type = Cmd.Type;
	const FRxJsonValue& Params = Cmd.Params;

	// ---- vocabulary (ERR_UNKNOWN_TYPE) ----
	if (!Types().Contains(Type))
	{
		return FRxCmdResult::Err(RxCode::ErrUnknownType,
			FString(TEXT("unknown command type: ")) + Type);
	}

	// ---- per-type parameter shape (ERR_MALFORMED) ----
	if (Type == RxCmd::MoveTo)
	{
		if (!IsInt(Params, TEXT("x")) || !IsInt(Params, TEXT("y")))
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, MoveToParams);
		}
	}
	else if (Type == RxCmd::Strike)
	{
		const bool bHasRegion = IsInt(Params, TEXT("region")) || IsInt(Params, TEXT("region_id"));
		const bool bHasEntity = IsInt(Params, TEXT("target_id")) || IsInt(Params, TEXT("target_entity_id"));
		if (!bHasRegion && !bHasEntity)
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, StrikeParams);
		}
	}
	else if (Type == RxCmd::Reference)
	{
		if (!IsObject(Params, TEXT("target")))
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, ReferenceTarget);
		}
	}
	else if (Type == RxCmd::Instruct || Type == RxCmd::Correct)
	{
		if (!IsString(Params, TEXT("text")))
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed,
				Type + TEXT(" requires String params.text"));
		}
	}
	else if (Type == RxCmd::UseSkill)
	{
		if (!IsString(Params, TEXT("skill_id")))
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, UseSkillSkillId);
		}
		if (!IsObject(Params, TEXT("target")))
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, UseSkillTarget);
		}
	}
	else if (Type == RxCmd::TokenweaveBegin)
	{
		if (Params.HasKey(TEXT("region_id")) && !IsInt(Params, TEXT("region_id")))
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, TokenweaveRegion);
		}
		if (Params.HasKey(TEXT("mode")))
		{
			const FRxJsonValue* Mode = Params.Find(TEXT("mode"));
			const bool bLegalMode = Mode != nullptr
				&& Mode->Type == ERxJsonType::String
				&& (Mode->StringValue == TEXT("anchor") || Mode->StringValue == TEXT("fabricate"));
			if (!bLegalMode)
			{
				return FRxCmdResult::Err(RxCode::ErrMalformed, TokenweaveMode);
			}
		}
	}
	else if (Type == RxCmd::SocketFragment)
	{
		if (!IsObject(Params, TEXT("fragment")))
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, SocketFragment);
		}
	}
	else if (Type == RxCmd::AuthorSkill)
	{
		if (!IsObject(Params, TEXT("spec")))
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, AuthorSkillSpec);
		}
	}

	// ---- authority (ERR_AUTHORITY) ----
	const int32 Tier = TierOf(Type);
	if (Tier < 0)
	{
		return FRxCmdResult::Err(RxCode::ErrUnknownType,
			FString(TEXT("type has no authority tier: ")) + Type);
	}
	const bool bApprovedVal = Cmd.bHasApproved ? Cmd.bApproved : false; // cmd.get("approved", false)
	if (Cmd.Actor == RxActor::Companion && Tier >= 2 && bApprovedVal != true)
	{
		return FRxCmdResult::Err(RxCode::ErrAuthority,
			FString::Printf(TEXT("companion T2 command '%s' requires approved:true"), *Type));
	}

	// ---- world-state checks (ERR_STATE) ----
	if (World != nullptr)
	{
		const FRxCmdResult StateCheck = ValidateState(Cmd, World, Cmd.Actor, Type, Params);
		if (!StateCheck.bOk)
		{
			return StateCheck;
		}
	}
	return FRxCmdResult::Ok();
}

// ---------------------------------------------------------------------------
// _validate_state
// ---------------------------------------------------------------------------

FRxCmdResult FRxCommands::ValidateState(const FRxCommandEnvelope& /*Cmd*/, FRxSimWorld* World,
	const FString& Actor, const FString& Type, const FRxJsonValue& Params)
{
	using namespace RxCmdDetail;

	const int32 Eid = World->EntityIdForActor(Actor);
	const FString NoEntityDetail = FString(TEXT("no entity for actor ")) + Actor;

	if (Type == RxCmd::MoveTo)
	{
		if (Actor != RxActor::System && Eid == -1)
		{
			return FRxCmdResult::Err(RxCode::ErrState, NoEntityDetail);
		}
	}
	else if (Type == RxCmd::Reference || Type == RxCmd::Instruct || Type == RxCmd::Correct)
	{
		if (Actor != RxActor::System && Eid == -1)
		{
			return FRxCmdResult::Err(RxCode::ErrState, NoEntityDetail);
		}
		if (Type == RxCmd::Instruct && !World->HasCompanion())
		{
			return FRxCmdResult::Err(RxCode::ErrState, NoCompanionInstruct);
		}
	}
	else if (Type == RxCmd::Approve || Type == RxCmd::Cancel)
	{
		if (!World->HasCompanion())
		{
			return FRxCmdResult::Err(RxCode::ErrState, NoCompanionApproveCancel);
		}
	}
	else if (Type == RxCmd::Strike)
	{
		if (Eid == -1)
		{
			return FRxCmdResult::Err(RxCode::ErrState, NoEntityDetail);
		}
		return ValidateStrike(World, Eid, Params);
	}
	else if (Type == RxCmd::TokenweaveBegin)
	{
		if (Eid == -1)
		{
			return FRxCmdResult::Err(RxCode::ErrState, NoEntityDetail);
		}
		const FRxEntity& E = World->GetEntity(Eid);
		if (E.State == RxState::Weaving)
		{
			return FRxCmdResult::Err(RxCode::ErrState, EntityAlreadyWeaving);
		}
		const int32 Rid = Params.HasKey(TEXT("region_id"))
			? static_cast<int32>(ToInt64(*Params.Find(TEXT("region_id")))) : -1;
		if (Rid != -1 && !World->Terrain().RegionExists(Rid))
		{
			return FRxCmdResult::Err(RxCode::ErrState,
				FString::Printf(TEXT("unknown weave region %d"), Rid));
		}
	}
	else if (Type == RxCmd::UseSkill)
	{
		if (Eid == -1)
		{
			return FRxCmdResult::Err(RxCode::ErrState, NoEntityDetail);
		}
		// can_use(world, eid, str(params.skill_id), params.target)
		const FString SkillId = Params.GetString(TEXT("skill_id"));
		FRxSkillTarget Target;
		if (const FRxJsonValue* T = Params.Find(TEXT("target")))
		{
			if (const FRxJsonValue* R = T->Find(TEXT("region_id")))
			{
				if (R->Type == ERxJsonType::Int) { Target.bHasRegionId = true; Target.RegionId = static_cast<int32>(R->IntValue); }
			}
			if (const FRxJsonValue* R = T->Find(TEXT("region")))
			{
				if (R->Type == ERxJsonType::Int) { Target.bHasRegion = true; Target.Region = static_cast<int32>(R->IntValue); }
			}
		}
		const FRxSkillResult Can = World->Skills().CanUse(*World, Eid, SkillId, Target);
		if (!Can.bOk)
		{
			return FRxCmdResult::Err(RxCode::ErrState,
				Can.Detail.IsEmpty() ? FString(SkillNotUsable) : Can.Detail);
		}
	}
	else if (Type == RxCmd::SocketFragment)
	{
		if (!World->GetFlag(TEXT("boss_defeated")))
		{
			return FRxCmdResult::Err(RxCode::ErrState, NoFragmentBeforeBoss);
		}
	}
	else if (Type == RxCmd::AuthorSkill)
	{
		if (!World->GetFlag(TEXT("fragment_acquired")))
		{
			return FRxCmdResult::Err(RxCode::ErrState, AuthoringRequiresFragment);
		}
		// SkillSystem.validate_spec(params.spec)
		FRxSkillSpec Spec;
		if (const FRxJsonValue* S = Params.Find(TEXT("spec")))
		{
			Spec.Name        = S->GetString(TEXT("name"));
			Spec.Trigger     = S->GetString(TEXT("trigger"));
			Spec.Effect      = S->GetString(TEXT("effect"));
			Spec.Cost        = static_cast<int32>(S->GetInt(TEXT("cost"), -1));
			Spec.Cooldown    = static_cast<int32>(S->GetInt(TEXT("cooldown"), -1));
			Spec.CommitWindow = static_cast<int32>(S->GetInt(TEXT("commit_window"), -1));
		}
		const FRxSkillResult SpecCheck = FRxSkillSystem::ValidateSpec(Spec);
		if (!SpecCheck.bOk)
		{
			return FRxCmdResult::Err(RxCode::ErrState,
				SpecCheck.Detail.IsEmpty() ? FString(IllegalSpec) : SpecCheck.Detail);
		}
	}

	return FRxCmdResult::Ok();
}

// ---------------------------------------------------------------------------
// _validate_strike
// ---------------------------------------------------------------------------

FRxCmdResult FRxCommands::ValidateStrike(FRxSimWorld* World, int32 AttackerId,
	const FRxJsonValue& Params)
{
	using namespace RxCmdDetail;

	// attacker = world.entities.get(attacker_id, {}); if attacker.is_empty(): ...
	if (!World->HasEntity(AttackerId))
	{
		return FRxCmdResult::Err(RxCode::ErrState, AttackerMissing);
	}
	const FRxEntity& Attacker = World->GetEntity(AttackerId);

	FString TargetKey;
	const bool bEntityTarget = IsInt(Params, TEXT("target_id")) || IsInt(Params, TEXT("target_entity_id"));
	if (bEntityTarget)
	{
		// int(params.get("target_id", params.get("target_entity_id", -1)))
		const int32 Tid = static_cast<int32>(
			GetIntFallback(Params, TEXT("target_id"), TEXT("target_entity_id"), -1));
		if (!World->HasEntity(Tid))
		{
			return FRxCmdResult::Err(RxCode::ErrState,
				FString::Printf(TEXT("strike target entity %d does not exist"), Tid));
		}
		const int32 D = World->Idist(Attacker.Pos, World->GetEntity(Tid).Pos);
		if (D > RxSim::STRIKE_RANGE)
		{
			return FRxCmdResult::Err(RxCode::ErrState, TargetOutOfRange);
		}
		TargetKey = FString::Printf(TEXT("e%d"), Tid);
	}
	else
	{
		// int(params.get("region", params.get("region_id", -1)))
		const int32 Rid = static_cast<int32>(
			GetIntFallback(Params, TEXT("region"), TEXT("region_id"), -1));
		if (!World->Terrain().RegionExists(Rid))
		{
			return FRxCmdResult::Err(RxCode::ErrState,
				FString::Printf(TEXT("strike target region %d does not exist"), Rid));
		}
		const int32 MyRegion = World->Terrain().RegionAt(Attacker.Pos);
		if (MyRegion != Rid && !World->Terrain().RegionsConnected(MyRegion, Rid))
		{
			return FRxCmdResult::Err(RxCode::ErrState, RegionNotConnected);
		}
		TargetKey = FString::Printf(TEXT("r%d"), Rid);
	}

	if (!World->StrikeCooldownOk(AttackerId, TargetKey))
	{
		return FRxCmdResult::Err(RxCode::ErrState, StrikeOnCooldown);
	}
	return FRxCmdResult::Ok();
}

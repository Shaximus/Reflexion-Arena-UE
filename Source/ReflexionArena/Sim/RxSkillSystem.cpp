#include "RxSkillSystem.h"

// Shared sim units — world-owned headers (see integration report for the exact
// assumed signatures this translation unit depends on).
#include "RxSimWorld.h"    // FRxSimWorld, FRxTerrain, FRxCommandEnvelope
#include "RxCanonJson.h"   // FRxCanonJson::HashValue (UE port of canon_json.gd)

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ---------------------------------------------------------------------------
// Small canonical-JSON build helpers (GDScript Dictionary -> FJsonObject).
// Integers are set via SetNumberField; FRxCanonJson is responsible for rendering
// integral numbers without a fractional part (CONTRACTS.md §0 "never float").
// ---------------------------------------------------------------------------
namespace
{
	TSharedRef<FJsonObject> NewObj()
	{
		return MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonValue> Str(const FString& S)
	{
		return MakeShared<FJsonValueString>(S);
	}
}

// ---------------------------------------------------------------------------
// Static fixed-choice tables (CONTRACTS.md §2 "no free-form")
// ---------------------------------------------------------------------------

const FString& FRxSkillSystem::SkillIdName()
{
	static const FString Value(TEXT("faultline_interrupt"));
	return Value;
}

const FString& FRxSkillSystem::ResidualRiskText()
{
	static const FString Value(
		TEXT("wrong surface id amplifies local instability (+200 stress on wrong region)"));
	return Value;
}

const TArray<FString>& FRxSkillSystem::LegalNames()
{
	static const TArray<FString> Value = { TEXT("FAULTLINE INTERRUPT") };
	return Value;
}

const TArray<FString>& FRxSkillSystem::LegalTriggers()
{
	static const TArray<FString> Value = { TEXT("committed_ground_propagation") };
	return Value;
}

const TArray<FString>& FRxSkillSystem::LegalEffects()
{
	static const TArray<FString> Value = { TEXT("destabilize_anchor") };
	return Value;
}

// ---------------------------------------------------------------------------
// Data spec -> canonical JSON
// ---------------------------------------------------------------------------

int32 FRxSkillTarget::ResolveRegion() const
{
	if (bHasRegionId)
	{
		return RegionId;
	}
	if (bHasRegion)
	{
		return Region;
	}
	return -1;
}

FRxJsonValue FRxSkillTarget::ToJson() const
{
	FRxJsonValue Obj = FRxJsonValue::Object();
	if (bHasRegionId)
	{
		Obj.Set(TEXT("region_id"), FRxJsonValue::Int(RegionId));
	}
	if (bHasRegion)
	{
		Obj.Set(TEXT("region"), FRxJsonValue::Int(Region));
	}
	return Obj;
}

FRxJsonValue FRxFragmentSpec::ToJson() const
{
	FRxJsonValue Obj = FRxJsonValue::Object();
	if (!bValid)
	{
		return Obj; // empty object mirrors duplicate({})
	}

	// compiler provenance block
	FRxJsonValue Comp = FRxJsonValue::Object();
	{
		FRxJsonValue Checks = FRxJsonValue::Array();
		for (const FString& C : Compiler.ChecksRun)
		{
			Checks.Push(FRxJsonValue::Str(C));
		}
		Comp.Set(TEXT("checks_run"), Checks);
		Comp.Set(TEXT("notes"), FRxJsonValue::Str(Compiler.Notes));
		Comp.Set(TEXT("ref_commit"), FRxJsonValue::Str(Compiler.RefCommit));
		Comp.Set(TEXT("repo"), FRxJsonValue::Str(Compiler.Repo));
		Comp.Set(TEXT("tool"), FRxJsonValue::Str(Compiler.Tool));

		FRxJsonValue Vendored = FRxJsonValue::Object();
		for (const TPair<FString, FString>& Kv : Compiler.VendoredSha)
		{
			Vendored.Set(Kv.Key, FRxJsonValue::Str(Kv.Value));
		}
		Comp.Set(TEXT("vendored_sha"), Vendored);
		Comp.Set(TEXT("version"), FRxJsonValue::Str(Compiler.Version));
	}
	Obj.Set(TEXT("compiler"), Comp);

	Obj.Set(TEXT("counterplay"), FRxJsonValue::Str(Counterplay));
	Obj.Set(TEXT("fragment_hash"), FRxJsonValue::Str(FragmentHash));
	Obj.Set(TEXT("propagation"), FRxJsonValue::Str(Propagation));
	Obj.Set(TEXT("residual_risk"), FRxJsonValue::Str(ResidualRisk));

	FRxJsonValue Domains = FRxJsonValue::Array();
	for (const FString& D : TransferDomains)
	{
		Domains.Push(FRxJsonValue::Str(D));
	}
	Obj.Set(TEXT("transfer_domains"), Domains);

	Obj.Set(TEXT("trigger"), FRxJsonValue::Str(Trigger));
	return Obj;
}

FRxJsonValue FRxSkillArtifact::ToJson(bool bIncludeHash) const
{
	FRxJsonValue Obj = FRxJsonValue::Object();
	if (!bValid)
	{
		return Obj; // empty object mirrors duplicate({})
	}
	// Field set matches the Godot artifact Dictionary. Canonicalization
	// (sorted keys) is applied by FRxCanonJson at hash time.
	Obj.Set(TEXT("skill_id"), FRxJsonValue::Str(SkillId));
	Obj.Set(TEXT("name"), FRxJsonValue::Str(Name));
	Obj.Set(TEXT("derived_from"), FRxJsonValue::Str(DerivedFrom));
	Obj.Set(TEXT("trigger"), FRxJsonValue::Str(Trigger));
	Obj.Set(TEXT("effect"), FRxJsonValue::Str(Effect));
	Obj.Set(TEXT("cost"), FRxJsonValue::Int(Cost));
	Obj.Set(TEXT("cooldown"), FRxJsonValue::Int(Cooldown));
	Obj.Set(TEXT("commit_window"), FRxJsonValue::Int(CommitWindow));
	Obj.Set(TEXT("residual_risk"), FRxJsonValue::Str(ResidualRisk));
	Obj.Set(TEXT("authority"), FRxJsonValue::Str(Authority));
	Obj.Set(TEXT("fragment_hash"), FRxJsonValue::Str(FragmentHash));
	if (bIncludeHash)
	{
		Obj.Set(TEXT("skill_hash"), FRxJsonValue::Str(SkillHash));
	}
	return Obj;
}

// ---------------------------------------------------------------------------
// validate_spec
// ---------------------------------------------------------------------------

FRxSkillResult FRxSkillSystem::ValidateSpec(const FRxSkillSpec& Spec)
{
	if (!LegalNames().Contains(Spec.Name))
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"),
			TEXT("name must be one of [\"FAULTLINE INTERRUPT\"]"));
	}
	if (!LegalTriggers().Contains(Spec.Trigger))
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"),
			TEXT("trigger must be one of [\"committed_ground_propagation\"]"));
	}
	if (!LegalEffects().Contains(Spec.Effect))
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"),
			TEXT("effect must be one of [\"destabilize_anchor\"]"));
	}
	if (Spec.Cost != SkillCost)
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"),
			FString::Printf(TEXT("cost must be %d"), SkillCost));
	}
	if (Spec.Cooldown != SkillCooldown)
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"),
			FString::Printf(TEXT("cooldown must be %d"), SkillCooldown));
	}
	if (Spec.CommitWindow != SkillCommitWindow)
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"),
			FString::Printf(TEXT("commit_window must be %d"), SkillCommitWindow));
	}
	return FRxSkillResult::Ok();
}
// NOTE: ValidateSpec returns bOk/Detail; the Godot original returns {ok,detail}
// (no code). AuthorSkill maps a validation failure to code ERR_STATE, exactly as
// the .gd does ({"ok":false,"code":"ERR_STATE","detail":check["detail"]}).

// ---------------------------------------------------------------------------
// socket_fragment  (single-fragment socket per canon — NOT a skill factory)
// ---------------------------------------------------------------------------

void FRxSkillSystem::SocketFragment(FRxSimWorld& World, const FRxFragmentSpec& Frag)
{
	Fragment = Frag;                 // duplicate(true) equivalent (value copy)
	Fragment.bValid = true;
	World.SetFlag(TEXT("fragment_acquired"), true);

	TSharedRef<FJsonObject> Data = NewObj();
	Data->SetStringField(TEXT("fragment_hash"), Fragment.FragmentHash);
	Data->SetStringField(TEXT("trigger"), Fragment.Trigger);
	World.Emit(TEXT("fragment_socketed"), Data);
}

// ---------------------------------------------------------------------------
// author_skill  (bounded skill from owned fragment; fixed FAULTLINE_INTERRUPT)
// ---------------------------------------------------------------------------

FRxSkillResult FRxSkillSystem::AuthorSkill(FRxSimWorld& World, const FRxSkillSpec& Spec)
{
	if (!World.GetFlag(TEXT("fragment_acquired")))
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"), TEXT("no socketed fragment"));
	}
	const FRxSkillResult Check = ValidateSpec(Spec);
	if (!Check.bOk)
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"), Check.Detail);
	}

	FRxSkillArtifact Artifact;
	Artifact.bValid = true;
	Artifact.SkillId = SkillIdName();
	Artifact.Name = Spec.Name;
	Artifact.DerivedFrom = TEXT("earthquake");
	Artifact.Trigger = Spec.Trigger;
	Artifact.Effect = Spec.Effect;
	Artifact.Cost = SkillCost;
	Artifact.Cooldown = SkillCooldown;
	Artifact.CommitWindow = SkillCommitWindow;
	Artifact.ResidualRisk = ResidualRiskText();
	Artifact.Authority = TEXT("validated_request_only");
	Artifact.FragmentHash = Fragment.FragmentHash;

	// skill_hash is the CanonJson hash of the artifact BEFORE the hash is added.
	Artifact.SkillHash = FRxCanonJson::HashValue(Artifact.ToJson(/*bIncludeHash=*/false));

	AuthoredSkill = Artifact;

	TSharedRef<FJsonObject> Data = NewObj();
	Data->SetStringField(TEXT("name"), Artifact.Name);
	Data->SetStringField(TEXT("skill_hash"), Artifact.SkillHash);
	World.Emit(TEXT("skill_authored"), Data);

	return FRxSkillResult::Ok();
}

// ---------------------------------------------------------------------------
// can_use  (resource/validity gate; surface correctness deliberately NOT checked)
// ---------------------------------------------------------------------------

FRxSkillResult FRxSkillSystem::CanUse(FRxSimWorld& World, int32 /*ActorId*/,
	const FString& SkillId, const FRxSkillTarget& Target) const
{
	if (!AuthoredSkill.bValid)
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"), TEXT("no authored skill"));
	}
	if (SkillId != AuthoredSkill.SkillId)
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"),
			TEXT("unknown skill_id ") + SkillId);
	}
	if (World.GetTick() < Cooldowns.FindRef(SkillId))
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"), TEXT("skill on cooldown"));
	}
	if (Focus < SkillCost)
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"), TEXT("insufficient focus"));
	}
	const int32 Rid = TargetRegion(Target);
	if (Rid == -1 || !World.Terrain().RegionExists(Rid))
	{
		return FRxSkillResult::Err(TEXT("ERR_STATE"), TEXT("unknown target surface"));
	}
	return FRxSkillResult::Ok();
}

// ---------------------------------------------------------------------------
// execute  (routes a validated use_skill command via World.Submit — never
// mutates the world directly)
// ---------------------------------------------------------------------------

FRxSkillResult FRxSkillSystem::Execute(FRxSimWorld& World, int32 ActorId,
	const FString& SkillId, const FRxSkillTarget& Target)
{
	FString Actor = TEXT("player");
	if (World.HasEntity(ActorId))
	{
		Actor = World.EntityKind(ActorId); // defaults to "player" world-side
	}

	// Build the use_skill params object carried by the command envelope
	// (FRxJsonValue — matches FRxCommandEnvelope::Params, hashed by the receipt).
	FRxJsonValue Params = FRxJsonValue::Object();
	Params.Set(TEXT("skill_id"), FRxJsonValue::Str(SkillId));
	Params.Set(TEXT("target"), Target.ToJson());

	FRxCommandEnvelope Envelope;
	Envelope.Tick = World.GetTick();
	Envelope.bHasTick = true;
	Envelope.Actor = Actor;
	Envelope.Type = TEXT("use_skill");
	Envelope.Params = Params;
	Envelope.bApproved = true;
	Envelope.bHasApproved = true;
	Envelope.Authority = TEXT("player-approved");

	const FRxCmdResult R = World.Submit(Envelope);
	return R.bOk ? FRxSkillResult::Ok() : FRxSkillResult::Err(R.Code, R.Detail);
}

// ---------------------------------------------------------------------------
// apply_use  (pay cost/cooldown, resolve correct vs wrong surface)
// ---------------------------------------------------------------------------

FRxSkillResult FRxSkillSystem::ApplyUse(FRxSimWorld& World, int32 ActorId,
	const FRxUseSkillParams& Params, const FRxCommandEnvelope& Envelope)
{
	const FString SkillId = Params.SkillId;
	const FRxSkillTarget& Target = Params.Target;
	const int32 Rid = TargetRegion(Target);

	const FRxSkillResult Gate = CanUse(World, ActorId, SkillId, Target);
	if (!Gate.bOk)
	{
		return FRxSkillResult::Err(Gate.Code, Gate.Detail);
	}

	Focus -= SkillCost;
	Cooldowns.Add(SkillId, World.GetTick() + SkillCooldown);
	LastAction = FString::Printf(TEXT("use_skill:%s@region%d"), *SkillId, Rid);
	LastAuthority = Envelope.Authority;

	const int32 S = World.Terrain().StressOf(Rid);
	if (S >= DampCancel && S < StressThreshold)
	{
		// Correct surface: committed ground-propagation window -> destabilize.
		World.Terrain().Dampen(Rid, SkillDampen);

		TSharedRef<FJsonObject> Data = NewObj();
		Data->SetStringField(TEXT("skill_id"), SkillId);
		Data->SetNumberField(TEXT("region"), static_cast<double>(Rid));
		Data->SetNumberField(TEXT("dampened"), static_cast<double>(SkillDampen));
		Data->SetNumberField(TEXT("actor"), static_cast<double>(ActorId));
		World.Emit(TEXT("skill_executed"), Data);

		// Transfer encounter resolution (SHENRON §8): collapsing exit bridge.
		if (World.GetFlag(TEXT("transfer_active"))
			&& Rid == World.GetTransferRegionId()
			&& !World.GetFlag(TEXT("transfer_resolved")))
		{
			World.SetFlag(TEXT("transfer_resolved"), true);
			World.SetFlag(TEXT("transfer_success"), true);
		}
		return FRxSkillResult::Ok(
			FString::Printf(TEXT("destabilized surface %d"), Rid));
	}

	// Residual risk: wrong surface id amplifies local instability.
	World.Terrain().AddStress(Rid, SkillWrongStress);

	TSharedRef<FJsonObject> BfData = NewObj();
	BfData->SetStringField(TEXT("skill_id"), SkillId);
	BfData->SetNumberField(TEXT("region"), static_cast<double>(Rid));
	BfData->SetNumberField(TEXT("added_stress"), static_cast<double>(SkillWrongStress));
	BfData->SetNumberField(TEXT("actor"), static_cast<double>(ActorId));
	World.Emit(TEXT("skill_backfire"), BfData);

	// Transfer failure path (SHENRON §10.12): backfiring against the transfer
	// region is a meaningfully failed transfer attempt -> failure receipt.
	if (World.GetFlag(TEXT("transfer_active"))
		&& Rid == World.GetTransferRegionId()
		&& !World.GetFlag(TEXT("transfer_resolved")))
	{
		const FString Reason = TEXT("skill backfire amplified exit_bridge instability");
		World.SetFlag(TEXT("transfer_failed"), true);
		World.SetFlag(TEXT("transfer_resolved"), true);
		World.SetFlagString(TEXT("transfer_failure_reason"), Reason);

		TSharedRef<FJsonObject> FailData = NewObj();
		FailData->SetNumberField(TEXT("region"), static_cast<double>(Rid));
		FailData->SetStringField(TEXT("reason"), Reason);
		World.Emit(TEXT("transfer_failed"), FailData);
	}

	return FRxSkillResult::Ok(
		FString::Printf(TEXT("wrong surface: +%d stress on region %d"),
			SkillWrongStress, Rid));
}

// ---------------------------------------------------------------------------
// tick  (focus regen; transfer receipt emission)
// ---------------------------------------------------------------------------

void FRxSkillSystem::Tick(FRxSimWorld& World)
{
	Focus = FMath::Min(FocusMax, Focus + FocusRegen);

	// Transfer receipt: built from ACTUAL events + flags, never static text
	// (CONTRACTS.md §2 Receipts, SHENRON §9). Emitted on success OR on a
	// meaningfully failed transfer attempt (SHENRON §10.12).
	const bool bDone = World.GetFlag(TEXT("transfer_success"))
		|| World.GetFlag(TEXT("transfer_failed"));
	if (bDone && !World.GetFlag(TEXT("receipt_emitted")))
	{
		World.SetFlag(TEXT("receipt_emitted"), true);
		World.Emit(TEXT("transfer_receipt"), BuildTransferReceipt(World));
	}
}

// ---------------------------------------------------------------------------
// build_transfer_receipt  (SHENRON §9 minimum fields, from simulation state)
// ---------------------------------------------------------------------------

TSharedRef<FJsonObject> FRxSkillSystem::BuildTransferReceipt(FRxSimWorld& World) const
{
	TArray<TSharedPtr<FJsonValue>> Evidence;
	if (World.GetFlag(TEXT("evidence_tremor")))
	{
		Evidence.Add(Str(TEXT("precursor vibration")));
	}
	if (World.GetFlag(TEXT("evidence_propagation")))
	{
		Evidence.Add(Str(TEXT("connected-surface propagation")));
	}
	if (World.GetFlag(TEXT("evidence_anchor_failure")))
	{
		Evidence.Add(Str(TEXT("anchor failure")));
	}
	if (World.GetFlag(TEXT("evidence_recovery")))
	{
		Evidence.Add(Str(TEXT("recovery after release")));
	}

	FString Outcome = TEXT("failure: transfer attempt unresolved");
	if (World.GetFlag(TEXT("transfer_success")))
	{
		Outcome = TEXT("success: exit_bridge stabilized (stress dampened below threshold)");
	}
	else if (World.GetFlag(TEXT("transfer_failed")))
	{
		Outcome = TEXT("failure: ")
			+ World.GetFlagString(TEXT("transfer_failure_reason"),
				TEXT("transfer attempt failed"));
	}

	const FString Authority = (!LastAuthority.IsEmpty())
		? LastAuthority : FString(TEXT("player-approved"));

	TSharedRef<FJsonObject> Obj = NewObj();
	Obj->SetStringField(TEXT("source_encounter"), TEXT("EARTHQUAKE_BOSS"));
	Obj->SetArrayField(TEXT("observed_evidence"), Evidence);
	Obj->SetStringField(TEXT("acquired_fragment"), Fragment.FragmentHash);
	Obj->SetStringField(TEXT("authored_skill"), AuthoredSkill.bValid ? AuthoredSkill.Name : FString());
	Obj->SetStringField(TEXT("new_encounter"), TEXT("EXIT_BRIDGE_COLLAPSE"));
	Obj->SetStringField(TEXT("recognized_invariant"),
		TEXT("Accumulated stress propagating through connected structure"));
	Obj->SetStringField(TEXT("action_selected"), LastAction);
	Obj->SetStringField(TEXT("authority"), Authority);
	Obj->SetStringField(TEXT("outcome"), Outcome);
	Obj->SetStringField(TEXT("replay_hash"), World.StateHash());
	return Obj;
}

// ---------------------------------------------------------------------------
// snapshot
// ---------------------------------------------------------------------------

FRxJsonValue FRxSkillSystem::Snapshot() const
{
	// cooldowns -> [[skill_id, tick], ...] sorted by key (deterministic order).
	TArray<FString> Keys;
	Cooldowns.GetKeys(Keys);
	Keys.Sort([](const FString& A, const FString& B)
	{
		const int32 Min = FMath::Min(A.Len(), B.Len());
		const TCHAR* PA = *A;
		const TCHAR* PB = *B;
		for (int32 i = 0; i < Min; ++i)
		{
			if (PA[i] != PB[i])
			{
				return static_cast<uint32>(PA[i]) < static_cast<uint32>(PB[i]);
			}
		}
		return A.Len() < B.Len();
	});

	FRxJsonValue Cd = FRxJsonValue::Array();
	for (const FString& K : Keys)
	{
		FRxJsonValue Row = FRxJsonValue::Array();
		Row.Push(FRxJsonValue::Str(K));
		Row.Push(FRxJsonValue::Int(Cooldowns.FindRef(K)));
		Cd.Push(Row);
	}

	FRxJsonValue Obj = FRxJsonValue::Object();
	Obj.Set(TEXT("fragment"), Fragment.ToJson());
	Obj.Set(TEXT("authored_skill"), AuthoredSkill.ToJson(/*bIncludeHash=*/true));
	Obj.Set(TEXT("cooldowns"), Cd);
	Obj.Set(TEXT("focus"), FRxJsonValue::Int(Focus));
	Obj.Set(TEXT("last_action"), FRxJsonValue::Str(LastAction));
	Obj.Set(TEXT("last_authority"), FRxJsonValue::Str(LastAuthority));
	return Obj;
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

int32 FRxSkillSystem::TargetRegion(const FRxSkillTarget& Target)
{
	return Target.ResolveRegion();
}

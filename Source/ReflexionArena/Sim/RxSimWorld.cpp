#include "RxSimWorld.h"

#include "RxCompanionAI.h"  // full type: TUniquePtr dtor + AiTick/ReceiveInstruction/OnApproved/OnCancelled
#include "RxEncounters.h"   // FRxEncounters::BuildTransfer (transfer auto-trigger)

#include "Dom/JsonObject.h" // FJsonObject event payloads (Emit overload)
#include "Dom/JsonValue.h"

/**
 * RxSimWorld.cpp — implementation mirroring sim_world.gd step()/_apply/snapshot/
 * state_hash EXACTLY. Every hashed structure is an FRxJsonValue built here and
 * fed through FRxCanonJson (CONTRACTS.md §0). Events are FRxEvent (unhashed).
 */

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------
namespace
{
	using EJsonT = ERxJsonType;

	// Mirror of GDScript int(v) for the closed canonical value set (see commands.cpp).
	int64 ToInt64(const FRxJsonValue& V)
	{
		switch (V.Type)
		{
		case EJsonT::Int:    return V.IntValue;
		case EJsonT::String: return FCString::Atoi64(*V.StringValue);
		case EJsonT::Bool:   return V.bValue ? 1 : 0;
		default:             return 0;
		}
	}

	// params.get(primary, params.get(secondary, Default)) then int(): prefer the
	// primary KEY if present (any type, coerced), else secondary, else Default.
	int64 GetIntFallback(const FRxJsonValue& P, const TCHAR* Primary, const TCHAR* Secondary, int64 Default)
	{
		if (const FRxJsonValue* V = P.Find(Primary))   { return ToInt64(*V); }
		if (const FRxJsonValue* V = P.Find(Secondary)) { return ToInt64(*V); }
		return Default;
	}

	bool IsIntKey(const FRxJsonValue& P, const TCHAR* Key)
	{
		const FRxJsonValue* V = P.Find(Key);
		return V != nullptr && V->Type == EJsonT::Int;
	}

	// Recursive FJsonValue -> FRxJsonValue (events only; never hashed). Numbers are
	// integral in this sim (SetNumberField(double(int))), so int64 cast is exact.
	FRxJsonValue JsonToRx(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid())
		{
			return FRxJsonValue::Int(0);
		}
		switch (V->Type)
		{
		case EJson::Boolean:
			return FRxJsonValue::Bool(V->AsBool());
		case EJson::Number:
			return FRxJsonValue::Int(static_cast<int64>(V->AsNumber()));
		case EJson::String:
			return FRxJsonValue::Str(V->AsString());
		case EJson::Array:
		{
			FRxJsonValue Arr = FRxJsonValue::Array();
			for (const TSharedPtr<FJsonValue>& Item : V->AsArray())
			{
				Arr.Push(JsonToRx(Item));
			}
			return Arr;
		}
		case EJson::Object:
		{
			FRxJsonValue Obj = FRxJsonValue::Object();
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (O.IsValid())
			{
				for (const auto& Kv : O->Values)
				{
					Obj.Set(FString(Kv.Key), JsonToRx(Kv.Value));
				}
			}
			return Obj;
		}
		default:
			return FRxJsonValue::Int(0);
		}
	}

	FRxJsonValue JsonObjToRx(const TSharedRef<FJsonObject>& O)
	{
		FRxJsonValue Obj = FRxJsonValue::Object();
		for (const auto& Kv : O->Values)
		{
			Obj.Set(FString(Kv.Key), JsonToRx(Kv.Value));
		}
		return Obj;
	}

	// FRxJsonValue Object -> FRxFragmentSpec (round-trips the compiled fragment so
	// skills.snapshot() rehashes identically). Mirrors frag.duplicate(true).
	FRxFragmentSpec ParseFragment(const FRxJsonValue* F)
	{
		FRxFragmentSpec Frag;
		if (F == nullptr || F->Type != EJsonT::Object)
		{
			return Frag; // bValid = false (mirrors empty Dictionary)
		}
		Frag.bValid = true;
		if (const FRxJsonValue* C = F->Find(TEXT("compiler")))
		{
			if (const FRxJsonValue* Cr = C->Find(TEXT("checks_run")))
			{
				if (Cr->Type == EJsonT::Array)
				{
					for (const FRxJsonValue& Item : Cr->ArrayItems)
					{
						Frag.Compiler.ChecksRun.Add(Item.StringValue);
					}
				}
			}
			Frag.Compiler.Notes = C->GetString(TEXT("notes"));
			Frag.Compiler.RefCommit = C->GetString(TEXT("ref_commit"));
			Frag.Compiler.Repo = C->GetString(TEXT("repo"));
			Frag.Compiler.Tool = C->GetString(TEXT("tool"));
			if (const FRxJsonValue* Vs = C->Find(TEXT("vendored_sha")))
			{
				if (Vs->Type == EJsonT::Object)
				{
					for (const TPair<FString, FRxJsonValue>& Kv : Vs->ObjectItems)
					{
						Frag.Compiler.VendoredSha.Add(Kv.Key, Kv.Value.StringValue);
					}
				}
			}
			Frag.Compiler.Version = C->GetString(TEXT("version"));
		}
		Frag.Counterplay = F->GetString(TEXT("counterplay"));
		Frag.FragmentHash = F->GetString(TEXT("fragment_hash"));
		Frag.Propagation = F->GetString(TEXT("propagation"));
		Frag.ResidualRisk = F->GetString(TEXT("residual_risk"));
		if (const FRxJsonValue* D = F->Find(TEXT("transfer_domains")))
		{
			if (D->Type == EJsonT::Array)
			{
				for (const FRxJsonValue& Item : D->ArrayItems)
				{
					Frag.TransferDomains.Add(Item.StringValue);
				}
			}
		}
		Frag.Trigger = F->GetString(TEXT("trigger"));
		return Frag;
	}

	FRxSkillSpec ParseSkillSpec(const FRxJsonValue* S)
	{
		FRxSkillSpec Spec;
		if (S != nullptr && S->Type == EJsonT::Object)
		{
			Spec.Name = S->GetString(TEXT("name"));
			Spec.Trigger = S->GetString(TEXT("trigger"));
			Spec.Effect = S->GetString(TEXT("effect"));
			Spec.Cost = static_cast<int32>(S->GetInt(TEXT("cost"), -1));
			Spec.Cooldown = static_cast<int32>(S->GetInt(TEXT("cooldown"), -1));
			Spec.CommitWindow = static_cast<int32>(S->GetInt(TEXT("commit_window"), -1));
		}
		return Spec;
	}
}

// ---------------------------------------------------------------------------
// Construction (_init)
// ---------------------------------------------------------------------------
FRxSimWorld::FRxSimWorld(uint64 Seed)
	: Rng(Seed)
{
	// flags default set (mirrors the 11 bool keys the .gd initialises), so they
	// are ALWAYS present in the snapshot from tick 0 (hash parity).
	static const TCHAR* InitialFlags[] = {
		TEXT("fragment_acquired"), TEXT("correction_made"), TEXT("transfer_recognized"),
		TEXT("weave_interrupted"), TEXT("weave_was_interrupted"), TEXT("boss_defeated"),
		TEXT("transfer_active"), TEXT("transfer_resolved"), TEXT("transfer_success"),
		TEXT("transfer_failed"), TEXT("receipt_emitted"),
	};
	for (const TCHAR* Key : InitialFlags)
	{
		Flags.Add(FString(Key), false);
	}
}

FRxSimWorld::~FRxSimWorld() = default;

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------
FRxEntityId FRxSimWorld::SpawnEntity(const FString& Kind, const FIntPoint& Pos)
{
	const FRxEntityId Id = NextEntityId;
	NextEntityId += 1;

	int32 Hp = RxSim::PLAYER_HP;
	if (Kind == RxKind::Companion)
	{
		Hp = RxSim::COMPANION_HP;
	}
	else if (Kind == RxKind::Boss)
	{
		Hp = 300; // mirrors BossEarthquake.stability; synced via SyncBossEntity
	}
	else if (Kind == RxKind::Structure)
	{
		Hp = 100;
	}

	FRxEntity E;
	E.Id = Id;
	E.Kind = Kind;
	E.Pos = Pos;
	E.Hp = Hp;
	E.MaxHp = Hp;
	E.State = RxState::Idle;

	Entities.Add(Id, E);
	EntityOrder.Add(Id);
	return Id;
}

int32 FRxSimWorld::EntityIdForActor(const FString& Actor) const
{
	if (Actor == RxActor::Player)
	{
		if (PlayerId != -1)
		{
			return PlayerId;
		}
	}
	else if (Actor == RxActor::Companion)
	{
		if (CompanionId != -1)
		{
			return CompanionId;
		}
	}
	for (const FRxEntityId Id : EntityOrder)
	{
		if (Entities[Id].Kind == Actor)
		{
			return Id;
		}
	}
	return -1;
}

FIntPoint FRxSimWorld::GetEntityPos(FRxEntityId Id) const
{
	const FRxEntity* E = Entities.Find(Id);
	return E != nullptr ? E->Pos : FIntPoint::ZeroValue;
}

FString FRxSimWorld::EntityKind(FRxEntityId Id) const
{
	const FRxEntity* E = Entities.Find(Id);
	return E != nullptr ? E->Kind : FString(RxKind::Player);
}

void FRxSimWorld::SyncBossEntity()
{
	if (BossId != -1)
	{
		if (FRxEntity* E = Entities.Find(BossId))
		{
			E->Hp = FMath::Max(0, Boss.Stability);
			E->State = Boss.GetStateName();
		}
	}
}

void FRxSimWorld::AttachCompanion(FRxEntityId InCompanionId, FRxEntityId InPlayerId)
{
	CompanionAI = MakeUnique<FRxCompanionAI>();
	CompanionAI->Bind(InCompanionId, InPlayerId);
}

// ---------------------------------------------------------------------------
// Flags / cooldown / distance helpers
// ---------------------------------------------------------------------------
bool FRxSimWorld::GetFlag(const FString& Key, bool bDefault) const
{
	const bool* B = Flags.Find(Key);
	return B != nullptr ? *B : bDefault;
}

void FRxSimWorld::SetFlag(const FString& Key, bool bValue)
{
	Flags.Add(Key, bValue);
}

FString FRxSimWorld::GetFlagString(const FString& Key, const FString& Default) const
{
	const FString* S = FlagStrings.Find(Key);
	return S != nullptr ? *S : Default;
}

void FRxSimWorld::SetFlagString(const FString& Key, const FString& Value)
{
	FlagStrings.Add(Key, Value);
}

int64 FRxSimWorld::ISqrt(int64 N)
{
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

int32 FRxSimWorld::Idist(const FIntPoint& A, const FIntPoint& B) const
{
	const int64 Dx = static_cast<int64>(B.X) - A.X;
	const int64 Dy = static_cast<int64>(B.Y) - A.Y;
	return static_cast<int32>(ISqrt(Dx * Dx + Dy * Dy));
}

bool FRxSimWorld::StrikeCooldownOk(FRxEntityId AttackerId, const FString& TargetKey) const
{
	const FString K = FString::Printf(TEXT("%d|%s"), AttackerId, *TargetKey);
	const int32* Last = StrikeCd.Find(K);
	const int32 LastTick = Last != nullptr ? *Last : -RxSim::STRIKE_COOLDOWN;
	return Tick - LastTick >= RxSim::STRIKE_COOLDOWN;
}

void FRxSimWorld::MarkStrike(FRxEntityId AttackerId, const FString& TargetKey)
{
	StrikeCd.Add(FString::Printf(TEXT("%d|%s"), AttackerId, *TargetKey), Tick);
}

void FRxSimWorld::QueueRelease(int32 Origin, int32 Force)
{
	ScheduleRelease(TerrainImpl.ForceRelease(Origin, Force));
}

// ---------------------------------------------------------------------------
// Submit (companion path: canonical JSON envelope)
// ---------------------------------------------------------------------------
FRxCmdResult FRxSimWorld::Submit(const FRxJsonValue& Command)
{
	using namespace RxCmdDetail;

	if (Command.Type != ERxJsonType::Object)
	{
		return FRxCmdResult::Err(RxCode::ErrMalformed, EnvelopeNotDict);
	}

	FRxJsonValue C = Command; // duplicate(true) equivalent

	// Runtime-generated envelopes may omit tick/seq — assigned deterministically.
	if (!C.HasKey(TEXT("tick")))
	{
		C.Set(TEXT("tick"), FRxJsonValue::Int(Tick));
	}
	if (!C.HasKey(TEXT("seq")))
	{
		C.Set(TEXT("seq"), FRxJsonValue::Int(RuntimeSeq));
		RuntimeSeq += 1;
	}

	// JSON -> typed envelope, hoisting the TYPE-level malformed checks the typed
	// FRxCommandEnvelope cannot express (see RxCommands.h note).
	FRxCommandEnvelope Env;
	if (const FRxJsonValue* V = C.Find(TEXT("seq")))
	{
		if (V->Type != ERxJsonType::Int)
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, SeqNotInt);
		}
		Env.Seq = static_cast<int32>(V->IntValue);
		Env.bHasSeq = true;
	}
	if (const FRxJsonValue* V = C.Find(TEXT("tick")))
	{
		if (V->Type != ERxJsonType::Int)
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, TickNotNonNegInt);
		}
		Env.Tick = static_cast<int32>(V->IntValue);
		Env.bHasTick = true;
	}
	if (const FRxJsonValue* V = C.Find(TEXT("approved")))
	{
		if (V->Type != ERxJsonType::Bool)
		{
			return FRxCmdResult::Err(RxCode::ErrMalformed, ApprovedNotBool);
		}
		Env.bApproved = V->bValue;
		Env.bHasApproved = true;
	}
	Env.Actor = C.GetString(TEXT("actor"));
	Env.Type = C.GetString(TEXT("type"));
	if (const FRxJsonValue* P = C.Find(TEXT("params")))
	{
		Env.Params = *P; // non-Object caught by Validate (ParamsNotDict)
	}
	Env.Authority = C.GetString(TEXT("authority"));

	return SubmitEnvelopeInternal(MoveTemp(Env), MoveTemp(C));
}

// ---------------------------------------------------------------------------
// Submit (skills path: typed envelope)
// ---------------------------------------------------------------------------
FRxCmdResult FRxSimWorld::Submit(const FRxCommandEnvelope& InEnv)
{
	FRxCommandEnvelope Env = InEnv;
	if (!Env.bHasTick)
	{
		Env.Tick = Tick;
		Env.bHasTick = true;
	}
	if (!Env.bHasSeq)
	{
		Env.Seq = RuntimeSeq;
		Env.bHasSeq = true;
		RuntimeSeq += 1;
	}
	FRxJsonValue Json = EnvelopeToJson(Env);
	return SubmitEnvelopeInternal(MoveTemp(Env), MoveTemp(Json));
}

FRxCmdResult FRxSimWorld::SubmitEnvelopeInternal(FRxCommandEnvelope Env, FRxJsonValue Json)
{
	const FRxCmdResult V = FRxCommands::Validate(Env, this);
	if (V.bOk)
	{
		FRxPending P;
		P.Seq = Env.Seq;
		P.Env = MoveTemp(Env);
		P.Json = MoveTemp(Json);
		Pending.Add(MoveTemp(P));
	}
	return V;
}

FRxJsonValue FRxSimWorld::EnvelopeToJson(const FRxCommandEnvelope& Env) const
{
	FRxJsonValue J = FRxJsonValue::Object();
	J.Set(TEXT("actor"), FRxJsonValue::Str(Env.Actor));
	J.Set(TEXT("type"), FRxJsonValue::Str(Env.Type));
	J.Set(TEXT("params"), Env.Params);
	if (Env.bHasTick)
	{
		J.Set(TEXT("tick"), FRxJsonValue::Int(Env.Tick));
	}
	if (Env.bHasSeq)
	{
		J.Set(TEXT("seq"), FRxJsonValue::Int(Env.Seq));
	}
	if (Env.bHasApproved)
	{
		J.Set(TEXT("approved"), FRxJsonValue::Bool(Env.bApproved));
	}
	if (!Env.Authority.IsEmpty())
	{
		J.Set(TEXT("authority"), FRxJsonValue::Str(Env.Authority));
	}
	return J;
}

// ---------------------------------------------------------------------------
// Emit (three producer styles -> one FRxEvent store)
// ---------------------------------------------------------------------------
void FRxSimWorld::Emit(const FString& Type, const FRxJsonValue& Data)
{
	FRxEvent Ev;
	Ev.Tick = Tick;
	Ev.Type = Type;
	Ev.Data = Data;
	Events.Add(MoveTemp(Ev));
}

void FRxSimWorld::Emit(const FString& Type, const TSharedRef<FJsonObject>& Data)
{
	FRxEvent Ev;
	Ev.Tick = Tick;
	Ev.Type = Type;
	Ev.Data = JsonObjToRx(Data);
	Events.Add(MoveTemp(Ev));
}

// --- typed boss emit wrappers (payloads verbatim from boss_earthquake.gd) ---
void FRxSimWorld::EmitBossTelegraph(const FString& Word)
{
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("word"), FRxJsonValue::Str(Word));
	Emit(TEXT("boss_telegraph"), D);
}

void FRxSimWorld::EmitTremor(int32 Region, int32 Pct, int32 Stress)
{
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("region"), FRxJsonValue::Int(Region));
	D.Set(TEXT("pct"), FRxJsonValue::Int(Pct));
	D.Set(TEXT("stress"), FRxJsonValue::Int(Stress));
	Emit(TEXT("tremor"), D);
}

void FRxSimWorld::EmitBossDestabilized(int32 Window)
{
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("window"), FRxJsonValue::Int(Window));
	Emit(TEXT("boss_destabilized"), D);
}

void FRxSimWorld::EmitBossRecover(int32 AftershockIn)
{
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("aftershock_in"), FRxJsonValue::Int(AftershockIn));
	Emit(TEXT("boss_recover"), D);
}

void FRxSimWorld::EmitAftershock(int32 Force, int32 Origin)
{
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("force"), FRxJsonValue::Int(Force));
	D.Set(TEXT("origin"), FRxJsonValue::Int(Origin));
	Emit(TEXT("aftershock"), D);
}

void FRxSimWorld::EmitBossStruck(int32 Attacker, int32 Dmg, int32 Stability, const FString& State)
{
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("attacker"), FRxJsonValue::Int(Attacker));
	D.Set(TEXT("dmg"), FRxJsonValue::Int(Dmg));
	D.Set(TEXT("stability"), FRxJsonValue::Int(Stability));
	D.Set(TEXT("state"), FRxJsonValue::Str(State));
	Emit(TEXT("boss_struck"), D);
}

void FRxSimWorld::EmitBossDefeated(int32 Stability)
{
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("stability"), FRxJsonValue::Int(Stability));
	Emit(TEXT("boss_defeated"), D);
}

void FRxSimWorld::EmitAnchorStruck(int32 Region, int32 ReleaseDelay)
{
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("region"), FRxJsonValue::Int(Region));
	D.Set(TEXT("release_delay"), FRxJsonValue::Int(ReleaseDelay));
	Emit(TEXT("anchor_struck"), D);
}

// ---------------------------------------------------------------------------
// step() — EXACT sim_world.gd contract order
// ---------------------------------------------------------------------------
void FRxSimWorld::Step()
{
	Tick += 1;
	Upkeep();

	// apply pending commands by seq (stable: seqs are unique)
	Pending.Sort([](const FRxPending& A, const FRxPending& B) { return A.Seq < B.Seq; });
	TArray<FRxPending> Applied = MoveTemp(Pending);
	Pending.Reset();

	TArray<FRxCmdResult> Results;
	Results.Reserve(Applied.Num());
	for (const FRxPending& Cmd : Applied)
	{
		Results.Add(Apply(Cmd));
	}

	// boss (created in _init -> always present; AnchorRegion==-1 guards inside)
	Boss.AiTick(*this);
	SyncBossEntity();

	// companion (if set by encounters setup)
	if (CompanionAI.IsValid())
	{
		CompanionAI->AiTick(*this);
	}

	// transfer stress source (collapsing exit bridge)
	if (GetFlag(TEXT("transfer_active")) && !GetFlag(TEXT("transfer_resolved")))
	{
		TerrainImpl.AddStress(TransferRegionId, RxSim::TRANSFER_STRESS_RATE);
	}

	// terrain diffusion + threshold releases -> delayed propagation waves
	for (const FRxRelease& Rel : TerrainImpl.Tick())
	{
		ScheduleRelease(Rel);
		// Transfer failure path: exit_bridge collapsed before the skill resolved it.
		if (GetFlag(TEXT("transfer_active")) && !GetFlag(TEXT("transfer_resolved"))
			&& Rel.Origin == TransferRegionId)
		{
			const FString Reason = TEXT("exit_bridge collapsed under accumulated stress");
			SetFlag(TEXT("transfer_failed"), true);
			SetFlag(TEXT("transfer_resolved"), true);
			SetFlagString(TEXT("transfer_failure_reason"), Reason);
			FRxJsonValue D = FRxJsonValue::Object();
			D.Set(TEXT("region"), FRxJsonValue::Int(TransferRegionId));
			D.Set(TEXT("reason"), FRxJsonValue::Str(Reason));
			Emit(TEXT("transfer_failed"), D);
		}
	}

	// due wave entries
	TArray<FRxScheduledWave> Due;
	TArray<FRxScheduledWave> Keep;
	for (const FRxScheduledWave& W : ScheduledWaves)
	{
		if (W.ApplyTick <= Tick)
		{
			Due.Add(W);
		}
		else
		{
			Keep.Add(W);
		}
	}
	ScheduledWaves = MoveTemp(Keep);
	for (const FRxScheduledWave& W : Due)
	{
		ApplyWaveEntry(W);
	}

	// transfer auto-trigger: fragment socketed + player stands on exit_bridge
	if (GetFlag(TEXT("fragment_acquired")) && !GetFlag(TEXT("transfer_active")))
	{
		if (PlayerId != -1)
		{
			if (const FRxEntity* PE = Entities.Find(PlayerId))
			{
				if (TerrainImpl.RegionAt(PE->Pos) == TransferRegionId)
				{
					FRxEncounters::BuildTransfer(*this);
				}
			}
		}
	}

	// skills
	SkillsImpl.Tick(*this);

	// receipt seal: one hash-chained receipt per applied command (contract order)
	for (int32 i = 0; i < Applied.Num(); ++i)
	{
		Receipts.Record(Applied[i].Json, ResultToJson(Results[i]), StateHash());
	}
}

// ---------------------------------------------------------------------------
// _upkeep — movement glide (entity-id order) + deferred weave aborts
// ---------------------------------------------------------------------------
void FRxSimWorld::Upkeep()
{
	for (const FRxEntityId Id : EntityOrder)
	{
		FRxEntity& E = Entities[Id];

		// Deferred weave-abort finalization.
		if (E.State == RxState::Weaving && E.bHasWeaveAbort)
		{
			if (Tick >= E.WeaveAbortTick + 2)
			{
				E.State = RxState::Idle;
				E.bHasWeaveAbort = false; // erase weave_abort_tick
				E.WeaveRegionId = -1;     // erase weave_region_id
				E.WeaveMode = FString();  // erase weave_mode
				// weave_start_tick intentionally NOT erased (mirrors the .gd)
			}
			continue; // rooted while weaving
		}

		// Sim-internal T3 weave completion.
		if (E.State == RxState::Weaving)
		{
			if (Tick - E.WeaveStartTick >= RxSim::WEAVE_DURATION_TICKS)
			{
				const int32 Rid = E.WeaveRegionId;
				const FString Mode = E.WeaveMode.IsEmpty() ? FString(TEXT("anchor")) : E.WeaveMode;
				if (Rid != -1 && TerrainImpl.RegionExists(Rid))
				{
					if (Mode == TEXT("anchor"))
					{
						TerrainImpl.Anchor(Rid, E.Id);
					}
					TerrainImpl.Dampen(Rid, RxSim::WEAVE_DAMPEN);
				}
				E.State = RxState::Idle;
				EraseWeaveProps(E);
				FRxJsonValue D = FRxJsonValue::Object();
				D.Set(TEXT("entity_id"), FRxJsonValue::Int(E.Id));
				D.Set(TEXT("region_id"), FRxJsonValue::Int(Rid));
				D.Set(TEXT("mode"), FRxJsonValue::Str(Mode));
				Emit(TEXT("weave_completed"), D);
			}
			continue; // rooted while weaving
		}

		// Movement glide toward move_target.
		if (E.bHasMoveTarget)
		{
			const int64 Dx = static_cast<int64>(E.MoveTarget.X) - E.Pos.X;
			const int64 Dy = static_cast<int64>(E.MoveTarget.Y) - E.Pos.Y;
			const int64 D = ISqrt(Dx * Dx + Dy * Dy);
			if (D <= RxSim::MOVE_SPEED)
			{
				E.Pos = E.MoveTarget;
				E.bHasMoveTarget = false;
			}
			else
			{
				E.Pos = FIntPoint(
					E.Pos.X + static_cast<int32>(Dx * RxSim::MOVE_SPEED / D),
					E.Pos.Y + static_cast<int32>(Dy * RxSim::MOVE_SPEED / D));
			}
		}
	}
}

void FRxSimWorld::EraseWeaveProps(FRxEntity& E)
{
	E.bHasWeaveAbort = false;
	E.WeaveRegionId = -1;
	E.WeaveMode = FString();
	E.WeaveStartTick = -1;
}

// ---------------------------------------------------------------------------
// _apply — command dispatch (mirrors the .gd match)
// ---------------------------------------------------------------------------
FRxCmdResult FRxSimWorld::Apply(const FRxPending& P)
{
	const FString& Type = P.Env.Type;
	const FRxJsonValue& Params = P.Env.Params;
	const FString& Actor = P.Env.Actor;
	const int32 Eid = EntityIdForActor(Actor);

	if (Type == RxCmd::MoveTo)
	{
		if (Eid == -1 || !HasEntity(Eid))
		{
			return FRxCmdResult::Err(RxCode::ErrState, FString(TEXT("no entity for actor ")) + Actor);
		}
		FRxEntity& E = Entities[Eid];
		E.MoveTarget = FIntPoint(static_cast<int32>(Params.GetInt(TEXT("x"))),
			static_cast<int32>(Params.GetInt(TEXT("y"))));
		E.bHasMoveTarget = true;
		return FRxCmdResult::Ok();
	}
	if (Type == RxCmd::Reference)
	{
		if (const FRxJsonValue* T = Params.Find(TEXT("target")))
		{
			LastReference = *T;
		}
		else
		{
			LastReference = FRxJsonValue::Object();
		}
		FRxJsonValue D = FRxJsonValue::Object();
		D.Set(TEXT("actor"), FRxJsonValue::Str(Actor));
		D.Set(TEXT("target"), LastReference);
		Emit(TEXT("reference"), D);
		return FRxCmdResult::Ok();
	}
	if (Type == RxCmd::Instruct)
	{
		FRxJsonValue Ref = LastReference;
		if (const FRxJsonValue* R = Params.Find(TEXT("reference")))
		{
			Ref = *R;
		}
		if (Ref.Type != ERxJsonType::Object)
		{
			Ref = FRxJsonValue::Object();
		}
		FRxInstructionResult R = CompanionAI->ReceiveInstruction(*this, Params.GetString(TEXT("text")), Ref);
		FRxJsonValue Plan = FRxJsonValue::Object();
		FRxJsonValue Steps = FRxJsonValue::Array();
		for (const FRxJsonValue& S : R.PlanSteps)
		{
			Steps.Push(S);
		}
		Plan.Set(TEXT("steps"), Steps);
		FRxJsonValue D = FRxJsonValue::Object();
		D.Set(TEXT("echo"), FRxJsonValue::Str(R.Echo));
		D.Set(TEXT("plan"), Plan);
		D.Set(TEXT("needs_approval"), FRxJsonValue::Bool(R.bNeedsApproval));
		Emit(TEXT("companion_plan"), D);
		return FRxCmdResult{ true, FString(RxCode::Ok), R.Echo };
	}
	if (Type == RxCmd::Correct)
	{
		FRxJsonValue Ref = LastReference;
		if (const FRxJsonValue* R = Params.Find(TEXT("reference")))
		{
			Ref = *R;
		}
		if (Ref.Type != ERxJsonType::Object)
		{
			Ref = FRxJsonValue::Object();
		}
		FRxInstructionResult R = CompanionAI->ReceiveInstruction(*this, Params.GetString(TEXT("text")), Ref);
		if (R.bOk)
		{
			SetFlag(TEXT("correction_made"), true);
		}
		FRxJsonValue Plan = FRxJsonValue::Object();
		FRxJsonValue Steps = FRxJsonValue::Array();
		for (const FRxJsonValue& S : R.PlanSteps)
		{
			Steps.Push(S);
		}
		Plan.Set(TEXT("steps"), Steps);
		FRxJsonValue D = FRxJsonValue::Object();
		D.Set(TEXT("echo"), FRxJsonValue::Str(R.Echo));
		D.Set(TEXT("plan"), Plan);
		D.Set(TEXT("needs_approval"), FRxJsonValue::Bool(R.bNeedsApproval));
		Emit(TEXT("companion_plan"), D);
		return FRxCmdResult{ true, FString(RxCode::Ok), R.Echo };
	}
	if (Type == RxCmd::Approve)
	{
		CompanionAI->OnApproved(*this);
		FRxJsonValue D = FRxJsonValue::Object();
		D.Set(TEXT("actor"), FRxJsonValue::Str(Actor));
		Emit(TEXT("approved"), D);
		return FRxCmdResult::Ok();
	}
	if (Type == RxCmd::Cancel)
	{
		CompanionAI->OnCancelled(*this);
		FRxJsonValue D = FRxJsonValue::Object();
		D.Set(TEXT("actor"), FRxJsonValue::Str(Actor));
		Emit(TEXT("plan_cancelled"), D);
		return FRxCmdResult::Ok();
	}
	if (Type == RxCmd::Strike)
	{
		return ApplyStrike(Eid, Params);
	}
	if (Type == RxCmd::TokenweaveBegin)
	{
		if (Eid == -1)
		{
			return FRxCmdResult::Err(RxCode::ErrState, TEXT("no entity to weave"));
		}
		FRxEntity& E = Entities[Eid];
		int32 Rid = static_cast<int32>(Params.GetInt(TEXT("region_id"), -1));
		if (Rid == -1)
		{
			Rid = TerrainImpl.RegionAt(E.Pos);
		}
		const FString Mode = Params.GetString(TEXT("mode"), TEXT("anchor"));
		E.State = RxState::Weaving;
		E.WeaveRegionId = Rid;
		E.WeaveMode = Mode;
		E.WeaveStartTick = Tick;
		E.bHasMoveTarget = false; // erase move_target
		SetFlag(TEXT("weave_interrupted"), false);
		FRxJsonValue D = FRxJsonValue::Object();
		D.Set(TEXT("entity_id"), FRxJsonValue::Int(Eid));
		D.Set(TEXT("region_id"), FRxJsonValue::Int(Rid));
		D.Set(TEXT("mode"), FRxJsonValue::Str(Mode));
		Emit(TEXT("weave_begin"), D);
		return FRxCmdResult::Ok();
	}
	if (Type == RxCmd::UseSkill)
	{
		FRxUseSkillParams UP;
		UP.SkillId = Params.GetString(TEXT("skill_id"));
		if (const FRxJsonValue* T = Params.Find(TEXT("target")))
		{
			if (const FRxJsonValue* R = T->Find(TEXT("region_id")))
			{
				if (R->Type == ERxJsonType::Int)
				{
					UP.Target.bHasRegionId = true;
					UP.Target.RegionId = static_cast<int32>(R->IntValue);
				}
			}
			if (const FRxJsonValue* R = T->Find(TEXT("region")))
			{
				if (R->Type == ERxJsonType::Int)
				{
					UP.Target.bHasRegion = true;
					UP.Target.Region = static_cast<int32>(R->IntValue);
				}
			}
		}
		const FRxSkillResult SR = SkillsImpl.ApplyUse(*this, Eid, UP, P.Env);
		return FRxCmdResult{ SR.bOk, SR.Code, SR.Detail };
	}
	if (Type == RxCmd::SocketFragment)
	{
		const FRxFragmentSpec Frag = ParseFragment(Params.Find(TEXT("fragment")));
		SkillsImpl.SocketFragment(*this, Frag);
		return FRxCmdResult::Ok();
	}
	if (Type == RxCmd::AuthorSkill)
	{
		const FRxSkillSpec Spec = ParseSkillSpec(Params.Find(TEXT("spec")));
		const FRxSkillResult AR = SkillsImpl.AuthorSkill(*this, Spec);
		return FRxCmdResult{ AR.bOk, AR.Code, AR.Detail };
	}
	if (Type == RxCmd::Wait)
	{
		return FRxCmdResult::Ok();
	}
	return FRxCmdResult::Err(RxCode::ErrUnknownType, FString(TEXT("unhandled command type ")) + Type);
}

FRxCmdResult FRxSimWorld::ApplyStrike(int32 AttackerId, const FRxJsonValue& Params)
{
	if (AttackerId == -1 || !HasEntity(AttackerId))
	{
		return FRxCmdResult::Err(RxCode::ErrState, TEXT("no striker entity"));
	}

	FString TargetKey;
	const bool bEntityTarget = IsIntKey(Params, TEXT("target_id")) || IsIntKey(Params, TEXT("target_entity_id"));
	if (bEntityTarget)
	{
		const int32 Tid = static_cast<int32>(
			GetIntFallback(Params, TEXT("target_id"), TEXT("target_entity_id"), -1));
		if (!HasEntity(Tid))
		{
			return FRxCmdResult::Err(RxCode::ErrState, TEXT("strike target missing"));
		}
		TargetKey = FString::Printf(TEXT("e%d"), Tid);
		MarkStrike(AttackerId, TargetKey);
		FRxEntity& Target = Entities[Tid];
		if (Target.Kind == RxKind::Boss)
		{
			Boss.TakeStrike(*this, AttackerId);
		}
		else
		{
			Target.Hp -= RxSim::STRIKE_DAMAGE;
			FRxJsonValue D = FRxJsonValue::Object();
			D.Set(TEXT("attacker"), FRxJsonValue::Int(AttackerId));
			D.Set(TEXT("target"), FRxJsonValue::Int(Tid));
			D.Set(TEXT("hp"), FRxJsonValue::Int(Target.Hp));
			Emit(TEXT("entity_struck"), D);
		}
	}
	else
	{
		const int32 Rid = static_cast<int32>(
			GetIntFallback(Params, TEXT("region"), TEXT("region_id"), -1));
		TargetKey = FString::Printf(TEXT("r%d"), Rid);
		MarkStrike(AttackerId, TargetKey);
		TerrainImpl.Dampen(Rid, RxSim::STRIKE_DAMPEN);
		FRxJsonValue D = FRxJsonValue::Object();
		D.Set(TEXT("attacker"), FRxJsonValue::Int(AttackerId));
		D.Set(TEXT("region"), FRxJsonValue::Int(Rid));
		D.Set(TEXT("dampened"), FRxJsonValue::Int(RxSim::STRIKE_DAMPEN));
		D.Set(TEXT("stress"), FRxJsonValue::Int(TerrainImpl.StressOf(Rid)));
		Emit(TEXT("region_struck"), D);
		if (Rid == Boss.AnchorRegion)
		{
			Boss.OnAnchorStruck(*this);
		}
	}
	return FRxCmdResult::Ok();
}

// ---------------------------------------------------------------------------
// Waves (terrain release application)
// ---------------------------------------------------------------------------
void FRxSimWorld::ScheduleRelease(const FRxRelease& Rel)
{
	int32 OriginForce = 0;
	if (Rel.Wave.Num() > 0)
	{
		OriginForce = Rel.Wave[0].Force;
	}
	FRxJsonValue D = FRxJsonValue::Object();
	D.Set(TEXT("origin"), FRxJsonValue::Int(Rel.Origin));
	D.Set(TEXT("force"), FRxJsonValue::Int(OriginForce));
	Emit(TEXT("quake_release"), D);

	for (const FRxWaveCell& W : Rel.Wave)
	{
		FRxScheduledWave SW;
		SW.ApplyTick = Tick + W.DelayTicks;
		SW.Region = W.Region;
		SW.Force = W.Force;
		SW.Origin = Rel.Origin;
		ScheduledWaves.Add(SW);
	}
}

void FRxSimWorld::ApplyWaveEntry(const FRxScheduledWave& W)
{
	const int32 RegionId = W.Region;
	const int32 Force = W.Force;
	SetFlag(TEXT("evidence_propagation"), true);

	{
		FRxJsonValue D = FRxJsonValue::Object();
		D.Set(TEXT("region"), FRxJsonValue::Int(RegionId));
		D.Set(TEXT("force"), FRxJsonValue::Int(Force));
		D.Set(TEXT("origin"), FRxJsonValue::Int(W.Origin));
		Emit(TEXT("quake_wave"), D);
	}

	for (const FRxEntityId Id : EntityOrder)
	{
		FRxEntity& E = Entities[Id];
		if (E.Kind == RxKind::Boss)
		{
			continue; // the boss is the source; it does not damage itself
		}
		const int32 EReg = TerrainImpl.RegionAt(E.Pos);
		const bool bOnRegion = EReg == RegionId;
		const int32 WeaveRegion = E.WeaveRegionId;
		const bool bWeaveHit = E.State == RxState::Weaving && WeaveRegion == RegionId;

		if (bOnRegion)
		{
			const int32 Dmg = Force / RxSim::WAVE_DAMAGE_DIV;
			E.Hp -= Dmg;
			FRxJsonValue D = FRxJsonValue::Object();
			D.Set(TEXT("entity"), FRxJsonValue::Int(Id));
			D.Set(TEXT("region"), FRxJsonValue::Int(RegionId));
			D.Set(TEXT("force"), FRxJsonValue::Int(Force));
			D.Set(TEXT("dmg"), FRxJsonValue::Int(Dmg));
			D.Set(TEXT("hp"), FRxJsonValue::Int(E.Hp));
			Emit(TEXT("wave_damage"), D);
			if (E.Hp <= 0 && E.State != RxState::Dead)
			{
				E.State = RxState::Dead;
				FRxJsonValue DD = FRxJsonValue::Object();
				DD.Set(TEXT("entity"), FRxJsonValue::Int(Id));
				Emit(TEXT("entity_downed"), DD);
			}
		}

		// Interrupted Tokenweave: a RELEASE wave reaching the weave's region breaks it.
		if ((bOnRegion || bWeaveHit) && E.State == RxState::Weaving && !E.bHasWeaveAbort)
		{
			E.WeaveAbortTick = Tick;
			E.bHasWeaveAbort = true;
			SetFlag(TEXT("weave_interrupted"), true);
			SetFlag(TEXT("weave_was_interrupted"), true);
			FRxJsonValue D = FRxJsonValue::Object();
			D.Set(TEXT("entity"), FRxJsonValue::Int(Id));
			D.Set(TEXT("region"), FRxJsonValue::Int(RegionId));
			Emit(TEXT("weave_interrupted"), D);
		}
	}
}

// ---------------------------------------------------------------------------
// snapshot() / state_hash()
// ---------------------------------------------------------------------------
FRxJsonValue FRxSimWorld::ResultToJson(const FRxCmdResult& R)
{
	FRxJsonValue O = FRxJsonValue::Object();
	O.Set(TEXT("ok"), FRxJsonValue::Bool(R.bOk));
	O.Set(TEXT("code"), FRxJsonValue::Str(R.Code));
	O.Set(TEXT("detail"), FRxJsonValue::Str(R.Detail));
	return O;
}

FRxJsonValue FRxSimWorld::EntitySnapshotJson(const FRxEntity& E) const
{
	FRxJsonValue O = FRxJsonValue::Object();
	O.Set(TEXT("id"), FRxJsonValue::Int(E.Id));
	O.Set(TEXT("kind"), FRxJsonValue::Str(E.Kind));
	O.Set(TEXT("pos"), FRxJsonValue::IntPoint(E.Pos));
	O.Set(TEXT("hp"), FRxJsonValue::Int(E.Hp));
	O.Set(TEXT("max_hp"), FRxJsonValue::Int(E.MaxHp));
	O.Set(TEXT("state"), FRxJsonValue::Str(E.State));

	// props: present ONLY when the matching prop exists (mirror Dictionary.has()).
	FRxJsonValue Props = FRxJsonValue::Object();
	if (E.bHasMoveTarget)
	{
		Props.Set(TEXT("move_target"), FRxJsonValue::IntPoint(E.MoveTarget));
	}
	if (E.WeaveRegionId != -1)
	{
		Props.Set(TEXT("weave_region_id"), FRxJsonValue::Int(E.WeaveRegionId));
	}
	if (!E.WeaveMode.IsEmpty())
	{
		Props.Set(TEXT("weave_mode"), FRxJsonValue::Str(E.WeaveMode));
	}
	if (E.WeaveStartTick != -1)
	{
		Props.Set(TEXT("weave_start_tick"), FRxJsonValue::Int(E.WeaveStartTick));
	}
	if (E.bHasWeaveAbort)
	{
		Props.Set(TEXT("weave_abort_tick"), FRxJsonValue::Int(E.WeaveAbortTick));
	}
	O.Set(TEXT("props"), Props);
	return O;
}

FRxJsonValue FRxSimWorld::TerrainSnapshotJson() const
{
	const FRxTerrain& T = GetTerrain();
	FRxJsonValue O = FRxJsonValue::Object();

	FRxJsonValue Edges = FRxJsonValue::Array();
	for (const FRxEdge& E : T.GetEdges())
	{
		FRxJsonValue Row = FRxJsonValue::Array();
		Row.Push(FRxJsonValue::Int(E.A));
		Row.Push(FRxJsonValue::Int(E.B));
		Edges.Push(Row);
	}
	O.Set(TEXT("edges"), Edges);

	FRxJsonValue Regions = FRxJsonValue::Array();
	for (const FRxRegion& R : T.GetRegions())
	{
		FRxJsonValue RO = FRxJsonValue::Object();
		RO.Set(TEXT("anchored_by"), FRxJsonValue::Int(R.AnchoredBy));
		RO.Set(TEXT("id"), FRxJsonValue::Int(R.Id));
		RO.Set(TEXT("kind"), FRxJsonValue::Str(R.Kind));
		RO.Set(TEXT("name"), FRxJsonValue::Str(R.Name));
		FRxJsonValue Poly = FRxJsonValue::Array();
		for (const FIntPoint& P : R.Poly)
		{
			Poly.Push(FRxJsonValue::IntPoint(P));
		}
		RO.Set(TEXT("poly"), Poly);
		RO.Set(TEXT("stable"), FRxJsonValue::Bool(R.bStable));
		RO.Set(TEXT("stress"), FRxJsonValue::Int(R.Stress));
		Regions.Push(RO);
	}
	O.Set(TEXT("regions"), Regions);

	FRxJsonValue Sched = FRxJsonValue::Array();
	for (const FRxStressEvent& S : T.GetStressSchedule())
	{
		FRxJsonValue SO = FRxJsonValue::Object();
		SO.Set(TEXT("rate"), FRxJsonValue::Int(S.Rate));
		SO.Set(TEXT("region"), FRxJsonValue::Int(S.Region));
		SO.Set(TEXT("tick"), FRxJsonValue::Int(S.Tick));
		SO.Set(TEXT("until"), FRxJsonValue::Int(S.Until));
		Sched.Push(SO);
	}
	O.Set(TEXT("stress_schedule"), Sched);

	O.Set(TEXT("time"), FRxJsonValue::Int(T.GetTime()));
	return O;
}

FRxJsonValue FRxSimWorld::BossSnapshotJson() const
{
	const FRxBossSnapshot B = Boss.Snapshot();
	FRxJsonValue O = FRxJsonValue::Object();
	O.Set(TEXT("state"), FRxJsonValue::Str(B.State));
	O.Set(TEXT("state_ticks"), FRxJsonValue::Int(B.StateTicks));
	O.Set(TEXT("anchor_region"), FRxJsonValue::Int(B.AnchorRegion));
	O.Set(TEXT("stability"), FRxJsonValue::Int(B.Stability));
	FRxJsonValue AR = FRxJsonValue::Array();
	for (const int32 R : B.ArenaRegions)
	{
		AR.Push(FRxJsonValue::Int(R));
	}
	O.Set(TEXT("arena_regions"), AR);
	O.Set(TEXT("release_delay"), FRxJsonValue::Int(B.ReleaseDelay));
	O.Set(TEXT("prev_anchor_stress"), FRxJsonValue::Int(B.PrevAnchorStress));
	O.Set(TEXT("tremor_stage"), FRxJsonValue::Int(B.TremorStage));
	return O;
}

FRxJsonValue FRxSimWorld::FlagsSnapshotJson() const
{
	FRxJsonValue O = FRxJsonValue::Object();
	for (const TPair<FString, bool>& Kv : Flags)
	{
		O.Set(Kv.Key, FRxJsonValue::Bool(Kv.Value));
	}
	for (const TPair<FString, FString>& Kv : FlagStrings)
	{
		O.Set(Kv.Key, FRxJsonValue::Str(Kv.Value));
	}
	return O;
}

FRxJsonValue FRxSimWorld::Snapshot() const
{
	FRxJsonValue Snap = FRxJsonValue::Object();

	Snap.Set(TEXT("tick"), FRxJsonValue::Int(Tick));
	// rng_state: Godot stores signed int64; reinterpret the two's-complement bits.
	Snap.Set(TEXT("rng_state"), FRxJsonValue::Int(static_cast<int64>(Rng.State)));
	Snap.Set(TEXT("next_entity_id"), FRxJsonValue::Int(NextEntityId));

	FRxJsonValue Ents = FRxJsonValue::Array();
	for (const FRxEntityId Id : EntityOrder)
	{
		Ents.Push(EntitySnapshotJson(Entities[Id]));
	}
	Snap.Set(TEXT("entities"), Ents);

	Snap.Set(TEXT("terrain"), TerrainSnapshotJson());
	Snap.Set(TEXT("boss"), BossSnapshotJson());
	Snap.Set(TEXT("skills"), SkillsImpl.Snapshot());
	Snap.Set(TEXT("receipts_head"), FRxJsonValue::Str(Receipts.Head));
	Snap.Set(TEXT("receipt_count"), FRxJsonValue::Int(Receipts.Chain.Num()));
	Snap.Set(TEXT("flags"), FlagsSnapshotJson());

	FRxJsonValue PendingArr = FRxJsonValue::Array();
	for (const FRxPending& P : Pending)
	{
		PendingArr.Push(P.Json);
	}
	Snap.Set(TEXT("pending"), PendingArr);

	FRxJsonValue Waves = FRxJsonValue::Array();
	for (const FRxScheduledWave& W : ScheduledWaves)
	{
		FRxJsonValue WO = FRxJsonValue::Object();
		WO.Set(TEXT("apply_tick"), FRxJsonValue::Int(W.ApplyTick));
		WO.Set(TEXT("region"), FRxJsonValue::Int(W.Region));
		WO.Set(TEXT("force"), FRxJsonValue::Int(W.Force));
		WO.Set(TEXT("origin"), FRxJsonValue::Int(W.Origin));
		Waves.Push(WO);
	}
	Snap.Set(TEXT("scheduled_waves"), Waves);

	// strike_cooldowns: sorted "attacker|target" keys -> [[key,tick],...].
	TArray<FString> CdKeys;
	StrikeCd.GetKeys(CdKeys);
	// Code-point-wise compare (matches Godot Array.sort() on Strings; avoids any
	// case-insensitive FString::operator< behaviour).
	CdKeys.Sort([](const FString& A, const FString& B)
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
	for (const FString& K : CdKeys)
	{
		FRxJsonValue Row = FRxJsonValue::Array();
		Row.Push(FRxJsonValue::Str(K));
		Row.Push(FRxJsonValue::Int(StrikeCd.FindRef(K)));
		Cd.Push(Row);
	}
	Snap.Set(TEXT("strike_cooldowns"), Cd);

	Snap.Set(TEXT("runtime_seq"), FRxJsonValue::Int(RuntimeSeq));
	Snap.Set(TEXT("player_id"), FRxJsonValue::Int(PlayerId));
	Snap.Set(TEXT("companion_id"), FRxJsonValue::Int(CompanionId));
	Snap.Set(TEXT("boss_id"), FRxJsonValue::Int(BossId));
	Snap.Set(TEXT("transfer_region_id"), FRxJsonValue::Int(TransferRegionId));
	Snap.Set(TEXT("last_reference"), LastReference);

	return Snap;
}

FString FRxSimWorld::StateHash() const
{
	return FRxCanonJson::HashValue(Snapshot());
}

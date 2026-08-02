#include "RxOracleCommandlet.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Ported deterministic sim (plain C++ — NOT UObjects). Bare includes: the module
// adds Sim/ to the include search path (the Sim TUs include each other this way).
#include "Sim/RxSimWorld.h"
#include "Sim/RxEncounters.h"
#include "Sim/RxDataSource.h"
#include "Sim/RxTerrain.h"
#include "Sim/RxCommands.h"
#include "Sim/RxReceipts.h"
#include "Sim/RxCompanionAI.h"
#include "Sim/RxCanonJson.h"
#include "Sim/RxTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogRxOracle, Log, All);

// ---------------------------------------------------------------------------
// Reference values from the Godot-faithful Python oracle
// (Reflexion-Arena/tools/oracle/run_acceptance.py, sim_mirror.py). Used only to
// print a self-contained VERDICT; the harness computes its own values
// independently and compares.
//
// UPDATED 2026-08-02, twice over. The previous values (1d4a7ce4… / 728 /
// weave_interrupted 5790 / transfer_recognized 6834) were the output of a BUGGY
// mirror: sim_mirror.py was missing the sim-internal T3 weave-completion block
// and the boss entity.state mirror from sim_world.gd. A companion that began
// weaving and was never hit stayed "weaving" forever. Both were restored from
// the GDScript ground truth; the mirror now agrees with this port bit-for-bit,
// so these are no longer "divergent reference" values — they are the SAME
// values this port produces.
//
// Then RE-BASELINED (founder ruling): transfer_domains restored to the canon
// SHENRON §2.2 line 98 set of six. The fragment is embedded in the
// socket_fragment command in acceptance_run_v1.json and enters the hashed
// snapshot, so the correction moved fragment_hash bc4ac7ea…→7b45e77f… and
// final_state_hash b976626…→b36ad6d0….
// ---------------------------------------------------------------------------
namespace RxRef
{
	static const TCHAR* AcceptanceHash = TEXT("b36ad6d028e1b5452629df480c537adc7ce85e1b4b5b0fab4c95067506261cfa");
	static const TCHAR* ChainHead      = TEXT("94090edffdc3f30a7b58d22420832063d2ced17ae8d9962ac39ae4fdf96933c6");
	static constexpr int32 ReceiptCount = 776;
	// beat name -> expected tick
	static const TArray<TPair<FString, int32>>& Beats()
	{
		static const TArray<TPair<FString, int32>> V = {
			{ TEXT("telegraph"), 5386 },
			{ TEXT("weave_interrupted"), 5770 },
			{ TEXT("DESTABILIZED"), 6424 },
			{ TEXT("DEFEATED"), 6462 },
			{ TEXT("fragment"), 6701 },
			{ TEXT("skill"), 6761 },
			{ TEXT("transfer_recognized"), 6851 },
			{ TEXT("receipt"), 6902 },
		};
		return V;
	}
	static constexpr int32 AdversarialTotal = 44;
}

// Default fixture paths (Godot reference repo, alongside the Python oracle).
namespace RxPath
{
	static const TCHAR* Acceptance = TEXT("/home/shax/Projects/core-tech/Reflexion-Arena/game/data/acceptance_run_v1.json");
	static const TCHAR* Malformed  = TEXT("/home/shax/Projects/core-tech/Reflexion-Arena/tests/adversarial/malformed_plans.jsonl");
	static const TCHAR* Injection  = TEXT("/home/shax/Projects/core-tech/Reflexion-Arena/tests/adversarial/injection_corpus.jsonl");
}

// ---------------------------------------------------------------------------
// DATA BOUNDARY state (RX_DATA_BOUNDARY_CONTRACT.md v1).
//
// The arena config used by EVERY BuildArena call in this harness is loaded FROM
// DISK (Data/arena_earthquake.json) through FRxDataSource. Proof obligation §5.2
// is precisely that the hash still holds when the arena comes from a file rather
// than from C++ literals. There is deliberately NO fallback to the baked config:
// if the load fails the harness reports FAIL rather than silently passing on
// hardcoded data.
// ---------------------------------------------------------------------------
namespace RxData
{
	static FRxArenaConfig ArenaCfg;
	static bool bArenaLoaded = false;
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------
namespace
{
	// Recursive FJsonValue -> FRxJsonValue (closed canonical set). JSON numbers in
	// these fixtures are integral, so int64 cast is exact.
	FRxJsonValue JsonToRx(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid())
		{
			return FRxJsonValue::Int(0);
		}
		switch (V->Type)
		{
		case EJson::Boolean: return FRxJsonValue::Bool(V->AsBool());
		case EJson::Number:  return FRxJsonValue::Int(static_cast<int64>(V->AsNumber()));
		case EJson::String:  return FRxJsonValue::Str(V->AsString());
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
					Obj.Set(FString(Kv.Key.ToView()), JsonToRx(Kv.Value));
				}
			}
			return Obj;
		}
		default: return FRxJsonValue::Int(0);
		}
	}

	bool LoadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& Out)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Path))
		{
			UE_LOG(LogRxOracle, Error, TEXT("ORACLE could not read file: %s"), *Path);
			return false;
		}
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
		return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
	}

	// Parse one JSON document from a string (used for JSONL rows + embedded payloads).
	bool ParseJsonObjectString(const FString& Text, TSharedPtr<FJsonObject>& Out)
	{
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
	}

	bool LoadJsonLines(const FString& Path, TArray<TSharedPtr<FJsonObject>>& Out)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *Path))
		{
			UE_LOG(LogRxOracle, Error, TEXT("ORACLE could not read file: %s"), *Path);
			return false;
		}
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines, /*CullEmpty*/ true);
		for (const FString& Line : Lines)
		{
			const FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				continue;
			}
			TSharedPtr<FJsonObject> Row;
			if (ParseJsonObjectString(Trimmed, Row))
			{
				Out.Add(Row);
			}
		}
		return true;
	}
}

// ---------------------------------------------------------------------------
// ACCEPTANCE — mirror run_acceptance.run_once() exactly.
// ---------------------------------------------------------------------------
namespace
{
	struct FAcceptResult
	{
		bool bValid = false;
		FString FinalHash;
		int32 ReceiptCount = 0;
		FString Head;
		int32 Accepted = 0;
		TMap<FString, int32> Beats; // beat name -> first tick
		int32 FinalTick = 0;
		int32 PlayerHp = 0;
		int32 CompanionHp = 0;
		int32 BossStability = 0;
	};

	// beat name -> world event type (transfer_recognized is flag-based, handled separately)
	const TArray<TPair<FString, FString>>& BeatEventMap()
	{
		static const TArray<TPair<FString, FString>> V = {
			{ TEXT("telegraph"),         TEXT("boss_telegraph") },
			{ TEXT("weave_interrupted"), TEXT("weave_interrupted") },
			{ TEXT("DESTABILIZED"),      TEXT("boss_destabilized") },
			{ TEXT("DEFEATED"),          TEXT("boss_defeated") },
			{ TEXT("fragment"),          TEXT("fragment_socketed") },
			{ TEXT("skill"),             TEXT("skill_authored") },
			{ TEXT("receipt"),           TEXT("transfer_receipt") },
		};
		return V;
	}

	FAcceptResult RunAcceptanceOnce(uint64 Seed,
		const TMap<int32, TArray<FRxJsonValue>>& ByTick, int32 MaxTick)
	{
		FAcceptResult R;

		FRxSimWorld World(Seed);
		FRxEncounters::BuildArena(World, RxData::ArenaCfg); // FROM DISK (contract §5.2)

		int32 TransferRecognizedTick = -1;
		// while not agent.exhausted(world)  <=>  world.tick <= max_tick
		while (World.Tick <= MaxTick)
		{
			if (const TArray<FRxJsonValue>* Cmds = ByTick.Find(World.Tick))
			{
				for (const FRxJsonValue& Cmd : *Cmds)
				{
					const FRxCmdResult Res = World.Submit(Cmd);
					if (Res.bOk)
					{
						R.Accepted += 1;
					}
				}
			}
			World.Step();
			if (TransferRecognizedTick == -1 && World.GetFlag(TEXT("transfer_recognized")))
			{
				TransferRecognizedTick = World.Tick;
			}
		}

		// beats: first event of each mapped type
		for (const TPair<FString, FString>& Beat : BeatEventMap())
		{
			for (const FRxEvent& Ev : World.GetEvents())
			{
				if (Ev.Type == Beat.Value)
				{
					R.Beats.Add(Beat.Key, Ev.Tick);
					break;
				}
			}
		}
		R.Beats.Add(TEXT("transfer_recognized"), TransferRecognizedTick);

		R.FinalHash = World.StateHash();
		R.ReceiptCount = World.Receipts.Chain.Num();
		R.Head = World.Receipts.Head;
		R.FinalTick = World.Tick;
		R.PlayerHp = World.PlayerId != -1 && World.HasEntity(World.PlayerId)
			? World.GetEntity(World.PlayerId).Hp : -1;
		R.CompanionHp = World.CompanionId != -1 && World.HasEntity(World.CompanionId)
			? World.GetEntity(World.CompanionId).Hp : -1;
		R.BossStability = World.Boss.Stability;
		R.bValid = true;
		return R;
	}

	// Returns true if acceptance parity holds against the Python-oracle reference.
	bool RunAcceptance()
	{
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ============================================================"));
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE == ACCEPTANCE (C++ FRxSimWorld vs Python oracle) =="));
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ============================================================"));

		TSharedPtr<FJsonObject> Root;
		if (!LoadJsonObject(RxPath::Acceptance, Root))
		{
			UE_LOG(LogRxOracle, Error, TEXT("ORACLE ACCEPTANCE FAIL: cannot load %s"), RxPath::Acceptance);
			return false;
		}

		const uint64 Seed = static_cast<uint64>(Root->GetIntegerField(TEXT("seed")));

		TMap<int32, TArray<FRxJsonValue>> ByTick;
		int32 MaxTick = -1;
		int32 TotalCmds = 0;
		const TArray<TSharedPtr<FJsonValue>>* Ticks = nullptr;
		if (Root->TryGetArrayField(TEXT("ticks"), Ticks) && Ticks != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Ticks)
			{
				const TSharedPtr<FJsonObject> EO = Entry->AsObject();
				if (!EO.IsValid())
				{
					continue;
				}
				const int32 T = static_cast<int32>(EO->GetIntegerField(TEXT("tick")));
				if (T < 0)
				{
					continue;
				}
				MaxTick = FMath::Max(MaxTick, T);
				TArray<FRxJsonValue>& Bucket = ByTick.FindOrAdd(T);
				const TArray<TSharedPtr<FJsonValue>>* Cmds = nullptr;
				if (EO->TryGetArrayField(TEXT("cmds"), Cmds) && Cmds != nullptr)
				{
					for (const TSharedPtr<FJsonValue>& C : *Cmds)
					{
						Bucket.Add(JsonToRx(C));
						TotalCmds += 1;
					}
				}
			}
		}

		UE_LOG(LogRxOracle, Display, TEXT("ORACLE seed=%llu tick_entries=%d total_cmds=%d max_tick=%d"),
			Seed, ByTick.Num(), TotalCmds, MaxTick);

		const FAcceptResult R1 = RunAcceptanceOnce(Seed, ByTick, MaxTick);
		const FAcceptResult R2 = RunAcceptanceOnce(Seed, ByTick, MaxTick);

		UE_LOG(LogRxOracle, Display, TEXT("ORACLE run1 final_state_hash: %s"), *R1.FinalHash);
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE run2 final_state_hash: %s"), *R2.FinalHash);
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE receipt_count: %d (scripted accepted: %d)"),
			R1.ReceiptCount, R1.Accepted);
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE receipt chain head: %s"), *R1.Head);
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE final: tick=%d player_hp=%d companion_hp=%d boss_stability=%d"),
			R1.FinalTick, R1.PlayerHp, R1.CompanionHp, R1.BossStability);

		// ---- read the two candidate ground-truth references ----
		// (A) GODOT ENGINE: the acceptance file's own expect block, "Re-anchored to
		//     Godot 4.7.1 engine output" (final_state_hash + receipt_count). This is
		//     the product truth the UE port must reproduce.
		// (B) PYTHON MIRROR: tools/oracle/sim_mirror.py values (RxRef::*), which the
		//     acceptance notes document as DIVERGENT from the engine (+48 companion-
		//     runtime receipts). Shown for completeness / triage.
		FString EngineHash;
		int32 EngineCount = -1;
		const TSharedPtr<FJsonObject>* Expect = nullptr;
		if (Root->TryGetObjectField(TEXT("expect"), Expect) && Expect != nullptr)
		{
			(*Expect)->TryGetStringField(TEXT("final_state_hash"), EngineHash);
			EngineCount = static_cast<int32>((*Expect)->GetIntegerField(TEXT("receipt_count")));
		}

		bool bOk = true;

		const bool bDeterministic = R1.FinalHash == R2.FinalHash;
		bOk &= bDeterministic;
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE  [%s] determinism_2x  (run1==run2)"),
			bDeterministic ? TEXT("PASS") : TEXT("FAIL"));

		// ---- PRIMARY: parity against the Godot 4.7.1 engine (expect block) ----
		const bool bEngHash = !EngineHash.IsEmpty() && R1.FinalHash == EngineHash;
		bOk &= bEngHash;
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE  [%s] final_state_hash vs GODOT-ENGINE  cpp=%s  engine=%s"),
			bEngHash ? TEXT("PASS") : TEXT("FAIL"), *R1.FinalHash, *EngineHash);

		const bool bEngCount = R1.ReceiptCount == EngineCount;
		bOk &= bEngCount;
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE  [%s] receipt_count    vs GODOT-ENGINE  cpp=%d  engine=%d"),
			bEngCount ? TEXT("PASS") : TEXT("FAIL"), R1.ReceiptCount, EngineCount);

		const bool bAccepted = R1.Accepted == TotalCmds && R1.Accepted == 50;
		bOk &= bAccepted;
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE  [%s] scripted_stream_fully_accepted  accepted=%d/%d"),
			bAccepted ? TEXT("PASS") : TEXT("FAIL"), R1.Accepted, TotalCmds);

		// ---- SECONDARY (informational): diff against the Python mirror ----
		const bool bMirHash = R1.FinalHash == RxRef::AcceptanceHash;
		const bool bMirCount = R1.ReceiptCount == RxRef::ReceiptCount;
		const bool bMirHead = R1.Head == RxRef::ChainHead;
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE  [INFO] vs PYTHON-MIRROR  hash %s (mirror=%s)  count %s (mirror=%d)  head %s"),
			bMirHash ? TEXT("MATCH") : TEXT("DIFF"), RxRef::AcceptanceHash,
			bMirCount ? TEXT("MATCH") : TEXT("DIFF"), RxRef::ReceiptCount,
			bMirHead ? TEXT("MATCH") : TEXT("DIFF"));

		// beats: cpp vs mirror (engine expect block records no beats). Ordering is a
		// hard invariant; per-beat equality against the mirror is informational.
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE key beats (cpp vs python-mirror):"));
		TArray<int32> Order;
		for (const TPair<FString, int32>& RefBeat : RxRef::Beats())
		{
			const int32* Got = R1.Beats.Find(RefBeat.Key);
			const int32 CppTick = Got != nullptr ? *Got : -1;
			const bool bMatch = CppTick == RefBeat.Value;
			Order.Add(CppTick);
			UE_LOG(LogRxOracle, Display, TEXT("ORACLE    [%s] %-20s cpp=%d mirror=%d"),
				bMatch ? TEXT("MATCH") : TEXT("DIFF"), *RefBeat.Key, CppTick, RefBeat.Value);
		}
		bool bOrdered = true;
		for (int32 i = 1; i < Order.Num(); ++i)
		{
			if (Order[i] < Order[i - 1]) { bOrdered = false; break; }
		}
		bOk &= bOrdered;
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE  [%s] beats_ordered"),
			bOrdered ? TEXT("PASS") : TEXT("FAIL"));

		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ACCEPTANCE RESULT (vs Godot engine): %s"),
			bOk ? TEXT("PARITY") : TEXT("MISMATCH"));
		return bOk;
	}
}

// ---------------------------------------------------------------------------
// ADVERSARIAL — mirror run_adversarial.py (PART 1/2/2b/3), 44 checks.
// ---------------------------------------------------------------------------
namespace
{
	int32 AdvPassed = 0;
	int32 AdvTotal = 0;

	void AdvCheck(const FString& Name, bool bOk, const FString& Detail)
	{
		AdvTotal += 1;
		if (bOk) { AdvPassed += 1; }
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE  [%s] %-46s %s"),
			bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
	}

	// legacy 'kind_NNNNNN' string (or int) -> id; -1 when absent (mirrors _entity_num)
	int32 EntityNum(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) { return -1; }
		if (V->Type == EJson::Number) { return static_cast<int32>(V->AsNumber()); }
		if (V->Type == EJson::String)
		{
			const FString S = V->AsString();
			FString Left, Right;
			if (S.Split(TEXT("_"), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				if (!Right.IsEmpty() && Right.IsNumeric())
				{
					return FCString::Atoi(*Right);
				}
			}
			else if (S.IsNumeric())
			{
				return FCString::Atoi(*S);
			}
		}
		return -1;
	}

	struct FPlanVerdict
	{
		bool bRejected = false;
		FString Layer = TEXT("none");
		FString Code = TEXT("OK");
	};

	FPlanVerdict PlanReject(const FString& Layer, const FString& Code)
	{
		return FPlanVerdict{ true, Layer, Code };
	}

	// Mirror validate_plan_payload(): map one legacy intent-plan onto §2 envelopes.
	FPlanVerdict ValidatePlanPayload(const FString& PayloadStr, FRxSimWorld& World)
	{
		static const TSet<FString> AllowedKeys = {
			TEXT("schema_version"), TEXT("plan_id"), TEXT("summary"), TEXT("references"),
			TEXT("constraints"), TEXT("actions"), TEXT("approval"), TEXT("expected_effect")
		};
		static const TSet<FString> ConstraintBoolKeys = {
			TEXT("may_sell_structures"), TEXT("may_alter_primary_route")
		};
		static const TSet<FString> ConstraintIntKeys = { TEXT("minimum_resource_reserve") };
		constexpr int32 PlanSanityCap = 32;

		// (1) parse == legacy schema layer
		TSharedPtr<FJsonObject> Plan;
		if (!ParseJsonObjectString(PayloadStr, Plan))
		{
			return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED")); // parse failed
		}

		// (2) plan-level structural pre-checks
		FString SchemaVer;
		if (!Plan->TryGetStringField(TEXT("schema_version"), SchemaVer) || SchemaVer != TEXT("arena.intent-plan.v1"))
		{
			return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED"));
		}
		for (const auto& Kv : Plan->Values)
		{
			if (!AllowedKeys.Contains(FString(Kv.Key.ToView())))
			{
				return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED")); // additionalProperties
			}
		}
		if (Plan->HasField(TEXT("constraints")))
		{
			const TSharedPtr<FJsonObject>* Constraints = nullptr;
			if (!Plan->TryGetObjectField(TEXT("constraints"), Constraints) || Constraints == nullptr)
			{
				return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED")); // constraints not object
			}
			for (const auto& Kv : (*Constraints)->Values)
			{
				const FString CKey = FString(Kv.Key.ToView());
				if (ConstraintBoolKeys.Contains(CKey) && Kv.Value->Type != EJson::Boolean)
				{
					return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED"));
				}
				if (ConstraintIntKeys.Contains(CKey) && Kv.Value->Type != EJson::Number)
				{
					return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED"));
				}
			}
		}
		// actions must be an array
		const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
		if (!Plan->TryGetArrayField(TEXT("actions"), Actions) || Actions == nullptr)
		{
			return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED"));
		}
		for (const TSharedPtr<FJsonValue>& A : *Actions)
		{
			const TSharedPtr<FJsonObject> AO = A->AsObject();
			if (!AO.IsValid())
			{
				return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED")); // action malformed
			}
			FString ActionName;
			if (!AO->TryGetStringField(TEXT("action"), ActionName))
			{
				return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED"));
			}
			if (AO->HasField(TEXT("cost")))
			{
				const TSharedPtr<FJsonValue> Cost = AO->Values.FindRef(TEXT("cost"));
				if (!Cost.IsValid() || Cost->Type != EJson::Number || Cost->AsNumber() < 0.0)
				{
					return PlanReject(TEXT("schema"), TEXT("ERR_MALFORMED")); // non-negative int
				}
			}
		}

		// (3) plan sanity cap
		if (Actions->Num() > PlanSanityCap)
		{
			return PlanReject(TEXT("validator"), TEXT("ERR_STATE"));
		}

		// (4) approval smuggle: approval=="approved" with zero actions -> companion
		//     T2 envelope WITHOUT approved -> ERR_AUTHORITY.
		FString Approval;
		Plan->TryGetStringField(TEXT("approval"), Approval);
		if (Approval == TEXT("approved") && Actions->Num() == 0)
		{
			FRxCommandEnvelope Env;
			Env.Actor = RxActor::Companion;
			Env.Type = RxCmd::TokenweaveBegin;
			Env.Params = FRxJsonValue::Object();
			Env.Params.Set(TEXT("mode"), FRxJsonValue::Str(TEXT("anchor")));
			Env.Params.Set(TEXT("region_id"), FRxJsonValue::Int(1));
			const FRxCmdResult Rr = FRxCommands::Validate(Env, &World);
			return Rr.bOk ? FPlanVerdict{ false, TEXT("none"), TEXT("OK") }
				: PlanReject(TEXT("authority"), Rr.Code);
		}

		// (5) references[0].entity_id -> strike envelope (ghost ref dies in ERR_STATE)
		int32 RefEntity = -1;
		const TArray<TSharedPtr<FJsonValue>>* Refs = nullptr;
		if (Plan->TryGetArrayField(TEXT("references"), Refs) && Refs != nullptr && Refs->Num() > 0)
		{
			const TSharedPtr<FJsonObject> R0 = (*Refs)[0]->AsObject();
			if (R0.IsValid())
			{
				RefEntity = EntityNum(R0->Values.FindRef(TEXT("entity_id")));
			}
		}
		if (RefEntity != -1)
		{
			FRxCommandEnvelope Env;
			Env.Actor = RxActor::Companion;
			Env.Type = RxCmd::Strike;
			Env.Params = FRxJsonValue::Object();
			Env.Params.Set(TEXT("target_id"), FRxJsonValue::Int(RefEntity));
			Env.bApproved = true;
			Env.bHasApproved = true;
			const FRxCmdResult Rr = FRxCommands::Validate(Env, &World);
			if (!Rr.bOk)
			{
				return PlanReject(TEXT("validator"), Rr.Code);
			}
		}

		// action translation -> §2 envelopes through the REAL validator
		for (const TSharedPtr<FJsonValue>& A : *Actions)
		{
			const TSharedPtr<FJsonObject> AO = A->AsObject();
			const FString Name = AO->GetStringField(TEXT("action"));

			// ACTION_MAP: build_structure->tokenweave_begin, set_targeting->strike,
			// ping->wait, sell_structure->None(unknown), else unknown.
			FString CType;
			bool bMapped = false;
			if (Name == TEXT("build_structure")) { CType = RxCmd::TokenweaveBegin; bMapped = true; }
			else if (Name == TEXT("set_targeting")) { CType = RxCmd::Strike; bMapped = true; }
			else if (Name == TEXT("ping")) { CType = RxCmd::Wait; bMapped = true; }
			else if (Name == TEXT("sell_structure")) { bMapped = false; } // None analogue
			else { bMapped = false; }

			if (!bMapped)
			{
				return PlanReject(TEXT("schema"), TEXT("ERR_UNKNOWN_TYPE"));
			}

			FRxCommandEnvelope Env;
			Env.Actor = RxActor::Companion;
			Env.Params = FRxJsonValue::Object();
			if (CType == RxCmd::Strike)
			{
				int32 Tid = EntityNum(AO->Values.FindRef(TEXT("entity_id")));
				if (Tid == -1) { Tid = RefEntity; }
				Env.Type = RxCmd::Strike;
				Env.Params.Set(TEXT("target_id"), FRxJsonValue::Int(Tid));
				Env.bApproved = true; Env.bHasApproved = true;
			}
			else if (CType == RxCmd::TokenweaveBegin)
			{
				Env.Type = RxCmd::TokenweaveBegin;
				Env.Params.Set(TEXT("mode"), FRxJsonValue::Str(TEXT("fabricate")));
				Env.Params.Set(TEXT("region_id"), FRxJsonValue::Int(1));
				Env.bApproved = true; Env.bHasApproved = true;
			}
			else // wait
			{
				Env.Type = RxCmd::Wait;
			}
			const FRxCmdResult Rr = FRxCommands::Validate(Env, &World);
			if (!Rr.bOk)
			{
				const FString Layer = Rr.Code == FString(RxCode::ErrAuthority) ? TEXT("authority")
					: ((Rr.Code == FString(RxCode::ErrMalformed) || Rr.Code == FString(RxCode::ErrUnknownType))
						? TEXT("schema") : TEXT("validator"));
				return PlanReject(Layer, Rr.Code);
			}
		}
		return FPlanVerdict{ false, TEXT("none"), TEXT("OK") }; // accepted (BAD)
	}

	bool AdvPart1()
	{
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE --- PART 1: malformed_plans.jsonl -> CONTRACTS §2 envelopes ---"));
		TArray<TSharedPtr<FJsonObject>> Rows;
		if (!LoadJsonLines(RxPath::Malformed, Rows) || Rows.Num() == 0)
		{
			UE_LOG(LogRxOracle, Error, TEXT("ORACLE PART1 FAIL: cannot load malformed fixtures"));
			return false;
		}
		FRxSimWorld World(7);
		FRxEncounters::BuildArena(World, RxData::ArenaCfg); // FROM DISK (contract §5.2)
		for (const TSharedPtr<FJsonObject>& Row : Rows)
		{
			const FString Id = Row->GetStringField(TEXT("id"));
			const FString Case = Row->GetStringField(TEXT("case"));
			const FString ExpLayer = Row->GetStringField(TEXT("expected_layer"));
			const FString Payload = Row->GetStringField(TEXT("payload"));
			const FPlanVerdict V = ValidatePlanPayload(Payload, World);
			const bool bGood = V.bRejected && V.Layer == ExpLayer;
			AdvCheck(FString::Printf(TEXT("%s (%s)"), *Id, *Case), bGood,
				FString::Printf(TEXT("-> %s/%s [expect %s]"), *V.Layer, *V.Code, *ExpLayer));
		}
		return true;
	}

	bool AdvPart2()
	{
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE --- PART 2: injection_corpus.jsonl through intent layer (live world) ---"));
		TArray<TSharedPtr<FJsonObject>> Rows;
		if (!LoadJsonLines(RxPath::Injection, Rows) || Rows.Num() == 0)
		{
			UE_LOG(LogRxOracle, Error, TEXT("ORACLE PART2 FAIL: cannot load injection fixtures"));
			return false;
		}
		int32 ParseReject = 0, ApprovalGated = 0, Executed = 0;
		for (const TSharedPtr<FJsonObject>& Row : Rows)
		{
			const FString Id = Row->GetStringField(TEXT("id"));
			const FString Vector = Row->GetStringField(TEXT("vector"));
			const FString ExpLayer = Row->GetStringField(TEXT("expected_layer"));
			const FString Payload = Row->GetStringField(TEXT("payload"));

			FRxSimWorld World(7);
		FRxEncounters::BuildArena(World, RxData::ArenaCfg); // FROM DISK (contract §5.2)
			World.Step(); // step once so the world has run
			const FString Before = World.StateHash();

			FRxJsonValue Ref = FRxJsonValue::Object();
			if (Vector == TEXT("entity_label"))
			{
				Ref.Set(TEXT("kind"), FRxJsonValue::Str(TEXT("region")));
				Ref.Set(TEXT("region_id"), FRxJsonValue::Int(1));
				Ref.Set(TEXT("name"), FRxJsonValue::Str(Payload));
			}

			FRxCompanionAI* Companion = World.GetCompanion();
			bool bMutated = true;
			FString Mech = TEXT("EXECUTED");
			if (Companion != nullptr)
			{
				const FRxInstructionResult IR = Companion->ReceiveInstruction(World, Payload, Ref);
				const FString After = World.StateHash(); // snapshot includes pending
				bMutated = Before != After;
				if (!IR.bOk) { Mech = TEXT("PARSE_REJECT"); ParseReject++; }
				else if (!IR.bNeedsApproval) { Mech = TEXT("EXECUTED"); Executed++; }
				else { Mech = TEXT("APPROVAL_GATED"); ApprovalGated++; }
			}
			const bool bGood = (!bMutated) && (Mech == TEXT("PARSE_REJECT") || Mech == TEXT("APPROVAL_GATED"));
			AdvCheck(FString::Printf(TEXT("%s [%s/%s]"), *Id, *Vector, *ExpLayer), bGood,
				FString::Printf(TEXT("%s; world unchanged=%s"), *Mech, (!bMutated) ? TEXT("True") : TEXT("False")));
		}
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE   mechanisms: PARSE_REJECT=%d APPROVAL_GATED=%d EXECUTED=%d"),
			ParseReject, ApprovalGated, Executed);
		return true;
	}

	bool AdvPart2b()
	{
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE --- PART 2b: authority matrix on raw envelopes ---"));
		FRxSimWorld World(7);
		FRxEncounters::BuildArena(World, RxData::ArenaCfg); // FROM DISK (contract §5.2)

		// companion T2 without approval -> ERR_AUTHORITY (all 5 T2 types)
		auto MakeT2Params = [](const FString& T) -> FRxJsonValue
		{
			FRxJsonValue P = FRxJsonValue::Object();
			if (T == RxCmd::Strike) { P.Set(TEXT("region"), FRxJsonValue::Int(1)); }
			else if (T == RxCmd::TokenweaveBegin) { P.Set(TEXT("mode"), FRxJsonValue::Str(TEXT("anchor"))); P.Set(TEXT("region_id"), FRxJsonValue::Int(1)); }
			else if (T == RxCmd::UseSkill) { P.Set(TEXT("skill_id"), FRxJsonValue::Str(TEXT("faultline_interrupt"))); FRxJsonValue Tg = FRxJsonValue::Object(); Tg.Set(TEXT("region_id"), FRxJsonValue::Int(1)); P.Set(TEXT("target"), Tg); }
			else if (T == RxCmd::SocketFragment) { P.Set(TEXT("fragment"), FRxJsonValue::Object()); }
			else if (T == RxCmd::AuthorSkill) { P.Set(TEXT("spec"), FRxJsonValue::Object()); }
			return P;
		};
		const TArray<FString> T2Types = {
			RxCmd::Strike, RxCmd::TokenweaveBegin, RxCmd::UseSkill, RxCmd::SocketFragment, RxCmd::AuthorSkill
		};
		for (const FString& T : T2Types)
		{
			FRxCommandEnvelope Env;
			Env.Actor = RxActor::Companion;
			Env.Type = T;
			Env.Params = MakeT2Params(T);
			const FRxCmdResult R = FRxCommands::Validate(Env, &World);
			AdvCheck(FString::Printf(TEXT("companion %s unapproved"), *T),
				(!R.bOk) && R.Code == FString(RxCode::ErrAuthority), R.Code);
		}

		// approved as a STRING (type smuggle) -> ERR_MALFORMED (hoisted by the
		// JSON->envelope parser, exactly as the reference validator's _is_bool check).
		{
			FRxJsonValue Cmd = FRxJsonValue::Object();
			Cmd.Set(TEXT("actor"), FRxJsonValue::Str(RxActor::Companion));
			Cmd.Set(TEXT("type"), FRxJsonValue::Str(RxCmd::Strike));
			FRxJsonValue P = FRxJsonValue::Object();
			P.Set(TEXT("region"), FRxJsonValue::Int(1));
			Cmd.Set(TEXT("params"), P);
			Cmd.Set(TEXT("approved"), FRxJsonValue::Str(TEXT("founder_preauthorized")));
			const FRxCmdResult R = World.Submit(Cmd);
			AdvCheck(TEXT("approved-string smuggle"),
				(!R.bOk) && R.Code == FString(RxCode::ErrMalformed), R.Code);
		}

		// player T2 (UI pre-approved) with valid state -> OK
		{
			FRxCommandEnvelope Env;
			Env.Actor = RxActor::Player;
			Env.Type = RxCmd::Strike;
			Env.Params = FRxJsonValue::Object();
			Env.Params.Set(TEXT("region"), FRxJsonValue::Int(1));
			Env.bApproved = true; Env.bHasApproved = true;
			const FRxCmdResult R = FRxCommands::Validate(Env, &World);
			AdvCheck(TEXT("player strike approved valid"), R.bOk, R.Code);
		}

		// T3 world mutation is not commandable at all -> ERR_UNKNOWN_TYPE
		{
			FRxCommandEnvelope Env;
			Env.Actor = RxActor::Companion;
			Env.Type = TEXT("set_world_state");
			Env.Params = FRxJsonValue::Object();
			Env.bApproved = true; Env.bHasApproved = true;
			const FRxCmdResult R = FRxCommands::Validate(Env, &World);
			AdvCheck(TEXT("T3 type not commandable"),
				(!R.bOk) && R.Code == FString(RxCode::ErrUnknownType), R.Code);
		}
		return true;
	}

	bool AdvPart3()
	{
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE --- PART 3: receipt chain tamper (RA-01/RA-02/RA-03) ---"));
		FRxSimWorld World(7);
		FRxEncounters::BuildArena(World, RxData::ArenaCfg); // FROM DISK (contract §5.2)

		// small scripted run (mirror the Python PART 3 fixture)
		TMap<int32, TArray<FRxJsonValue>> ByTick;
		auto MakeCmd = [](int32 Seq, int32 T, const FString& Type, TFunction<void(FRxJsonValue&)> Params) -> FRxJsonValue
		{
			FRxJsonValue C = FRxJsonValue::Object();
			C.Set(TEXT("seq"), FRxJsonValue::Int(Seq));
			C.Set(TEXT("tick"), FRxJsonValue::Int(T));
			C.Set(TEXT("actor"), FRxJsonValue::Str(RxActor::Player));
			C.Set(TEXT("type"), FRxJsonValue::Str(Type));
			FRxJsonValue P = FRxJsonValue::Object();
			Params(P);
			C.Set(TEXT("params"), P);
			C.Set(TEXT("approved"), FRxJsonValue::Bool(true));
			return C;
		};
		ByTick.FindOrAdd(0).Add(MakeCmd(1, 0, RxCmd::MoveTo, [](FRxJsonValue& P){ P.Set(TEXT("x"), FRxJsonValue::Int(13000)); P.Set(TEXT("y"), FRxJsonValue::Int(15000)); }));
		ByTick.FindOrAdd(5).Add(MakeCmd(2, 5, RxCmd::Wait, [](FRxJsonValue&){}));
		ByTick.FindOrAdd(9).Add(MakeCmd(3, 9, RxCmd::Wait, [](FRxJsonValue&){}));
		const int32 MaxTick = 9;
		while (World.Tick <= MaxTick)
		{
			if (const TArray<FRxJsonValue>* Cmds = ByTick.Find(World.Tick))
			{
				for (const FRxJsonValue& C : *Cmds) { World.Submit(C); }
			}
			World.Step();
		}

		const TArray<FRxReceipt> Chain = World.Receipts.Chain;
		const FString Head = World.Receipts.Head;
		AdvCheck(TEXT("chain baseline verifies"), Chain.Num() >= 3,
			FString::Printf(TEXT("count=%d"), Chain.Num()));

		// RA-01: reorder two receipts
		{
			FRxReceipts R;
			R.Chain.Add(Chain[0]); R.Chain.Add(Chain[2]); R.Chain.Add(Chain[1]);
			for (int32 i = 3; i < Chain.Num(); ++i) { R.Chain.Add(Chain[i]); }
			R.Head = Head;
			const FRxVerifyResult V = R.Verify();
			AdvCheck(TEXT("RA-01 reorder rejected"), !V.bOk, V.Detail);
		}
		// RA-02: edit sealed content (state_hash), keep length + head
		{
			FRxReceipts R;
			R.Chain = Chain;
			R.Chain[1].StateHash = FString::ChrN(64, TEXT('0'));
			R.Head = Head;
			const FRxVerifyResult V = R.Verify();
			AdvCheck(TEXT("RA-02 content edit rejected"), !V.bOk, V.Detail);
		}
		// RA-03: append a forged receipt after the head
		{
			FRxReceipts R;
			R.Chain = Chain;
			FRxReceipt Forged;
			Forged.Seq = 999; Forged.Tick = 99;
			Forged.CmdHash = FString::ChrN(64, TEXT('f'));
			Forged.Prev = Head;
			Forged.ResultCode = TEXT("OK");
			Forged.StateHash = FString::ChrN(64, TEXT('0'));
			R.Chain.Add(Forged);
			R.Head = Head; // attacker claims the old head
			const FRxVerifyResult V = R.Verify();
			AdvCheck(TEXT("RA-03 append forgery rejected"), !V.bOk, V.Detail);
		}
		return true;
	}

	bool RunAdversarial()
	{
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ============================================================"));
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE == ADVERSARIAL (C++ FRxCommands/FRxReceipts/CompanionAI) =="));
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ============================================================"));
		AdvPassed = 0;
		AdvTotal = 0;
		AdvPart1();
		AdvPart2();
		AdvPart2b();
		AdvPart3();
		const bool bOk = (AdvPassed == AdvTotal) && (AdvTotal == RxRef::AdversarialTotal);
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ADVERSARIAL checks: %d/%d passed (oracle target: %d)"),
			AdvPassed, AdvTotal, RxRef::AdversarialTotal);
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ADVERSARIAL RESULT: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
		return bOk;
	}
}

// ---------------------------------------------------------------------------
// DATA BOUNDARY — RX_DATA_BOUNDARY_CONTRACT.md v1 proof obligations §5.1 and
// §5.3 (§5.2 hash invariance and §5.4 ordering are proven by the ACCEPTANCE
// section above, which now builds every arena from the on-disk config).
//
//   §5.1 Loader fidelity  — Load*FromFile(Data/*.json) == Load*Baked(), field by
//                           field, PASS/FAIL printed per field group.
//   §5.3 Malformed input  — truncated / non-integral coordinate / missing key
//                           each rejected with an error naming the offence, and
//                           with the output struct left untouched.
// ---------------------------------------------------------------------------
namespace
{
	int32 DataPassed = 0;
	int32 DataTotal = 0;

	void DataCheck(const FString& Name, bool bOk, const FString& Detail)
	{
		DataTotal += 1;
		if (bOk) { DataPassed += 1; }
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE  [%s] %-46s %s"),
			bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
	}

	// Records the FIRST differing field path in a group, so a failure says
	// exactly which field diverged rather than just "not equal".
	struct FFieldDiff
	{
		bool bOk = true;
		FString First;

		void Note(const FString& Where)
		{
			if (bOk) { bOk = false; First = Where; }
		}
		template <typename T>
		void Eq(const FString& Where, const T& A, const T& B)
		{
			if (!(A == B)) { Note(Where); }
		}
		FString Detail(const FString& OkText) const
		{
			return bOk ? OkText : FString::Printf(TEXT("first difference at %s"), *First);
		}
	};

	// ---- §5.1 arena fidelity -------------------------------------------------
	void CheckArenaFidelity(const FRxArenaConfig& L, const FRxArenaConfig& B)
	{
		// regions: id / name / kind / poly / stress / stable / anchored_by, IN ORDER
		{
			FFieldDiff D;
			if (L.Terrain.Regions.Num() != B.Terrain.Regions.Num())
			{
				D.Note(FString::Printf(TEXT("regions.Num (loaded=%d baked=%d)"),
					L.Terrain.Regions.Num(), B.Terrain.Regions.Num()));
			}
			else
			{
				for (int32 i = 0; i < B.Terrain.Regions.Num(); ++i)
				{
					const FRxRegion& RL = L.Terrain.Regions[i];
					const FRxRegion& RB = B.Terrain.Regions[i];
					const FString P = FString::Printf(TEXT("regions[%d]"), i);
					D.Eq(P + TEXT(".id"), RL.Id, RB.Id);
					D.Eq(P + TEXT(".name"), RL.Name, RB.Name);
					D.Eq(P + TEXT(".kind"), RL.Kind, RB.Kind);
					D.Eq(P + TEXT(".stress"), RL.Stress, RB.Stress);
					D.Eq(P + TEXT(".stable"), RL.bStable, RB.bStable);
					D.Eq(P + TEXT(".anchored_by"), RL.AnchoredBy, RB.AnchoredBy);
					if (RL.Poly.Num() != RB.Poly.Num())
					{
						D.Note(P + TEXT(".poly.Num"));
					}
					else
					{
						for (int32 v = 0; v < RB.Poly.Num(); ++v)
						{
							D.Eq(FString::Printf(TEXT("%s.poly[%d]"), *P, v), RL.Poly[v], RB.Poly[v]);
						}
					}
				}
			}
			DataCheck(TEXT("fidelity arena.regions"), D.bOk,
				D.Detail(FString::Printf(TEXT("%d regions identical (id/name/kind/poly/stress/stable/anchored_by, in order)"),
					B.Terrain.Regions.Num())));
		}

		// edges: order is load-bearing (diffusion order + BFS layering)
		{
			FFieldDiff D;
			if (L.Terrain.Edges.Num() != B.Terrain.Edges.Num())
			{
				D.Note(FString::Printf(TEXT("edges.Num (loaded=%d baked=%d)"),
					L.Terrain.Edges.Num(), B.Terrain.Edges.Num()));
			}
			else
			{
				for (int32 i = 0; i < B.Terrain.Edges.Num(); ++i)
				{
					if (L.Terrain.Edges[i].A != B.Terrain.Edges[i].A ||
						L.Terrain.Edges[i].B != B.Terrain.Edges[i].B)
					{
						D.Note(FString::Printf(TEXT("edges[%d]"), i));
					}
				}
			}
			DataCheck(TEXT("fidelity arena.edges (ordered)"), D.bOk,
				D.Detail(FString::Printf(TEXT("%d edges identical in document order"), B.Terrain.Edges.Num())));
		}

		// stress_schedule: order is hashed in the terrain snapshot
		{
			FFieldDiff D;
			if (L.Terrain.StressSchedule.Num() != B.Terrain.StressSchedule.Num())
			{
				D.Note(FString::Printf(TEXT("stress_schedule.Num (loaded=%d baked=%d)"),
					L.Terrain.StressSchedule.Num(), B.Terrain.StressSchedule.Num()));
			}
			else
			{
				for (int32 i = 0; i < B.Terrain.StressSchedule.Num(); ++i)
				{
					const FRxStressEvent& SL = L.Terrain.StressSchedule[i];
					const FRxStressEvent& SB = B.Terrain.StressSchedule[i];
					const FString P = FString::Printf(TEXT("stress_schedule[%d]"), i);
					D.Eq(P + TEXT(".tick"), SL.Tick, SB.Tick);
					D.Eq(P + TEXT(".region"), SL.Region, SB.Region);
					D.Eq(P + TEXT(".rate"), SL.Rate, SB.Rate);
					D.Eq(P + TEXT(".until"), SL.Until, SB.Until);
				}
			}
			DataCheck(TEXT("fidelity arena.stress_schedule (ordered)"), D.bOk,
				D.Detail(FString::Printf(TEXT("%d events identical in document order"),
					B.Terrain.StressSchedule.Num())));
		}

		// spawns: all three, and spawn order fixes entity ids 1/2/3
		{
			FFieldDiff D;
			D.Eq(TEXT("spawns.player"), L.SpawnPlayer, B.SpawnPlayer);
			D.Eq(TEXT("spawns.companion"), L.SpawnCompanion, B.SpawnCompanion);
			D.Eq(TEXT("spawns.boss"), L.SpawnBoss, B.SpawnBoss);
			DataCheck(TEXT("fidelity arena.spawns"), D.bOk,
				D.Detail(FString::Printf(TEXT("player=(%d,%d) companion=(%d,%d) boss=(%d,%d)"),
					L.SpawnPlayer.X, L.SpawnPlayer.Y, L.SpawnCompanion.X, L.SpawnCompanion.Y,
					L.SpawnBoss.X, L.SpawnBoss.Y)));
		}

		// boss: anchor_region / stability / arena_regions (ordered)
		{
			FFieldDiff D;
			D.Eq(TEXT("boss.anchor_region"), L.BossAnchorRegion, B.BossAnchorRegion);
			D.Eq(TEXT("boss.stability"), L.BossStability, B.BossStability);
			if (L.BossArenaRegions.Num() != B.BossArenaRegions.Num())
			{
				D.Note(TEXT("boss.arena_regions.Num"));
			}
			else
			{
				for (int32 i = 0; i < B.BossArenaRegions.Num(); ++i)
				{
					D.Eq(FString::Printf(TEXT("boss.arena_regions[%d]"), i),
						L.BossArenaRegions[i], B.BossArenaRegions[i]);
				}
			}
			DataCheck(TEXT("fidelity arena.boss"), D.bOk,
				D.Detail(FString::Printf(TEXT("anchor_region=%d stability=%d arena_regions=%d entries"),
					L.BossAnchorRegion, L.BossStability, L.BossArenaRegions.Num())));
		}

		// transfer region (exit_bridge)
		{
			const bool bOk = L.TransferRegion == B.TransferRegion;
			DataCheck(TEXT("fidelity arena.transfer_region"), bOk,
				FString::Printf(TEXT("loaded=%d baked=%d"), L.TransferRegion, B.TransferRegion));
		}

		// whole-terrain canonical JSON: one hash over regions+edges+schedule that
		// is sensitive to VALUES and ORDER simultaneously (belt and braces on top
		// of the field-by-field walk above).
		{
			FRxTerrain TL, TB;
			TL.LoadDef(L.Terrain);
			TB.LoadDef(B.Terrain);
			const FString HL = FRxCanonJson::Sha256Hex(TL.ToCanonicalJson());
			const FString HB = FRxCanonJson::Sha256Hex(TB.ToCanonicalJson());
			DataCheck(TEXT("fidelity arena.terrain_canonical_hash"), HL == HB,
				FString::Printf(TEXT("loaded=%s baked=%s"), *HL.Left(16), *HB.Left(16)));
		}
	}

	// ---- §5.1 fragment fidelity ---------------------------------------------
	void CheckFragmentFidelity(const FRxFragmentSpec& L, const FRxFragmentSpec& B)
	{
		// compiler scalars + ordered checks_run
		{
			FFieldDiff D;
			D.Eq(TEXT("compiler.notes"), L.Compiler.Notes, B.Compiler.Notes);
			D.Eq(TEXT("compiler.ref_commit"), L.Compiler.RefCommit, B.Compiler.RefCommit);
			D.Eq(TEXT("compiler.repo"), L.Compiler.Repo, B.Compiler.Repo);
			D.Eq(TEXT("compiler.tool"), L.Compiler.Tool, B.Compiler.Tool);
			D.Eq(TEXT("compiler.version"), L.Compiler.Version, B.Compiler.Version);
			if (L.Compiler.ChecksRun.Num() != B.Compiler.ChecksRun.Num())
			{
				D.Note(TEXT("compiler.checks_run.Num"));
			}
			else
			{
				for (int32 i = 0; i < B.Compiler.ChecksRun.Num(); ++i)
				{
					D.Eq(FString::Printf(TEXT("compiler.checks_run[%d]"), i),
						L.Compiler.ChecksRun[i], B.Compiler.ChecksRun[i]);
				}
			}
			DataCheck(TEXT("fidelity fragment.compiler"), D.bOk,
				D.Detail(FString::Printf(TEXT("%d checks_run in order + notes/ref_commit/repo/tool/version"),
					B.Compiler.ChecksRun.Num())));
		}

		// vendored_sha map (contract §5.1 calls this out explicitly): same size,
		// same keys, same values — compared in BOTH directions.
		{
			FFieldDiff D;
			if (L.Compiler.VendoredSha.Num() != B.Compiler.VendoredSha.Num())
			{
				D.Note(FString::Printf(TEXT("vendored_sha.Num (loaded=%d baked=%d)"),
					L.Compiler.VendoredSha.Num(), B.Compiler.VendoredSha.Num()));
			}
			for (const TPair<FString, FString>& Kv : B.Compiler.VendoredSha)
			{
				const FString* Got = L.Compiler.VendoredSha.Find(Kv.Key);
				if (Got == nullptr) { D.Note(TEXT("vendored_sha['") + Kv.Key + TEXT("'] missing in loaded")); }
				else if (*Got != Kv.Value) { D.Note(TEXT("vendored_sha['") + Kv.Key + TEXT("']")); }
			}
			for (const TPair<FString, FString>& Kv : L.Compiler.VendoredSha)
			{
				if (B.Compiler.VendoredSha.Find(Kv.Key) == nullptr)
				{
					D.Note(TEXT("vendored_sha['") + Kv.Key + TEXT("'] extra in loaded"));
				}
			}
			DataCheck(TEXT("fidelity fragment.vendored_sha"), D.bOk,
				D.Detail(FString::Printf(TEXT("%d path->sha entries identical"), B.Compiler.VendoredSha.Num())));
		}

		// SHENRON §6 canon fields + validity + ordered transfer_domains
		{
			FFieldDiff D;
			D.Eq(TEXT("valid"), L.bValid, B.bValid);
			D.Eq(TEXT("counterplay"), L.Counterplay, B.Counterplay);
			D.Eq(TEXT("propagation"), L.Propagation, B.Propagation);
			D.Eq(TEXT("residual_risk"), L.ResidualRisk, B.ResidualRisk);
			D.Eq(TEXT("trigger"), L.Trigger, B.Trigger);
			if (L.TransferDomains.Num() != B.TransferDomains.Num())
			{
				D.Note(TEXT("transfer_domains.Num"));
			}
			else
			{
				for (int32 i = 0; i < B.TransferDomains.Num(); ++i)
				{
					D.Eq(FString::Printf(TEXT("transfer_domains[%d]"), i),
						L.TransferDomains[i], B.TransferDomains[i]);
				}
			}
			DataCheck(TEXT("fidelity fragment.canon_fields"), D.bOk,
				D.Detail(FString::Printf(TEXT("valid/counterplay/propagation/residual_risk/trigger + %d transfer_domains"),
					B.TransferDomains.Num())));
		}

		// the fragment hash STRING (contract §5.1 calls this out explicitly)
		{
			const bool bOk = L.FragmentHash == B.FragmentHash;
			DataCheck(TEXT("fidelity fragment.fragment_hash"), bOk,
				FString::Printf(TEXT("loaded=%s"), *L.FragmentHash));
		}

		// canonical-JSON hash of the whole socketed artifact — the exact bytes the
		// sim would hash if this fragment were socketed.
		{
			const FString HL = FRxCanonJson::HashValue(L.ToJson());
			const FString HB = FRxCanonJson::HashValue(B.ToJson());
			DataCheck(TEXT("fidelity fragment.canonical_hash"), HL == HB,
				FString::Printf(TEXT("loaded=%s baked=%s"), *HL.Left(16), *HB.Left(16)));
		}
	}

	// ---- §5.1 skill-template fidelity ---------------------------------------
	void CheckSkillFidelity(const FRxSkillSpec& L, const FRxSkillSpec& B)
	{
		FFieldDiff D;
		D.Eq(TEXT("name"), L.Name, B.Name);
		D.Eq(TEXT("trigger"), L.Trigger, B.Trigger);
		D.Eq(TEXT("effect"), L.Effect, B.Effect);
		D.Eq(TEXT("cost"), L.Cost, B.Cost);
		D.Eq(TEXT("cooldown"), L.Cooldown, B.Cooldown);
		D.Eq(TEXT("commit_window"), L.CommitWindow, B.CommitWindow);
		DataCheck(TEXT("fidelity skill_template"), D.bOk,
			D.Detail(FString::Printf(TEXT("'%s' trigger=%s effect=%s cost=%d cooldown=%d commit_window=%d"),
				*L.Name, *L.Trigger, *L.Effect, L.Cost, L.Cooldown, L.CommitWindow)));
	}

	// ---- §5.3 malformed input must be rejected precisely ---------------------
	// Each case must (a) return false, (b) produce an error naming the offence,
	// and (c) leave the output struct untouched — never a partial load.
	void CheckMalformed(const FString& CaseName, const FString& Path, const FString& MustMention)
	{
		constexpr int32 Sentinel = -987654;
		FRxArenaConfig Out;
		Out.TransferRegion = Sentinel;   // canary for "no partial load"
		FString Error;

		const bool bLoaded = FRxDataSource::LoadArenaFromFile(Path, Out, Error);
		const bool bRejected = !bLoaded;
		const bool bNamed = !Error.IsEmpty() && Error.Contains(MustMention);
		const bool bUntouched = Out.TransferRegion == Sentinel && Out.Terrain.Regions.Num() == 0;

		DataCheck(FString::Printf(TEXT("malformed %s rejected"), *CaseName),
			bRejected && bNamed && bUntouched,
			FString::Printf(TEXT("rejected=%s names('%s')=%s no_partial_load=%s | error: %s"),
				bRejected ? TEXT("yes") : TEXT("NO — ACCEPTED"),
				*MustMention,
				bNamed ? TEXT("yes") : TEXT("no"),
				bUntouched ? TEXT("yes") : TEXT("no"),
				Error.IsEmpty() ? TEXT("(none)") : *Error));
	}

	bool RunDataBoundary()
	{
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ============================================================"));
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE == DATA BOUNDARY (FRxDataSource: on-disk JSON -> sim) =="));
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE ============================================================"));

		DataPassed = 0;
		DataTotal = 0;

		const FString ArenaPath = FRxDataSource::DefaultArenaPath();
		const FString FragmentPath = FRxDataSource::DefaultFragmentPath();
		const FString SkillPath = FRxDataSource::DefaultSkillTemplatePath();
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE data dir: %s"), *FRxDataSource::DataDir());

		// ---- load the three files from disk ----
		FRxArenaConfig LoadedArena;
		FString ArenaError;
		const bool bArenaOk = FRxDataSource::LoadArenaFromFile(ArenaPath, LoadedArena, ArenaError);
		DataCheck(TEXT("load arena_earthquake.json"), bArenaOk,
			bArenaOk ? FString::Printf(TEXT("%d regions, %d edges, %d stress events"),
				LoadedArena.Terrain.Regions.Num(), LoadedArena.Terrain.Edges.Num(),
				LoadedArena.Terrain.StressSchedule.Num())
			: ArenaError);

		FRxFragmentSpec LoadedFragment;
		FString FragmentError;
		const bool bFragOk = FRxDataSource::LoadFragmentFromFile(FragmentPath, LoadedFragment, FragmentError);
		DataCheck(TEXT("load fragment_earthquake.json"), bFragOk,
			bFragOk ? FString::Printf(TEXT("fragment_hash=%s"), *LoadedFragment.FragmentHash.Left(16))
			: FragmentError);

		FRxSkillSpec LoadedSkill;
		FString SkillError;
		const bool bSkillOk = FRxDataSource::LoadSkillTemplateFromFile(SkillPath, LoadedSkill, SkillError);
		DataCheck(TEXT("load skill_faultline_interrupt.json"), bSkillOk,
			bSkillOk ? FString::Printf(TEXT("'%s'"), *LoadedSkill.Name) : SkillError);

		// ---- §5.1 fidelity: loaded == baked, field by field ----
		if (bArenaOk)
		{
			CheckArenaFidelity(LoadedArena, FRxEncounters::LoadArenaBaked());
			RxData::ArenaCfg = LoadedArena;   // used by ACCEPTANCE + ADVERSARIAL
			RxData::bArenaLoaded = true;
		}
		else
		{
			UE_LOG(LogRxOracle, Error, TEXT("ORACLE DATA BOUNDARY: arena load failed — %s"), *ArenaError);
		}
		if (bFragOk)
		{
			CheckFragmentFidelity(LoadedFragment, FRxEncounters::LoadFragmentBaked());
		}
		if (bSkillOk)
		{
			CheckSkillFidelity(LoadedSkill, FRxEncounters::LoadSkillTemplateBaked());
		}

		// ---- §5.3 malformed input rejection ----
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE --- malformed-input rejection (Data/tests) ---"));
		const FString TestDir = FPaths::Combine(FRxDataSource::DataDir(), TEXT("tests"));
		CheckMalformed(TEXT("truncated"),
			FPaths::Combine(TestDir, TEXT("arena_truncated.json")), TEXT("parse failed"));
		CheckMalformed(TEXT("non-integral coord (2000.5)"),
			FPaths::Combine(TestDir, TEXT("arena_nonintegral_coord.json")), TEXT("regions[0].poly[0][0]"));
		CheckMalformed(TEXT("missing key (boss.stability)"),
			FPaths::Combine(TestDir, TEXT("arena_missing_key.json")), TEXT("boss.stability"));

		const bool bOk = (DataPassed == DataTotal) && (DataTotal > 0);
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE DATA BOUNDARY checks: %d/%d passed"), DataPassed, DataTotal);
		UE_LOG(LogRxOracle, Display, TEXT("ORACLE DATA BOUNDARY RESULT: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"));
		return bOk;
	}
}

// ---------------------------------------------------------------------------
// Commandlet entry point
// ---------------------------------------------------------------------------
UReflexionOracleCommandlet::UReflexionOracleCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UReflexionOracleCommandlet::Main(const FString& /*Params*/)
{
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE BEGIN Reflexion Arena UE5.8 parity harness"));

	// The data boundary runs FIRST: it loads the arena from disk into
	// RxData::ArenaCfg, which every subsequent BuildArena consumes.
	const bool bDataBoundary = RunDataBoundary();

	bool bAcceptance = false;
	bool bAdversarial = false;
	if (RxData::bArenaLoaded)
	{
		bAcceptance = RunAcceptance();
		bAdversarial = RunAdversarial();
	}
	else
	{
		// No fallback to the baked config: a green run on hardcoded data while
		// the on-disk load is broken would be a false pass.
		UE_LOG(LogRxOracle, Error,
			TEXT("ORACLE SKIPPING acceptance+adversarial: no arena config could be loaded from disk"));
	}

	UE_LOG(LogRxOracle, Display, TEXT("ORACLE ============================================================"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE == VERDICT =="));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE   data boundary:     %s"), bDataBoundary ? TEXT("PROVEN") : TEXT("FAIL"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE   acceptance parity: %s"), bAcceptance ? TEXT("PROVEN") : TEXT("MISMATCH"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE   adversarial:       %s"), bAdversarial ? TEXT("44/44") : TEXT("NOT 44/44"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE   overall:           %s"),
		(bDataBoundary && bAcceptance && bAdversarial) ? TEXT("PASS") : TEXT("FAIL"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE END"));

	return (bDataBoundary && bAcceptance && bAdversarial) ? 0 : 1;
}

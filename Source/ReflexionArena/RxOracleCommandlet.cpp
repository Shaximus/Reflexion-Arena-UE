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
#include "Sim/RxCommands.h"
#include "Sim/RxReceipts.h"
#include "Sim/RxCompanionAI.h"
#include "Sim/RxCanonJson.h"
#include "Sim/RxTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogRxOracle, Log, All);

// ---------------------------------------------------------------------------
// Reference values from the Godot-faithful Python oracle
// (Reflexion-Arena/tools/oracle/run_acceptance.py, sim_mirror.py). These are the
// ACTUAL computed values of the sim mirror (NOT the stale acceptance_expect
// fixture). Used only to print a self-contained VERDICT; the harness computes its
// own values independently and compares.
// ---------------------------------------------------------------------------
namespace RxRef
{
	static const TCHAR* AcceptanceHash = TEXT("1d4a7ce4f60e4bafc3c242bc33c8b9b1dd241fc9f2efa7ace411e2efac56c581");
	static const TCHAR* ChainHead      = TEXT("6d20643cea8de0136a340f5c12f819fd0f7b4220c2ecd84cf50702d919fb7524");
	static constexpr int32 ReceiptCount = 728;
	// beat name -> expected tick
	static const TArray<TPair<FString, int32>>& Beats()
	{
		static const TArray<TPair<FString, int32>> V = {
			{ TEXT("telegraph"), 5386 },
			{ TEXT("weave_interrupted"), 5790 },
			{ TEXT("DESTABILIZED"), 6424 },
			{ TEXT("DEFEATED"), 6462 },
			{ TEXT("fragment"), 6701 },
			{ TEXT("skill"), 6761 },
			{ TEXT("transfer_recognized"), 6834 },
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
		FRxEncounters::BuildArena(World);

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
		FRxEncounters::BuildArena(World);
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
		FRxEncounters::BuildArena(World);
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
		FRxEncounters::BuildArena(World);

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
		FRxEncounters::BuildArena(World);

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

	const bool bAcceptance = RunAcceptance();
	const bool bAdversarial = RunAdversarial();

	UE_LOG(LogRxOracle, Display, TEXT("ORACLE ============================================================"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE == VERDICT =="));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE   acceptance parity: %s"), bAcceptance ? TEXT("PROVEN") : TEXT("MISMATCH"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE   adversarial:       %s"), bAdversarial ? TEXT("44/44") : TEXT("NOT 44/44"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE   overall:           %s"), (bAcceptance && bAdversarial) ? TEXT("PASS") : TEXT("FAIL"));
	UE_LOG(LogRxOracle, Display, TEXT("ORACLE END"));

	return (bAcceptance && bAdversarial) ? 0 : 1;
}

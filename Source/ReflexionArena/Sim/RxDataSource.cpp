#include "RxDataSource.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/**
 * RxDataSource.cpp — parse + intify + validate for the data boundary.
 * See RxDataSource.h for the contract narrative (RX_DATA_BOUNDARY_CONTRACT.md
 * v1 §2 boundary, §3 intify law, §4 provenance, §5.4 ordering).
 *
 * Structure of every loader:
 *   1. read the whole file (missing/unreadable -> error)
 *   2. parse to an FJsonObject (parse failure -> error with reader detail)
 *   3. WHOLE-DOCUMENT intify validation — mirrors CanonJson.intify() walking the
 *      entire parsed value; any non-integral or out-of-int32 number anywhere
 *      fails the load, reported with its JSON path
 *   4. field extraction, every key REQUIRED, every number via ExactInt
 *
 * Step 3 is deliberately redundant with step 4: it catches floats in parts of
 * the document this port does not currently consume (e.g. "seed"), which is what
 * the Godot reference does. Step 4 is what produces per-field error messages.
 */

// ---------------------------------------------------------------------------
// Local helpers — all failures are loud, specific, and name the JSON path.
// ---------------------------------------------------------------------------
namespace
{
	const TCHAR* JsonTypeName(EJson T)
	{
		switch (T)
		{
		case EJson::None:    return TEXT("none");
		case EJson::Null:    return TEXT("null");
		case EJson::String:  return TEXT("string");
		case EJson::Number:  return TEXT("number");
		case EJson::Boolean: return TEXT("boolean");
		case EJson::Array:   return TEXT("array");
		case EJson::Object:  return TEXT("object");
		default:             return TEXT("unknown");
		}
	}

	// Short display name for error prefixes (full path is noisy in logs).
	FString FileTag(const FString& Path)
	{
		return FPaths::GetCleanFilename(Path);
	}

	bool RxError(const FString& Path, const FString& Message, FString& OutError)
	{
		OutError = FString::Printf(TEXT("%s: %s"), *FileTag(Path), *Message);
		return false;
	}

	bool ReadWholeFile(const FString& Path, FString& OutContent, FString& OutError)
	{
		if (!FPaths::FileExists(Path))
		{
			return RxError(Path, FString::Printf(TEXT("file does not exist (%s)"), *Path), OutError);
		}
		if (!FFileHelper::LoadFileToString(OutContent, *Path))
		{
			return RxError(Path, FString::Printf(TEXT("cannot read file (%s)"), *Path), OutError);
		}
		return true;
	}

	bool ParseRootObject(const FString& Path, const FString& Content,
		TSharedPtr<FJsonObject>& OutRoot, FString& OutError)
	{
		TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Content);
		if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
		{
			const FString Detail = Reader->GetErrorMessage().TrimStartAndEnd();
			return RxError(Path, FString::Printf(
				TEXT("invalid JSON — parse failed: %s"),
				Detail.IsEmpty() ? TEXT("document is not a JSON object") : *Detail), OutError);
		}
		return true;
	}

	// --- the intify law (contract §3), applied to one value -------------------
	bool ExactIntImpl(const FString& Path, double D, const FString& FieldPath,
		int32& Out, FString& OutError)
	{
		if (D != FMath::TruncToDouble(D))
		{
			return RxError(Path, FString::Printf(
				TEXT("non-integral number at '%s' (%s) — the simulation is integer-only (intify law)"),
				*FieldPath, *FString::SanitizeFloat(D)), OutError);
		}
		if (D < static_cast<double>(MIN_int32) || D > static_cast<double>(MAX_int32))
		{
			return RxError(Path, FString::Printf(
				TEXT("number out of int32 range at '%s' (%s)"),
				*FieldPath, *FString::SanitizeFloat(D)), OutError);
		}
		Out = static_cast<int32>(D);
		return true;
	}

	// Whole-document intify validation (mirrors CanonJson.intify walking every
	// value). Also rejects JSON null, which the canonical value set forbids.
	bool ValidateIntegralRecursive(const FString& Path, const TSharedPtr<FJsonValue>& V,
		const FString& JsonPath, FString& OutError)
	{
		if (!V.IsValid())
		{
			return RxError(Path, FString::Printf(TEXT("null/invalid value at '%s'"), *JsonPath), OutError);
		}
		switch (V->Type)
		{
		case EJson::Null:
			return RxError(Path, FString::Printf(
				TEXT("JSON null at '%s' — the canonical value set forbids null"), *JsonPath), OutError);
		case EJson::Number:
		{
			int32 Ignored = 0;
			return ExactIntImpl(Path, V->AsNumber(), JsonPath, Ignored, OutError);
		}
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& Items = V->AsArray();
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				if (!ValidateIntegralRecursive(Path, Items[i],
					FString::Printf(TEXT("%s[%d]"), *JsonPath, i), OutError))
				{
					return false;
				}
			}
			return true;
		}
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid())
			{
				return RxError(Path, FString::Printf(TEXT("malformed object at '%s'"), *JsonPath), OutError);
			}
			for (const auto& Kv : O->Values)
			{
				const FString Key = FString(Kv.Key.ToView());
				const FString Child = JsonPath.IsEmpty() ? Key : (JsonPath + TEXT(".") + Key);
				if (!ValidateIntegralRecursive(Path, Kv.Value, Child, OutError))
				{
					return false;
				}
			}
			return true;
		}
		default:
			return true; // string / bool are canonical as-is
		}
	}

	bool ValidateDocumentIntegral(const FString& Path, const TSharedPtr<FJsonObject>& Root, FString& OutError)
	{
		for (const auto& Kv : Root->Values)
		{
			if (!ValidateIntegralRecursive(Path, Kv.Value, FString(Kv.Key.ToView()), OutError))
			{
				return false;
			}
		}
		return true;
	}

	// --- required-field accessors (NO silent defaults, contract §2.2) ---------

	bool RequireField(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, EJson Expected, TSharedPtr<FJsonValue>& Out, FString& OutError)
	{
		const TSharedPtr<FJsonValue> V = Obj->TryGetField(Key);
		if (!V.IsValid() || V->Type == EJson::Null)
		{
			return RxError(Path, FString::Printf(TEXT("missing required key '%s'"), *JsonPath), OutError);
		}
		if (V->Type != Expected)
		{
			return RxError(Path, FString::Printf(TEXT("field '%s' must be %s, got %s"),
				*JsonPath, JsonTypeName(Expected), JsonTypeName(V->Type)), OutError);
		}
		Out = V;
		return true;
	}

	bool ReqInt(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, int32& Out, FString& OutError)
	{
		TSharedPtr<FJsonValue> V;
		if (!RequireField(Path, Obj, Key, JsonPath, EJson::Number, V, OutError)) { return false; }
		return ExactIntImpl(Path, V->AsNumber(), JsonPath, Out, OutError);
	}

	bool ReqString(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, FString& Out, FString& OutError)
	{
		TSharedPtr<FJsonValue> V;
		if (!RequireField(Path, Obj, Key, JsonPath, EJson::String, V, OutError)) { return false; }
		Out = V->AsString();
		return true;
	}

	bool ReqBool(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, bool& Out, FString& OutError)
	{
		TSharedPtr<FJsonValue> V;
		if (!RequireField(Path, Obj, Key, JsonPath, EJson::Boolean, V, OutError)) { return false; }
		Out = V->AsBool();
		return true;
	}

	// Ordered array — returned as the parser's TArray, i.e. exact document order.
	bool ReqArray(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, const TArray<TSharedPtr<FJsonValue>>*& Out, FString& OutError)
	{
		TSharedPtr<FJsonValue> V;
		if (!RequireField(Path, Obj, Key, JsonPath, EJson::Array, V, OutError)) { return false; }
		Out = &V->AsArray();
		return true;
	}

	bool ReqObject(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, TSharedPtr<FJsonObject>& Out, FString& OutError)
	{
		TSharedPtr<FJsonValue> V;
		if (!RequireField(Path, Obj, Key, JsonPath, EJson::Object, V, OutError)) { return false; }
		Out = V->AsObject();
		if (!Out.IsValid())
		{
			return RxError(Path, FString::Printf(TEXT("field '%s' is a malformed object"), *JsonPath), OutError);
		}
		return true;
	}

	// Element accessors (for values living inside arrays rather than objects).
	bool ElemAsObject(const FString& Path, const TSharedPtr<FJsonValue>& V,
		const FString& JsonPath, TSharedPtr<FJsonObject>& Out, FString& OutError)
	{
		if (!V.IsValid() || V->Type != EJson::Object)
		{
			return RxError(Path, FString::Printf(TEXT("element '%s' must be an object, got %s"),
				*JsonPath, V.IsValid() ? JsonTypeName(V->Type) : TEXT("none")), OutError);
		}
		Out = V->AsObject();
		if (!Out.IsValid())
		{
			return RxError(Path, FString::Printf(TEXT("element '%s' is a malformed object"), *JsonPath), OutError);
		}
		return true;
	}

	bool ElemAsInt(const FString& Path, const TSharedPtr<FJsonValue>& V,
		const FString& JsonPath, int32& Out, FString& OutError)
	{
		if (!V.IsValid() || V->Type != EJson::Number)
		{
			return RxError(Path, FString::Printf(TEXT("element '%s' must be a number, got %s"),
				*JsonPath, V.IsValid() ? JsonTypeName(V->Type) : TEXT("none")), OutError);
		}
		return ExactIntImpl(Path, V->AsNumber(), JsonPath, Out, OutError);
	}

	bool ElemAsString(const FString& Path, const TSharedPtr<FJsonValue>& V,
		const FString& JsonPath, FString& Out, FString& OutError)
	{
		if (!V.IsValid() || V->Type != EJson::String)
		{
			return RxError(Path, FString::Printf(TEXT("element '%s' must be a string, got %s"),
				*JsonPath, V.IsValid() ? JsonTypeName(V->Type) : TEXT("none")), OutError);
		}
		Out = V->AsString();
		return true;
	}

	// [x, y] milli-unit point (Vector2i mirror). Exactly two integral entries.
	bool ElemAsIntPoint(const FString& Path, const TSharedPtr<FJsonValue>& V,
		const FString& JsonPath, FIntPoint& Out, FString& OutError)
	{
		if (!V.IsValid() || V->Type != EJson::Array)
		{
			return RxError(Path, FString::Printf(TEXT("element '%s' must be an [x,y] array, got %s"),
				*JsonPath, V.IsValid() ? JsonTypeName(V->Type) : TEXT("none")), OutError);
		}
		const TArray<TSharedPtr<FJsonValue>>& Items = V->AsArray();
		if (Items.Num() != 2)
		{
			return RxError(Path, FString::Printf(TEXT("element '%s' must have exactly 2 entries, got %d"),
				*JsonPath, Items.Num()), OutError);
		}
		int32 X = 0, Y = 0;
		if (!ElemAsInt(Path, Items[0], FString::Printf(TEXT("%s[0]"), *JsonPath), X, OutError)) { return false; }
		if (!ElemAsInt(Path, Items[1], FString::Printf(TEXT("%s[1]"), *JsonPath), Y, OutError)) { return false; }
		Out = FIntPoint(X, Y);
		return true;
	}

	// Point living under an object key (spawns.player etc.).
	bool ReqIntPoint(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, FIntPoint& Out, FString& OutError)
	{
		TSharedPtr<FJsonValue> V;
		if (!RequireField(Path, Obj, Key, JsonPath, EJson::Array, V, OutError)) { return false; }
		return ElemAsIntPoint(Path, V, JsonPath, Out, OutError);
	}

	bool ReqStringArray(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, TArray<FString>& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!ReqArray(Path, Obj, Key, JsonPath, Items, OutError)) { return false; }
		TArray<FString> Built;
		Built.Reserve(Items->Num());
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			FString S;
			if (!ElemAsString(Path, (*Items)[i], FString::Printf(TEXT("%s[%d]"), *JsonPath, i), S, OutError))
			{
				return false;
			}
			Built.Add(MoveTemp(S)); // document order preserved
		}
		Out = MoveTemp(Built);
		return true;
	}

	bool ReqIntArray(const FString& Path, const TSharedPtr<FJsonObject>& Obj, const FString& Key,
		const FString& JsonPath, TArray<int32>& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!ReqArray(Path, Obj, Key, JsonPath, Items, OutError)) { return false; }
		TArray<int32> Built;
		Built.Reserve(Items->Num());
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			int32 N = 0;
			if (!ElemAsInt(Path, (*Items)[i], FString::Printf(TEXT("%s[%d]"), *JsonPath, i), N, OutError))
			{
				return false;
			}
			Built.Add(N); // document order preserved
		}
		Out = MoveTemp(Built);
		return true;
	}
}

// ---------------------------------------------------------------------------
// Public intify entry point (contract §3)
// ---------------------------------------------------------------------------
bool FRxDataSource::ExactInt(double D, const FString& FieldPath, int32& Out, FString& OutError)
{
	return ExactIntImpl(TEXT("<value>"), D, FieldPath, Out, OutError);
}

// ---------------------------------------------------------------------------
// Default data locations (contract §4)
// ---------------------------------------------------------------------------
FString FRxDataSource::DataDir()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data"));
}

FString FRxDataSource::DefaultArenaPath()
{
	return FPaths::Combine(DataDir(), TEXT("arena_earthquake.json"));
}

FString FRxDataSource::DefaultFragmentPath()
{
	return FPaths::Combine(DataDir(), TEXT("fragment_earthquake.json"));
}

FString FRxDataSource::DefaultSkillTemplatePath()
{
	return FPaths::Combine(DataDir(), TEXT("skill_faultline_interrupt.json"));
}

// ---------------------------------------------------------------------------
// LoadArenaFromFile — arena_earthquake.json -> FRxArenaConfig
//
// Ordered collections (regions / each region's poly / edges / stress_schedule /
// boss.arena_regions) are read straight out of the parser's TArray, so document
// order is preserved exactly. Nothing is sorted; no unordered container is used.
// ---------------------------------------------------------------------------
bool FRxDataSource::LoadArenaFromFile(const FString& Path, FRxArenaConfig& Out, FString& OutError)
{
	FString Content;
	if (!ReadWholeFile(Path, Content, OutError)) { return false; }

	TSharedPtr<FJsonObject> Root;
	if (!ParseRootObject(Path, Content, Root, OutError)) { return false; }

	// Intify law, whole document (mirrors CanonJson.intify).
	if (!ValidateDocumentIntegral(Path, Root, OutError)) { return false; }

	// Build into a local so a failure NEVER leaves Out partially populated.
	FRxArenaConfig Cfg;

	// --- regions (document order == region-array order == iteration order) ---
	{
		const TArray<TSharedPtr<FJsonValue>>* Regions = nullptr;
		if (!ReqArray(Path, Root, TEXT("regions"), TEXT("regions"), Regions, OutError)) { return false; }
		if (Regions->Num() == 0)
		{
			return RxError(Path, TEXT("field 'regions' must not be empty"), OutError);
		}
		Cfg.Terrain.Regions.Reserve(Regions->Num());
		for (int32 i = 0; i < Regions->Num(); ++i)
		{
			const FString RPath = FString::Printf(TEXT("regions[%d]"), i);
			TSharedPtr<FJsonObject> RO;
			if (!ElemAsObject(Path, (*Regions)[i], RPath, RO, OutError)) { return false; }

			FRxRegion R;
			if (!ReqInt(Path, RO, TEXT("id"), RPath + TEXT(".id"), R.Id, OutError)) { return false; }
			if (!ReqString(Path, RO, TEXT("name"), RPath + TEXT(".name"), R.Name, OutError)) { return false; }
			if (!ReqString(Path, RO, TEXT("kind"), RPath + TEXT(".kind"), R.Kind, OutError)) { return false; }
			if (!ReqInt(Path, RO, TEXT("stress"), RPath + TEXT(".stress"), R.Stress, OutError)) { return false; }
			if (!ReqBool(Path, RO, TEXT("stable"), RPath + TEXT(".stable"), R.bStable, OutError)) { return false; }
			if (!ReqInt(Path, RO, TEXT("anchored_by"), RPath + TEXT(".anchored_by"), R.AnchoredBy, OutError)) { return false; }

			const TArray<TSharedPtr<FJsonValue>>* Poly = nullptr;
			const FString PolyPath = RPath + TEXT(".poly");
			if (!ReqArray(Path, RO, TEXT("poly"), PolyPath, Poly, OutError)) { return false; }
			if (Poly->Num() < 3)
			{
				return RxError(Path, FString::Printf(
					TEXT("field '%s' must have at least 3 vertices, got %d"), *PolyPath, Poly->Num()), OutError);
			}
			R.Poly.Reserve(Poly->Num());
			for (int32 v = 0; v < Poly->Num(); ++v)
			{
				FIntPoint P;
				if (!ElemAsIntPoint(Path, (*Poly)[v],
					FString::Printf(TEXT("%s[%d]"), *PolyPath, v), P, OutError))
				{
					return false;
				}
				R.Poly.Add(P); // vertex order preserved (point-in-poly is order sensitive)
			}
			Cfg.Terrain.Regions.Add(MoveTemp(R)); // region order preserved
		}
	}

	// --- edges (edge-array order drives diffusion + BFS layering) ---
	{
		const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
		if (!ReqArray(Path, Root, TEXT("edges"), TEXT("edges"), Edges, OutError)) { return false; }
		Cfg.Terrain.Edges.Reserve(Edges->Num());
		for (int32 i = 0; i < Edges->Num(); ++i)
		{
			const FString EPath = FString::Printf(TEXT("edges[%d]"), i);
			FIntPoint Pair;
			if (!ElemAsIntPoint(Path, (*Edges)[i], EPath, Pair, OutError)) { return false; }
			Cfg.Terrain.Edges.Add(FRxEdge(Pair.X, Pair.Y)); // edge order preserved
		}
	}

	// --- stress_schedule (schedule order is hashed in the terrain snapshot) ---
	{
		const TArray<TSharedPtr<FJsonValue>>* Sched = nullptr;
		if (!ReqArray(Path, Root, TEXT("stress_schedule"), TEXT("stress_schedule"), Sched, OutError)) { return false; }
		Cfg.Terrain.StressSchedule.Reserve(Sched->Num());
		for (int32 i = 0; i < Sched->Num(); ++i)
		{
			const FString SPath = FString::Printf(TEXT("stress_schedule[%d]"), i);
			TSharedPtr<FJsonObject> SO;
			if (!ElemAsObject(Path, (*Sched)[i], SPath, SO, OutError)) { return false; }

			FRxStressEvent Ev;
			if (!ReqInt(Path, SO, TEXT("tick"), SPath + TEXT(".tick"), Ev.Tick, OutError)) { return false; }
			if (!ReqInt(Path, SO, TEXT("region"), SPath + TEXT(".region"), Ev.Region, OutError)) { return false; }
			if (!ReqInt(Path, SO, TEXT("rate"), SPath + TEXT(".rate"), Ev.Rate, OutError)) { return false; }
			if (!ReqInt(Path, SO, TEXT("until"), SPath + TEXT(".until"), Ev.Until, OutError)) { return false; }
			Cfg.Terrain.StressSchedule.Add(Ev); // schedule order preserved
		}
	}

	// --- spawns (spawn ORDER is fixed by BuildArena: player=1, companion=2, boss=3) ---
	{
		TSharedPtr<FJsonObject> Spawns;
		if (!ReqObject(Path, Root, TEXT("spawns"), TEXT("spawns"), Spawns, OutError)) { return false; }
		if (!ReqIntPoint(Path, Spawns, TEXT("player"), TEXT("spawns.player"), Cfg.SpawnPlayer, OutError)) { return false; }
		if (!ReqIntPoint(Path, Spawns, TEXT("companion"), TEXT("spawns.companion"), Cfg.SpawnCompanion, OutError)) { return false; }
		if (!ReqIntPoint(Path, Spawns, TEXT("boss"), TEXT("spawns.boss"), Cfg.SpawnBoss, OutError)) { return false; }
	}

	// --- boss ---
	{
		TSharedPtr<FJsonObject> Boss;
		if (!ReqObject(Path, Root, TEXT("boss"), TEXT("boss"), Boss, OutError)) { return false; }
		if (!ReqInt(Path, Boss, TEXT("anchor_region"), TEXT("boss.anchor_region"), Cfg.BossAnchorRegion, OutError)) { return false; }
		if (!ReqInt(Path, Boss, TEXT("stability"), TEXT("boss.stability"), Cfg.BossStability, OutError)) { return false; }
		if (!ReqIntArray(Path, Boss, TEXT("arena_regions"), TEXT("boss.arena_regions"), Cfg.BossArenaRegions, OutError)) { return false; }
	}

	// --- transfer region (exit_bridge) ---
	if (!ReqInt(Path, Root, TEXT("transfer_region"), TEXT("transfer_region"), Cfg.TransferRegion, OutError)) { return false; }

	Out = MoveTemp(Cfg);
	OutError.Reset();
	return true;
}

// ---------------------------------------------------------------------------
// LoadFragmentFromFile — fragment_earthquake.json -> FRxFragmentSpec
// ---------------------------------------------------------------------------
bool FRxDataSource::LoadFragmentFromFile(const FString& Path, FRxFragmentSpec& Out, FString& OutError)
{
	FString Content;
	if (!ReadWholeFile(Path, Content, OutError)) { return false; }

	TSharedPtr<FJsonObject> Root;
	if (!ParseRootObject(Path, Content, Root, OutError)) { return false; }

	if (!ValidateDocumentIntegral(Path, Root, OutError)) { return false; }

	FRxFragmentSpec F;

	// --- compiler provenance block ---
	{
		TSharedPtr<FJsonObject> C;
		if (!ReqObject(Path, Root, TEXT("compiler"), TEXT("compiler"), C, OutError)) { return false; }

		if (!ReqStringArray(Path, C, TEXT("checks_run"), TEXT("compiler.checks_run"), F.Compiler.ChecksRun, OutError)) { return false; }
		if (!ReqString(Path, C, TEXT("notes"), TEXT("compiler.notes"), F.Compiler.Notes, OutError)) { return false; }
		if (!ReqString(Path, C, TEXT("ref_commit"), TEXT("compiler.ref_commit"), F.Compiler.RefCommit, OutError)) { return false; }
		if (!ReqString(Path, C, TEXT("repo"), TEXT("compiler.repo"), F.Compiler.Repo, OutError)) { return false; }
		if (!ReqString(Path, C, TEXT("tool"), TEXT("compiler.tool"), F.Compiler.Tool, OutError)) { return false; }
		if (!ReqString(Path, C, TEXT("version"), TEXT("compiler.version"), F.Compiler.Version, OutError)) { return false; }

		// vendored_sha: path -> sha. The destination is a TMap because the sim
		// type is a TMap; it is a keyed LOOKUP, never an ordered iteration —
		// FRxCanonJson sorts object keys at stringify time, so map order can
		// never reach a hash.
		TSharedPtr<FJsonObject> VS;
		if (!ReqObject(Path, C, TEXT("vendored_sha"), TEXT("compiler.vendored_sha"), VS, OutError)) { return false; }
		for (const auto& Kv : VS->Values)
		{
			const FString Key = FString(Kv.Key.ToView());
			FString Sha;
			if (!ElemAsString(Path, Kv.Value,
				FString::Printf(TEXT("compiler.vendored_sha.%s"), *Key), Sha, OutError))
			{
				return false;
			}
			F.Compiler.VendoredSha.Add(Key, Sha);
		}
		if (F.Compiler.VendoredSha.Num() == 0)
		{
			return RxError(Path, TEXT("field 'compiler.vendored_sha' must not be empty"), OutError);
		}
	}

	// --- SHENRON §6 canon fields ---
	if (!ReqString(Path, Root, TEXT("counterplay"), TEXT("counterplay"), F.Counterplay, OutError)) { return false; }
	if (!ReqString(Path, Root, TEXT("fragment_hash"), TEXT("fragment_hash"), F.FragmentHash, OutError)) { return false; }
	if (!ReqString(Path, Root, TEXT("propagation"), TEXT("propagation"), F.Propagation, OutError)) { return false; }
	if (!ReqString(Path, Root, TEXT("residual_risk"), TEXT("residual_risk"), F.ResidualRisk, OutError)) { return false; }
	if (!ReqStringArray(Path, Root, TEXT("transfer_domains"), TEXT("transfer_domains"), F.TransferDomains, OutError)) { return false; }
	if (!ReqString(Path, Root, TEXT("trigger"), TEXT("trigger"), F.Trigger, OutError)) { return false; }

	F.bValid = true; // mirrors "the parsed Dictionary is non-empty"

	Out = MoveTemp(F);
	OutError.Reset();
	return true;
}

// ---------------------------------------------------------------------------
// LoadSkillTemplateFromFile — skill_faultline_interrupt.json -> FRxSkillSpec
//
// The authoring-REQUEST subset only (name/trigger/effect/cost/cooldown/
// commit_window). The template's skill_id / derived_from / legal_options /
// residual_risk / authority belong to FRxSkillArtifact, which
// FRxSkillSystem::AuthorSkill produces from this spec — see RxEncounters.h.
// ---------------------------------------------------------------------------
bool FRxDataSource::LoadSkillTemplateFromFile(const FString& Path, FRxSkillSpec& Out, FString& OutError)
{
	FString Content;
	if (!ReadWholeFile(Path, Content, OutError)) { return false; }

	TSharedPtr<FJsonObject> Root;
	if (!ParseRootObject(Path, Content, Root, OutError)) { return false; }

	if (!ValidateDocumentIntegral(Path, Root, OutError)) { return false; }

	FRxSkillSpec S;
	if (!ReqString(Path, Root, TEXT("name"), TEXT("name"), S.Name, OutError)) { return false; }
	if (!ReqString(Path, Root, TEXT("trigger"), TEXT("trigger"), S.Trigger, OutError)) { return false; }
	if (!ReqString(Path, Root, TEXT("effect"), TEXT("effect"), S.Effect, OutError)) { return false; }
	if (!ReqInt(Path, Root, TEXT("cost"), TEXT("cost"), S.Cost, OutError)) { return false; }
	if (!ReqInt(Path, Root, TEXT("cooldown"), TEXT("cooldown"), S.Cooldown, OutError)) { return false; }
	if (!ReqInt(Path, Root, TEXT("commit_window"), TEXT("commit_window"), S.CommitWindow, OutError)) { return false; }

	Out = MoveTemp(S);
	OutError.Reset();
	return true;
}

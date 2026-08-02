#pragma once

#include "CoreMinimal.h"
#include "RxEncounters.h"    // FRxArenaConfig (the parsed arena definition)
#include "RxSkillSystem.h"   // FRxFragmentSpec / FRxSkillSpec

/**
 * RxDataSource.h — the DATA BOUNDARY (RX_DATA_BOUNDARY_CONTRACT.md v1 §2).
 *
 * Splits *where data comes from* away from *what the sim does with it*:
 *
 *     on-disk JSON  ->  FRxDataSource (parse + intify + validate)  ->  FRxArenaConfig
 *                                                                          |
 *                                                                          v
 *                                                    FRxEncounters::BuildArena(World, Cfg)
 *
 * The deterministic sim core never opens a file. `BuildArena` is a pure function
 * of its config, which is what makes a second universe (Rx Worlds) possible
 * without touching sim code.
 *
 * THE INTIFY LAW (contract §3). The Godot reference runs every parsed value
 * through `CanonJson.intify()` before it reaches the sim, because the sim is
 * integer-only. UE's JSON parser yields `double`. Every simulation-affecting
 * number is therefore converted to int32 through an EXACT round-trip check
 * (see ExactInt); a non-integral or out-of-range value FAILS THE LOAD LOUDLY.
 * A float sneaking into the sim is exactly the class of bug that silently
 * breaks determinism months later — fail at load, not at hash-compare.
 *
 * NO SILENT DEFAULTS (contract §2.2). Unlike the Godot loader (which uses
 * `Dictionary.get(key, default)` throughout), every key consumed here is
 * REQUIRED. A missing key is an error naming the exact JSON path
 * (e.g. `boss.stability`), never a partial load and never a default.
 *
 * ORDER IS LOAD-BEARING (contract §5.4). Region order, edge order,
 * stress-schedule order and spawn order determine entity ids and iteration
 * order, both of which are hashed. Every ordered collection is read straight
 * out of the JSON array (a TArray) in document order: nothing is sorted and no
 * unordered container appears anywhere in the load path.
 */
class FRxDataSource
{
public:
	// -----------------------------------------------------------------------
	// Loaders (contract §2.2). Each returns false and fills OutError on ANY
	// problem. No silent defaults, ever. On failure Out is left untouched — a
	// failed load never produces a partially-populated config.
	// -----------------------------------------------------------------------

	/** arena_earthquake.json -> FRxArenaConfig (terrain + spawns + boss + transfer). */
	static bool LoadArenaFromFile(const FString& Path, FRxArenaConfig& Out, FString& OutError);

	/** fragment_earthquake.json -> FRxFragmentSpec (SHENRON §6 canon fields). */
	static bool LoadFragmentFromFile(const FString& Path, FRxFragmentSpec& Out, FString& OutError);

	/** skill_faultline_interrupt.json -> FRxSkillSpec (authoring-request subset). */
	static bool LoadSkillTemplateFromFile(const FString& Path, FRxSkillSpec& Out, FString& OutError);

	// -----------------------------------------------------------------------
	// Default on-disk locations (contract §4: the UE project is self-contained;
	// Data/PROVENANCE.json records the SHA-256 of each file and of its Godot
	// original so drift between the two repos is detectable rather than silent).
	// -----------------------------------------------------------------------
	static FString DataDir();
	static FString DefaultArenaPath();
	static FString DefaultFragmentPath();
	static FString DefaultSkillTemplatePath();

	/**
	 * The intify law, exactly as specified in contract §3. Converts a parsed
	 * JSON double to int32 iff the round trip is exact; otherwise fails with an
	 * error naming FieldPath.
	 */
	static bool ExactInt(double D, const FString& FieldPath, int32& Out, FString& OutError);
};

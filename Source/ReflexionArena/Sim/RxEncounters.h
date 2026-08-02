#pragma once

#include "CoreMinimal.h"
#include "RxTerrain.h"       // FRxTerrainDef / FRxRegion / FRxEdge / FRxStressEvent (built for FRxTerrain::LoadDef)
#include "RxSkillSystem.h"   // FRxFragmentSpec / FRxSkillSpec (LoadFragment / LoadSkillTemplate return these)

/**
 * RxEncounters.h — UE5.8 port of the Godot reference
 * (Reflexion-Arena/game/sim/encounters.gd, class_name Encounters).
 *
 * Arena + transfer setup, static. Ported 1:1 to preserve behavior and
 * replay/hash parity across the engine switch (CONTRACTS.md §0 determinism law,
 * §3 "Encounters & flow", §5 data formats; SHENRON §5 arena, §8 transfer).
 *
 * BuildArena (mirrors build_arena): loads the arena definition (regions ->
 * spawn_path -> bridge W/E supports -> approach -> arena ring N/E/S/W around the
 * boss_anchor pillar -> exit_bridge), loads it into the terrain, spawns
 * player/companion/boss entities (sequential ids 1,2,3 — ADR-0005, order is
 * load-bearing for determinism), configures the boss FSM, wires the id fields,
 * binds the CompanionAI, and emits encounter_ready.
 *
 * BuildTransfer (mirrors build_transfer, SHENRON §8): post-boss reconfiguration —
 * the exit_bridge starts accumulating stress (collapsing). Sets transfer_active,
 * pre-stresses the exit_bridge (TRANSFER_START_STRESS) and emits transfer_begin.
 *
 * Conventions (CONTRACTS.md §0): plain C++ class (NOT a UObject/AActor),
 * CoreMinimal only, integer math, deterministic. All positions are milli-units
 * (Vector2i -> FIntPoint). Encounters mutates world SETUP through FRxSimWorld&
 * (forward-declared; owner reconciles the interface — see the call list reported
 * at integration).
 *
 * DATA SOURCING (RX_DATA_BOUNDARY_CONTRACT.md v1 — P0.3). The arena / fragment /
 * skill data are now INJECTED, not welded in:
 *
 *     on-disk JSON -> FRxDataSource (parse+intify+validate) -> FRxArenaConfig
 *                                                                   |
 *                                                                   v
 *                                            FRxEncounters::BuildArena(World, Cfg)
 *
 * The sim core never opens a file: BuildArena is a pure function of its config,
 * which is what makes a second universe (Rx Worlds) possible without touching
 * sim code. See Sim/RxDataSource.h for the loader.
 *
 * The original hardcoded transcriptions are RETAINED as LoadArenaBaked() /
 * LoadFragmentBaked() / LoadSkillTemplateBaked(). They are not dead code: they
 * are the COMPARAND for the loader-fidelity proof (contract §5.1) — the oracle
 * asserts field-by-field that the from-disk load equals the baked values — and
 * they back the convenience BuildArena(World) overload.
 */

class FRxSimWorld;      // world-owned sim registry; encounters mutates its setup
class FRxCompanionAI;   // world-owned companion; attach is routed through the world

/**
 * FRxArenaConfig — the whole parsed arena definition (mirrors the Godot `def`
 * Dictionary loaded from arena_earthquake.json). `Terrain` is fed to
 * FRxTerrain::LoadDef; the remaining fields drive the spawns, boss.Configure and
 * transfer wiring.
 *
 * This struct IS the data boundary: FRxDataSource::LoadArenaFromFile produces
 * one from JSON, LoadArenaBaked() produces the historical hardcoded one, and
 * BuildArena consumes it without caring which. A different universe is a
 * different FRxArenaConfig — no sim change required.
 */
struct FRxArenaConfig
{
	FRxTerrainDef Terrain;

	FIntPoint SpawnPlayer = FIntPoint::ZeroValue;    // def.spawns.player   (default [4000,15000])
	FIntPoint SpawnCompanion = FIntPoint::ZeroValue; // def.spawns.companion(default [5000,15500])
	FIntPoint SpawnBoss = FIntPoint::ZeroValue;      // def.spawns.boss     (default [31000,15000])

	int32 BossAnchorRegion = 5;                      // def.boss.anchor_region (default 5)
	int32 BossStability = 300;                       // def.boss.stability     (default 300)
	TArray<int32> BossArenaRegions;                  // def.boss.arena_regions (default [4,6,7,8])

	int32 TransferRegion = 9;                        // def.transfer_region    (default 9)
};

/**
 * FRxEncounters — static arena/transfer setup. Method-for-method mirror of the
 * Godot `class_name Encounters` static functions.
 */
class FRxEncounters
{
public:
	// res:// provenance of the source data (the Godot originals, which remain the
	// parity ground truth). The UE project's own copies live in Data/ — see
	// FRxDataSource::DefaultArenaPath() and Data/PROVENANCE.json.
	static const TCHAR* ArenaPath()         { return TEXT("res://game/data/arena_earthquake.json"); }
	static const TCHAR* FragmentPath()      { return TEXT("res://game/data/fragment_earthquake.json"); }
	static const TCHAR* SkillTemplatePath() { return TEXT("res://game/data/skill_faultline_interrupt.json"); }

	/**
	 * build_arena, DATA-DRIVEN FORM (contract §2.1). Config is injected: load the
	 * terrain def, spawn player/companion/boss (sequential ids 1,2,3), configure
	 * the boss FSM, wire the id fields, attach the companion AI, emit
	 * encounter_ready. Spawn ORDER is load-bearing (entity ids follow it).
	 *
	 * This function opens no file and consults no global — it is a pure function
	 * of Cfg, so a different universe is just a different Cfg.
	 */
	static void BuildArena(FRxSimWorld& World, const FRxArenaConfig& Cfg);

	/**
	 * Convenience overload using the default BAKED config, so callers that do
	 * not (yet) source data from disk keep working unchanged. Equivalent to
	 * BuildArena(World, LoadArenaBaked()).
	 */
	static void BuildArena(FRxSimWorld& World);

	/**
	 * build_transfer: SHENRON §8 post-boss reconfiguration — the exit_bridge
	 * begins collapsing. No-op if transfer already active or no transfer region.
	 */
	static void BuildTransfer(FRxSimWorld& World);

	// -----------------------------------------------------------------------
	// BAKED data (contract §5.1). The historical hardcoded transcriptions of the
	// three JSON files, kept SPECIFICALLY as the comparand for the loader-
	// fidelity proof: the oracle asserts field-by-field that
	// FRxDataSource::Load*FromFile(Data/<file>.json) == Load*Baked().
	// Do not delete these — deleting them deletes the proof.
	// -----------------------------------------------------------------------

	/** The arena definition, hardcoded from arena_earthquake.json. */
	static FRxArenaConfig LoadArenaBaked();

	/** The compiled EARTHQUAKE Compression Fragment (SHENRON §6), hardcoded. */
	static FRxFragmentSpec LoadFragmentBaked();

	/**
	 * The fixed-choice FAULTLINE_INTERRUPT template, hardcoded.
	 * NOTE: the shared contract's FRxSkillSpec is the authoring-request subset
	 * (name/trigger/effect/cost/cooldown/commit_window). The template file's
	 * extra fields (skill_id/derived_from/legal_options/residual_risk/authority)
	 * are intentionally not represented — they belong to FRxSkillArtifact, which
	 * FRxSkillSystem::AuthorSkill produces from this spec.
	 */
	static FRxSkillSpec LoadSkillTemplateBaked();
};

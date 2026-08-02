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
 * DATA SOURCING NOTE: the Godot reference parses the arena / fragment / skill
 * JSON at runtime (FileAccess + JSON.parse + CanonJson.intify). This port
 * HARDCODES those values from the on-disk JSON instead, because (a) the UE
 * project ships no Content data directory / JSON files, (b) it removes the Json
 * module + file-path + parse dependencies from the deterministic core, and
 * (c) it guarantees the exact positions/config values byte-for-byte. The res://
 * provenance paths are kept below for traceability. If the data ever needs to be
 * swappable, replace LoadArena/LoadFragment/LoadSkillTemplate with a parser that
 * produces the same structs.
 */

class FRxSimWorld;      // world-owned sim registry; encounters mutates its setup
class FRxCompanionAI;   // world-owned companion; attach is routed through the world

/**
 * FRxArenaConfig — the whole parsed arena definition (mirrors the Godot `def`
 * Dictionary loaded from arena_earthquake.json). `Terrain` is fed to
 * FRxTerrain::LoadDef; the remaining fields drive the spawns, boss.Configure and
 * transfer wiring. Values are hardcoded from the JSON (see LoadArena()).
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
	// res:// provenance of the source data (informational; the data itself is
	// hardcoded — see LoadArena/LoadFragment/LoadSkillTemplate and the header note).
	static const TCHAR* ArenaPath()         { return TEXT("res://game/data/arena_earthquake.json"); }
	static const TCHAR* FragmentPath()      { return TEXT("res://game/data/fragment_earthquake.json"); }
	static const TCHAR* SkillTemplatePath() { return TEXT("res://game/data/skill_faultline_interrupt.json"); }

	/**
	 * build_arena: load terrain def + arena config, spawn player/companion/boss
	 * (sequential ids 1,2,3), configure the boss FSM, wire the id fields, attach
	 * the companion AI, and emit encounter_ready. Spawn ORDER is load-bearing.
	 */
	static void BuildArena(FRxSimWorld& World);

	/**
	 * build_transfer: SHENRON §8 post-boss reconfiguration — the exit_bridge
	 * begins collapsing. No-op if transfer already active or no transfer region.
	 */
	static void BuildTransfer(FRxSimWorld& World);

	/** load_fragment: the compiled EARTHQUAKE Compression Fragment (SHENRON §6). */
	static FRxFragmentSpec LoadFragment();

	/**
	 * load_skill_template: the fixed-choice FAULTLINE_INTERRUPT template.
	 * NOTE: the shared contract's FRxSkillSpec is the authoring-request subset
	 * (name/trigger/effect/cost/cooldown/commit_window). The template file's
	 * extra fields (skill_id/derived_from/legal_options/residual_risk/authority)
	 * are intentionally not represented — they belong to FRxSkillArtifact, which
	 * FRxSkillSystem::AuthorSkill produces from this spec.
	 */
	static FRxSkillSpec LoadSkillTemplate();

	/**
	 * The arena definition (hardcoded from arena_earthquake.json). Exposed for
	 * tests/tools; BuildArena consumes it internally.
	 */
	static FRxArenaConfig LoadArena();
};

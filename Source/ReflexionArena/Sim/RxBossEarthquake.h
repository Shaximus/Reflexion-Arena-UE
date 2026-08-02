#pragma once

#include "CoreMinimal.h"

/**
 * FRxBossEarthquake — deterministic boss FSM, ported 1:1 from the Godot ground
 * truth (Reflexion-Arena/game/sim/boss_earthquake.gd, class_name BossEarthquake)
 * to preserve behavior + replay/hash parity across the engine switch
 * (CONTRACTS.md §0 determinism law, §2 "BossEarthquake").
 *
 * FSM (contract): DORMANT -> TAUNT -> ACCUMULATE -> RELEASE -> RECOVER
 *   -> (ACCUMULATE ...) -> DESTABILIZED -> DEFEATED
 *
 * Behavior law (canon, mirrored verbatim from the .gd):
 *  - TAUNT entry emits event boss_telegraph {"word":"Earthquake"}.
 *  - ACCUMULATE pumps ACCUMULATE_RATE stress/tick into the anchor pillar;
 *    readable precursor "tremor" events fire at 25/50/75% of STRESS_THRESHOLD.
 *  - If the anchor region is dampened below DAMP_CANCEL mid-ACCUMULATE (after
 *    having been at/above it) the boss's own charge backfires: DESTABILIZED
 *    (40-tick vulnerability window, strikes deal x3, stability 300 -> DEFEATED
 *    at 0). This is the causal defeat path — destabilization of the boss's own
 *    anchor, NOT HP depletion.
 *  - Striking the anchor region during ACCUMULATE dampens stress and delays the
 *    release by STRIKE_DELAY ticks (creating the companion's Tokenweave window).
 *  - RELEASE: world queues the propagation wave -> RECOVER (aftershock at
 *    +AFTERSHOCK_TICKS with AFTERSHOCK_FORCE — residual risk).
 *  - The boss NEVER reads player input and never reads hidden state: it reacts
 *    only to world state (player region, anchor stress). Fair in retrospect.
 *
 * Conventions (CONTRACTS.md §0): plain C++ class (NOT UObject), CoreMinimal only,
 * integer math only, no floats, deterministic. The boss talks to the sim world
 * through FRxSimWorld& (forward-declared; owner reconciles the interface — see
 * the call list reported at integration).
 */

class FRxSimWorld;

/** Boss FSM states — exact names mirror the Godot string states. */
enum class ERxBossState : uint8
{
	Dormant,      // "DORMANT"
	Taunt,        // "TAUNT"
	Accumulate,   // "ACCUMULATE"
	Release,      // "RELEASE"
	Recover,      // "RECOVER"
	Destabilized, // "DESTABILIZED"
	Defeated,     // "DEFEATED"
};

/**
 * Canonical-safe snapshot mirroring BossEarthquake.snapshot(). Field comments
 * give the exact Godot dictionary keys so the world's canonical-JSON serializer
 * reproduces the same bytes (and thus the same state hash).
 */
struct FRxBossSnapshot
{
	FString State;              // "state"
	int32 StateTicks = 0;       // "state_ticks"
	int32 AnchorRegion = -1;    // "anchor_region"
	int32 Stability = 0;        // "stability"
	TArray<int32> ArenaRegions; // "arena_regions"
	int32 ReleaseDelay = 0;     // "release_delay"
	int32 PrevAnchorStress = 0; // "prev_anchor_stress"
	int32 TremorStage = 0;      // "tremor_stage"
};

class FRxBossEarthquake
{
public:
	// --- FSM state (public: SimWorld reads State / Stability / AnchorRegion,
	//     mirroring the open GDScript RefCounted). ---
	ERxBossState State = ERxBossState::Dormant;
	int32 StateTicks = 0;
	int32 AnchorRegion = -1; // -1 => inactive until Configure() binds encounter data
	int32 Stability = 300;   // boss HP analogue (300 -> DEFEATED at 0)
	TArray<int32> ArenaRegions; // player on any of these regions wakes the boss

	int32 ReleaseDelay = 0;      // remaining forced delay from anchor strikes
	int32 PrevAnchorStress = 0;  // stress at end of previous ACCUMULATE tick
	int32 TremorStage = 0;       // 0..3 — which tremor precursors already fired

	FRxBossEarthquake() = default;

	/** encounters setup: bind encounter data (arena_earthquake.json "boss" block). */
	void Configure(int32 InAnchorRegion, int32 InStability, const TArray<int32>& InArenaRegions);

	/** One 20Hz sim tick of boss intelligence (SimWorld.step() step 4). */
	void AiTick(FRxSimWorld& World);

	/** Player/companion strike on the boss entity. DESTABILIZED => x3 damage. */
	void TakeStrike(FRxSimWorld& World, int32 AttackerId);

	/** Anchor region struck during ACCUMULATE: delay the release (the window). */
	void OnAnchorStruck(FRxSimWorld& World);

	/** Canonical-safe full boss state (for save/replay/hash). */
	FRxBossSnapshot Snapshot() const;

	/** Exact Godot state string for the given enum value. */
	static FString StateToString(ERxBossState InState);

	/** Exact Godot state string for the current state. */
	FString GetStateName() const { return StateToString(State); }

private:
	void TickDormant(FRxSimWorld& World);
	void TickAccumulate(FRxSimWorld& World);
	int32 AnchorStress(FRxSimWorld& World) const;
	void Enter(ERxBossState NextState);
};

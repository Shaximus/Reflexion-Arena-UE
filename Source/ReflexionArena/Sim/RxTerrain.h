#pragma once

#include "CoreMinimal.h"
#include "RxTypes.h" // FRxEntityId, RxSim::* constants (shared deterministic core)

/**
 * RxTerrain.h — region graph + integer stress-propagation model, ported
 * one-to-one from the proven Godot reference
 * (Reflexion-Arena/game/sim/terrain.gd, class_name Terrain) to preserve
 * replay/hash parity across the engine switch (CONTRACTS.md §0 determinism law,
 * §2 "Terrain — stress propagation model").
 *
 * Plain C++ class (NOT a UObject/AActor): the sim layer stays deterministic and
 * engine-agnostic — integer math ONLY (no floats in authoritative state), no
 * engine RNG, and strictly ordered iteration (TArray edge/region order, FIFO
 * BFS). All positions are milli-units (1 world unit = 1000, mirrors Vector2i);
 * ticks run at 20Hz. Constants come from RxSim (RxTypes.h) verbatim.
 *
 * Model (see terrain.gd header for the canon narrative):
 *  - Regions hold "stress"; edges are the propagation medium.
 *  - Ambient diffusion per edge per tick: transfer = (high-low)/DIFFUSION_RATE,
 *    edge-array order, sequential update. Pillar regions never diffuse
 *    (stress concentrators — the boss anchors there).
 *  - Crossing STRESS_THRESHOLD RELEASES: a wave BFSes over CONNECTED regions,
 *    force decaying DECAY_PER_HOP per hop, arriving HOP_DELAY_TICKS later per
 *    hop. Disconnected regions receive nothing (decouple = safe). The origin
 *    drains to 0 and is left unstable.
 *  - Counterplay: Anchor() (drain ANCHOR_DRAIN/tick + release immunity while
 *    anchored), Dampen() (direct stress reduction).
 *  - Data-driven StressSchedule injects stress over fixed tick windows.
 *
 * ForceRelease(origin, force) is the additive helper (terrain.gd) used by the
 * boss aftershock: a forced release below STRESS_THRESHOLD, routed through the
 * IDENTICAL wave code path as threshold releases.
 */

// One region: mirrors the Godot region Dictionary
//   {"id","name","poly":Array[Vector2i],"kind","stress","stable","anchored_by"}
struct FRxRegion
{
	int32 Id = 0;
	FString Name;
	TArray<FIntPoint> Poly;              // polygon vertices, milli-units (Vector2i)
	FString Kind = TEXT("ground");       // "ground"|"rock"|"bridge"|"pillar"
	int32 Stress = 0;
	bool bStable = true;
	FRxEntityId AnchoredBy = -1;         // -1 == not anchored
};

// One connectivity edge: mirrors the Godot [a_id, b_id] pair.
struct FRxEdge
{
	int32 A = -1;
	int32 B = -1;

	FRxEdge() = default;
	FRxEdge(int32 InA, int32 InB) : A(InA), B(InB) {}
};

// Data-driven stress source: mirrors {"tick","region","rate","until"}.
// Active while  Tick <= time < Until, injecting Rate stress/tick into Region.
struct FRxStressEvent
{
	int32 Tick = 0;
	int32 Region = -1;
	int32 Rate = 0;
	int32 Until = 0;
};

// One arrival in a release wave: mirrors {"region","force","delay_ticks"}.
struct FRxWaveCell
{
	int32 Region = -1;
	int32 Force = 0;
	int32 DelayTicks = 0;
};

// One release: mirrors {"origin","wave":[...]}. Tick() returns an ordered array
// of these; ForceRelease() returns exactly one.
struct FRxRelease
{
	int32 Origin = -1;
	TArray<FRxWaveCell> Wave;
};

// Input definition for LoadDef — mirrors the Godot `def` Dictionary shape.
// Field defaulting/normalisation (id<-index, name<-"region_%d", etc.) is the
// parser's job (mirrors terrain.gd load_def's per-key `.get(...)` defaults);
// this struct is consumed verbatim.
struct FRxTerrainDef
{
	TArray<FRxRegion> Regions;
	TArray<FRxEdge> Edges;
	TArray<FRxStressEvent> StressSchedule;
};

// Deep-copy snapshot — mirrors terrain.gd snapshot() (regions/edges/time/
// stress_schedule). A "form suitable for canonical hashing": feed it to
// ToCanonicalJson() (or a future shared RxCanonJson) for the hash bytes.
struct FRxTerrainSnapshot
{
	TArray<FRxRegion> Regions;
	TArray<FRxEdge> Edges;
	int32 Time = 0;
	TArray<FRxStressEvent> StressSchedule;
};

/**
 * FRxTerrain — the region graph + stress model. Deterministic, integer-only.
 */
class FRxTerrain
{
public:
	FRxTerrain() = default;

	/** Load a terrain definition (copies regions/edges/schedule, resets time). */
	void LoadDef(const FRxTerrainDef& Def);

	/** True iff a region with this id exists. */
	bool RegionExists(int32 RegionId) const;

	/** Current stress of a region (0 for unknown ids). Public read accessor. */
	int32 StressOf(int32 RegionId) const;

	/**
	 * Id of the first region whose polygon contains pos (even-odd rule, exact
	 * integer arithmetic), or -1 if none. Regions are disjoint rectangles;
	 * boundary points count as inside the FIRST matching region.
	 */
	int32 RegionAt(const FIntPoint& Pos) const;

	/** Add (may be negative via caller) stress to a region; no-op if unknown. */
	void AddStress(int32 RegionId, int32 Amount);

	/** Reduce stress, clamped at 0 (counterplay: dampen). */
	void Dampen(int32 RegionId, int32 Amount);

	/** Set the anchoring entity for a region (-1 clears). Grants release immunity. */
	void Anchor(int32 RegionId, FRxEntityId EntityId);

	/** True iff a and b are in the same connected component (edge graph). */
	bool RegionsConnected(int32 A, int32 B) const;

	/** Contract alias (CONTRACTS.md §2 names it is_connected). */
	FORCEINLINE bool IsConnected(int32 A, int32 B) const { return RegionsConnected(A, B); }

	/**
	 * Additive helper (terrain.gd): forced release below threshold (aftershock).
	 * Uses the identical wave code path as threshold releases.
	 */
	FRxRelease ForceRelease(int32 Origin, int32 Force) const;

	/**
	 * One 20Hz diffusion+release pass. Returns releases in region-array order:
	 *   [{Origin, Wave:[{Region,Force,DelayTicks}]}].
	 */
	TArray<FRxRelease> Tick();

	/** Deep-copy state snapshot (mirrors terrain.gd snapshot()). */
	FRxTerrainSnapshot Snapshot() const;

	/**
	 * Canonical JSON of the snapshot per CONTRACTS.md §0: keys sorted
	 * lexicographically, no whitespace, ints/strings/bools/arrays/objects only.
	 * The bytes are what you SHA-256 for replay/hash parity.
	 */
	FString ToCanonicalJson() const;

	// --- read accessors (state is otherwise private; mirrors Godot public vars) ---
	FORCEINLINE int32 GetTime() const { return Time; }
	FORCEINLINE const TArray<FRxRegion>& GetRegions() const { return Regions; }
	FORCEINLINE const TArray<FRxEdge>& GetEdges() const { return Edges; }
	FORCEINLINE const TArray<FRxStressEvent>& GetStressSchedule() const { return StressSchedule; }

private:
	TArray<FRxRegion> Regions;
	TArray<FRxEdge> Edges;
	int32 Time = 0;
	TArray<FRxStressEvent> StressSchedule;

	/** Linear search by id (region-array order). nullptr if unknown. */
	FRxRegion* FindRegion(int32 RegionId);
	const FRxRegion* FindRegion(int32 RegionId) const;

	/** Neighbor ids in edge-array order (deterministic BFS layering). */
	TArray<int32> Neighbors(int32 RegionId) const;

	/** BFS wave from origin: force decays per hop, arrival delayed per hop. */
	FRxRelease BuildWave(int32 Origin, int32 Force) const;

	/** Even-odd point-in-polygon, exact integer math (64-bit to match Godot int). */
	static bool PointInPoly(const FIntPoint& P, const TArray<FIntPoint>& Poly);
};

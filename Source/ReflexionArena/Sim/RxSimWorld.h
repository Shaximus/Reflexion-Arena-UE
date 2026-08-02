#pragma once

#include "CoreMinimal.h"
#include "RxTypes.h"          // FRxEntity, FRxEntityId, RxSim::*, RxKind/RxState/RxCmd/RxActor/RxCode
#include "RxRng.h"            // FRxRng (SplitMix64)
#include "RxCanonJson.h"      // FRxJsonValue, FRxCanonJson
#include "RxTerrain.h"        // FRxTerrain, FRxRelease
#include "RxBossEarthquake.h" // FRxBossEarthquake
#include "RxSkillSystem.h"    // FRxSkillSystem
#include "RxReceipts.h"       // FRxReceipts
#include "RxCommands.h"       // FRxCommandEnvelope, FRxCmdResult, FRxCommands, RxCmdDetail

/**
 * RxSimWorld.h — the FRxSimWorld orchestrator, a plain C++ class (NOT a UObject)
 * ported 1:1 from the Godot ground truth
 * (Reflexion-Arena/game/sim/sim_world.gd, class_name SimWorld) to preserve the
 * step() pipeline, snapshot() tree and state_hash() byte-for-byte across the
 * engine switch (CONTRACTS.md §0 determinism law, §2 SimWorld).
 *
 * Conventions (CONTRACTS.md §0): integer math ONLY, no floats in authoritative
 * state, no engine RNG (FRxRng only), deterministic iteration (ALL entity
 * iteration goes over EntityOrder; every hashed structure is an FRxJsonValue fed
 * through FRxCanonJson — never a UE FJsonObject).
 */

class FRxCompanionAI; // world-owned; TUniquePtr needs an out-of-line destructor.
class FJsonObject;    // event payloads produced by skills/encounters (unhashed).

/**
 * FRxEvent — one entry in the world event log (drained by presentation; NOT part
 * of the canonical snapshot / state hash). Mirrors the Godot event Dictionary
 * {"tick","type","data"}.
 */
struct FRxEvent
{
	int32 Tick = 0;
	FString Type;
	FRxJsonValue Data;
};

// Companion/skills call World.Submit and read {bOk, Code}; the submit result IS
// the command validation result (mirrors submit() returning Commands.validate()).
using FRxSubmitResult = FRxCmdResult;

class FRxSimWorld
{
public:
	explicit FRxSimWorld(uint64 Seed);
	~FRxSimWorld();

	// -----------------------------------------------------------------------
	// Public members mirroring sim_world.gd (encounters wires these directly).
	// -----------------------------------------------------------------------
	int32 Tick = 0;
	FRxRng Rng;
	TMap<FRxEntityId, FRxEntity> Entities;
	TArray<FRxEntityId> EntityOrder; // iteration order (determinism law)
	int32 NextEntityId = 1;

	FRxBossEarthquake Boss;
	FRxReceipts Receipts;

	int32 PlayerId = -1;
	int32 CompanionId = -1;
	int32 BossId = -1;
	int32 TransferRegionId = -1;

	// -----------------------------------------------------------------------
	// Accessors (the UNION of what the ported modules already call)
	// -----------------------------------------------------------------------
	int32 GetTick() const { return Tick; }
	int32 GetPlayerId() const { return PlayerId; }
	int32 GetTransferRegionId() const { return TransferRegionId; }

	bool HasEntity(FRxEntityId Id) const { return Entities.Contains(Id); }
	const FRxEntity& GetEntity(FRxEntityId Id) const { return Entities.FindChecked(Id); }
	FRxEntity* FindEntity(FRxEntityId Id) { return Entities.Find(Id); }
	FIntPoint GetEntityPos(FRxEntityId Id) const;
	const TMap<FRxEntityId, FRxEntity>& GetEntities() const { return Entities; }
	FString EntityKind(FRxEntityId Id) const;
	int32 EntityIdForActor(const FString& Actor) const;

	// Terrain: Encounters/Skills/Commands call Terrain() (mutable); Boss binds a
	// mutable ref from GetTerrain(); Companion/Snapshot read the const overload.
	FRxTerrain& Terrain() { return TerrainImpl; }
	FRxTerrain& GetTerrain() { return TerrainImpl; }
	const FRxTerrain& GetTerrain() const { return TerrainImpl; }

	FRxSkillSystem& Skills() { return SkillsImpl; }
	bool HasCompanion() const { return CompanionAI.IsValid(); }
	// Read-only access to the world-owned companion (verification harness / oracle
	// parity: PART 2 injection drives ReceiveInstruction directly, as the Python
	// oracle does). Observes only — does not alter sim behavior/state/hashing.
	FRxCompanionAI* GetCompanion() { return CompanionAI.Get(); }

	const TArray<FRxEvent>& GetEvents() const { return Events; }

	bool GetFlag(const FString& Key, bool bDefault = false) const;
	void SetFlag(const FString& Key, bool bValue);
	FString GetFlagString(const FString& Key, const FString& Default = FString()) const;
	void SetFlagString(const FString& Key, const FString& Value);

	int32 Idist(const FIntPoint& A, const FIntPoint& B) const;
	bool StrikeCooldownOk(FRxEntityId AttackerId, const FString& TargetKey) const;
	void MarkStrike(FRxEntityId AttackerId, const FString& TargetKey);

	FRxEntityId SpawnEntity(const FString& Kind, const FIntPoint& Pos);
	void AttachCompanion(FRxEntityId InCompanionId, FRxEntityId InPlayerId);
	void SyncBossEntity();
	void QueueRelease(int32 Origin, int32 Force);

	FString StateHash() const;
	FRxJsonValue Snapshot() const;

	// Submit: companion path (canonical JSON) + skills path (typed envelope).
	FRxCmdResult Submit(const FRxJsonValue& Command);
	FRxCmdResult Submit(const FRxCommandEnvelope& Envelope);

	// Emit: reconcile 3 producer styles into one FRxEvent store.
	void Emit(const FString& Type, const FRxJsonValue& Data);
	void Emit(const FString& Type, const TSharedRef<FJsonObject>& Data);

	// Typed boss telegraph/precursor wrappers (build the .gd emit payloads).
	void EmitBossTelegraph(const FString& Word);
	void EmitTremor(int32 Region, int32 Pct, int32 Stress);
	void EmitBossDestabilized(int32 Window);
	void EmitBossRecover(int32 AftershockIn);
	void EmitAftershock(int32 Force, int32 Origin);
	void EmitBossStruck(int32 Attacker, int32 Dmg, int32 Stability, const FString& State);
	void EmitBossDefeated(int32 Stability);
	void EmitAnchorStruck(int32 Region, int32 ReleaseDelay);

	// ONE 20Hz tick (sim_world.gd step() — exact contract order).
	void Step();

private:
	// --- world-owned sim modules (accessed via methods above) ---
	FRxTerrain TerrainImpl;
	FRxSkillSystem SkillsImpl;
	TUniquePtr<FRxCompanionAI> CompanionAI;

	// --- event log + blackboard flags (bool + string, mirrors the .gd dict) ---
	TArray<FRxEvent> Events;
	TMap<FString, bool> Flags;
	TMap<FString, FString> FlagStrings;

	// --- deterministic bookkeeping ---
	int32 RuntimeSeq = RxSim::RUNTIME_SEQ_BASE;
	TMap<FString, int32> StrikeCd; // "attacker|target" -> last strike tick

	struct FRxScheduledWave
	{
		int32 ApplyTick = 0;
		int32 Region = -1;
		int32 Force = 0;
		int32 Origin = -1;
	};
	TArray<FRxScheduledWave> ScheduledWaves;

	// One queued command: the canonical JSON (for cmd_hash + snapshot "pending")
	// and the typed envelope (for _apply). Kept together so both stay in sync.
	struct FRxPending
	{
		int32 Seq = 0;
		FRxJsonValue Json;
		FRxCommandEnvelope Env;
	};
	TArray<FRxPending> Pending;

	FRxJsonValue LastReference = FRxJsonValue::Object();

	// --- internal pipeline helpers (mirror the .gd private funcs) ---
	void Upkeep();
	FRxCmdResult Apply(const FRxPending& Cmd);
	FRxCmdResult ApplyStrike(int32 AttackerId, const FRxJsonValue& Params);
	void ScheduleRelease(const FRxRelease& Rel);
	void ApplyWaveEntry(const FRxScheduledWave& W);
	FRxCmdResult SubmitEnvelopeInternal(FRxCommandEnvelope Env, FRxJsonValue Json);
	FRxJsonValue EnvelopeToJson(const FRxCommandEnvelope& Env) const;
	static void EraseWeaveProps(FRxEntity& E);
	static int64 ISqrt(int64 N);

	// --- snapshot subtree builders (all produce FRxJsonValue) ---
	FRxJsonValue TerrainSnapshotJson() const;
	FRxJsonValue BossSnapshotJson() const;
	FRxJsonValue EntitySnapshotJson(const FRxEntity& E) const;
	FRxJsonValue FlagsSnapshotJson() const;
	static FRxJsonValue ResultToJson(const FRxCmdResult& R);
};

#pragma once

#include "CoreMinimal.h"

/**
 * RxTypes.h — shared entity model + simulation constants for the Reflexion Arena
 * deterministic core, ported from the proven Godot reference
 * (Reflexion-Arena/game/sim/sim_world.gd + sim_config.gd) to preserve
 * replay/hash parity across the engine switch (CONTRACTS.md §0 determinism law).
 *
 * Plain C++ POD/structs — NOT UObjects/AActors. The sim layer must stay
 * deterministic and engine-agnostic: integer math only, no floats in
 * authoritative state, no engine RNG, deterministic (ordered) iteration.
 *
 * All positions are milli-units (1 world unit = 1000). Ticks run at 20Hz
 * (20 ticks = 1 second). Stress/HP/costs are plain ints.
 */

// Sequential entity id (ADR-0005): starts at 1, assigned in spawn order.
using FRxEntityId = int32;

/**
 * FRxEntity — mirrors the Godot entity Dictionary
 *   {"id","kind","pos","hp","max_hp","state","props"}
 * with the loose `props` Dictionary promoted to typed fields. The "bHas*" flags
 * mark which optional props are present (equivalent to Dictionary.has(key) in
 * the reference), which is load-bearing for snapshot/hash parity: an absent prop
 * must NOT appear in the canonical snapshot.
 *
 * PRESENCE IS NEVER INFERRED FROM VALUE (P0.4). The reference assigns props
 * unconditionally — `e["props"]["weave_region_id"] = rid` (sim_world.gd:309)
 * runs even when `rid == -1`, so `Dictionary.has()` is true for a value that a
 * sentinel test would read as absent. Every optional prop therefore carries its
 * own bool; the value fields keep their -1/empty initialisers only as the
 * reference's `.get(key, default)` fallbacks, never as presence tests.
 *
 * props key mapping (see sim_world.gd snapshot()/_upkeep()):
 *   move_target       -> MoveTarget      (present iff bHasMoveTarget)
 *   weave_region_id   -> WeaveRegionId   (present iff bHasWeaveRegionId)
 *   weave_mode        -> WeaveMode       (present iff bHasWeaveMode)
 *   weave_start_tick  -> WeaveStartTick  (present iff bHasWeaveStartTick)
 *   weave_abort_tick  -> WeaveAbortTick  (present iff bHasWeaveAbort)
 *
 * Lifetimes differ and the asymmetry is deliberate: a weave abort erases
 * region/mode/abort but KEEPS weave_start_tick (sim_world.gd:212-214), while a
 * completed weave erases all four (_erase_weave_props, sim_world.gd:248-251).
 */
struct FRxEntity
{
	FRxEntityId Id = 0;
	FString Kind;                       // "player"|"companion"|"boss"|"structure"
	FIntPoint Pos = FIntPoint::ZeroValue; // milli-units (mirrors Godot Vector2i)
	int32 Hp = 0;
	int32 MaxHp = 0;
	FString State = TEXT("idle");       // "idle"|"weaving"|"dead"|<boss FSM state>

	// --- typed props (present iff the matching bHas* flag is set) ---
	FIntPoint MoveTarget = FIntPoint::ZeroValue;
	bool bHasMoveTarget = false;

	int32 WeaveRegionId = -1;           // fallback -1 mirrors .get("weave_region_id", -1)
	bool bHasWeaveRegionId = false;

	FString WeaveMode;                  // "anchor" | (other weave modes)
	bool bHasWeaveMode = false;

	int32 WeaveStartTick = -1;
	bool bHasWeaveStartTick = false;

	int32 WeaveAbortTick = -1;
	bool bHasWeaveAbort = false;
};

/**
 * RxSim — simulation constants ported verbatim from sim_config.gd.
 * constexpr so callers pay nothing at runtime and the values are a hard contract.
 */
namespace RxSim
{
	// --- core sim (CONTRACTS.md §2 constants list — verbatim) ---
	constexpr int32 TICK_RATE = 20;             // 20 ticks = 1 second
	constexpr int32 STRESS_THRESHOLD = 1000;    // region stress at/above => release wave
	constexpr int32 DECAY_PER_HOP = 250;        // wave force lost per propagation hop
	constexpr int32 DIFFUSION_RATE = 20;        // ambient diffusion divisor
	constexpr int32 AFTERSHOCK_TICKS = 60;      // aftershock fires this many ticks into RECOVER
	constexpr int32 AFTERSHOCK_FORCE = 300;     // aftershock wave origin force

	// --- terrain propagation model ---
	constexpr int32 HOP_DELAY_TICKS = 10;       // wave arrival delay per propagation hop
	constexpr int32 ANCHOR_DRAIN = 5;           // stress/tick dissipated by an anchored region

	// --- boss FSM ---
	constexpr int32 TAUNT_TICKS = 40;           // telegraph duration (2s)
	constexpr int32 ACCUMULATE_RATE = 3;        // stress/tick pumped into boss anchor pillar
	constexpr int32 RELEASE_TICKS = 10;         // RELEASE state duration
	constexpr int32 RECOVER_TICKS = 80;         // recovery window (4s; aftershock at +60)
	constexpr int32 DESTABILIZED_TICKS = 40;    // vulnerability window (2s), strikes x3
	constexpr int32 DAMP_CANCEL = 400;          // anchor dampened below this mid-ACCUMULATE => DESTABILIZED

	// --- strikes (player/companion counter-play) ---
	constexpr int32 STRIKE_DAMAGE = 10;         // base stability damage to boss
	constexpr int32 STRIKE_MULT_DESTABILIZED = 3; // damage multiplier during DESTABILIZED
	constexpr int32 STRIKE_COOLDOWN = 3;        // ticks between strikes on same target
	constexpr int32 STRIKE_RANGE = 6000;        // milli-units; entity-target strike range
	constexpr int32 STRIKE_DAMPEN = 250;        // stress removed by striking a region
	constexpr int32 STRIKE_DELAY = 20;          // release delay from striking boss anchor mid-ACCUMULATE

	// --- tokenweave (completion is sim-internal T3, applied by SimWorld upkeep) ---
	constexpr int32 WEAVE_DURATION_TICKS = 100; // uninterrupted ticks a weave must hold (5s)
	constexpr int32 WEAVE_DAMPEN = 300;         // stress removed on weave completion

	// --- entities / movement ---
	constexpr int32 MOVE_SPEED = 600;           // milli-units/tick glide toward move_to target
	constexpr int32 PLAYER_HP = 1000;
	constexpr int32 COMPANION_HP = 1000;
	constexpr int32 WAVE_DAMAGE_DIV = 10;       // entity damage = wave force / this

	// --- bounded skill FAULTLINE_INTERRUPT ---
	inline constexpr const TCHAR* SKILL_ID = TEXT("faultline_interrupt");
	constexpr int32 SKILL_COST = 30;            // focus
	constexpr int32 SKILL_COOLDOWN = 240;       // ticks (12s)
	constexpr int32 SKILL_COMMIT_WINDOW = 20;   // fixed artifact field (commit/exposure window)
	constexpr int32 SKILL_DAMPEN = 600;         // destabilize_anchor effect magnitude
	constexpr int32 SKILL_WRONG_STRESS = 200;   // residual risk: wrong surface id (+200 stress)
	constexpr int32 FOCUS_MAX = 100;
	constexpr int32 FOCUS_REGEN = 1;            // focus/tick

	// --- transfer encounter ---
	constexpr int32 TRANSFER_STRESS_RATE = 15;  // stress/tick injected into exit_bridge once active
	constexpr int32 TRANSFER_START_STRESS = 300; // exit_bridge pre-stress when transfer begins

	// --- runtime command sequencing (SimWorld.submit assigns when absent) ---
	constexpr int32 RUNTIME_SEQ_BASE = 1000000; // runtime cmds get seqs above this; scripted 1..N first
}

/**
 * RxKind / RxState / RxCmd / RxActor / RxCode — well-known string literals from
 * CONTRACTS.md §2. The reference sim keys on raw strings; these constants keep
 * every crew member spelling them identically (they feed the canonical snapshot,
 * so a typo would silently break hash parity).
 */
namespace RxKind
{
	inline constexpr const TCHAR* Player = TEXT("player");
	inline constexpr const TCHAR* Companion = TEXT("companion");
	inline constexpr const TCHAR* Boss = TEXT("boss");
	inline constexpr const TCHAR* Structure = TEXT("structure");
}

namespace RxState
{
	inline constexpr const TCHAR* Idle = TEXT("idle");
	inline constexpr const TCHAR* Weaving = TEXT("weaving");
	inline constexpr const TCHAR* Dead = TEXT("dead");
	// boss FSM states (boss_earthquake.gd)
	inline constexpr const TCHAR* Dormant = TEXT("DORMANT");
	inline constexpr const TCHAR* Taunt = TEXT("TAUNT");
	inline constexpr const TCHAR* Accumulate = TEXT("ACCUMULATE");
	inline constexpr const TCHAR* Release = TEXT("RELEASE");
	inline constexpr const TCHAR* Recover = TEXT("RECOVER");
	inline constexpr const TCHAR* Destabilized = TEXT("DESTABILIZED");
	inline constexpr const TCHAR* Defeated = TEXT("DEFEATED");
}

namespace RxActor
{
	inline constexpr const TCHAR* Player = TEXT("player");
	inline constexpr const TCHAR* Companion = TEXT("companion");
	inline constexpr const TCHAR* System = TEXT("system");
}

namespace RxCmd
{
	inline constexpr const TCHAR* MoveTo = TEXT("move_to");
	inline constexpr const TCHAR* Strike = TEXT("strike");
	inline constexpr const TCHAR* Reference = TEXT("reference");
	inline constexpr const TCHAR* Instruct = TEXT("instruct");
	inline constexpr const TCHAR* Approve = TEXT("approve");
	inline constexpr const TCHAR* Cancel = TEXT("cancel");
	inline constexpr const TCHAR* Correct = TEXT("correct");
	inline constexpr const TCHAR* UseSkill = TEXT("use_skill");
	inline constexpr const TCHAR* TokenweaveBegin = TEXT("tokenweave_begin");
	inline constexpr const TCHAR* SocketFragment = TEXT("socket_fragment");
	inline constexpr const TCHAR* AuthorSkill = TEXT("author_skill");
	inline constexpr const TCHAR* Wait = TEXT("wait");
}

namespace RxCode
{
	inline constexpr const TCHAR* Ok = TEXT("OK");
	inline constexpr const TCHAR* ErrMalformed = TEXT("ERR_MALFORMED");
	inline constexpr const TCHAR* ErrAuthority = TEXT("ERR_AUTHORITY");
	inline constexpr const TCHAR* ErrUnknownType = TEXT("ERR_UNKNOWN_TYPE");
	inline constexpr const TCHAR* ErrState = TEXT("ERR_STATE");
}

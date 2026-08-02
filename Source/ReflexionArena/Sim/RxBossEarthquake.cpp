#include "RxBossEarthquake.h"

// The boss reacts to and mutates the sim world through these. They are provided
// by the sim-world port (owner reconciles the exact interface). The header only
// forward-declares FRxSimWorld; the calls resolve here against the full types.
#include "RxSimWorld.h"
#include "RxTerrain.h"

namespace
{
	// Mirrors sim_config.gd (SimConfig) — the exact subset BossEarthquake reads.
	// Kept local so this port depends on nothing beyond the world/terrain surface;
	// replace with FRxSimConfig references if/when that shared header lands.
	constexpr int32 TAUNT_TICKS = 40;             // telegraph "Earthquake." duration (2s)
	constexpr int32 ACCUMULATE_RATE = 3;          // stress/tick pumped into the anchor
	constexpr int32 RELEASE_TICKS = 10;           // RELEASE state duration
	constexpr int32 RECOVER_TICKS = 80;           // recovery window (4s; aftershock at +60)
	constexpr int32 DESTABILIZED_TICKS = 40;      // vulnerability window (2s), strikes x3
	constexpr int32 DAMP_CANCEL = 400;            // anchor dampened below this => DESTABILIZED
	constexpr int32 STRESS_THRESHOLD = 1000;      // stress at/above this releases a wave
	constexpr int32 AFTERSHOCK_TICKS = 60;        // aftershock fires this far into RECOVER
	constexpr int32 AFTERSHOCK_FORCE = 300;       // aftershock wave origin force
	constexpr int32 STRIKE_DAMAGE = 10;           // base stability damage to the boss
	constexpr int32 STRIKE_MULT_DESTABILIZED = 3; // damage multiplier during DESTABILIZED
	constexpr int32 STRIKE_DELAY = 20;            // release delay from striking the anchor
}

void FRxBossEarthquake::Configure(int32 InAnchorRegion, int32 InStability, const TArray<int32>& InArenaRegions)
{
	AnchorRegion = InAnchorRegion;
	Stability = InStability;
	ArenaRegions = InArenaRegions; // .duplicate() -> value copy
}

void FRxBossEarthquake::AiTick(FRxSimWorld& World)
{
	if (AnchorRegion == -1 || State == ERxBossState::Defeated)
	{
		return;
	}
	StateTicks += 1;
	switch (State)
	{
	case ERxBossState::Dormant:
		TickDormant(World);
		break;

	case ERxBossState::Taunt:
		if (StateTicks >= TAUNT_TICKS)
		{
			Enter(ERxBossState::Accumulate);
			TremorStage = 0;
			PrevAnchorStress = AnchorStress(World);
		}
		break;

	case ERxBossState::Accumulate:
		TickAccumulate(World);
		break;

	case ERxBossState::Release:
		if (StateTicks >= RELEASE_TICKS)
		{
			Enter(ERxBossState::Recover);
			World.SetFlag(TEXT("evidence_recovery"), true);
			World.EmitBossRecover(AFTERSHOCK_TICKS);
		}
		break;

	case ERxBossState::Recover:
		if (StateTicks == AFTERSHOCK_TICKS)
		{
			// Residual risk: smaller aftershock wave (CONTRACTS.md §2).
			World.QueueRelease(AnchorRegion, AFTERSHOCK_FORCE);
			World.EmitAftershock(AFTERSHOCK_FORCE, AnchorRegion);
		}
		if (StateTicks >= RECOVER_TICKS)
		{
			Enter(ERxBossState::Accumulate);
			TremorStage = 0;
			PrevAnchorStress = AnchorStress(World);
		}
		break;

	case ERxBossState::Destabilized:
		if (StateTicks >= DESTABILIZED_TICKS)
		{
			// Window closed without a kill: back to accumulating (fair cycle).
			Enter(ERxBossState::Accumulate);
			TremorStage = 0;
			PrevAnchorStress = AnchorStress(World);
		}
		break;

	default:
		break;
	}
}

void FRxBossEarthquake::TickDormant(FRxSimWorld& World)
{
	const int32 Pid = World.GetPlayerId();
	if (Pid == -1 || !World.HasEntity(Pid))
	{
		return;
	}
	const int32 Pr = World.GetTerrain().RegionAt(World.GetEntityPos(Pid));
	if (ArenaRegions.Contains(Pr))
	{
		Enter(ERxBossState::Taunt);
		// The word is the lesson title (SHENRON §5).
		World.EmitBossTelegraph(TEXT("Earthquake"));
	}
}

void FRxBossEarthquake::TickAccumulate(FRxSimWorld& World)
{
	FRxTerrain& Terrain = World.GetTerrain();
	Terrain.AddStress(AnchorRegion, ACCUMULATE_RATE);
	const int32 S = AnchorStress(World);

	// Readable precursors at 25/50/75% of threshold (integer division, exact).
	const int32 Stages[3] = {
		STRESS_THRESHOLD / 4,       // 250
		STRESS_THRESHOLD / 2,       // 500
		STRESS_THRESHOLD * 3 / 4,   // 750
	};
	for (int32 I = 0; I < 3; ++I)
	{
		if (TremorStage <= I && S >= Stages[I])
		{
			TremorStage = I + 1;
			World.SetFlag(TEXT("evidence_tremor"), true);
			World.EmitTremor(AnchorRegion, (I + 1) * 25, S);
		}
	}

	// Backfire: anchor dampened below DAMP_CANCEL after having been at/above it.
	if (PrevAnchorStress >= DAMP_CANCEL && S < DAMP_CANCEL)
	{
		Terrain.Dampen(AnchorRegion, S); // charge dissipates harmlessly
		Enter(ERxBossState::Destabilized);
		World.SetFlag(TEXT("evidence_anchor_failure"), true);
		World.EmitBossDestabilized(DESTABILIZED_TICKS);
	}
	else if (ReleaseDelay > 0)
	{
		ReleaseDelay -= 1; // anchor strikes push the release back (the window)
	}
	else if (S >= STRESS_THRESHOLD)
	{
		Enter(ERxBossState::Release);
		World.QueueRelease(AnchorRegion, S);
		Terrain.Dampen(AnchorRegion, S);
	}
	PrevAnchorStress = AnchorStress(World); // re-read: reflects any dampen above
}

void FRxBossEarthquake::TakeStrike(FRxSimWorld& World, int32 AttackerId)
{
	const int32 Mult = (State == ERxBossState::Destabilized) ? STRIKE_MULT_DESTABILIZED : 1;
	const int32 Dmg = STRIKE_DAMAGE * Mult;
	Stability -= Dmg;
	// state string is captured BEFORE any transition to DEFEATED (mirrors the .gd).
	World.EmitBossStruck(AttackerId, Dmg, Stability, StateToString(State));
	World.SyncBossEntity();
	if (Stability <= 0 && State != ERxBossState::Defeated)
	{
		Stability = 0;
		Enter(ERxBossState::Defeated);
		World.SetFlag(TEXT("boss_defeated"), true);
		World.SyncBossEntity();
		World.EmitBossDefeated(0);
	}
}

void FRxBossEarthquake::OnAnchorStruck(FRxSimWorld& World)
{
	if (State == ERxBossState::Accumulate)
	{
		ReleaseDelay += STRIKE_DELAY;
		World.EmitAnchorStruck(AnchorRegion, ReleaseDelay);
	}
}

int32 FRxBossEarthquake::AnchorStress(FRxSimWorld& World) const
{
	return World.GetTerrain().StressOf(AnchorRegion);
}

void FRxBossEarthquake::Enter(ERxBossState NextState)
{
	State = NextState;
	StateTicks = 0;
}

FRxBossSnapshot FRxBossEarthquake::Snapshot() const
{
	FRxBossSnapshot Snap;
	Snap.State = StateToString(State);
	Snap.StateTicks = StateTicks;
	Snap.AnchorRegion = AnchorRegion;
	Snap.Stability = Stability;
	Snap.ArenaRegions = ArenaRegions; // .duplicate() -> value copy
	Snap.ReleaseDelay = ReleaseDelay;
	Snap.PrevAnchorStress = PrevAnchorStress;
	Snap.TremorStage = TremorStage;
	return Snap;
}

FString FRxBossEarthquake::StateToString(ERxBossState InState)
{
	switch (InState)
	{
	case ERxBossState::Dormant:      return TEXT("DORMANT");
	case ERxBossState::Taunt:        return TEXT("TAUNT");
	case ERxBossState::Accumulate:   return TEXT("ACCUMULATE");
	case ERxBossState::Release:      return TEXT("RELEASE");
	case ERxBossState::Recover:      return TEXT("RECOVER");
	case ERxBossState::Destabilized: return TEXT("DESTABILIZED");
	case ERxBossState::Defeated:     return TEXT("DEFEATED");
	default:                         return TEXT("DORMANT");
	}
}

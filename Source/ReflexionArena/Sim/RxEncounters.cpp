#include "RxEncounters.h"

#include "RxTypes.h"            // RxSim::* constants, RxKind::*, FRxEntity
#include "RxBossEarthquake.h"   // FRxBossEarthquake (World.Boss.Configure / .Stability)
#include "Dom/JsonObject.h"     // FJsonObject event payloads (World.Emit)

// The concrete world type. Not yet written at the time of this port
// (RxSimWorld.h is authored by the sim-world owner); this unit only depends on
// the ASSUMED interface documented in the integration report. Including it here
// so BuildArena/BuildTransfer see the full type — reconcile at integration.
#include "RxSimWorld.h"

/**
 * RxEncounters.cpp — implementation mirroring encounters.gd build_arena /
 * build_transfer / load_fragment / load_skill_template.
 *
 * BuildArena takes its FRxArenaConfig as a parameter (RX_DATA_BOUNDARY_CONTRACT
 * v1 §2.1): this TU opens no file and parses no JSON. Sourcing config from disk
 * is FRxDataSource's job (Sim/RxDataSource.h).
 *
 * The Load*Baked() functions below are the historical verbatim transcriptions of
 * arena_earthquake.json / fragment_earthquake.json /
 * skill_faultline_interrupt.json. They are retained as the comparand for the
 * loader-fidelity proof (contract §5.1).
 */

// ---------------------------------------------------------------------------
// build_arena — data-driven form: config is injected, nothing is loaded here.
// ---------------------------------------------------------------------------

void FRxEncounters::BuildArena(FRxSimWorld& World, const FRxArenaConfig& Def)
{
	World.Terrain().LoadDef(Def.Terrain);

	// spawns (sequential ids: player=1, companion=2, boss=3 — ADR-0005).
	// Order is load-bearing: entity ids are assigned by spawn order.
	World.PlayerId    = World.SpawnEntity(RxKind::Player, Def.SpawnPlayer);
	World.CompanionId = World.SpawnEntity(RxKind::Companion, Def.SpawnCompanion);
	World.BossId      = World.SpawnEntity(RxKind::Boss, Def.SpawnBoss);

	// boss configuration (arena_earthquake.json "boss" block).
	World.Boss.Configure(Def.BossAnchorRegion, Def.BossStability, Def.BossArenaRegions);
	// mirror BossEarthquake.stability onto the boss entity (world.entities[boss_id]).
	if (FRxEntity* BossEntity = World.FindEntity(World.BossId))
	{
		BossEntity->Hp = World.Boss.Stability;
		BossEntity->MaxHp = World.Boss.Stability;
	}

	// transfer encounter region (exit_bridge).
	World.TransferRegionId = Def.TransferRegion;

	// companion AI binding (owner B's class; contract: "set by encounters setup").
	// Mirrors: ai = CompanionAI.new(); ai.bind(companion_id, player_id);
	//          world.companion = ai. Routed through the world so lifecycle/storage
	//          stays world-owned (see integration report).
	World.AttachCompanion(World.CompanionId, World.PlayerId);

	// encounter_ready {"name","regions"}.
	const TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), TEXT("arena_earthquake"));
	Data->SetNumberField(TEXT("regions"), World.Terrain().GetRegions().Num());
	World.Emit(TEXT("encounter_ready"), Data);
}

// Convenience overload: the default baked config (contract §2.1 — existing
// callers keep working unchanged during the transition).
void FRxEncounters::BuildArena(FRxSimWorld& World)
{
	BuildArena(World, LoadArenaBaked());
}

// ---------------------------------------------------------------------------
// build_transfer (SHENRON §8: exit_bridge starts collapsing)
// ---------------------------------------------------------------------------

void FRxEncounters::BuildTransfer(FRxSimWorld& World)
{
	if (World.GetFlag(TEXT("transfer_active")))
	{
		return;
	}
	if (World.TransferRegionId == -1)
	{
		return;
	}
	World.SetFlag(TEXT("transfer_active"), true);
	World.Terrain().AddStress(World.TransferRegionId, RxSim::TRANSFER_START_STRESS);

	// transfer_begin {"region","start_stress","rate"}.
	const TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("region"), World.TransferRegionId);
	Data->SetNumberField(TEXT("start_stress"), RxSim::TRANSFER_START_STRESS);
	Data->SetNumberField(TEXT("rate"), RxSim::TRANSFER_STRESS_RATE);
	World.Emit(TEXT("transfer_begin"), Data);
}

// ---------------------------------------------------------------------------
// LoadArenaBaked — hardcoded mirror of arena_earthquake.json (fidelity comparand)
// ---------------------------------------------------------------------------

FRxArenaConfig FRxEncounters::LoadArenaBaked()
{
	FRxArenaConfig Cfg;

	auto MakeRegion = [](int32 Id, const TCHAR* Name, const TCHAR* Kind,
		TArray<FIntPoint> Poly) -> FRxRegion
	{
		FRxRegion R;
		R.Id = Id;
		R.Name = Name;
		R.Kind = Kind;
		R.Poly = MoveTemp(Poly);
		R.Stress = 0;
		R.bStable = true;
		R.AnchoredBy = -1;
		return R;
	};

	Cfg.Terrain.Regions = {
		MakeRegion(0, TEXT("spawn_path"),      TEXT("ground"),
			{ {2000, 12000}, {10000, 12000}, {10000, 18000}, {2000, 18000} }),
		MakeRegion(1, TEXT("bridge_w_support"), TEXT("bridge"),
			{ {10000, 13000}, {16000, 13000}, {16000, 17000}, {10000, 17000} }),
		MakeRegion(2, TEXT("bridge_e_support"), TEXT("bridge"),
			{ {16000, 13000}, {22000, 13000}, {22000, 17000}, {16000, 17000} }),
		MakeRegion(3, TEXT("approach"),         TEXT("ground"),
			{ {22000, 11500}, {26500, 11500}, {26500, 18500}, {22000, 18500} }),
		MakeRegion(4, TEXT("arena_n"),          TEXT("rock"),
			{ {29800, 9500}, {32200, 9500}, {32200, 14000}, {29800, 14000} }),
		MakeRegion(5, TEXT("boss_anchor"),      TEXT("pillar"),
			{ {30000, 14000}, {32000, 14000}, {32000, 16000}, {30000, 16000} }),
		MakeRegion(6, TEXT("arena_e"),          TEXT("rock"),
			{ {32200, 13500}, {35000, 13500}, {35000, 16500}, {32200, 16500} }),
		MakeRegion(7, TEXT("arena_s"),          TEXT("rock"),
			{ {29800, 16000}, {32200, 16000}, {32200, 20500}, {29800, 20500} }),
		MakeRegion(8, TEXT("arena_w"),          TEXT("rock"),
			{ {26500, 13500}, {29800, 13500}, {29800, 16500}, {26500, 16500} }),
		MakeRegion(9, TEXT("exit_bridge"),      TEXT("bridge"),
			{ {35000, 13000}, {39000, 13000}, {39000, 17000}, {35000, 17000} }),
	};

	// edges: connectivity = propagation medium (edge-array order is deterministic).
	Cfg.Terrain.Edges = {
		{0, 1}, {1, 2}, {2, 3}, {3, 8}, {8, 4}, {4, 6}, {6, 7},
		{7, 8}, {4, 5}, {6, 5}, {7, 5}, {8, 5}, {6, 9},
	};

	// stress_schedule: {tick, region, rate, until}.
	Cfg.Terrain.StressSchedule = {
		{3000, 3, 60, 3040},
		{3400, 1, 15, 3520},
		{4800, 8,  2, 5400},
	};

	// spawns / boss / transfer.
	Cfg.SpawnPlayer    = FIntPoint(4000, 15000);
	Cfg.SpawnCompanion = FIntPoint(5000, 15500);
	Cfg.SpawnBoss      = FIntPoint(31000, 15000);

	Cfg.BossAnchorRegion = 5;
	Cfg.BossStability    = 300;
	Cfg.BossArenaRegions = { 4, 6, 7, 8 };

	Cfg.TransferRegion = 9;

	return Cfg;
}

// ---------------------------------------------------------------------------
// LoadFragmentBaked — hardcoded mirror of fragment_earthquake.json (SHENRON §6 canon)
// ---------------------------------------------------------------------------

FRxFragmentSpec FRxEncounters::LoadFragmentBaked()
{
	FRxFragmentSpec F;
	F.bValid = true;

	F.Compiler.ChecksRun = {
		TEXT("kernel_import"),
		TEXT("packet_construction"),
		TEXT("semantic_ir_validation"),
		TEXT("gate_security"),
		TEXT("gate_security_negative_control"),
		TEXT("gate_boundary"),
		TEXT("gate_boundary_negative_control"),
		TEXT("canon_field_fidelity"),
		TEXT("packet_serialization"),
		TEXT("fragment_hash"),
	};
	F.Compiler.Notes = TEXT("Offline subset: packet construction + SemanticIR.validate + security/boundary hard gates. Full pipeline (extraction/scoring/expansion) not vendored; see tools/semantic_kernel/VENDORED_FROM.md.");
	F.Compiler.RefCommit = TEXT("9bf2f6ac408066ccec94ed3e3d2478a0a1f4eb80");
	F.Compiler.Repo = TEXT("Shaximus/semantic_compiler_kernel");
	F.Compiler.Tool = TEXT("semantic_compiler_kernel");
	F.Compiler.VendoredSha.Add(TEXT("core/__init__.py"),    TEXT("1ea36e8db322c1006ab48ab86e63a1e8aa41324c"));
	F.Compiler.VendoredSha.Add(TEXT("core/packet.py"),      TEXT("667d4c364e0e370638ccbc1c535e42ab7f782cb9"));
	F.Compiler.VendoredSha.Add(TEXT("core/semantic_ir.py"), TEXT("a4d21b09054d70cd1e37d25c37b398b6ba58d430"));
	F.Compiler.VendoredSha.Add(TEXT("core/types.py"),       TEXT("b156834cbca30e42334ba1cc588448ef7f68cc92"));
	F.Compiler.VendoredSha.Add(TEXT("gates/boundaries.py"), TEXT("5c2f653769132819eebb48cf5c7514345535574c"));
	F.Compiler.VendoredSha.Add(TEXT("gates/security.py"),   TEXT("be455440c6431fa002bb3ab08e34dd60f358de1f"));
	F.Compiler.Version = TEXT("2.0.0-draft (upstream release V2.1.3 freeze, packet v2.0.0)");

	F.Counterplay = TEXT("Decouple, anchor, dampen, relocate, or interrupt release.");
	F.FragmentHash = TEXT("bc4ac7eab932f8c59d6011b54114f4a5793b4336a138b71cbab180ad87a4cafa");
	F.Propagation = TEXT("Connected surfaces transmit disruptive force.");
	F.ResidualRisk = TEXT("Secondary cascades and aftershocks.");
	F.TransferDomains = {
		TEXT("terrain"),
		TEXT("structures"),
		TEXT("formations"),
		TEXT("infrastructure"),
		TEXT("distributed systems"),
	};
	F.Trigger = TEXT("Accumulated structural stress exceeds threshold.");

	return F;
}

// ---------------------------------------------------------------------------
// LoadSkillTemplateBaked — hardcoded mirror of skill_faultline_interrupt.json
// (authoring-request subset; see header note).
// ---------------------------------------------------------------------------

FRxSkillSpec FRxEncounters::LoadSkillTemplateBaked()
{
	FRxSkillSpec S;
	S.Name = TEXT("FAULTLINE INTERRUPT");
	S.Trigger = TEXT("committed_ground_propagation");
	S.Effect = TEXT("destabilize_anchor");
	S.Cost = 30;
	S.Cooldown = 240;
	S.CommitWindow = 20;
	return S;
}

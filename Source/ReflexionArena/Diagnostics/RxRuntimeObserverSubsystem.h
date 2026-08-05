#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RxRuntimeObserverSubsystem.generated.h"

/**
 * URxRuntimeObserverSubsystem — headless runtime proof of movement and collision.
 *
 * WHY THIS EXISTS. Item 6 of the ARM-04 checklist ("movement and collision") could be
 * verified structurally but not at runtime. Four out-of-process observation routes were
 * tried and every one failed a control:
 *   - commandlet physics traces  -> missed a floor that provably exists (failed its
 *                                   OWN positive control, so its misses proved nothing)
 *   - `getall` console command   -> emits nothing under -nullrhi -unattended
 *   - launch-log comparison      -> a no-floor control map produced an IDENTICAL verdict
 *   - `py` via -ExecCmds -game   -> PythonScriptPlugin is UncookedOnly, never loads
 * A tick-driven in-process observer has none of those problems.
 *
 * DESIGN CONSTRAINTS this class is built around:
 *   - It must need NO hook anywhere else. UTickableWorldSubsystem is auto-instantiated
 *     by the engine, so nothing outside Source/ReflexionArena/Diagnostics/ is touched.
 *     (ARM-04 holds an exclusive carve-out on that directory; ARM-08 owns the rest.)
 *   - It must be INERT unless explicitly asked for. Without -RxObserve= on the command
 *     line ShouldCreateSubsystem returns false and the subsystem is never created, so
 *     shipping/normal play is unaffected.
 *   - It must report a machine-readable verdict, because this project's process exit
 *     codes are known not to discriminate success (unrelated plugins log errors every
 *     run, and a headless -game session never self-exits).
 *
 * Usage:
 *   UnrealEditor-Cmd <project> /Game/Maps/RxTestMap -game -unattended -nullrhi \
 *     -RxObserve=/abs/path/out.json [-RxObserveSeconds=20]
 *
 * The observer runs three phases and records what actually happened in each:
 *   SETTLE  wait for the pawn to land            -> proves floor collision + gravity
 *   DRIVE   apply forward input                  -> proves movement responds to input
 *   REPORT  compare travel against the wall at X=600 -> proves lateral collision blocks
 */
UCLASS()
class URxRuntimeObserverSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

private:
	enum class EPhase : uint8
	{
		WaitingForPawn,
		Settling,
		Driving,
		Done
	};

	void WriteReport(const TCHAR* Reason, bool bRequestExit = true);

	FString OutputPath;
	double  SecondsBudget      = 20.0;
	double  ElapsedSeconds     = 0.0;
	double  DriveSeconds       = 0.0;

	EPhase  Phase              = EPhase::WaitingForPawn;
	bool    bReportWritten     = false;

	// Observations. Every one of these is written to the report exactly as measured.
	bool    bPawnFound         = false;
	bool    bEverOnGround      = false;
	double  SettleSeconds      = -1.0;
	FVector SpawnLocation      = FVector::ZeroVector;
	FVector SettledLocation    = FVector::ZeroVector;
	FVector FinalLocation      = FVector::ZeroVector;
	double  MinZ               =  TNumericLimits<double>::Max();
	double  MaxForwardX        = -TNumericLimits<double>::Max();
	int32   SampleCount        = 0;
	int32   GroundSampleCount  = 0;
	FString LastMovementMode   = TEXT("none");

	// ---- camera (Checkpoint A: "camera operates") ----------------------------
	// Structural verification only proved a CameraComponent exists on the Blueprint.
	// That is not the same claim as "the player's view actually comes from it and
	// follows the pawn", which is what these measure.
	bool    bCameraFound        = false;
	FString ViewTargetName      = TEXT("none");
	bool    bViewTargetIsPawn   = false;
	FVector CameraStartLocation = FVector::ZeroVector;
	FVector CameraFinalLocation = FVector::ZeroVector;
	double  MinCameraDistance   =  TNumericLimits<double>::Max();
	double  MaxCameraDistance   = -TNumericLimits<double>::Max();
};

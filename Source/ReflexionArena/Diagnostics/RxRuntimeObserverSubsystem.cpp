#include "RxRuntimeObserverSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"   // UPackage: GetOutermost()->GetName() in ShouldCreateSubsystem

DEFINE_LOG_CATEGORY_STATIC(LogRxObserver, Log, All);

namespace
{
	/** The observer is opt-in: no switch, no subsystem, no effect on normal play. */
	bool GetObservePath(FString& OutPath)
	{
		return FParse::Value(FCommandLine::Get(), TEXT("RxObserve="), OutPath) && !OutPath.IsEmpty();
	}

	const TCHAR* MovementModeToString(EMovementMode Mode)
	{
		switch (Mode)
		{
		case MOVE_None:      return TEXT("None");
		case MOVE_Walking:   return TEXT("Walking");
		case MOVE_NavWalking:return TEXT("NavWalking");
		case MOVE_Falling:   return TEXT("Falling");
		case MOVE_Swimming:  return TEXT("Swimming");
		case MOVE_Flying:    return TEXT("Flying");
		case MOVE_Custom:    return TEXT("Custom");
		default:             return TEXT("Unknown");
		}
	}
}

bool URxRuntimeObserverSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Unused;
	if (!GetObservePath(Unused))
	{
		return false;
	}

	// Only in a world that actually plays. Editor/preview worlds have no possessed pawn.
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	if (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE)
	{
		return false;
	}

	// MEASURED: the engine creates a transient "Untitled" world BEFORE loading the real
	// map. An earlier revision attached to it, and that world's teardown fired the
	// Deinitialize fallback -> RequestExit, killing the session before the real map ever
	// ticked (sample_count was 0). Require a real /Game/ package so only the loaded map
	// is observed.
	const FString PackageName = World->GetOutermost() ? World->GetOutermost()->GetName() : FString();
	return PackageName.StartsWith(TEXT("/Game/"));
}

void URxRuntimeObserverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	GetObservePath(OutputPath);

	float ParsedSeconds = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("RxObserveSeconds="), ParsedSeconds) && ParsedSeconds > 0.f)
	{
		SecondsBudget = ParsedSeconds;
	}

	UE_LOG(LogRxObserver, Warning,
		TEXT("RX_OBSERVER_ACTIVE output=%s budget=%.1fs world=%s"),
		*OutputPath, SecondsBudget, *GetWorld()->GetName());
}

void URxRuntimeObserverSubsystem::Deinitialize()
{
	// If the process is torn down before the budget expires, still emit what was seen --
	// but ONLY if something was actually observed, and NEVER request exit from here.
	// Requesting exit on teardown is what broke the first revision.
	if (!bReportWritten && SampleCount > 0)
	{
		WriteReport(TEXT("deinitialize_before_budget"), /*bRequestExit=*/false);
	}
	Super::Deinitialize();
}

bool URxRuntimeObserverSubsystem::IsTickable() const
{
	return Phase != EPhase::Done && !OutputPath.IsEmpty();
}

TStatId URxRuntimeObserverSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URxRuntimeObserverSubsystem, STATGROUP_Tickables);
}

void URxRuntimeObserverSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ElapsedSeconds += DeltaTime;

	APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
	ACharacter* Character = Cast<ACharacter>(Pawn);
	if (!Character)
	{
		if (ElapsedSeconds > SecondsBudget)
		{
			WriteReport(TEXT("budget_expired_no_pawn"));
		}
		return;
	}

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		WriteReport(TEXT("pawn_has_no_character_movement"));
		return;
	}

	const FVector Location = Character->GetActorLocation();
	const bool bOnGround = Movement->IsMovingOnGround();

	if (!bPawnFound)
	{
		bPawnFound    = true;
		SpawnLocation = Location;
		Phase         = EPhase::Settling;
		UE_LOG(LogRxObserver, Warning, TEXT("RX_PAWN_FOUND %s at %s"),
			*Character->GetName(), *Location.ToString());
	}

	++SampleCount;
	MinZ             = FMath::Min(MinZ, Location.Z);
	MaxForwardX      = FMath::Max(MaxForwardX, Location.X);
	LastMovementMode = MovementModeToString(Movement->MovementMode);
	FinalLocation    = Location;
	if (bOnGround)
	{
		++GroundSampleCount;
	}

	// ---- camera: what the player is actually looking through -------------------
	// Not "does a CameraComponent exist" (already known) but "is the view target the
	// pawn, and does the camera track it at the spring-arm distance".
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
		{
			const FVector CamLoc = CamMgr->GetCameraLocation();
			const AActor* ViewTarget = PC->GetViewTarget();

			if (!bCameraFound)
			{
				bCameraFound        = true;
				CameraStartLocation = CamLoc;
			}
			CameraFinalLocation = CamLoc;

			if (ViewTarget)
			{
				ViewTargetName    = ViewTarget->GetName();
				bViewTargetIsPawn = (ViewTarget == Character);
			}

			const double CamDist = FVector::Dist(CamLoc, Location);
			MinCameraDistance = FMath::Min(MinCameraDistance, CamDist);
			MaxCameraDistance = FMath::Max(MaxCameraDistance, CamDist);
		}
	}

	switch (Phase)
	{
	case EPhase::Settling:
		// Landing is the collision proof: gravity pulled the capsule down and something
		// stopped it. On the no-floor control map this never becomes true.
		if (bOnGround)
		{
			bEverOnGround   = true;
			SettleSeconds   = ElapsedSeconds;
			SettledLocation = Location;
			Phase           = EPhase::Driving;
			UE_LOG(LogRxObserver, Warning,
				TEXT("RX_SETTLED after %.2fs at Z=%.2f mode=%s"),
				SettleSeconds, Location.Z, *LastMovementMode);
		}
		else if (ElapsedSeconds > SecondsBudget)
		{
			WriteReport(TEXT("budget_expired_never_settled"));
		}
		break;

	case EPhase::Driving:
		// Drive forward (+X) toward the blocking wall at X=600. Two things get proven:
		// that input moves the pawn at all, and that the wall stops it.
		DriveSeconds += DeltaTime;
		Character->AddMovementInput(FVector::ForwardVector, 1.0f);
		if (DriveSeconds > (SecondsBudget * 0.5) || ElapsedSeconds > SecondsBudget)
		{
			WriteReport(TEXT("drive_complete"));
		}
		break;

	default:
		break;
	}
}

void URxRuntimeObserverSubsystem::WriteReport(const TCHAR* Reason, bool bRequestExit)
{
	if (bReportWritten)
	{
		return;
	}
	bReportWritten = true;
	Phase          = EPhase::Done;

	const double TravelledX = (bEverOnGround && MaxForwardX > -TNumericLimits<double>::Max())
		? (MaxForwardX - SettledLocation.X)
		: 0.0;

	// Report facts, not a pass/fail opinion. The caller asserts, and asserts differently
	// for the floored map than for the no-floor control.
	const FString Json = FString::Printf(
		TEXT("{\n")
		TEXT("  \"reason\": \"%s\",\n")
		TEXT("  \"world\": \"%s\",\n")
		TEXT("  \"pawn_found\": %s,\n")
		TEXT("  \"ever_on_ground\": %s,\n")
		TEXT("  \"settle_seconds\": %.3f,\n")
		TEXT("  \"spawn_z\": %.3f,\n")
		TEXT("  \"settled_z\": %.3f,\n")
		TEXT("  \"final_z\": %.3f,\n")
		TEXT("  \"min_z\": %.3f,\n")
		TEXT("  \"settled_x\": %.3f,\n")
		TEXT("  \"max_forward_x\": %.3f,\n")
		TEXT("  \"travelled_x\": %.3f,\n")
		TEXT("  \"sample_count\": %d,\n")
		TEXT("  \"ground_sample_count\": %d,\n")
		TEXT("  \"last_movement_mode\": \"%s\",\n")
		TEXT("  \"elapsed_seconds\": %.3f,\n")
		TEXT("  \"drive_seconds\": %.3f,\n")
		TEXT("  \"camera_found\": %s,\n")
		TEXT("  \"view_target\": \"%s\",\n")
		TEXT("  \"view_target_is_pawn\": %s,\n")
		TEXT("  \"camera_start_x\": %.3f,\n")
		TEXT("  \"camera_final_x\": %.3f,\n")
		TEXT("  \"camera_travelled_x\": %.3f,\n")
		TEXT("  \"camera_min_distance\": %.3f,\n")
		TEXT("  \"camera_max_distance\": %.3f\n")
		TEXT("}\n"),
		Reason,
		GetWorld() ? *GetWorld()->GetName() : TEXT("none"),
		bPawnFound ? TEXT("true") : TEXT("false"),
		bEverOnGround ? TEXT("true") : TEXT("false"),
		SettleSeconds,
		SpawnLocation.Z,
		SettledLocation.Z,
		FinalLocation.Z,
		(MinZ == TNumericLimits<double>::Max()) ? 0.0 : MinZ,
		SettledLocation.X,
		(MaxForwardX == -TNumericLimits<double>::Max()) ? 0.0 : MaxForwardX,
		TravelledX,
		SampleCount,
		GroundSampleCount,
		*LastMovementMode,
		ElapsedSeconds,
		DriveSeconds,
		bCameraFound ? TEXT("true") : TEXT("false"),
		*ViewTargetName,
		bViewTargetIsPawn ? TEXT("true") : TEXT("false"),
		CameraStartLocation.X,
		CameraFinalLocation.X,
		CameraFinalLocation.X - CameraStartLocation.X,
		(MinCameraDistance ==  TNumericLimits<double>::Max()) ? 0.0 : MinCameraDistance,
		(MaxCameraDistance == -TNumericLimits<double>::Max()) ? 0.0 : MaxCameraDistance);

	const bool bSaved = FFileHelper::SaveStringToFile(Json, *OutputPath);
	UE_LOG(LogRxObserver, Warning, TEXT("RX_OBSERVER_REPORT saved=%s path=%s reason=%s"),
		bSaved ? TEXT("true") : TEXT("false"), *OutputPath, Reason);

	// End the session deterministically instead of relying on an external timeout --
	// but only when the observation actually completed.
	if (bRequestExit)
	{
		FPlatformMisc::RequestExit(false);
	}
}

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RxOracleCommandlet.generated.h"

/**
 * UReflexionOracleCommandlet — headless verification harness that drives the
 * ported C++ FRxSimWorld and proves parity against the Godot-faithful Python
 * oracle (Reflexion-Arena/tools/oracle).
 *
 * It does NOT change any sim numeric constant, hashing algorithm, or behavior.
 * It only OBSERVES: parses the acceptance script + adversarial fixtures, replays
 * the sim exactly as run_acceptance.py / run_adversarial.py do, and prints the
 * results (final_state_hash, beat ticks, receipt count/head, adversarial pass
 * count) in a clearly-parseable "ORACLE|" block.
 *
 * Run headless:
 *   UnrealEditor-Cmd <project> -run=ReflexionOracle -nullrhi -unattended -nosplash
 */
UCLASS()
class UReflexionOracleCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UReflexionOracleCommandlet();

	virtual int32 Main(const FString& Params) override;
};

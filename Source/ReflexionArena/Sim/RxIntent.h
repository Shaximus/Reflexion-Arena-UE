#pragma once

#include "CoreMinimal.h"
#include "RxCanonJson.h"

/**
 * RxIntent.h — closed-vocabulary, deterministic instruction parser, ported from
 * the Godot reference (Reflexion-Arena/game/sim/intent.gd). Zero model inference,
 * no RNG, no floats: identical output for identical (Text, Reference, SessionState)
 * on every run/machine (CONTRACTS.md §0). SHENRON §5/§10/§11.
 *
 * SessionState (an FRxJsonValue Object owned by CompanionAI) is mutated in place:
 *   "stabilize_bridge_calls" : Int
 *   "pending_correction"     : Int (0/1)   — bool stored as int (no GetBool)
 *   "corrected"              : Int (0/1)
 * A fresh/empty Object is a valid initial state (missing keys read 0).
 * CompanionAI mirrors "corrected" into world flag "correction_made".
 */
struct FRxIntentResult
{
	bool bOk = false;
	FString Intent;
	FRxJsonValue Target = FRxJsonValue::Object();
	FString Clarification;
	bool bCorrection = false; // true only on the correction path (mirrors the .gd "correction" key presence)
};

class FRxIntent
{
public:
	static FRxIntentResult Parse(const FString& Text, const FRxJsonValue& Reference, FRxJsonValue& SessionState);

private:
	static FRxJsonValue BuildTarget(const FRxJsonValue& Reference, const FString& Side);
	static FString Normalize(const FString& Text);
	static int32 StableIndex(const FString& Text, int32 Size);
};

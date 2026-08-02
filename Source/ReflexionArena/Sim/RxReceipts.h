#pragma once

#include "CoreMinimal.h"
#include "RxCanonJson.h"

/**
 * RxReceipts.h — hash-chained command receipts, ported bit-for-bit from the
 * Godot reference (Reflexion-Arena/game/sim/receipts.gd), CONTRACTS.md §2.
 *
 * Every applied command is sealed into a receipt:
 *   {"seq":n,"tick":t,"cmd_hash":h,"prev":head,"result_code":c,"state_hash":s}
 * head = sha256 of the receipt's canonical JSON, appended to the chain.
 * Verification walks the chain recomputing every link. Plain C++, no UObjects.
 */

class FRxSimWorld; // forward declaration (contract wiring only)

/** One chain link. Field set + types mirror the Godot receipt Dictionary. */
struct FRxReceipt
{
	int32 Seq = -1;
	int32 Tick = -1;
	FString CmdHash;
	FString Prev;
	FString ResultCode;
	FString StateHash;

	/**
	 * Canonical JSON object with EXACTLY the reference key set/types
	 * (seq/tick are ints; the rest strings). This is the value that gets hashed,
	 * so it must reconstruct byte-for-byte what receipts.gd hashed.
	 */
	FRxJsonValue ToJson() const;
};

/** Result of Verify() — mirrors {"ok","count","head"[,"detail"]}. */
struct FRxVerifyResult
{
	bool bOk = false;
	int32 Count = 0;
	FString Head;
	FString Detail;
};

/**
 * FRxReceipts — the receipt chain. Mirrors `class_name Receipts` method set:
 * Record / Verify / Snapshot, with a "GENESIS" seed head.
 */
class FRxReceipts
{
public:
	TArray<FRxReceipt> Chain;
	FString Head = TEXT("GENESIS");

	/**
	 * Seal one applied command.
	 *   Cmd    — the applied command envelope (must carry seq + tick by now).
	 *   Result — the validate/apply result (its "code" field feeds result_code).
	 *   StateHash — SimWorld.state_hash() at seal time.
	 * Advances Head to hash_value(receipt) and appends. Returns the receipt.
	 */
	FRxReceipt Record(const FRxJsonValue& Cmd, const FRxJsonValue& Result, const FString& StateHash);

	/** Walk the chain: recompute each link, check prev-linkage and head. */
	FRxVerifyResult Verify() const;

	/** Canonical-safe snapshot fragment: {"chain":[...],"head":...}. */
	FRxJsonValue Snapshot() const;
};

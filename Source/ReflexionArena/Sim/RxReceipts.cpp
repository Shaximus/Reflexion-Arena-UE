#include "RxReceipts.h"

/**
 * RxReceipts.cpp — implementation mirroring receipts.gd record/verify/snapshot.
 * All hashing routes through FRxCanonJson so the chain is byte-identical to the
 * Godot product truth (CONTRACTS.md §0/§2).
 */

FRxJsonValue FRxReceipt::ToJson() const
{
	FRxJsonValue R = FRxJsonValue::Object();
	R.Set(TEXT("seq"), FRxJsonValue::Int(Seq));
	R.Set(TEXT("tick"), FRxJsonValue::Int(Tick));
	R.Set(TEXT("cmd_hash"), FRxJsonValue::Str(CmdHash));
	R.Set(TEXT("prev"), FRxJsonValue::Str(Prev));
	R.Set(TEXT("result_code"), FRxJsonValue::Str(ResultCode));
	R.Set(TEXT("state_hash"), FRxJsonValue::Str(StateHash));
	return R;
}

FRxReceipt FRxReceipts::Record(const FRxJsonValue& Cmd, const FRxJsonValue& Result, const FString& StateHash)
{
	FRxReceipt Receipt;
	// int(cmd.get("seq", -1)) / int(cmd.get("tick", -1)) — narrow int64->int32.
	Receipt.Seq = static_cast<int32>(Cmd.GetInt(TEXT("seq"), -1));
	Receipt.Tick = static_cast<int32>(Cmd.GetInt(TEXT("tick"), -1));
	Receipt.CmdHash = FRxCanonJson::HashValue(Cmd);
	Receipt.Prev = Head;
	Receipt.ResultCode = Result.GetString(TEXT("code"), TEXT(""));
	Receipt.StateHash = StateHash;

	// head = sha256 of the canonical receipt; then append (order matters).
	Head = FRxCanonJson::HashValue(Receipt.ToJson());
	Chain.Add(Receipt);
	return Receipt;
}

FRxVerifyResult FRxReceipts::Verify() const
{
	FRxVerifyResult Out;
	FString Prev = TEXT("GENESIS");
	for (int32 i = 0; i < Chain.Num(); ++i)
	{
		const FRxReceipt& R = Chain[i];
		if (R.Prev != Prev)
		{
			Out.bOk = false;
			Out.Count = i;
			Out.Head = Head;
			Out.Detail = FString::Printf(TEXT("broken prev link at index %d"), i);
			return Out;
		}
		Prev = FRxCanonJson::HashValue(R.ToJson());
	}
	if (Prev != Head)
	{
		Out.bOk = false;
		Out.Count = Chain.Num();
		Out.Head = Head;
		Out.Detail = TEXT("head mismatch");
		return Out;
	}
	Out.bOk = true;
	Out.Count = Chain.Num();
	Out.Head = Head;
	return Out;
}

FRxJsonValue FRxReceipts::Snapshot() const
{
	FRxJsonValue ChainArr = FRxJsonValue::Array();
	for (const FRxReceipt& R : Chain)
	{
		ChainArr.Push(R.ToJson());
	}
	FRxJsonValue Snap = FRxJsonValue::Object();
	Snap.Set(TEXT("chain"), MoveTemp(ChainArr));
	Snap.Set(TEXT("head"), FRxJsonValue::Str(Head));
	return Snap;
}

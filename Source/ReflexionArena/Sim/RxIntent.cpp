#include "RxIntent.h"

// Ported 1:1 from intent.gd. Closed vocab; longest keyword wins, ties broken by
// VOCAB order (strictly-greater comparison keeps the earlier entry); deterministic
// misunderstanding fixture (first bridge_support stabilize → WEST; correction → EAST).

namespace
{
	// VOCAB order is load-bearing (tie-break). Keyword lists mirror KEYWORDS.
	struct FVocabEntry { const TCHAR* Word; TArray<FString> Keywords; };

	const TArray<FVocabEntry>& Vocab()
	{
		static const TArray<FVocabEntry> V = {
			{ TEXT("stabilize"), { TEXT("stabilize"), TEXT("hold"), TEXT("brace"), TEXT("fix") } },
			{ TEXT("strike"),    { TEXT("hit"), TEXT("attack"), TEXT("smash"), TEXT("break") } },
			{ TEXT("dodge_to"),  { TEXT("move"), TEXT("go"), TEXT("dodge"), TEXT("retreat") } },
			{ TEXT("anchor"),    { TEXT("anchor"), TEXT("root"), TEXT("pin") } },
			{ TEXT("weave"),     { TEXT("weave"), TEXT("build"), TEXT("fabricate") } },
			{ TEXT("interrupt"), { TEXT("stop"), TEXT("interrupt"), TEXT("cancel that") } },
			{ TEXT("follow"),    { TEXT("follow"), TEXT("with me") } },
			{ TEXT("wait"),      { TEXT("wait"), TEXT("hold position"), TEXT("stay") } },
		};
		return V;
	}

	const TArray<FString>& Corrections()
	{
		static const TArray<FString> C = {
			TEXT("the other one"), TEXT("other one"), TEXT("wrong one"),
			TEXT("not that one"), TEXT("not that"), TEXT("east"), TEXT("no"),
		};
		return C;
	}

	const TArray<FString>& Clarify()
	{
		static const TArray<FString> C = {
			TEXT("I don't follow. Anchor, strike, move, or wait?"),
			TEXT("Unclear. Reference a target and name the action?"),
			TEXT("Give me one of: stabilize, strike, move, anchor, weave, interrupt, follow, wait."),
		};
		return C;
	}

	// " " + kw + " " substring test against the padded haystack (case-sensitive; all lowercase).
	FORCEINLINE bool HayContains(const FString& Hay, const FString& Kw)
	{
		return Hay.Contains(FString(TEXT(" ")) + Kw + TEXT(" "), ESearchCase::CaseSensitive);
	}

	// Session bools are stored as Int(0/1).
	FORCEINLINE bool SessBool(const FRxJsonValue& S, const TCHAR* Key)
	{
		return S.GetInt(Key, 0) != 0;
	}
}

FString FRxIntent::Normalize(const FString& Text)
{
	const FString T = Text.ToLower();
	FString Out;
	bool bPendingSpace = false;
	for (int32 i = 0; i < T.Len(); ++i)
	{
		const TCHAR C = T[i];
		const bool bIsWord = (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9'));
		if (bIsWord)
		{
			if (bPendingSpace && !Out.IsEmpty())
			{
				Out += TEXT(" ");
			}
			bPendingSpace = false;
			Out.AppendChar(C);
		}
		else
		{
			bPendingSpace = true;
		}
	}
	return Out;
}

int32 FRxIntent::StableIndex(const FString& Text, int32 Size)
{
	int64 Acc = 0;
	for (int32 i = 0; i < Text.Len(); ++i)
	{
		Acc = (Acc + static_cast<int64>(Text[i]) * (i + 1)) % 1000003;
	}
	return static_cast<int32>(Acc % Size);
}

FRxJsonValue FRxIntent::BuildTarget(const FRxJsonValue& Reference, const FString& Side)
{
	FRxJsonValue T = FRxJsonValue::Object();
	T.Set(TEXT("kind"), FRxJsonValue::Str(Reference.GetString(TEXT("kind"))));
	if (!Side.IsEmpty())
	{
		T.Set(TEXT("support"), FRxJsonValue::Str(Side));
	}

	const FRxJsonValue* Src = &Reference;
	if (!Side.IsEmpty())
	{
		if (const FRxJsonValue* Alt = Reference.Find(TEXT("alt_supports")))
		{
			if (Alt->Type == ERxJsonType::Object)
			{
				if (const FRxJsonValue* SideVal = Alt->Find(Side))
				{
					if (SideVal->Type == ERxJsonType::Object)
					{
						Src = SideVal;
					}
				}
			}
		}
	}

	static const TCHAR* Keys[] = { TEXT("entity_id"), TEXT("region_id"), TEXT("pos"), TEXT("name") };
	for (const TCHAR* K : Keys)
	{
		if (const FRxJsonValue* V = Src->Find(K))
		{
			T.Set(K, *V);
		}
		else if (const FRxJsonValue* V2 = Reference.Find(K))
		{
			T.Set(K, *V2);
		}
	}
	return T;
}

FRxIntentResult FRxIntent::Parse(const FString& Text, const FRxJsonValue& Reference, FRxJsonValue& SessionState)
{
	const FString Hay = FString(TEXT(" ")) + Normalize(Text) + TEXT(" ");
	const FString Kind = Reference.GetString(TEXT("kind"));

	// 1) Correction path — only while a WEST mistarget is pending on the same bridge support.
	if (Kind == TEXT("bridge_support")
		&& SessBool(SessionState, TEXT("pending_correction"))
		&& !SessBool(SessionState, TEXT("corrected")))
	{
		for (const FString& Kw : Corrections())
		{
			if (HayContains(Hay, Kw))
			{
				SessionState.Set(TEXT("corrected"), FRxJsonValue::Int(1));
				SessionState.Set(TEXT("pending_correction"), FRxJsonValue::Int(0));

				FRxIntentResult R;
				R.bOk = true;
				R.Intent = TEXT("stabilize");
				R.Target = BuildTarget(Reference, TEXT("east"));
				R.Clarification = TEXT("");
				R.bCorrection = true;
				return R;
			}
		}
	}

	// 2) Keyword match: longest keyword wins; ties → earlier VOCAB entry.
	FString Best;
	int32 BestLen = 0;
	for (const FVocabEntry& Entry : Vocab())
	{
		for (const FString& Kw : Entry.Keywords)
		{
			if (Kw.Len() > BestLen && HayContains(Hay, Kw))
			{
				Best = Entry.Word;
				BestLen = Kw.Len();
			}
		}
	}

	if (Best.IsEmpty())
	{
		FRxIntentResult R;
		R.bOk = false;
		R.Intent = TEXT("");
		R.Target = FRxJsonValue::Object();
		R.Clarification = Clarify()[StableIndex(Text, Clarify().Num())];
		return R;
	}

	// 3) Target resolution + deterministic misunderstanding fixture.
	FRxJsonValue Target = BuildTarget(Reference, TEXT(""));
	if (Best == TEXT("stabilize") && Kind == TEXT("bridge_support"))
	{
		if (Reference.HasKey(TEXT("support")))
		{
			// Explicit world-independent disambiguation flag: respect it, no fixture.
			Target = BuildTarget(Reference, Reference.GetString(TEXT("support")));
		}
		else
		{
			const int64 Calls = SessionState.GetInt(TEXT("stabilize_bridge_calls"), 0);
			SessionState.Set(TEXT("stabilize_bridge_calls"), FRxJsonValue::Int(Calls + 1));
			if (SessBool(SessionState, TEXT("corrected")))
			{
				Target = BuildTarget(Reference, TEXT("east"));
			}
			else
			{
				Target = BuildTarget(Reference, TEXT("west"));
				SessionState.Set(TEXT("pending_correction"), FRxJsonValue::Int(1));
			}
		}
	}

	FRxIntentResult R;
	R.bOk = true;
	R.Intent = Best;
	R.Target = Target;
	R.Clarification = TEXT("");
	return R;
}

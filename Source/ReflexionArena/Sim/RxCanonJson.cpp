#include "RxCanonJson.h"

#include <type_traits>

// Sign-safe TCHAR -> code-unit value. TCHAR is char16_t on some platforms and
// (signed) wchar_t on others; make_unsigned prevents sign extension for values
// >= 0x80 so ordering/escape decisions are made on the true code-unit value.
static FORCEINLINE uint32 RxCodeUnit(TCHAR C)
{
	return static_cast<uint32>(static_cast<std::make_unsigned<TCHAR>::type>(C));
}

/**
 * RxCanonJson.cpp — implementation. The SHA-256 is implemented from scratch
 * (FIPS 180-4) rather than routed through an engine/platform crypto helper, so
 * the digest is bit-identical on every platform and provably matches GDScript's
 * HashingContext(HASH_SHA256) output — the only thing that guarantees replay/
 * hash parity with the Godot product truth.
 */

// ---------------------------------------------------------------------------
// FRxJsonValue
// ---------------------------------------------------------------------------

FRxJsonValue FRxJsonValue::Bool(bool bIn)
{
	FRxJsonValue V;
	V.Type = ERxJsonType::Bool;
	V.bValue = bIn;
	return V;
}

FRxJsonValue FRxJsonValue::Int(int64 InValue)
{
	FRxJsonValue V;
	V.Type = ERxJsonType::Int;
	V.IntValue = InValue;
	return V;
}

FRxJsonValue FRxJsonValue::Str(const FString& InValue)
{
	FRxJsonValue V;
	V.Type = ERxJsonType::String;
	V.StringValue = InValue;
	return V;
}

FRxJsonValue FRxJsonValue::Array()
{
	FRxJsonValue V;
	V.Type = ERxJsonType::Array;
	return V;
}

FRxJsonValue FRxJsonValue::Object()
{
	FRxJsonValue V;
	V.Type = ERxJsonType::Object;
	return V;
}

FRxJsonValue FRxJsonValue::IntPoint(const FIntPoint& P)
{
	FRxJsonValue V = FRxJsonValue::Array();
	V.Push(FRxJsonValue::Int(P.X));
	V.Push(FRxJsonValue::Int(P.Y));
	return V;
}

FRxJsonValue& FRxJsonValue::Push(FRxJsonValue Item)
{
	checkf(Type == ERxJsonType::Array, TEXT("FRxJsonValue::Push on non-array"));
	ArrayItems.Add(MoveTemp(Item));
	return *this;
}

FRxJsonValue& FRxJsonValue::Set(const FString& Key, FRxJsonValue InValue)
{
	checkf(Type == ERxJsonType::Object, TEXT("FRxJsonValue::Set on non-object"));
	for (TPair<FString, FRxJsonValue>& Pair : ObjectItems)
	{
		if (Pair.Key == Key)
		{
			Pair.Value = MoveTemp(InValue);
			return *this;
		}
	}
	ObjectItems.Emplace(Key, MoveTemp(InValue));
	return *this;
}

const FRxJsonValue* FRxJsonValue::Find(const FString& Key) const
{
	if (Type != ERxJsonType::Object)
	{
		return nullptr;
	}
	for (const TPair<FString, FRxJsonValue>& Pair : ObjectItems)
	{
		if (Pair.Key == Key)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

bool FRxJsonValue::HasKey(const FString& Key) const
{
	return Find(Key) != nullptr;
}

int64 FRxJsonValue::GetInt(const FString& Key, int64 Default) const
{
	const FRxJsonValue* Found = Find(Key);
	if (Found && Found->Type == ERxJsonType::Int)
	{
		return Found->IntValue;
	}
	return Default;
}

FString FRxJsonValue::GetString(const FString& Key, const FString& Default) const
{
	const FRxJsonValue* Found = Find(Key);
	if (Found && Found->Type == ERxJsonType::String)
	{
		return Found->StringValue;
	}
	return Default;
}

// ---------------------------------------------------------------------------
// Canonical serialization (mirrors canon_json.gd stringify / _escape_string)
// ---------------------------------------------------------------------------

bool FRxCanonJson::KeyLess(const FString& A, const FString& B)
{
	// Godot Array.sort() on Strings compares code point by code point. FString is
	// UTF-16; for the ASCII/BMP keys this sim uses, comparing TCHAR units by
	// unsigned value reproduces Godot's char32 ordering exactly.
	const int32 LenA = A.Len();
	const int32 LenB = B.Len();
	const int32 Min = FMath::Min(LenA, LenB);
	const TCHAR* PA = *A;
	const TCHAR* PB = *B;
	for (int32 i = 0; i < Min; ++i)
	{
		const uint32 CA = RxCodeUnit(PA[i]);
		const uint32 CB = RxCodeUnit(PB[i]);
		if (CA != CB)
		{
			return CA < CB;
		}
	}
	return LenA < LenB;
}

void FRxCanonJson::EscapeStringInto(const FString& S, FString& Out)
{
	// Mirrors canon_json.gd _escape_string: only the mandatory short escapes,
	// other control chars (< 0x20) as \u00XX (lowercase), everything else
	// verbatim. Iterating TCHAR units is sufficient because every specially
	// handled value (0x09/0x0A/0x0D/0x22/0x5C and all < 0x20) is a single
	// UTF-16 code unit; anything >= 0x20 is copied verbatim, so its UTF-8
	// encoding (produced at hash time) is byte-identical to Godot's substr(i,1).
	const TCHAR* Hex = TEXT("0123456789abcdef");
	Out.AppendChar(TEXT('"'));
	const int32 Len = S.Len();
	const TCHAR* P = *S;
	for (int32 i = 0; i < Len; ++i)
	{
		const uint32 C = RxCodeUnit(P[i]);
		switch (C)
		{
		case 0x22: // "
			Out.Append(TEXT("\\\""));
			break;
		case 0x5C: // backslash
			Out.Append(TEXT("\\\\"));
			break;
		case 0x0A:
			Out.Append(TEXT("\\n"));
			break;
		case 0x0D:
			Out.Append(TEXT("\\r"));
			break;
		case 0x09:
			Out.Append(TEXT("\\t"));
			break;
		default:
			if (C < 0x20)
			{
				Out.Append(TEXT("\\u00"));
				Out.AppendChar(Hex[(C >> 4) & 0xF]);
				Out.AppendChar(Hex[C & 0xF]);
			}
			else
			{
				Out.AppendChar(P[i]);
			}
			break;
		}
	}
	Out.AppendChar(TEXT('"'));
}

void FRxCanonJson::StringifyInto(const FRxJsonValue& Value, FString& Out)
{
	switch (Value.Type)
	{
	case ERxJsonType::Bool:
		Out.Append(Value.bValue ? TEXT("true") : TEXT("false"));
		break;

	case ERxJsonType::Int:
		// GDScript str(int) — plain decimal, '-' for negatives.
		Out.Append(FString::Printf(TEXT("%lld"), Value.IntValue));
		break;

	case ERxJsonType::String:
		EscapeStringInto(Value.StringValue, Out);
		break;

	case ERxJsonType::Array:
	{
		Out.AppendChar(TEXT('['));
		const int32 Num = Value.ArrayItems.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			if (i > 0)
			{
				Out.AppendChar(TEXT(','));
			}
			StringifyInto(Value.ArrayItems[i], Out);
		}
		Out.AppendChar(TEXT(']'));
		break;
	}

	case ERxJsonType::Object:
	{
		// Sort keys lexicographically (canon_json.gd: keys.sort()).
		TArray<int32> Order;
		Order.Reserve(Value.ObjectItems.Num());
		for (int32 i = 0; i < Value.ObjectItems.Num(); ++i)
		{
			Order.Add(i);
		}
		const TArray<TPair<FString, FRxJsonValue>>& Items = Value.ObjectItems;
		Order.Sort([&Items](const int32& A, const int32& B)
		{
			return FRxCanonJson::KeyLess(Items[A].Key, Items[B].Key);
		});

		Out.AppendChar(TEXT('{'));
		bool bFirst = true;
		for (int32 Idx : Order)
		{
			if (!bFirst)
			{
				Out.AppendChar(TEXT(','));
			}
			bFirst = false;
			EscapeStringInto(Items[Idx].Key, Out);
			Out.AppendChar(TEXT(':'));
			StringifyInto(Items[Idx].Value, Out);
		}
		Out.AppendChar(TEXT('}'));
		break;
	}

	default:
		// Unreachable: the closed value set forbids float/null/object handles.
		checkf(false, TEXT("FRxCanonJson: non-canonical value type"));
		break;
	}
}

FString FRxCanonJson::Stringify(const FRxJsonValue& Value)
{
	FString Out;
	StringifyInto(Value, Out);
	return Out;
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4) — self-contained, deterministic, platform-independent.
// ---------------------------------------------------------------------------

namespace
{
	FORCEINLINE uint32 RotR(uint32 x, uint32 n)
	{
		return (x >> n) | (x << (32 - n));
	}

	static const uint32 GSha256K[64] = {
		0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
		0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
		0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
		0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
		0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
		0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
		0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
		0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
	};

	void Sha256Compute(const uint8* Data, int32 Length, uint8 OutDigest[32])
	{
		uint32 H[8] = {
			0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
			0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
		};

		// Padded message length: append 0x80, then zeros, then 64-bit bit length,
		// to a multiple of 64 bytes.
		const uint64 BitLen = static_cast<uint64>(Length) * 8ull;
		int64 TotalLen = Length + 1; // 0x80
		while ((TotalLen % 64) != 56)
		{
			++TotalLen;
		}
		TotalLen += 8; // 64-bit length field

		TArray<uint8> Msg;
		Msg.SetNumUninitialized(static_cast<int32>(TotalLen));
		FMemory::Memcpy(Msg.GetData(), Data, Length);
		Msg[Length] = 0x80;
		for (int64 i = Length + 1; i < TotalLen - 8; ++i)
		{
			Msg[static_cast<int32>(i)] = 0;
		}
		for (int32 i = 0; i < 8; ++i)
		{
			Msg[static_cast<int32>(TotalLen) - 1 - i] = static_cast<uint8>((BitLen >> (8 * i)) & 0xFF);
		}

		const int64 Blocks = TotalLen / 64;
		for (int64 b = 0; b < Blocks; ++b)
		{
			const uint8* Block = Msg.GetData() + b * 64;
			uint32 W[64];
			for (int32 t = 0; t < 16; ++t)
			{
				W[t] = (static_cast<uint32>(Block[t * 4]) << 24)
					| (static_cast<uint32>(Block[t * 4 + 1]) << 16)
					| (static_cast<uint32>(Block[t * 4 + 2]) << 8)
					| (static_cast<uint32>(Block[t * 4 + 3]));
			}
			for (int32 t = 16; t < 64; ++t)
			{
				const uint32 s0 = RotR(W[t - 15], 7) ^ RotR(W[t - 15], 18) ^ (W[t - 15] >> 3);
				const uint32 s1 = RotR(W[t - 2], 17) ^ RotR(W[t - 2], 19) ^ (W[t - 2] >> 10);
				W[t] = W[t - 16] + s0 + W[t - 7] + s1;
			}

			uint32 a = H[0], bb = H[1], c = H[2], d = H[3];
			uint32 e = H[4], f = H[5], g = H[6], h = H[7];

			for (int32 t = 0; t < 64; ++t)
			{
				const uint32 S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
				const uint32 ch = (e & f) ^ ((~e) & g);
				const uint32 temp1 = h + S1 + ch + GSha256K[t] + W[t];
				const uint32 S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
				const uint32 maj = (a & bb) ^ (a & c) ^ (bb & c);
				const uint32 temp2 = S0 + maj;

				h = g;
				g = f;
				f = e;
				e = d + temp1;
				d = c;
				c = bb;
				bb = a;
				a = temp1 + temp2;
			}

			H[0] += a; H[1] += bb; H[2] += c; H[3] += d;
			H[4] += e; H[5] += f; H[6] += g; H[7] += h;
		}

		for (int32 i = 0; i < 8; ++i)
		{
			OutDigest[i * 4] = static_cast<uint8>((H[i] >> 24) & 0xFF);
			OutDigest[i * 4 + 1] = static_cast<uint8>((H[i] >> 16) & 0xFF);
			OutDigest[i * 4 + 2] = static_cast<uint8>((H[i] >> 8) & 0xFF);
			OutDigest[i * 4 + 3] = static_cast<uint8>(H[i] & 0xFF);
		}
	}
} // namespace

FString FRxCanonJson::Sha256Hex(const FString& Text)
{
	// SHA-256 over the UTF-8 bytes (matches text.to_utf8_buffer() in Godot).
	FTCHARToUTF8 Utf8(*Text);
	const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
	const int32 NumBytes = Utf8.Length();

	uint8 Digest[32];
	Sha256Compute(Bytes, NumBytes, Digest);

	// Lowercase hex (matches PackedByteArray.hex_encode()).
	const TCHAR* Hex = TEXT("0123456789abcdef");
	FString Out;
	Out.Reserve(64);
	for (int32 i = 0; i < 32; ++i)
	{
		Out.AppendChar(Hex[(Digest[i] >> 4) & 0xF]);
		Out.AppendChar(Hex[Digest[i] & 0xF]);
	}
	return Out;
}

FString FRxCanonJson::HashValue(const FRxJsonValue& Value)
{
	return Sha256Hex(Stringify(Value));
}

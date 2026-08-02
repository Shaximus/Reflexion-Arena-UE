#pragma once

#include "CoreMinimal.h"

/**
 * RxCanonJson.h — canonical JSON + deterministic hash, ported bit-for-bit from
 * the Godot reference (Reflexion-Arena/game/sim/canon_json.gd) so that hashes
 * computed here MATCH the Godot product truth exactly (CONTRACTS.md §0). Replay/
 * hash parity across engines is the entire point of the port.
 *
 * Canonical JSON law (CONTRACTS.md §0):
 *   - dict keys sorted lexicographically (by Unicode code point), no whitespace
 *   - ints / strings / bools / arrays / dicts ONLY — never float, never null
 * Hash: SHA-256 over the canonical-JSON UTF-8 bytes, lowercase hex string
 *   (GDScript: HashingContext HASH_SHA256 + PackedByteArray.hex_encode()).
 *
 * The reference is dynamically typed (Godot Variant). Here we mirror the exact
 * closed value set with a tagged value type — plain C++, no UObjects.
 */

// Forward declaration (contract wiring; not needed by the serializer itself).
class FRxSimWorld;

enum class ERxJsonType : uint8
{
	Bool,
	Int,
	String,
	Array,
	Object
};

/**
 * FRxJsonValue — the closed canonical value set (bool/int/string/array/object).
 * Objects are stored as an ordered key/value list; keys are sorted only at
 * stringify time (mirroring `keys.sort()` in canon_json.gd), so insertion order
 * never affects the canonical string or the hash.
 *
 * Ints are int64 to match GDScript's 64-bit `int` (e.g. the 63-bit rng_state).
 */
struct FRxJsonValue
{
	ERxJsonType Type = ERxJsonType::Int;

	bool bValue = false;
	int64 IntValue = 0;
	FString StringValue;
	TArray<FRxJsonValue> ArrayItems;
	TArray<TPair<FString, FRxJsonValue>> ObjectItems;

	FRxJsonValue() = default;

	// --- factories ---
	static FRxJsonValue Bool(bool bIn);
	static FRxJsonValue Int(int64 InValue);
	static FRxJsonValue Str(const FString& InValue);
	static FRxJsonValue Array();
	static FRxJsonValue Object();

	/** Vector2i mirror: emits [x,y] exactly as sim_world.gd snapshot() does. */
	static FRxJsonValue IntPoint(const FIntPoint& P);

	// --- builders (return *this for chaining) ---
	/** Append to an Array value. */
	FRxJsonValue& Push(FRxJsonValue Item);
	/** Set/overwrite a key on an Object value (last-write-wins, like a Dict). */
	FRxJsonValue& Set(const FString& Key, FRxJsonValue InValue);

	// --- read accessors (Object only; return default when absent/mismatched) ---
	bool HasKey(const FString& Key) const;
	int64 GetInt(const FString& Key, int64 Default = 0) const;
	FString GetString(const FString& Key, const FString& Default = FString()) const;
	const FRxJsonValue* Find(const FString& Key) const;
};

/**
 * FRxCanonJson — static canonical serializer + SHA-256 hasher.
 * Mirrors `class_name CanonJson` (canon_json.gd) method-for-method.
 */
class FRxCanonJson
{
public:
	/** Canonical JSON string per CONTRACTS.md §0 (mirrors CanonJson.stringify). */
	static FString Stringify(const FRxJsonValue& Value);

	/** Lowercase hex SHA-256 of the UTF-8 bytes of Text (mirrors sha256_hex). */
	static FString Sha256Hex(const FString& Text);

	/** sha256_hex(stringify(value)) — mirrors CanonJson.hash_value. */
	static FString HashValue(const FRxJsonValue& Value);

private:
	static void StringifyInto(const FRxJsonValue& Value, FString& Out);
	static void EscapeStringInto(const FString& S, FString& Out);
	/** Code-point-wise lexicographic compare (matches Godot String::operator<). */
	static bool KeyLess(const FString& A, const FString& B);
};

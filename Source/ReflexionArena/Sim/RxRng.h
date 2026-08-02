#pragma once

#include "CoreMinimal.h"

/**
 * FRxRng — deterministic SplitMix64, ported from the proven Godot reference
 * (Reflexion-Arena/game/sim/rng.gd) to preserve bit-identical output and thus
 * replay/hash parity across the engine switch (CONTRACTS.md §0 determinism law).
 *
 * Canonical SplitMix64:
 *   state += 0x9E3779B97F4A7C15
 *   z = state
 *   z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
 *   z = (z ^ (z >> 27)) * 0x94D049BB133111EB
 *   z = z ^ (z >> 31)
 *   return z & 0x7FFFFFFFFFFFFFFF        // masked to 63 bits, always non-negative
 *
 * The Godot version emulated unsigned math inside signed int64 (two's-complement
 * decimal constants + a logical-shift helper) because Godot 4.7 rejects hex
 * literals above int64-max. Native uint64 here makes that emulation unnecessary
 * while producing identical bits. No floats, no engine RNG — sim determinism only.
 */
struct FRxRng
{
	uint64 State = 0;

	FRxRng() = default;
	explicit FRxRng(uint64 InSeed) : State(InSeed) {}

	FORCEINLINE uint64 NextU64()
	{
		State += 0x9E3779B97F4A7C15ULL;
		uint64 z = State;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		z = z ^ (z >> 31);
		return z & 0x7FFFFFFFFFFFFFFFULL; // 63-bit mask: matches gd rng non-negative contract
	}

	/** bound > 0; returns NextU64() % bound per contract §2. */
	FORCEINLINE uint64 NextInt(uint64 Bound)
	{
		checkf(Bound > 0, TEXT("FRxRng::NextInt: bound must be > 0"));
		return NextU64() % Bound;
	}
};

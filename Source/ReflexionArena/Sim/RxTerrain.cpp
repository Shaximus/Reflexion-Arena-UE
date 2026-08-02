#include "RxTerrain.h"

// RxTerrain.cpp — see RxTerrain.h for the model narrative and provenance.
// Behavioural mirror of Reflexion-Arena/game/sim/terrain.gd. Every numeric
// threshold, iteration order, and integer-truncation point matches the Godot
// reference so replay hashes stay bit-identical (CONTRACTS.md §0).

// ---------------------------------------------------------------------------
// Definition loading
// ---------------------------------------------------------------------------
void FRxTerrain::LoadDef(const FRxTerrainDef& Def)
{
	Regions = Def.Regions;
	Edges = Def.Edges;
	StressSchedule = Def.StressSchedule;
	Time = 0;
}

// ---------------------------------------------------------------------------
// Region lookup / read accessors
// ---------------------------------------------------------------------------
FRxRegion* FRxTerrain::FindRegion(int32 RegionId)
{
	for (FRxRegion& R : Regions)
	{
		if (R.Id == RegionId)
		{
			return &R;
		}
	}
	return nullptr;
}

const FRxRegion* FRxTerrain::FindRegion(int32 RegionId) const
{
	for (const FRxRegion& R : Regions)
	{
		if (R.Id == RegionId)
		{
			return &R;
		}
	}
	return nullptr;
}

bool FRxTerrain::RegionExists(int32 RegionId) const
{
	return FindRegion(RegionId) != nullptr;
}

int32 FRxTerrain::StressOf(int32 RegionId) const
{
	const FRxRegion* R = FindRegion(RegionId);
	return R != nullptr ? R->Stress : 0;
}

// ---------------------------------------------------------------------------
// Geometry: point-in-polygon (even-odd rule, exact integer arithmetic)
// ---------------------------------------------------------------------------
bool FRxTerrain::PointInPoly(const FIntPoint& P, const TArray<FIntPoint>& Poly)
{
	const int32 N = Poly.Num();
	if (N < 3)
	{
		return false;
	}
	bool bInside = false;
	int32 J = N - 1;
	for (int32 I = 0; I < N; ++I)
	{
		// 64-bit to mirror Godot's 64-bit int and avoid overflow in the cross term.
		const int64 Xi = Poly[I].X;
		const int64 Yi = Poly[I].Y;
		const int64 Xj = Poly[J].X;
		const int64 Yj = Poly[J].Y;
		const int64 Py = P.Y;
		const int64 Px = P.X;

		if ((Yi > Py) != (Yj > Py))
		{
			// sign-exact form of  x < xi + (xj-xi)*(p.y-yi)/(yj-yi)
			const int64 Lhs = (Px - Xi) * (Yj - Yi);
			const int64 Rhs = (Xj - Xi) * (Py - Yi);
			if ((Yj - Yi) > 0)
			{
				if (Lhs < Rhs)
				{
					bInside = !bInside;
				}
			}
			else
			{
				if (Lhs > Rhs)
				{
					bInside = !bInside;
				}
			}
		}
		J = I;
	}
	return bInside;
}

int32 FRxTerrain::RegionAt(const FIntPoint& Pos) const
{
	for (const FRxRegion& R : Regions)
	{
		if (PointInPoly(Pos, R.Poly))
		{
			return R.Id;
		}
	}
	return -1;
}

// ---------------------------------------------------------------------------
// Stress mutators
// ---------------------------------------------------------------------------
void FRxTerrain::AddStress(int32 RegionId, int32 Amount)
{
	if (FRxRegion* R = FindRegion(RegionId))
	{
		R->Stress += Amount;
	}
}

void FRxTerrain::Dampen(int32 RegionId, int32 Amount)
{
	if (FRxRegion* R = FindRegion(RegionId))
	{
		R->Stress = FMath::Max(0, R->Stress - Amount);
	}
}

void FRxTerrain::Anchor(int32 RegionId, FRxEntityId EntityId)
{
	if (FRxRegion* R = FindRegion(RegionId))
	{
		R->AnchoredBy = EntityId;
	}
}

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------
bool FRxTerrain::RegionsConnected(int32 A, int32 B) const
{
	if (A == B)
	{
		return A != -1;
	}
	if (A == -1 || B == -1)
	{
		return false;
	}
	TSet<int32> Visited;
	Visited.Add(A);
	TArray<int32> Queue;
	Queue.Add(A);
	int32 Head = 0;
	while (Head < Queue.Num())
	{
		const int32 Cur = Queue[Head++];
		for (const FRxEdge& E : Edges)
		{
			int32 Nxt = -1;
			if (E.A == Cur)
			{
				Nxt = E.B;
			}
			else if (E.B == Cur)
			{
				Nxt = E.A;
			}
			if (Nxt == -1 || Visited.Contains(Nxt))
			{
				continue;
			}
			if (Nxt == B)
			{
				return true;
			}
			Visited.Add(Nxt);
			Queue.Add(Nxt);
		}
	}
	return false;
}

TArray<int32> FRxTerrain::Neighbors(int32 RegionId) const
{
	TArray<int32> Out;
	for (const FRxEdge& E : Edges)
	{
		if (E.A == RegionId)
		{
			Out.Add(E.B);
		}
		else if (E.B == RegionId)
		{
			Out.Add(E.A);
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Release wave (BFS)
// ---------------------------------------------------------------------------
FRxRelease FRxTerrain::BuildWave(int32 Origin, int32 Force) const
{
	FRxRelease Release;
	Release.Origin = Origin;

	TMap<int32, int32> Dist; // region id -> hop count (also serves as "visited")
	Dist.Add(Origin, 0);
	TArray<int32> Queue;
	Queue.Add(Origin);
	int32 Head = 0;
	while (Head < Queue.Num())
	{
		const int32 Cur = Queue[Head++];
		const int32 Hop = Dist[Cur];
		const int32 F = Force - Hop * RxSim::DECAY_PER_HOP;
		if (F > 0)
		{
			FRxWaveCell Cell;
			Cell.Region = Cur;
			Cell.Force = F;
			Cell.DelayTicks = Hop * RxSim::HOP_DELAY_TICKS;
			Release.Wave.Add(Cell);
		}
		for (int32 Nxt : Neighbors(Cur))
		{
			if (!Dist.Contains(Nxt))
			{
				Dist.Add(Nxt, Hop + 1);
				Queue.Add(Nxt);
			}
		}
	}
	return Release;
}

FRxRelease FRxTerrain::ForceRelease(int32 Origin, int32 Force) const
{
	return BuildWave(Origin, Force);
}

// ---------------------------------------------------------------------------
// One simulation tick (20Hz diffusion + release pass)
// ---------------------------------------------------------------------------
TArray<FRxRelease> FRxTerrain::Tick()
{
	Time += 1;

	// 1) data-driven stress sources (hazards / precursors)
	for (const FRxStressEvent& S : StressSchedule)
	{
		if (S.Tick <= Time && Time < S.Until)
		{
			AddStress(S.Region, S.Rate);
		}
	}

	// 2) anchored regions dissipate stress (counterplay: anchor)
	for (FRxRegion& R : Regions)
	{
		if (R.AnchoredBy != -1 && R.Stress > 0)
		{
			R.Stress = FMath::Max(0, R.Stress - RxSim::ANCHOR_DRAIN);
		}
	}

	// 3) ambient diffusion over edges (edge-array order; pillars never diffuse)
	for (const FRxEdge& E : Edges)
	{
		FRxRegion* Ra = FindRegion(E.A);
		FRxRegion* Rb = FindRegion(E.B);
		if (Ra == nullptr || Rb == nullptr)
		{
			continue;
		}
		if (Ra->Kind == TEXT("pillar") || Rb->Kind == TEXT("pillar"))
		{
			continue;
		}
		const int32 Sa = Ra->Stress;
		const int32 Sb = Rb->Stress;
		if (Sa > Sb)
		{
			// Integer division truncates toward zero; numerator > 0 so it matches
			// Godot's `/` on positive ints (floor).
			const int32 T = (Sa - Sb) / RxSim::DIFFUSION_RATE;
			Ra->Stress = Sa - T;
			Rb->Stress = Sb + T;
		}
		else if (Sb > Sa)
		{
			const int32 T = (Sb - Sa) / RxSim::DIFFUSION_RATE;
			Rb->Stress = Sb - T;
			Ra->Stress = Sa + T;
		}
	}

	// 4) stability bookkeeping: fully damped recovers; high stress is unstable
	for (FRxRegion& R : Regions)
	{
		if (R.Stress == 0)
		{
			R.bStable = true;
		}
		else if (R.Stress >= RxSim::DAMP_CANCEL)
		{
			R.bStable = false;
		}
	}

	// 5) threshold releases (region-array order; anchored regions are immune)
	TArray<FRxRelease> Releases;
	for (FRxRegion& R : Regions)
	{
		if (R.Stress >= RxSim::STRESS_THRESHOLD && R.AnchoredBy == -1)
		{
			Releases.Add(BuildWave(R.Id, R.Stress));
			R.Stress = 0;
			R.bStable = false;
		}
	}
	return Releases;
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------
FRxTerrainSnapshot FRxTerrain::Snapshot() const
{
	FRxTerrainSnapshot Snap;
	Snap.Regions = Regions;
	Snap.Edges = Edges;
	Snap.Time = Time;
	Snap.StressSchedule = StressSchedule;
	return Snap;
}

// ---------------------------------------------------------------------------
// Canonical JSON (CONTRACTS.md §0: sorted keys, no whitespace, no floats/null)
// ---------------------------------------------------------------------------
namespace
{
	// Minimal RFC-8259 string escaping for canonical JSON.
	FString CanonEscape(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() + 2);
		Out.AppendChar(TEXT('"'));
		for (const TCHAR C : In)
		{
			switch (C)
			{
			case TEXT('"'):  Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\b'): Out += TEXT("\\b");  break;
			case TEXT('\f'): Out += TEXT("\\f");  break;
			case TEXT('\n'): Out += TEXT("\\n");  break;
			case TEXT('\r'): Out += TEXT("\\r");  break;
			case TEXT('\t'): Out += TEXT("\\t");  break;
			default:
				if (C < 0x20)
				{
					Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C));
				}
				else
				{
					Out.AppendChar(C);
				}
				break;
			}
		}
		Out.AppendChar(TEXT('"'));
		return Out;
	}

	FString CanonInt(int32 V)
	{
		return FString::Printf(TEXT("%d"), V);
	}

	// [x,y] pair (poly vertex or edge id pair).
	FString CanonPair(int32 A, int32 B)
	{
		return FString::Printf(TEXT("[%d,%d]"), A, B);
	}

	FString CanonRegion(const FRxRegion& R)
	{
		// keys sorted lexicographically: anchored_by,id,kind,name,poly,stable,stress
		FString Poly = TEXT("[");
		for (int32 I = 0; I < R.Poly.Num(); ++I)
		{
			if (I > 0)
			{
				Poly += TEXT(",");
			}
			Poly += CanonPair(R.Poly[I].X, R.Poly[I].Y);
		}
		Poly += TEXT("]");

		FString Out = TEXT("{");
		Out += TEXT("\"anchored_by\":") + CanonInt(R.AnchoredBy);
		Out += TEXT(",\"id\":") + CanonInt(R.Id);
		Out += TEXT(",\"kind\":") + CanonEscape(R.Kind);
		Out += TEXT(",\"name\":") + CanonEscape(R.Name);
		Out += TEXT(",\"poly\":") + Poly;
		Out += TEXT(",\"stable\":") + FString(R.bStable ? TEXT("true") : TEXT("false"));
		Out += TEXT(",\"stress\":") + CanonInt(R.Stress);
		Out += TEXT("}");
		return Out;
	}

	FString CanonStressEvent(const FRxStressEvent& S)
	{
		// keys sorted lexicographically: rate,region,tick,until
		FString Out = TEXT("{");
		Out += TEXT("\"rate\":") + CanonInt(S.Rate);
		Out += TEXT(",\"region\":") + CanonInt(S.Region);
		Out += TEXT(",\"tick\":") + CanonInt(S.Tick);
		Out += TEXT(",\"until\":") + CanonInt(S.Until);
		Out += TEXT("}");
		return Out;
	}
}

FString FRxTerrain::ToCanonicalJson() const
{
	// top-level keys sorted lexicographically: edges,regions,stress_schedule,time
	FString Out = TEXT("{");

	// edges
	Out += TEXT("\"edges\":[");
	for (int32 I = 0; I < Edges.Num(); ++I)
	{
		if (I > 0)
		{
			Out += TEXT(",");
		}
		Out += CanonPair(Edges[I].A, Edges[I].B);
	}
	Out += TEXT("]");

	// regions
	Out += TEXT(",\"regions\":[");
	for (int32 I = 0; I < Regions.Num(); ++I)
	{
		if (I > 0)
		{
			Out += TEXT(",");
		}
		Out += CanonRegion(Regions[I]);
	}
	Out += TEXT("]");

	// stress_schedule
	Out += TEXT(",\"stress_schedule\":[");
	for (int32 I = 0; I < StressSchedule.Num(); ++I)
	{
		if (I > 0)
		{
			Out += TEXT(",");
		}
		Out += CanonStressEvent(StressSchedule[I]);
	}
	Out += TEXT("]");

	// time
	Out += TEXT(",\"time\":") + CanonInt(Time);

	Out += TEXT("}");
	return Out;
}

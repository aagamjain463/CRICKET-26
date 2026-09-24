#include "CricketStadium.h"
#include "CricketTypes.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace CricketStadium
{
void FColouredMesh::AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FLinearColor& Col, const FVector& Facing)
{
	// Every triangle is stored so that (B - A) x (C - A) points along its normal, the way it faces.
	FVector N = FVector::CrossProduct(C - A, D - B).GetSafeNormal(); // diagonals: still sound when one edge has shrunk to a point
	const bool bFlip = FVector::DotProduct(N, Facing) < 0.f;
	if (bFlip) N = -N;
	const int32 I = Pos.Num();
	for (const FVector& P : { A, B, C, D })
	{
		Pos.Add(FVector3f(P));
		Normal.Add(FVector3f(N));
		Colour.Add(Col);
	}
	if (bFlip) Tri.Append({ I, I + 2, I + 1, I, I + 3, I + 2 });
	else Tri.Append({ I, I + 1, I + 2, I, I + 2, I + 3 });
}

void FColouredMesh::AddBox(const FVector& Centre, const FVector& Out, const FVector& Half, const FLinearColor& Col, bool bEnds)
{
	const FVector O = FVector(Out.X, Out.Y, 0.f).GetSafeNormal();
	const FVector Along = FVector::CrossProduct(FVector::UpVector, O);
	const FVector X = O * Half.X, Y = Along * Half.Y, Z = FVector::UpVector * Half.Z;
	auto P = [&](float SX, float SY, float SZ) { return Centre + SX * X + SY * Y + SZ * Z; };
	AddQuad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), Col, -O);                       // front, toward the field
	AddQuad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Col * 1.08f, FVector::UpVector);    // top, lit from above
	if (!bEnds) return;
	AddQuad(P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), P(-1, -1, -1), Col * 0.85f, -Along);          // ends
	AddQuad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), Col * 0.85f, Along);
}

namespace
{
	FVector Radial(float AngleDeg) { const float A = FMath::DegreesToRadians(AngleDeg); return FVector(FMath::Cos(A), FMath::Sin(A), 0.f); }

	/** Degrees from the pitch's line (0 or 180): the sightscreen ends. */
	float FromPitchLine(float AngleDeg)
	{
		const float A = FMath::Abs(FMath::Fmod(AngleDeg + 360.f, 180.f));
		return FMath::Min(A, 180.f - A);
	}

	/** True inside one of the aisles that run up the stands at every section boundary; R is the radius in metres. */
	bool InAisle(float AngleDeg, float R)
	{
		const float Step = 360.f / NumSections, Off = FMath::Fmod(AngleDeg + 360.f, Step);
		return FMath::DegreesToRadians(FMath::Min(Off, Step - Off)) * R < AisleHalfWidth;
	}

	/** A bar from A to B, W wide and H deep: both sides and the underside, which are all the stands see of it. */
	void Beam(FColouredMesh& M, const FVector& A, const FVector& B, float W, float H, const FLinearColor& Col)
	{
		const FVector Along = (B - A).GetSafeNormal();
		const FVector Side = FVector::CrossProduct(Along, FVector::UpVector).GetSafeNormal() * (W / 2.f);
		FVector Up = FVector::CrossProduct(Side, Along).GetSafeNormal() * (H / 2.f);
		if (Up.Z < 0.f) Up = -Up;
		M.AddQuad(A + Side - Up, A + Side + Up, B + Side + Up, B + Side - Up, Col, Side);
		M.AddQuad(A - Side - Up, A - Side + Up, B - Side + Up, B - Side - Up, Col, -Side);
		M.AddQuad(A + Side - Up, A - Side - Up, B - Side - Up, B + Side - Up, Col * 0.7f, -Up);
	}

	/**
	 * A ring of quads from (R0, Z0) to (R1, Z1) round the ground, in Segments pieces, facing In toward the
	 * field and Up (either may be negative); Colour by angle.
	 */
	template <typename F>
	void Ring(FColouredMesh& M, const FVector& C, float R0, float Z0, float R1, float Z1, int32 Segments, float In, float Up, F&& Colour)
	{
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A0 = 360.f * I / Segments, A1 = 360.f * (I + 1) / Segments;
			const FVector D0 = Radial(A0), D1 = Radial(A1);
			M.AddQuad(C + D0 * R0 + FVector(0, 0, Z0), C + D0 * R1 + FVector(0, 0, Z1), C + D1 * R1 + FVector(0, 0, Z1), C + D1 * R0 + FVector(0, 0, Z0),
				Colour(0.5f * (A0 + A1)), -In * Radial(0.5f * (A0 + A1)) + FVector(0.f, 0.f, Up));
		}
	}

	struct FTier { float R0, Z0, RowDepth, Rise; int32 Rows; };
	// Lower tier from the pitch-side wall, upper tier set back above a facade.
	FTier Tiers(int32 I)
	{
		const float Front = CricketGeo::BoundaryRadius + StandRadiusOffset;
		return I == 0 ? FTier{ Front, 1.4f, 0.85f, 0.42f, 14 } : FTier{ Front + 14.5f, 10.8f, 0.9f, 0.55f, 16 };
	}
}

const FVenue& Venue(int32 Index)
{
	static const FVenue Venues[NumVenues] = {
		{ TEXT("HARBOURSIDE OVAL"), EPitchType::Flat, 0.3f, 0.15f, false }, // a true surface on a bright afternoon
		{ TEXT("GREENHILL PARK"), EPitchType::Green, 0.1f, 0.85f, false },  // grass on the pitch and cloud overhead: it seams and swings
		{ TEXT("SUNFORT STADIUM"), EPitchType::Dusty, 0.6f, 0.f, true } };  // a dry turner under lights, with the dew coming
	return Venues[FMath::Clamp(Index, 0, NumVenues - 1)];
}

FStadium Build(const FStadiumSpec& Spec)
{
	using namespace CricketGeo;
	FStadium S;
	S.Crowd.SetNum(NumSections * NumGroups);
	FRandomStream Rng(int32(Spec.Seed));
	const FVector C = PitchCentre();

	// Outfield: mown stripes across the ground, square to the pitch, alternating shade; clipped to the rope.
	{
		const float R = BoundaryRadius + 0.2f, Band = 5.f;
		for (int32 I = 0; -R + I * Band < R; ++I)
		{
			const float X0 = -R + I * Band, X1 = FMath::Min(X0 + Band, R);
			// Split each band along its length so its ends follow the circle.
			const int32 Steps = 6;
			for (int32 J = 0; J < Steps; ++J)
			{
				const float XA = FMath::Lerp(X0, X1, float(J) / Steps), XB = FMath::Lerp(X0, X1, float(J + 1) / Steps);
				const float YA = FMath::Sqrt(FMath::Max(R * R - XA * XA, 0.f)), YB = FMath::Sqrt(FMath::Max(R * R - XB * XB, 0.f));
				S.Outfield[I % 2].AddQuad(C + FVector(XA, -YA, 0.f), C + FVector(XA, YA, 0.f), C + FVector(XB, YB, 0.f), C + FVector(XB, -YB, 0.f), StripeColours[I % 2], FVector::UpVector);
			}
		}
	}

	FColouredMesh& M = S.Structure;
	// LED bands: the boards just beyond the rope, leaning back a little, and a ribbon along the upper tier's front.
	auto Band = [&](float R, float Z0, float Z1, float Lean)
	{
		const int32 First = S.Boards.Pos.Num();
		Ring(S.Boards, C, R, Z0, R + Lean, Z1, FMath::CeilToInt(2.f * PI * R / 2.f), 1.f, 0.f,
			[&](float A) { return (int32(A / 6.f) % 2 ? Spec.Home : Spec.Away) * 0.9f + FLinearColor(0.04f, 0.04f, 0.05f); });
		if (!Spec.bLedBoards) return;
		for (int32 I = First; I < S.Boards.Pos.Num(); ++I) S.Boards.Colour[I] = FLinearColor((I - First) % 4 == 1 || (I - First) % 4 == 2 ? 1.f : 0.f, 0.f, 0.f);
	};
	Band(BoundaryRadius + BoardRadiusOffset, 0.f, 0.95f, 0.15f);
	// The rope: a padded white cushion, triangular in section.
	Ring(M, C, BoundaryRadius - 0.12f, 0.f, BoundaryRadius, 0.12f, 240, 1.f, 1.f, [](float) { return FLinearColor(0.85f, 0.85f, 0.87f); });
	Ring(M, C, BoundaryRadius, 0.12f, BoundaryRadius + 0.12f, 0.f, 240, -1.f, 1.f, [](float) { return FLinearColor(0.7f, 0.7f, 0.72f); });

	// Stands: eight blocks of coloured seats, each tier a flight of steps; a pitch-side wall and a facade.
	// Each venue's own seats: navy, red, teal and grey; green, cream and slate; orange, charcoal and sand.
	static const FLinearColor Schemes[NumVenues][4] = {
		{ { 0.03f, 0.06f, 0.22f }, { 0.22f, 0.025f, 0.03f }, { 0.02f, 0.15f, 0.17f }, { 0.1f, 0.1f, 0.11f } },
		{ { 0.03f, 0.12f, 0.05f }, { 0.4f, 0.37f, 0.3f }, { 0.07f, 0.08f, 0.1f }, { 0.03f, 0.12f, 0.05f } },
		{ { 0.35f, 0.1f, 0.02f }, { 0.05f, 0.05f, 0.055f }, { 0.3f, 0.22f, 0.12f }, { 0.05f, 0.05f, 0.055f } } };
	const FLinearColor* SeatCols = Schemes[FMath::Clamp(Spec.Scheme, 0, NumVenues - 1)];
	auto SeatCol = [&](float A, int32 Row)
	{
		if (FromPitchLine(A) < SightscreenGapDeg) return FLinearColor(0.03f, 0.03f, 0.035f);
		return SeatCols[int32((A + 22.5f) / 45.f) % 4] * (Row % 2 ? 1.f : 0.85f);
	};
	const FLinearColor Concrete(0.2f, 0.2f, 0.21f), Facade(0.08f, 0.09f, 0.12f);
	for (int32 T = 0; T < 2; ++T)
	{
		const FTier Ti = Tiers(T);
		const int32 Segs = FMath::CeilToInt(2.f * PI * Ti.R0 / 3.f);
		Ring(M, C, Ti.R0, T == 0 ? 0.f : Tiers(0).Z0 + Tiers(0).Rows * Tiers(0).Rise, Ti.R0, Ti.Z0, Segs, 1.f, 0.f, [&](float) { return T == 0 ? Concrete : Facade; });
		if (T == 1) Band(Ti.R0 - 0.05f, Ti.Z0 - 1.6f, Ti.Z0 - 0.3f, 0.f);
		for (int32 Row = 0; Row < Ti.Rows; ++Row)
		{
			const float R = Ti.R0 + Row * Ti.RowDepth, Z = Ti.Z0 + Row * Ti.Rise;
			Ring(M, C, R, Z, R, Z + Ti.Rise, Segs, 1.f, 0.f, [&](float A) { return SeatCol(A, Row) * 0.7f; });                 // riser
			Ring(M, C, R, Z + Ti.Rise, R + Ti.RowDepth, Z + Ti.Rise, Segs, 0.f, 1.f, [&](float A) { return SeatCol(A, Row); }); // tread
			// The row of seat backs at the back of each tread, and a concrete step up every aisle.
			Ring(M, C, R + Ti.RowDepth - 0.18f, Z + Ti.Rise, R + Ti.RowDepth - 0.12f, Z + Ti.Rise + 0.42f, Segs, 1.f, 0.2f, [&](float A) { return SeatCol(A, Row) * 0.9f; });
			for (int32 K = 0; K < NumSections; ++K)
			{
				const FVector D = Radial(360.f * K / NumSections);
				M.AddBox(C + D * (R + Ti.RowDepth / 2.f - 0.02f) + FVector(0, 0, Z + Ti.Rise / 2.f + 0.02f), D, FVector(Ti.RowDepth / 2.f, AisleHalfWidth, Ti.Rise / 2.f),
					Row % 2 ? Concrete : Concrete * 1.12f, false);
			}
		}
		// A railing along the tier's front edge: a top rail on posts.
		const float RailR = Ti.R0 - 0.06f, RailZ = Ti.Z0 + 1.f;
		Ring(M, C, RailR, RailZ - 0.06f, RailR, RailZ, Segs, 1.f, 0.f, [](float) { return FLinearColor(0.5f, 0.5f, 0.52f); });
		Ring(M, C, RailR, RailZ - 0.45f, RailR, RailZ - 0.42f, Segs, 1.f, 0.f, [](float) { return FLinearColor(0.4f, 0.4f, 0.42f); });
		for (int32 I = 0; I < Segs; ++I)
		{
			const FVector D = Radial(360.f * I / Segs);
			M.AddBox(C + D * RailR + FVector(0, 0, (Ti.Z0 + RailZ) / 2.f), D, FVector(0.03f, 0.03f, (RailZ - Ti.Z0) / 2.f), FLinearColor(0.45f, 0.45f, 0.47f), false);
		}
	}
	// Back wall and roof: a canopy over the upper tier, dark underneath so the stands read as shade.
	const FTier Up = Tiers(1);
	const float Back = Up.R0 + Up.Rows * Up.RowDepth, Top = Up.Z0 + Up.Rows * Up.Rise;
	Ring(M, C, Back, Top, Back, Top + 6.f, 160, 1.f, 0.f, [&](float) { return Facade; });
	Ring(M, C, Back, Top + 6.f, Up.R0 + 2.f, Top + 7.5f, 160, 0.3f, -1.f, [](float) { return FLinearColor(0.05f, 0.05f, 0.06f); });
	Ring(M, C, Up.R0 + 2.f, Top + 7.5f, Up.R0 + 2.f, Top + 6.8f, 160, 1.f, 0.f, [](float) { return FLinearColor(0.6f, 0.6f, 0.62f); }); // roof edge
	// Steel trusses under the canopy, light against its shade: a bottom chord below the roof and diagonals up to it.
	const FLinearColor Steel(0.42f, 0.43f, 0.45f);
	for (int32 K = 0; K < 64; ++K)
	{
		const FVector D = Radial(360.f * (K + 0.5f) / 64.f);
		auto Roof = [&](float R) { return C + D * R + FVector(0, 0, Top + 6.f + 1.5f * (Back - R) / (Back - Up.R0 - 2.f) - 0.1f); };
		const int32 Bays = 6;
		Beam(M, Roof(Back) - FVector(0, 0, 1.2f), Roof(Up.R0 + 2.f) - FVector(0, 0, 0.2f), 0.25f, 0.3f, Steel);
		for (int32 B = 0; B < Bays; ++B)
		{
			const float R0 = FMath::Lerp(Back, Up.R0 + 2.f, float(B) / Bays), R1 = FMath::Lerp(Back, Up.R0 + 2.f, float(B + 1) / Bays);
			const float Drop0 = FMath::Lerp(1.2f, 0.2f, float(B) / Bays);
			Beam(M, Roof(R0) - FVector(0, 0, Drop0), Roof(R1), 0.12f, 0.12f, Steel * 0.9f);
		}
	}
	// Two big screens up on the roof at opposite corners, square to the middle, facing the broadcast ends' cameras.
	for (const float A : { 145.f, 325.f })
	{
		const FVector D = Radial(A);
		FScreen Sc{ C + D * (Back - 1.f) + FVector(0, 0, Top + 15.f), -D, 18.f, 9.f };
		const FVector Side = Radial(A + 90.f);
		M.AddBox(Sc.Centre + D * 0.5f, D, FVector(0.45f, Sc.Width / 2.f + 0.5f, Sc.Height / 2.f + 0.5f), FLinearColor(0.06f, 0.06f, 0.07f));
		M.AddQuad(Sc.Centre - Side * (Sc.Width / 2.f) - FVector(0, 0, Sc.Height / 2.f), Sc.Centre - Side * (Sc.Width / 2.f) + FVector(0, 0, Sc.Height / 2.f),
			Sc.Centre + Side * (Sc.Width / 2.f) + FVector(0, 0, Sc.Height / 2.f), Sc.Centre + Side * (Sc.Width / 2.f) - FVector(0, 0, Sc.Height / 2.f), FLinearColor(0.01f, 0.012f, 0.02f), Sc.Facing);
		for (const float Leg : { -0.3f, 0.3f })
			M.AddBox(C + D * (Back - 0.5f) + Side * (Leg * Sc.Width) + FVector(0, 0, (Top + 6.f + Sc.Centre.Z - Sc.Height / 2.f) / 2.f), D,
				FVector(0.3f, 0.3f, (Sc.Centre.Z - Sc.Height / 2.f - Top - 6.f) / 2.f), FLinearColor(0.2f, 0.2f, 0.22f), false);
		S.Screens.Add(Sc);
	}
	// Floodlight towers behind the four corners, their lamp banks tilted toward the square.
	for (const float A : { 45.f, 135.f, 225.f, 315.f })
	{
		const FVector D = Radial(A);
		const FVector Base = C + D * (Back + 4.f);
		M.AddBox(Base + FVector(0, 0, 24.f), D, FVector(0.8f, 0.8f, 24.f), FLinearColor(0.35f, 0.35f, 0.37f));
		M.AddBox(Base + FVector(0, 0, 50.f) - D * 1.5f, D, FVector(0.8f, 5.f, 3.2f), FLinearColor(0.2f, 0.2f, 0.22f));
		// A grid of lamps on the bank.
		const FVector Side = Radial(A + 90.f);
		S.Floodlights.Add(Base + FVector(0, 0, 50.f) - D * 2.6f);
		for (int32 I = 0; I < 5; ++I)
			for (int32 J = 0; J < 4; ++J)
				S.Lamps.AddBox(Base + FVector(0, 0, 50.f + (J - 1.5f) * 1.5f) - D * 2.4f + Side * ((I - 2) * 1.9f), D, FVector(0.15f, 0.8f, 0.6f), FLinearColor(1.f, 0.98f, 0.9f), false);
	}

	// Crowd: seated along every row, shirts in the home colour, the away colour or anything else.
	const FLinearColor Neutral[] = { { 0.8f, 0.8f, 0.78f }, { 0.05f, 0.05f, 0.06f }, { 0.4f, 0.4f, 0.42f }, { 0.75f, 0.6f, 0.08f },
		{ 0.5f, 0.12f, 0.1f }, { 0.25f, 0.45f, 0.7f }, { 0.12f, 0.35f, 0.15f } };
	const FLinearColor Skin[] = { { 0.55f, 0.36f, 0.25f }, { 0.35f, 0.21f, 0.13f }, { 0.2f, 0.12f, 0.08f }, { 0.65f, 0.47f, 0.36f } };
	const float Pitch = 0.55f; // seat width
	for (int32 T = 0; T < 2; ++T)
	{
		const FTier Ti = Tiers(T);
		for (int32 Row = 0; Row < Ti.Rows; ++Row)
		{
			const float R = Ti.R0 + Row * Ti.RowDepth + 0.45f, Z = Ti.Z0 + (Row + 1) * Ti.Rise;
			const int32 Seats = FMath::FloorToInt(2.f * PI * R / Pitch);
			for (int32 Seat = 0; Seat < Seats; ++Seat)
			{
				const float A = 360.f * (Seat + 0.5f) / Seats;
				const float Take = Rng.FRand(), Pick = Rng.FRand();
				const int32 SkinI = Rng.RandRange(0, 3), NeutralI = Rng.RandRange(0, 6);
				const float Tone = Rng.FRandRange(0.8f, 1.15f), Size = Rng.FRandRange(0.92f, 1.08f);
				const float SkinTone = Rng.FRand(), Phase = Rng.FRand();
				if (FromPitchLine(A) < SightscreenGapDeg || Take > Spec.CrowdDensity || InAisle(A, R)) continue;
				const FLinearColor Shirt = (Pick < Spec.HomeShare ? Spec.Home : Pick < Spec.HomeShare + Spec.AwayShare ? Spec.Away : Neutral[NeutralI]) * Tone;
				const FVector D = Radial(A);
				++S.Spectators;
				if (Spec.bFanCrowd)
				{
					// Sat towards the back of the row, feet forward on the step.
					S.Fans.Add({ C + D * (R + 0.1f) + FVector(0, 0, Z), A + 180.f, Shirt, SkinTone, Phase, Size });
					continue;
				}
				const int32 Section = FMath::Min(int32(A / 360.f * NumSections), NumSections - 1);
				FColouredMesh& Out = S.Crowd[Section * NumGroups + Seat % NumGroups];
				const FVector At = C + D * R + FVector(0, 0, Z);
				Out.AddBox(At + FVector(0, 0, 0.3f * Size), D, FVector(0.14f, 0.21f, 0.3f) * Size, Shirt);
				Out.AddBox(At + FVector(0, 0, 0.72f * Size), D, FVector(0.1f, 0.09f, 0.11f) * Size, Skin[SkinI], false);
			}
		}
	}
	return S;
}

float JumpHeight(float T, float Excitement, int32 Section, int32 Group)
{
	if (Excitement <= 0.f) return 0.f;
	// Each group bounces at its own rate and phase so neighbours never move in step.
	FRandomStream R(Section * NumGroups + Group + 1);
	const float Phase = R.FRand(), Rate = R.FRandRange(1.6f, 2.6f);
	const float S = FMath::Sin(2.f * PI * (Rate * T + Phase));
	return Excitement * (0.12f + 0.3f * FMath::Max(S, 0.f) * FMath::Max(S, 0.f));
}

UStaticMesh* ToStaticMesh(UObject* Outer, const FColouredMesh& Mesh, UMaterialInterface* Material)
{
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Pos = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector4f> Col = Attr.GetVertexInstanceColors();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	Attr.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Main");
	MD.ReserveNewVertices(Mesh.Pos.Num());
	MD.ReserveNewVertexInstances(Mesh.Pos.Num());
	MD.ReserveNewTriangles(Mesh.NumTriangles());
	TArray<FVertexInstanceID> Inst;
	Inst.Reserve(Mesh.Pos.Num());
	for (int32 I = 0; I < Mesh.Pos.Num(); ++I)
	{
		const FVertexID V = MD.CreateVertex();
		Pos[V] = Mesh.Pos[I] * 100.f;
		const FVertexInstanceID VI = MD.CreateVertexInstance(V);
		Nrm[VI] = Mesh.Normal[I];
		Col[VI] = FVector4f(Mesh.Colour[I].R, Mesh.Colour[I].G, Mesh.Colour[I].B, 1.f);
		Inst.Add(VI);
	}
	for (int32 I = 0; I + 2 < Mesh.Tri.Num(); I += 3)
	{
		// Unreal's front faces wind the other way round from the (B - A) x (C - A) order stored above.
		MD.CreateTriangle(Group, { Inst[Mesh.Tri[I]], Inst[Mesh.Tri[I + 2]], Inst[Mesh.Tri[I + 1]] });
	}
	UStaticMesh* SM = NewObject<UStaticMesh>(Outer);
	SM->GetStaticMaterials().Add(FStaticMaterial(Material, TEXT("Main")));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	SM->BuildFromMeshDescriptions({ &MD }, Params);
	return SM;
}
}

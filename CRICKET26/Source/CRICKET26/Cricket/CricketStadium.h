// The ground around the playing area, generated rather than authored: a two-tier bowl of seating with a
// roof and four floodlight towers, advertising-free boundary boards, a mown outfield and a seated crowd in
// the two teams' colours. Everything is flat-shaded boxes and quads in vertex colours, merged into a few
// meshes so the whole stadium costs a handful of draw calls; the crowd is split into sections that can
// jump independently when the ground erupts. Geometry is in the simulation frame (metres, pitch centre at
// PitchCentre()).

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;

namespace CricketStadium
{
	/** Triangles with per-vertex normals and colours (linear), ready to become a static mesh. */
	struct FColouredMesh
	{
		TArray<FVector3f> Pos, Normal;
		TArray<FLinearColor> Colour;
		TArray<int32> Tri;

		/** A flat quad from its four corners in order round the edge, seen from the side Facing points to. */
		void AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FLinearColor& Col, const FVector& Facing);
		/**
		 * A box turned to face the pitch: Out is the horizontal direction away from the field, Half the half
		 * extents along (Out, along the row, up). Only the faces the field can see are built: the front,
		 * the top and (optionally) the two ends.
		 */
		void AddBox(const FVector& Centre, const FVector& Out, const FVector& Half, const FLinearColor& Col, bool bEnds = true);
		int32 NumTriangles() const { return Tri.Num() / 3; }
	};

	struct FStadiumSpec
	{
		float Seed = 26.f;
		float CrowdDensity = 0.7f; // share of seats taken (the mobile tiers thin the crowd)
		FLinearColor Home = FLinearColor::Blue, Away = FLinearColor::Red;
		float HomeShare = 0.45f, AwayShare = 0.25f;
	};

	/** Crowd sections by angle round the ground, and interleaved groups within each (for staggered jumps). */
	constexpr int32 NumSections = 24, NumGroups = 3;
	/** Seats are left empty (and the stand darkened) within this angle of the pitch's line, behind the sightscreens. */
	constexpr float SightscreenGapDeg = 9.f;
	constexpr float BoardRadiusOffset = 3.f;   // boundary boards, beyond the rope
	constexpr float StandRadiusOffset = 9.f;   // the front of the lower tier, beyond the rope
	/** The two shades of the mown outfield. */
	const FLinearColor StripeColours[2] = { { 0.1f, 0.3f, 0.075f }, { 0.075f, 0.24f, 0.06f } };

	struct FStadium
	{
		FColouredMesh Structure;   // stands, roof, towers, boards
		FColouredMesh Outfield[2]; // mown stripes inside the rope, light and dark (drawn in the grass material)
		TArray<FColouredMesh> Crowd; // [Section * NumGroups + Group]
		int32 Spectators = 0;
	};

	FStadium Build(const FStadiumSpec& Spec);

	/** Crowd jump height (m) for a group at time T, with Excitement 0 (seated) to 1 (the ground erupting). */
	float JumpHeight(float T, float Excitement, int32 Section, int32 Group);

	/** Turns a coloured mesh (metres) into a static mesh (centimetres) drawn with Material. */
	UStaticMesh* ToStaticMesh(UObject* Outer, const FColouredMesh& Mesh, UMaterialInterface* Material);
}

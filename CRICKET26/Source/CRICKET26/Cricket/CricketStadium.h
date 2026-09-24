// The ground around the playing area, generated rather than authored: a two-tier bowl of seating with a
// roof and four floodlight towers, LED boards round the rope and on the upper tier, a padded rope, a mown
// outfield, two dugouts, a media box and a seated crowd in the two teams' colours, some of them waving flags. Everything is flat-shaded boxes and quads in vertex colours, merged into a few
// meshes so the whole stadium costs a handful of draw calls; the crowd is split into sections that can
// jump independently when the ground erupts. Geometry is in the simulation frame (metres, pitch centre at
// PitchCentre()).

#pragma once

#include "CoreMinimal.h"
#include "BallSimulation.h"

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
		/** The LED boards carry their height within the band (0 at the bottom, 1 at the top) in vertex red, for the
		 *  LED material to place its ads; otherwise they are plain panels in the teams' colours. */
		bool bLedBoards = false;
		/** The crowd as Fans, for instanced spectators (Scripts/stadium/make_fan.py); the block meshes stay empty. */
		bool bFanCrowd = false;
		/** The seats' colours, one scheme per venue. */
		int32 Scheme = 0;
	};

	/** A ground (all fictional): the pitch it prepares, how used that pitch is by the time of the Super Over, and
	 *  its weather. Under lights the outfield takes dew. */
	struct FVenue
	{
		const TCHAR* Name;
		EPitchType Pitch;
		float Wear;   // 0 fresh to 1 at the end of a long match
		float Cloud;  // 0 clear to 1 overcast
		bool bNight;
	};
	constexpr int32 NumVenues = 3;
	const FVenue& Venue(int32 Index);

	/** One spectator of the instanced crowd: the floor under their seat (metres), the way they face, and their look. */
	struct FFan
	{
		FVector Pos;
		float Yaw = 0.f;      // degrees
		FLinearColor Shirt;
		float Skin = 0.f;     // 0 darkest to 1 lightest
		float Phase = 0.f;    // 0 to 1: their rhythm, cheering pose, hair and trousers
		float Scale = 1.f;
	};

	/** Crowd sections by angle round the ground, and interleaved groups within each (for staggered jumps). */
	constexpr int32 NumSections = 24, NumGroups = 3;
	/** Seats are left empty (and the stand darkened) within this angle of the pitch's line, behind the sightscreens. */
	constexpr float SightscreenGapDeg = 9.f;
	constexpr float BoardRadiusOffset = 3.f;   // boundary boards, beyond the rope
	constexpr float StandRadiusOffset = 9.f;   // the front of the lower tier, beyond the rope
	constexpr float AisleHalfWidth = 0.6f;     // the aisles up the stands, one at every section boundary
	/** The two shades of the mown outfield. */
	const FLinearColor StripeColours[2] = { { 0.1f, 0.3f, 0.075f }, { 0.075f, 0.24f, 0.06f } };

	/** A big screen on the roof: the middle of its face (metres), the way the face looks, and its size. */
	struct FScreen
	{
		FVector Centre, Facing;
		float Width = 0.f, Height = 0.f;
	};

	struct FStadium
	{
		FColouredMesh Structure;   // stands, roof, towers, rope cushion
		FColouredMesh Boards;      // LED boards beyond the rope and the ribbon on the upper tier's front
		FColouredMesh Outfield[2]; // mown stripes inside the rope, light and dark (drawn in the grass material)
		TArray<FColouredMesh> Crowd; // [Section * NumGroups + Group]
		TArray<FFan> Fans;           // with bFanCrowd
		TArray<FFan> Flags;          // with bFanCrowd: flags in the team colour (Shirt), where a seated fan holds them up
		TArray<FScreen> Screens;     // their frames are in Structure; the game draws what they show
		FColouredMesh Lamps;         // the floodlights' lamps, which glow at night
		TArray<FVector> Floodlights; // the middle of each tower's lamp bank, where its light comes from
		int32 Spectators = 0;
	};

	FStadium Build(const FStadiumSpec& Spec);

	/** One flag on its pole, the pole's foot at the origin and the cloth flying along +Y, facing +X. Vertex red runs
	 *  from 0 at the pole to 1 at the fly end, green is 1 on the cloth, blue runs down it (for the flag material). */
	FColouredMesh Flag();

	/** Crowd jump height (m) for a group at time T, with Excitement 0 (seated) to 1 (the ground erupting). */
	float JumpHeight(float T, float Excitement, int32 Section, int32 Group);

	/** Turns a coloured mesh (metres) into a static mesh (centimetres) drawn with Material. */
	UStaticMesh* ToStaticMesh(UObject* Outer, const FColouredMesh& Mesh, UMaterialInterface* Material);
}

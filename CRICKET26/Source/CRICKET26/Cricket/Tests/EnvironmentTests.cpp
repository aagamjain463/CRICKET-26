// Environment automation tests: the generated stadium and crowd.

#include "Misc/AutomationTest.h"
#include "CricketStadium.h"
#include "CricketTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnvStadium, "CRICKET26.Environment.Stadium",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FEnvStadium::RunTest(const FString&)
{
	using namespace CricketStadium;
	const FStadiumSpec Spec;
	const FStadium S = Build(Spec);
	const FVector C = CricketGeo::PitchCentre();

	TestTrue(*FString::Printf(TEXT("a full house (%d)"), S.Spectators), S.Spectators > 15000);
	int32 Tris = S.Structure.NumTriangles() + S.Boards.NumTriangles() + S.Lamps.NumTriangles();
	for (const FColouredMesh& M : S.Crowd) Tris += M.NumTriangles();
	TestTrue(*FString::Printf(TEXT("within the mobile triangle budget (%d)"), Tris), Tris < 300000);
	TestEqual(TEXT("one crowd mesh per section and group"), S.Crowd.Num(), NumSections * NumGroups);
	TestEqual(TEXT("the same ground every time"), Build(Spec).Spectators, S.Spectators);
	FStadiumSpec Thin = Spec;
	Thin.CrowdDensity = 0.35f; // the Low tier
	const int32 ThinCount = Build(Thin).Spectators;
	TestTrue(*FString::Printf(TEXT("the Low tier halves the crowd (%d of %d)"), ThinCount, S.Spectators), ThinCount > S.Spectators * 0.4f && ThinCount < S.Spectators * 0.6f);

	// Every spectator sits in the stands, and nobody sits behind the sightscreens.
	float Nearest = 1e9f, NearestLine = 1e9f;
	for (const FColouredMesh& M : S.Crowd)
	{
		for (const FVector3f& P : M.Pos)
		{
			const FVector D = FVector(P) - C;
			Nearest = FMath::Min(Nearest, FVector2D(D).Size());
			const float A = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(D.Y), FMath::Abs(D.X)));
			NearestLine = FMath::Min(NearestLine, A);
		}
	}
	TestTrue(*FString::Printf(TEXT("crowd beyond the boards (%.1f m)"), Nearest), Nearest > CricketGeo::BoundaryRadius + StandRadiusOffset - 0.5f);
	TestTrue(*FString::Printf(TEXT("sightscreen gap clear (%.1f deg)"), NearestLine), NearestLine > SightscreenGapDeg - 1.f);

	// Every triangle's winding agrees with its stored normal, so nothing is culled from the field's side.
	int32 Bad = 0;
	auto Check = [&](const FColouredMesh& M)
	{
		for (int32 I = 0; I < M.Tri.Num(); I += 3)
		{
			const FVector3f A = M.Pos[M.Tri[I]], B = M.Pos[M.Tri[I + 1]], D = M.Pos[M.Tri[I + 2]];
			const FVector3f N = FVector3f::CrossProduct(B - A, D - A);
			if (N.SizeSquared() > 1e-8f && FVector3f::DotProduct(N, M.Normal[M.Tri[I]]) <= 0.f) ++Bad; // slivers at the rope have no area
		}
	};
	Check(S.Structure);
	Check(S.Outfield[0]);
	Check(S.Boards);
	Check(S.Lamps);
	for (const FColouredMesh& M : S.Crowd) Check(M);
	TestEqual(TEXT("windings agree with normals"), Bad, 0);

	// With LED boards, vertex red runs from 0 along each band's foot to 1 along its top, where the material reads it
	// as the height within the printed ad.
	FStadiumSpec Led = Spec;
	Led.bLedBoards = true;
	const FStadium L = Build(Led);
	int32 Misread = 0;
	for (int32 I = 0; I < L.Boards.Pos.Num(); I += 4)
	{
		Misread += L.Boards.Colour[I].R != 0.f || L.Boards.Colour[I + 3].R != 0.f || L.Boards.Colour[I + 1].R != 1.f || L.Boards.Colour[I + 2].R != 1.f;
		Misread += L.Boards.Pos[I + 1].Z <= L.Boards.Pos[I].Z;
	}
	TestTrue(TEXT("boards built"), L.Boards.Pos.Num() > 0 && L.Boards.Pos.Num() % 4 == 0);
	TestEqual(TEXT("LED boards read top to bottom"), Misread, 0);

	// With the instanced crowd, the same seats are filled by fans instead of blocks, each facing the middle.
	FStadiumSpec Instanced = Spec;
	Instanced.bFanCrowd = true;
	const FStadium F = Build(Instanced);
	TestEqual(TEXT("one fan per spectator"), F.Fans.Num(), S.Spectators);
	int32 Blocks = 0, Astray = 0;
	for (const FColouredMesh& M : F.Crowd) Blocks += M.Tri.Num();
	for (const FFan& Fan : F.Fans)
	{
		const FVector ToMiddle = FVector(C - Fan.Pos).GetSafeNormal2D();
		Astray += FVector::DotProduct(FRotator(0.f, Fan.Yaw, 0.f).Vector(), ToMiddle) < 0.99f;
	}
	TestEqual(TEXT("no block crowd alongside the fans"), Blocks, 0);
	TestEqual(TEXT("fans face the middle"), Astray, 0);
	// A few of them, all in a team's colours, hold up flags, and the flag mesh faces the field.
	TestTrue(*FString::Printf(TEXT("a flag every so often (%d)"), F.Flags.Num()), F.Flags.Num() > F.Fans.Num() / 100 && F.Flags.Num() < F.Fans.Num() / 10);
	int32 OffTeam = 0;
	for (const FFan& Fl : F.Flags) OffTeam += Fl.Shirt != Spec.Home && Fl.Shirt != Spec.Away;
	TestEqual(TEXT("flags in the teams' colours"), OffTeam, 0);
	Bad = 0;
	Check(Flag());
	TestEqual(TEXT("flag windings agree with normals"), Bad, 0);

	// The big screens stand above the roof, facing the middle.
	TestEqual(TEXT("two big screens"), S.Screens.Num(), 2);
	for (const FScreen& Sc : S.Screens)
	{
		TestTrue(TEXT("screen faces the middle"), FVector::DotProduct(Sc.Facing, FVector(C - Sc.Centre).GetSafeNormal2D()) > 0.99f);
		TestTrue(TEXT("screen high above the stands"), Sc.Centre.Z - Sc.Height / 2.f > 25.f);
	}

	// Four floodlight towers, their lamps high above the roof.
	TestEqual(TEXT("four floodlights"), S.Floodlights.Num(), 4);
	for (const FVector& Lamp : S.Floodlights) TestTrue(TEXT("floodlight high above the stands"), Lamp.Z > 40.f);
	TestTrue(TEXT("lamps built"), S.Lamps.NumTriangles() > 0);

	// Every venue plays differently: its own pitch, and its own seats.
	TSet<EPitchType> Pitches;
	for (int32 V = 0; V < NumVenues; ++V) Pitches.Add(Venue(V).Pitch);
	TestEqual(TEXT("each venue prepares its own kind of pitch"), Pitches.Num(), NumVenues);
	FStadiumSpec Other = Spec;
	Other.Scheme = 1;
	TestTrue(TEXT("each venue has its own seats"), Build(Other).Structure.Colour != S.Structure.Colour);

	// The crowd sits still until the ground erupts, then jumps, and neighbours are out of step.
	TestEqual(TEXT("seated when quiet"), JumpHeight(3.f, 0.f, 4, 1), 0.f);
	float Max = 0.f, Apart = 0.f;
	for (float T = 0.f; T < 3.f; T += 0.02f)
	{
		Max = FMath::Max(Max, JumpHeight(T, 1.f, 4, 1));
		Apart = FMath::Max(Apart, FMath::Abs(JumpHeight(T, 1.f, 4, 1) - JumpHeight(T, 1.f, 4, 2)));
	}
	TestTrue(TEXT("jumps a believable height"), Max > 0.25f && Max < 0.5f);
	TestTrue(TEXT("groups out of step"), Apart > 0.1f);
	return true;
}

#endif

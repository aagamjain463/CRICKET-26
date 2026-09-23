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
	int32 Tris = S.Structure.NumTriangles();
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
	for (const FColouredMesh& M : S.Crowd) Check(M);
	TestEqual(TEXT("windings agree with normals"), Bad, 0);

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

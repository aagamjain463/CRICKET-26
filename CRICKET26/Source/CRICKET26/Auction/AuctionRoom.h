// The 3D auction hall, after the IPL 2025 mega auction room and Real Cricket's RCPL auction: a dark hall with a
// raised stage, the auctioneer at her lectern beside the trophy, a huge LED wall showing the player under the hammer
// under LED arches that turn the buyer's colour when the hammer falls, side screens with the purse table, and the ten
// franchise tables in two banks beside a centre aisle, each with seated front-office staff (MetaHumans, posed by IK), laptops,
// bottles, a name tent, a standee and the bidding paddle they raise.
//
// Everything but the people is generated: flat-shaded vertex-coloured boxes merged into a few meshes, like the
// stadium (CricketStadium::FColouredMesh). Geometry is in metres: the stage front's centre is the origin, the hall
// runs along +X from the stage to the back wall, and the auctioneer faces +X. The franchises sit in index order
// on her left (-Y) and right (+Y), which is what AuctionCalls::Where says out loud.
//
// The camera is a small broadcast director: a wide of the hall, the LED wall on each new lot, the auctioneer for the
// calls, a close-up on the table that bids, and the buyer's table when the hammer falls.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AuctionTypes.h"
#include "AuctionRoom.generated.h"

class FAuction;
struct FAuctionEventRecord;
class ACameraActor;
class UCricketAnimInstance;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class USkeletalMeshComponent;

namespace AuctionRoomLayout
{
	constexpr float StageHeight = 0.5f;   // m
	constexpr float TableTop = 0.76f, TableHalfLength = 1.4f, TableHalfDepth = 0.45f;
	constexpr float RearRowHeight = 0.45f; // raised second rank behind each bank's front three tables
	// Jeddah had three to six at a table; phones seat two, as every seated MetaHuman costs a full character.
	constexpr int32 SeatsPerTable = (PLATFORM_ANDROID || PLATFORM_IOS) ? 2 : 3;

	/** The middle of a franchise's table front edge, at its row's floor height (m), and the way it faces (toward the lectern). */
	FVector TableFront(int32 Team);
	FVector TableFacing(int32 Team);
	/** The floor under a staff member's pelvis, Seat 0 on the table's right as its staff see it (the paddle), seat 1
	 *  on its left and seat 2 between them, so the table's middle stays halfway from seat 0 to seat 1. */
	FVector SeatAt(int32 Team, int32 Seat);
	/** Which MetaHuman sits where, given how many there are: every table has different people. */
	int32 StaffFor(int32 Team, int32 Seat, int32 NumStaff);
}

UCLASS()
class AAuctionRoom : public AActor
{
	GENERATED_BODY()

public:
	AAuctionRoom();
	virtual void BeginPlay() override;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera = nullptr;

	void Present(const FAuction& Auction, const FAuctionEventRecord& E);
	void Update(const FAuction* Auction, float Dt, int32 HumanTeam);
	/** Clears the director and table gestures (restart): the next lot opens on the wide shot. */
	void ResetDirector();

	/** The broadcast split screen of a bidding duel: the two tables, in table order, each framed by its own capture
	 *  into SplitTargets; INDEX_NONE while the director is on one camera (and always on mobile, which has no captures). */
	int32 Split[2] = { INDEX_NONE, INDEX_NONE };
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> SplitTargets[2];
	bool IsSplitShot() const { return bShowSplit && Split[0] != INDEX_NONE; }

	/** Sets up a spawned MetaHuman's meshes for the room and returns its body, null if it has none. */
	static USkeletalMeshComponent* PrepareParts(AActor* Person);

private:
	struct FPerson
	{
		TObjectPtr<AActor> Actor;
		TObjectPtr<USkeletalMeshComponent> Body;
		int32 Team = INDEX_NONE, Seat = 0; // INDEX_NONE: the auctioneer
		FVector Floor;                     // under the pelvis, m
		FVector Facing;
	};
	TArray<FPerson> People;
	UPROPERTY() TArray<TObjectPtr<AActor>> Spawned;          // keeps the people and props alive
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Paddles; // per franchise, held by its seat 0
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Gavel;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> WallTarget;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> SideTarget;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> AccentTarget;
	UPROPERTY() TArray<TObjectPtr<UTextureRenderTarget2D>> SkirtTargets;
	UPROPERTY() TObjectPtr<class URectLightComponent> WallLight;
	UPROPERTY() TObjectPtr<class USceneCaptureComponent2D> SplitCaptures[2];
	int32 LastBidder = INDEX_NONE;

	// What each table and the auctioneer are doing, in world seconds.
	struct FTableAct { double RaiseAt = -100.0, HuddleAt = -100.0, CheerAt = -100.0, OutAt = -100.0; };
	FTableAct Acts[10];
	double PointAt = -100.0, GavelAt = -100.0;
	int32 PointTeam = INDEX_NONE, LookTeam = INDEX_NONE;

	// Accent colour of the arches and wall light: teal and gold at rest, the buyer's colour after a sale.
	FLinearColor Accent, AccentGoal;
	double AccentUntil = -100.0;
	// The screens redraw on a timer as well as on events, so purses and bids never lag the auction.
	double ScreensAt = 0.0;
	int32 SkirtDraws = 0;

	// The camera director.
	struct FShot { FVector From, To, Drift; float Fov = 40.f; };
	FShot Shot;
	double ShotAt = -100.0;
	double SaleWideAt = -1.0;
	int32 ShotBeat = 0;
	bool bShowSplit = false;
	void Cut(const FShot& S, bool bForce = true);
	void NextBiddingShot();
	FShot Wide() const;
	FShot OnAuctioneer() const;
	FShot OnWall() const;
	FShot OnTable(int32 Team) const;

	void BuildHall();
	void SpawnPeople();
	void DrawWall(const FAuction* A, const FAuctionEventRecord* E);
	void DrawSideScreens(const FAuction* A);
	void DrawSkirt(int32 Team);
	void PoseStaff(FPerson& P, double T);
	void PoseAuctioneer(FPerson& P, double T);
	double Now() const;
};

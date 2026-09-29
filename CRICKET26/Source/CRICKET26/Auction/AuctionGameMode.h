// The IPL mega auction as a playable mode: pick a franchise, make your retentions, then bid live against nine AI
// front offices in the 3D auction room, with the broadcast HUD painted over it. Entered on the Entry map with
// ?game=/Script/CRICKET26.AuctionGameMode (see UFrontendStatics::OpenAuction).
//
// Dev switches: -AuctionTeam=CSK skips the team pick, -AuctionAuto lets the AI run your retentions and passes on every
// lot, -AuctionSpeed=N runs the clock N times faster, -AuctionShotEvery=S saves a screenshot (HUD included) every S
// seconds into Saved/Screenshots, -AuctionQuitAfter=S quits.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AuctionEngine.h"
#include "AuctionGameMode.generated.h"

class AAuctionRoom;

UCLASS()
class AAuctionGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAuctionGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float Dt) override;

	enum class EScreen : uint8 { PickTeam, Retain, Live, Results };
	EScreen Screen = EScreen::PickTeam;

	/** The auction; built when the franchise is picked (the human side is fixed at construction). */
	TUniquePtr<FAuction> Auction;
	int32 Team = 0;          // the human's franchise (highlighted on the pick screen before it is chosen)
	TArray<int32> Keep;      // retention list being built, in slab order
	float Speed = 1.f;

	// Presentation state the HUD and room read.
	FString Caption;         // the auctioneer's current line
	double CaptionAt = -100.0;
	double VoiceUntil = -100.0; // the auctioneer finishes this line before the next one starts
	int32 Seen = 0;          // events already presented
	double SoldAt = -100.0;  // when the last hammer fell, for the SOLD stamp
	int32 RaiseTo = 0;       // the human's final raise being dialled in (RTM)
	bool bPaused = false;    // the auction clock is held; resume, restart or exit from the HUD
	bool bCurrentSetOnly = false; // the player list shows only the set under the hammer

	enum class EPanel : uint8 { None, Purse, Players, Squad };
	EPanel Panel = EPanel::None;
	int32 PanelTeam = 0;     // whose squad the squad panel shows

	bool bNoRetentions = false;
	int32 PendingPickTeam = INDEX_NONE; // franchise tapped on pick screen awaiting format choice

	void PickTeam(int32 Index);
	void PickTeamAndFormat(int32 Index, bool bNoRetentionsChoice);
	void StartWithNoRetentions();
	void StartWithRetentions();
	void ToggleKeep(int32 Player);
	void SuggestKeep();
	void ConfirmRetentions();
	void Bid();
	void SkipCurrentLot();
	void TogglePause();
	void RestartAuction();
	void ExitToMenu();
	/** Auction -> tournament handoff: persists the final squads as an IPL season and opens its hub. */
	void StartSeason();

	const AAuctionRoom* GetRoom() const { return Room; }

	/** Real seconds since the page opened, for HUD animation. */
	double Now() const;

private:
	UPROPERTY()
	TObjectPtr<AAuctionRoom> Room;
	bool bViewSet = false;
	bool bAuto = false;
	// The auctioneer's voice: one line at a time, each played to the end before the next starts.
	UPROPERTY() TObjectPtr<class USoundWaveProcedural> VoiceWave;
	UPROPERTY() TObjectPtr<class UAudioComponent> VoiceAudio;

	void Present(const FAuctionEventRecord& E);
};

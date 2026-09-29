// The IPL auction as a playable mode: pick a mode (a mega auction with or without retentions, or the 2027 mini
// auction), a difficulty and how many people are playing (pass the paddle: each picks a franchise), trade and make
// retentions (or releases), then bid live against the AI front offices in the 3D auction room, with the broadcast HUD
// painted over it. A career continues from a finished IPL season into its next auction (mini, or mega every three
// years). The auction saves itself after every lot and at the end of day one, and resumes from the pick screen.
// Entered on the Entry map with ?game=/Script/CRICKET26.AuctionGameMode (see UFrontendStatics::OpenAuction), plus
// ?career=1 for the career's next auction.
//
// Dev switches: -AuctionTeam=CSK skips the team pick, -AuctionAuto lets the AI run your retentions and passes on every
// lot, -AuctionSpeed=N runs the clock N times faster, -AuctionShotEvery=S saves a screenshot (HUD included) every S
// seconds into Saved/Screenshots, -AuctionQuitAfter=S quits, -AuctionMini plays the mini auction.

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

	/** The auction; built once every table has a franchise (the human sides are fixed at construction). */
	TUniquePtr<FAuction> Auction;
	int32 Team = 0;          // the human table in focus: its purse card, panels and the retention screen
	TArray<int32> Keep;      // retention (mega) or keep (mini) list being built for Team, in slab order
	float Speed = 1.f;

	// Setup, chosen on the pick screen.
	FAuctionConfig Setup;
	int32 Seats = 1;         // people playing, pass-the-paddle (1..4)
	TArray<int32> Picked;    // franchises picked so far, one per seat
	int32 RetainSeat = 0;    // whose retentions are being made
	bool bCareer = false;    // continuing a career from a finished IPL season
	bool bHasSave = false;   // a saved auction can be resumed

	// Presentation state the HUD and room read.
	FString Caption;         // the auctioneer's current line
	double CaptionAt = -100.0;
	double VoiceUntil = -100.0; // the auctioneer finishes this line before the next one starts
	int32 Seen = 0;          // events already presented
	double SoldAt = -100.0;  // when the last hammer fell, for the SOLD stamp
	int32 RaiseTo = 0;       // the human's final raise being dialled in (RTM)
	int32 JumpTo = 0;        // a jump bid being dialled in
	bool bPaused = false;    // the auction clock is held; resume, restart or exit from the HUD
	bool bDayBreak = false;  // the end of day one: the clock holds until the tables come back
	bool bCurrentSetOnly = false; // the player list shows only the set under the hammer

	/** How much of the auction plays out live: all of it, only the lots a human shortlisted, or none of it. */
	enum class EPace : uint8 { Watch, Targets, SimToEnd };
	EPace PaceMode = EPace::Watch;

	/** The analyst desk between sets: two voices over the league, up for a few seconds. */
	TArray<FString> AnalystLines;
	double AnalystAt = -100.0;
	/** A record-breaking sale: the banner's title and when it went up. */
	FString RecordBanner;
	double RecordAt = -100.0;

	enum class EPanel : uint8 { None, Purse, Players, Squad, WarRoom, Trade };
	EPanel Panel = EPanel::None;
	int32 PanelTeam = 0;     // whose squad the squad panel shows

	// The trade window (retention screen).
	int32 TradeGive = INDEX_NONE, TradeWith = INDEX_NONE, TradeGet = INDEX_NONE;
	FString TradeResult;

	bool bNoRetentions = false;
	int32 PendingPickTeam = INDEX_NONE; // franchise tapped on the pick screen, awaiting the setup choice

	void PickTeam(int32 Index);
	/** The first seat's pick with the auction's setup; later seats pick straight from the grid. */
	void PickTeamAndSetup(int32 Index, EAuctionMode Mode, bool bNoRetentionsChoice);
	void PickTeamAndFormat(int32 Index, bool bNoRetentionsChoice) { PickTeamAndSetup(Index, EAuctionMode::Mega, bNoRetentionsChoice); }
	void StartWithNoRetentions();
	void StartWithRetentions();
	void ToggleKeep(int32 Player);
	void SuggestKeep();
	void ConfirmRetentions();
	void ProposeTrade();
	void Bid(int32 Table = INDEX_NONE);
	void JumpBid(int32 Table);
	void Timeout(int32 Table);
	/** The shortlist: a player's max price (0 removes him) and whether the paddle goes up on its own. */
	void SetWish(int32 Player, int32 Max, bool bAutoBid);
	void SkipCurrentLot();
	void TogglePause();
	void EndDayBreak();
	void SetPace(EPace Pace);
	void RestartAuction();
	void ResumeSaved();
	void ExitToMenu();
	/** Auction -> tournament handoff: persists the final squads as an IPL season and opens its hub. */
	void StartSeason();
	/** The next human table (hot seat), for the focus buttons. */
	void FocusNext();

	const AAuctionRoom* GetRoom() const { return Room; }

	/** Real seconds since the page opened, for HUD animation. */
	double Now() const;

private:
	UPROPERTY()
	TObjectPtr<AAuctionRoom> Room;
	bool bViewSet = false;
	bool bAuto = false;
	int32 SetsSinceDesk = 0;
	// The auctioneer's voice: one line at a time, each played to the end before the next starts.
	UPROPERTY() TObjectPtr<class USoundWaveProcedural> VoiceWave;
	UPROPERTY() TObjectPtr<class UAudioComponent> VoiceAudio;

	void Present(const FAuctionEventRecord& E);
	void BuildAuction();
	void BeginLive();
	void SaveProgress() const;
	void ClearSave();
	void Desk(); // fills the analyst desk from the league as it stands
	static FString SavePath();
};

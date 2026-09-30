// Broadcast sequence director: the order, timing and framing of everything the broadcast shows around a ball
// once it is dead, and before the next one (toss, new batter, new bowler). Built from the first-hand Cricket 26
// reference (Docs/BROADCAST_REFERENCE_GAME_MP4.md): umpire signal, reactions, celebration, crowd, event stinger,
// replay, logo stinger, the dismissed batter's walk off, THIS OVER, and the intro cards.
//
// Pure logic, no actors and no engine types beyond maths and containers: the game mode feeds it facts and
// positions and plays what it returns. Like the other directors it never decides what happened: the match is
// settled before a sequence is built, and a sequence only moves bodies, the camera and the graphics.
//
// Units: world centimetres (the game mode's ToWorld space), seconds.

#pragma once

#include "CoreMinimal.h"

namespace CricketSequence
{
	/** One beat of the broadcast. */
	enum class ESegment : uint8
	{
		None,
		LiveHold,        // the live follow keeps rolling while the ball settles (the event strip may ride it)
		UmpireSignal,    // the bowler's-end umpire signals to the scorers
		BatterReaction,
		BowlerReaction,
		FielderReaction,
		Celebration,     // the wicket's hero and a team-mate
		TeamHuddle,      // the bowling side gathered round the hero
		CrowdCutaway,
		StumpsClose,     // the broken wicket
		BattersConfer,   // the pair mid-pitch after a wicket
		StingerIn,       // event word over the batting crest, into the replay
		Replay,          // the replay package plays here
		StingerOut,      // the logo, out of the replay
		WalkOff,         // the dismissed batter under the dismissal card
		ThisOver,        // end-of-over summary over the length bands
		BowlerIntro,     // the bowler under the bowler card
		BatterIntro,     // the incoming batter under the batter card
		Establishing,    // the ground
		TossCaptains,    // the captains at the pitch under the pitch-conditions panel
		TossCoin,        // the coin in the air
		TossResult,      // who won it and what they chose
		PairWalkOff,     // the not-out pair leave at the innings break
		WinCaptain,      // the winning captain
		Handshake,       // bowler and batter shake hands
		Count
	};

	/** How a beat is framed. */
	enum class EShot : uint8
	{
		Live,          // the broadcast director's live camera (delivery, follows)
		UmpireFront,   // waist-up from the front, slow push-in
		UmpireLow,     // low three-quarter, looking up at the arms
		CloseUp,       // head and shoulders, shallow focus
		Medium,        // waist-up
		LowWide,       // low wide lens: the player against the sky and the big screen
		HighCrane,     // high behind the player, looking over them at the field
		TwoShot,       // over one shoulder onto the other player
		Group,         // the huddle
		Crowd,         // a stand section from inside the ground
		StumpsClose,   // the striker's stumps at knee height
		ThisOver,      // long lens from behind the far stumps, down the pitch over the length bands
		TrackBeside,   // walking alongside, square on
		WalkFront,     // tracking back in front of a walking player
		WalkHigh,      // high behind a walking player
		Establishing,  // slow orbit high over the ground
		TossCoin,      // the coin against the stands
		TossTwoShot,   // the two captains side by side
		Count
	};

	/** Who a beat is about. The game mode maps each to an actor. */
	enum class ESubject : uint8
	{
		None, Striker, NonStriker, Bowler, Keeper, Hero, Umpire, DismissedBatter, Fielder, BattingCaptain, BowlingCaptain, Crowd, Ground, Coin
	};

	/** The umpire's signal for the scorers. */
	enum class ESignal : uint8 { None, Four, Six, Out, Wide, NoBall, Bye, LegBye };

	/** A graphic over the beat. */
	enum class ECard : uint8
	{
		None,
		EventStrip,      // full-width WICKET / SIX / FOUR band in place of the score strip
		Dismissal,       // name, how out, runs (balls), 4s, 6s, strike rate, fall of wicket
		Batter,          // name, runs (balls), 4s, 6s, strike rate this match
		Bowler,          // name, overs, maidens, runs, wickets, economy this match
		ThisOver,        // the over's balls as numbered dots, coloured by length
		PitchConditions, // the toss panel
		TossCall,        // the call and the coin
		TossResult,      // "X won the toss and chose to bat"
		Result,          // "X WON BY N RUNS" across the bottom
		Count
	};

	/** Outcome words: kept local so this module needs nothing from the match headers. */
	enum class EOut : uint8 { None, Bowled, Caught, LBW, RunOut, Stumped, HitWicket };

	/** How much of the broadcast to play: Broadcast is the reference; Quick keeps the essentials. */
	enum class EPacing : uint8 { Full, Broadcast, Quick };

	struct FSegment
	{
		ESegment Kind = ESegment::None;
		float Start = 0.f, Duration = 0.f;
		EShot Shot = EShot::Live;
		ESubject Subject = ESubject::None, Second = ESubject::None;
		ECard Card = ECard::None;
		ESignal Signal = ESignal::None;
		/** Which side of the subject the camera takes (+1 / -1), varied so repeated beats do not look identical. */
		float Side = 1.f;
		float End() const { return Start + Duration; }
	};

	struct FSequence
	{
		TArray<FSegment> Segments;
		float Duration() const { return Segments.Num() ? Segments.Last().End() : 0.f; }
		bool IsValid() const { return Segments.Num() > 0; }
		/** The beat on screen at T, or INDEX_NONE before the first or past the last. */
		int32 IndexAt(float T) const;
		const FSegment* At(float T) const;
		const FSegment* Find(ESegment Kind) const;
		bool Has(ESegment Kind) const { return Find(Kind) != nullptr; }
		/** Where the replay starts and how long it runs (-1 / 0 without one). */
		float ReplayStart() const;
		float ReplayDuration() const;
		/** Sets the replay's length and moves every later beat to follow it. */
		void SetReplayDuration(float Seconds);
		/** The first beat at or after the replay's end (skipping the replay lands there). */
		float AfterReplay() const;
	};

	/** Everything about the finished ball the director needs. The game mode fills it after the umpire's decision. */
	struct FBallFacts
	{
		int32 RunsRun = 0;
		int32 Boundary = 0;           // 0, 4 or 6
		bool bBatContact = false;
		bool bWide = false, bNoBall = false, bLegBye = false;
		EOut Out = EOut::None;
		bool bKeeperCatch = false, bCaughtAndBowled = false;
		bool bBeaten = false, bEdge = false, bDroppedCatch = false;
		bool bOverComplete = false;   // the over (not the innings) is done
		bool bInningsOver = false;
		bool bMatchOver = false, bTied = false;
		bool bReplay = false;         // a replay package was built
		float ReplaySeconds = 0.f;
		bool bFieldedDeep = false;    // stopped out near the rope: the fielder close-up earns its place
		bool bMilestone = false;
	};

	/** What the pre-ball intro has to show before the next delivery. */
	struct FIntroFacts
	{
		bool bToss = false;          // the first ball of the match
		bool bInningsStart = false;  // the first ball of an innings
		bool bNewBatter = false;     // a batter who has not faced walks in (after a wicket)
		bool bNewBowler = false;     // the first ball of an over
	};

	/** Anti-repetition: variants rotate so no beat reads the same twice running. */
	struct FDirector
	{
		EPacing Pacing = EPacing::Broadcast;
		FRandomStream Rng{ 2626 };
		TArray<int32> Recent; // (beat * 16 + variant) of recent picks, newest last
		int32 Pick(ESegment Beat, int32 Variants);
		float Side() { return Rng.FRand() < 0.5f ? -1.f : 1.f; }
		void Reset() { Recent.Reset(); }
	};

	/** The umpire's signal for a ball, as the scorers would need it: out first, then boundaries, then extras. */
	ESignal SignalFor(const FBallFacts& Ball);

	/** The dead-ball sequence, from the ball going dead to the next delivery (the replay slot sized from the facts). */
	FSequence BuildDeadBall(FDirector& D, const FBallFacts& Ball);

	/** The beats before a delivery: toss, innings start, new batter, new bowler (empty when none apply). */
	FSequence BuildIntro(FDirector& D, const FIntroFacts& Intro);

	/** Screen time of a beat in this pacing (the reference's measured durations at Broadcast). */
	float BeatSeconds(ESegment Beat, EPacing Pacing);

	// ---------- Stingers (drawn by the HUD, timed here) ----------

	enum class EStinger : uint8 { None, Event, Logo };

	/** How a stinger looks at one moment. Bands close from top and bottom at a tilt into a solid card. */
	struct FStingerLook
	{
		float Cover = 0.f;       // 0 open .. 1 the bands meet (full card)
		float TiltDeg = -4.f;    // the bands' slant
		float CardAlpha = 0.f;   // the card over the whole screen once closed (dissolving out at the end)
		float LabelAlpha = 0.f;  // the word or the logo
		float LabelScale = 1.f;
		float Dim = 0.f;         // the picture darkening as the stinger begins (the reference's dissolve)
		bool bVisible() const { return Cover > 0.f || CardAlpha > 0.f || Dim > 0.f; }
	};

	/** Event stinger into a replay: 1.0 s, the camera cuts to the replay at EventCut, under the full card. */
	constexpr float EventTotal = 1.0f, EventCut = 0.6f;
	/** Logo stinger out of a replay: 0.85 s, the bands close over the replay's last LogoCut seconds. */
	constexpr float LogoTotal = 0.85f, LogoCut = 0.3f;
	FStingerLook StingerLook(EStinger Kind, float T);

	/**
	 * Which stinger shows at sequence time T and its local time. The event stinger starts at its beat and runs
	 * EventTotal - EventCut into the replay; the logo stinger starts LogoCut before the replay ends.
	 */
	EStinger StingerAt(const FSequence& S, float T, float& OutLocal);

	/** A lower-third card's reveal: 0 hidden .. 1 fully shown (the header bar grows, then the stats fade in, and it fades before the beat ends). */
	struct FCardLook { float Bar = 0.f, Body = 0.f, Alpha = 0.f; };
	FCardLook CardLook(float T, float Duration);

	// ---------- Presentation cameras ----------

	/** The subject's ground point and facing, the second subject, and the scene clock. World cm. */
	struct FShotFrame
	{
		FVector Subject = FVector::ZeroVector;       // ground point under the subject
		FVector Facing = FVector(1.f, 0.f, 0.f);     // subject's facing, flat
		FVector Second = FVector::ZeroVector;        // ground point under the second subject (two-shots)
		FVector Target = FVector::ZeroVector;        // crowd: a point on the stand; coin: the coin itself; stumps: the stumps
		FVector From = FVector::ZeroVector;          // crowd: where the camera stands
		FVector PitchCentre = FVector::ZeroVector;
		float PitchLength = 2012.f;                  // cm, striker's stumps at X=0, bowler's at X=PitchLength
		float OffSign = 1.f;
		float Side = 1.f;
		float T = 0.f, Duration = 1.f;               // seconds into the beat, its length
	};

	struct FShotSolution
	{
		FVector Location = FVector::ZeroVector;
		FVector LookAt = FVector::ZeroVector;
		float FOV = 20.f;           // horizontal on 16:9
		float FocusCm = 0.f;        // 0: no depth of field
		float Aperture = 0.f;       // f-number, 0 when FocusCm is 0
	};

	/** The framing for a presentation shot. Every shot creeps in a little over the beat, as the reference's do. */
	FShotSolution SolveShot(EShot Shot, const FShotFrame& F);

	/** True when the shot follows a moving subject every frame (else it holds its station and only pans). */
	bool IsTracking(EShot Shot);

	// ---------- Bodies ----------

	/** How a hand's fingers are held: as the clip has them, open and flat, a fist, or a fist with the forefinger out. */
	enum class EHandShape : uint8 { Relaxed, Open, Fist, Point };

	/** A body's shoulders (the upper-arm joints) and axes, and its arm's lengths, all measured off the skeleton (world cm). */
	struct FArmFrame
	{
		FVector Shoulder[2] = { FVector::ZeroVector, FVector::ZeroVector }; // [0] left, [1] right
		FVector Forward = FVector(1.f, 0.f, 0.f);
		FVector Right = FVector(0.f, 1.f, 0.f);
		float Upper = 28.f; // shoulder to elbow
		float Lower = 27.f; // elbow to wrist
	};

	/**
	 * Arm targets for the two-bone solve: each wrist's point, the pole its elbow bends toward, the way its palm faces
	 * and its fingers point, and the fingers' shape. The fingers run on from the forearm (the elbow the solver will
	 * find, worked out here the same way), so the hand always carries on the line of the arm and never kinks off it.
	 * Every wrist stays inside the arm's reach, and a raise travels round an arc from the hanging arm, never in a
	 * straight line through the shoulder.
	 */
	struct FArms
	{
		FVector Hand[2] = { FVector::ZeroVector, FVector::ZeroVector };  // [0] left, [1] right
		FVector Elbow[2] = { FVector::ZeroVector, FVector::ZeroVector }; // the pole
		FVector Palm[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector Fingers[2] = { FVector::ZeroVector, FVector::ZeroVector };
		EHandShape Shape[2] = { EHandShape::Relaxed, EHandShape::Relaxed };
		float Weight[2] = { 0.f, 0.f };
	};

	/** Where a two-bone arm Upper and Lower long puts its elbow between Shoulder and Wrist, bent toward Pole. */
	FVector ElbowAt(const FVector& Shoulder, const FVector& Wrist, const FVector& Pole, float Upper, float Lower);

	/**
	 * The umpire's arms for a signal at T seconds into it. The arm takes over from the idle in the first moment,
	 * raises over RaiseTime (quicker for the side signals), holds, lowers from LowerTime before the end and hands back.
	 * OUT: the right forefinger straight up. SIX: both arms straight up, forefingers raised, palms forward. FOUR: the
	 * right arm swept to and fro across the front of the body at waist height, palm down. WIDE: both arms straight out
	 * level, palms down. NO-BALL: the right arm straight out level. BYE: an open right palm raised. LEG BYE: the right
	 * hand patting the front of the right thigh.
	 */
	constexpr float RaiseTime = 0.9f, LowerTime = 0.8f;
	FArms SignalArms(ESignal Signal, float T, float Duration, const FArmFrame& Body);

	/** Gestures for players in a beat. */
	enum class EGesture : uint8 { None, ArmsUp, FistPump, HandsOnHips, HandsOnHead, Clap, HighFive, Point };
	FArms GestureArms(EGesture Gesture, float T, float Duration, const FArmFrame& Body);

	/** Where a walker is T seconds into a walk from From toward To at Speed (cm/s), stopping on arrival. */
	FVector WalkTo(const FVector& From, const FVector& To, float Speed, float T);

	/** Stable names for the debug overlay. */
	const TCHAR* SegmentName(ESegment Kind);
}

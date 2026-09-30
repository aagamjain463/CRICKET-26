# Broadcast sequence: presentation and replays after the Cricket 26 reference

**Reference:** `Docs/game.mp4`, broken down in `Docs/BROADCAST_REFERENCE_GAME_MP4.md`.

**Goal:** a premium, realistic broadcast around every ball. That means:

- the umpire's signal, the reactions, the celebrations and the crowd;
- a replay framed by full-screen stingers;
- the walk off, THIS OVER and the intro cards;
- the toss before the first ball.

All of it follows the reference's beat order and measured timings.

## 1. How it fits together

| Piece | File | Role |
|---|---|---|
| Sequence director | `Cricket/CricketBroadcastSequence.h/.cpp` | Pure logic. It builds the dead-ball sequence and the pre-ball intro from facts about the finished ball. It also provides the stinger and card animation curves, the umpire's signal and gesture arm targets, and the framing for every presentation shot. Tested. |
| Match runtime | `Cricket/SuperOverGameModeSequence.cpp` | Builds each sequence once the match is settled. It maps each beat to actors (the hero, the team-mate, the dismissed batter), walks players, poses arms and gaze, frames the presentation camera, fills the card text, and places the THIS OVER bands and the toss coin. |
| Game mode hooks | `Cricket/SuperOverGameMode.cpp/.h` | Builds the sequence in `ScoreDelivery`. `UpdateIntro` runs before each tick's phase switch. The dead ball lasts the sequence's length. The presentation camera takes over from the director, with depth of field on High and Epic. The striker is freed to walk. A tap skips. |
| Replay angles | `Cricket/CricketBroadcast.h/.cpp` | Six new replay roles, and the reference's boundary and bowled packages. |
| Graphics | `Cricket/MatchHUDWidget.cpp` | The full-screen stingers, the live event strip, the lower-third cards, THIS OVER, the toss panels, the result bar. The score strip is hidden on scene shots. |

The match is always decided before a sequence is built. A sequence moves bodies, the camera and the graphics, and nothing else. Scoring, rules and state are only read.

## 2. The dead-ball sequence

```
LiveHold (live follow; WICKET/SIX/FOUR strip in place of the score strip)
  -> event beats
  -> StingerIn -> Replay (the package; any ball-tracking review plays in this slot) -> StingerOut
  -> WalkOff (dismissal card) | PairWalkOff (innings over) | ThisOver (+ crowd) | the match-won beats
```

| Ball | Event beats (Broadcast pacing) |
|---|---|
| Caught, LBW, stumped, run out | Umpire OUT (finger raised over about 0.9 s, 3.4 s). The crowd (1 in 2). The celebration: two-shot, close-up or low-wide on the hero with a team-mate high-fiving (3.0 s). The team huddle: everyone within 45 m runs in and rings the hero, alternate players clapping (2.6 s). The batters confer (3.0 s). The crowd (1 in 2). |
| Bowled, hit wicket | No finger. The celebration (arms up), the huddle, then the broken stumps at knee height (1.8 s). |
| Six | The live follow carries into the stands (1.6 s). Then either straight to the umpire (both arms raised slowly, from low), or the batter first: low wide-lens against the sky, or the crane from behind watching it go. |
| Four | The umpire's wave (2.8 s). Then, in turn, nothing, a batter close-up, or the bowler walking back hands on hips. |
| Wide, no-ball, bye, leg bye | The umpire's signal (2.1 s). |
| Dropped catch | The fielder, hands on head. Then the bowler close-up. |
| Beaten, edged | The bowler close-up or medium, walking back. |
| Dot, runs | One short look: the bowler walking back, the batter low-wide, or the fielder who stopped it near the rope. |
| Match won | The winning captain (arms up), the huddle, the crowd, and the bowler and batter shaking hands, all under the result bar. |

Variants rotate. No beat repeats until its other variants have played (`FDirector::Pick`), and the camera takes alternate sides of the subject.

**Pacing** (`-CricketPacing=`):

- **Broadcast**, the default, uses the reference's timings.
- **Full** is 1.25× longer.
- **Quick** drops the optional beats (reactions, crowd, huddle, confer, stumps) and shortens the rest to 0.7×.

A tap skips to the replay, then to the end of the sequence.

## 3. The intros (before a ball)

`UpdateIntro` checks, once per state, what the next ball needs:

- **Toss** (the first ball of the match): the ground from high up (3.5 s). Then the captains side by side under PITCH CONDITIONS (3.2 s). Then the coin, flicked up, hanging in slow motion and falling to the turf, with the BAT / BOWL call and a countdown (4.6 s). Then the result card, "X WON THE TOSS AND CHOSE TO BAT" (2.8 s). Then the crowd.
  - The toss is seeded and always agrees with who actually bats first. The toss winner chose to bat, or the other side won and chose to bowl.
- **Innings start**: the opener's close-up under the batter card, then the bowler low-wide under the bowler card.
- **New batter**: walks in the last 6.5 m from the pavilion side, framed high behind or tracking in front, under the batter card. This covers either end, if the pair crossed.
- **New bowler**: walks back to the top of the mark under the bowler card.

The AI does not start its run-up during an intro. A human's tap skips it. At the end of the intro everyone is put back on their marks, under a cut to the delivery camera, and the bowler sets off 2 s later.

`-CricketNoIntros` turns intros off, for captures and soaks. IPL picks wait for the dead ball to finish, as the reference's do after THIS OVER and after the walk off. The new batter's intro plays after the pick.

## 4. Cameras

**Presentation shots** (`SolveShot`) take the beat's subject and second subject from the actors.

| Shot | Framing |
|---|---|
| UmpireFront | Waist-up, 4.8 m in front, three-quarter. |
| UmpireLow | 0.85 m high, looking up at the raised arms. |
| CloseUp | Head and shoulders, 15° lens, f/2. |
| Medium | Waist-up. |
| LowWide | 0.6 m high, 52° lens: the player against the sky and the big screen. |
| HighCrane | 6.5 m up behind the player. |
| TwoShot | Over the second player's shoulder. |
| Group | The huddle. |
| Crowd | From inside the rope, at a stand section toward where the ball went, never behind the sightscreens; slow pan. |
| StumpsClose | The striker's stumps at knee height. |
| ThisOver | 8.5° lens from 26 m behind the bowler's stumps, down the pitch: the length bands compressed in front of the striker's stumps. |
| TrackBeside, WalkFront, WalkHigh | Follow a walking player. |
| Establishing | A slow orbit 26 m up. |
| TossCoin, TossTwoShot | The toss. |

- **Push-in:** every shot creeps in about 9% of its lens over the beat, as the reference's close-ups do.
- **Depth of field:** close-ups, two-shots, the crowd and the umpire focus on their subject at f/1.8-2.8 on High and Epic.
- **Cuts:** every beat change is a hard cut. The camera changes under the stingers' full card, so the viewer never sees a cut into or out of a replay.

**Replay packages** (`BuildReplayPackage`), after the reference's §4.

Every package carries exactly one **hero angle** (`bHero`): the package's clear look at what mattered.

- It plays at real speed well before the moment.
- It eases over `HeroSlowRamp` (0.5 s of ball time) into a steady `HeroSlowFactor` (0.4×).
- It holds that speed for `HeroSlowHold` (0.35 s of ball time) either side of the moment, about 1.75 s on screen, then eases back out.

It is slow enough to read the bat, the ball and the hands, and never the crawl of a super-slow. The speed curve (`ReplaySpeedAt`) now has a held plateau as well as the cosine shoulders.

Every angle plays at its true speed curve: its screen time is what the curve takes, so a slow factor is the speed the viewer actually sees. Nothing plays below `SuperSlowFactor` (now 0.3).

| Event | Angles (hero in bold) |
|---|---|
| FOUR | **The stroke**: `ReplayBeauty` (square, batter height) or `ReplayBowlerTrack` (behind the bowler, then down the pitch behind the ball). Then `ReplayLongLens` from the far stand, following the ball to the rope. |
| SIX | **The stroke**: `ReplayBeauty`, `ReplayGroundLevel` (on the turf, the batter against the sky) or `ReplayCrane`. Then `ReplayStandTilt`, behind the stroke into the stands, the lens rising past the roof. |
| BOWLED, LBW, hit wicket | The full pass. **`ReplayBowlerTrack`** down the pitch into the stumps or pads. Then `ReplayStumpCam` at knee height (bowled) or side-on (LBW), at 0.3× for a moment. |
| Caught | The full pass. The stroke that got him out, square. **The take** off the catcher (`Catch`). |
| Run out, stumped | The full pass. How it happened, square. **Square on the crease** (`RunOut`) as the bails come off. |
| Dropped catch, diving stop | The full pass, then **the fielder** (`Catch`) as the ball reaches the hands. |
| Edge | The full pass, then **the edge** side-on. |

When a package is trimmed to its angle count, the supporting angles go first. The full pass and the hero always play.

The travelling replay cameras use a quicker operator: position 9/s, rotation 10/s.

A live ball's replay carries no REPLAY tag, only its stingers, as the reference does. The highlights reel keeps its counter and its lower-third stinger.

## 5. The umpire's signals and the players' gestures

`SignalArms` and `GestureArms` give each arm four things:

- the wrist's point;
- a pole square to the arm, so the elbow's bend plane never turns over;
- the palm's facing and the fingers' line;
- the fingers' shape: open, a fist, or a fist with the forefinger out.

The fingers run straight on from the forearm. The elbow is worked out here (`ElbowAt`) just as the engine's two-bone solve will place it.

Every wrist stays inside 97.5% of the arm's measured reach. The upper arm and forearm lengths are read off the skeleton each frame.

A raise travels round an arc from the hanging arm, through the front or the side, never in a straight line through the shoulder.

The anim instance (`CricketAnimInstance::ApplyActions`) turns the hand to its facing in three steps:

1. It rolls the forearm about its own length first, as a real forearm pronates, so the hand sits straight on it as in the reference pose.
2. The wrist then makes up only what is left.
3. It curls the fingers knuckle by knuckle.

All three are blended in with the hand's weight.

Before this change, the solve swung the arm but left the hand in the idle's hanging rotation. The palm faced the wrong way and the hand looked detached from the elbow.

| Signal | Arms |
|---|---|
| OUT | The right forefinger straight up in front of the face, palm forward. |
| SIX | Both arms straight up, raised slowly round the front, forefingers up, palms to the field. |
| FOUR | The right arm swept to and fro across the front of the body at waist height, palm down. |
| WIDE | Both arms straight out level, palms down. |
| NO-BALL | The right arm straight out level. |
| BYE | An open right palm raised. |
| LEG BYE | The right hand patting the front of the right thigh. |

## 6. Graphics

- **Stingers** (`StingerLook`, measured at 10 fps off the reference). Both are drawn in the design system's deep teal, with the project's own crests and logo.
  - Event stinger, 1.0 s: the picture dips, then two slanted bands close from the top and the bottom (0.1-0.4 s). The word (WICKET, SIX, FOUR, DROPPED, REVIEW, EDGE, REPLAY) comes up under the side's crest. The camera cuts to the replay at 0.6 s under the full card, then the word grows as the card dissolves.
  - Logo stinger, 0.85 s: the bands close over the replay's last 0.3 s, the cut happens under the card, and the logo grows and dissolves into the next shot.
- **Event strip:** the full-width band in the side's colour replaces the score strip during the live hold, with the word faintly repeated and drifting either side.
- **Cards** (`CardLook`): the header bar grows out of the crest block (0.3 s), then the stats row fades in, and the card fades over the beat's last 0.3 s.
  - Dismissal: name, how out, runs (balls); balls, strike rate, 4s, 6s, fall of wicket.
  - Batter and bowler: T20 career numbers where the squads carry them (real teams and the IPL), otherwise this match's figures.
- **THIS OVER:** the over's balls as numbered discs, each over a bar in the colour of the length band it pitched in. In the world, the five length bands (yorker red, full orange, good yellow, back of a length green, short blue) are painted on the pitch, with a marker where each ball landed.
- **Toss:** pitch conditions (venue, pitch, wear, sky, day or night); BAT / BOWL with the countdown; the result card.
- **Result bar:** "X WON BY N RUNS" or "BY N WICKETS" in the winner's colour, under the closing beats.

## 7. Tests

- `CRICKET26.BroadcastSequence.Signals`, `.DeadBall`, `.Intro`, `.Cameras` (`Tests/BroadcastSequenceTests.cpp`), 576 checks, cover:
  - the beat order for each event;
  - sequences are contiguous at every pacing;
  - the stingers cover the frame at each cut;
  - replay resizing;
  - the umpire's arms;
  - every presentation camera stays above the turf and looks at something in front of it, with a broadcast lens;
  - the push-in;
  - card reveals.
- `CRICKET26.Broadcast.ReplayPackages` covers the reference's packages: a six plays the stroke then the stand tilt, with no full pass; a four plays the stroke then the long lens; bowled plays the full pass, the bowler track and the stump camera. It also checks every package:
  - exactly one hero angle;
  - the hero is held at the hero speed either side of the moment and is back at real speed at both ends;
  - no angle plays below the super-slow;
  - caught and dropped heroes are the catch camera, bowled's is the bowler track, and boundaries' is the stroke.
- `CRICKET26.BroadcastSequence.Signals` checks every umpire signal and player gesture frame by frame at 60 Hz:
  - the wrist stays inside the arm's reach and never sweeps through the shoulder;
  - the hand stays on the line of the forearm the solver will make;
  - the palm is square to the fingers;
  - no target jumps or flips between frames.

The pure sequence module and its tests were also compiled and run outside the engine, against a small shim of the core types, with `-Wall -Wshadow`: all 576 checks pass. The replay package test needs the engine. **The engine build, the full suite and an in-engine capture have not been run in this change** (the container has no Unreal Engine). Run `Scripts/run_tests.sh`, then `Scripts/capture.sh`, before merging.

## 8. Not matched yet

- **Pyro jets** at the boards for a six (reference 2:46, 12:38): needs a Niagara asset.
- **LED bails lighting** on the broken stumps (19:17): the stumps close-up is there, the lights are not.
- **In-stadium ribbon boards and big screens** showing the score, the clock and the decision pending (8:34, 11:09, 22:46). `UpdateBigScreens` still shows its own content.
- **The over's ball paths drawn in the air** on THIS OVER (10:36): the pitch bands, markers and panel are done; the trails are not.
- **The batters-confer review countdown panel** (5:34): the beat plays; the panel is not drawn. The game's reviews are LBW-only.
- **Captured celebration clips:** celebrations and the umpire's signals are procedural IK over the idle and walk (arms, wrists, fingers, gaze, chest), not motion capture. The leg bye's raised knee is not posed: the hand pats the thigh.
- **Player names on the dismissal card:** fielders carry no names, so a catch reads "c b Bowler", "c (wk)" or "c & b".
- **The fielder close-up after a boundary stop** (7:15) uses the `FielderReaction` beat, which plays on some dot and run balls.

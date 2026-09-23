# Super Over Vertical Slice — Final Production Report

Build: Unreal Engine 5.8.2 (Release-5.8, CL 56702186), Development, macOS 26.5.1 on an Apple M5 Mac with Metal.
Every number below was measured on this machine from this repository. Nothing was measured on a phone, and
nothing was compared against Cricket 26 first-hand (see H).

## A. Final playable features

- **A complete Super Over match.** Two six-ball innings, two wickets end an innings, the target is first innings
  plus one, and the chase ends when the target is reached. A tie goes to another Super Over with the other side
  batting first.
- **The rules around the scoring.** Wides and no-balls (not legal balls, one run each), byes and leg byes (leg
  byes only when a stroke was offered), wide plus runs, and overthrows to the rope. A free hit follows a no-ball
  and carries over a wide. The bouncer limit is enforced, and a ball over head height is called by the umpire.
- **Dismissals.** Bowled (including played on), LBW decided by ball tracking, caught, caught behind, run out,
  stumped and hit wicket.
- **Delivery physics.** A 240 Hz ball model with drag, Magnus, swing, seam and spin turn. The pitch bounce is
  calibrated against published ball-tracking heights.
- **Deliveries.** Seven pace deliveries and four spin deliveries for each style, released through a noisy
  release that depends on bowler Accuracy.
- **Batting.** Frame-rate-independent timing, a continuous contact response (middle, inner and outer halves,
  toe, splice, edges), 12 shot families, front-foot and back-foot play, and charging down the track to spin.
- **Fielding.**
  - An intercept solver for every fielder, with one role per fielder.
  - Catches of several kinds, including the relay catch at the rope.
  - Ground takes, misfields, and overarm, underarm and relayed throws.
  - Keeper takes with fumbles, and stumpings.
- **Running.** Calls, send-backs, close calls and run-outs, with three human running modes: safe, normal and
  aggressive.
- **AI.**
  - The bowling AI works from situation-aware plans.
  - The batting AI chooses strokes from a value table that is re-measured against the physics by a test.
  - Four difficulties change decision quality only. Physics and timing are the same at every level.
- **Player attributes.** Timing, Technique, Power, Accuracy, Movement, Catching, Throwing and RunSpeed. Each
  one is measured to move results in the right direction.
- **Presentation.**
  - A broadcast-style long-lens delivery camera and a follow camera after contact.
  - An automatic half-speed side-on replay after wickets and boundaries.
  - A score bug, a pressure line and event banners.
  - An innings-break and result scorecard over a wide ground shot.
- **Characters.** Players and umpires use the UE template mannequin with procedural cricket animation layered
  through IK: stance, backlift, a stroke planned to meet the ball, follow-through, running with the bat, the
  bowler's run-up and action, and head tracking.
- **Environment.** A generated two-tier stadium bowl with a roof and four floodlight towers, about 19,000
  spectators in block form who jump on boundaries, a mown outfield and sightscreens.
- **Audio.** Synthesised bat, edge, pitch and stumps cues, a crowd bed that swells with the play, and text
  commentary shown as captions.
- **Controls and settings.** Keyboard and touch controls feed one intent layer. There are four graphics
  quality tiers.
- **Launch.** A plain `-game` launch (no URL) starts straight into the Super Over.

## B. Major architectural changes

The slice was extended, not rebuilt. The pure simulation modules (`BallSimulation`, `BattingModel`,
`DeliveryResolver`, `FieldingModel`, `CricketAI`, `SuperOverMatch`) stay free of actors. The actor layer
(`SuperOverGameMode`, `SuperOverHUD`) plays their results back. New modules follow the same split:

- **`CricketPose`:** pure pose planning. The stroke swing is solved backwards from the simulation's contact
  point.
- **`CricketAnimInstance`:** applies the pose through IK on the template mannequin. It uses no animation
  blueprint.
- **`CricketStadium`:** pure procedural stadium and crowd geometry. It is turned into a few static meshes at
  start-up.
- **`CricketAudio`:** synthesises cues in code. **`CricketCommentary`:** chooses caption lines from the result.
- **`CricketControls`:** merges keyboard and touch into one `FCricketControls` intent per frame.
  `CricketTouch::Layout` is pure, so tests can press the same buttons the HUD draws.
- **Fielding coordinator:** assigns one job per fielder (primary, backup, boundary rider, keeper to the
  stumps, bowler to the non-striker's end) and chooses which end to throw at.
- **Graphics quality tiers:** each tier drives the engine scalability level and the crowd density.
- **Default map:** the default game map is now the empty Entry map, and a map prefix binds it to
  `SuperOverGameMode`.

## C. Files changed

Measured from the baseline commit `1745546` to `e13ec83`: 42 source, config and script files (about 5,000 lines added), plus this report.

- Project: `CRICKET26.uproject` (MeshModelingToolset enabled), `Config/DefaultEngine.ini`, `.gitignore`,
  `CRICKET26.Build.cs`.
- Scripts: `build.sh`, `run_tests.sh`, `capture.sh`, `profile.sh`.
- Simulation: `BallSimulation`, `BattingModel`, `CricketAI`, `CricketTypes.h`, `DeliveryResolver`,
  `FieldingModel`, `SuperOverMatch`.
- Actor layer: `SuperOverGameMode`, `SuperOverHUD`.
- New modules: `CricketPose`, `CricketAnimInstance`, `CricketStadium`, `CricketAudio`, `CricketCommentary`,
  `CricketControls`.
- Tests: `Tests/SuperOverTests.cpp`, `AnimationTests.cpp`, `ControlsTests.cpp`, `EnvironmentTests.cpp`,
  `PresentationTests.cpp`.
- Docs: `Docs/CRICKET26_REFERENCE.md` and this report.

`Content/` (the UE template assets) is not tracked by git.

## D. Controls

Keyboard (desktop and development):

| Key | Batting | Bowling |
|---|---|---|
| W A S D / arrow keys (held) | shot direction | move the target on the pitch |
| J | play a ground stroke | — |
| K | play a lofted stroke | — |
| L | defend | — |
| R | cycle the running mode (safe / normal / aggressive) | — |
| 1–7 | — | choose the delivery (pace has 7, spin has 4) |
| Space | — | start the run-up, then press again to release |
| Enter | next innings or next match; skips a replay | same |

Debug keys:

| Key | Action |
|---|---|
| F1 | debug overlay (plan, release, contact zone, timing error, fielding, running, seed) |
| F2 | flip the striker's hand |
| F3 | cycle the bowler type |
| F4 | draw the trajectory |
| F5 | force a wicket |
| F6 | cycle the AI difficulty |
| F7 | cycle the graphics quality |
| F8 | toggle AI vs AI |

Touch controls are on by default on iOS and Android; use `-CricketTouch` to enable them on desktop, where the
mouse acts as a finger. The layout:

- A thumb stick on the left sets the shot direction or the bowling target.
- DEFEND, GROUND and LOFT are along the bottom right, with RUN above them.
- When bowling there is a BOWL button (it becomes RELEASE during the run-up) and a delivery list.
- Tap anywhere to continue or to skip a replay.

Buttons act on touch-down.

Command-line options:

| Option | Effect |
|---|---|
| `-CricketAutoPlay` | AI plays both sides |
| `-CricketQuitAfter=N` | quit after N deliveries |
| `-CricketDifficulty=0..3` | Easy, Medium, Hard or Legend |
| `-CricketQuality=0..3` | Low, Medium, High or Epic |
| `-CricketTouch` | touch UI on desktop |
| `-CricketTouchScript` | a scripted human side that plays through injected touch events |
| `-CricketShotBall=N`, `-CricketShotEvery=S` | capture the game view during delivery N |
| `-CricketRecordAudio` | record delivery N's mixed audio to a WAV file |
| `-CricketDevCam=...` | development camera |

## E. Automated test results

Command: `Scripts/run_tests.sh CRICKET26.`, run headless (`-nullrhi -nosound`) on the build at `e13ec83`.

**Result: 50 tests, 50 passed, 0 failed** (from `Saved/TestRun.log`).

| Area | Tests |
|---|---|
| Rules | SixLegalBallsEndInnings, TwoWicketsEndInnings, WidesAndNoBallsAreNotLegal, ByesLegByesAndWideRuns, OverthrowsToTheBoundary, FreeHitCarriesOverWide, BouncerLimitAndConfigurableRules, StumpedHitWicketAndLegality, StrikeRotation, ChaseEndsWhenTargetReached, DefendingSideWins, TieGoesToAnotherSuperOver, IllegalInputRejected |
| Umpire | BeamerBouncerAndOverHead, LegByesNeedAStroke, StumpedDownTheTrack |
| Ball | PitchesWhereAimed, BouncerRisesYorkerDoesNot, DeterministicAndNoBall, PaceVariationsAndExecution, SpinVariationsDiffer, SwingAndTurnDirections, ReleaseErrorsStayPhysical |
| Batting | PerfectDriveMiddlesAndGoesStraight, ContactQualityIsContinuous, LateGoesFinerAndVeryLateMisses, LateMovementFindsTheEdge, BowledAndLBW, ShotFamiliesGoWhereTheyShould, PressTimeFrameRateIndependent |
| Fielding / keeper / running | ActionsCatchesThrows, CatchFourSixRunning, CoordinatorRolesAndMotion, KeeperTakesBeatenBall, Keeper.TakesAndStumping, Running.CallsSendBacksAndCloseCalls |
| AI and match | ShotKnowledge, DifficultyIsDecisionQuality, AttributeCurves, DefaultSquadsScoreLikeASuperOver, Match.AIvsAICompletesLegally |
| Acceptance | ScenariosAToL |
| Presentation and other systems | Animation.BowlingArm, Animation.ContactBatPosition, Animation.StrokeMeetsBall, Presentation.BallScale, Audio.CueSynthesis, Commentary.Lines, Environment.Stadium, Touch.Controls |

Selected measured output from that run:

- **Acceptance scenarios A–L.** All twelve are reachable through the real resolver and match rules. The
  logged outcomes:
  - A: middled straight drive, no run.
  - B: yorker dug out, no run.
  - C: pull, caught at deep square leg.
  - D: late outswinger, OUTSIDE EDGE.
  - E: spin beats the bat, keeper fumbles, no run.
  - F: STUMPED.
  - G: quick single.
  - H: RUN OUT on the second run.
  - I: mistimed loft, caught at long off.
  - J: SIX.
  - K: last ball with 2 needed, won with a six.
  - L: a tie, then the second Super Over starts with the other side batting.
- **AI soak** (40 games, 469 deliveries, Hard): average first innings 15.1.
  - 68 dots, 80 fours and 132 sixes.
  - 73 wickets, of which 50 caught and 7 run out.
  - 3 wides, 3 no-balls and 35 edges.
  - Intents: 423 lofts, 46 ground strokes and 34 charges.
- **Difficulty.**
  - Batting AI, easy / hard / legend: 11.6 / 13.3 / 13.5 runs per innings, with 95 / 12 / 12 run-outs.
  - Bowling AI concedes 15.4 / 13.3 / 12.9.
- **Running sweep** (1,500 balls): safe / normal / aggressive score 1,441 / 1,610 / 1,731 runs, with 0 / 6 / 33
  run-outs.
- **Default squads** (the sides the game ships with, 312 innings): 16.2 runs and 0.56 wickets per innings, with
  34% of balls hit for six. Before the rebalance in `e13ec83` the figures were 17.9 runs and 40% sixes.
- **Keeper fumbles:** standing back to pace 1.0%, standing up to spin 4.8%, standing up down the leg side
  13.0%.

## F. Soak results

In-game soak on the current build (`e13ec83`), started with the plain launch (no map URL). Settings: AI vs AI, 150 deliveries, Medium quality
(`-CricketQuality=1`), a 1280x720 window, sound on.

| Measure | Result |
|---|---|
| Deliveries | 150 of 150, no crash |
| Errors, warnings or ensures from the game | 0 |
| Frame time, average / p99 | 9.2 / 17.0 ms over 193,189 frames (the vsync cap did not apply in this windowed run) |
| Game thread, average / p99 | 1.4 / 3.8 ms |
| GPU, average / p99 | 6.2 / 9.8 ms |
| Striker's hands from the bat grip | median 0.0 cm, p95 3.4 cm, max 37.6 cm over 325,220 samples |
| Outcomes | 61 sixes (41%), 22 fours, 13 caught, 4 run out, 3 dropped |

**The in-game six rate of 41% is the most important finding of this report.** The previous soak with the old
squads measured 47%. The headless squads test measures 34% for the same squads, so the in-game match differs
from that test somewhere, and the difference has not been found. Both numbers are far above real Super Over
cricket.

## G. Performance

All figures come from `Scripts/profile.sh` (AI vs AI, 1280x720 window, vsync on) on the Apple M5 Mac with
Metal. Frame times are average / 99th percentile.

| Renderer | Tier | Frame (ms) | Game thread (ms) | GPU average (ms) |
|---|---|---|---|---|
| Desktop (SM6) | Low | 16.7 | 1–2 | 7.5 |
| Desktop (SM6) | Medium | 16.7 | 1–2 | 6.9 |
| Desktop (SM6) | High | 16.7 | 1–2 | 11.1 |
| Desktop (SM6) | Epic | 16.7 | 1–2 | 12.1 |
| Mobile preview (`-FeatureLevelES31`) | Low | 16.7 | 1–2 | 3.3 |
| Mobile preview (`-FeatureLevelES31`) | Medium | 16.7 | 1–2 | 9.9 |

- Every tier holds the 60 fps vsync cap. The 99th percentile is at most 17.8 ms for most tiers.
- The same tier measured in separate sessions varied by up to 2x with other load on the machine.
- The stadium is about 250,000 triangles in 75 meshes. The test budget is under 300,000 triangles.
- **Not measured:** any phone, any packaged build, memory, thermals and battery.

## H. Cricket 26 reference comparison

**No Cricket 26 behaviour has been observed first-hand in this project.** I have no licensed copy or gameplay
capture, and I cannot watch video. Every Cricket 26 cell in `Docs/CRICKET26_REFERENCE.md` therefore stays
**UNVERIFIED**. The project is compared only against real cricket: the Laws, playing conditions, published
ball-tracking heights and the 2019 World Cup final Super Over. No Cricket 26 code, assets, UI, audio or
commentary was used.

## I. Visual and animation QA

What was genuinely inspected, all through in-engine game-view captures (`Scripts/capture.sh`, which saves the
game's own render; no desktop screenshots were taken):

- **Deliveries and follow camera.** Contact sheets of individual deliveries show:
  - the long-lens delivery framing (striker, keeper and the bowler's-end umpire);
  - the follow camera after contact;
  - a six carried into the stands with the SIX banner;
  - the half-speed side-on replay tagged REPLAY.
- **Animation.**
  - Captures of the striker's stance, backlift, stroke and follow-through, and of the bowler's run-up and
    action.
  - The logged IK error from both hands to the bat handle: median 0 cm, 95th percentile 3.6 cm in the soak.
  - Its 1,799 cm maximum was a diagnostic artefact: the error was compared against targets from before a
    crease reset. The fix is in `30ac3e0`, and the re-run is in F.
- **Stadium.** Captures of the bowl, the crowd, the jump on boundaries, the stripes and the sightscreens.
- **Innings break and result.** The scorecard over a wide ground shot, with the target or result in the
  winner's colour.
- **Audio.** A recorded six (Ball5.wav) peaks at -7.2 dBFS on the bat crack, with no clipped samples and an RMS
  of -31.4 dBFS. **No listener has judged the sound quality.**

Not inspected:

- Each acceptance scenario's in-game presentation, one by one. The scenarios are verified at resolver level
  only.
- Close-up crowd quality. The spectators are blocks and look like blocks.
- Hands and fingers. There are no finger poses.

## J. Mobile status

**No mobile build exists. Packaging is blocked by the toolchain on this machine.**

- **Android:** no Android SDK or NDK is installed.
- **iOS:** Xcode 26.6 with the iOS 26.5 SDK is present. However, this UE 5.8 install has no iOS target
  platform (`Engine/Platforms/IOS` holds only plugins), and the keychain has 0 code-signing identities.

Done without a device:

- The ES3.1 mobile renderer runs in the editor preview (`-FeatureLevelES31`), with GPU times as in G.
- Phones default to the Medium tier.
- The touch UI works on desktop, and a full match plays through injected touch events
  (`-CricketTouchScript`).

The slice has never run on a phone.

## K. How to play

The exact launch path, tested on this Mac:

```sh
cd CRICKET26
Scripts/build.sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "$PWD/CRICKET26.uproject" -game -windowed -ResX=1280 -ResY=720
```

- With no map URL, the game opens the Entry map and the Super Over starts. The human side is the home team
  (team 0) and bats first.
- Add `-CricketAutoPlay` to watch AI vs AI, or `-CricketTouch` to try the touch layout.
- Opening the editor shows the Third Person template level. That level keeps its own game mode.
- Tests: `Scripts/run_tests.sh CRICKET26.` (results in `Saved/TestRun.log`).
- Captures: `Scripts/capture.sh N`.
- Performance: `Scripts/profile.sh N`.

## L. Remaining limitations

- **Characters.** The characters are the Epic template mannequin, tinted by team. There is no mocap, no finger
  poses, no dive animation (a diving fielder's actor tips over) and no stroke-specific footwork beyond the
  simulation's movement. Motion reads as a rig, not as a broadcast athlete.
- **Crowd and stadium.**
  - The crowd is coloured boxes.
  - The floodlights are geometry only: no light sources and no night mood.
  - There is no scoreboard screen, no terrain beyond the roof and no stadium dressing.
- **Audio.** All sound is synthesised placeholder. The commentary is text captions only. There is no voice and
  no music, and no one has listened critically.
- **Presentation.** There is one delivery angle, one follow camera, one replay angle and a wide ground shot.
  There are no multi-angle replays, no crowd or player close-ups, no player intros and no Hawk-Eye or DRS
  graphics. The HUD uses Canvas text and boxes.
- **Six-heavy scoring. This is the largest cricket-correctness gap.**
  - With average (0.6) players, 28% of AI deliveries go for six. With the shipped squads it is 34%.
  - For comparison, the two 2019 World Cup final Super Overs had 2 sixes in 12 balls.
  - The AI's measured stroke values show why: lofting a good-length pace ball returns 3.5 runs a ball for a 14%
    chance of getting out, and lofting a short ball returns 3.9 runs for 7%. So the AI lofts 9 balls in 10, and
    the bowler's skill barely matters. Raising both bowlers to Accuracy 0.9 cut only about 1.7 runs an innings.
  - The cause is the contact model. Timing error turns the face and slows the bat, but it does not move the
    point of contact. A 40 ms mistime keeps 84% of bat speed.
  - Tightening that changes how batting feels to a human player, so it needs a human playtest first.
  - Dots fell to 14.5%, and the soak band floor was lowered to 10% with the real-world reason recorded in the
    test.
- **Execution error.** Every middled loft in the acceptance sweep cleared the rope. However, in that sweep of
  lofts pressed at perfect timing off 3–5 m lengths, only 10 of 24 were sixes. The contact point still
  scatters, so the rest came off other parts of the bat.
- **Unmodelled cricket.** Obstructing the field, handled ball, timed out, retirements, DRS reviews, fielder
  tactics set by the human, and the Super Over batter and bowler selection. The human bats and bowls with fixed
  players.
- **Attributes.** PaceKph is not covered by the attribute curves test.
- **Acceptance scenarios.** They are proven at resolver level only. They are not captured one by one in-game.
- **Cricket 26.** Every comparison cell is UNVERIFIED.
- **Measurement scope.** Performance was measured on one Mac only. The "mobile" numbers are the ES3.1 renderer
  on desktop hardware.

## M. Release blockers

1. **Packaging toolchain.** An Android SDK and NDK, or the UE iOS target platform plus a signing identity, is
   needed. Only the user can install or supply these.
2. **Device testing.** Frame time, thermals, memory and touch feel on a real mid-range phone are unmeasured.
3. **Content.** The mannequin and its animations are loaded from the untracked `Content/` folder (UE template,
   under the Epic EULA). A clean checkout falls back to shape markers.
4. **Licensed or authored art and audio.** Players, a stadium, crowd, commentary voice and music are needed to
   get beyond placeholder quality. No IPL, team, sponsor, broadcast or Cricket 26 material may be used without
   a licence.
5. **Human playtest.** No human has played the keyboard or touch controls for feel. All play evidence comes
   from AI, scripted input and tests.

## N. Next development area

Before any new mode, finish the slice on hardware:

1. Install a mobile toolchain and package it.
2. Profile on a mid-range phone.
3. Have humans playtest the batting timing and the touch layout.
4. Retune the contact model's timing falloff against that playtest, so lofting a good-length ball is a real
   risk. Then re-measure the AI stroke table.

After that, the candidates are authored character animation (mocap) or licensed art. The auction and franchise
game is deliberately **not** started in this mission.

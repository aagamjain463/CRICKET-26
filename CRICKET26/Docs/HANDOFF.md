# Handoff: CRICKET26 parity work (2026-09-24)

This note lets a new agent pick up the "make it like Cricket 26" work where it stopped. Read
`Docs/MASTER_PLAN.md` first: it is the plan this work now follows (it absorbs the 8 items of
`Docs/CRICKET26_PARITY_PLAN.md`).

The captured-animation work (friend's batting takes, sprint, fielding dive and throw) is done and committed; see
"Captured animations" below and `Docs/ANIM_HANDOFF.md` for the detail.

## Project basics

- Unreal Engine 5.8 project at `/Users/aagamjain/Desktop/CRICKET-26/CRICKET26`. The git root is the parent
  folder (`/Users/aagamjain/Desktop/CRICKET-26`), so git paths start with `CRICKET26/`.
- Game code: `Source/CRICKET26/Cricket/` (Super Over game mode, HUD, ball simulation, delivery resolver,
  batting model, AI). Tests: `Source/CRICKET26/Cricket/Tests/`.
- Scripts:
  - `Scripts/build.sh`: builds the game (compiled with `-Werror -Wshadow`, so shadowed locals fail the build).
  - `Scripts/run_tests.sh [Filter]`: runs the automation tests and writes `Saved/TestRun.log`. The full suite is
    61 tests and takes about 80 s. Count passes with `grep -c "Result={Success}" Saved/TestRun.log`.
  - `Scripts/capture.sh N [args]`: plays AI vs AI and saves game-view frames of delivery N (including the wait
    before it) to `Saved/Screenshots/MacEditor/BallN_*.png`, with a log in `Saved/Capture.log`. Never use the
    desktop `screencapture`.
  - `Scripts/profile.sh`: perf soak.
  - `Scripts/metahuman/make_players.sh`: builds the MetaHuman players (see item 7).
- The test helpers `PlaySuperOvers` and `FDifficultyStats` must stay in `SuperOverTests.cpp` (unity build).
- clangd errors such as "'CoreMinimal.h' file not found" are noise: clangd has no UE include paths.
- After code changes run `graphify update .`.

## Rules the owner set

- Do not restart the project or rebuild the Super Over from scratch. Never hard reset, blindly delete assets,
  force checkout, or rewrite history.
- Do not fake tests, playtesting or Cricket 26 comparisons. Cricket 26 comparisons stay UNVERIFIED.
- Do not ship or copy unlicensed or proprietary content (team or IPL logos, official kits, real player likenesses,
  broadcast graphics, commentary, music, Cricket 26 assets). Check licence, commercial use, skeleton and mobile
  cost before importing any external asset. Do not use `~/Downloads/Cricket26_BasePlayer.fbx`: its licence
  cannot be verified.
- Commit only `Source/`, `Docs/`, `Config/`, `Scripts/` and `CRICKET26.uproject`. Never commit `Content/`,
  `.serena/`, `graphify-out/` or `Saved/`. End each commit message with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Do not start the auction or franchise mode.
- Keep working from item to item without asking permission. Stop only for something only the owner can
  supply. Give short progress notes during long work.

## Status by plan item

| # | Item | Status |
|---|---|---|
| 1 | Batting feel | Done, commit `1157fad` |
| 2 | Broadcast HUD | Done, commit `5b5bf0c` (score bar, speed gun, this-over discs, player cards; checked in capture; 50/50 tests) |
| 3 | Ball tracking | LBW tracking and its Hawk-Eye view done (master plan M8, 6.5); pitch map and wagon wheel on the scorecard done (6.4) |
| 4 | Camera director | First cuts done: boundary, fielder and close-up shots (master plan M8, 6.1) |
| 5 | Stadium | Done for master plan M9 (5.1 to 5.7), see below; the big screens' live replay feed was left out |
| 6 | T20 and ODI formats | Not started |
| 7 | MetaHuman players | Done (master plan M2) |
| 8 | Audio | Not started |

## Item 7: MetaHuman players (done)

- `Scripts/metahuman/make_players.sh` builds 10 characters (8 players, 2 umpires) from the engine's MetaHuman
  Creator presets into `Content/MetaHumans/<Name>/BP_<Name>`. Content is never committed, so a fresh clone must run it.
- `AddBody(Marker, MetaHumanName)` spawns the blueprint on the figure marker and poses its "Body" component. It falls
  back to the Manny mannequin when the blueprint is missing or with `-CricketMannequin`.
- Hand IK: the blueprints froze each body's pose while it was off screen, so the first frames after every camera cut
  drew stale limbs (hands up to 88 cm off the bat). Bodies now tick and refresh their bones off screen. The capture
  log's "striker hands from bat grip" max is the regression check: 31 cm, all of it from one-frame load hitches
  (the mannequin shows 32 cm).
- Perf, Medium, 12 deliveries: MetaHumans 14.5 ms frame and 9.9 ms GPU; mannequin 11.2 ms and 7.8 ms.
- The players are cooked through `DirectoriesToAlwaysCook` in `Config/DefaultGame.ini`. No packaged build has
  tested this yet.
- Kit: `Scripts/metahuman/make_kit.sh` (run after `make_players.sh`) gives each player a cricket kit: a collared shirt,
  trousers and shoes. Unreal builds each body once more without the preset garment into `/Game/MetaHumans/Bare`,
  because the garment's hidden-face map cuts the skin under it out of the real build. That full body goes to Blender
  (`make_kit.py`), which cuts, smooths and pushes the pieces out from the skin, keeping the body's skin weights. The
  kit comes back as `/Game/MetaHumans/<Name>/Kit/SKM_<Name>_Kit`. `AddBody` hides the preset garment and gives
  the kit the body's pose. `Paint` colours the shirt in the team colour, the trousers in a darker shade and the
  shoes white. The same script makes `SKM_<Name>_Gear` (pads, gloves, and a helmet with a grille), which only the
  two batters wear, `SKM_<Name>_Keeper` (white pads and gloves for the keeper, who is always `Fielders[0]`) and
  `SKM_<Name>_Hat` (the umpires' white hat). `make_bat.py` models the bat (`/Game/MetaHumans/Kit/SM_Bat`, willow grain from `M_Bat`), which
  replaces the box blade and cylinder handle when it is there. Needs Blender at `/Applications/Blender.app`.
- Kit material: `M_Kit` is a cloth-shaded material built by `kit_ue.py`. It uses a knit micro-normal and soft
  memory wrinkles from the MetaHuman plugin's clothing textures, and a fuzz sheen. `Paint` sets its `Fabric` (0 for
  the helmet's shell and grille, 0.3 for the shoes, 1 for cloth) and `Ribs` (1 on the pads) per slot. To rebuild only
  the material: `KIT_STEP=material KIT_DIR=Saved/Kit KIT_NAMES=x UnrealEditor-Cmd ... -script=Scripts/metahuman/kit_ue.py`.
- Outfits: `Scripts/metahuman/outfit_ue.py` dresses the players in Epic's free parametric MetaHuman outfits from Fab
  (Standard License): the tucked long-sleeve T-shirt, slim trousers and running shoes (`WI_OA_TshirtTkLngSlv`,
  `WI_OA_Jeans_slm`, `WI_OA_RunningShoes`). They resize to each body in MetaHuman Creator, so they have real folds,
  seams and a collar. `OUTFIT_PKGS="a.mhpkg:b.mhpkg"` imports the packages into `/Game/MetaHumans/Outfits`, and
  `OUTFIT_WEAR="MH_Home_Opener ..."` rebuilds each named character in them (no cloud step). All 10 are built this way.
  `AddBody` sees a `WI_OA_` material and keeps the outfit on show instead of the Blender kit. The gear still comes
  from `make_kit.sh`. `Paint` tints the outfit: shirt in the team colour, trousers darker, shoes white. The only free
  trousers are jeans, so `Paint` swaps their faded-denim colour map for white, sets `div_fabric` to white and turns off
  the twill overlay. That leaves plain team-coloured cloth, with the seams and folds still in the normal and AO maps.
- Helmet: when `Saved/Kit/cricket_helmet.glb` is there (`HELMET_GLB` overrides the path), `make_kit.py` uses
  "Cricket Helmet" by Helindu (Sketchfab, CC-BY 4.0; the credit must ship with the game). The script decimates it to
  about 8000 faces and fits it where the modelled shell sat. The shell and ear guards get `Gear_Helmet` (dark team
  shade), and the grille and chin strap get `Gear_Grille`. Without the file, the modelled shell and grille are used.
  The .glb is not committed: download it from Sketchfab (free, sign-in needed) into `Saved/Kit/`.
- Pads: modelled in `make_kit.py` on a smooth grid round each leg, measured from the body. They have a knee roll of
  three bolsters, 7 canes down the shin and 5 up the thigh, flat side wings, and 3 straps round the back of the leg
  (`Gear_Straps`, team colour). Both legs' grids face outwards (`outward`). The right leg is the left mirrored and
  would otherwise wind the other way, so solidify thickened it out through its own grooves. `both` checks that the
  two legs' pieces stand off their legs alike and stops the build if they don't.

MetaHuman scripting notes (UE 5.8 Python):
- It runs as a commandlet:
  `-run=pythonscript -AllowCommandletRendering -dpcvars=TextureGraph.AllowCommandlets=1`. Call
  `save_directory` after the build.
- `can_build_meta_human` needs both the face rig and the high-resolution textures. `prepare()` requests
  textures first and asks for the rig only while the check still fails, with 3 attempts.
- The cloud auto-rig takes about 4 to 5 minutes per character. The commandlet uses about 1.2 GB RAM.
- To rebuild one character: `MHMAKE_ONLY=<Name> Scripts/metahuman/make_players.sh`. Already-built characters
  are skipped. Progress is logged: `grep MHMAKE Saved/MHMake.log`.

## Item 3: ball tracking

- `FDeliveryResult::Tracking` (`FBallTracking`, `DeliveryResolver.h`) is filled on every pad impact: the impact
  point, the projected path to the stumps plane (`SimulateToPlane` now records the path), the pitch and impact
  lines, and the calls (pitched outside leg, impact in line, wickets hitting, umpire's call). The LBW decision is
  made from these calls; `CRICKET26.Umpire.BallTrackingMatchesLBW` checks they agree.
- The game mode reviews every pad impact (`bReviewThis`, `IsReviewing()`, `ReviewProgress()`), after the replay if
  there is one. `UpdateTracking` spawns the trail segments on the first review, lays them along the path as the
  review reveals it, and hides the players through the player controller's `HiddenActors`. The HUD draws the
  "BALL TRACKING" panel.
- To see one: `Scripts/capture.sh 3 -CricketAiLeaves` (the AI batter leaves every ball; ball 3 is an LBW).
- `FinishDelivery` records every ball in `Marks` (`FBallMark`: where it pitched, where the stroke went, runs,
  wicket). The scorecard draws the innings just played as a wagon wheel on its left and a pitch map on its right.
  To see them: `Scripts/capture.sh 7 -CricketQuitAfter=7` (the innings break before ball 7).
- The edge detector (`CricketDelivery::EdgeSignal`, `FBallTracking::ImpactTime`) draws over the super slow-motion
  replay: `Scripts/capture.sh 4 -CricketQuitAfter=4` shows it on an edged catch.
- Player reviews: every pad impact goes to an appeal. `CricketUmpire::GivesLBW` gives the on-field call, and while
  `bAwaitingReview` holds the ball the HUD shows the "LBW APPEAL" panel. The human presses V to review or Enter to
  accept (touch players can only accept); the AI decides after 1.5 s (`AiReviews`). `SettleReview` applies
  `CricketUmpire::Review` and `ScoreDelivery` scores the ball. The ball-tracking panel ends with the verdict. Each
  team has `Rules.ReviewsPerTeam` (1) reviews, lost when the decision stands. To see one:
  `Scripts/capture.sh 3 -CricketAiLeaves`.
- Third umpire: `FDeliveryResult::BrokenTime`, `bBrokenAtStrikerEnd` and `HomeMargin` describe every broken wicket
  (run out chance or stumping chance). `CricketUmpire::RefersToThirdUmpire` sends calls within `CloseCall` (0.12 s)
  upstairs; `bAwaitingThirdUmpire` holds the ball for `ThirdUmpireTime` while `ThirdUmpireBallTime()` rolls the
  frames, then `ScoreDelivery` scores it and the HUD shows the verdict over the close-up. `-CricketRunMargin=S` sets
  the AI batters' running margin (negative: suicidal) to make run outs. To see one:
  `Scripts/capture.sh 18 -CricketDifficulty=1 -CricketQuitAfter=18`.

## Item 4: camera director

- `UpdatePresentation` picks one shot a frame (`EShot`): the delivery view, the follow view, then a boundary
  camera beyond the rope where a four or six crosses it, a fielder camera as the fielder gathers or catches, and
  once the ball is dead a close-up of the bowler after a wicket or of the striker otherwise. The replay, review
  and scorecard shots come after these.
- A change of shot is a cut (`LastShot`, `bCutCamera`), except delivery to follow, which stays a smooth move.
- `ReplayDelay` is 2 seconds, so the close-up plays before the replay.
- A replay has two angles (`ReplayAngle()`, `ReplayBallTime()`): side-on at half speed, then end-on in super slow
  motion around the contact. The HUD tags the second "SUPER SLOW-MO".
- To see them: `Scripts/capture.sh 1` (a four) and `Scripts/capture.sh 4` (a diving catch, then a wicket
  close-up).
- Highlight reel: every ball that earned a replay (`bReplayThis`) is kept in `Highlights`. When an innings or the
  match ends, the dead ball flows into the reel. `PlayClip` restores each clip's delivery and replays it on both
  angles, tagged "HIGHLIGHTS n/m", then puts the live ball back (`LiveClip`) and shows the scorecard. Enter skips
  the whole reel. To see it: `Scripts/capture.sh 6 -CricketQuitAfter=6`.

## Item 5: stadium, pitch and crowd (master plan M9)

- Everything is generated, with no third-party art. `CricketStadium::Build` makes the bowl in code (stands, roof and
  trusses, floodlight towers, big screens, LED boards, rope, dugouts, media box, crowd seats). `Scripts/stadium/make_stadium.sh`
  builds the textures and materials into `Content/Stadium` (not committed, so a fresh clone must run it) and the
  instanced spectator in Blender. Without those assets the game falls back to flat colours and a block crowd.
- Venues (`CricketStadium::Venue`, `-CricketVenue=0..2`; random in play, 0 in capture runs): Harbourside Oval
  (flat pitch, clear day), Greenhill Park (green top, overcast, volumetric cloud) and Sunfort Stadium (worn dusty
  turner, at night under four floodlight towers). Each has its own seat colours and its name on the big screens.
- The pitch feeds the ball's physics (`CricketBall::Conditions`): pitch type, wear, cloud and dew scale seam, swing,
  turn and bounce, and the rough outside the stumps (`IsInRough`) keeps the ball lower and turns it more. Wear grows
  by 0.02 a ball. `CRICKET26.Ball.PitchCharacterAndWear` checks it all.
- Pitch marks: `DrawPitchMarks` stamps each ball's landing spot and the bowler's footholds into `MarksTarget`, a
  render target the pitch material reads over the middle 23 m by 3.2 m. To see them: `Scripts/capture.sh 6
  -CricketQuitAfter=6 -CricketVenue=2 -CricketDevCam=0.5,0.01,8,5,0,0,55`.
- Crowd: one instanced mesh of about 18,000 fans at High, whose material moves them (`Excite`). About 680 flags in
  the teams' colours ride on a second instanced mesh (`CricketStadium::Flag`, `M_Flag`). Both materials take a `Fill`
  emissive of a few percent of the venue's light: without it a packed stand under the roof renders near black.
- Perf, Apple Silicon Mac, 1280x720, three deliveries, GPU average ms (Low / Medium / High / Epic): night 6.8 / 9.3 /
  17.7 / 36.5, overcast 6.2 / 8.7 / 15.4, clear day 14.2 at High. Each shadowed floodlight costs about 1 to 2 ms, so
  High gives shadows to two opposite towers and Epic to all four (four at High was 20.0).

Stadium scripting notes (UE 5.8):
- In Custom material nodes sample with `Texture2DSample(Tex, TexSampler, uv)`. An input must not share a name with
  an output (the pitch's `Rough` output forced the `RoughSpots` input); a clash fails the material only at run time,
  so check `Saved/Capture.log` for "Failed to compile Material".
- A light's spawn rotation is added to its component's built-in tilt. Spawn at identity and call
  `SetActorLocationAndRotation` afterwards (`SpawnSun`, the floodlights).

## Captured animations

- Pipeline: `Scripts/anim/cut_clips.sh` cuts a batting video into one clip per shot; DeepMotion Animate 3D (free
  tier, web) turns each into an FBX; Mixamo (web) supplies the sprint, throw and dives. `Scripts/anim/import_anims.sh`
  imports both folders under `~/Downloads/mocap` headless and retargets them through IK Rigs onto the mannequin skeleton
  as `/Game/Anims/Mocap/<Name>` (not committed, so a fresh clone must run it with the FBX files in place).
- In game: strokes (`CricketPose::StrokeClip`, right-handers only), sprint over jog, the throw
  (`CricketPose::ThrowClip`) and the dives (`CricketPose::DiveClip`) blend over the procedural bodies, with IK kept for
  the bat. Every clip is loaded quietly, so the game still runs on the procedural poses without them.
- Sources and licences:
  - DeepMotion free tier: **no commercial licence**. Fine for a prototype; shipping needs a paid plan or a re-capture.
  - The batting takes are of the owner's friend: get their OK before any public release.
  - Mixamo clips: royalty-free for games.
- `-CricketAiShot=Intent,Dir` makes the AI batter play one intent (1 defend, 2 ground, 3 loft) toward Dir degrees
  (+ off side) to every ball, timed as it would. The ball's length still picks the stroke, so a scan shows each clip:
  `Scripts/capture.sh 1 -CricketAiShot=2,-55` gives flicks and pulls, `2,0` drives and punches, `1,0` defences.
- Tests: `CRICKET26.Animation.StrokeClips`, `FieldingClips`, `BallInHand`. Capture recipes are in `ANIM_HANDOFF.md`.

## Blocked on the owner

- A motion-capture performer, voice commentary, and licences for teams, players and music.
- A phone plus the Android SDK or an iOS signing identity for the mobile build.
- Human playtesting and visual sign-off.

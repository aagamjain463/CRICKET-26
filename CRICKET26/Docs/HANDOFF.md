# Handoff: CRICKET26 parity work (2026-09-24)

This note lets a new agent pick up the "make it like Cricket 26" work where it stopped. Read
`Docs/MASTER_PLAN.md` first: it is the plan this work now follows (it absorbs the 8 items of
`Docs/CRICKET26_PARITY_PLAN.md`).

## Project basics

- Unreal Engine 5.8 project at `/Users/aagamjain/Desktop/CRICKET-26/CRICKET26`. The git root is the parent
  folder (`/Users/aagamjain/Desktop/CRICKET-26`), so git paths start with `CRICKET26/`.
- Game code: `Source/CRICKET26/Cricket/` (Super Over game mode, HUD, ball simulation, delivery resolver,
  batting model, AI). Tests: `Source/CRICKET26/Cricket/Tests/`.
- Scripts:
  - `Scripts/build.sh`: builds the game (compiled with `-Werror -Wshadow`, so shadowed locals fail the build).
  - `Scripts/run_tests.sh [Filter]`: runs the automation tests and writes `Saved/TestRun.log`. The full suite is
    52 tests and takes about 80 s. Count passes with `grep -c "Result={Success}" Saved/TestRun.log`.
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
| 5 | Stadium | Not started (CC0 sources only) |
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
- Not started: the edge detector and player reviews.

## Item 4: camera director

- `UpdatePresentation` picks one shot a frame (`EShot`): the delivery view, the follow view, then a boundary
  camera beyond the rope where a four or six crosses it, a fielder camera as the fielder gathers or catches, and
  once the ball is dead a close-up of the bowler after a wicket or of the striker otherwise. The replay, review
  and scorecard shots come after these.
- A change of shot is a cut (`LastShot`, `bCutCamera`), except delivery to follow, which stays a smooth move.
- `ReplayDelay` is 2 seconds, so the close-up plays before the replay.
- To see them: `Scripts/capture.sh 1` (a four) and `Scripts/capture.sh 4` (a diving catch, then a wicket
  close-up).

## Blocked on the owner

- A motion-capture performer, voice commentary, and licences for teams, players and music.
- A phone plus the Android SDK or an iOS signing identity for the mobile build.
- Human playtesting and visual sign-off.

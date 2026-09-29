# CRICKET26 Mobile-First 10/10 Plan — Agent Handoff

Read this + `Docs/MASTER_PLAN.md` + `Docs/HANDOFF.md` before any work. This doc is the order of work for mobile. MASTER_PLAN is desktop/parity background. HANDOFF owner rules still apply.

## 1. Goal

Best touch cricket game. Super Over only until 10/10.

10/10 = 60fps on mid phones, fun in 10 seconds, broadcast look on a 6" screen. Original fictional teams/kits/audio only. No IPL, real likeness, Cricket 24 assets, broadcast graphics, commentary, music.

A phase is done only when: full test suite green + on-device profile/capture + owner eye sign-off. Tests prove it works. Owner eye proves premium.

## 2. Where we stand (2026-09-27)

- Overall 4.0/10 vs Cricket 24 gameplay. Simulation 7/10, shippable mobile 1.5/10.
- 88 tests pass on desktop (`Scripts/run_tests.sh CRICKET26.`).
- ~3600 lines uncommitted across Broadcast/Controls/Commentary/Audio/HUD — stabilize first.
- Never packaged for mobile. Never run on a phone. Only `-FeatureLevelES31` desktop preview.
- MetaHumans Medium on M5 Mac Metal: 16.7ms frame (vsync), 12.5ms GPU avg — over phone budget.
- Touch is only input (`CricketControls.cpp`, `CricketTouch::Layout`). Mouse = finger on desktop.
- 18k instanced crowd, procedural bowl ~250k tris, 4 tiers Low/Med/High/Epic. Lumen High+ only.
- Six rate 25-29% (was 41%), target 18-22%. Batting 3 buttons, no footwork modifier. Bowling no visible marker.
- Active wardrobe is T-shirt + jeans — reads casual. Print soft at distance.
- Commentary directors correct (intensity/silence/P0-P4/90+ lines) but voiced 20/2838, rest caption-only.

## 3. Project basics for new agent

- Git root: `/Users/aagamjain/Desktop/CRICKET-26`, game at `CRICKET26/`. Git paths start with `CRICKET26/`.
- Engine: UE 5.8. Code: `Source/CRICKET26/Cricket/`, Frontend: `Source/CRICKET26/Frontend/`, Tests: `Source/CRICKET26/Cricket/Tests/`.
- Scripts:
  - `Scripts/build.sh` — build, `-Werror -Wshadow`, shadowed locals fail.
  - `Scripts/run_tests.sh [Filter]` — writes `Saved/TestRun.log`, count `grep -c "Result={Success}" Saved/TestRun.log`.
  - `Scripts/capture.sh N [args]` — AI vs AI game-view frames to `Saved/Screenshots/MacEditor/BallN_*.png`, log `Saved/Capture.log`. Never desktop `screencapture`.
  - `Scripts/profile.sh` — perf soak. `Scripts/ui_capture.sh` — UI shots. `Scripts/compare.sh` — side-by-side vs ref.
  - `Scripts/metahuman/make_players.sh`, `make_kit.sh` — rebuild `Content/MetaHumans` (never committed, fresh clone must run).
  - `Scripts/stadium/make_stadium.sh` — rebuild `Content/Stadium` (never committed).
  - `Scripts/anim/stroke_qa.sh <label> <view> [args]` — 0.25x stroke sheets to `Saved/AnimQA/`.
- Flags: `-CricketAutoPlay`, `-CricketQuitAfter=N`, `-CricketDifficulty=0..3`, `-CricketQuality=0..3`, `-CricketTouchScript`, `-CricketShotBall=N`, `-CricketDevCam=face/kit`, `-CricketVenue=0..2`, `-CricketAiLeaves`, `-CricketAiShot=Intent,Dir`, `-CricketLength=M`, `-CricketPace`, `-CricketLeftHanded`, `-CricketRunMargin=S`, `-CricketMannequin`, `-FeatureLevelES31`.
- Debug keys: F1 overlay, F2 hand, F3 bowler, F4 trajectory, F5 wicket, F6 difficulty, F7 quality, F8 AI vs AI.
- After code changes run `graphify update .`.

## 4. Owner rules (do not break)

- Do not restart project or rewrite history. No hard reset, blind delete, force checkout.
- Do not fake tests, playtesting, comparisons. Cricket 24/26 cells stay UNVERIFIED without first-hand capture.
- No unlicensed content. Check licence/commercial/skeleton/mobile cost before import. No `~/Downloads/Cricket26_BasePlayer.fbx`. DeepMotion free tier + friend takes NOT shippable. Mixamo royalty-free ok. Sketchfab helmet needs credit + check.
- Commit only `Source/ Docs/ Config/ Scripts/ CRICKET26.uproject`. Never `Content/ Saved/ .serena/ graphify-out/`.
- End commit message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Do not start auction/franchise mode.
- Keep working item to item. Stop only for owner-supplied items below.

## 5. Mobile budgets (everything bows to this)

| Budget | Target |
|---|---|
| Frame | 60fps Medium target, 30fps Low fallback, p99 <20ms / <34ms |
| Game thread | <4ms, zero allocs per frame after warmup |
| Draw calls | <120 |
| Tris in view | <300k, players <25k each on phone |
| Memory PSS | <1.2GB Med, <800MB Low |
| Install | <450MB AAB/IPA initial |
| Battery | 12-min session <8%, auto High->Med at 45C |
| Touch latency | tap->swing <100ms, buttons on touch-down |
| Touch size | >=48dp, thumbs bottom 40%, avoid top 8% / bottom 12% notch |

Tiers (`Config/DefaultEngine.ini`, `CricketStadium.cpp`):
- Low: 30fps, 35% crowd, no shadows, TAA, block crowd fallback ok
- Medium: 60fps phone default, 50% crowd, 1x1024 dir shadow, TAA
- High: flagship only, 70% crowd, 2048 shadows
- Epic: desktop preview only, Lumen/TSR/VSM, never ship to phones

Mobile OFF: Lumen GI/reflections, VSM, volumetric cloud, cloth sim, strand hair, motion blur.
Mobile ON: baked sun+skylight, simple bloom/AO, instanced crowd vertex shader (`Fill`+`Excite`).

## 6. Owner-supplied blockers

- Android SDK+NDK or iOS platform + signing identity + test phones.
- Target lock: iPhone 12+/A14+ 4GB, Snapdragon 865/7 Gen 1+ 4-6GB, Android 11+, Vulkan ES3.1. One low-end 680/SE for Low.
- Landscape only (16:9 + 19.5:9). No portrait.
- Mocap performer + route: (a) studio day best, (b) Move.ai/Rokoko Vision cheap, (c) packs lack cricket. ~300 clips needed for 10/10 animation.
- Budget: Fab parts, Marvelous Designer, voice actors + music.
- Playtest + visual sign-off after every visual milestone.

## 7. Order of work

### M0 — Stabilize (1 day)
1. Fix `Frontend/Widgets/FrontendRoot.h` missing + `MatchHUDWidget.cpp` vs `SuperOverGameMode.cpp` unity clash.
2. `Scripts/build.sh`, `Scripts/run_tests.sh CRICKET26.` (must be 88/88), `Scripts/capture.sh 1`.
3. Commit per section 4. `graphify update .`.
DoD: clean build + green + 1 capture.

### M1 — Packageable (3-5 days, blocked on owner toolchain)
1. Install SDK/NDK or iOS platform + signing.
2. `Config/DefaultGame.ini` `DirectoriesToAlwaysCook`: `/Game/MetaHumans`, `/Game/Stadium`, `/Game/UI`.
3. Add `Scripts/package_android.sh`, `Scripts/package_ios.sh` + PSO cache + chunked MetaHumans.
DoD: install on phone, boot Entry map, AI Super Over 12 balls, 0 ensures.

### M2 — Perf rescue (1-2 weeks)
Files: `SuperOverGameMode.cpp`, `CricketStadium.cpp`, `Scripts/metahuman/kit_ue.py`, `make_kit.py`
1. LODs: striker+bowler LOD0 only, other 13 LOD2. Garments LOD3. Gear 1 LOD.
2. Hair cards only on phones. Keep hide-under-helmet/hat.
3. Crowd 18k→8k Low/12k Med, flags 680→300.
4. Stadium <75 meshes, HLOD bowl. Floodlight shadows: 0 Low, 1 Med, 2 High.
5. Cloth sim off on phones, keep `M_Kit` shade.
DoD: `profile.sh` on device 150 balls, game <4ms, p99 <20ms Med, no throttle 12 min.

### M3 — Touch 10/10 (1 week)
Files: `CricketControls.cpp/h`, `SuperOverHUD.cpp`, `MatchHUDWidget.cpp`
1. Batting bottom-right DEFEND/GROUND/LOFT+RUN/CANCEL, left 60% drag aim, tap=defence, release=timing.
2. Widen phone timing +15% vs desktop. Buttons touch-down.
3. Bowling: add visible landing marker (missing), then list+drag+PACE/SWING+BOWL→RELEASE.
4. `USafeZone`, 48dp min, haptics tick/thud/ramp.
DoD: `-CricketTouchScript` full match + 5 humans finish no tutorial, mis-tap <5%.

### M4 — Batting feel 4→9 (1 week)
File: `BattingModel.cpp/h`, `CricketAI.cpp`
1. Mistime slides contact toe/splice + loses pace. Retune six 25-29%→18-22%, dots >12%.
2. Re-measure `ShotKnowledge` (6000/kind). Show EARLY/GOOD/PERFECT/LATE from `R.Contact.TimingError` every ball.
DoD: 40-game soak avg 13-16, band tests pass.

### M5 — Bowling + fielding (4-5 days)
Files: `DeliveryResolver.cpp`, `BallSimulation.cpp`, `FieldingModel.cpp`
1. Over/around + crease + rhythm meter→Accuracy, seam in hand.
2. Keep auto fielding default. Add presets + 2D drag editor. AI captain adjusts.
DoD: human can hit yorker/wide/slower/bouncer on demand.

### M6 — Pro kit (1-2 weeks)
Scripts: `make_kit.sh`, `make_bat.py`, `outfit_ue.py`
1. Replace jeans-read with collared shirt/trousers/spikes. Keep paint + sponsor/name/number print (1024 striker, 512 fielders, `UpdateResourceImmediate(false)` for mips).
2. Gear LODs match body, remove temp LOD0-force with proper LODs.
DoD: `capture.sh 1 -CricketDevCam=face/kit` day+night + ES3.1, owner sign-off.

### M7 — Animation (2-4 weeks, blocked on performer)
1. Wire golden drive from `Scripts/anim/author_stroke.py`: bat follows top-hand, unreachable=miss. Then other 11 shots.
2. Locomotion via Epic Game Animation Sample + `PoseSearch`. `MotionWarping` for feet/release/intercept.
3. Faces: blink + track ball. iPhone Animator optional for cut-scenes.
DoD: foot slide <2cm/s, bat error 0, `stroke_qa.sh` 0.25x sheets pass. Without 300 mocap clips ceiling is 7/10.

### M8 — Stadium + pitch phones (1 week)
Files: `CricketStadium.cpp`, `BallSimulation.cpp`
1. Keep 3 venues + wear render target feeding physics (beats others).
2. Grass+mow+30yd, big screens static Low / live render High only.
3. No Nanite on phones, HLOD bowl.
DoD: day/overcast/night device captures + perf green.

### M9 — Broadcast + UI small screen (1 week)
Files: `CricketBroadcast.cpp`, `CricketReplayBuffer.*`, `MatchHUDWidget.cpp`
1. Keep stand cam 51.7m/8.1m lens curve + reverse 70m/14m. Widen phones 40→48m.
2. Replays max 2 angles phones, 0.7MB buffer keep, highlight reel keep Enter-skips.
3. Finish Canvas→UMG/CommonUI. Fonts 1.3x condensed. Keep wagon/pitchmap/speed/radar/this-over. Loading <3s.
DoD: `ui_capture.sh` 720p/900p/1080p + notch device shots.

### M10 — Audio speaker (3-4 days, voices blocked on owner)
Files: `CricketAudioDirector.*`, `CricketCommentaryDirector.*`, `CricketAudio.*`, `CricketCommentary.*`
1. Keep directors. Phone mix -16 LUFS, commentary top, crowd -6dB. 22kHz mono ~234KB keep, crowd 2s chunks.
2. Record 8-eval set per `Scripts/audio/VOICE_PIPELINE.md`, then full set. Caption-only until then.
DoD: `BallN.wav` A/B speaker+headphones.

### M11 — Ship
1. Keep `UFrontendSettingsSave`. Cloud save.
2. Crash/ANR, thermal auto-drop, <450MB, <1.2GB PSS.
3. Licence audit, rating, EULA, store pages.
DoD: soaks every build + external playtest + owner premium sign-off side-by-side vs `~/Downloads/CRICKET26.mp4` (observe only, never copy).

## 8. Status table (agent: update each commit)

| Milestone | Status | Proof |
|---|---|---|
| M0 stabilize | IN PROGRESS 2026-09-29: Touch.Controls guard expectation fixed, Auction untracked breakage blocks build | build blocked by AuctionRoom/AuctionCalls unity errors; Cricket files green in isolation |
| M1 package | DONE config, BLOCKED SDK/signing | cook + package_mobile.sh, SDK fail-fast verified |
| M2 perf | DONE guards, device profile BLOCKED | VSM off Low, strands off, LOD/crowd/shadow tiers verified in code |
| M3 touch | DONE + 2026-09-29: phone timing +15%, reticle 24→30 | Cancel 0.15, min-size asserts, Touch.Controls guard fix; timing Perfect 0.017/Good 0.046/EarlyLate 0.092 |
| M4 batting | VERIFIED no change (retune reverted 2026-09-29: broke ContactQuality smoothness + scenario H without playtest) | soak 15.4 runs, 24.6% sixes within bands; retune needs human playtest first per FINAL_REPORT |
| M5 bowling/field | VERIFIED no change | TargetMarker Waiting/RunUp exists, radar entry exists |
| M6 kit | VERIFIED no change | print 1024/512 exists; Blender rebuild + device captures BLOCKED |
| M7 anim | BLOCKED performer | stroke QA sheets |
| M8 stadium | TODO | venue captures |
| M9 broadcast/UI | IN PROGRESS 2026-09-29: replay wipe fullscreen→lower-third, debug gated out of shipping | UI shots 3 res; was: opaque fullscreen wipe + debug rect in captures |
| M10 audio | BLOCKED voices | BallN.wav A/B |
| M11 ship | TODO | store build + audit |

## 9. How to continue

1. Pick next TODO above, smallest slice that allows a capture.
2. Edit, build, test, capture/profile on device if visual/perf, `graphify update .`, commit per section 4.
3. Update this table + relevant `Docs/*_QA.md` + `CRICKET26_REFERENCE.md` (Cricket 26 cells stay UNVERIFIED without first-hand capture).
4. Ask owner only for section 6 items.

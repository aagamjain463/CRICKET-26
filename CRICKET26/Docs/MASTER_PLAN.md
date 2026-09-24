# CRICKET26 master plan: Cricket 24 quality, then beyond it

Written 2026-09-24. This plan replaces `Docs/CRICKET26_PARITY_PLAN.md` as the order of work. Every item from that
plan is folded in below. Read `Docs/HANDOFF.md` for the project basics and the owner's rules; they still apply.

## 1. Where the game stands

The reference is `~/Downloads/CRICKET26.mp4`: 26 minutes of Cricket 24 (India v Australia). It is used only to see
what a shipped cricket game presents, never as a source of content.

The rating against that footage, from an in-game capture on 2026-09-24, is **2.5 / 10**.

| Area | Score | Main gap |
|---|---|---|
| Ball physics and rules | 7 | Already strong. It is tested and deterministic. |
| Fielding and running AI | 6 | Strong logic, but there is no animation to show it. |
| Batting gameplay | 3.5 | There are only three buttons, no footwork control and no timing feedback. 41% of balls go for six. |
| Bowling gameplay | 4 | The target is not visible and there is no crease or wicket choice. |
| Players and animation | 1.5 | MetaHumans in the default T-shirt and shorts, barefoot, with no pads, helmet or gloves. Movement is IK poses only. |
| Stadium, lighting and crowd | 1.5 | Faces are backlit to black, the grass is flat stripes and the crowd is boxes. |
| Cameras and presentation | 2 | One delivery camera, one replay angle and no cut-scenes. |
| HUD | 4 | The layout is right, but the names are placeholders. There is no radar, pitch map or ball tracking. |
| Audio | 1 | Synthesised placeholder sound and text-only commentary. |
| Modes and front end | 1 | Super Over only, with no menus. |
| Shippable build | 0 | No build has ever been packaged. |

## 2. What "better than Cricket 24" means here

We cannot outspend Big Ant on content volume. We win where an engine and a simulation beat hand-authored content:

- **Lighting and rendering.** Lumen global illumination, Nanite stadiums and MetaHuman skin. This is a visible step
  above Cricket 24's flat look.
- **A true simulation.** The ball physics, fielding and AI already go deeper than an arcade model. Animation must
  follow the simulation, never the other way round, so nothing snaps or teleports.
- **A living pitch.** Footmarks, cracks and wear build up during the match and change the spin and bounce.
- **Kit that ages.** Grass stains and dust on the kit build up over an innings.
- **Honest ball tracking.** Hawk-Eye, the LBW projection and the edge detector are drawn from the real simulated
  path, not faked afterwards.

A phase is "at Cricket 24 level" only when the owner has compared it side by side with the reference footage and
agreed. Tests and captures prove that something works. Only the owner's eye can judge whether it looks premium.

## 3. Decisions the owner must make first

1. **Platform.** Cricket 24 quality needs a PC or console-class GPU. The recommendation is Windows and Mac first,
   targeting 60 fps at 1440p on an RTX 3070 or Apple M-series Pro class machine. A mobile version would come later
   as a scaled port. The current project was built mobile-first, so this choice changes the budgets.
2. **Teams.** Choose between an original fictional league (our own names, badges and kits, with no licence needed)
   and licensed teams (a licence from a board is needed). The plan assumes the fictional league until licences exist.
3. **Motion capture route.** Choose one of:
   - (a) a mocap studio day with a club cricketer (best quality);
   - (b) markerless capture from several phones with Move.ai or Rokoko Vision (cheap, good enough for a first pass);
   - (c) purchased packs (few cricket-specific packs exist).
4. **Asset budget.**
   - Fab purchases for stadium parts and gear.
   - Optionally a Marvelous Designer licence for the kit.
   - Voice actors and music.
5. **Test hardware.** A Windows PC with a mid-range GPU if Windows is a target.

## 4. Engine features this plan relies on

All of these are verified present in the local UE 5.8 install:

| Feature | Plugin |
|---|---|
| MetaHuman characters | `MetaHumanCharacter` |
| MetaHuman crowd | `MetaHumanCrowd` |
| Motion matching | `PoseSearch` |
| Motion warping | `MotionWarping` |
| Cloth outfits that fit every MetaHuman body | `ChaosOutfitAsset` |
| Vertex-animated crowds | `AnimToTexture` |

## 5. Phases

Each step ends with the full test suite green and an in-game capture where the change is visual. It is then
committed following the rules in `HANDOFF.md`. Steps are kept small enough that one capture can judge them.

### Phase 0: Foundations

0.1 **Finish plan item 7 (MetaHumans).**
   - Trace and fix the 39–45 cm hand-IK miss.
   - Add `/Game/MetaHumans` to the cook.
   - Run a perf soak with and without `-CricketMannequin`.
   - Commit the wiring.

0.2 **Real levels instead of a code-spawned scene.**
   - Today `BuildScene` spawns the ground, stadium, lights and crowd from code, so no art can be placed in the editor.
   - Move all static content into a level: `/Game/Maps/Stadium_01`.
   - The game mode keeps spawning only dynamic actors: players, ball, cameras.
   - The procedural stadium stays as a fallback for tests.

0.3 **Rendering baseline.**
   - Turn on Lumen GI and reflections, Virtual Shadow Maps, Nanite for static meshes, TSR, and auto exposure with a
     broadcast range.
   - Redefine the four quality tiers.
   - Add a PSO cache so "Preparing Shaders" never shows in play.

0.4 **UI framework.** Move the HUD from Canvas drawing to UMG with CommonUI. Everything in Phase 6 and Phase 8
   depends on this.

0.5 **Capture tooling.**
   - Named camera shots for review (`-CricketShot=FaceStriker`, `KitFull`, `Broadcast`).
   - A side-by-side tool that puts a game frame next to a reference frame.

### Phase 1: Lighting (the fastest visible gain)

1.1 **Fix the backlit players.**
   - Root cause, found 2026-09-24: the sun (`FRotator(-42, 35, 0)` in `BuildScene`) shines from the striker's end
     toward the bowler.
   - The delivery camera looks back from the bowler's end, so every player shows it their unlit side.
   - Fix: move the sun to the bowler's end, off to one side.
   - Check it with the close-up capture:
     `Scripts/capture.sh 1 -CricketDevCam=4.5,-1.5,1.5,0.6,0,1.2,22 -CricketQuality=3`

1.2 **Physically based light.**
   - Real sun and sky intensities with auto exposure.
   - Remove the fixed `ExposureBias = -1.5` placeholder hack.
   - Add skylight fill so shadows keep detail.

1.3 **Broadcast colour grade.** A LUT with a clean, bright and slightly warm look like a TV feed, TSR sharpening and
   light bloom.

1.4 **Match lighting presets:** day, late afternoon, dusk, and night under floodlights. The floodlights are real spot
   lights with IES profiles and soft multiple shadows.

1.5 **Close-up light rig.** A subtle key and rim light that turns on only for close-up and cut-scene cameras, as TV
   production does.

### Phase 2: Premium players

2.1 **Squad faces and bodies.**
   - Author each player in MetaHuman Creator rather than using raw presets: height, build, skin tone, hair, beard.
   - All are fictional; none resembles a real player.
   - Build hero quality (the High pipeline) for close-ups, with Optimized LODs for field distance.

2.2 **Cricket kit.**
   - Pieces: a collared playing shirt, long trousers, spiked shoes, and caps or sunhats for fielders.
   - Model them in Marvelous Designer or Blender, then import them as ChaosOutfitAsset outfits so they resize to every
     body.
   - Team colours come from material parameters.
   - Names and numbers on the back come from a runtime text or number atlas.

2.3 **Protective gear.**
   - Batting pads, batting gloves, a helmet with a grille, and keeper pads and gloves.
   - Umpire shirt, trousers, hat and shoes.
   - Rigid or skinned meshes on the MetaHuman skeleton. Original designs only, modelled in Blender or bought on Fab
     with a commercial licence.

2.4 **Bat.** A detailed original bat: grain, edges, grip, and our own fictional brand stickers.

2.5 **Hair under helmets and caps.** Swap to a compressed groom when headwear is on. Use grooms close up and hair
   cards at distance.

2.6 **Kit wear.** Grass stains and pitch dust build up on the knees and elbows after dives and runs, through a
   material parameter driven by the simulation's events. Cricket 24 does not do this.

2.7 **Performance.**
   - 15 players on screen.
   - Set a budget for the LOD bias, groom LOD, and cloth simulation (cloth only near the camera).
   - Measure with `Scripts/profile.sh` on each tier.

Checks:
- Close-up captures of every player in full kit.
- The existing hand-to-bat and ankle-height checks still pass.
- Perf soak within budget.
- The owner signs off on the looks.

### Phase 3: Animation (the largest gap)

3.1 **Locomotion with motion matching.**
   - Retarget Epic's free Game Animation Sample clips to the MetaHuman skeleton: walk, jog, sprint, starts, stops,
     turns and strafes.
   - Drive fielders, runners and the walk-back with `PoseSearch`.
   - Replace `IdleAnim`, `JogAnim` and the procedural running.

3.2 **Cricket mocap library.** The shot list for the capture session:
   - **Batting.** Stances, trigger movements, and every shot (13 types) across front foot, back foot and down the
     track, each in several directions and heights. Also leaves, defences, beaten or missed balls, edges, hits on the
     pads and body, and running with the bat, including a dive to make the crease.
   - **Bowling.**
     - Run-ups and actions: at least three pace styles, plus off-spin and leg-spin.
     - Follow-throughs, appeals and reactions.
   - **Fielding.**
     - Pick-ups and dives both ways, low and high.
     - Slide stops, and catches: high, low, diving and at the rope.
     - Throws: overarm, underarm and flat.
   - **Wicketkeeping.** Keeper takes, dives and stumpings.
   - **Umpiring.** The signals: out, four, six, wide, no-ball, bye, leg bye, free hit and review.
   - **Match moments.** Celebrations, huddles, walk-ins and walk-offs, and idle fidgets between balls.
   - That is roughly 300 clips. The owner supplies the performer and the capture route (decision 3).

3.3 **Retarget pipeline.**
   - An IK Retargeter asset from the capture skeleton to MetaHuman.
   - Clean-up in Cascadeur or Blender.
   - A naming convention (`A_Bat_<Shot>_<Foot>_<Dir>_<Var>`) and a scripted import.

3.4 **Shot animation driven by the simulation.**
   - Pick a clip from the shot, footwork, length, line and direction.
   - Time-scale the clip so its contact frame lands exactly on the simulation's `ContactTime`.
   - Use `MotionWarping` to plant the front foot at the ball's line.
   - Keep the existing `CricketPose` IK as a final correction layer, so the bat meets the ball where the simulation
     says it does.

3.5 **Bowling driven by the simulation.** Warp the run-up stride length so the delivery stride and release frame hit
   the simulation's release point and time.

3.6 **Fielding driven by the simulation.**
   - The fielding model already chooses the action and the intercept point.
   - The animation picks the matching clip and warps it to that point and time.
   - Hand IK takes the ball, with no more tipping actors for dives.

3.7 **Faces.** Blinking, eyes tracking the ball, and expressions for appeals and reactions through the MetaHuman face
   rig. Optionally, MetaHuman Animator performances captured on the owner's iPhone for cut-scenes.

3.8 **Secondary motion.** Cloth simulation on shirts near the camera, and follow-through on pad straps and helmet
   flaps.

Checks:
- Automated tests for bat-to-contact error and foot sliding (foot speed while planted under 2 cm/s).
- A coverage test proving every simulation outcome has a clip.
- Capture review.

### Phase 4: Batting and bowling feel

4.1 **Fix the six rate at its root cause.**
   - Today a 40 ms mistime keeps 84% of bat speed, and the contact point does not move along the blade. That is why
     41% of balls go for six.
   - Target the six, four, dot and wicket shares of real T20 cricket.
   - Re-measure the AI's stroke table.
   - Tests reproduce the old six-heavy result and hold the new band.

4.2 **Gamepad-first controls through Enhanced Input.**
   - The left stick sets the shot direction.
   - A modifier sets footwork: front, back, charge or step across.
   - Face buttons play defend, ground, loft and power.
   - Held combinations play premeditated scoops, ramps and reverse sweeps.
   - Keyboard and touch map onto the same intent layer that `CricketControls` already uses.

4.3 **Timing feedback.**
   - After every ball, show EARLY, GOOD, PERFECT or LATE with the shot name and the contact quality.
   - The data already exists in `R.Contact.TimingError`.
   - Add a toggle for purists.

4.4 **Nets mode** for practice, with a replay of the last ball.

4.5 **Bowling.**
   - A visible landing marker moved with the stick.
   - A choice of over or around the wicket, and of crease position.
   - A rhythm-based run-up with a release meter that sets accuracy.
   - Delivery variations from each bowler's attributes.
   - The ball shown in the hand with its seam position.

4.6 **Fielding control.** Auto by default. Optional manual mode with catch timing, and throw end and power.

4.7 **Field setting.** Presets plus a drag-and-drop editor on a 2D field map. The AI captain changes the field with
   the match situation.

4.8 **Playtest loop.** The owner plays, telemetry logs every ball, and the timing windows are tuned from the data.
   Nothing in Phase 4 counts as done until a human has played it.

### Phase 5: Stadium and world

5.1 **Original Nanite stadium.**
   - A modular kit: tiered stands, a roof, floodlight towers, a media box, sightscreens, dugouts, LED boundary boards,
     a big screen, and a boundary rope with a cushion.
   - Built in Blender, or from Fab parts with a commercial licence.

5.2 **Outfield.**
   - A grass material with micro detail and real mowing patterns.
   - Worn areas near the pitch.
   - A painted 30-yard circle and fictional painted logos.

5.3 **Pitch.**
   - A layered material with dry, green and dusty variants.
   - Cracks, plus footmarks and ball marks that build up where the simulation says feet landed and balls pitched,
     through a runtime render target.
   - Wear feeds back into the simulation's spin and bounce. Cricket 24 does not do this.

5.4 **Crowd.**
   - `AnimToTexture` instanced crowds for 30,000+ spectators, who sit, stand, cheer and wave flags in team colours.
   - `MetaHumanCrowd` people for the rows near the camera.
   - The crowd reacts to the simulation's events.

5.5 **Screens.** The big screen and the LED boards show live replays and scores through render targets.

5.6 **Variety.**
   - Three or more venues with different pitch characters.
   - Weather: cloud cover helps swing.
   - Dew at night.

5.7 **Budgets.** HLOD and Nanite budgets; a perf soak on every tier.

### Phase 6: Broadcast presentation

6.1 **Camera director.**
   - A lens set:
     - main delivery (lower and wider than now);
     - bowler run-up, end-on and side-on;
     - square leg, fielder tracking, boundary and a spider-cam;
     - close-ups.
   - Rule-based cuts on the simulation's events.

6.2 **Replays.**
   - Multi-angle replays from the deterministic simulation.
   - Slow motion, and super slow motion on edges and close calls.
   - A highlight reel at the innings break and the end of the match.

6.3 **Cut-scenes.**
   - The toss.
   - The walk-out with a player card.
   - The wicket celebration huddle and the batter's walk-off.
   - Fifty and hundred milestones.
   - The end-of-match presentation.

6.4 **Broadcast graphics (UMG, original design).**
   - Score bug and player cards.
   - Partnership, run rate and required rate.
   - Run-rate worm and Manhattan chart.
   - Wagon wheel and pitch map.
   - Speed gun, field-radar mini map, "this over" and session stats.

6.5 **Ball tracking and reviews (DRS).**
   - A Hawk-Eye trail and the LBW projection with its three calls, taken from the simulation. Test it against the
     existing LBW decision.
   - An edge detector driven by the contact events.
   - Player reviews with a per-innings limit and umpire's call.

6.6 **Third umpire.** Run-out and stumping reviews with frozen multi-angle frames.

### Phase 7: Audio

7.1 **MetaSounds with recorded foley.**
   - Bat on ball (middle, edge, thick edge, toe).
   - Ball on pad, ball on pitch, stumps, and ball into gloves.
   - Spikes and footsteps, and the rope.
   - The synthesised cues stay as a fallback.

7.2 **Crowd.** Layered stems (murmur, build-up, roar, groan, chants, horns) mixed live from the match state, and
   spatialised by stand.

7.3 **Commentary.**
   - A two-voice system with a context engine: situation, stats, milestones and history.
   - Several thousand recorded lines.
   - Names and numbers stitched in.
   - Built on `CricketCommentary`.
   - The owner supplies the voice actors, or synthetic voices with commercial rights.

7.4 **Music.** Stadium PA and menu music, original or licensed.

7.5 **Mix.** Ducking under commentary and a loudness target of about -16 LUFS integrated.

### Phase 8: Formats, modes and front end

8.1 **Rules.**
   - T20, ODI and Test, generalised from the Super Over rules: overs, powerplays, fielding restrictions and bowler
     limits.
   - Test cricket adds declarations, the follow-on, the new ball, sessions, bad light and several days.
   - Rain rules follow a published method, re-implemented from the published description.

8.2 **Modes.**
   - Quick match, series and tournament.
   - Nets.
   - Career: create a player from preset blends, since MetaHuman Creator does not run in a shipped game.
   - Online play last. The auction and franchise modes stay off until the owner asks for them.

8.3 **Front end (CommonUI).**
   - Team and line-up selection, and the toss.
   - Settings: graphics, control remapping, audio, difficulty and accessibility.
   - Save, load and resuming a match.

8.4 **Player database.** Ratings, a ratings editor, and career statistics.

### Phase 9: AI realism (where we beat Cricket 24)

9.1 The batting AI paces an innings for each format and situation.

9.2 The bowling AI plans each over. The captaincy AI changes bowlers and fields.

9.3 **Fielders.** Positions and reactions based on awareness.

9.4 **Realism soaks.**
   - Thousands of AI v AI overs for each format.
   - Compare against real statistics: runs per over by phase, the mix of dismissals, boundary share and extras.
   - These become tests.

### Phase 10: Ship

10.1 **Packaging.** Packaged Windows and Mac builds, with a PSO cache, load times measured and crash reporting.

10.2 **Performance, memory and thermals** on the target hardware from decision 5.

10.3 **QA.**
   - The automated suite and soaks on every build.
   - External playtests.

10.4 **Store and legal.**
   - Licence audit of every asset.
   - Age rating, EULA and store pages.
   - Consoles need publisher or dev-kit access.

## 6. Order of work

| Milestone | Steps | Blocked on the owner? | Status |
|---|---|---|---|
| M1 | 1.1, 1.2, 1.3 lighting | No | 1.1 and 1.2 done (`3a610bd`); 1.3 waits for a reference frame to grade against |
| M2 | 0.1 finish MetaHumans | No | Done (hand-IK root cause fixed; cooking not yet tried) |
| M3 | 4.1 six rate, 4.3 timing feedback | No (playtest later) | 4.3 done (timing bar, F9). 4.1: the root cause (full pace kept on a mistime, fixed contact point) was fixed in `1157fad`. Sixes fell from 41% to 25-29% of balls. The final band waits for playtest data |
| M4 | 0.2, 0.3, 0.4, 0.5 levels, rendering, UMG, capture tools | No | 0.5 done (`29bf74e`: `-CricketDevCam=face`/`kit`, `Scripts/compare.sh`). 0.3 mostly done. Tiers: Low/Medium use sky light, SSR and TAA; High adds Lumen GI and reflections; Epic adds TSR. VSM and Nanite are on at every tier, with light bloom and AO (no measurable cost: 6.5 ms GPU at Medium). Exposure stays fixed at EV15 for day play; auto exposure comes with the night preset (1.4). The PSO cache needs a packaged build. 0.2 and 0.4 not started |
| M5 | 2.1 to 2.7 kit, gear, bat | Asset budget or Marvelous Designer | 2.2 first pass done: `Scripts/metahuman/make_kit.sh` cuts a collared shirt, trousers and shoes from each player's own full body in Blender. The kit keeps the body's skin weights, and the game paints it in team colours. 2.3 batting gear done: pads, gloves and a helmet with a grille, skinned to each batter's body by the same script. Still to do: names and numbers, keeper gear, umpire kit, bat (2.4) |
| M6 | 3.1 locomotion by motion matching | No (free Epic sample) | |
| M7 | 3.2 to 3.8 cricket mocap and simulation-driven animation | Performer and capture route | |
| M8 | 6.1 to 6.6 camera director, replays, graphics, DRS | No | |
| M9 | 5.1 to 5.7 stadium, pitch, crowd | Asset budget | |
| M10 | 4.2, 4.4 to 4.8 controls, bowling, field setting | Playtests | |
| M11 | 7.1 to 7.5 audio | Voice actors, music | |
| M12 | 8.1 to 8.4 formats and front end | No | |
| M13 | 9.1 to 9.4 AI realism | No | |
| M14 | 10.1 to 10.4 ship | Hardware, stores | |

Work that does not wait on the owner goes first, so assets can be gathered in parallel. Where a milestone is
blocked, the work moves on to the next one and comes back.

## 7. What the owner supplies

- The five decisions in section 3.
- The mocap performer and session, and optionally iPhone face captures.
- Budget for Fab assets (stadium parts, gear), a Marvelous Designer licence, voice actors and music.
- Playtesting after M3, M7 and M10, and a visual sign-off after every visual milestone.
- Target test hardware, and store or dev-kit accounts when shipping.

## 8. Honest scale

The engineering side (simulation, animation logic, cameras, UI, rules, AI, tools) can be done here. The long pole
is content: about 300 mocap clips, kit and gear, a stadium, recorded audio and commentary. Cricket 24 had a large
studio for this. The plan closes the gap by using engine systems (MetaHumans, motion matching and warping, Nanite,
Lumen, crowd plugins) instead of hand-authoring everything, and by winning on the simulation. Without the mocap and
art in section 7, the ceiling is about 5 out of 10, however good the code is.

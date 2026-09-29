# CRICKET26 review and the road to 10/10

Written 2026-09-29 from a read of the whole repository at `34a630b`: source, tests, configs, scripts, every doc and
every screenshot in `Docs/UIShots/`. The game was not built or played for this review. This machine has no Unreal
Engine, and `Content/` is not in git, so nothing here was seen running. Every number below comes from the project's own
reports, and each one is marked where it could not be checked.

The yardstick is **the best mobile cricket game on the store**. Cricket 24 on console is not the yardstick. The owner
should play the top mobile cricket games first-hand on the target phone and write down what they do well, before
calling any phase done. That keeps the "no faked comparisons" rule.

## 1. Rating: 3.5 / 10 as a mobile game you could ship today

As a technology prototype it is about a 7. The simulation and the engineering discipline are well above what an
indie cricket project usually has. The low overall score is because a player cannot get the game, the game has one
six-ball mode, and nobody has played it on a phone.

| Area | Score | Why |
|---|---|---|
| Ball physics, rules and umpiring | 8 | 240 Hz ball model with drag, Magnus, swing, seam and turn. The pitch wears and feeds back into the physics. The rules cover every dismissal, free hits, the bouncer limit and wides with runs. LBW comes from real ball tracking. Deterministic and tested. |
| Fielding, running and AI | 7 | Intercept solver, fielding roles, relay catches, run-out logic, a value-table batting AI re-measured by a test, and difficulty that changes decisions and not physics. |
| Broadcast systems | 6.5 | DRS with umpire's call, an edge detector drawn from contact events, a third umpire with frozen frames, two-angle replays, a highlight reel and a camera director. Few mobile games have all of this. |
| Batting and bowling feel | 4 | Never played by a human. Sixes are still 24.6% of balls against the plan's own 18-22% target. A retune was reverted because no playtest data exists. Bowling has no real rhythm or skill loop yet. |
| Touch controls | 5 | Well designed on paper: one intent layer, buttons act on touch-down, and a scripted touch match plays through. No finger has touched it on a phone. |
| Players and animation | 3 | MetaHuman faces are strong. The kit reads as a T-shirt and jeans with running shoes (see `UI_Premium_HUD.png`). Batting is a solved IK motion, and the only captured clips are 4 Mixamo moves. No mocap. |
| Stadium and world | 4.5 | Three generated venues with their own pitch and weather, a live pitch-marks target and an instanced crowd with flags. Low-poly bowl and low-poly fans. |
| Menus and HUD | 5.5 | The shell is attractive (`UI_Brand_Home.png`). But the in-match result screen uses a different look (`UI_After_Result.png`), three of five tabs show "coming soon", teams are HOME XI and AWAY XI, and players are called "Opener" and "Finisher". The wicket shot shows the same caption twice and crops the players at the chest. |
| Audio and commentary | 2.5 | The audio and commentary directors are good work: intensity, silence, priorities and anti-repetition. But every sound is synthesised, 20 of 2,838 lines are voiced, and nobody has listened critically. |
| Modes and depth | 2 | Super Over only. The IPL season and auction code exists but cannot ship as it is (see section 2). |
| Mobile readiness | 1 | Never packaged and never run on a phone. MetaHumans measured 12.5 ms GPU on an M5 Mac, which is over the phone budget. The 450 MB install target has not been checked against 10 MetaHumans with grooms. |
| Retention and online | 1 | No progression, no daily reason to come back, no multiplayer, no cloud save. |
| Shippable and legal | 1.5 | See section 2. |

## 2. Problems to fix before anything else

These are ordered by how much damage they do if left.

1. **Real IPL teams and real players are in the code.** The last commit added `Scripts/auction/Players.csv` and
   `Teams.csv` (400 real cricketers, including MS Dhoni, and the 10 real franchises with owners and colours).
   `Auction/AuctionRoster.inl` and `AuctionData.cpp` compile them into the game. This breaks the owner's own rules in
   `HANDOFF.md` and `MOBILE_10_10_PLAN.md` ("No IPL, real likeness"), and it would get the app pulled from the stores.
   Either license it (BCCI for the IPL, and players' names through their association) or replace it with a fictional
   league generated from the same statistical shape. The auction and franchise modes also go against the rule
   "Do not start auction/franchise mode". The owner needs to decide on that rule again, in writing.
2. **Build status at HEAD is unknown.** `MOBILE_10_10_PLAN.md` says the Auction files broke the build before they were
   committed. Nobody has confirmed that `34a630b` builds and passes its 144 tests. There is no CI.
3. **Menu art provenance.** The player and stadium photos in the menus were made with Gemini (per
   `CRICKET26_DESIGN_SYSTEM.md`), and the stadium plate was cropped to remove the generator's watermark. Check the
   generator's terms for commercial use. The photo-real menu players also promise a look the match does not deliver
   yet, which store reviewers will call out. Longer term, render the menu art from the game's own MetaHumans.
4. **The menu advertises modes that do not exist.** Store, Live and Club show "coming soon". At launch, hide any
   tab without a mode behind it.
5. **Engineering debt that will slow every later phase.**
   - `SuperOverGameMode.cpp` is 4,538 lines and owns match flow, presentation, replays, reviews and scene building.
     Split it before T20 and ODI are added, or every new format multiplies the risk.
   - The UE template code (`Variant_Combat`, `Variant_Platforming`, `Variant_SideScrolling`, about 60 files) still
     compiles into the game. Delete it.
   - `README.md` is one line. The docs assume a Mac at `/Users/aagamjain/...`.

## 3. What 10/10 means, as numbers

A phase counts as done only when its row holds on a real phone, measured, plus the owner's own sign-off.

| Pillar | 10/10 target |
|---|---|
| Performance | 60 fps on a Snapdragon 7 Gen 1 or A14 phone at Medium. p99 frame under 20 ms. No thermal throttle in a 20-minute session. |
| Load and size | Cold start to the first ball in under 20 s. Install under 450 MB, with more venues downloaded on demand. |
| First session | A new player hits a boundary within 60 s of first launch, with no tutorial text wall. |
| Feel | 8 of 10 blind playtesters say batting timing feels fair. Mis-taps under 5%. Six rate inside the real-cricket band for each format. |
| Look | Side-by-side captures against the best mobile cricket games, judged by the owner and 5 outside players. Players in proper cricket kit, with mocap on every common action. |
| Sound | Voiced two-person commentary with names stitched in, recorded bat and crowd sound, passing a speaker and headphone listening test. |
| Depth | At least T20, ODI, a tournament, a career mode, nets and daily challenges. |
| Retention | Day-1 retention of 40% or more and day-7 of 15% or more in a soft launch. |
| Stability | 99.8% crash-free sessions, ANR rate under 0.3%. |
| Store | 4.5 stars or more in the soft-launch markets. |

## 4. The plan

Phases overlap. The order puts the things that decide the whole project (can it run on a phone, is it fun) before the
expensive content. Estimates assume one developer working with an AI agent, plus outside help for content where
marked.

### Phase A: Clean and honest base (1 week)

1. Confirm `34a630b` builds with `Scripts/build.sh` and that all 144 tests pass. Fix anything red first.
2. Decide what to do about the IPL data (section 2, item 1). Until a licence exists, move `Players.csv`,
   `Teams.csv` and the roster out of the shipping target, and generate a fictional 10-team league with made-up
   names, crests and kits, and statistics in the same range.
3. Give the two Super Over sides real fictional identities: team names, crests, and a name for every player.
   Replace "HOME XI", "Opener" and "Finisher" everywhere.
4. Delete the `Variant_*` template code and the template character, controller and game mode if unused.
5. Set up CI on a Mac runner (self-hosted, since UE needs it): build with `-Werror -Wshadow`, run the test suite and
   fail on any red. Nothing merges red.
6. Write a real `README.md`: what the game is, how to build it, how to rebuild `Content/` from the scripts.

Exit: green CI, no real names or brands in the shipping build, and the owner has decided on the auction and IPL rule.

### Phase B: Get it on a phone and measure (2-3 weeks, needs the owner's toolchain)

1. Install the Android SDK and NDK, or the UE iOS platform with a signing identity. Buy or borrow the three target
   phones: one mid-range Android, one iPhone 12-class, and one low-end phone for the Low tier.
2. Package with `Scripts/package_mobile.sh`, install it, and play a full AI Super Over.
3. Profile frame time, memory, install size, temperature and battery over a 20-minute session.
4. **The character decision.** This is the biggest technical risk. If 15 MetaHumans do not fit the budget with
   striker and bowler at LOD0 and the rest at LOD2, move the fielders, and on Low everyone, to a baked mobile
   character: one skinned mesh of about 15-25k triangles, baked textures and hair cards, sharing the MetaHuman
   skeleton so all animation still works. Keep full MetaHumans for close-ups and cut-scenes.
5. Build the PSO cache, so no shader hitches appear in play.

Exit: 60 fps at Medium on the mid-range phone, under 450 MB, and a baseline profile checked into `Docs/`.

### Phase C: Make it fun in 10 seconds (3-4 weeks, needs playtesters)

This is the phase that decides whether people keep the game.

1. **Playtest loop.** Log every ball to a file on the phone (input times, timing error, shot, outcome). Put the game
   in front of 5-10 people who have never seen it, every week, and watch without helping.
2. **First session.** Cold start goes straight into a guided nets over, with one prompt per ball: tap to defend,
   swipe to drive, hold to loft. The menu comes after the first boundary.
3. **Batting.** Retune the contact model with the playtest data, until sixes fall into the target band and a
   mistimed loft is a real risk. Show EARLY / GOOD / PERFECT / LATE and the shot name after every ball, with an
   option to turn it off. Add footwork (front foot, back foot, charge) as a swipe modifier. Add haptics for a middled
   shot, an edge and a wicket.
4. **Bowling.** A visible landing marker you drag, a timing meter on release that sets accuracy, over or around the
   wicket, and each bowler's variations from their attributes. Test: a human can bowl a yorker, a bouncer and a slower
   ball on purpose.
5. **Difficulty.** Easy should be winnable by a first-time player. Legend should take practice.
6. **Tutorial and practice.** A nets mode with a replay of the last ball, and short skill drills that also teach.

Exit: the "Feel" and "First session" rows of section 3 hold with real playtesters.

### Phase D: Formats and depth (4-6 weeks)

1. **Refactor first.** Split `SuperOverGameMode` into a match flow, a presentation layer, a replay and review system
   and a scene builder. Generalise `SuperOverMatch` into overs-based rules: T20, ODI and Super Over as settings.
   Powerplays, fielding restrictions and bowler over limits.
2. **Quick match** with team and line-up choice, the toss, and a match summary.
3. **Tournament** mode for the fictional league (the IPL season code can be reused on fictional data).
4. **Career** mode: create a player, rise from club to national side, and build attributes by playing.
5. **Challenges.** "Chase 18 off the last over" style scenarios generated from the simulation, with a new set every
   day.
6. **Save and resume** a match mid-innings. Cloud save.
7. **Field setting.** Presets plus a drag editor on the field radar. The AI captain changes the field for the
   situation.
8. **Short sessions.** A mobile player needs to finish a match in 5-15 minutes: T5 and T10 options, and a
   "key moments" mode that plays only the overs that matter.

Exit: all the formats pass AI v AI realism soaks against real scoring rates, and the owner has played each one.

### Phase E: Look like the best (6-10 weeks, needs a mocap route and a small asset budget)

1. **Kit.** A real collared cricket shirt, cricket trousers (not jeans), spiked shoes, caps and sunhats. Keep the
   runtime name, number and sponsor print. Coloured kits for white-ball games and whites for red-ball games.
2. **Mocap.** Pick a route: a studio day with a club cricketer (best), or markerless capture with several phones
   (cheapest). Capture in order of screen time:
   - the 12 batting shots each way, the leave and the defence;
   - pace and spin run-ups and actions;
   - fielding pick-ups, throws, dives and catches;
   - keeper takes;
   - celebrations, appeals, umpire signals and idles.
   About 80 clips cover most of a match. Aim for about 300 in the end.
3. **Motion matching** for running and fielding, and motion warping, so the clips land exactly on the simulation's
   contact, release and intercept. Keep the IK as a final correction, so the bat still meets the ball where the
   physics says.
4. **Stadium.** Real modelled stands with HLOD, a crowd that reacts in waves, a grass material with mowing stripes and
   wear, and live replays on the big screen on High only. Two more venues.
5. **Lighting.** Day, late afternoon, dusk and night presets, graded for a 6-inch screen.
6. **Cameras.** Fix the wicket close-up framing (faces in shot, not chests). Add the missing lenses: run-up side-on,
   square leg, spider-cam and fielder tracking.
7. **Menu art from the game.** Render the cover players from the in-game MetaHumans, so the menus show what the match
   looks like.

Exit: the "Look" row of section 3.

### Phase F: Sound like a broadcast (3-4 weeks, needs voice and music budget)

1. Record foley: bat on ball (middle, edge, toe), pads, gloves, stumps, spikes, the rope. Keep the synthesised cues as
   fallbacks only.
2. Crowd stems (murmur, build-up, roar, groan, chants, horns), mixed live by the existing audio director.
3. Voiced commentary for the existing director: two voices, a few thousand lines, player names stitched in. Use voice
   actors, or synthetic voices with a written commercial licence (see `Scripts/audio/VOICE_PIPELINE.md`). Include
   Hindi commentary; it is a big reason people pick one cricket game over another in India.
4. Menu music, original or licensed. Mix to about -16 LUFS for phone speakers.

Exit: the "Sound" row of section 3.

### Phase G: One consistent look for the UI (2 weeks, overlaps E)

1. Give the in-match overlays (pause, result, scorecard) the same navy-and-gold style as the menu shell.
2. Fix the duplicated caption in the wicket view.
3. Add the broadcast graphics still missing: the run-rate worm, the Manhattan chart, player walk-out cards and
   milestone cards.
4. Test every screen on a notched phone and on a tablet. Minimum touch target 48 dp, fonts readable at arm's length.
5. Accessibility: left-handed layout, colour-blind-safe ball tracking colours, subtitles for all commentary.

### Phase H: Reasons to come back (4-6 weeks)

1. **Progression.** Player XP, attribute growth in career mode, cosmetic unlocks (kits, bats, celebrations).
2. **Daily and weekly challenges** from the scenario generator in phase D.
3. **Online.** The simulation is deterministic, which makes fair online play much easier. Start with async
   head-to-head: both players play the same over, and the server re-runs it from the inputs to check the result.
   Then leaderboards. Real-time PvP comes last.
4. **Monetisation**, if any: cosmetics and a one-time premium unlock only. Nothing that sells runs, attributes or
   wins. Players rate pay-to-win cricket games badly, and a fair game is how you beat the store's leaders.

### Phase I: Ship (3-4 weeks)

1. Crash and ANR reporting, analytics for the retention numbers, and thermal auto-drop from High to Medium.
2. A licence audit of every asset: fonts, MetaHumans, Mixamo, the Sketchfab helmet (CC-BY credit), Gemini art, voices
   and music.
3. Store pages, age rating, privacy policy and EULA.
4. **Soft launch** in one or two smaller markets. Tune from real data until the "Retention", "Stability" and
   "Store" rows of section 3 hold. Only then do the global launch.

## 5. Order and blockers

| Order | Phase | Owner must supply | Approximate time |
|---|---|---|---|
| 1 | A: clean base | IPL and auction decision | 1 week |
| 2 | B: on a phone | SDK or iOS signing, 3 test phones | 2-3 weeks |
| 3 | C: fun in 10 seconds | 5-10 playtesters each week | 3-4 weeks |
| 4 | D: formats and depth | Nothing | 4-6 weeks |
| 5 | E: look | Mocap route, asset budget | 6-10 weeks |
| 5 | F: sound | Voices, music | 3-4 weeks, alongside E |
| 5 | G: UI | Nothing | 2 weeks, alongside E |
| 6 | H: retention and online | A server budget for online | 4-6 weeks |
| 7 | I: ship | Store accounts | 3-4 weeks and a soft launch |

That is about 6-9 months to a global launch. Without the owner-supplied items it stops around 6/10: phases A, C, D, G
and H can be done here, but the look and sound phases need real capture, recording and a phone.

## 6. Where this game can beat everyone

Most mobile cricket games use canned outcomes and pre-made animations. This project has a real simulation, and that
is the edge to lean on:

- **Honest ball tracking and DRS** drawn from the simulated path, not faked afterwards.
- **A pitch that changes** during the match and changes how the ball behaves.
- **AI that plays the situation**, measured against real cricket statistics by tests.
- **Fair online play**, because a deterministic simulation can be re-checked on a server.
- **Replays of anything**, because every ball can be re-simulated from its inputs.

Put these on the store page and in the first minute of play, once phases B and C prove the game runs and feels good.

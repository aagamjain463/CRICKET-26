# Presentation Director

The Presentation Director turns match moments into short broadcast scenes: who is on screen, which camera shots, what the players do, the lower-third graphic, and whether a replay follows. It never changes gameplay state. The match is fully decided (score, wickets, strike, milestones) before a scene is chosen, and a scene only moves bodies and the camera.

## Flow

Delivery settles → `ScoreDelivery` builds `FDeliveryInput` (commentary context, identities, runs before/after, edge/beaten, over complete, catcher) → `CricketPresentation::Direct` → `FScenePlan` → `UpdateScene` (movement), `PoseScene` (gestures, eye-lines), `SceneCamera` (shots), HUD lower-third → replay → next ball.

- `Source/CRICKET26/Cricket/CricketPresentation.h/.cpp`: pure logic (levels, variants, milestones, merging, participants, player of the match).
- `SuperOverGameMode.cpp`: `UpdateScene`, `PoseScene`, `SceneCamera`, `StartToss`, the lower-third text in `ScoreDelivery`.
- `MatchHUDWidget.cpp`: the lower-third card and the PLAYER OF THE MATCH row on the result scorecard.

## Importance levels (Balanced pacing)

| Level | Seconds | Examples |
|------:|--------:|----------|
| 0 | 0 | Routine play: dot, runs, edge, play-and-miss, four, six (no live scene; banner, commentary and replay only) |
| 1 | 1.4 | End-of-over talk |
| 2 | 2.4 | Maiden, expensive over |
| 3 | 3.6 | Wicket, dropped catch, toss |
| 4 | 5.0 | High-pressure wicket, last-ball build-up, innings break, any milestone |
| 5 | 7.5 | Match won, tie, result |

Scenes are for special moments only: nobody stops to celebrate a routine boundary.

Pacing: **Full** ×1.35, **Balanced** (default), **Quick** only level 3+ at half length, and no replay below level 4. Set with `-CricketPacing=Full|Quick`.

## Merging

One scene per ball. The most decisive fact picks the scene (win > tie > wicket > six > four > drop > edge > beaten > runs > dot). Routine balls (every level-0 type, boundaries included) then fold in what follows: last-ball build-up, innings break (with the target card), or the over summary. A milestone lifts the scene to level 4 and adds its own opening beat, so a match-winning century is one scene with the centurion first, then the team celebration. So a six that ends an over leads straight into the over talk, and a six that ends the innings plays the innings break. A six that brings up a fifty plays the fifty's scene.

## Milestones

Data-driven thresholds (50, 100, 150, 200; five wickets; hat-trick). Each fires exactly once per player per match, is never triggered by a wide, and a hat-trick streak survives wides and no-balls but not a legal ball without a wicket.

## Participants

- Wicket hero: bowler (bowled, LBW, hit wicket, caught and bowled), keeper (caught behind, stumped), the actual catcher, or the fielder who ran the batter out.
- The team converges from where each fielder stands, in a ring round the hero; the far ones jog and arrive later.
- Dismissed batter (striker, or the non-striker when run out at that end) walks toward the pavilion.
- Captain: the nearest non-keeper fielder to the bowler.
- Win: the batting pair celebrate, the bowling side is dejected, then the bowler walks over to shake the striker's hand.

## Anti-repetition

Each scene type has authored variants (3 for wickets, 2 for over and win scenes). A variant is not repeated until the others have played. The opening shot is recorded too.

## Skip and interruption safety

- Tap during a scene: the scene ends where everyone stands (no teleport) and the replay, if any, starts at once.
- Tap during a replay skips it; tap during the highlights reel skips the reel.
- The scorecard at the innings break and result waits for the last ball's scene to finish (`ScorecardFrom`).
- The toss plays in the wait before the first ball; the first delivery waits for it, then everyone returns to their marks.

## Crowd

While a scene plays, the crowd bed is held at `0.3 + 0.7 × Scene.Crowd` (Crowd = 0.2 × level), so the noise and the stand animation follow the scene's importance and then settle.

## Captured clips

`/Game/Anims/Mocap/Scene_<Action>` (Celebrate, Clap, Talk, WalkOff, Dejected, Handshake and so on) replaces the procedural gesture for that action when imported. Until then the procedural gestures are used: arms up, hands on hips, clapping, a handshake offered at waist height, looking round, checking the gloves.

## Tests

- `CRICKET26.PresentationDirector.*`: levels (routine balls including boundaries get no scene), milestones (exactly 50, 100 by a six, fire-once, hat-trick), merge (match-winning century, over scenes, last ball, innings break), wicket heroes per dismissal, 20-scene repetition, pacing, player of the match.
- `CRICKET26.Broadcast.*`: replay angles always cut in.
- `CRICKET26.Presentation.ScorecardAfterScene`: the scorecard never covers a scene.

Debug flags: `-CricketRunsBias=N` (presentation-only runs offset, to reach milestones in a Super Over), `-CricketForceWicket`, `-CricketPacing=`.

## Not yet done

- Captured celebration and handshake clips (Mixamo downloads need the user's approval).
- New-batter walk-in after a wicket (the Super Over has two wickets, so the next batter is placed directly).

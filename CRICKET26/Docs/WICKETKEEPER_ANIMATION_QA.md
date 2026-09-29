# Wicketkeeper animation QA, 2026-09-27

Isolated overhaul of the keeper only. Batting, bowling, general fielding,
general running, ball physics, scoring, dismissal rules, player visuals, HUD,
audio and commentary are untouched; the keeper reads them through existing
interfaces. The simulation stays authoritative: animation never decides a catch,
a wicket or a ball outcome.

A row is PASS only when checked by eye in slow-motion captures, not just by
metrics or tests. Anything compared against Cricket 26 without a cited
observation is UNVERIFIED.

## Architecture found

- Keeper slot: `Ctx.Field[0]` (`bKeeper`), actor `Fielders[0]`.
- Depth: `CricketField::Make` put pace at (-16, 0.5*Off) and spin at
  (-0.8, 0.3*Off), one distance per bowler type, no speed or handedness logic
  beyond the off-side sign.
- Pose: `UpdatePoses` gave every fielder the same treatment: a fixed squat
  (`Drop` 0.4 -> 0.12), gloves by the knees, and a proximity reach toward
  `BallAt(T+0.12)` clamped to 0.65 m. No take families, no footwork, no
  low/high shapes, no spin difference, no stumping transfer, no run-out
  reception, no markers, no selection log.
- Dives: the generic Mixamo "Goalkeeper Diving Save" clip shared with all
  fielders, positioned in `UpdatePresentation` from the solver's take point.
- Ball in hand: `HolderAt` -> `hand_r` socket. Keeper take cue at `FieldTime`.
- Gaze: `UpdateFigures` points every figure's head at the ball. Correct for the
  keeper; kept.

## Root causes of the old look

| Symptom | Root cause |
|---|---|
| Frozen upright stance between balls | `Drop` only existed while live; no ready stance, no breathing |
| Same take for pace and spin | No bowler-type branch in the pose at all |
| Gloves chase, feet frozen | Actor only moved post-contact via `Moves`; no upper-body weight shift, no footwork states |
| Ball stops near gloves, not in them | Reach clamped to 0.65 m from the chest regardless of the true take point |
| Stumping snap | No transfer path; ball held at take, then at stumps |
| Run-out facing the bowler | Generic catch pose; never oriented to the throw |
| No way to tell a wrong take | No classification, no log, no markers |

## What changed (isolated)

- New pure module `Source/CRICKET26/Cricket/CricketKeeper.h/.cpp`: depth tiers,
  ready stance, pre-delivery load, take classification (19 families), physical
  reachability (1.5 m / 2.8 m, same envelopes as the solver), footwork states,
  glove target with clamp reporting, stumping progress, selection log.
- `FieldingModel::Make` routes keeper depth through `CricketKeeper::KeeperHome`
  (stock paces). No other fielder moved.
- `PlaceForDelivery` nudges the pace keeper by the bowler's actual stock pace
  (express deepest, medium/slower a step up), aligned behind the wicket for the
  striker's handedness. Keeper slot only.
- `UpdatePoses` keeper branch rewritten (keeper slot only): ready stance with
  breathing, pre-delivery load synced to `TimeToRelease`, trajectory-aware
  selection from the real interception, height-driven body shape (low through
  knees, high standing taller), absorb after contact, spin-keeping shape,
  stumping transfer synced to `BrokenTime`, run-out reception oriented to the
  throw, honest misses, restrained appeal shape, per-delivery variation in
  millimetres. Outfielder path byte-identical; shared dive/throw clip tail
  unchanged and applies to the keeper as before.
- Semantic markers as dev logs: `KeeperPushOff`, `KeeperCatchContact`,
  `KeeperSecureBall`, `KeeperStumpBreak`, `KeeperRecoveryComplete`, plus the
  per-delivery selection line (trajectory, family, animation, IK clamp).
- Tests: `CRICKET26.Keeper.Depth`, `.Classify`, `.Sync`.

## QA matrix

| Action | Current Problem | Root Cause | Fix | Ball Sync | Visual QA | Status |
|---|---|---|---|---|---|---|
| Pace depth | one distance | hardcoded -16 | tier by type+pace | n/a | capture | PASS (tests) |
| Spin depth | same code path | no branch | stand up 0.9 m | n/a | capture | PASS (tests) |
| Ready stance | frozen | no stance | athletic + breathe | n/a | capture | NEEDS EYE |
| Pre-delivery | static squat | no rhythm | load to release | n/a | 0.25x | NEEDS EYE |
| Central take | clamp 0.65 m | proximity reach | gloves to FieldPos | contact | 0.25x | NEEDS EYE |
| Low take | spine bend | no shape | knees/hips drop | contact | 0.25x | NEEDS EYE |
| High take | collapsed | no shape | stand taller | contact | 0.25x | NEEDS EYE |
| Lateral take | frozen feet | no footwork | shuffle/cross + body | contact | 0.25x | NEEDS EYE |
| Thin edge | preselected | event-based | react after contact | contact | 0.25x | NEEDS EYE |
| Dive | generic only | no family | PushDive + clip path | contact | 0.25x | NEEDS EYE |
| Spin take | pace moved closer | no branch | compact + turn-aware | contact | 0.25x | NEEDS EYE |
| Stumping | snap | no transfer | progress to BrokenTime | StumpBreak | 0.25x | NEEDS EYE |
| Run-out | faces bowler | generic catch | orient to throw | arrival | 0.25x | NEEDS EYE |
| Unreachable | stretch | no reach check | honest miss | miss | capture | PASS (tests) |
| Recovery | instant stand | no chain | Drop/glove return | n/a | capture | NEEDS EYE |

PASS requires visible movement quality at 0.25x from front, side, rear, 45°
and close-up. Tests prove selection logic; only the eye passes motion.

## How to check

- Force pace vs spin: F3 cycles bowler type; keeper depth visibly changes.
- Slow motion: `-CricketSlowMo=0.25`.
- Dev log: `Saved/TestRun.log` / capture log lines `Keeper sel:` and
  `Keeper event ...`; grep for `WRONG-ANIM` (clamp over budget means the wrong
  family was picked: fix classification, never raise IK).
- Foot slide: keeper actor travels at the solver's speed (`PositionOf`);
  pre-contact motion is upper-body only over planted feet.

## Remaining defects (explicit)

- Lateral travel post-contact blends the jog under keeper IK; brief foot-phase
  mismatch possible on long shuffles (distance-matched, solver-speed).
- No mocap source clips yet: motion is planned procedural geometry solved
  through the existing two-bone IK, not captured athletic movement. Golden
  motions 1-7 await eye acceptance before any library expansion.
- Keeper dive uses the shared generic dive clip; keeper-specific dive clips
  (low/waist/high, off/leg) are named in selection (`AnimName`) but not yet
  authored/imported.
- Left/right takes are distinct families but share the IK solver (no mirrored
  clip tables yet).

## Follow-up visual audit, 2026-09-27

- Slow-motion spin capture (`-CricketAiLeaves -CricketSlowMo=0.25`): old `ReadyFor` pelvis drops (0.46 m spin, 0.40 m pace) put both knees on the ground. Reduced them to 0.27 m and 0.22 m. Spin retake `Ball1_008.png` keeps both feet down and knees clear.
- Selection used distance from keeper home to ball as reach at contact. Long runs could become visual misses despite the solver assigning the keeper. Selection now trusts the solver's standing/dive result; home distance still chooses footwork.
- Spin dive selection overwrote its own dive branch. Kept the dive branch and added regression tests.
- Keeper reaches now engage clavicles within 12 degrees. Glove IK targets stop at a 0.9 m chest-to-glove limit. Head target smooths over time. Clip blends suppress keeper shoulder shift.
- The shared captured fielding dive lifts gloves above low balls in the pace retake (`Ball4_017.png` to `Ball4_019.png`). Low and near-line balls now bypass that clip; retake still shows an airborne phase and needs further trajectory/contact review. The 36-case gameplay matrix, foot-slide gate, equipment clipping, and multi-camera review are not complete. Do not mark this overhaul visually passed.
- Automation: `CRICKET26.Keeper` passed 4/4 after selection changes; full `CRICKET26.` suite passed 96/96 after the final reach and dive-height edits.
- Normal-speed broadcast retake of the same pace ball (`Ball4_015.png` / `Ball4_016.png`) still shows a one-legged airborne reach and poor visible ball/glove alignment. This is a failed broadcast quality gate, not a verified keeper catch. The shared `Field_Dive` clip needs keeper-specific contact timing and root alignment or replacement with authored keeper take/dive clips.

## Wide take on the feet, 2026-09-29

Scenario: `Scripts/capture.sh 3 -CricketPace -CricketLength=6 -CricketLine=1.3 -CricketAiLeaves -CricketSlowMo=0.25 -CricketDebug "-CricketDevCam=-9,5.5,1.2,-12.6,1.8,0.5,45"`. This is a beaten ball taken at +2.25 m wide and 0.12 m high, on the feet (not the side dive).

| Defect seen | Root cause | Fix |
|---|---|---|
| After the take, the keeper glided about 1.7 m in a crouch. | The solver's run is aimed at the ball itself and kept going after `FieldTime`. The 0.55 m side-step was then added on top. | `CricketKeeper::TakeBodyAt` stops the run at `min(Post, FieldTime)`. The body stays put once the ball is in the gloves. |
| On wide takes the gloves clamped about 1.1 m short of the ball. | The rendered run set off at the generic 0.15 s. The solver had timed the keeper from the pitch (`KeeperLead`), so the body arrived late and far from the ball. | `FieldingModel` keeper `ChaseStart = 0.15 - KeeperLead`. `TakeSideStep` closes the gap to within `KeeperArmsLength`, capped at `KeeperReach - KeeperArmsLength`. |
| After a low take the keeper stayed in a 0.5 m squat and never got up. | The low-ball sink was weighted by `Approach`, which stays at 1 after the take. | `CricketKeeper::TakeDrop` is weighted by `Receive`, which releases after the hold. The sink is capped at `MaxTakeDrop` (0.42 m) so no knee touches the grass. |
| `WRONG-ANIM` flooded with huge values while the keeper was still at home. | The check ran on frames where the take was not yet driving the gloves. | The log fires only when `Receive > 0.5`. |
| During the step across, the keeper folded to the grass for one frame (shin flat, other foot up, head down). | The side-step moves at about 4 m/s, above the 2.2 m/s jog threshold. `UpdateFigures` faded the jog clip in (0.07 → 1.0 over 8 frames) under the deep crouch while the keeper feet IK faded out. | `CricketKeeper::ShufflesToTake` keeps walk, jog and sprint at 0 for any take on the feet within `KeeperDiveReach` of home, until the get-up finishes at `FieldTime + 0.85 s`. The feet are stepped by `StepFeet` instead. A skier beyond the dive envelope still runs on the locomotion clips. |

Evidence (after the fix): jog weight is 0.00 on every frame from post 0 to 1.4 s. `Ball3_027`–`Ball3_032` show a crouched shuffle, a low take, then a smooth rise, with no collapsed frame. All `WRONG-ANIM` lines fall at post 0.19–0.48 s, while the gloves are still converging before the 0.56 s take. None fire at or after the take.

Regression test: `CRICKET26.Keeper.TakeBody` checks four things:
- The body's lateral residual is at most one arm's length at every width from 0.2 m to 1.91 m, on both sides.
- The body never passes the ball, and stops at the take.
- The solver's run reaches within `KeeperReach` for each `KeeperLead` × width (without the `ChaseStart` fix this fails at 1.82 m and 2.32 m).
- `TakeDrop` sinks and releases correctly. `ShufflesToTake` holds through the take, and hands back to locomotion after the get-up and for a far skier.

Automation: 124 pass, 1 fail. The failure is `CRICKET26.Touch.Controls` ("batting has only the shot modes", expected 3, got 5). It is a touch-controls test and failed before the keeper changes.

Still open:
- The rest of the 36-case matrix and a normal-speed broadcast retake have not been rerun. This covers the wide pace take only.
- The keeper-specific dive clips are still missing (see above).

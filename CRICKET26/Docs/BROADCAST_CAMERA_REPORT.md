# Broadcast Camera + Replay Overhaul — Final Report

Reference: Cricket 24 gameplay capture (`~/Downloads/CRICKET26.mp4`), frames 64–73 s (delivery A–E and
post-contact) and 136–152 s (a FOUR). In-game captures: `-CricketAutoPlay -CricketShotBall=N
-CricketShotEvery=0.1` at 1280x720.

## 1. Summary
The delivery camera is now the Cricket 24 stand camera: fixed 51.7 m behind the bowler's stumps at
8.1 m, zooming 25.9° → 4.6° along a measured lens curve keyed to a delivery clock (run-up start →
release → ball at the batter). Post-contact coverage follows the Cricket 24 pattern:
- An infield ball is followed by the same stand camera, widening and panning.
- An outfield or boundary ball cuts to a reverse angle from the far side of the ground. That camera
  holds through the rope.

The replay system (a rolling buffer of what was actually presented, priority packages, a time-remap
slow motion) observes only. It never writes match state.

## 2. Delivery camera (priority 1)
- Position: `(PitchLength + 51.7 m, 0, 8.1 m)`. It never moves during the delivery.
- Lens and tilt: `FDeliveryCameraTune::LensCurve`, 9 keys `(U, FOV, CreaseY)` measured from the
  reference. FOV interpolates log-linearly and crease height linearly.
  - The FOV is horizontal on a 16:9 screen.
  - CreaseY is the striker's popping-crease screen height. It sets the tilt exactly.
- Delivery clock: `DeliveryClock()`.
  - U from 0 to 1 is run-up progress, taken from the bowler's real time-to-release.
  - U from 1 to 2 is ball time to contact.
- Pan: a weighted bowler/release/ball/batter target, scaled by `LateralFollow` (0.25).
- The ground clamp keeps the look target on the turf.

## 3. A–E comparison against Cricket 24
Measured at 640 px width:
- D (mid-flight) and E (near the batter): the return-crease width matches within about 5%
  (230 vs 235 px, 280 vs 290 px).
- A–B (run-up): the composition matches, with the pitch at the top at similar scale.
- C (release): the zoom tracks the curve. A ~1° lag was fixed (the FOV now damps at 3× the rotation rate).

This is **not an exact match**. The remaining differences come from other systems (see §19).

## 4. Aspect ratio
Lenses are tuned at 16:9. `AspectFOV()` keeps the vertical view on other screens, so a 19.5:9 phone
sees wider, not closer. Covered by a test. Not yet viewed on a device.

## 5. Smoothing (priority 2)
`FSmoother` is frame-rate independent (tested at 30/60/120 Hz).
- The look dead zone now scales with FOV. On the 4.6° lens the old 10 cm dead zone froze the frame
  (regression test `SmootherLongLens`).
- The FOV has its own damping, so the zoom stays on its curve.

## 6. Phase resolution fix
Root cause: `ResolvePhase` returned CONTACT for the whole flight, because the after-contact time is
negative before contact. It now stays DELIVERY until contact. Regression test in `Phases`.

## 7. Post-contact coverage (priority 3)
- **Infield ground ball** (`GroundFollow`): the same stand camera widens (12°–42°) and pans between
  the ball and the striker. It blends, never cuts. The horizon is held in the top strip
  (`FollowHorizonY` 0.7), so the field fills the frame, as in the reference.
- **Outfield, boundary or lofted ball** (`OutfieldFollow`): a cut to a reverse angle.
  - Placement: 70 m behind pitch centre, opposite the exit line, 14 m up.
  - Framing: the lens holds about 40 m of outfield around the ball, and the horizon sits about 40%
    from the top (`OutfieldHorizonY` 0.2).
  - A climbing ball takes the tilt up with it.
  - The station is fixed per stroke, so the camera only pans and zooms.
  - Verified against the reference FOUR: the pitch sits lower-left, the boards and crowd run across
    the top, and the camera holds through the rope.
- Regression tests: `FollowHoldsHorizon` checks horizon share, ball in frame, turf clamp and
  reverse-angle side; `CutRules` and live selection were updated.

### Non-boundary balls (follow-up fix)
Symptom: after a miss, a defensive push or an ordinary ground/lofted shot, the camera cut to a low angle
pointed at the turf with no pitch or players in it.

Root cause: a "take window" in the game mode forced the `Catch` role for every stop that was not a
boundary or wicket. That camera stands 9 m from the fielder at head height, looking out at the boards.
- For a miss, the fielder is the keeper, so the lens landed at the batter looking back at the keeper.
- On top of that, every run cut to the `RunOut` crease camera 0.6 s after contact, run-out or not.

Fix (the selection now lives entirely in `SelectLiveShot`, so it is tested):
- A miss holds the delivery lens through the keeper's take, then the bowler close-up.
- Every other non-boundary ball goes to the high reverse angle (`OutfieldFollow`) after the contact breath
  and stays there through the stop, the throw and the runs, as Cricket 24 does (reference 68–75 s).
  This covers ground, lofted, dropped-catch and running balls.
- The crease camera cuts in only when a run-out is really on (the race is lost or within 0.3 s). It waits
  until the throw is released and is placed at the end the throw goes to.
- A low edge fielded by someone other than the keeper counts as an outfield ball. An edged four is a boundary.
- Byes are covered as a running ball. Their station comes from the path the ball ran.

Regression test: `NormalBallCoverage`. It checks selection over the whole live ball for every non-boundary
class, and checks framing for balls hit round the ring at 12, 25 and 45 m: pitch, striker and ball are in
shot, and the camera dips between 0° and 20°.

## 8. Director (priority 4)
`SelectLiveShot` maps phase and shot class to a role. `FDirectorState` enforces minimum durations,
no thrashing and no ping-pong. `TransitionFor` sets the grammar:
- Delivery → infield follow: blend.
- Every other role change: cut.

The rope camera (`Boundary`) is now a replay angle. Live coverage holds the reverse angle, as
Cricket 24 does.

## 9. Replay recording (priority 5)
`FCricketReplayBuffer` is a ring of the presented frames, fed from live frames only:
- Contents: the ball plus up to 20 actors (position, rotation, scale), keyed by ball time.
- Size: 30 Hz, 14 s.
- Allocation: none once full.
- Estimated size: about 0.7 MB (420 frames × 21 poses × 80 B).
- Event markers: contact, stumps, broken, boundary.

Recording is suspended during replays, reviews and reels, so the buffer always holds one true live pass.

## 10. Replay packages and camera direction (priority 6)
`ClassifyReplayEvent` assigns a priority (LOW / MEDIUM / HIGH / HERO). `BuildReplayPackage` then
builds one to four angles. `PickReplayShot` avoids the angles used recently.

Example sequences from the capture:
- FOUR: beauty then rope.
- SIX: alternate delivery, side-on, then aerial.

### Full replay (Cricket 24 structure)
The reference (`~/Downloads/CRICKET26.mp4`, FOUR at 151–160 s) plays: event stinger, then the whole ball
from the bowler's delivery stride through the stroke, then closer angles split by logo wipes. Every package
now opens the same way:
- The entry wipe names the event (FOUR, SIX, BOWLED, ...). Later wipes carry the logo.
- Angle 1 is the full pass (`bFullPass`): ball time from 0.9 s before release to the ball crossing the rope,
  or 1.2 s after the decisive moment (7 s cap). It is directed like live coverage: delivery lens, then the
  live follow. Bowled, LBW and hit-wicket balls hold the delivery lens through the stumps.
- Its screen time is `NaturalWallTime`: real speed, easing to 0.6x through the decisive moment.
- The delivery stride before release is posed analytically. From release on, the replay uses the recorded buffer.
- One or two detail angles follow (stroke, slow-mo, rope). Live-duplicate angles are dropped.
  Tunables: `ReplayFullLead`, `ReplayFullMax`, `ReplayFullSlow`.

Verified from an in-engine capture (`Scripts/capture.sh 1 -CricketForceWicket -CricketDebug`): BOWLED stinger,
then run-in, release, stumps and keeper in one continuous shot, then super slow-mo and side-on. No device test yet.

## 11. Slow motion (priority 7)
`BuildTimeRemap` and `ReplaySpeedAt` give per-angle wall-time → ball-time remaps. Speed eases into
the slow factor around the decisive moment. Both are tested (`ReplayPackages`).

## 12. Match-state safety
During replays the camera and replay code write only:
- actor transforms (buffer pose), and
- camera state.

Scoring, the result, fielding and rules are only read. The next delivery re-poses every actor from
live state (`Waiting`), so the return to live is deterministic.

## 13. Occlusion and safeguards
- `ApplyOcclusion` traces only against WorldStatic, and only on a cut, so players never yank the camera.
- `ApplyCameraSafeguards` keeps the camera above the turf.
- Follow look targets are clamped to the turf.

## 14. Centralised tuning
Every number lives in `FBroadcastTuning` / `FDeliveryCameraTune` (`CricketBroadcast.h`), each with a
doc comment. New this pass:
- `LensCurve`, `LateralFollow`
- `FollowMinFOV`, `FollowHorizonY`
- `OutfieldHalfWidth`, `OutfieldCamHeight`, `OutfieldCamBack`, `OutfieldHorizonY`

## 15. Development calibration and debug overlay
- `-CricketDeliveryCam=` overrides the delivery camera at launch.
- `-CricketDebug` shows the overlay: shot, phase, shot time, the **live FOV and delivery clock U**
  (new), the replay package and angle, ball time and speed, and buffer fill and size.

## 16. Performance (mobile)
Per frame:
- one smoother update;
- one recorded sample at 30 Hz, O(actors), with no allocation;
- one line trace, only on cuts.

The replay buffer memory is fixed (about 0.7 MB). There are no new assets, tick groups or
components. Not yet profiled on a device.

## 17. Automated tests
- `CRICKET26.Broadcast.*`: 13 tests, all pass. New or extended this pass: `DeliveryLensCurve`,
  `SmootherLongLens`, `FollowHoldsHorizon`, plus additions to `Phases` and `CutRules`.
- Full `CRICKET26.` suite: 88 pass, 0 fail.

## 18. Manual acceptance matrix
| Check | Status |
|---|---|
| Delivery A–E vs Cricket 24 | Compared visually. D and E zoom within ~5%. A–C composition close. Not exact (§19). |
| Zoom continuity through release | Verified from the per-frame FOV trace: monotonic, no hitch. |
| Infield follow (same camera, field fills frame) | Verified in capture. |
| FOUR: reverse angle, holds to the rope | Verified in capture against reference 144–150 s. |
| SIX: reverse angle tracks the climb | Captured. Crowd-dominant mid-flight, as in the reference. Ball visibility at 1280 not confirmed. |
| Replays play real buffered poses, no state change | Seen in capture (beauty/rope, alternate/side-on/aerial). Covered by tests. |
| Catch / run-out / stumping live coverage | **Not visually verified** this pass (no such event in captured deliveries). |
| 19.5:9 phone aspect | Covered by test only. **Not viewed on a device.** |
| Mobile frame time | **Not profiled on a device.** |

## 19. Limitations
**Mine (camera and replay):**
- The delivery match is close, not exact. The lens curve was measured from one reference delivery.
- The outfield reverse angle uses one fixed height and distance for every ground. It was tuned on
  this stadium only.
- Catch, run-out and keeper coverage were not re-verified visually against the reference.
- There is no batter close-up yet for a boundary live. The reference shows one; here that falls to the
  existing dead-ball presentation.

**Other systems (read-only for this mission):**
- The run-up now matches Cricket 24: a quick bowler comes in from 15 m (a spinner from 7 m) and the ideal
  release comes 2.5 s after setting off. Before this change it was 8 m / 0.9 s.
- Camera moves are gentler. The smoother's slew cap is now a ceiling, not a floor, so small target nudges
  ease in instead of snapping in one frame. Delivery tracking is looser (lag 0.3 s, rotation damping 8),
  the contact FOV punch scales with the lens, and a shot now holds for at least 1.2 s.
- The bowler's follow-through runs down the pitch centre line and occludes more than in the reference.
- The umpire stands to the right.
- HUD placement and scene lighting differ from the reference.

## 20. Files touched
Camera/replay files (this mission):
- `Source/CRICKET26/Cricket/CricketBroadcast.h`
- `Source/CRICKET26/Cricket/CricketBroadcast.cpp`
- `Source/CRICKET26/Cricket/CricketReplayBuffer.h`
- `Source/CRICKET26/Cricket/CricketReplayBuffer.cpp`
- `Source/CRICKET26/Cricket/Tests/BroadcastTests.cpp`
- `Docs/BROADCAST_CAMERA_REPORT.md`

**Shared files touched** (additive, camera block only):
- `Source/CRICKET26/Cricket/SuperOverGameMode.cpp`: the delivery clock and lens feed, the aspect
  correction, FOV damping in the smoother call, the debug overlay FOV and U, the replay record and
  buffer-pose hooks.
- `Source/CRICKET26/Cricket/SuperOverGameMode.h`: camera and replay members (`ViewAspect`,
  `LastCameraDebug`, replay package and buffer state).
- `Source/CRICKET26/Cricket/SuperOverHUD.cpp`: one debug-overlay line printing `CameraDebugString()`.

These shared files also carry other agents' uncommitted work. They were not reset, reformatted or
reverted.

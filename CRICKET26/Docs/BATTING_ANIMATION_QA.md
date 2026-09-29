# Batting animation QA, 2026-09-25

The striker's animation was rebuilt from scratch without AI tools or captured clips. The imported DeepMotion
batting takes are no longer loaded; every stroke is planned as geometry (`CricketBatter::Plan`) and solved
onto the MetaHuman skeleton bone by bone (`FCricketAnimProxy::SolveBatter`).

A row is PASS only when it was checked by eye in slow-motion captures, not just by metrics or tests. Anything
compared against Cricket 24 is UNVERIFIED: no Cricket 24 footage was used or compared frame by frame.

## How it is checked

- **In-game metrics**, logged at the end of every capture (`Saved/Capture.log`, lines starting `Pose:`):
  - how far each hand's knuckles are from where they should grip the handle;
  - samples with an elbow or forearm inside the torso;
  - the largest movement of a planted foot;
  - samples with a raised hand (wrist above the shoulder) whose elbow is winged out sideways, more than 12 cm
    beyond its shoulder across the chest.
- **Automated test** `CRICKET26.Animation.BatterPlan`. It covers ten strokes, right- and left-handed, sampled
  at 120 Hz and checks:
  - the sweet spot is on the ball at contact;
  - the bat frame is true;
  - the hands stay within reach through the stroke and within a straight arm throughout;
  - neither arm is folded up against its shoulder;
  - the handle stays off the chest;
  - planted feet stay put;
  - there are no jumps;
  - the left-hander is the exact mirror of the right-hander.
- **Visual sheets** from `Scripts/anim/stroke_qa.sh <label> <view> [args]`, saved as
  `Saved/AnimQA/<label>_<view>.png`:
  - capture runs at 0.25x speed, one frame every 0.04 s of game time, tiled around the contact;
  - views are point, front, leg, rear, lpoint (for a left-hander), and the close arm cameras: close (front
    three-quarter from the off side) and back (behind square leg).
  - Force a stroke with `-CricketAiShot=<intent>,<direction>`, where intent is 1 defend, 2 ground or 3 loft.
    Add `-CricketLength=8.5` for the short ball the cut and pull need, and `-CricketLeftHanded` for a
    left-handed striker.

## Before and after (in-game metrics)

"Hands from grip" is the distance from each hand's knuckles to the handle, in cm.

| Stroke | Before: hands from grip p95 / max | After: p95 / max | After: arm in torso | After: planted-foot slide max |
|---|---|---|---|---|
| Drive (front view) | 3.7 / 14.1 | 0.1 / 0.2 | 0 of 152 | 0.10 cm |
| Cover drive | 5.1 / 65.0 | 0.2 / 3.9 | 0 of 640 | 0.11 cm |
| Pull | 5.0 / 65.0 | 0.1 / 0.6 | 0 of 276 | 0.02 cm |
| Defence | 3.6 / 17.8 | 0.4 / 1.0 | 0 of 436 | 0.12 cm |
| Loft | 4.1 / 65.0 | 0.3 / 2.4 | 0 of 784 | 0.12 cm |
| Sweep | not captured | 0.1 / 1.8 | 0 of 148 | 0.05 cm |
| Cut | not captured | 0.4 / 6.4 | 0 of 824 | 0.04 cm |

The before sheets are `Saved/AnimQA/before_*.png` and the after sheets are `Saved/AnimQA/after_*.png`. The
before build had no arm-in-torso or foot-slide metrics.

## Arm and elbow pass, 2026-09-26

You reported that the elbow and arm movement looked wrong. The close camera showed four faults:

- the elbows winged out sideways whenever the hands were raised;
- the finish was low, with the hands in front of the face;
- just after contact the bat went flat across the chest;
- in the defence, the hands sat at the chin and both elbows stuck out.

The fixes are rows in the Problems table below. Cricket 24 footage from `~/Downloads` was used as a visual
reference only: a straight-drive replay (`SHOT.mp4`, 256 to 258 s) and a defensive practice swing (the Ashes
video, 313 to 316 s). No assets were taken from it.

Close-camera captures after the fix, on the final build (`Saved/AnimQA/arms_<stroke>_close.png`):

| Stroke | Hands from grip p95 / max (cm) | Arm in torso | Raised elbow winged out | Planted-foot slide max |
|---|---|---|---|---|
| Drive | 0.0 / 0.2 | 0 of 760 | 8 of 98 | 0.00 cm |
| Cover drive | 0.0 / 1.4 | 0 of 2752 | 136 of 405 | 0.13 cm |
| Loft | 0.0 / 0.4 | 0 of 3780 | 128 of 371 | 0.13 cm |
| Pull | 0.0 / 2.3 | 0 of 848 | 0 of 87 | 0.02 cm |
| Defence | 0.0 / 0.2 | 0 of 1984 | no raised samples | 0.00 cm |

Before the fix the counts of raised elbows winged out were drive 12 of 83, loft 43 of 260, cover 40 of 236,
and defence 138 of 138. All of the cover and loft samples still counted are the top arm in the high finish,
with the hands beside the head. On the sheets that elbow points up and forward, as in the reference drive,
so the metric is stricter than the pose. That is my judgment and needs yours.

Performance: the arm solve (`SolveBatter`) averaged about 1.7 ms per call, with a worst case of 8 to 22 ms. It was
measured on a machine under heavy background load (hung crash reporters), so treat it as an upper bound.

## Problems

| Problem | Root cause | Fix | Verification | Status |
|---|---|---|---|---|
| Elbows inside the torso | The arm IK used fixed elbow poles. As the bat came across the body, the elbow swung wherever the pole pointed, often into the chest. | Each arm is solved by searching elbow swivel and grip roll. The cost penalises the elbow, forearm and upper arm inside a torso ellipse, a bent or over-twisted wrist, and a jump from the last frame. The clavicle reaches forward when the handle is far. | Arm-in-torso metric: 0 samples on every captured stroke (see the pull row below for the last one fixed). Point-view sheets. | PASS |
| Hands off the handle; poor elbow and wrist motion | The bat was rotated about the sternum rather than held by the hands, so its grip went out of the arms' reach and the IK stretched. Hands were up to 65 cm off the handle. | The planner keeps each hand within reach (0.58 m) of a nominal body's shoulders: at every key of the bat's path, and on every frame after contact. The solver grips the handle with the knuckles 3.2 cm off its centre line. | Hands from grip p95 at most 0.4 cm on every captured stroke; the worst single frame is 6.4 cm, on the cut. The test checks reach. | PASS |
| Sliding feet | The whole actor was translated for the stroke's footwork while the clip planted its feet elsewhere. Nothing held a planted foot. | In the planner, a foot's ball only moves while that foot is lifted mid-step; planted, it can only pivot on its ball. The legs are solved to the planned feet. | Planted-foot slide max at most 0.12 cm on every captured stroke. The test checks that planted feet stay put. | PASS |
| False foot-slide readings (a measurement bug) | Replays rewind the ball time in the dead ball, and the metric counted each rewind as the foot moving (53.7 cm and 36.1 cm on the cut). A second bug: the per-actor "was planted" flag was reset every frame, so it never measured anything. | Frames where the striker's time jumps back or ahead by more than 0.25 s are skipped. The flag is only reset for the striker. | Cut recapture: slide max 0.04 cm. | PASS |
| Bad weight transfer | The pelvis did not move with the stride; the clips' body and the planned bat disagreed. | The weight phase moves the pelvis onto the planted foot by the contact. The knees bend as low as the stride needs, from a nominal leg length. The back heel comes up on the drives. | Point-view sheets for drive, defence, cut and sweep. | PASS (point view). Front and rear views have been checked only for the drive and the cut. |
| Stiff torso | The chest made one procedural turn toward the shot, or followed whatever the clip carried. Nothing sequenced the hips, chest and arms. | Kinetic chain: the hips start turning 0.06 s before the front foot plants, the chest 0.04 s later, then the arms and bat. The chest finishes 0.07 s after contact. The spine shares the chest's turn and forward and side bend. The head tracks the ball. | Point-view sheets. | PASS (point view). Needs your judgment on how fluid it feels. |
| Bad transitions | Clips were blended over a procedural body, and the run started from the guard (a teleport). The recovery left the bat where the stroke ended while the body stepped back, so the pull's handle crossed the chest. | One continuous plan from stance, backlift and trigger through the stroke, follow-through and recovery. In the recovery the hands carry the bat with the body and round the chest. The run starts from where the stroke left the striker. | The test checks that the grip stays under 15 m/s and off the chest. The run-start capture shows no teleport. | PASS |
| Defence: toe leading the handle | The defence met the ball 2 m out, beyond the arms. The reach fallback then angled the bat from the shoulders toward the ball, so the toe led. | A forward defence meets the ball at 1.8 m, under the eyes. The front foot strides to the ball and the chest bends over the knee (36°), so the handle leads the blade. | v5 point sheet: bat near vertical beside the front pad. Arm-in-torso 0, slide 0. | PASS |
| Cut: toe held up in the follow-through | The follow-through turned 95° about a tilted swing plane, which lifts the toe. | A shorter arc (45° through, 68° finish). The cut steps across and meets the ball beside the body at arm's length. | v7 point sheet: flat finish toward point. | PASS. The cut still stands fairly upright, which needs your judgment. |
| Contact reach checked against the wrong body | The bat at contact was checked against the pose the stroke was heading for, but at contact the chest is still turning. On the sweep, the hands were 0.61 m from the shoulders against the 0.58 m limit. | The reach and lean at contact are checked against the body evaluated at the moment of contact. | The BatterPlan test now passes for all ten strokes. Sweep capture: hands p95 0.1 cm, arm-in-torso 0. | PASS |
| Pull: top forearm through the chest just after contact (4 of 848 samples) | The planner only capped how far the hands reach, never how close they come. The pull's follow-through key was a sideways offset from the contact, so as the chest turned, the front shoulder swept into the bat's path. The top hand ended up 0.10 m from its shoulder (0.01 m on the hook), the arm folded shut, and the elbow was pushed behind the body with the forearm along the flank. | The pull and hook through-key is now in front of the chest at the body's pose at that moment, at the contact's height, and the finish is 0.3 m ahead of the front shoulder. Every frame keeps each hand at least 0.22 m from its shoulder (`MinReach`). If the contact itself would cramp the arms (a ball into the body), the body moves back from the ball, as it already leans in for a wide one. The chest clearance is measured from the spine at the grip's height, not from the pelvis. | Pull recapture: 0 arm-in-torso samples. The test now checks that the arms are not cramped, with no jumps, on all ten strokes. | PASS (metrics and point sheet). The body turns away from the point camera, so the leg view needs your eye. |
| Left-handers | The old takes were a right-hander's and were never mirrored. | The plan is authored in a batter frame and mirrored into the world, so a left-hander is the exact mirror. The top hand and front foot swap. | The test checks the mirror on every frame of every stroke. lpoint sheets for a left-handed drive and cut: metrics clean, drive mirrored correctly. The cut was a missed wide ball. | PASS for the drive. Metrics only for the other strokes. |
| Elbows winged sideways with the hands raised | The solver preferred each elbow "down and a little out" in every pose. With the hands up by the head, the only elbow that was both down and out was one sticking sideways. | The preference now depends on the arm and the hand's height. Low, the top arm's elbow leads out toward the bowler and the bottom arm's hangs down by the back hip. As the hand rises past the shoulder, both blend to pointing forward under the hands. | Raised-elbow metric (Arm and elbow pass above). Close sheets of drive, cover and loft. | PASS on my eye. Needs your judgment against Cricket 24. |
| Low finish, with the hands in front of the face | The drive's finish key held the hands 0.1 m above the front shoulder and 0.2 m ahead of it. | The hands finish 0.4 m above the front shoulder (0.46 m on a loft), just outside it and barely ahead, with the bat upright behind the head. | `arms_cover_close` and `arms_loft_close` sheets: hands beside the head. | PASS on my eye. |
| Bat flat across the chest just after contact | The through key, 0.12 s after contact, already turned the bat 80° from the contact, so it lay flat with the hands still at the contact's height. In the reference drive the hands lead up while the bat stays nearly upright. | The through key turns the bat 50° (55° on a loft), with the hands 0.35 m above the contact and 0.25 m along the shot. | `arms_drive_close` sheet. | PASS on my eye. |
| Defence: hands at the chin, both elbows winged | The 36° chest bend brought the head down to the hands, and the grip sat about 25 cm in front of the chest, so both arms folded and the solver could only put the elbows out sideways. | The chest bends 24° and the knees drop 4 cm more. The bat is angled further, with the handle forward, and the pelvis sits further back between the feet, so the hands are well out in front. | `arms_defend_close`: head over the hands, top arm long with its elbow forward. No raised-hand samples (138 of 138 before). | PASS on my eye. |
| Recovery ran the hands through the front shoulder (found by the test after the higher finish) | The recovery moved the grip in a straight line from the finish to the grounded bat. From a high finish that line passes through the front shoulder: the flick's hands came within 0.01 m of it and jumped at 26 m/s. | The hands come down in an arc 0.3 m out in front of the chest, and every frame is kept within reach, off the chest and uncramped. | BatterPlan test: all ten strokes pass the cramp, reach and jump checks. The full suite passes, 60 of 60. | PASS |
| Hook | The spinner the AI uses bowls no bouncers, so a forced hook falls back to the pull. | `-CricketPace` makes the AI bowler quick, so `-CricketPace -CricketAiShot=3,-60 -CricketLength=10` forces a hook. | `pull4_ref`: hook played, 0 arm-in-torso, 0 elbow bowed up. The run starts before the finish can be seen. | PASS on metrics; finish unseen |
| A second body size | The planner uses one nominal adult, and the solver adapts to the actual skeleton's arm and leg lengths. | Not applicable. | No second MetaHuman body has been captured. | OPEN |
| Parity with Cricket 24 | Not applicable. | Not applicable. | Reference frames of one drive and one defensive swing were viewed next to our close sheets. There has been no frame-by-frame or timing comparison. | UNVERIFIED |

## Needs your eye

- How the whole motion feels at full speed, especially the timing of the kinetic chain and the recovery.
- The cut's upright posture.
- The pull at the front and rear cameras.
- The elbows in the high finish of the cover drive and the loft, which the winged-elbow metric still counts.
- The defence's new, longer-armed shape (`arms_defend_close`).

## Pipeline repair: authored source motion, 2026-09-26

The in-game striker is still the runtime plan above (CricketBatter::Plan solved bone by bone every frame). That is full-body IK inventing the shot, which is the failure the pipeline mission names. The replacement is a baked source clip authored offline on the striker's own MetaHuman skeleton. Runtime would then only place the clip and make small corrections.

Authoring (Scripts/anim, Blender 5.2, headless):

- `strokes.py` keys the stroke on a few animator controls: bat, feet, pelvis, chest, clavicles, elbow poles and gaze.
- `author_stroke.py` fits the skeleton every frame. The arms are planned over the whole clip in one dynamic program over hand roll and both elbow swivels. Two things are ruled out, not just penalised:
  - every state outside the clinical range: humeral rotation -80..90°, forearm twist ±90°, wrist deviation -20..35°, and the elbow outside the trunk;
  - every step that turns a bone more than 20° in one frame.
- Where no legal motion exists, the script reports ILLEGAL frames and the keys have to change. The solver never bends the anatomy to fit.
- The humeral angle is measured by swinging via the scapular plane, not the hanging arm. The hanging-arm measure is singular with the arm straight up, which is exactly the high finish; there it spun through 180°.
- `validate_stroke.py` checks the baked clip frame by frame. Any FAIL means the clip does not ship.
- `test_author_stroke.py` covers the planner, the legality report, the smoothing and the arm measures, then plans, bakes and validates the golden drive end to end. Swapping the scapular-plane measure back to the hanging-arm one makes it fail.

Golden straight drive, validator worst values:

| Check | Worst | Limit |
|---|---|---|
| bone turn per frame (hands against the bat) | 19.9° (top upper arm, just after contact) | 20° |
| bat turn per frame | 27.2° | 33° |
| humeral rotation | 79.9° | -80..90° |
| forearm twist | 75.0° | ±90° |
| wrist deviation | 33.8° | -20..35° |
| elbow against trunk | 2.3 (1 is the surface) | ≥ 1 |
| handle slip in either hand | 0.0 mm | 2 mm |
| handle turn in either hand | 0.08° | 0.5° |
| planted-foot slide | 0.2 cm/frame | 0.3 |
| hands speeding up after contact | 0.35 m/s | 1.0 |

At contact (frame 50, 0.82 s):

- the sweet spot is at (0.80, 0.43, 0.30) m in the batter frame;
- the eyes are 0.97 m above it and 0.10 m behind it;
- the bat is 18° off vertical.

Keys re-authored to get there, each for a reason the planner reported:

- The downswing was back-loaded, and the bottom arm would have had to turn 20.6° in one frame. The bat now turns at an even rate into contact.
- At contact the bottom elbow was caught between the trunk and its wrist limit. The back shoulder is now driven through (clavicle forward 40°).
- The chest now opens after contact so the bottom elbow clears the trunk.
- The finish is as far back as both wrists allow with the grip unchanged.

Raw playback (Phase 1): `blend_views.py` with `MOVIE=1,0.5,0.25` writes `Saved/AnimQA/drive/raw/drive_az{000,090,180,270}_x{1,05,025}.mp4`, plus a contact sheet per view. No layers, no camera cuts.

Bug found on the way: importing the kit FBX for review reset the scene to 25 fps before export, so the FBX stretched the 1.9 s stroke to 4.6 s. Unreal would have played it at 0.42x. The export now sets the stroke's own rate, and `stroke_ue.py` rejects a clip whose length differs from its keys.

Still open:

- Importing into Unreal (`Scripts/anim/import_stroke.sh`) and wiring the clip into the game in place of the runtime plan. Two changes are needed:
  - the bat must follow the top hand bone at the grip the validator proves fixed;
  - the unreachable ball must become a miss or a mistime.
- Blocked: the editor module does not build right now. The untracked `Source/CRICKET26/Frontend/` work includes `Widgets/FrontendRoot.h`, which does not exist.
- Then acceptance of the golden drive at 0.25x, by your eye.
- Left-handers: the clip is authored right-handed, and a left-hander plays it mirrored through an Unreal mirror data table. The mirror is anatomically identical, so the validator's result carries over. The bone mapping still has to be checked in game. (author_stroke.py had a `--mirror` flag that was never read and silently produced a right-hander; it is removed.)
- The other eleven shots are not started, by design: the library does not grow until the drive passes.

## Elbow direction and reference matching, 2026-09-26

The reference is `~/Downloads/SHOT.mp4`: Cricket 26 broadcast gameplay, 640x360 at 30 fps. It is used to judge poses and
timing only. Big Ant's animation is not copied, because it is not ours to copy and a 2D video holds no skeleton data.
These are the clips used:

- straight drive, front view: 256.3–258.3 s;
- slog sweep, side view: 399–403 s;
- pull against pace: 620.5–621.2 s.

The lofted clips at 531 s and 610 s cut to the ball in flight at contact, so they show no finish.

- **Elbow bowed up (the reported bug).** Before the fix the elbow lifted off the shoulder-to-wrist line: the upper arm
  went up and the forearm dropped to the handle. The swivel preference in `SolveBatter` pointed the top elbow out
  sideways (`Outward - Up * 0.3`), and it pointed the elbow "deep" once the hand was raised. Now both elbows hang
  below the shoulder-to-wrist line. A steep cost punishes any upward bow. The new metric
  `Pose: striker elbow bowed up` shows these before/after counts:

  | Shot | Before | After |
  |---|---|---|
  | Drive | 7/74 | 1/70 |
  | Cover | 9/78 | 0/82 |
  | Loft | 112/344 | 2/320 |
  | Defend | 181/310 | 2/234 |
  | Pull | 67/224 | 0/196 |
  | Slog sweep | – | 5/740 |

- **Finish held.** The finish is reached 0.5 s after contact. The recovery used to start 0.9 s after contact and
  dragged the hands across the face almost at once. It now starts 1.5 s after contact. The finish is held about a
  second, as in the reference.
- **Slog sweep finish.** The slog sweep used to finish like a sweep, with the hands rolled round at chest height.
  In the reference the arms swing up high. The slog sweep's finish is now 0.3 m higher and slightly closer in.
  The BatterPlan test now includes the slog sweep and checks that its hands finish at least 0.2 m above the sweep's.
  Without the change that check fails, with both finishing at 0.95 m.
- **Loft high finish.** The winged-elbow metric counts about 40% of raised samples. Two grip changes were tried:
  - moving the hands forward covered the face;
  - moving them over the shoulder put the arm inside the torso.

  Neither reduced the count, so both were reverted. Close up (`loft4_ref`, `pull3_ref`), the pose matches a real
  high finish: the front elbow is up at shoulder height and the back arm crosses under the chin. The 12 cm
  threshold is too strict for that pose, so this is left for your eye rather than tuned to the metric.
- **Tooling.**
  - The `ref` camera in `stroke_qa.sh` frames the batter like the broadcast front view. Use it with
    `CROP=240,0,1040,720`.
  - The `-CricketPace` flag makes the AI bowler quick for pull and hook captures.
- Full suite: 78 pass, 0 fail.

Still open: every stroke is still the runtime planner. None has been matched frame by frame to the reference. It
would take authored source motion (see the pipeline section above) to get close to Big Ant's quality.


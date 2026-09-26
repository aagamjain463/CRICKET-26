# Handoff: captured animations (batting, running, fielding), 2026-09-25

This note lets a new agent continue the animation work where it stopped. For project basics (build, tests,
capture, commit rules) read `Docs/HANDOFF.md` first. This note covers only the animation work.

## The user's request

1. Take the video `~/Downloads/friend.mp4` of the user's friend playing each batting shot. Cut it into one
   clip per shot, turn each clip into an FBX with DeepMotion Animate 3D (free tier), and blend all the
   batting animations smoothly into the game.
2. Later request: "along with batting also work on fielding and running between wicket animations using free
   tools". The user is signed in to DeepMotion and Mixamo in Chrome.

Use only free tools. Ask the user to sign in if a site asks for it. Never type passwords, create accounts,
solve CAPTCHAs or accept terms for the user. Use they/them for the friend.

**Superseded for batting (2026-09-25, later).** The striker no longer plays the DeepMotion batting takes.
Batting is now a planned, solved motion; see "Batting" in `Docs/HANDOFF.md` and `Docs/BATTING_ANIMATION_QA.md`.
The sprint, throw and dive clips below are still in use. The batting parts of this note are history.

## Status (2026-09-25, second session)

All of it is wired in, tested (61/61) and checked in captures. See "Done in the second session" at the end.

| Piece | State |
|---|---|
| 10 batting takes (DeepMotion) | All imported (Bat_Cut turned -167 degrees). Right-handers play them. Loft and Cut checked in capture; Flick never came up in about 150 scanned balls, so it is unchecked. |
| Sprint (Mixamo Run_Sprint) | Playing. Made in place at import; stride speed measured at 6.3 m/s (`SprintSpeed`). Checked in capture: no visible foot sliding. |
| Throw (Mixamo Field_Throw) | Playing on overarm and relay throws, release on the simulation's release. Checked in capture. |
| Dives (Field_Dive, Field_Dive_Left) | Playing on dives and diving catches, hands at full stretch on the take. Checked in capture. |
| Ball in hand | The ball is drawn in the holder's right hand from the take to the release. |
| Docs | Pipeline, sources and licences are in `MASTER_PLAN.md` (3.3) and `HANDOFF.md`. |

## Changes from the first session (now committed)

Modified files:

- `Source/CRICKET26/Cricket/CricketPose.h/.cpp`
  - `FStrokeClip { Name, Contact }` and `StrokeClip(Shot, Foot, DirectionDeg)` pick a take for each shot.
    Each take's contact time (in seconds) was read off the filmed takes.
  - `StrokeClipTime(Clip, T, Press, Impact)` fits the take's last `ClipDownswing` (0.45 s) before contact
    into the simulation's press-to-impact window, then plays in real time. This makes the clip's bat meet
    the ball exactly when the simulation says it does.
- `Source/CRICKET26/Cricket/CricketAnimInstance.h/.cpp`
  - `FCricketBodyPose` has new fields:
    - `Clip`, `ClipTime` and `ClipWeight`: one clip blended over idle and jog.
    - `ShouldersAt` and `ShouldersWeight`: the pelvis is shifted so the clip's shoulders sit on the swing
      pivot, which lets the arms' IK reach the simulated bat.
    - `SprintWeight` and `SprintRate`.
  - `ClearActions()` keeps the jog and sprint fields.
  - The proxy blends in this order: idle, then jog, then sprint (weight is `JogWeight * SprintWeight`), then
    the clip, then the IK actions.
- `Source/CRICKET26/Cricket/SuperOverGameMode.h/.cpp`
  - The game loads `StrokeAnims` (a map from name to `UAnimSequence`, from `/Game/Anims/Mocap/<Name>`). It
    also loads `SprintAnim` (`/Game/Anims/Mocap/Run_Sprint`), using `LOAD_Quiet | LOAD_NoWarn`, so the game
    still runs if the assets are missing.
  - Batting (`UpdatePoses`, striker block):
    - Clip weight is `ClipW = Into * (1 - RunW)`.
    - The hands stay on the planned bat by IK.
    - While a clip plays, the procedural lean, crouch, chest turn and head look give way to it.
    - **Right-handers only**, because the takes are of a right-hander. Left-handers keep the procedural
      stroke. There is a `ponytail:` comment about mirroring the takes for left-handers.
  - Locomotion (`UpdateFigures`):
    - `SprintWeight = clamp((Speed - 4.5) / 1.5)`.
    - `SprintRate = clamp(Speed / SprintSpeed)`, where `SprintSpeed` is 7 m/s. This value is a guess and
      needs checking in a capture: look for foot sliding.
- `Source/CRICKET26/Cricket/Tests/AnimationTests.cpp` has a new test, `CRICKET26.Animation.StrokeClips`.

New, untracked, and must be committed: `Scripts/anim/`

- `cut_clips.sh`: cuts `friend.mp4` into short clips, one per shot. They are short because the free tier's
  monthly credits pay for about one second of video each.
- `import_anims.sh [dir]`: headless Unreal import. The default dir is `~/Downloads/mocap`. It runs
  `anim_ue.py` with `ANIM_DIR` set.
- `anim_ue.py`: handles two sources:
  - `fbx/`: DeepMotion takes. These are imported under `/Game/Anims/DeepMotion`.
  - `mixamo/`: Mixamo clips. There must be one Mixamo character, and one of the files must include the
    skin. The mesh is taken from the largest file.

  Each source is imported as `SK_<Source>`. The script then builds `IK_<Source>`, `IK_Mannequin` and
  `RTG_<Source>`, and batch-retargets onto the mannequin into `/Game/Anims/Mocap/<Name>`. The DeepMotion
  takes (only those) are then turned to face and slide the same way. The far-camera takes (Flick, Loft and
  probably Cut) turn about -173 degrees. That was checked in Blender: it is a real 180-degree camera turn,
  not a mirror.

## Assets on disk (all outside git; never commit `Content/`)

- `~/Downloads/mocap/clips/Bat_*.mp4`: the 10 cut video clips.
- `~/Downloads/mocap/fbx/Bat_{Cut,Defend_Back,Defend_Front,Drive,Drive_Cover,Flick,Loft,Pull,Punch,Sweep}.fbx`:
  DeepMotion output. Bones are Mixamo-style without a prefix, 30 fps, about 2 s each.
- `~/Downloads/mocap/mixamo/`:
  - `Run_Sprint.fbx`: 27 MB, **with skin**. This is the character mesh source. The character is Remy.
  - `Field_Dive.fbx`: without skin.
  - `Field_Throw.fbx`: without skin.
- `~/Downloads/Goalkeeper Diving Save.fbx`: the mirrored (left) dive, without skin. Move it to
  `~/Downloads/mocap/mixamo/Field_Dive_Left.fbx` and rename it by its **exact** name. Never use `ls -t | head`
  on Downloads: that once grabbed an unrelated user file.
- `~/Downloads/Bat_Cut_customModel_ucoj55mFKU8zNnT6izbssr.zip`: the original Cut download. Its FBX was
  already copied into `mocap/fbx/Bat_Cut.fbx`.
- Leave the other files in Downloads alone: `Cricket26_BasePlayer.fbx`, the older `Cricket_*.fbx` Mixamo
  exports, and the Hunyuan3D FBX `7f234717830dc560b1133f810c81a1e2.fbx`.

## Clip timings (measured in Blender; the trace script is below)

Mixamo clips are 30 fps. Blender units are about 2x metres. The character faces -Y in Blender.

- **Field_Throw** ("Baseball Pitching", 3.93 s, 119 frames):
  - Wind-up from 0 to 1.2 s.
  - The right hand peaks at about 16 units/s at **t = 1.6 s (frame 49): the release**.
  - Follow-through lasts to about 2.3 s, and the body is back at rest by about 3.0 s.
  - The hips move forward about 0.45 m.
- **Field_Dive** ("Goalkeeper Diving Save", 3.23 s, 98 frames):
  - Shuffle from 0 to 0.4 s, then the launch.
  - Dives to the character's **right**: about 3.3 m sideways and 0.4 m forward.
  - Hands at full stretch at **about 1.2 to 1.3 s**, on the ground by 1.35 to 1.4 s, lying until about
    2.1 s, standing again by about 3.2 s.
- **Mirrored dive**: the same timings, diving **left** (Blender hips X is +5.9 instead of -5.9).
- **Run_Sprint**: an in-place loop of 0.53 s.

Trace script (Blender headless). It prints the hips, head height, both hands and the right hand's speed
every 3 frames:

```python
import bpy, sys
f = sys.argv[sys.argv.index('--')+1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=f)
sc = bpy.context.scene
arm = next(o for o in sc.objects if o.type=='ARMATURE')
act = arm.animation_data.action
s, e = int(act.frame_range[0]), int(act.frame_range[1])
fps = sc.render.fps
def p(b): return arm.matrix_world @ arm.pose.bones["mixamorig:"+b].head
prev = None
for fr in range(s, e+1):
    sc.frame_set(fr)
    h, rh, lh, hd = p("Hips"), p("RightHand"), p("LeftHand"), p("Head")
    v = (rh - prev).length * fps if prev is not None else 0
    prev = rh.copy()
    if (fr - s) % 3 == 0:
        print("RESULT %3d t=%.2f hips(%.2f %.2f %.2f) head z %.2f rhand(%.2f %.2f %.2f) lhand(%.2f %.2f %.2f) rhand speed %.1f" % (fr, (fr-s)/fps, h.x, h.y, h.z, hd.z, rh.x, rh.y, rh.z, lh.x, lh.y, lh.z, v))
```

Run: `/Applications/Blender.app/Contents/MacOS/Blender -b -P mix.py -- <file.fbx> 2>/dev/null | grep RESULT`

## Next steps, in order

1. **Import everything.**
   1. Move the mirrored dive (see above).
   2. Run `Scripts/anim/import_anims.sh`, which takes about 2 to 4 minutes.
   3. Check that the log shows `ANIM retargeted` for Field_Dive, Field_Dive_Left, Field_Throw and Run_Sprint,
      and a turn angle for Bat_Cut. Cut was filmed from the far camera, so expect about -174 degrees.
2. **Build and test.**
   1. Run `Scripts/build.sh`.
   2. Run `Scripts/run_tests.sh CRICKET26.`.
   3. Check the result with `grep -c "Result={Success}" Saved/TestRun.log`. The last count was 59. Count
      again, and make sure no test fails.
3. **Check the sprint in a capture.**
   1. Run `pkill -f CricketAutoPlay; Scripts/capture.sh N`, choosing a ball where the batters run.
   2. Look at the frames for foot sliding.
   3. Tune `SuperOverGameMode::SprintSpeed` if needed.
4. **Throw clip on the fielders.** The code is in `ASuperOverGameMode::UpdatePoses`, in the fielders loop
   (around line 1730). The throw is currently procedural: `ArmCircle` IK driven by
   `BowlingArmWeight(Tt)`, where `Tt = Post - Run.ThrowRelease` (or `RelayRelease` for the relay fielder).
   - Load `Field_Throw` the same way as `SprintAnim`.
   - For the thrower or relay fielder, set `P.Clip`, `P.ClipTime` and `P.ClipWeight` with
     `ClipTime = 1.6 + Tt`, clamped to [0, clip length]. This puts the clip's release on the simulation's
     release. Fade the weight in over about 1.0 s before release and out by about 1.2 s after.
   - Before release, turn the actor so it faces `Aim` (the `Aim` vector is already computed there). The
     clip throws along the character's forward.
   - Drop or reduce the ArmCircle IK while the clip weight is up.
   - The simulation allows only about `GatherTime` (0.25 to 1.35 s) between fielding the ball and the
     release. If the gap is shorter than the wind-up, start the clip later, for example at
     `max(0.6, 1.6 - gap)`. Do not squeeze the clip.
   - Add a small pure helper in CricketPose (like `StrokeClipTime`) for the timing mapping, plus a test in
     `AnimationTests.cpp`.
5. **Dive clip.**
   - A dive happens when `Result.Fielding.bDive`, or when the action is `CatchDiving`.
   - The current placeholder is in `UpdatePresentation` (around line 1832). It tilts the whole actor toward
     `Fd.FieldPos` over `Post` in [FieldTime - 0.2, FieldTime + 0.8]. Replace that tilt with the clip.
   - Pick Field_Dive or Field_Dive_Left by the sign of the ball's side relative to the fielder's facing.
   - Play with `ClipTime = 1.25 + (Post - FieldTime)`, so the hands are at full stretch at the take.
   - The clip travels about 3.3 m sideways. The simulation's reach at full stretch is `DiveReach`: 2.3 m, or
     2.8 m for the keeper (see `FieldingModel.cpp` around line 245). Where the fielder has run to is decided
     by `PositionOf`. So either:
     - offset the actor back against the clip's root and hips travel so the hands land on `Fd.FieldPos`, or
     - accept the difference and add a `ponytail:` note.
   - The getting-up part (1.4 to 3.2 s) is about as long as the simulation's extra +0.5 s gather for a dive
     before the throw. Hand over to the throw clip, or cut the get-up short, when the throw starts.
6. **Look at captures** of Loft, Flick, Cut (new), the dive and the throw. There is no way to force a shot,
   so run several balls: `Scripts/capture.sh N [-CricketShotEvery=s]`, or use
   `-CricketDevCam=X,Y,Z,LX,LY,LZ,Fov` for a close camera.
7. **Docs.** Add the animation pipeline, its sources and their licences to `Docs/MASTER_PLAN.md` and
   `Docs/HANDOFF.md`.
8. **Commit** only `Source/`, `Docs/` and `Scripts/` (including `Scripts/anim/`).
   - Never commit `Content/`, `.serena/`, `graphify-out/`, `Saved/` or `Scripts/metahuman/__pycache__/`.
   - End the commit message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
   - Then run `graphify update .`.
9. **Close the browser tab** used for DeepMotion and Mixamo (tab 284759808, currently on Mixamo).

## Things to tell the user when done

- **DeepMotion free tier: no commercial licence.** The 10 batting takes are fine for a personal or prototype
  build. Shipping them needs a paid DeepMotion plan, or a re-capture with a tool whose licence allows it.
- **The friend's likeness and motion:** get the friend's OK before any public release.
- **Mixamo clips** (sprint, throw, both dives) are royalty-free for use in games.
- Left-handed batters still use the procedural stroke, because the takes have not been mirrored.
- About 35 DeepMotion credits were left after the Cut job (2 credits per clip).

## Coordinate frames (easy to get wrong)

- **Simulation**:
  - Metres. The bowler's end is at +X.
  - The off side is +Y for a right-hander (`OffSideSign(BatHand)`).
  - Run speed is 7 m/s (`FCricketPlayer::RunSpeed`).
- **Mannequin component space**:
  - Faces +Y.
  - The anim instance's pose targets are given in world space and converted in `FCricketAnimProxy::PreUpdate`.
- **Figures**:
  - Actors turn toward their velocity while moving (`UpdateFigures`, the `SetActorRotation` with
    `RInterpConstantTo`).
  - That turn is skipped while an actor is tilted (up vector Z < 0.95), which is how the dive placeholder
    avoids a fight over the actor's rotation.

## Done in the second session

- Two import bugs, both in `anim_ue.py`:
  - The Mixamo retargets were never saved: the batch retarget leaves its new assets unsaved, and only the DeepMotion
    loop saved. So `/Game/Anims/Mocap/Run_Sprint` and the fielding clips did not exist and the game fell back to
    the jog. The library loop now saves every asset.
  - Run_Sprint was not in place: its root travelled 3.16 m a loop, so the body would slide ahead of the actor and
    snap back. Clips named `Run_*` have their root's linear drift removed at import, which also measures the
    stride speed (6.32 m/s; `SprintSpeed` 7 became 6.3).
- `CricketPose::ThrowClip` and `DiveClip` map game time to clip time and weight (test
  `CRICKET26.Animation.FieldingClips`). `FCricketBodyPose` has two clip layers: [0] a stroke or dive, [1] a throw.
- Dive (`UpdatePresentation`): the side whose facing looks back at the incoming ball is picked, the actor is turned
  so the clip's hands reach toward the take, and stood where the dive launches. `ClipBoneAt` reads bones from the
  clip. The actor takes over the clip's travel as the clip fades out. Known gap (a `ponytail:` note): the clip's dive
  is about 3.6 m to the hands, longer than `DiveReach` (2.3 m).
- Throw: the thrower turns to the relay fielder or the stumps through the wind-up. The procedural `ArmCircle` throw
  is left only for underarm throws (and when the clip is missing). A diver's throw starts from when they land.
- `FFigureState::bHeld` stops `UpdateFigures` turning a figure toward its velocity while a clip owns its facing.
- `FDeliveryResult::HolderAt(Post)` says who has the ball; the ball is drawn in their right hand (test
  `CRICKET26.Animation.BallInHand`). Before, it sat where the simulation took it, a metre from the diver's hands.
- `-CricketDevCam=fielder` now follows the fielder's pelvis, since a dive carries the body away from the actor.
- Capture recipes (autoplay is deterministic per venue and difficulty):
  - Sprint and throw: `Scripts/capture.sh 4 -CricketDifficulty=1 -CricketDevCam=fielder -CricketShotEvery=0.05`.
  - Batters' sprint side-on: `Scripts/capture.sh 4 -CricketDifficulty=1 -CricketDevCam=0,-9,1.2,0,0,1,60`.
  - Diving catch: `Scripts/capture.sh 10 -CricketVenue=2 -CricketDifficulty=1 -CricketDevCam=fielder -CricketShotEvery=0.05`.
  - Cut: `Scripts/capture.sh 13 -CricketDifficulty=0 -CricketDevCam=kit`. Loft: `Scripts/capture.sh 2 -CricketDifficulty=2 -CricketDevCam=kit`.
- Still open: mirror the takes for left-handers; check Flick; a thrown ball starts from the take, not the hand
  (`ponytail:` note by the ball code).

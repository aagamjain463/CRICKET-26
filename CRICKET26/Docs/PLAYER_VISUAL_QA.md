# Player and Umpire Visual QA

Status: partial character-side correction. Premium overhaul is not complete. No visual gate is marked PASS without capture review.

## Protected baseline

Inspection started 2026-09-26 at commit `6ce9b2e`, branch `main`, in existing worktree. Existing foundation contains eight player MetaHumans and two umpires. All retain existing optimized Medium assembly, face identities, joint-based face rigs, fitted parametric shirts/trousers/shoes, body skeletons, leader-pose equipment, and current gameplay presentation. Each face/body has three LODs; fitted garments have four; imported equipment has one. Animation sequences, Control Rigs, bat contact, gameplay, and stadium lighting were not changed.

Identities: `MH_Home_Opener`, `MH_Home_Finisher`, `MH_Home_Allrounder`, `MH_Home_Quick`, `MH_Away_Hitter`, `MH_Away_Anchor`, `MH_Away_KeeperBat`, `MH_Away_WristSpinner`, `MH_Umpire_1`, `MH_Umpire_2`.

## QA table

| Character | Area | Current issue | Cause | Improvement | Verification | Status |
|---|---|---|---|---|---|---|
| Equipped batters and keeper | Gloves / LOD | Imported one-LOD gloves stretched into spikes when body LOD dropped finger bones | Gear/body LOD mismatch | Force synchronized LOD 0 only for two equipped batters and keeper | Matched night captures; spikes absent at LOD 0 | IMPROVED; sampled pose only |
| `MH_Away_Anchor` keeper | Pads / trousers | Red trousers penetrated keeper pads | Gear measured against bare body, fitted trousers stand farther out | Rebuilt keeper pads with 3 cm more clearance; original asset backed up | Matched night captures of red keeper | IMPROVED; other actions pending |
| Eight player identities | Batting pads / trousers | Blue trouser showed through front pad knee grooves on active opener | Standard pad shell stood too close to fitted trousers | Added 2 cm clearance to standard batting pads and rebuilt all eight existing player Gear meshes; originals backed up | Matched night opener captures and ES3.1 frame sequence | IMPROVED; sampled shot clear, other poses pending |
| Batters / umpires | Hair under helmet/hat | Umpire hair penetrated white hat crown; batter helmet coverage not yet proven | Full groom stayed visible under rigid headgear | Hide covered `Hair` component when helmet/hat gear is equipped | Matched umpire frames with and without correction; crown inspected | VERIFIED for umpire hat; batter open |
| Batter `MH_Home_Opener` | Face, skin, eyes, teeth, facial hair, neck | Close inspection incomplete | Not diagnosed | Preserve current MetaHuman assets pending evidence | Close-up capture pending | NOT VERIFIED |
| All players | Face identity / body variety | Eight identities reused across field; medium-distance identity audit incomplete | Existing roster allocation | Retain compatible identity roster; no replacement justified | Asset inventory only | NOT VERIFIED |
| All players | Hair / eyebrows / helmet interaction | No broad defect established; helmet shadow limits inspection | Unknown | Retain existing groom/card assets | Front/side capture sweep pending | NOT VERIFIED |
| All players and umpires | Jersey / trousers / footwear | Match captures show a close-fitting crew-neck shirt and jean-like trousers; silhouette reads casual, not professional cricket/officiating clothing. Shirt name and number are soft in close view. Existing knit detail and shoe fit are worth preserving. | Asset audit confirms all ten active wardrobe sets use `WI_OA_TshirtTkLngSlv`, `WI_OA_Jeans_slm`, and `WI_OA_Runningshoes`; runtime prefers these to existing custom `Kit` mesh. | Compare already-built `Kit` against active wardrobe in identical lighting and poses before changing visibility or importing anything. Keep better asset and correct only demonstrated fit/material defects. | Asset inventory plus [bowler back close-up](../Saved_PlayerVisual_bowler_hair_lod2/Screenshots/MacEditor/Ball1_004.png) and [umpire full body](../Saved_PlayerVisual_umpire2_night/Screenshots/MacEditor/Ball1_004.png); no controlled `Kit` comparison yet. | DIAGNOSED; correction pending |
| Batters | Helmet / gloves / pads / bat | Equipment present; full close-up sweep incomplete | Existing skinned attachments | Preserve role assignment and attachment system | Gameplay baseline only | NOT VERIFIED |
| Fielders | Role equipment | Full role-equipment sweep incomplete | Existing role assignment | Preserve fielder assignment; no logic rewrite | Sampled boundary fielder in `Saved_PlayerVisual_fielder_qa3`; white shoes and no batting or keeper gear visible | PARTIAL; one fielder checked |
| Bowler `MH_Away_WristSpinner` | Face, hair, beard, jersey | Close match view showed harsh hairline/scalp transition, patchy light beard region, and soft shirt print | Medium assembly mapped close hair/beard to card LOD 3, though denser card LOD 2 exists | Active bowler alone uses existing card LOD 2 at close range; no strand LOD forced | Matched bowler frames before/after; ES3.1 close-camera smoke | HAIR IMPROVED; shirt print remains soft |
| Umpires 1 and 2 | Face / clothing / hat | Near-black outfit hid fabric shape; umpire 1 face heavily shadowed in daylight | Outfit tint and hat occlusion under sun angle | Retained narrower clean-crown hats; changed existing outfit tint to charcoal slate | Matched umpire 1 daylight frames, umpire 1 night close-up, umpire 2 night profile/full body | IMPROVED; daylight eye gate remains open |
| All characters | Sweat response | No diagnosed material defect | Unknown | Do not invent uniform gloss or unsupported parameter system | Floodlight comparison pending | NOT VERIFIED |
| All characters | LOD / deformation / mobile cost | Imported gear has one LOD; body has three | LOD mismatch | Three equipped actors use full pose; others retain automatic LOD | Metal captures and ES3.1 editor smoke reached gameplay | PARTIAL; mobile device validation pending |

## Pass 10: close-up sweep of every figure

Method: `-CricketDevLook=all,head,1.5,0,35 -CricketShotEvery=1` steps the camera through all 16 figures, one per saved frame. Frame N shows figure N+1, because the index advances before the frame is drawn. The run was repeated at Quality 1, Quality 2 and `-FeatureLevelES31`. Frames and contact sheets are in `Saved/PlayerVisualQA/pass10/`.

| Defect | Root cause (verified) | Fix | Regression check |
|---|---|---|---|
| Shaded faces pitch black below Quality 2 | Screen-space AO radius of 120 cm. A bisect showed faces clear at 30 cm. | `SetAmbientOcclusion`: radius 30 cm, intensity 0.65 | `CRICKET26.Presentation.AmbientOcclusionHeadScale` |
| Faces black on ES3.1 | The mobile renderer cannot capture the sky light in real time, so the sky contributed nothing | Captured sky below SM5, with `RecaptureSky()` after the sky dome is built | ES3.1 sheet `es31_sky_sheet.jpg` against `es31_sheet.jpg` |
| Umpire scalp through the hat crown | Fixed crown radii of 10 × 11.5 × 9 cm. Umpire 2's scalp top stood 0.3 cm above the crown. | `make_kit.py` fits the crown to the exported face mesh. Crowns are now 10.4 × 12.0 × 12.5 cm and 10.7 × 12.3 × 12.9 cm. | `KIT hat crown` line in the build output; `hatfit.jpg` |
| Players' shaded sides and backs black at Quality 0 to 2 | The sky light was captured in real time, which lights nothing without Lumen; at Epic, Lumen hid it. Raising the sky light fivefold changed nothing, and switching distance-field AO off did not help. | `SpawnSky` captures the sky once on every tier, as phones already did | `CRICKET26.Presentation.SkyLightsShade`; bowler sheets at Quality 1, 2 and 3 |
| Trousers read as jeans: leather back-pocket label, brass rivets | The only free trousers are `WI_OA_Jeans_slm` | `trouser_stripe.py` repaints the jeans' mask: the label gives way to team-accent piping down each outseam, and the rivets take the cloth's colour | `CRICKET26.Presentation.TrouserPiping`; side and back captures |
| Fielders cloned the active striker, non-striker and bowler | The fielder roster indexed the whole `Players[]` list | Fielders draw only from identities 3 onwards | Contact sheets |
| Fielder's mouth dragged into a tube down the neck on the first frame of a cut (replay start) | The `Face` component ticked only when rendered, so its bones were stale on the first frame after a camera cut. The body already refreshed off screen. | Face uses `AlwaysTickPoseAndRefreshBones` | `FigureProblems` flags "face bones go stale off screen"; covered by `CRICKET26.Presentation.FigureRoleGear` |
| No loud failure for missing or misassigned assets | None existed | `FigureProblems` runs at match start and logs `Figure check FAILED: <figure>: <problem>`. It checks for: no body, body not driven by `UCricketAnimInstance`, no face, stale face, empty material slot, gear on the wrong role, missing role gear, and grooms disabled. | `FigureRoleGear` and `HairStaysOn` tests; the live log shows "Figure check passed: 15 bodies" |

Diagnosed, not changed (needs lighting judgment):
- Eye whites and the undersides of umpire hat brims pick up green in shade at the Lumen tiers. The source is physical bounce light from the turf, whose linear albedo is about (0.10, 0.19, 0.05). The eye materials and slots are intact. Lowering turf albedo or indirect intensity would change the whole stadium look.
- The striker's face behind the helmet grille stays very dark on ES3.1.
- The bowler's beard reads as a dark mass at Quality 1.
- The hat brim casts a jagged shadow on chins at Quality 1.

Open, not attempted in this pass: keeper gloves still read as lumpy mittens. Trousers are still the MetaHuman jeans cut, with front pockets and yoke seams, now plain team cloth with piping. The helmet shell is a plain team colour with no badge. Long hair falls to a helmet-like card LOD at mid range.

## Baseline evidence

Original run: 1280x720, default quality, venue 0, AI autoplay, delivery 1. Comparable night runs: 1280x720, quality 1, venue 2, delivery 1, `CricketDevCam=kit`.

| Night run | GPU average/p99 | Frame average/p99 | Visible result |
|---|---:|---:|---|
| Before correction | 10.3 / 20.1 ms | 23.7 / 276.6 ms | Glove spikes; keeper pad penetration |
| Selective LOD 0 | 10.2 / 18.3 ms | 22.1 / 249.2 ms | Gloves hold shape; pad overlap remains |
| Final keeper pad mesh | 10.4 / 18.3 ms | 22.2 / 266.2 ms | Gloves hold shape; pad overlap smaller |

These one-delivery desktop Metal captures include screenshot-readback/loading hitches. They do not establish mobile performance.

Evidence: [before night](../Saved_PlayerVisual_before_night/Screenshots/MacEditor/Ball1_003.png), [after glove LOD](../Saved_PlayerVisual_after_night/Screenshots/MacEditor/Ball1_003.png), [after keeper pad](../Saved_PlayerVisual_after_pad2/Screenshots/MacEditor/Ball1_003.png), [after batting pad clearance](../Saved_PlayerVisual_batter_pad_clearance2b/Screenshots/MacEditor/Ball1_003.png), [ES3.1 batting gear](../Saved_PlayerVisual_final_mobile_gear/Screenshots/MacEditor/Ball1_003.png), [bowler before](../Saved_PlayerVisual_bowler_qa/Screenshots/MacEditor/Ball1_003.png), [bowler hair after](../Saved_PlayerVisual_bowler_hair_lod2/Screenshots/MacEditor/Ball1_003.png), [umpire before hat](../Saved_PlayerVisual_umpire_after/Screenshots/MacEditor/Ball1_002.png), [umpire after hat](../Saved_PlayerVisual_umpire_after_hat/Screenshots/MacEditor/Ball1_002.png).

Further match captures: [umpire slate daylight](../Saved_PlayerVisual_umpire_slate/Screenshots/MacEditor/Ball1_002.png), [umpire slate floodlights](../Saved_PlayerVisual_umpire_slate_night/Screenshots/MacEditor/Ball1_002.png), [umpire 2 profile](../Saved_PlayerVisual_umpire2_night/Screenshots/MacEditor/Ball1_004.png), [boundary fielder](../Saved_PlayerVisual_fielder_qa3/Screenshots/MacEditor/Ball1_008.png).

## Hat comparison

Matched daylight frames retain rebuilt narrower-brim umpire hat. Corrected capture shows no hair through crown. Original brim width remains backed up under `Saved/PlayerVisualQA/baseline_assets/`. Umpire face remains shadowed by match lighting; no lighting asset changed.

## Mobile smoke

Earlier `-FeatureLevelES31` run exited before gameplay because concurrent Frontend work lacked `Widgets/FrontendRoot.h`. After that file appeared, editor build succeeded and a fresh Mac Metal ES3.1 smoke reached gameplay and exited normally. Over 694 frames it reported frame average/p99 20.1/82.0 ms and GPU 7.5/13.9 ms. This is an editor feature-level smoke on Apple M5, not a mobile-device benchmark. Mobile FPS, memory, groom, and skeletal-mesh costs remain unmeasured.

After pad and bowler changes, a 1280x720 Metal ES3.1 match capture completed normally. It reported 1499 frames, frame average/p99 9.2/25.6 ms and GPU 6.5/7.8 ms. Cache state and capture work differ from earlier runs, so these numbers are smoke data, not an isolated before/after cost. Android SDK setup and physical-device profiling remain open.

## Rejected trial

An extra 1 cm of keeper pad clearance did not resolve the blue openings because those openings belonged to the batter's standard pads. The keeper generator and asset were restored to the prior 3 cm correction. The rejected frame is in `Saved_PlayerVisual_keeper_clearance4/`; restored asset and FBX backups are under `Saved/PlayerVisualQA/baseline_assets/`.

An existing Unreal distance field font was tested for the soft shirt print. It rendered rectangular glyph blocks in the Canvas texture, so the original font was restored. Rejected frame: `Saved_PlayerVisual_bowler_print_df2/Screenshots/MacEditor/Ball1_004.png`.

## Preserved systems

No animation-owned files modified. No skeleton replacement, retargeting, animation Blueprint, Control Rig, gameplay state, shot selection, bowling logic, ball physics, or stadium-lighting rewrite. Existing MetaHuman assets remain foundation.

## Files changed for character visuals

- `Scripts/metahuman/make_kit.py`: targeted pad clearance and umpire brim geometry.
- `Scripts/metahuman/kit_ue.py`: targeted Gear, Keeper, and Hat reimport steps.
- `Scripts/metahuman/audit_visuals.py`: read-only groom LOD inventory.
- `Source/CRICKET26/Cricket/SuperOverGameMode.cpp`: selective equipped-gear pose LOD, covered-hair visibility, and active bowler card LOD. This shared file also contains concurrent presentation edits outside this pass.
- Eight `Content/MetaHumans/MH_*/Kit/SKM_*_Gear.uasset` player Gear meshes, `MH_Away_Anchor` Keeper mesh, and both umpire Hat meshes. Originals are backed up in `Saved/PlayerVisualQA/baseline_assets/`.

No animation-owned files modified for this visual pass.

## Pass 11: premium face overhaul (no cloud rebuild)

Method: `Scripts/metahuman/premium_faces.py` retunes baked material instances in place (903 tweaks, 0 failures);
`Scripts/metahuman/premium_faces.sh` reruns it. `Scripts/metahuman/make_players.py` locks short sport cuts and
boxed/stubble beards (`SPORT_BEARD` + `facial()`) for the next cloud rebuild. No geometry, rig, texture, or
wardrobe asset replaced; identities and presets unchanged.

| Defect | Root cause (verified) | Fix | Regression check |
|---|---|---|---|
| Eyes glowed green/milky in close-ups | `Cloudy Eye Intensity` up to 2.0, `Pupil Dilation` 0.95 (iris hidden), `Iris Global Saturation` 1.7 (neon), `Sclera Transmission Spread` 0.12 bleeding green turf bounce | Cloudy 0.35, dilation 0.45, saturation 1.15, transmission 0.08, sharper cornea 0.05, reflection 0.25, vein detail 0.22 | `CRICKET26.Presentation.FacePremiumFill`; close captures show dark brown irises |
| Eye whites/hat brims picked up green in shade | Day lower-hemisphere fill `(0.24, 0.28, 0.18)` was green-dominant | Warm neutral `(0.27, 0.245, 0.185)`, same energy | `FacePremiumFill` fails if fill ever goes green-dominant again |
| Skin read waxy/flat at 1.5 m | Micro normal 0.7 with tiling 44 (pores invisible), flattened normals, weak AO | Micro normal 1.0, tiling 36, flatten 0.25, AO power 1.15 / strength 1.1, SSS 0.9, specular 1.0 | In-engine verify of all 10 identities; `PremiumVerify.log` 10/10 |
| Lashes looked brown/ginger | Melanin 0.3, redness 0.28 | Near-black 0.85 / 0.12, roughness 0.35 | Same verify |
| Teeth grey/plastic | Value 0.75, roughness 0.17, micro 0.2 | Value 0.82, roughness 0.22, micro 0.35 | Same verify |
| Hair read brown/straw | Scalp `hairMelanin` 0.8, roughness 0.61 | Near-black 0.92/0.88, roughness 0.42/0.5 (umpires keep grey) | Same verify; dense cards baked below |
| Close hair/beard thin | Face LOD mapped to sparse card LOD 3 in blueprints | Baked dense card LOD 2 for Hair/Beard/Mustache/Eyebrows on all 10 blueprints (runtime already did this live) | `FigureRoleGear` + `HairStaysOn` still pass |

Verification: editor build `Result: Succeeded`; `Scripts/run_tests.sh CRICKET26.Presentation.` **9/9 pass, 0 fail**
(includes new `FacePremiumFill`); close captures (`Saved/Screenshots/MacEditor/Ball1_000.png` opener with full
sport cut, Ball1_006 keeper-bat with full hair/moustache) show natural dark eyes, pore detail, deep sockets.
Pre-existing issues reproduced but untouched: `MH_Away_Hitter` has no `Kit/` meshes (non-striker wears no gear);
non-pitch faces tick only when rendered (`face bones go stale` self-check lines).

Rejected trial: repointing `MH_Away_Hitter` / `MH_Umpire_2` placeholder hair (`GroomAsset_0`, an empty import)
at Epic's pristine BrushCut groom did not restore any hair (binding/cards built from the empty source), so both
blueprints were restored to the placeholder. Those two scalps need a genuine cloud rebuild.

## Remaining gates

Face individuality, neck seams, body variety, garment microdetail, umpire close-up, each role's full equipment sweep
(including `MH_Away_Hitter`'s missing pads/gloves/helmet), deformation across requested actions, and close-camera LOD
transitions remain open. `MH_Away_Hitter` and `MH_Umpire_2` scalps need a genuine cloud rebuild (placeholder
`GroomAsset_0`); long/preset cuts (`PulledBack`, `Layered`, `BobLayered`, `HairLoss`) and thin beards
(`PencilThin`, `MuttonChops`, `ChinStrap`, `SoulpatchStrip`) retire on the next rebuild via `SPORT_BEARD`.
Skin roughness/microdetail, eye response, lashes and teeth are now CLOSED for the baked-material tier by Pass 11.
Desktop captures cannot establish device performance. No Cricket 24 assets, likenesses, shaders, textures, or code are used.

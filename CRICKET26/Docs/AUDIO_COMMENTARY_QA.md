# AUDIO + COMMENTARY QA

Overhaul: central `CricketAudioDirector` + `CricketCommentaryDirector` consuming semantic gameplay
events (gameplay owns score/wickets/physics; audio only describes). Baseline captured 2026-09-26 by
code inspection (headless `-nullrhi` cannot boot the scene: pre-existing `ShirtPrint` font crash,
unrelated to audio).

Baseline was: 5 procedural cues (`BatCrack/EdgeTick/Bounce/Stumps/Crowd`), one shared ball channel
(new cue cut the last), crowd bed 0.3 bumped to 1.0/0.8 on six/four/wicket with linear decay, and one
caption line per delivery rotating 2-3 phrasings (`Variant = BallsPlayed`). No roles, memory,
priorities, queue, silence, intensity, footsteps, keeper/throw/pad sounds, ducking or buses.

| Area | Previous Problem | Root Cause | Fix | Verification | Status |
|---|---|---|---|---|---|
| Commentary architecture | Triggers scattered (`Describe` call in `ScoreDelivery`), no director | No central system | `CricketCommentaryDirector`: context, intensity, priority, queue, memory, handoffs | `CRICKET26.Commentary.*` (6 tests) pass | Done |
| Commentary roles | Single voice, every ball narrated | No speaker model | Play-by-play + Analyst (`ESpeaker`), analyst only as post-moment handoff (50%, never over the call) | StaleInterrupt + soak | Done |
| Silence | Caption on every ball | No silence path | `bSilent` on routine balls (dot ~35%, single ~20%); speech delays (six 1.0s, wicket 0.8s, four 0.6s) | DirectorSoak: 7/60 silent, silence-only-when-routine | Done |
| Emotion | Flat rotation, six = dot energetically | No intensity model | `ComputeIntensity` from chase math (6-off-last = 1.0, 1-off-last = 0.95) + event base + momentum | Intensity test | Done |
| Anti-repetition | 2-3 variants, word-for-word repeats | Pure rotation | 90+ line ids, cooldowns, recent-id/opening/topic penalties, opening-step avoidance | Repetition (12 sixes, no back-to-back, >=5 distinct) + soak trailing-4 | Done |
| Correctness | Caught-behind read like bowled; dive unpraised | No authority filtering | Candidate filtering: edge/dive/middled/mistimed/direct-hit/close called only with sim evidence; victim naming incl. run-outs | Correctness test | Done |
| Priorities/interrupts | Analysis could overlap wickets | No queue | P0-P4, wicket (P3/P4) interrupts breakable current + pending analyst; stale queue dropped | StaleInterrupt test | Done |
| Match end | Generic suffix only | No result calls | `ResultLines` (chase/final-ball/defense/tie), tie never announces a winner | Soak result assertions, 6 games | Done |
| Pronunciation | No layer; TTS would guess names | Nothing stored | `Lexicon()` + `SpokenFor` + `HasPronunciation` (fallback = display, flagged) | AssetValidation test | Done |
| Bat sounds | One crack for all contacts | No contact routing | `BatMiddle/BatToe` + `SelectBatCue(Zone, Quality)` + `BatVolume`; distinct DSP per family | AssetValidation (distinct PCM, mapping, louder-middle) | Done |
| Pitch/pad/keeper/catch/throw | Missing entirely | No cues/hooks | `Bounce` w/ `PitchVolume(SpeedKph)`, `PadThud` at pad-impact time, `KeeperGlove`/`CatchPop` at take time, `ThrowRelease`, `Footstep` run-up strides, `Stumps` at `BrokenTime` for run-outs | Director test + soak (finite, unclipped) | Done |
| Player vocals | Missing | No system | `EVocal` scheduler (`HOWZAT/RUN/NO/WAIT/catch-call/celebrate/frustrated`) with timing hooks; samples pending | Director vocal tests | Placeholder samples |
| Crowd | One loop, instant 0.3->1.0 cuts | Direct level sets | Energy model: fast attack (1.5/s), slow release (0.25/s), pre-delivery tension, release micro-drop, victory sustain, duck 0.8, replay x0.6 | Director swell/duck/replay tests | Done (procedural bed) |
| Mix | Two channels, no structure | No buses | Logical `EMixBus` trims (commentary/impacts top, ambience low); maps 1:1 to future submixes | AssetValidation | Logical only |
| Replay audio | Identical re-loud mix | No treatment | Crowd x0.6 in replay, contact floor 0.7, cues re-fire on rewind (existing crossing) | Director test | Done |
| Mobile | Unmeasured | No budget | 22050 Hz mono 16-bit; all cues ~234 KB; crowd streams in 2 s chunks; concurrency gaps | AssetValidation + build | Done |
| Voice/TTS | None | No pipeline | `Scripts/audio/VOICE_PIPELINE.md`: eval set of 8, provider criteria, no cloning rule; no files generated yet | Doc | Pipeline only |
| Listening QA | Not done here | No display on this machine | Procedure + acceptance scenarios in pipeline doc; `-CricketRecordAudio` captures BallN.wav for A/B | Automated only | Open |

## Automated results (2026-09-26, full `CRICKET26.` suite)

`PASS: 76 FAIL: 1` — the single failure is `CRICKET26.Touch.Controls` (expects 11 touch buttons,
code lays out 10; `CricketTouch::Layout`, untouched by this overhaul — other workstream's arithmetic).
New tests: `Audio.AssetValidation`, `Audio.Director`, `Commentary.Correctness`,
`Commentary.Repetition`, `Commentary.StaleInterrupt`, `Commentary.Intensity`,
`Commentary.DirectorSoak` (6 full AI Super Overs: 60 balls, 53 spoken, 7 silent).
Legacy `Commentary.Lines` and `Audio.CueSynthesis` still pass unchanged.

## Remaining placeholders (explicit)

1. Voiced commentary recordings (library + metadata + selection ship; evaluation set defined, no TTS run yet).
2. Studio SFX/crowd/vocal samples (procedural cues keep exact ids/selection so swap is cue-for-cue).
3. Unreal submixes/SoundClasses (logical trims ship; `EMixBus` maps 1:1).
4. Result/menu music (no music under live cricket by design).
5. Headphone/speaker listening pass + baseline BallN.wav A/B (procedure documented; needs a GPU machine).

## Files

- `Source/CRICKET26/Cricket/CricketAudioDirector.h/.cpp` (new)
- `Source/CRICKET26/Cricket/CricketCommentaryDirector.h/.cpp` (new)
- `CricketAudio.h/.cpp` (7 new cue families, buses, validation)
- `CricketCommentary.h/.cpp` (90+ lines, metadata, lexicon, authority filtering)
- `SuperOverGameMode.h/.cpp` (director wiring, timing-accurate cues, crowd tick)
- `Tests/PresentationTests.cpp` (7 new tests)
- Incidental unblock fixes in co-existing workstream files: `CricketReplayBuffer.h` (illegal
  `UPROPERTY` on `TArray<TPair>`), `CricketBroadcast.h` (namespace-scoped `struct` elaborators),
  `FieldingModel.cpp` (`-Wshadow` renames, behavior-preserving), `BroadcastTests.cpp` (tune arg).

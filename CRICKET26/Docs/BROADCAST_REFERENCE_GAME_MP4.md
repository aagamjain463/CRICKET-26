# Broadcast reference: `Docs/game.mp4`

The source is `Docs/game.mp4`, 25:42 of first-hand Cricket 26 gameplay (Big Ant Studios): India v Australia T20, a clean feed with no commentary, 640x360 at 30 fps.

How it was read:

- Every second was sampled into contact sheets.
- Hard cuts were found from scene-change spikes: 285 of them, with a median shot of 3.7 s.
- Each transition and graphic was re-read at 10 fps and at full resolution.

The timestamps below are video time (m:ss).

This document records what the broadcast **does**: shot grammar, beat order, timing, transitions and the information each graphic carries. It is used only as a reference. No art, logos, UI textures, audio or data from the video are used. The stingers, cards and banners in this project are drawn in code in the CRICKET 26 design system (`Docs/CRICKET26_DESIGN_SYSTEM.md`), with the project's own logo and crests.

## 1. The shape of a ball

Every ball follows the same arc. Only the middle changes with what happened.

```
live delivery -> live follow (reverse angle) -> dead ball
  -> [event beats: umpire signal, reactions, celebration, crowd]
  -> event stinger (word + crest) -> replay (1-3 angles) -> logo stinger
  -> [wicket: dismissed batter walks off under the dismissal card]
  -> [over done: THIS OVER graphic -> crowd -> new bowler under the bowler card]
  -> [new batter: walks in under the batter card]
  -> hard cut to the delivery camera for the next ball
```

A dot ball or single skips the middle: 1-3 s of reaction (a bowler close-up, a fielder returning the ball, a batter close-up), then the delivery camera. The scorebug is hidden on every close-up and scene shot. It shows only on the delivery and live-follow cameras.

## 2. Beat catalogue (with measured durations)

| Beat | Where seen | Duration | Framing |
|---|---|---|---|
| Toss: captains two-shot + pitch-conditions panel | 0:18-0:21 | 3 s | waist-up two-shot at the pitch, crowd soft behind; panel: pitch hardness, type, age, cracks, temperature, wind |
| Toss: coin flipped, spins in the air, lands big in frame | 0:21-0:28 | 7 s | fixed camera at mid-pitch, stands out of focus; call panel (BAT / BOWL) with a countdown, then the result line |
| Crowd cutaways before the first ball | 0:29-0:31, 6:36, 17:07, 17:47 | 1-3 s each | a stand section from inside the ground, medium lens, shallow focus |
| Umpire FOUR signal (arm swept across the body) | 0:45, 1:08, 12:39, 13:01, 16:05, 17:19, 21:41, 23:41 | 2.5-3 s | waist-up front or three-quarter, often with the bowler or batter passing in the foreground |
| Umpire SIX signal (both arms raised, slowly) | 2:23, 3:20, 4:47, 7:31, 9:20, 10:26, 18:37, 22:47, 24:26 | 2.5-3.5 s | front or low front; the arms go up over about a second, hold, then the hands come down |
| Umpire OUT (finger raised) | 5:23, 22:13 | 3.5-4.5 s | waist-up, slow push-in; the finger rises over about a second |
| Umpire WIDE / NO-BALL / bouncer (arms out, one arm, tap on the shoulder) | 1:09, 1:33-1:35, 11:44, 12:31, 17:39 | 1.5-2.5 s | medium close, crowd behind |
| Third-umpire referral (umpire draws the TV box) | 8:30 | 1.5 s | medium |
| Six lands: the follow camera carries into the stands, pyro jets at the boards | 2:43-2:46, 12:38, 24:50 | 1.5-3 s | reverse angle held into the crowd; flame jets for about 0.4 s |
| Batter reaction after a boundary | 2:04-2:10, 2:47-2:51, 11:58-12:03, 12:16-12:19, 24:51-24:55 | 3-5 s | low wide-angle close-up (sky and big screen behind), or a tight close-up with a fielder behind |
| Batter admiring the six from above | 3:43-3:46, 6:12-6:20, 21:57-22:00 | 3-8 s | high crane behind the striker, then medium from behind |
| Bowler reaction after runs or a boundary | 1:48-1:51, 16:28-16:31, 23:21-23:25 | 3-4 s | medium close walking back, low wide lens, frustration gestures |
| Fielder close-up after the stop near the rope | 7:15-7:19, 8:14-8:17 | 3-4 s | medium close, crowd behind |
| Captain and bowler talk | 6:58-7:03, 23:53, 24:06-24:09 | 3-5 s | over-the-shoulder two-shot |
| Wicket: live WICKET strip | 0:12, 9:39, 19:05 | 2-3 s | full-width band in the bowling side's colour replaces the scorebug, the word in the middle and faint repeats scrolling either side |
| Wicket: umpire OUT, crowd, then the celebration | 5:23-5:31, 8:45-8:52, 9:42-9:51, 19:08-19:16, 22:13-22:19 | 6-9 s | umpire; crowd close (shallow focus); hero plus team-mate two-shot (fist pump, hug); group high-fives |
| Wicket: broken stumps close-up | 9:52, 19:17 | 1.5-2 s | stumps at knee height, crowd blurred |
| Wicket: the batters confer (review decision) | 5:32-5:35, 22:22-22:25 | 3-4 s | over-the-shoulder two-shot mid-pitch, with the review countdown panel |
| Dismissed batter walks off under the dismissal card | 5:48-5:50, 8:53-8:57, 10:01-10:02, 19:32-19:33, 22:32-22:34 | 2.5-4 s | tight tracking close-up, crowd behind; card: name, how out, R, minutes, balls, SR, 4s, 6s, fall of wicket |
| New batter walks in under the batter card | 5:55-5:59, 9:02-9:07, 10:07-10:11, 19:35-19:38, 22:34-22:36 | 3-5 s | high wide from behind and above, shadow swings, or front medium tracking back; card: matches, runs, average, SR, 50s, 100s, HS |
| End of over: THIS OVER | 3:00-3:02, 10:36, 15:48, 17:56, 20:55, 22:56, 25:23 | 2-3 s | from behind the far stumps along the pitch, length bands painted on it (full to short, one colour each); a panel of the over's six balls as coloured, numbered dots; the over's ball paths drawn in the air |
| New bowler walks back under the bowler card | 3:03-3:05, 5:10-5:12, 7:43-7:45, 10:38-10:42, 15:49-15:53, 18:00-18:04, 20:58-21:02, 23:01-23:03 | 3-4 s | medium or low wide, walking or rubbing the ball; card: matches, wickets, runs, average, 5WI, 10WM, best |
| Innings end: not-out pair walk off together | 13:10-13:13 | 3-4 s | front, tracking back, fielders walking alongside |
| Innings start: opener card, opening bowler card | 13:20-13:27 | 3-4 s each | close-up; low wide at the top of the mark |
| Match end: winning captain close-up | 25:18-25:22 | 4 s | medium close, crowd blurred |
| Match end: team huddle, crowd, handshake | 25:24-25:34 | 10 s | close group; wide crowd; bowler and batter shake hands; result bar across the bottom |

The umpire gets more screen time than anyone except the batter. Every boundary and wicket is signalled on camera before the replay.

## 3. Transitions (read at 10 fps)

**Event stinger into a replay** (1:10.6, 3:22.6, 2:51.2):

1. The live or scene shot dissolves for about 0.2 s. Frames 3-4 are a double exposure.
2. Two solid bands close in from the top and the bottom at a slight tilt, in about 0.3 s. They squeeze the picture to a thin slanted slit, then shut.
3. On the solid card, the event word (FOUR, SIX, WICKET) fades and scales up under the batting side's crest, in about 0.3 s.
4. The word grows while the card dissolves into the first replay frame, in about 0.15 s.

Total: about 1.0 s. A variant (2:52, 7:36, 16:32) skips step 2. The huge word dissolves straight over the replay's first frame.

**Logo stinger out of a replay** (3:28.6, 4:33, 5:47, 12:26):

- The bands close the same way over the last replay frame, in about 0.3 s.
- The game logo scales up on the card, in about 0.3 s.
- The logo keeps growing while the card dissolves into the next live shot, in about 0.2 s.

Total: about 0.8 s.

**Scene cuts**: every change of camera is a hard cut. A dissolve (0.3-0.5 s) is used only into and out of a crowd shot, and from the umpire into a replay.

**Push-in**: close-ups and the umpire shots creep in slowly, about 5-10% of the lens over the shot. The camera is never locked off.

**Depth of field**: every close-up and crowd shot has a shallow focus, with the stands soft.

## 4. Replay grammar

| Event | Angles in order | Seen at |
|---|---|---|
| FOUR | one long angle, 6-9 s: behind the bowler at waist height tracking the bowler, then down the pitch with the ball to the batter (a dolly); *or* side-on from square leg at batter height; *or* a long lens from the far stand following the ball to the rope | 1:12-1:20, 4:27-4:32, 12:43-12:48, 16:09-16:15, 17:23-17:27, 24:00-24:04 |
| SIX | ground-level from the bowler's end, wide lens, batter and sky, or side-on from square leg; then the follow into the stands, tilting up past the roof to the floodlights | 2:53-2:59, 3:24-3:28, 3:48-3:53, 7:37-7:41, 10:31-10:34, 11:15-11:18, 18:42-18:44, 21:14-21:16, 23:16-23:19 |
| BOWLED | behind the bowler, low, the camera travelling down the pitch with the ball into the stumps; then side-on at stump height, super slow, bails flying | 19:20-19:31 |
| CAUGHT | the delivery from a wide low angle; side-on on the stroke; the follow to the fielder; the catch | 5:37-5:46, 22:27-22:30 |
| RUN OUT / STUMPING (third umpire) | side-on square to the crease, looped slow; then the big screen with the decision | 8:32-8:44 |

The replay speed is near real time through the run-up and the flight. It slows (to about 0.3-0.5x) through contact or the stumps, then recovers. The score bar is not shown during a replay. No "REPLAY" tag is shown: the stingers are the only marking.

## 5. Graphics

- **Scorebug**: a full-width bottom bar. It carries the batting crest, two batters with runs and balls, score and overs, run rate or the equation, the bowler's figures and this over's balls, and the bowling crest. After contact it swaps to shot feedback (footwork, timing, shot choice) and the delivery speed.
- **Live event strip**: WICKET, and on some balls FOUR or SIX, replaces the scorebug for 2-3 s.
- **Lower-third player cards**: a crest block on the left, the name on a coloured header bar, and a white row of labelled stats. They slide in as an empty bar (about 0.3 s) then fill. They hold for the length of the shot and sit over the bottom third.
- **In-stadium ribbon boards** show the live score and a clock (11:09, 22:46). The big screens show the replay or the decision pending (8:34-8:44).
- **Result bar**: "INDIA WON BY 16 RUNS" in the winner's colour across the bottom, over the celebration shots (25:24-25:37).

## 6. What this project builds from it

See `Docs/BROADCAST_SEQUENCE.md` for the implementation, and for what differs from this reference and why.

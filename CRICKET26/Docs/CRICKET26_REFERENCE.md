# Cricket 26 Reference Matrix — Super Over Vertical Slice

Purpose: compare this project against *observable* behaviour of Cricket 26 (Big Ant Studios) so gaps are tracked honestly.

**Rule for this document:** a Cricket 26 entry is only filled in when it has been observed first-hand (own gameplay capture or a cited public video with timestamp). Nothing here was captured yet, so every Cricket 26 cell reads **UNVERIFIED**. Do not replace it with memory or assumption. No proprietary code, assets, UI art, audio or data from Cricket 26 are used in this project.

Status key: **DONE** (built and covered by automated tests), **PLAYABLE** (in game, only manually/smoke verified), **PLACEHOLDER** (isolated stand-in), **MISSING**.

| System | Cricket 26 Observable Reference | Current Game | Target | Status |
|---|---|---|---|---|
| Super Over rules | UNVERIFIED | 6 legal balls, 2 wickets end innings, wides/no-balls not legal and cost 1, free hit after no-ball (carries over a wide), target = first innings + 1, chase ends on reaching target, tie → another Super Over. `FSuperOverMatch` with invariant checks. | Laws/playing conditions as used in T20 internationals | DONE (9 rules tests) |
| Delivery physics | UNVERIFIED | 240 Hz integrator: gravity, quadratic drag, Magnus, conventional swing, seam kick, spin turn via friction-pyramid bounce, pitch vs outfield surfaces. Release solver pitches the ball where aimed (bisection, no lob branch). | Plausible real-world trajectories for pace and spin | DONE (5 ball tests incl. 3,960-case release sweep) |
| Bowling (human) | UNVERIFIED | Choose delivery type (1–5), move pitch target, timed release meter; early/late release biases length and widens scatter; overstep = no-ball. | Readable, skill-based bowling | PLAYABLE |
| Bowling AI | UNVERIFIED | Weighted plans per bowler type (yorker, wide yorker, slower, bouncer, hard length, swing / spin variations), avoids repetition, squeezes in the chase, executes through the same noisy release as a human. No access to the batter's input. | Match-situation-aware, non-cheating | DONE (AI soak test) |
| Batting (human) | UNVERIFIED | Direction from held keys, intent J/K/L (ground/loft/defend), press timing decides contact; resolved deterministically through bat geometry — edges, toe, splice, inner/outer half come from where the ball meets the blade, not dice. | Timing + placement + shot selection with geometric outcomes | PLAYABLE (4 batting tests on the model) |
| Batting AI | UNVERIFIED | Reads the delivery 0.35 s before arrival from observed flight only, picks intent from line/length/aggression, finds gaps in the actual field, has human-like timing error. | Non-cheating, situation-aware | DONE (AI soak test) |
| Dismissals | UNVERIFIED | Bowled (incl. played on), LBW with ball tracking (pitched outside leg, impact in line, no-shot rule), caught (catch difficulty from reach/pace/dive), run out (throw vs running race; crossed-batters law). Not yet: stumped, hit wicket, obstructing. | All common modes | PARTIAL |
| Fielding | UNVERIFIED | Intercept solver over the ball path per fielder (reaction, acceleration, dive reach), keeper standing up/back, catches, throws, run-outs. T20 presets: pace death / spin defensive, max 5 outside the circle. | Intercepts, catches, run-outs, T20 presets | DONE (2 fielding tests) |
| Running | UNVERIFIED | Runs decided by the race between batters and the throw with a risk margin; human cycles safe / normal / aggressive (R). Byes/leg byes are not run. | Full running with calls | PARTIAL |
| Match flow / HUD | UNVERIFIED | Canvas score bug, batters and bowler figures, this-over log, "N REQUIRED FROM M" pressure line, event banners (FOUR/SIX/WICKET/WIDE/NO BALL/FREE HIT/TARGET/result), innings break and result prompts. | Broadcast-style presentation | PLACEHOLDER (original, own design) |
| Camera | UNVERIFIED | Telephoto behind the bowler for the delivery, eased wide follow after contact. | Broadcast camera set with replays | PLACEHOLDER |
| Stadium / characters / animation | UNVERIFIED | Engine basic shapes: cylinders for players, box bat, slabs for stands. No skeletal animation. | Authored assets and animation | PLACEHOLDER |
| Audio / commentary | UNVERIFIED | Semantic event delegate (`OnCricketEvent`) exists; no audio. | Crowd, bat/ball, commentary | MISSING |
| Touch / mobile input | UNVERIFIED | Keyboard only. | Touch controls | MISSING |
| Debug tools | n/a | F1 overlay (plan, release, contact zone, timing error, fielding, running, seed), F2 flip striker hand, F3 cycle bowler type, F4 trajectory draw, F5 force wicket, F8 AI vs AI. `-CricketAutoPlay` for soak runs. | — | DONE |

## How to fill a Cricket 26 cell
1. Record gameplay from a licensed copy (or cite a public video with URL + timestamp).
2. Describe only what is visible or measurable (e.g. "ball count resets after a wide: yes/no", "free hit shown on HUD: yes/no").
3. Put the source in the cell. Keep it factual; do not copy UI art or text.

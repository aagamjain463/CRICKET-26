# VOICE PIPELINE — commentary recordings

No audio has been generated yet. This document is the procedure to follow before spending
credits or shipping a single clip. Nothing here clones a real commentator: both voices are
original identities (see casting). Verify provider licensing for commercial use before relying
on it; do not assume any provider is free.

## Casting (original, no impersonation)

- **Voice A — play-by-play.** Energetic professional broadcast cadence, faster reactions, lifts
  genuinely on big moments, never shouts routine balls.
- **Voice B — analyst.** Calmer, conversational, tactical. Speaks only as a handoff after the
  call lands (the director enforces the 0.4 s gap and expiry).

## Step 1 — evaluation set (generate THESE 8 first, nothing else)

1. Calm dot: `Dot_Stopped_01` — "Well stopped at cover. Dot ball."
2. Four: `Four_Timed_01` — "Timed sweetly, no need to run. Four past cover."
3. Huge six: `Six_Big_01` — "Into the stands over midwicket! That's a big six."
4. Wicket: `Bowled_Gate_01` — "Clean bowled. Through the gate, and Asha has to go."
5. Close run-out: `RunOut_Direct_01` — "Direct hit! Bina is well short."
6. Tactical (B): `An_Yorker_01` — "Yorker length from here; anything short is disappearing."
7. Final ball: `Res_FinalWin_02` — "Asha does it! Home Eleven win off the final ball!"
8. Match win: `Res_ChaseWin_01` — "It's all over! Asha seals it, and Home Eleven win the Super Over."

Listen critically: emotion, cricket pronunciation (lexicon in `CricketCommentary::Lexicon()`),
pacing, consistency of identity across all 8, no robotic prosody, no metallic artifacts, no
volume jumps. Only after all 8 pass do voices/settings lock; then batch by `FLineMeta.Id`.

## Step 2 — script QA (before every batch)

Regenerate the candidate pool from `CricketCommentary` pools, then reject: unnatural cricket
language, factual ambiguity (any line claimable of the wrong event), verbosity, overused
clichés (`incredible/unbelievable/absolutely/massive/extraordinary` at most once each per pool),
grammar slips. Lines must read spoken, not written. Full-sentence clips only — no word-level
stitching; `{S}/{NS}/{R}/{B}` slots are filled before synthesis, one clip per filled line id.

## Step 3 — import

- File per filled line id, normalized (no clipping in worst-case mix: crowd 1.0 + stumps + call).
- Attach `FLineMeta` (speaker, excitement, priority, cooldown, tags, duration, interrupt flags).
- Extend `HasPronunciation` coverage for every new squad name first; never ship a guessed name.
- Mobile: commentary gets top fidelity priority (after bat/wicket impacts); compress ambience first.

## Acceptance scenarios (listen to complete Super Overs, headphones + phone speaker)

Low-scoring defense; high chase; multiple sixes; multiple wickets; close run-out; final-ball
finish (6-required-from-1 must breathe: tension, footsteps, release, dip, crack, swell, eruption,
lifted call, analyst only after); tie into another Super Over. Compare against baseline
`BallN.wav` captures (`-CricketRecordAudio`).

## Provider selection criteria

Naturalness > emotional range > cricket pronunciation > identity consistency > latency >
license > cost. Re-verify the current API's supported emotion controls; never invent parameters.
If the chosen provider becomes unavailable, the text library, metadata and directors are
provider-agnostic — only Step 1 re-runs.

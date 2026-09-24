# Plan: closing the gap to Cricket 26

The reference footage in `~/Downloads/CRICKET26.mp4` is Cricket 24 (India v Australia); it is used only to see
what a shipped cricket game presents, never as a source of content. Every design here is original. Cricket 26
comparisons stay UNVERIFIED until someone plays both games side by side.

Each phase ends with the test suite green, a soak or capture where it is visual, and a commit.

| # | Phase | What ships | How it is checked |
|---|---|---|---|
| 1 | Batting feel | Mistiming moves contact along the blade (late to the toe, early to the splice); a tighter timing window; AI shot values re-measured | Default squads score like a real Super Over with a realistic six share; batting tests |
| 2 | Broadcast HUD | Score bug, speed gun, this-over dots, batter and bowler cards, result banner | In-engine capture |
| 3 | Ball tracking | Pitch map, wagon wheel, Hawk-Eye trail and LBW projection from the simulated path | Tests on the projection; capture |
| 4 | Camera director | Lower broadcast main camera, multi-angle replays, umpire signal shot, highlight reel | Capture |
| 5 | Stadium | Floodlight sources, pitch material with creases and wear, boundary rope, crowd LOD | Capture; perf soak on Medium |
| 6 | Formats | T20 and ODI rules generalised from the Super Over rules (overs, powerplays, bowler limits) | Rules tests |
| 7 | Players and animation | Premium MetaHuman players built from the engine's MetaHuman Creator presets (`Scripts/metahuman/make_players.sh`: face auto-rig and textures from Epic's cloud, Optimized pipeline), in team-coloured kit, replacing the template mannequin, which stays as the fallback; licensed clips (Mixamo needs the owner's login), retargeted, with contact kept sim-exact | Figure and pose error checks; capture; perf soak on Medium |
| 8 | Audio | Crowd that swells and settles with the match state | Recorded delivery |

## Blocked on the owner

- Motion capture performer, voice commentary, licences for teams, players and music.
- A phone and the Android SDK or an iOS signing identity for the mobile build.
- Human playtesting and visual sign-off.
- `Cricket26_BasePlayer.fbx` in Downloads is not used: its licence cannot be verified.

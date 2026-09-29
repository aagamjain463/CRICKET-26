# CRICKET 26 visual overhaul tracker

## Implemented flow and action map

The current product boots `FrontendGameMode` on the Entry map. `FrontendRoot` owns tabs. `FrontendStatics::OpenSuperOver` travels to the same map with the Super Over game mode and settings in URL options. `FrontendMatchSubsystem` adds the in-match overlay. Game simulation, squads, saves, and match rules remain in their existing classes.

| Screen/control | Existing action | Current verification |
|---|---|---|
| Home: Play Super Over | `StartSuperOver` then `OpenSuperOver` | Build and home capture; launch path unchanged |
| Home: Match Setup | Select Play tab | Callback retained |
| Home: Squad/Players | Select Franchise tab | Callback retained |
| Home: Settings | Select Settings tab | Callback retained |
| Rail tabs | Select corresponding page | `ShowTab` retained |
| Play: difficulty | Save `Difficulty` | `Persist` retained |
| Play: venue | Save `Venue`; update condition copy | `Persist` retained |
| Play: Start Match | `StartSuperOver` | Callback retained |
| Settings | Save audio, graphics, timing, frame rate | Existing screen logic retained |
| Match: Pause | Pause world, show overlay | New C++ overlay; device interaction pending |
| Match: Resume | Unpause world | New C++ overlay; device interaction pending |
| Match: Audio | Apply and persist master volume | Shared save path |
| Match: Restart | Open Super Over with saved options | New C++ overlay; end-to-end run pending |
| Match: Exit | Return to Play | Existing frontend travel path |
| Result: Rematch | Open Super Over with saved options | New C++ overlay; end-to-end run pending |
| Result: Continue | Return to Play | Existing frontend travel path |

The game has **one fixed Home XI versus Away XI matchup**. Team selection, a standalone VS confirmation screen, a full squad manager, and auction ownership are not functional systems. The Play screen presents the real matchup without a fake team picker.

## Design tokens

| Token | Values |
|---|---|
| Colours | Ground `#0B100D`, stand `#141D17`, raised `#1C261E`, ink `#F2F0E5`, gold `#EFB94F`, turf `#72B88A`, danger `#DC6A57` |
| Type scale | Display 72, screen 44, section 30, subhead 22, body 18, label 15, caption 14 |
| Spacing | 8, 16, 24, 32, 48 |
| Geometry | 2-unit corners, 1-unit structural lines, score strips and crease rule |
| Motion | Tab 280 ms, modal 200 ms, press scale 0.97, minimum match transition 600 ms |

The source of truth is [the design system](CRICKET26_DESIGN_SYSTEM.md) and `FrontendStyle.h`.

## Screen tracker

| Screen | Old problem | Design goal | Implementation | Verification | Status |
|---|---|---|---|---|---|
| Main Menu | Nested dashboard cards and no cricket imagery | Player-led first impression | Stadium/batter hero, fixture strip, single play action | 1600×900 before/after; 1280×720 and 2400×1080 after | Implemented |
| Play | Mode card, stats, roadmap cards compete with setup | Competitive matchup and fast launch | Home XI / Away XI and saved match control band | 1600×900 before/after | Implemented |
| Team Selection | No selection exists | Honest competitive context | Fixed team matchup shown | Visual capture | Functional system absent |
| Match Setup | Controls sat inside generic card | Scoreboard-like controls | Difficulty, venue, single start action | Build and visual capture | Implemented |
| Loading | Centered text and spinner-like bar | Broadcast transition | Matchup, venue, score rule, player art | 1600×900 capture | Implemented |
| Pause | Exit confirmation only | In-match control point | Pause, resume, volume, control hint, restart, exit | 1600×900 capture | Implemented, touch QA pending |
| Settings | Large generic panel | Quiet readable settings | Palette, geometry, type applied | Fresh 1600×900 capture; large empty panel remains | Partial |
| Result | HUD scorecard only | Consequential final presentation | Winner, innings scores, top score, rematch/continue overlay | Scripted full match, capture | Implemented, touch QA pending |
| Franchise | Generic player tiles and speculative management cards | Managerial editorial page | Club hero and roster rows from existing squad data | 1600×900 capture | Implemented |
| Auction, Scouts, Store | Future content promoted in primary navigation | Keep playable flow clear | Removed from primary rail; pages and data remain | Fresh Home, Play, Settings rail captures | Implemented in primary flow; dormant pages remain |
| Live HUD | Older broadcast styling | Menu-match continuity | No broad HUD refactor in this pass | Existing game tests | Visual debt |

## Comparable captures

- [Before Home](UIShots/UI_Before_Home.png) / [After Home](UIShots/UI_After_Home.png)
- [Before Play](UIShots/UI_Before_Play.png) / [After Play](UIShots/UI_After_Play.png)
- [After Franchise](UIShots/UI_After_Franchise.png), [Loading](UIShots/UI_After_Loading.png), [Pause](UIShots/UI_After_Pause.png), [Settings](UIShots/UI_After_Settings.png)
- [After Result](UIShots/UI_After_Result.png)
- [Small landscape](UIShots/UI_After_Home_Small.png) / [wide landscape](UIShots/UI_After_Home_Wide.png)

Only Home and Play were captured before source edits. No before image is claimed for other screens. Captures were produced in the Mac editor at 1280×720, 1600×900, and 2400×1080. They do not prove phone safe-area, touch, or GPU performance.

The current editor build succeeds. A fresh `CRICKET26.Frontend` run completed **6/6** tests. A fresh full-match capture with `-FrontendForceOverlay` reached and captured the widened result overlay.

## Gates still open

- Capture the remaining development pages only when they are promoted into the primary flow.
- Exercise all controls on touch devices, including Android Back and safe areas.
- Profile on target mobile GPUs and optimize the 1.9 MB imported hero texture if needed.
- Align the live broadcast HUD and remaining future-mode pages with the design system.
- Reduce Settings' unused content area and review its placeholder categories.
- Review icon set, audio cues, and haptics on device.
- Implement real team selection only with corresponding match data and launch support.

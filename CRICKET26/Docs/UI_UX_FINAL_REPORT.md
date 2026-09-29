# CRICKET 26 UI/UX overhaul report

## Brand pass (FC 26 Mobile finish)

This pass gives the shell its own brand graphics and the tile-driven layout of FC 26 Mobile. Captures are in `Docs/UIShots/UI_Brand_*.png`.

- **Brand art in code.** `Scripts/ui/make_brand_art.py` draws the logo lockup, the home and away crests, the OVR shield, the VS ring, four brushed-metal tile faces (gold, royal, ember, night) and two backdrops (a royal-graded shell stadium and a blue/red VS split). It uses Pillow and NumPy only, so every asset is free and can be regenerated.
- **Shell.** The top bar carries the logo lockup, a club chip with the home crest, the squad OVR shield and the settings gear. The bottom bar is a flat FC-style tab strip (HOME, PLAY, CLUB, LIVE, STORE) with an inline icon and label and a gold fade and rule on the active tab. Match setup gets the VS split backdrop behind the whole shell.
- **Home.** The cover batter now stands in front of the allrounder. The right side is a featured Super Over banner with the away player, then a club tile (crest and OVR shield), a Mega Auction tile and the gold PLAY tile.
- **Play.** Four mode cards on their own faces: Super Over (royal, batter, gold SELECT), Mega Auction (ember, gavel, ENTER), and Live Super Over and League Season, both coming soon. The auction is named "Mega Auction" and uses no real league name or logo.
- **Club, Live, Store.** Club opens on a royal header with the home crest. Store shows a kit-room tile with the allrounder. Everything unbuilt still says "Coming soon".
- **Settings.** The volume slider has a visible track and a gold thumb.
- **Fixes, each with a regression test.**
  - Wide art (the logo) drew squashed into a square in editor-hosted runs. A texture that is still compiling reports the square placeholder's size, and `Picture` took its aspect from that. `FrontendStyle::Art()` now sizes from the texture source in editor builds (`CRICKET26.Frontend.Brushes`).
  - On a 4:3 tablet the home tiles clipped their titles ("MEGA AUCT") and the Play cards clipped "AUCTION". Tile copy now sits in `FrontendUI::Fit`, which shrinks and never grows, and mode-card titles stack their last word (`CRICKET26.Frontend.Fit`).
- **Tests.** The frontend suite passes 10 of 10. Captured at 1600×740, 1600×900 and 1200×900, all silent.
- **Not verified in this pass.** The loading screen now uses the VS split backdrop, but there is no automated capture that starts a match from the menu, so it was not captured.

## Second premium pass (FC 26 language)

This pass moves the shell closer to the FC 26 and FC Mobile look: collectible player cards, icon navigation, and motion.

- **Player cards.** `FrontendUI::PlayerCard` draws a shield-shaped card, like a trading card, from generated frames (`Card_Gold`, `Card_Elite`). Each card shows the rating and role stacked top-left, a head-and-shoulders render, then the name and detail. The ratings are the real `BatRating` and `BowlRating` values from `FrontendData::Squad()`. The strongest player gets the elite navy-and-gold frame. Home features that card beside the cover athlete. Franchise shows the whole squad under "YOUR SQUAD".
- **Squad renders.** Three more Gemini renders, all in the home kit and cut out with rembg: `Cutout_Finisher`, `Cutout_Allrounder` and `Cutout_Spinner`. The bowler slot uses the spinner render, which holds a ball.
- **Icons.** Google Material Icons (Apache 2.0) ship in `Content/UI/Fonts`. `FrontendUI::Icon` draws a glyph from `FrontendStyle::Glyph`. The tab bar has an icon over each label, and the active icon turns gold. The top bar has a gear button. Every "Coming soon" chip carries a lock.
- **Top bar.** A squad rating chip shows the mean of the real squad ratings. It is not a progression number.
- **Motion.** `FrontendUI::Animate` registers widgets. `UFrontendRoot::NativeTick` drives them through `TickMotion`. There are three kinds of motion:
  - A light sweep crosses every primary blade button every 3.5 seconds.
  - The floodlight streak layer breathes.
  - Tiles and cards slide in with a stagger each time a page opens.
- **Atmosphere.** A generated streak-and-bokeh layer (`Streaks`) sits over the stadium on the shell, the splash and the loading screen.
- **Tests.** `CRICKET26.Frontend.Motion` checks that an entrance stays hidden through its delay and then settles visible and in place, and that the icon font loads. The frontend suite passes 8 of 8.

## Premium pass (FC Mobile reference)

The earlier "stadium broadcast" shell used flat charcoal-green bands and one engine font, and it read as a prototype. This pass rebuilds the menu around the conventions of premium sports mobile games, with EA SPORTS FC Mobile as the main reference. The tokens, type and artwork are documented in [the design system](CRICKET26_DESIGN_SYSTEM.md).

What changed:

- **Identity.** A floodlit-night navy palette with floodlight gold for every primary action, home blue against away red, and cyan for available states. A new logo lockup: a sheared gold "26" tile, a "CRICKET" wordmark and a "SUPER OVER EDITION" caption.
- **Type.** Barlow Condensed (Open Font License) is loaded at runtime from `Content/UI/Fonts`. Headlines use Black Italic. The match HUD inherits it.
- **Artwork.** Gemini generated a night stadium and two players (home batter, away player). rembg removed the player backgrounds. `Scripts/import_ui.py` imports all of them.
- **Components.** Sheared gold blade buttons, glass bars, radial glows, artwork tiles with accent rules, status chips, team name plates and a VS composition.
- **Screens.**
  - Home: a cover-athlete layout with a featured player card and a large Super Over tile.
  - Play: three portrait mode cards.
  - Match setup: a full VS screen with a glass control bar.
  - Loading: a VS card.
  - Franchise: a chapter track.
  - Live and Store: art tiles.
  - Settings: a glass panel.
  - Pause and result: glass panels with a gold rule.
- **Honesty.** Only Super Over is marked available. Every other mode carries a "Coming soon" chip and opens an explanatory sheet.

Fixes found on the way, each covered by the new `CRICKET26.Frontend.Brushes` test:

- A pill radius of 999 exceeded the size of the chip, so the rounded-box shader drew nothing. `Rounded()` now uses half-height rounding for pills.
- Artwork that had not been imported painted a white slab. `Art()` now returns a brush that draws nothing.

Evidence:

- Captures of every tab at 2400×1080, plus Home at 1600×900, are in `Docs/UIShots/UI_Premium_*.png`. Pause, loading and an in-match HUD frame are included.
- `Scripts/run_tests.sh CRICKET26.Frontend` passes 7 of 7.
- Every run used `-nosound`.

Open items:

- Profile texture memory on phones.
- A dedicated logo review.
- The generated players are generic, not licensed likenesses.

---


## Old design problems

The old Home and Play screens were built as large rounded mode cards next to equally prominent future-system cards. Dark navy, blue light pools, pills, and repeated dashboard tiles made cricket content secondary. Primary action competed with auction promotion and roadmap content. Numbers used the same generic tile pattern as descriptions. The result existed as a dense HUD scorecard and the in-match menu only confirmed leaving.

## New CRICKET 26 identity

**Direction:** stadium broadcast. Real cricket imagery, warm score accents, dark turf surfaces, short crease rules, and structured fixture bands join menu, match setup, loading, pause, and result. The existing `26` mark remains for recognition. The UI uses an image derived from the existing blue-kit in-game batter; it has no fake sponsor or embedded UI text.

**Signature motifs:** short crease rule under the lead moment; squared score strips; fixture notation and condensed cricket numbers. These recur without adding decorative balls or wickets.

**Colour:** `#0B100D` ground, `#141D17` structural plane, `#F2F0E5` text, `#EFB94F` primary action, `#72B88A` live state. Blue remains a team colour. Selection uses a filled segment and structure, not colour alone.

**Typography:** one Slate family. Bold/condensed faces carry the match, team, and score. Body and metadata stay quieter. Display through caption scale and spacing live in [the design system](CRICKET26_DESIGN_SYSTEM.md).

**Geometry:** squared bands, 2-unit button/panel corners, thin lines, and an 8-point spacing rhythm replace the repeated 20-unit cards and pills. The home hero gives the player space to break the visual grid.

**Iconography:** limited to essential direction and pause symbols. No mixed icon libraries. Custom cricket glyph work remains open.

**Motion:** existing 280 ms tab settle and 200 ms modal move remain fast; button presses scale to 0.97. Loading carries the same matchup and narrow progress rule. Audio/haptic button cues need device work.

## Screen changes

| Screen | Change |
|---|---|
| Main Menu | Full-width stadium/player moment, one Play Super Over action, current fixed fixture, compact squad/settings links. |
| Play / Team context | Home XI versus Away XI visual, no fake team picker. Venue and difficulty remain saved controls. |
| Match Setup | Match controls on one structural plane and a single Start Match action. |
| Loading | Matchup, venue, player, and progress rule carry the player into the ground. |
| Franchise | Existing four-player squad appears as ordered roster rows with roles, details, and ratings. |
| Settings | Quieter shared colours, type, and squared geometry; persistence code retained. |
| Pause | Live ground remains visible; resume, audio, controls hint, restart, and exit are available. |
| Result | Winner, both innings, leading batter, rematch, and continue replace a bare confirmation moment. Existing HUD scorecard still sits underneath. |

Auction, Scouts, and Store remain in code with their status content, but were removed from primary navigation until the systems are playable. A real team selection flow would require corresponding match setup data and launch support.

## Responsive and performance evidence

Home was inspected at **1280×720**, **1600×900**, and **2400×1080** in Mac editor game mode. Play, Franchise, Loading, Pause, Settings, and Result were captured at 1600×900. The Home, Play, and Settings captures show the current reduced navigation. `USafeZone` remains the shell container. No target-phone safe-area test or mobile GPU profile was run. The new hero uses one imported texture; the rest remains flat Slate brushes without full-screen blur or animated UI materials.

## Product logic preserved

`FrontendStatics` still opens the Super Over through the same map and game mode. Existing saved difficulty, venue, quality, timing bar, volume, and frame-rate settings remain in `UFrontendSettingsSave`. Match rules, squads, team/player data, and gameplay simulation were not redesigned. The frontend automation suite passed **6/6** after the screen changes; a scripted full Super Over reached the result overlay.

The current combined editor build succeeds. A fresh `CRICKET26.Frontend` automation run passed **6/6** tests. A fresh scripted match with `-FrontendForceOverlay` reached and captured the widened result overlay.

## Before and after

| Baseline | New capture |
|---|---|
| [Home before](UIShots/UI_Before_Home.png) | [Home after](UIShots/UI_After_Home.png) |
| [Play before](UIShots/UI_Before_Play.png) | [Play after](UIShots/UI_After_Play.png) |

Additional: [Franchise](UIShots/UI_After_Franchise.png), [Loading](UIShots/UI_After_Loading.png), [Pause](UIShots/UI_After_Pause.png), [Result](UIShots/UI_After_Result.png), [Settings](UIShots/UI_After_Settings.png), [small landscape](UIShots/UI_After_Home_Small.png), [wide landscape](UIShots/UI_After_Home_Wide.png). A baseline for those other screens was not captured before edits.

## Files changed

- `Source/CRICKET26/Frontend/FrontendStyle.*`: colour and shape tokens.
- `Source/CRICKET26/Frontend/Widgets/FrontendUI.cpp`: shared control appearance.
- `Source/CRICKET26/Frontend/Widgets/FrontendScreens.cpp`: Home, Play, Franchise.
- `Source/CRICKET26/Frontend/Widgets/FrontendRoot.cpp`: shell navigation, header, loading.
- `Source/CRICKET26/Frontend/Widgets/FrontendMatchOverlay.*`: pause and result.
- `Content/UI/Source/CRICKET26_Hero_Batter_v2.png` and imported texture: player/stadium image.
- `Scripts/import_ui.py`: repeatable editor asset import.
- `Source/CRICKET26/Cricket/MatchHUDWidget.cpp`: isolate broadcast helpers so unity builds do not collide with other HUD code.
- `Config/DefaultGame.ini`: set the project name and cook the dynamically loaded UI texture on mobile.
- `Source/CRICKET26/Cricket/MatchHUDWidget.cpp`, `SuperOverHUD.*`: narrow compile fixes for the newly present broadcast widget. No match simulation change.

## Remaining visual debt

This pass does not complete every gate in the brief. A fixed matchup is all the current game supports; team selection and a separate VS confirmation need product logic first. The live HUD and dormant future pages need a full consistency pass. Settings retains a large empty content area and placeholder categories. Icon drawing, button sounds, haptics, phone touch tests, safe-area tests, and device GPU profiling remain. The widened result overlay is visually verified; rematch/continue taps still need a touch run.

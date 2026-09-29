# CRICKET 26 design system

## Brand

CRICKET 26 is a premium cricket game about pressure moments. The shell follows the language of current sports mobile games, with EA SPORTS FC Mobile as the main reference: a cover athlete breaking out of the grid, large artwork tiles, italic condensed headlines, one gold primary action and a persistent bottom tab bar.

Principles: lead with players and stadium, give each screen one gold action, keep numbers large and legible, and never imply an unfinished mode is playable. Unbuilt modes carry a "Coming soon" chip and open an explanatory sheet. The shell invents no XP, currency or levels.

## Direction

**Floodlit night.** A deep navy base sits under a dimmed night-stadium backdrop. Floodlight gold marks every primary action. Home blue and away red identify the two sides. Cyan marks live or available states.

Signature motifs:

1. **Blade buttons.** Primary actions are gold parallelograms, sheared by -16 units with an upright label, carrying a vertical shade and a bright top edge.
2. **Cover athletes.** Background-removed player renders overlap tile edges and sit on coloured glows.
3. **Name plates.** Sheared team-colour bands carry the team names on the VS and loading screens.
4. **Accent rule.** A 4-unit rule marks the active tab, the bottom of each tile and the top of each modal panel.
5. **Player cards.** Shield cards, like trading cards, in gold or elite (navy with a gold rim). They show real squad ratings.
6. **Light sweep.** A slanted shine crosses primary buttons every few seconds.
7. **Faced tiles.** Every tile sits on a brushed-metal face (gold, royal, ember or night) with a watermark "26", a top gloss line, a 5-unit accent rule along the bottom and a hairline frame. The face says what kind of tile it is: gold is the one action, royal is your side, ember is the auction, night is coming soon.
8. **Crests and OVR shield.** The home and away sides carry shield crests. The squad rating sits on a gold-rimmed OVR shield in the top bar and on the club tile.

## Tokens

The values live in `FrontendStyle.h`. Slate converts hex colours from sRGB.

| Role | Value | Use |
|---|---|---|
| Bg | `#050913` | Base behind the backdrop |
| Surface / Card / CardHi | `#0B1222` / `#131D33` / `#1F2C4A` | Solid surfaces, hover and press states |
| Glass | `#0B1426` at 78% | Bars, panels and chips over artwork |
| Line | `#A9C2FF` at 16% | Hairline frames |
| Gold / GoldHi / GoldLo | `#F2B632` / `#FFD56B` / `#C98A12` | Primary actions, active tab, ratings |
| GoldInk | `#1A1204` | Text on gold |
| Teal | `#3FD8FF` | Available and live states, eyebrows |
| Blue / Red | `#2F6BFF` / `#E0344B` | Home and away sides |
| Ink / InkDim / InkFaint | `#FFFFFF` / `#B9C4DC` / `#7382A3` | Type |

The radii are 12 for cards, 6 for buttons and a pill value (`RPill`). `Rounded()` turns the pill value into half-height rounding, because a fixed radius larger than the box makes the rounded-box shader draw nothing.

## Type

The typeface is Barlow Condensed (SIL Open Font License), shipped as loose TTFs in `Content/UI/Fonts` and staged through `DirectoriesToAlwaysStageAsUFS`. `FrontendStyle::Font()` maps the weights as follows: Regular to Medium, Medium to SemiBold, Bold to Bold, Condensed to ExtraBold and Black to Black Italic. If a file is missing, it falls back to the engine font. Headlines use Black Italic in upper case. Eyebrows use Bold with wide tracking. Body copy uses Medium. The design reference is 1920×1080.

Icons come from Google Material Icons (Apache 2.0), stored as `Content/UI/Fonts/MaterialIcons-Regular.ttf`. `FrontendStyle::IconFont()` loads the file, and the glyph codepoints live in `FrontendStyle::Glyph`.

## Artwork

The photographic artwork (stadium, players) is AI-generated with Gemini. The brand graphics (logo, crests, tile faces, badges, backdrops) are drawn in code by `Scripts/ui/make_brand_art.py` with Pillow and NumPy, so they can be regenerated at any size. Source PNGs live in `Content/UI/Source/`. `Scripts/import_ui.py` imports them into `/Game/UI/T_CRICKET26_<name>` with the UI texture group.

| Texture | Content |
|---|---|
| `Stadium_Night` | Floodlit stadium, cropped to remove the generator watermark |
| `Cutout_Batter` | Home batter in blue kit, background removed with rembg (`isnet-general-use`) |
| `Cutout_Bowler` | Away player in red kit, background removed |
| `Hero_Batter_v2`, `Franchise_Stadium` | Tile artwork for modes that are coming soon |
| `Cutout_Finisher`, `Cutout_Allrounder`, `Cutout_Spinner` | Squad renders in the home kit, used on player cards |
| `Card_Gold`, `Card_Elite`, `Streaks`, `Shine` | Card frames, the atmosphere layer and the button shine, drawn by `Scripts/ui/make_ui_art.py` |
| `Logo` | The lockup: a sheared gold "26" blade, a chrome "CRICKET" wordmark and the "SUPER OVER EDITION" line |
| `Shell_Bg`, `VS_Bg` | The royal-graded, blurred stadium behind every page, and the blue/red split behind match setup and loading |
| `Tile_Gold`, `Tile_Royal`, `Tile_Ember`, `Tile_Night` | Tile faces |
| `Crest_Home`, `Crest_Away`, `Ovr_Badge`, `Ring` | Side crests, the OVR shield and the gold ring behind "VS" |

Run `Scripts/ui/make_brand_art.py [Name ...]` with a Python that has Pillow and NumPy, then `Scripts/import_ui.py` through `UnrealEditor-Cmd -run=pythonscript`.

`FrontendStyle::Art()` returns a brush that draws nothing when a texture is missing, so artwork that has not been imported never paints a white slab. In editor builds it sizes the brush from the texture source, because a texture that is still compiling reports the square placeholder's size and every `Picture` sized from it would be squashed.

## Components

`FrontendUI` owns the shared pieces:

- Layout: `Stack`, `Add`, `Sized`, `Fit` (shrinks its content to the space it gets and never grows it; tile copy sits in one so a 4:3 tablet never clips a title).
- Surfaces: `Box`, `Glow` (radial texture), `Fade`.
- Actions: `CTA` (blade button for Primary), `Segmented`, `Slider`, `SettingRow`.
- Brand and artwork: `Logo`, `Backdrop` (crop to fill), `Picture` (keeps aspect ratio), `NamePlate`, `Versus`, `Icon`, `PlayerCard`.
- Motion: `Animate(Widget, Shine | Pulse | Enter, Param)` registers a widget, and `TickMotion` runs every registered widget each frame from `UFrontendRoot::NativeTick`.

Screen-level tiles (`Tile`, `TileCopy`, `ModeCard`, `AuctionTile`, `SoonTile`, `Chip`) live in `FrontendScreens.cpp`. `Tile` takes an `FTileLook`: the face, an optional photo plate and its tint, the accent rule colour, how dark the left and bottom shade is, and an optional figure pinned bottom right.

The shell is `UFrontendRoot`. The top bar holds the logo, the club chip (crest, "HOME XI", taps to the club), the squad OVR shield and the settings gear. The bottom tab bar is flat, FC style: HOME, PLAY, CLUB, LIVE, STORE, each an icon and label inline, with the active tab lit by a gold fade and a 4-unit gold rule. Scouts opens under CLUB and match setup under PLAY.

## Screen application

- **Home:** the batter cover athlete in front of the allrounder on the left, with an elite card for the best player in the squad. On the right, a featured Super Over banner with the away player, then three tiles: the club (crest and OVR shield), the Mega Auction and the gold PLAY tile.
- **Play:** four portrait mode cards. Super Over and Mega Auction are available; Live Super Over and League Season are coming soon.
- **Match setup:** a full VS composition on the split backdrop, with ghosted HOME and AWAY words, both crests either side of the gold VS ring, and name plates. A glass control bar holds the saved difficulty and venue controls and START MATCH.
- **Loading:** the same VS language with a progress rule.
- **Franchise, Live, Store:** artwork tiles marked "Coming soon", plus the squad's player cards and a numbered chapter track on Franchise.
- **Settings:** one centred glass panel.
- **Pause and result overlays:** glass panels with a gold top rule over a glow. The result screen uses the stadium backdrop with team-coloured score rows.
- **Live HUD:** functional layout unchanged; it inherits the new type.

## Limits

Screenshots come from desktop editor builds (`Scripts/ui/menu_shots.sh SET [W H] [TABS...]`, silent). Before shipping, profile the texture memory of the full-resolution backdrops and tile faces on target phones. The cover athletes are generic generated players, not licensed likenesses.

# Auction reference

What the 3D auction (`Source/CRICKET26/Auction`) is modelled on, taken from two videos watched frame by frame
and, for the real one, from its captions:

- **IPL 2025 mega auction** (Jeddah, broadcast): https://www.youtube.com/watch?v=Oa07xig_80E
- **Real Cricket's RCPL auction** (mobile game): https://www.youtube.com/watch?v=Uog1yXl_EYM

## The real room

- **Hall.** Dark, with a green and gold theme. The stage is raised at one end. The auctioneer stands at a lectern
  on the stage's left, with the trophy beside her and a second host on the right. A huge LED wall behind them
  shows the player card: portrait, role, country, and a stats table (T20 / IPL / T20I matches, runs or wickets,
  strike rate or economy). "BASE PRICE" is written in large type across the wall.
- **Tables.** Ten white-clothed tables in two banks beside a central aisle, with front and raised rear ranks facing the stage. Each has three to six staff in franchise polos
  (red for RCB and PBKS, purple for KKR, orange for SRH, yellow for CSK), with laptops, water bottles, a name tent
  and the team's paddle. Owners sit at the front, and analysts lean over laptops.
- **People.** Staff huddle before a big raise and whisper across the table, holding their chins. The paddle goes
  up fast and comes straight down, and the side that wins applauds. The auctioneer points at the table that bid
  and names it ("on my far right now, Kolkata"), and brings the gavel down on the sale.

## Broadcast grammar

- **Split screen.** Most bidding is shown as two framed insets of the duelling tables, captioned with the team
  names (ROYAL CHALLENGERS | KNIGHT RIDERS), over the green motif.
- **Single shots.** A close-up of a table after it bids or wins, the auctioneer while she calls, and a wide of
  the stage with the wall on each new player.
- **Lower third** (always on): BASE PRICE | the portrait in a roundel with the name | CURRENT BID. Below it is a
  stats strip, and below that a ticker of every purse and squad size ("CHENNAI SUPER KINGS : ₹31.60Cr (9 PLAYERS)").
- **RTM.** When a side holds the Right to Match, the lower third shows its crest with "RTM ENABLED". The current
  bid box is labelled with the holder, for example "SRH - CURRENT BID".

## Cadence (from the captions)

- **Opening.** The auctioneer asks for an opening bid at the base price. She often waits and asks again ("any
  paddles going up... last chance") before the first paddle.
- **Stars.** Big names draw two sides at once. They race through the ladder (+20 L up to 5 Cr, +25 L after), with
  a third side jumping in late. Bids come every one to two seconds. Near the limit the auctioneer grants pauses
  ("request a couple of seconds", "we take a pause").
- **The close.** "Quick scan, any other team... all done... last chance... going once, going twice", then the
  gavel, the price, and "well done". A star lot runs one to three minutes.
- **RTM.** She asks the former side whether it will exercise the Right to Match. The buyer then gets one final
  raise (Pant 20.75 to 27 Cr), and the former side either matches or declines.
- **Record prices.** Pant 27 Cr, Iyer 26.75 Cr, Venkatesh 23.75 Cr. Every marquee player sold.

## RCPL (the game to beat)

- Stylised 3D: red hall, round tables with four suited staff each, and a woman auctioneer at a red lectern in
  front of a big logo wall.
- HUD: purse ("BAL"), a player card with ratings, the current bid with the leading team's crest, and SKIP / BID /
  AUCTION HUB buttons.
- The RTM prompt comes with a raise stepper, and the player list shows the pool with ratings and base prices.
- Its limits:
  - Generic suits, not franchise colours.
  - Stiff animation.
  - One camera at a time.
  - No split screen.
  - A static wall.

## Where CRICKET-26 stands against them

| Reference element | CRICKET-26 status |
| --- | --- |
| Hall, stage, lectern, trophy, LED wall and arches, side screens | Built (generated geometry, lit screens) |
| Ten tables in two banks, with laptops, bottles, tents, backdrops and skirts | Built: three tables at floor level and two on a low riser per bank. Staff, chairs, lights, paddles and close-up cameras share the table coordinates; branded backdrops sit below shoulder height so rear teams remain visible. The 07:55 broadcast shot shows Punjab in front of Hyderabad, and the 11:55 wide shows the central aisle and two banks. Other exact franchise positions are an approximation because the broadcast does not show all ten labels in one wide shot. |
| Staff at the tables | Seated MetaHumans, three per table on desktop (two on phones, where each one costs a full character), paddle on the right and the middle seat conferring with it; all Indian-looking with medium brown skin and short Indian hairstyles (`make_players.py` sets the tone and the groom), lit by a key behind them and a fill from the stage side on lighting channel 1, which only the people are on, so faces read and the franchise backdrop keeps its colour |
| Paddle raise, huddle, clap, auctioneer point and gavel | Built as IK poses |
| Real IPL flow: sets, marquee, accelerated rounds, RTM with final raise, purse and squad rules | Built, covered by the `CRICKET26.Auction` tests |
| Lower third, purse ticker, RTM badge | Built: base price, portrait roundel (initials: real photos are licensed), stats strip, the holder's current bid, the purse ticker, and the former side's crest on the RTM badge |
| Split screen of the duelling tables | Built: two scene captures framed over the green motif, the holder's inset in gold (desktop only; mobile keeps single cuts) |
| Franchise-coloured staff shirts | Built: the MetaHuman T-shirts tinted in the franchise colour; the auctioneer in deep red |
| Auctioneer's voice | Stitched from recorded pieces (`AuctionCalls::Voice`), recorded locally with Kokoro (bf_emma) by `Scripts/audio/auctioneer.py`: all 1956 pieces recorded, so every line of a full auction is voiced; a line with any unrecorded piece would stay a caption |
| Clean broadcast HUD | Built: one surface throughout (dark green glass with a hairline edge, opaque where it covers the stage), gold kept for money and the one action, tracked caps for field names. Live-screen pieces share one grid in `AuctionHUD.cpp` (`Edge`, `RailW`, `ThirdH`, `TopY`, `RailY`): the event bug top left mirrors the purse card top right, the side rail is a single column of text rows, the BID paddle sits level with the lower third, and the RTM countdown runs along the dialog's top edge |
| Rupee sign | Barlow Condensed has no ₹, so `FrontendStyle::AddRupee` maps U+20B9 to the engine's DroidSansFallback in every HUD face and on the LED wall (`CRICKET26.Frontend.RupeeGlyph`) |

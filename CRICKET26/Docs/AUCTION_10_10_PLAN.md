# Auction 10/10 Plan

A review of the IPL auction (`Source/CRICKET26/Auction`, `Scripts/auction`) against the real IPL 2025 mega auction
(Jeddah, 24–25 Nov 2024), and the order of work to make it feel like the real room. Read with
`Docs/AUCTION_REFERENCE.md`.

## 1. Where it stands (2026-09-29): 7/10

| Area | Score | Why |
| --- | --- | --- |
| Rules fidelity | 8.5 | 120 Cr purse, 18–25 squad, 8 overseas, 6 keeps (5 capped / 2 uncapped), 18/14/11/18/14 Cr + 4 Cr uncapped slabs, RTM with the buyer's final raise, the real bid ladder (+5 L / +10 L / +20 L / +25 L), two marquee sets of six, role sets, accelerated rounds with nominations. |
| Presentation | 8.5 | The 3D hall, ten tables with MetaHuman staff in franchise colours, paddle, huddle and gavel IK, the camera director, the split screen, the lower third and purse ticker, and an auctioneer voiced from 1956 clips who uses the real room's cadence. This is better than RCPL on every point in the reference doc. |
| AI front offices | 6 | Good base: per-team valuation with noise, personality (aggression, patience, loyalty, role bias), squad need, purse pressure, duel weighting, huddles near the limit, walking away. Weak where the real room is decided (section 2). |
| Player data and ratings | 6.5 | 400 real players with sources. Ratings are career aggregates, so recency and form are missing, and all T20 league columns are empty. |
| Things to do as the player | 5 | Pick a team, retain, BID, answer RTM prompts, nominate, skip. There is no planning, no max price or auto-bid, no scouting, no save or resume, no difficulty, no post-auction verdict, and no multiplayer. |

Method: I read the engine, data, calls, game mode, HUD and tests, and ported `BaseValue`, `Overall` and
`AiRetentions` to Python to check what they produce. The Unreal build and the automation tests were not run.

## 2. Findings

### Bugs and data truth
1. **RCB title count is stale.** `AuctionData.cpp` hardcodes RCB `Titles = 1`, but `Teams.csv` says 2 (2025 and 2026), so the pick screen shows "1 x CHAMPIONS". `Teams.csv` is not read by anything. Its captains, coaches, owners and colours are unused, and its hex values differ from the hardcoded ones.
2. **Set order.** The real draw goes Batters, All-rounders, Wicketkeepers, Fast bowlers, Spinners (BA, AL, WK, FA, SP). `BuildSets` loops through the enum order instead: BA, WK, AL, FA, SP.
3. **"IPL 2027 Mega Auction" is not what comes next.** Mega auctions run on a three-year cycle (2025, then 2028). The next real event is the **IPL 2027 mini auction** in Dec 2026, which has released players, carried-over purses, no RTM, and a cap on overseas fees.

### Valuation (from the Python port of `BaseValue`)
4. **The top of the market is set by clamps, not by bidding.** Five players have a base value above 30 Cr: KL Rahul 44.8, Vaibhav Sooryavanshi 43.6, Bumrah 37.9, Sai Sudharsan 33.9 and Gill 32.9. `Valuation` then caps everyone at `min(30 Cr, 0.4 × MaxBid)`, so the record price comes from the cap, not from two tables pushing each other.
5. **Career-aggregate ratings distort the order.** A 15-year-old with one season is the second most valuable player, and KL Rahul is valued above Bumrah. Warner is rated 93 at 39, which needed the hack `overseas batter ≥37 → ≤1.25× base`, and Dhoni is 89 at 45. Recency and age curves belong in the rating itself, not in special-case clamps.
6. **AI retentions look off.** CSK keeps Akeal Hosein, KKR keeps only four, and nobody keeps anyone above the slab. In the real 2025 retentions, Klaasen got 23 Cr and Kohli and Pooran 21 Cr each, and stars such as Pant, Iyer and Rahul refused to stay, which is what made the auction dramatic.

### AI bidding behaviour
7. **Each walk-away price is fixed when the lot opens** (`MaxThisLot`). Real prices move during a lot: heat in a bidding war, fear of missing out as the pool thins, sticking points at round numbers (10, 15 and 20 Cr), and a side that just lost Pant paying more for the next keeper-captain.
8. **No plan.** Sides do not go in with a target list, a budget per slot or tiered fallbacks. "Need" is a count of players per role (`RoleTarget`). It knows nothing about playing-XI slots: openers, a finisher, death pacers, left-arm pace, wrist spin, the keeper who bats in the top four, a captain, or the 4-overseas-in-the-XI balance.
9. **No RTM strategy.** The former side bids like everyone else, and if it wins, no RTM is used. Real sides often sit out and then use the card. The buyer's final raise is a random 25–85% of its own limit, when it should be priced against what the RTM side can pay (its purse and its need).
10. **No price-enforcing.** Real sides bid on players they do not want, to drain a rival's purse.

## 3. The plan

Order of work: calibration first, so every later change can be measured; then the AI brain, which is the biggest gain; then modes and the player's own tools; then polish.

### Phase 0: Truth and calibration harness (≈2 days)
- Fix RCB titles. Generate the franchises from `Teams.csv` through `roster.py` (as `AuctionTeams.inl`), so colours, captains, coaches and titles have one source.
- Set order BA → AL → WK → FA → SP.
- Add `CRICKET26.Auction.Calibration`, a headless test over 50 seeds that reports distributions, and assert bands against the 2025 mega auction:

| Metric (2025 mega auction) | Real | Band |
| --- | --- | --- |
| Players sold | 182 | 165–200 |
| Spent in the auction | ₹639 Cr | ₹560–720 Cr |
| Overseas sold | 62 | 50–72 |
| Top price | ₹27 Cr (Pant) | 22–30 Cr |
| Sold at ≥ ₹10 Cr | ~20 | 14–26 |
| RTMs used | 8 | 4–12 |
| Marquee players unsold | 0 | 0 |
| Retentions | 46, ₹558.5 Cr | 38–55 |

(Check these figures against the Wikipedia personnel-changes page before freezing the bands.)

### Phase 1: Ratings v2 (≈3 days, `Scripts/auction`)
- Recency-weighted ratings: last season 50%, the season before 30%, career 20%, with shrinkage toward the role prior for small samples. This fixes Sooryavanshi, Rahul and Warner without clamps.
- Age curve built into the rating, peaking at 26–30. Then delete the age multipliers and the Warner and `Price2026` clamps in `Valuation`.
- Fill the T20 columns from Cricsheet (BBL, PSL, SA20, CPL, The Hundred, ILT20, MLC, T20I), so overseas newcomers have real numbers.
- Tag sub-roles in `Players.csv`: `Opener, Anchor, Finisher, KeeperBat, PPPace, DeathPace, LeftArmPace, WristSpin, FingerSpin, Mystery, Captain`. Tag them from phase-wise Cricsheet splits (powerplay, middle and death economy and SR, batting position), and hand-check the top 100.

### Phase 2: AI front offices v2 (≈1.5 weeks, `AuctionEngine.cpp`)
1. **Pre-auction plan per side.** From its retentions, build a target XI plus a 7–10-man bench template: slots, a budget envelope per slot (from a franchise DNA table), and a ranked list of 2–4 targets per slot in tiers A/B/C.
2. **Value = marginal XI gain,** the projected XI rating with the player minus without him, plus bench depth. This replaces `Need()` and `RoleTarget`, and handles overseas balance: in the XI, a 5th overseas star is worth only its bench value.
3. **Market-derived ceiling.** Drop the 30 Cr and 0.4× clamps. The ceiling is the slot's budget plus a premium when the player is the last tier-A option left in the pool for that slot (scarcity).
4. **Live walk-away** instead of a fixed `MaxThisLot`:
   - heat: +3–8% after each round of a two-side duel, capped by aggression
   - sticking points: a pause and extra hesitation at 10, 15, 20 and 25 Cr
   - substitution: losing a tier-A target raises the ceiling on the next same-slot target by 10–25%
   - regret: a slot still empty late in the round adds a premium
5. **Price-enforcing:** a side with a low need but a rival with a high need for this player sometimes pushes the price, backing off near its own acceptable ceiling. It is visible on the broadcast, and it sometimes backfires.
6. **RTM tactics:**
   - The former side chooses between bidding and sitting out for the card. It sits out when it holds a card and wants him at a moderate price.
   - The buyer's final raise is set to just above its estimate of the RTM side's ceiling (the RTM side's purse and need), or it holds when that estimate is unaffordable.
7. **Franchise DNA** from real behaviour, in the style table: CSK loyal and experienced, RR and MI youth scouting, KKR mystery spin and all-rounders, PBKS big spenders when their purse is heavy. Also plan tempo: whether a side spends early on marquee names or waits for value.
8. **Difficulty levels** (Casual / Pro / Legend): noise, discipline and how well the AI reads the human's needs.

Exit: the Phase 0 bands hold over 50 seeds, and a blind reviewer can't tell a simulated results sheet from a real one.

### Phase 3: Rules and modes (≈1.5 weeks)
- **Retention v2:**
  - salary set at or above the slab, with the purse deduction being whichever is larger (Klaasen 23 Cr)
  - player willingness: a star may refuse, based on role, captaincy and money, and enters the pool, as Pant, Iyer and Rahul did
  - a trade window before the retention deadline
- **Mini auction mode (IPL 2027):**
  - releases, purse carry-over plus the league's purse increase, and open slots
  - no RTM
  - the overseas fee cap: pay above 18 Cr goes to the board, not the player, as with Cameron Green in Dec 2025
  - a smaller pool, with sets by role and base price
- **Career loop:** auction → season (`IPLSeason`) → release and retain → mini auction → … with a mega auction every three years, and player ageing and ratings updated from season stats.
- Rule events: an overseas player who withdraws after being bought is banned for two seasons, injury replacements, and the minimum spend check.
- Auction days: Day 1 ends after the marquee and capped round 1, and Day 2 opens with the accelerated rounds. Save between days.

### Phase 4: The player's war room (≈2 weeks, `AuctionHUD.cpp`, `AuctionGameMode.cpp`)
- **War room screen** before the hammer and between sets:
  - needs analysis of your XI and bench
  - shortlist with a max price per player
  - budget planner by slot
  - rival radar: each side's purse, open slots, overseas left and likely targets
  - set calendar: when your targets come up
- **In the lot:**
  - BID, and JUMP BID to a chosen figure
  - AUTO-BID UP TO X, which stops at your max
  - REQUEST A MOMENT: a timeout, limited per lot and per day, which the auctioneer grants ("take your time")
  - an analyst whisper ("Punjab still need a keeper, ₹38 Cr left")
- **Pace options:** watch everything, stop only for your shortlist, or simulate to the end. Save and resume at any lot, which a full auction needs.
- **Post-auction verdict:**
  - an analyst grade for every side (A+ to D)
  - best XI and impact sub
  - holes
  - steals and splurges
  - records (most expensive ever, most expensive by role)
- **Multiplayer:** local pass-the-paddle for 2–4 players, then online rooms for up to 10 humans with AI filling empty tables. The real auction is ten humans in one room, so this is the step that makes it feel real.

### Phase 5: Broadcast polish (≈1 week, `AuctionRoom.cpp`, `AuctionCalls.cpp`)
- An analyst desk between sets (two voices over squad graphics), and a record-price banner and sting.
- Lower third strip showing last-season form next to career numbers.
- Wider auctioneer variety:
  - "two tables in it now"
  - calls at sticking points
  - the timeout grant
  - "we're back after the break"
  - Day 1 and Day 2 openings
- Table reactions by outcome (owner applause, a groan from the side that lost the duel, a handshake across the aisle), and a pan to the side that dropped out.
- Crests and portraits: keep the licence-safe initials and stylised crests unless licences are obtained (owner rule: no unlicensed content).

### What 10/10 looks like
- Phase 0 bands hold.
- The ten sides build squads a fan would recognise as each franchise's, and star lots produce two- or three-way wars.
- The player plans, bluffs, gets outbid and adapts.
- Save and resume and a verdict screen are in, and a multiplayer room plays through.
- All `CRICKET26.Auction` tests pass, calibration included.

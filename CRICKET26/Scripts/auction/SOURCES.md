# Auction data sources

Compiled 2026-09-28. Players.csv has 400 rows: 250 original IPL 2026 squad players (25 per team) and 150 non-squad players. Teams.csv covers the 10 franchises.

## Stats cutoff
- IPL columns (IplMatches, IplRuns, IplSR, IplWkts, IplEcon) are career totals through the end of IPL 2026 (final on 31 May 2026).
- 224 rows come from Cricbuzz profile career tables. These are official figures.
- 108 rows come from Cricsheet ball-by-ball data (all 74 matches of 2026 included). The Cricsheet rules are:
  - runs conceded = batter runs + wides + no-balls
  - wickets exclude run outs
  - impact-player appearances count as matches
  - Cricsheet can differ from official figures by about 1 match or 10 runs for long careers
- The remaining 68 rows have never played an IPL match. They have IplMatches 0 and empty SR/Econ.
- T20* columns (all-T20 career) are left empty. ESPNcricinfo blocked automated access. Cricbuzz only publishes T20I, IPL, ODI and Test splits, not all T20s.
- Age is computed on 2026-09-28 from the date of birth.
- Capped status comes from Cricbuzz career timelines or Wikipedia infobox caps, and includes international debuts made up to Sep 2026 (for example Sooryavanshi, Prince Yadav, Naman Dhir and Manav Suthar debuted in 2026). Under the IPL 2025 rule, India players whose last international was before 2021 are set to Capped 0: MS Dhoni, Mayank Markande, Sandeep Sharma, Vijay Shankar, Karn Sharma and Mandeep Singh.

## URLs
- https://en.wikipedia.org/wiki/2026_Indian_Premier_League (squads context, captains, coaches, champion, venues)
- https://en.wikipedia.org/wiki/List_of_2026_Indian_Premier_League_personnel_changes (Nov 2025 retentions and trades, Dec 2025 auction sold list with base prices, replacements)
- https://en.wikipedia.org/wiki/List_of_2025_Indian_Premier_League_personnel_changes (2025 mega-auction base prices, released players)
- Franchise Wikipedia pages (owners, grounds): Chennai_Super_Kings, Mumbai_Indians, Royal_Challengers_Bengaluru, Kolkata_Knight_Riders, Sunrisers_Hyderabad, Delhi_Capitals, Punjab_Kings, Rajasthan_Royals, Gujarat_Titans, Lucknow_Super_Giants
- Individual player Wikipedia pages (infobox DOB, batting and bowling style, international caps), fetched through https://en.wikipedia.org/w/api.php
- https://www.ipl.com (IPL 2026 auction unsold player list with base prices)
- https://cricsheet.org/downloads/ipl_json.zip and https://cricsheet.org/register/people.csv
- https://www.cricbuzz.com/cricket-team/{team}/{id}/players (CSK 58, RCB 59, DC 61, MI 62, KKR 63, RR 64, PBKS 65, SRH 255, LSG 966, GT 971) and https://www.cricbuzz.com/profiles/{id}/{slug} (born, styles, role, IPL/T20I career, international timeline)
- Web search snippets for bios missing from the above sources: Krish Bhagat, RS Ambrish, Emanjot Chahal and Macneil Noronha (IPLT20 and ESPNcricinfo player pages); Saurabh Dubey (https://en.wikipedia.org/wiki/2026_Kolkata_Knight_Riders_season, https://sports.yahoo.com/articles/saurabh-dubey-uncapped-seamer-joins-080700911.html); Utkarsh Singh (https://www.punjabkingsipl.in/players/67129-utkarsh-singh-profile, https://www.sportskeeda.com/player/utkarsh-singh)

## Field notes
- Team2026 is the original 25-man squad after the Nov 2025 retentions and trades plus the Dec 2025 auction. Retirees (R Ashwin, Andre Russell, Mohit Sharma) are excluded.
- The 16 in-season replacement signings have an empty Team2026, and Price2026Lakh is left empty for them. They are Mayank Agarwal, Blessing Muzarabani, Charith Asalanka, David Payne, Dian Forrester, Dilshan Madushanka, George Linde, Keshav Maharaj, Kusal Mendis, Navdeep Saini, Rehan Ahmed, Will O'Rourke, Emanjot Chahal, Krish Bhagat, Kuldip Yadav and Saurabh Dubey.
- BasePriceLakh is the actual 2026 auction base price, or else the 2025 mega-auction base price, for 271 rows. The other 129 rows are derived (mostly retained players, who never had an auction base):
  - capped: 200 if IPL matches >= 40 or contract >= 10 cr; 150 if IPL matches >= 15 or contract >= 4 cr; otherwise 100
  - uncapped: 50 if IPL matches >= 50; otherwise 30
- Teams.csv:
  - Captain2026 and HeadCoach2026 are the season-start appointments from the 2026 season page.
  - Owner is the owner as of Sep 2026. RCB was sold to an Aditya Birla-led consortium in 2026, and Lakshmi Mittal holds 75% of RR.
  - Titles are counted through 2026: RCB won 2025 and 2026. SRH's count excludes Deccan Chargers 2009.
  - PrimaryHex and SecondaryHex are approximate brand colours, not official hex codes.

## Ratings v2 (`ratings.py`, then `roster.py`)
`ratings.py` rewrites BatRating, BowlRating and Tags, keeping the original ratings in BatRatingV1/BowlRatingV1 so it
can be re-run. Two sources, best first:
- **Cricsheet** (when `Scripts/auction/cache/` holds `ipl_json.zip`, the other league zips `t20s bbl psl sat ilt mlc cpl
  hnd`, and `people.csv`/`names.csv` from cricsheet.org/register): samples weighted by recency (last IPL season 1.0, the
  season before 0.6, older 0.3) and by league (IPL 1.0, T20I 0.9, the big leagues 0.75-0.8, the Hundred 0.6); bowling
  economy judged against the par of the phase bowled (powerplay 8.6, middle 8.2, death 10.8 an over) so death
  specialists are not punished for bowling at the death; tags from batting position and phase splits; the last IPL
  season into Last* (the broadcast's form strip) and every non-IPL T20 into T20*. `cricsheet_ids.csv` (Name,Identifier)
  overrides name matching.
- **Fallback** (no cache, as committed): v1 shrunk toward the role prior by sample size (trust 1 - exp(-balls/350)
  batting, 1 - exp(-balls/500) bowling, so a season is not yet a career and a long career keeps its rating), less the
  age decline career numbers hide (1 a year 34-36, 2 a year 37-39, 3 after), less 5 for a 33-plus with no 2026 deal
  (the Dec 2025 auction passed him over). Tags from bowling style, runs per match and strike rate, plus scouting lists
  in the script for mystery spin, captaincy, new-ball and death specialists.
- Tags: Opener, Anchor, Finisher, KeeperBat, PowerplayPace, DeathPace, LeftArmPace, WristSpin, FingerSpin, Mystery,
  Captain. The auction AI builds its best-eleven model on them.
- `test_ratings.py` checks both paths (the Cricsheet one on synthetic match files).

## Teams.csv
Compiled into the game by `roster.py` (AuctionTeams.inl), so the franchise names, cities, grounds, owners, captains,
coaches and titles have one source. Short is the broadcast caption name; PrimaryHex/SecondaryHex are the colours the
game's room and kits were tuned with.

## Ratings v1 (0-99, clamped 10-99)
- Bat: calc = 25 + 35*min(avg,45)/45 + 30*clamp01((SR-100)/70) + 20*min(runs/6000,1). The result is blended with a role prior (BAT 60, WK 55, AR 48, PACE 22, SPIN 25) by weight min(ballsFaced/300,1). The T20I sample replaces the IPL sample when the player has faced fewer than 150 IPL balls and the T20I sample is larger.
- Bowl: calc = 20 + 1.15*(40*clamp01((10.5-econ)/4) + 25*clamp01((30-ballsPerWkt)/14) + 15*min(wkts/150,1)). The result is blended with a prior (PACE/SPIN 55, AR 45, BAT 20 or 12 if no bowling style, WK 10) by weight min(ballsBowled/480,1). The T20I sample is used below 240 IPL balls if it is larger.

## Doubtful rows
- Mustafizur Rahman: bought by KKR for 9.2 cr, then released in Jan 2026 at the BCCI's request and did not play. Kept as KKR because he is in the original squad. He has no Cricbuzz profile, so his stats are from Cricsheet (through 2025).
- Harry Brook: pool player. He withdrew from IPL 2025 and is under a two-year IPL ban, so he may be ineligible.
- Prithvi Shaw (DC) and Aman Khan (CSK): no Cricbuzz profile. Bio is from Wikipedia and stats are from Cricsheet (PP Shaw, Aman Hakim Khan).
- Cricbuzz name differences resolved: Sahil Parekh = Sahil Parakh, Tejasvi Singh = Tejasvi Dahiya, Harnoor Pannu = Harnoor Singh.
- Pool players matched to Cricsheet by initials and checked by team/year: Kuldip Yadav (K Yadav, RR) and Saurabh Dubey (SR Dubey, KKR 2026).
- Dropped because no verifiable bio or stats page was found: Abhinav Tejrana, Aarya Desai, Eden Apple Tom, Ruchit Ahir, Vansh Bedi, Tushar Raheja, Raj Limbani, Shivam Shukla, Wahidullah Zadran, Dheeraj Kumar, Irfan Umair, Chintal Gandhu, Ayush Vartak, Jikku Bright, Izaz Sawariya, Mani Sankar Mura Singh, Money Grewal, Siddharth Yadav, Ritik Tada, Swastik Chikara, Andre Siddarth and Manvanth Kumar L.
- Utkarsh Singh (unsold at 30 L) is assumed to be the Jharkhand all-rounder born 1998-05-07. The auction list gives no team to confirm this.

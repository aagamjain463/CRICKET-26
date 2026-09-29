# Real teams: sources

Compiled 2026-09-29 for the quick match ("Play Match"). `International.csv` has the twelve ICC full members' current
T20I squads, 183 players. Run `python3 Scripts/teams/international.py` after editing the squads or the ratings in that
script. It rewrites `International.csv` and `Source/CRICKET26/Teams/InternationalRoster.inl`, and refuses an XI that is
not 11 squad members with a keeper and five bowling options.

The IPL side of the quick match uses the original 2026 squads (`Team2026` in `Scripts/auction/Players.csv`, see
`Scripts/auction/SOURCES.md`), never the auction's squads. The IPL has no match XI in the data: the game picks each
franchise's best XI from its real squad (`IPLSeason::MakeDefaultXI`). The user can change the XI in match setup.

## Rules

- Squad: the latest T20I squad the board named, as found by web search on 2026-09-29. Where the board has named a squad
  for a series that has not started yet, that squad is used (New Zealand, West Indies).
- Playing XI: the XI of the latest T20I that squad played, in batting order as the scorecard source listed it. Where the
  XI came from an earlier match, the players no longer in the squad are replaced. Each team's row below says which.
- Ratings, role, batting hand and bowling style:
  - A player with 25 or more IPL matches keeps the IPL database's values (column `Rated` = `ipl`). They are a measured
    sample on the game's 0..99 scale.
  - A player with a smaller IPL sample and no estimate keeps the IPL row (`ipl-light`).
  - Every other player is rated by hand from their international T20 record on the same scale (`estimate`). These are
    judgements, not calculations. They are listed in `ESTIMATED` in `international.py`.
- Ages: from the birth year, a year either way for a few players.
- Kit colours (`Source/CRICKET26/Teams/RealTeams.cpp`): approximate brand colours, not official codes. No logos are used.

## Squads and XIs

| Team | Squad | Playing XI |
|---|---|---|
| India | Afghanistan T20Is, Sep 2026 (captain Shreyas Iyer) | 1st T20I v Afghanistan, Delhi, 13 Sep 2026 |
| Australia | Bangladesh T20Is, Jun 2026, with Nikhil Chaudhary added (captain Mitchell Marsh) | 1st T20I v Bangladesh, Chattogram, 17 Jun 2026 |
| England | Sri Lanka T20Is, Sep 2026 (captain Harry Brook) | 3rd T20I v Sri Lanka, Manchester, 19 Sep 2026 |
| South Africa | T20 World Cup 2026, after the Rickelton and Stubbs replacements (captain Aiden Markram) | Semi-final v New Zealand, Kolkata, 4 Mar 2026 |
| New Zealand | India T20Is from 22 Oct 2026 (captain Mitchell Santner) | T20 World Cup final, 8 Mar 2026, with Devon Conway and Michael Bracewell for Rachin Ravindra (injured) and James Neesham (not selected) |
| Pakistan | T20 World Cup 2026 (captain Salman Agha) | Super 8 v England, 24 Feb 2026 |
| Sri Lanka | England T20Is, Sep 2026, after the injury replacements (captain Charith Asalanka) | 3rd T20I v England, Manchester, 19 Sep 2026 |
| West Indies | India T20Is from 6 Oct 2026 (captain Shai Hope) | No T20I yet for this squad: a likely XI picked from it by hand |
| Bangladesh | Australia T20Is, Jun 2026 (captain Litton Das) | 3rd T20I v Australia, 21 Jun 2026. The source named nine; Mustafizur Rahman and Taskin Ahmed complete it, by assumption |
| Afghanistan | India T20Is, Sep 2026 (captain Ibrahim Zadran) | 1st T20I v India, Delhi, 13 Sep 2026 |
| Ireland | India T20Is, Jun 2026 (captain Lorcan Tucker; Stirling, Little, Mark Adair, Campher and Barry McCarthy injured) | A T20I v India, Belfast, Jun 2026 (the source did not say which of the two) |
| Zimbabwe | India T20Is, Jul 2026 (captain Sikandar Raza) | No XI found in the sources: a likely XI picked from the squad by hand |

## Search results used

- India: bcci.tv "India's squad for Afghanistan T20I series announced"; cricinfo scorecard and playing XI, IND v AFG 1st
  T20I (series 1549583, match 1549586).
- Australia: cricket.com.au "Marsh returns, Aussies name two debutants for T20 series opener" and "Chaudhary added to
  Australia's squad"; cricinfo series 1532475.
- England: cricinfo England T20I squad, Sri Lanka in England 2026 (1552838); ENG v SL 3rd T20I scorecard (1496587).
- South Africa: ICC and ESPN "South Africa add Ryan Rickelton, Tristan Stubbs to T20 World Cup squad"; semi-final XI
  from cricketaddictor and The SportsTak line-ups.
- New Zealand: crictracker "New Zealand announce 16-member squad for India T20Is, Rachin Ravindra ruled out"; final XI
  from cricinfo (match 1512773) and Sportskeeda.
- Pakistan: pcb.com.pk "Pakistan announce squad for ICC Men's T20 World Cup 2026"; Super 8 XIs from CricketCountry.
- Sri Lanka: srilankacricket.lk squads for the England tour; crickettimes "Kusal Mendis ruled out"; cricinfo 3rd T20I
  report (1496587).
- West Indies: windiescricket.com and olympics.com "West Indies squad for India T20I series 2026"; player profiles on
  cricinfo for Kamil Pooran, Quentin Sampson, Shamar Springer and Jewel Andrew.
- Bangladesh: ICC "Bangladesh name squads for Australia T20Is"; cricinfo 3rd T20I scorecard (1532485).
- Afghanistan: acb.af "ACB Names Squad for the T20I Series against India"; cricinfo IND v AFG 1st T20I playing XI.
- Ireland: Cricket Times "Ireland announce squad for India T20Is; Lorcan Tucker named new captain"; Wikipedia player
  pages for Jai Moondra, Ben Calitz and Tim Tector.
- Zimbabwe: Republic World and NewsX "Sikandar Raza leads 15-man Zimbabwe squad" (India series, Jul 2026).

## Doubtful

- Squads change every series. Check this table before a release and rerun the script.
- The West Indies and Zimbabwe XIs, and the last two Bangladesh bowlers, are picks from the named squad, not a match XI.
- Batting hand for Joel Davies and Matthew Hollard was not found: right-handed is assumed.
- Nikhil Chaudhary's bowling is recorded as leg-spin (the ICC's profile); Wikipedia lists off-spin.

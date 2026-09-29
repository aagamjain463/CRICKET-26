#!/usr/bin/env python3
"""Tests for ratings.py: python3 Scripts/auction/test_ratings.py"""
import csv, io, json, tempfile, unittest, zipfile
from pathlib import Path

import ratings

HEADER = ("Name,Short,Country,Role,BatHand,BowlStyle,Age,Capped,Team2026,Price2026Lakh,IplMatches,IplRuns,IplSR,"
          "IplWkts,IplEcon,T20Matches,T20Runs,T20SR,T20Wkts,T20Econ,BasePriceLakh,BatRating,BowlRating,Retained2026")


def roster(tmp, rows):
    p = Path(tmp) / "Players.csv"
    p.write_text(HEADER + "\n" + "\n".join(rows) + "\n")
    return p


def delivery(batter, bowler, runs=1, wide=False, wicket=None):
    d = {"batter": batter, "bowler": bowler, "non_striker": "Other Bat", "runs": {"batter": 0 if wide else runs,
         "extras": 1 if wide else 0, "total": runs}}
    if wide:
        d["extras"] = {"wides": 1}
    if wicket:
        d["wickets"] = [{"player_out": batter, "kind": wicket}]
    return d


def match(season, overs):
    return {"info": {"season": season, "dates": [f"{season}-04-01"], "match_type": "T20",
                     "registry": {"people": {"A Opener": "id-a", "B Death": "id-b", "Other Bat": "id-o", "C Spin": "id-c"}}},
            "innings": [{"team": "X", "overs": overs}]}


class Fallback(unittest.TestCase):
    def test_idempotent_and_shrinks_small_samples(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = roster(tmp, [
                "Kid Star,K Star,India,BAT,L,,17,1,RR,110,20,900,220,0,,,,,,,30,93,20,0",
                "Old Great,O Great,Australia,BAT,L,,39,1,,,180,6500,140,0,,,,,,,200,93,20,0",
                "Solid Pro,S Pro,India,BAT,R,,28,1,MI,1000,150,4500,140,0,,,,,,,200,85,20,1",
            ])
            first = ratings.run(p, Path(tmp) / "nocache", out=io.StringIO())
            second = ratings.run(p, Path(tmp) / "nocache", out=io.StringIO())
            self.assertEqual([r["BatRating"] for r in first], [r["BatRating"] for r in second], "idempotent")
            kid, old, pro = (int(r["BatRating"]) for r in second)
            self.assertLess(kid, 93, "a season is not yet a career")
            self.assertGreater(kid, 70, "but still a real talent")
            self.assertLess(old, 86, "a 39-year-old's career numbers fade")
            self.assertGreaterEqual(pro, 84, "a long prime career keeps its rating")
            self.assertEqual(second[0]["BatRatingV1"], "93")

    def test_tags_from_style(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = roster(tmp, [
                "Lefty Quick,L Quick,India,PACE,R,Left-arm fast,27,1,,,50,40,90,60,8.5,,,,,,100,20,75,0",
                "Leggie,L Leg,India,SPIN,R,Right-arm legbreak,27,1,,,50,40,90,60,7.5,,,,,,100,20,75,0",
                "Sunil Narine,S Narine,West Indies,AR,L,Right-arm offbreak,38,1,KKR,1200,201,1820,165,207,6.8,,,,,,200,73,95,1",
            ])
            rows = ratings.run(p, Path(tmp) / "nocache", out=io.StringIO())
            self.assertIn("LeftArmPace", rows[0]["Tags"])
            self.assertIn("WristSpin", rows[1]["Tags"])
            self.assertIn("Mystery", rows[2]["Tags"])
            self.assertIn("Captain", ratings.run(roster(tmp, [
                "Rishabh Pant,R Pant,India,WK,L,,28,1,LSG,2700,139,3865,146.8,0,,,,,,,200,84,10,1"]),
                Path(tmp) / "nocache", out=io.StringIO())[0]["Tags"])


class Cricsheet(unittest.TestCase):
    def test_phase_adjusted_economy_and_tags(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = Path(tmp) / "cache"
            cache.mkdir()
            # B Death bowls only at the death at 10 an over; C Spin bowls the middle at 8.5. Raw economy says C is
            # better, the phase pars say B is (10 is under the death par of 10.8, 8.5 is over the middle par).
            games = []
            for season in (2025, 2026):
                for g in range(8):
                    overs = []
                    for o in range(20):
                        bowler = "B Death" if o >= 15 else "C Spin"
                        runs = 10 if o >= 15 else 8.5
                        dl = [delivery("A Opener", bowler, runs=2 if i < runs - 6 else 1) for i in range(6)]
                        if o == 19 and g % 2 == 0:
                            dl[5] = delivery("A Opener", bowler, runs=0, wicket="bowled")
                        overs.append({"over": o, "deliveries": dl})
                    games.append(match(season, overs))
            with zipfile.ZipFile(cache / "ipl_json.zip", "w") as z:
                for i, gm in enumerate(games):
                    z.writestr(f"{i}.json", json.dumps(gm))
            (cache / "people.csv").write_text("identifier,name\nid-a,A Opener\nid-b,B Death\nid-c,C Spin\n")
            p = roster(tmp, [
                "A Opener,A Opener,India,BAT,R,,26,1,,,0,0,,0,,,,,,,100,50,20,0",
                "B Death,B Death,India,PACE,R,Right-arm fast,26,1,,,0,0,,0,,,,,,,100,20,50,0",
                "C Spin,C Spin,India,SPIN,R,Right-arm offbreak,26,1,,,0,0,,0,,,,,,,100,20,50,0",
            ])
            rows = ratings.run(p, cache, out=io.StringIO())
            a, b, c = rows
            self.assertIn("Opener", a["Tags"])
            self.assertIn("DeathPace", b["Tags"])
            self.assertGreater(int(b["BowlRating"]), int(c["BowlRating"]), "death economy judged against the death par")
            self.assertEqual(a["LastMatches"], "8", "the form strip is the latest IPL season")


if __name__ == "__main__":
    unittest.main()

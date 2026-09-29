#!/usr/bin/env python3
"""Builds Scripts/teams/International.csv (the twelve ICC full members' current T20I squads and playing XIs) and
compiles it into Source/CRICKET26/Teams/InternationalRoster.inl, so the squads ship inside the binary like the IPL
roster (Content/ is not versioned). Sources and the rules behind every column are in Scripts/teams/SOURCES.md.

A player who has played 25 or more IPL matches keeps the ratings, role and styles of Scripts/auction/Players.csv (a
real sample on the same 0..99 scale). Everyone else is rated here from their international T20 record on that scale:
see ESTIMATED below. Usage: python3 Scripts/teams/international.py"""
import csv, io, sys, unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IPL = ROOT / "Scripts/auction/Players.csv"
OUT_CSV = ROOT / "Scripts/teams/International.csv"
OUT_INL = ROOT / "Source/CRICKET26/Teams/InternationalRoster.inl"
IPL_SAMPLE = 25  # IPL matches from which the IPL database's ratings are kept

# code: (name, captain, squad in the order announced, playing XI in batting order). See SOURCES.md for each.
TEAMS = {
    "IND": ("India", "Shreyas Iyer",
            ["Shreyas Iyer", "Abhishek Sharma", "Vaibhav Sooryavanshi", "Sanju Samson", "Ishan Kishan", "Tilak Varma", "Shivam Dube",
             "Nitish Kumar Reddy", "Axar Patel", "Washington Sundar", "Varun Chakravarthy", "Ravi Bishnoi", "Jasprit Bumrah",
             "Arshdeep Singh", "Harshit Rana"],
            ["Abhishek Sharma", "Sanju Samson", "Ishan Kishan", "Shreyas Iyer", "Tilak Varma", "Nitish Kumar Reddy", "Shivam Dube",
             "Axar Patel", "Arshdeep Singh", "Jasprit Bumrah", "Varun Chakravarthy"]),
    "AUS": ("Australia", "Mitchell Marsh",
            ["Mitchell Marsh", "Xavier Bartlett", "Nikhil Chaudhary", "Cooper Connolly", "Tim David", "Joel Davies", "Nathan Ellis",
             "Cameron Green", "Aaron Hardie", "Travis Head", "Josh Inglis", "Spencer Johnson", "Matthew Kuhnemann", "Riley Meredith",
             "Josh Philippe", "Matthew Renshaw", "Adam Zampa"],
            ["Mitchell Marsh", "Josh Inglis", "Cooper Connolly", "Matthew Renshaw", "Tim David", "Nikhil Chaudhary", "Joel Davies",
             "Xavier Bartlett", "Nathan Ellis", "Spencer Johnson", "Adam Zampa"]),
    "ENG": ("England", "Harry Brook",
            ["Harry Brook", "Ben Duckett", "James Coles", "Will Jacks", "Gus Atkinson", "Liam Dawson", "Jamie Overton", "Tom Banton",
             "Jos Buttler", "Jordan Cox", "Aneurin Donald", "Jofra Archer", "Sonny Baker", "Saqib Mahmood", "Josh Tongue", "Adil Rashid"],
            ["Aneurin Donald", "Jos Buttler", "Harry Brook", "Tom Banton", "Jordan Cox", "Will Jacks", "Jamie Overton", "Jofra Archer",
             "Liam Dawson", "Sonny Baker", "Adil Rashid"]),
    "SA": ("South Africa", "Aiden Markram",
           ["Aiden Markram", "Corbin Bosch", "Dewald Brevis", "Quinton de Kock", "Marco Jansen", "George Linde", "Keshav Maharaj",
            "Kwena Maphaka", "David Miller", "Lungi Ngidi", "Anrich Nortje", "Kagiso Rabada", "Ryan Rickelton", "Jason Smith",
            "Tristan Stubbs"],
           ["Aiden Markram", "Quinton de Kock", "Ryan Rickelton", "Dewald Brevis", "David Miller", "Tristan Stubbs", "Marco Jansen",
            "Corbin Bosch", "Kagiso Rabada", "Keshav Maharaj", "Lungi Ngidi"]),
    "NZ": ("New Zealand", "Mitchell Santner",
           ["Mitchell Santner", "Finn Allen", "Michael Bracewell", "Mark Chapman", "Devon Conway", "Jacob Duffy", "Lockie Ferguson",
            "Zak Foulkes", "Matt Henry", "Bevon Jacobs", "Kyle Jamieson", "Adam Milne", "Daryl Mitchell", "Glenn Phillips", "Tim Seifert",
            "Nathan Smith"],
           ["Tim Seifert", "Finn Allen", "Devon Conway", "Glenn Phillips", "Mark Chapman", "Daryl Mitchell", "Mitchell Santner",
            "Michael Bracewell", "Jacob Duffy", "Matt Henry", "Lockie Ferguson"]),
    "PAK": ("Pakistan", "Salman Agha",
            ["Salman Agha", "Abrar Ahmed", "Babar Azam", "Faheem Ashraf", "Fakhar Zaman", "Khawaja Mohammad Nafay", "Mohammad Nawaz",
             "Salman Mirza", "Naseem Shah", "Sahibzada Farhan", "Saim Ayub", "Shaheen Afridi", "Shadab Khan", "Usman Khan", "Usman Tariq"],
            ["Sahibzada Farhan", "Saim Ayub", "Salman Agha", "Babar Azam", "Fakhar Zaman", "Usman Khan", "Shadab Khan", "Mohammad Nawaz",
             "Shaheen Afridi", "Salman Mirza", "Usman Tariq"]),
    "SL": ("Sri Lanka", "Charith Asalanka",
           ["Charith Asalanka", "Pathum Nissanka", "Kamil Mishara", "Lahiru Udara", "Kamindu Mendis", "Janith Liyanage", "Dasun Shanaka",
            "Dunith Wellalage", "Wanindu Hasaranga", "Maheesh Theekshana", "Tharindu Rathnayake", "Dushmantha Chameera", "Eshan Malinga",
            "Nuwan Thushara", "Dilshan Madushanka"],
           ["Pathum Nissanka", "Lahiru Udara", "Kamil Mishara", "Charith Asalanka", "Kamindu Mendis", "Dasun Shanaka", "Wanindu Hasaranga",
            "Tharindu Rathnayake", "Eshan Malinga", "Dilshan Madushanka", "Nuwan Thushara"]),
    "WI": ("West Indies", "Shai Hope",
           ["Shai Hope", "Jewel Andrew", "Roston Chase", "Matthew Forde", "Shimron Hetmyer", "Akeal Hosein", "Shamar Joseph",
            "Gudakesh Motie", "Keemo Paul", "Kamil Pooran", "Rovman Powell", "Sherfane Rutherford", "Quentin Sampson", "Romario Shepherd",
            "Shamar Springer"],
           ["Shai Hope", "Jewel Andrew", "Shimron Hetmyer", "Sherfane Rutherford", "Rovman Powell", "Roston Chase", "Romario Shepherd",
            "Matthew Forde", "Akeal Hosein", "Gudakesh Motie", "Shamar Joseph"]),
    "BAN": ("Bangladesh", "Litton Das",
            ["Litton Das", "Parvez Hossain Emon", "Tanzid Hasan", "Saif Hassan", "Shamim Hossain", "Towhid Hridoy", "Nurul Hasan",
             "Mahedi Hasan", "Nasum Ahmed", "Rishad Hossain", "Shoriful Islam", "Mustafizur Rahman", "Taskin Ahmed", "Nahid Rana",
             "Abdul Gaffar Saqlain"],
            ["Tanzid Hasan", "Saif Hassan", "Parvez Hossain Emon", "Towhid Hridoy", "Nurul Hasan", "Shamim Hossain", "Rishad Hossain",
             "Nasum Ahmed", "Shoriful Islam", "Mustafizur Rahman", "Taskin Ahmed"]),
    "AFG": ("Afghanistan", "Ibrahim Zadran",
            ["Ibrahim Zadran", "Rahmanullah Gurbaz", "Noor Rahman", "Sediqullah Atal", "Darwish Rasooli", "Azmatullah Omarzai",
             "Gulbadin Naib", "Mohammad Nabi", "Rashid Khan", "Nangyal Kharoti", "Noor Ahmad", "Mujeeb Ur Rahman", "Fazalhaq Farooqi",
             "Abdullah Ahmadzai", "Naveen-ul-Haq"],
            ["Sediqullah Atal", "Rahmanullah Gurbaz", "Ibrahim Zadran", "Darwish Rasooli", "Azmatullah Omarzai", "Mohammad Nabi",
             "Rashid Khan", "Noor Ahmad", "Mujeeb Ur Rahman", "Fazalhaq Farooqi", "Naveen-ul-Haq"]),
    "IRE": ("Ireland", "Lorcan Tucker",
            ["Lorcan Tucker", "Ross Adair", "Ben Calitz", "Gareth Delany", "George Dockrell", "Stephen Doheny", "Matthew Humphreys",
             "Gavin Hoey", "Matthew Hollard", "Liam McCarthy", "Jai Moondra", "Harry Tector", "Tim Tector", "Reuben Wilson"],
            ["Tim Tector", "Ross Adair", "Harry Tector", "Lorcan Tucker", "Ben Calitz", "Gareth Delany", "George Dockrell",
             "Matthew Humphreys", "Matthew Hollard", "Liam McCarthy", "Jai Moondra"]),
    "ZIM": ("Zimbabwe", "Sikandar Raza",
            ["Sikandar Raza", "Brian Bennett", "Ryan Burl", "Tanaka Chivanga", "Ben Curran", "Brad Evans", "Wessly Madhevere",
             "Tadiwanashe Marumani", "Wellington Masakadza", "Blessing Muzarabani", "Dion Myers", "Richard Ngarava", "Newman Nyamhuri",
             "Milton Shumba", "Tafadzwa Tsiga"],
            ["Brian Bennett", "Tadiwanashe Marumani", "Dion Myers", "Sikandar Raza", "Wessly Madhevere", "Ryan Burl", "Brad Evans",
             "Wellington Masakadza", "Richard Ngarava", "Blessing Muzarabani", "Tanaka Chivanga"]),
}

# Players rated here: (role, bat hand, bowling style, age on 2026-09-29, bat rating, bowl rating). Roles and styles are
# the players' recorded ones; ages are from their birth year (a year either way for a few). Ratings follow the IPL
# database's 0..99 scale from each player's international T20 record (a regular international opener or top-order
# batter 70-82, a finisher 60-75, a strike bowler 72-85; a batter's bowling or a bowler's batting sits at the role prior
# of 10-30), judged by hand, not computed: they are estimates.
ESTIMATED = {
    # Australia
    "Nikhil Chaudhary": ("AR", "R", "Right-arm legbreak", 30, 50, 55),
    "Joel Davies": ("SPIN", "R", "Left-arm orthodox", 24, 22, 62),
    "Matthew Kuhnemann": ("SPIN", "L", "Left-arm orthodox", 30, 22, 66),
    "Matthew Renshaw": ("BAT", "L", "Right-arm offbreak", 30, 70, 30),
    "Josh Philippe": ("WK", "R", "", 29, 62, 10), "Aaron Hardie": ("AR", "R", "Right-arm fast-medium", 27, 56, 58),
    "Adam Zampa": ("SPIN", "R", "Right-arm legbreak", 34, 25, 82),
    # England
    "James Coles": ("AR", "R", "Left-arm orthodox", 22, 55, 52),
    "Liam Dawson": ("AR", "R", "Left-arm orthodox", 36, 45, 72),
    "Aneurin Donald": ("BAT", "R", "", 29, 64, 10),
    "Sonny Baker": ("PACE", "R", "Right-arm fast", 23, 18, 66),
    "Saqib Mahmood": ("PACE", "R", "Right-arm fast-medium", 29, 20, 72),
    "Josh Tongue": ("PACE", "R", "Right-arm fast-medium", 29, 18, 64),
    "Harry Brook": ("BAT", "R", "Right-arm medium", 27, 80, 20), "Adil Rashid": ("SPIN", "R", "Right-arm legbreak", 38, 26, 82),
    "Jordan Cox": ("WK", "R", "", 26, 62, 10), "Tom Banton": ("WK", "R", "", 27, 66, 10),
    "Ben Duckett": ("BAT", "L", "", 31, 70, 10), "Gus Atkinson": ("PACE", "R", "Right-arm fast", 28, 22, 68),
    "Jamie Overton": ("AR", "R", "Right-arm fast", 32, 55, 66),
    # South Africa
    "Jason Smith": ("AR", "R", "Right-arm medium", 32, 60, 30),
    # Pakistan
    "Salman Agha": ("AR", "R", "Right-arm offbreak", 32, 64, 50),
    "Abrar Ahmed": ("SPIN", "R", "Right-arm legbreak", 28, 18, 76),
    "Babar Azam": ("BAT", "R", "Right-arm offbreak", 31, 82, 10),
    "Faheem Ashraf": ("AR", "L", "Right-arm fast-medium", 32, 52, 62),
    "Fakhar Zaman": ("BAT", "L", "Left-arm orthodox", 36, 74, 10),
    "Khawaja Mohammad Nafay": ("WK", "R", "", 24, 60, 10),
    "Mohammad Nawaz": ("AR", "L", "Left-arm orthodox", 32, 58, 72),
    "Salman Mirza": ("PACE", "L", "Left-arm fast-medium", 31, 18, 70),
    "Naseem Shah": ("PACE", "R", "Right-arm fast", 23, 22, 74),
    "Sahibzada Farhan": ("WK", "R", "", 30, 76, 10),
    "Saim Ayub": ("AR", "L", "Right-arm offbreak", 24, 72, 45),
    "Shaheen Afridi": ("PACE", "L", "Left-arm fast", 26, 25, 82),
    "Shadab Khan": ("AR", "R", "Right-arm legbreak", 28, 58, 72),
    "Usman Khan": ("WK", "R", "", 31, 64, 10),
    "Usman Tariq": ("SPIN", "R", "Right-arm offbreak", 30, 15, 76),
    # Sri Lanka
    "Kamil Mishara": ("WK", "L", "", 25, 60, 10),
    "Lahiru Udara": ("WK", "R", "", 33, 58, 10),
    "Janith Liyanage": ("AR", "R", "Right-arm medium", 30, 58, 40),
    "Tharindu Rathnayake": ("SPIN", "L", "Right-arm offbreak", 30, 25, 62),
    "Pathum Nissanka": ("BAT", "R", "", 28, 78, 12), "Charith Asalanka": ("BAT", "L", "Right-arm offbreak", 29, 68, 30),
    "Dasun Shanaka": ("AR", "R", "Right-arm medium", 35, 58, 52), "Eshan Malinga": ("PACE", "R", "Right-arm fast-medium", 25, 16, 64),
    # West Indies
    "Jewel Andrew": ("WK", "R", "", 19, 58, 10),
    "Roston Chase": ("AR", "R", "Right-arm offbreak", 34, 60, 60),
    "Matthew Forde": ("PACE", "R", "Right-arm fast-medium", 24, 30, 68),
    "Keemo Paul": ("AR", "R", "Right-arm fast-medium", 28, 44, 62),
    "Kamil Pooran": ("BAT", "R", "Right-arm medium", 30, 60, 20),
    "Quentin Sampson": ("BAT", "R", "Right-arm medium-fast", 26, 56, 26),
    "Shamar Springer": ("AR", "R", "Right-arm medium-fast", 28, 52, 58),
    "Shai Hope": ("WK", "R", "", 32, 76, 10), "Akeal Hosein": ("SPIN", "L", "Left-arm orthodox", 33, 30, 76),
    "Gudakesh Motie": ("SPIN", "L", "Left-arm orthodox", 31, 28, 72),
    # Bangladesh
    "Parvez Hossain Emon": ("BAT", "L", "", 24, 62, 10),
    "Tanzid Hasan": ("BAT", "L", "", 25, 66, 10),
    "Saif Hassan": ("BAT", "R", "Right-arm offbreak", 28, 60, 25),
    "Shamim Hossain": ("AR", "L", "Right-arm offbreak", 26, 58, 30),
    "Nurul Hasan": ("WK", "R", "", 33, 56, 10),
    "Mahedi Hasan": ("AR", "R", "Right-arm offbreak", 32, 45, 68),
    "Nasum Ahmed": ("SPIN", "L", "Left-arm orthodox", 32, 24, 70),
    "Shoriful Islam": ("PACE", "L", "Left-arm fast-medium", 25, 18, 70),
    "Nahid Rana": ("PACE", "R", "Right-arm fast", 23, 12, 66),
    "Abdul Gaffar Saqlain": ("PACE", "R", "Right-arm fast-medium", 26, 25, 58),
    "Litton Das": ("WK", "R", "", 31, 68, 10), "Towhid Hridoy": ("BAT", "R", "Right-arm offbreak", 25, 68, 20),
    "Taskin Ahmed": ("PACE", "L", "Right-arm fast", 31, 22, 74), "Rishad Hossain": ("SPIN", "R", "Right-arm legbreak", 24, 40, 72),
    # Afghanistan
    "Noor Rahman": ("WK", "R", "", 22, 52, 10),
    "Darwish Rasooli": ("BAT", "R", "Right-arm offbreak", 27, 60, 20),
    "Nangyal Kharoti": ("SPIN", "L", "Left-arm orthodox", 22, 30, 64),
    "Abdullah Ahmadzai": ("PACE", "R", "Right-arm medium-fast", 23, 18, 64),
    "Sediqullah Atal": ("BAT", "L", "", 24, 66, 10),
    "Mohammad Nabi": ("AR", "R", "Right-arm offbreak", 41, 60, 68), "Gulbadin Naib": ("AR", "R", "Right-arm medium", 35, 54, 52),
    # Ireland
    "Lorcan Tucker": ("WK", "R", "", 30, 66, 10),
    "Ross Adair": ("BAT", "R", "", 32, 60, 10),
    "Ben Calitz": ("BAT", "R", "Right-arm offbreak", 24, 56, 20),
    "Gareth Delany": ("AR", "R", "Right-arm legbreak", 29, 56, 55),
    "George Dockrell": ("AR", "L", "Left-arm orthodox", 34, 55, 58),
    "Stephen Doheny": ("WK", "R", "", 28, 50, 10),
    "Matthew Humphreys": ("SPIN", "R", "Left-arm orthodox", 24, 15, 62),
    "Gavin Hoey": ("SPIN", "R", "Right-arm legbreak", 26, 15, 58),
    "Matthew Hollard": ("PACE", "R", "Right-arm fast-medium", 24, 15, 60),
    "Liam McCarthy": ("PACE", "R", "Right-arm fast-medium", 24, 20, 60),
    "Jai Moondra": ("PACE", "L", "Left-arm fast-medium", 27, 15, 62),
    "Harry Tector": ("BAT", "R", "Right-arm offbreak", 27, 68, 20),
    "Tim Tector": ("BAT", "R", "Right-arm offbreak", 23, 56, 20),
    "Reuben Wilson": ("AR", "R", "Right-arm medium", 24, 45, 50),
    # Zimbabwe
    "Sikandar Raza": ("AR", "R", "Right-arm offbreak", 40, 68, 66),
    "Brian Bennett": ("BAT", "R", "Right-arm offbreak", 22, 66, 30),
    "Ryan Burl": ("AR", "L", "Right-arm legbreak", 32, 56, 52),
    "Tanaka Chivanga": ("PACE", "R", "Right-arm fast-medium", 27, 12, 58),
    "Ben Curran": ("BAT", "L", "", 29, 55, 10),
    "Brad Evans": ("PACE", "R", "Right-arm fast-medium", 29, 30, 62),
    "Wessly Madhevere": ("AR", "R", "Right-arm offbreak", 26, 58, 45),
    "Tadiwanashe Marumani": ("WK", "L", "", 24, 58, 10),
    "Wellington Masakadza": ("SPIN", "L", "Left-arm orthodox", 33, 22, 64),
    "Blessing Muzarabani": ("PACE", "R", "Right-arm fast-medium", 29, 18, 70),
    "Dion Myers": ("AR", "R", "Right-arm medium", 24, 55, 35),
    "Richard Ngarava": ("PACE", "L", "Left-arm fast-medium", 28, 18, 68),
    "Newman Nyamhuri": ("PACE", "L", "Left-arm fast-medium", 20, 12, 58),
    "Milton Shumba": ("AR", "L", "Left-arm orthodox", 26, 50, 45),
    "Tafadzwa Tsiga": ("WK", "R", "", 32, 48, 10),
}

HEADER = ["Name", "Short", "Country", "Role", "BatHand", "BowlStyle", "Age", "Capped", "Team", "XI", "Captain", "BatRating",
          "BowlRating", "Rated"]


def short(name):
    parts = name.split()
    return name if len(parts) == 1 else parts[0][0] + " " + " ".join(parts[1:])


def main():
    ipl = {r["Name"]: r for r in csv.DictReader(io.open(IPL, encoding="utf-8-sig"))}
    out = []
    for code, (country, captain, squad, xi) in TEAMS.items():
        assert len(xi) == 11 and len(set(xi)) == 11, code
        assert len(set(squad)) == len(squad) and all(p in squad for p in xi), code
        assert captain in squad, code
        for name in squad:
            r = ipl.get(name)
            if r and int(r["IplMatches"] or 0) >= IPL_SAMPLE:
                role, hand, style, age, bat, bowl = r["Role"], r["BatHand"], r["BowlStyle"], r["Age"], r["BatRating"], r["BowlRating"]
                rated, sh = "ipl", r["Short"]
            else:
                est = ESTIMATED.get(name)
                if est is None and r:  # a light IPL sample and no estimate: the IPL row still beats nothing
                    est = (r["Role"], r["BatHand"], r["BowlStyle"], int(r["Age"]), int(r["BatRating"]), int(r["BowlRating"]))
                assert est, "no rating for %s (%s)" % (name, code)
                role, hand, style, age, bat, bowl = est
                rated, sh = "estimate" if name in ESTIMATED else "ipl-light", (r["Short"] if r else short(name))
            pos = xi.index(name) + 1 if name in xi else ""
            out.append([name, sh, country, role, hand, style, age, 1, code, pos, 1 if name == captain else 0, bat, bowl, rated])
        keepers = [n for n in xi if next(o for o in out if o[0] == n and o[8] == code)[3] == "WK"]
        bowlers = [n for n in xi if next(o for o in out if o[0] == n and o[8] == code)[3] in ("PACE", "SPIN", "AR")]
        assert keepers, "%s XI has no keeper" % code
        assert len(bowlers) >= 5, "%s XI has %d bowling options" % (code, len(bowlers))
    with io.open(OUT_CSV, "w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(HEADER)
        w.writerows(out)

    def cell(s):
        s = unicodedata.normalize("NFKD", str(s)).encode("ascii", "ignore").decode()
        return s.replace(",", " ").replace('"', "").replace("\\", "").strip()
    lines = ["// Generated by Scripts/teams/international.py from Scripts/teams/International.csv. Do not edit.",
             "static const TCHAR* InternationalRows[] =", "{"]
    for r in [HEADER] + out:
        lines.append('\tTEXT("%s"),' % ",".join(cell(c) for c in r))
    lines.append("};")
    OUT_INL.parent.mkdir(parents=True, exist_ok=True)
    OUT_INL.write_text("\n".join(lines) + "\n")
    counts = {}
    for r in out:
        counts[r[-1]] = counts.get(r[-1], 0) + 1
    print("%d players in %d squads -> %s, %s (%s)" % (len(out), len(TEAMS), OUT_CSV.relative_to(ROOT), OUT_INL.relative_to(ROOT),
                                                    ", ".join("%s %d" % kv for kv in sorted(counts.items()))))


if __name__ == "__main__":
    sys.exit(main())

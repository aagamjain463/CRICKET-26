#!/usr/bin/env python3
"""Ratings v2 for the auction roster: rewrites BatRating, BowlRating, Tags, Last* and T20* in Players.csv.

Two sources, best first:
  1. Cricsheet ball-by-ball (when Scripts/auction/cache/ holds the downloads, see SOURCES.md): ratings from
     recency-weighted samples (last IPL season 1.0, the one before 0.6, older 0.3; other T20 leagues at a league
     weight), bowling economy judged against the par of the phase it was bowled in (a death bowler is not punished
     for bowling at the death), batting position and phase splits for the tags, the last IPL season for the
     broadcast's form strip, and every non-IPL T20 for the T20* columns.
  2. Without it, the v1 career ratings (kept in BatRatingV1/BowlRatingV1 so this stays idempotent), shrunk toward the
     role prior by sample size, with the age decline career numbers hide, and tags from the columns we have.

Usage: Scripts/auction/ratings.py [--cache DIR] [--csv Players.csv] [--dry-run]
Then Scripts/auction/roster.py compiles the CSV into the game.
"""
import argparse, csv, io, json, math, os, re, sys, zipfile
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent

# ---- Shared shape ------------------------------------------------------------------------------------------------

ROLE_BAT_PRIOR = {"BAT": 60, "WK": 55, "AR": 48, "PACE": 22, "SPIN": 25}
ROLE_BOWL_PRIOR = {"PACE": 55, "SPIN": 55, "AR": 45, "BAT": 20, "WK": 10}

# Scouting knowledge the numbers cannot show: bowlers whose variations batters cannot read, and players who have
# led an IPL or international T20 side (the auction pays for a captain). Current captains come from Teams.csv.
MYSTERY = {"Sunil Narine", "Varun Chakravarthy", "Maheesh Theekshana", "Akila Dananjaya", "Mujeeb Ur Rahman",
           "Noor Ahmad", "Allah Ghazanfar", "Kuldeep Yadav", "Rashid Khan", "Digvesh Rathi"}
CAPTAINS = {"Rishabh Pant", "Shreyas Iyer", "KL Rahul", "Shubman Gill", "Ruturaj Gaikwad", "Rajat Patidar", "Axar Patel",
            "Riyan Parag", "Hardik Pandya", "Pat Cummins", "Ajinkya Rahane", "Sanju Samson", "Suryakumar Yadav",
            "Rohit Sharma", "MS Dhoni", "Virat Kohli", "David Warner", "Faf du Plessis", "Jos Buttler", "Aiden Markram",
            "Mitchell Marsh", "Kane Williamson", "Nicholas Pooran", "Heinrich Klaasen", "Rashid Khan", "Sam Curran",
            "Liam Livingstone", "Glenn Maxwell", "Travis Head", "Quinton de Kock", "Ishan Kishan", "Venkatesh Iyer"}
# Known new-ball and death specialists, used only when there is no ball-by-ball data to find them.
POWERPLAY_PACE = {"Bhuvneshwar Kumar", "Trent Boult", "Deepak Chahar", "Mohammed Siraj", "Mitchell Starc",
                  "Mohammed Shami", "Josh Hazlewood", "Jofra Archer", "Mukesh Kumar", "Khaleel Ahmed", "Marco Jansen",
                  "Lockie Ferguson", "Anrich Nortje", "Kagiso Rabada", "Yash Dayal"}
DEATH_PACE = {"Jasprit Bumrah", "Arshdeep Singh", "Matheesha Pathirana", "T Natarajan", "Harshal Patel",
              "Prasidh Krishna", "Avesh Khan", "Harshit Rana", "Mohsin Khan", "Tushar Deshpande", "Pat Cummins",
              "Mustafizur Rahman", "Lockie Ferguson", "Anrich Nortje", "Kagiso Rabada", "Josh Hazlewood",
              "Mohammed Shami", "Akash Madhwal", "Yash Thakur", "Sandeep Sharma"}

# The in-season replacement signings of 2026 (SOURCES.md): unattached in Team2026 but playing.
REPLACEMENTS_2026 = {"Mayank Agarwal", "Blessing Muzarabani", "Charith Asalanka", "David Payne", "Dian Forrester",
                     "Dilshan Madushanka", "George Linde", "Keshav Maharaj", "Kusal Mendis", "Navdeep Saini", "Rehan Ahmed",
                     "Will O'Rourke", "Emanjot Chahal", "Krish Bhagat", "Kuldip Yadav", "Saurabh Dubey"}
UNATTACHED_VETERAN = 5  # rating points: a 33-plus with no 2026 deal, whom the Dec 2025 auction already passed over

TAGS = ["Opener", "Anchor", "Finisher", "KeeperBat", "PowerplayPace", "DeathPace", "LeftArmPace", "WristSpin",
        "FingerSpin", "Mystery", "Captain"]

# Phase pars for recent IPL seasons (runs an over), and the par of a whole spell.
PAR = {"pp": 8.6, "mid": 8.2, "death": 10.8}
PAR_ALL = 9.0

# How much a ball in each competition counts toward a rating (Cricsheet's file names).
LEAGUES = {"ipl": 1.0, "t20s": 0.9, "bbl": 0.8, "psl": 0.8, "sat": 0.8, "ilt": 0.8, "mlc": 0.75, "cpl": 0.75,
           "hnd": 0.6}


def clamp01(x):
    return max(0.0, min(1.0, x))


def clamp_rating(x):
    return int(round(max(10, min(99, x))))


def age_decline(age):
    """Rating points a career aggregate overstates at this age: a slow fade after 33 that quickens later."""
    d = 0.0
    for a in range(34, age + 1):
        d += 1.0 if a <= 36 else 2.0 if a <= 39 else 3.0
    return d


def bat_calc(runs, balls, outs):
    avg = runs / max(outs, 1) if balls else 0.0
    sr = 100.0 * runs / balls if balls else 0.0
    return 25 + 40 * min(avg, 45) / 45 + 35 * clamp01((sr - 100) / 70)


def bowl_calc(runs, balls, wkts):
    econ = 6.0 * runs / balls if balls else PAR_ALL
    bpw = balls / wkts if wkts else 40.0
    # v1 also paid 15 points for career wickets; v2 rates the weighted sample only, and the 9 keeps the scale.
    return 20 + 1.15 * (40 * clamp01((10.5 - econ) / 4) + 25 * clamp01((30 - bpw) / 14)) + 9


def trust(n, k):
    """How far a sample of n balls is trusted: nearly nothing at a handful, all of it over a long career."""
    return 1.0 - math.exp(-n / k) if n > 0 else 0.0


def shrink(calc, prior, n, k):
    return prior + (calc - prior) * trust(n, k)


def is_pace(style):
    s = style.lower()
    return any(t in s for t in ("fast", "medium"))


def spin_kind(style):
    s = style.lower()
    if "legbreak" in s or "googly" in s or "wrist" in s:
        return "WristSpin"
    if "offbreak" in s or "orthodox" in s:
        return "FingerSpin"
    return ""


# ---- Fallback: the roster's own columns --------------------------------------------------------------------------

def fallback_player(p, captains):
    role = p["Role"]
    m = int(p["IplMatches"] or 0)
    runs = int(p["IplRuns"] or 0)
    sr = float(p["IplSR"] or 0)
    wkts = int(p["IplWkts"] or 0)
    econ = float(p["IplEcon"] or 0)
    age = int(p["Age"] or 0)
    bat1, bowl1 = int(p["BatRatingV1"]), int(p["BowlRatingV1"])

    # v1 blended with its role prior by min(balls/300, 1) (batting) and min(balls/480, 1) (bowling). v2 shrinks by
    # n/(n+k) instead, so a season of fireworks is not yet a career: rescale v1's distance from the prior.
    balls_faced = runs / sr * 100 if sr > 0 else 0
    w1 = min(balls_faced / 300, 1) if balls_faced else 0
    w2 = trust(balls_faced, 350)
    bprior = ROLE_BAT_PRIOR.get(role, 40)
    bat = bprior + (bat1 - bprior) * (w2 / w1) if w1 > 0 else bat1
    per_match = {"PACE": 21, "SPIN": 21, "AR": 12}.get(role, 3)
    balls_bowled = m * per_match if wkts or econ else 0
    v1 = min(balls_bowled / 480, 1) if balls_bowled else 0
    v2 = trust(balls_bowled, 500)
    wprior = ROLE_BOWL_PRIOR.get(role, 20)
    bowl = wprior + (bowl1 - wprior) * (v2 / v1) if v1 > 0 else bowl1

    # Career aggregates carry a veteran's best years; take off what age has since taken, and more for a veteran the
    # last auction passed over (no recent-form data to say otherwise without Cricsheet).
    d = age_decline(age)
    if not p["Team2026"] and p["Name"] not in REPLACEMENTS_2026 and age >= 33 and m > 0:
        d += UNATTACHED_VETERAN
    bat -= d
    bowl -= d if bowl > wprior else 0

    tags = set()
    style = p["BowlStyle"]
    rpm = runs / m if m else 0
    if role in ("BAT", "WK", "AR") and m >= 8:
        if rpm >= 24 and sr >= 128:
            tags.add("Opener" if sr >= 138 or rpm >= 30 else "Anchor")
        elif rpm >= 20 and sr < 128:
            tags.add("Anchor")
        if sr >= 150 and rpm < 24:
            tags.add("Finisher")
    if role == "WK" and bat >= 68:
        tags.add("KeeperBat")
    if role in ("PACE", "AR") and is_pace(style):
        if style.lower().startswith("left-arm"):
            tags.add("LeftArmPace")
        if p["Name"] in POWERPLAY_PACE:
            tags.add("PowerplayPace")
        if p["Name"] in DEATH_PACE:
            tags.add("DeathPace")
    if role in ("SPIN", "AR"):
        k = spin_kind(style)
        if k:
            tags.add(k)
    if p["Name"] in MYSTERY:
        tags.add("Mystery")
    if p["Name"] in CAPTAINS or p["Name"] in captains:
        tags.add("Captain")
    return clamp_rating(bat), clamp_rating(bowl), tags, None, None


# ---- Cricsheet ---------------------------------------------------------------------------------------------------

def season_year(info):
    s = str(info.get("season", ""))
    m = re.match(r"(\d{4})", s)
    return int(m.group(1)) if m else int(info["dates"][0][:4])


def phase_of(over):
    return "pp" if over < 6 else "mid" if over < 15 else "death"


class Sample:
    """Counts for one player in one competition and season."""
    __slots__ = ("matches", "bruns", "bballs", "outs", "positions", "death_runs", "death_balls", "wruns", "wballs",
                 "wkts", "phase_balls", "phase_runs")

    def __init__(self):
        self.matches = 0
        self.bruns = self.bballs = self.outs = 0
        self.positions = []
        self.death_runs = self.death_balls = 0
        self.wruns = self.wballs = self.wkts = 0
        self.phase_balls = defaultdict(int)
        self.phase_runs = defaultdict(int)


def read_match(data, league, stats):
    """Adds one Cricsheet match to stats[(person_id, league, season)]."""
    info = data["info"]
    if info.get("match_type") not in (None, "T20", "IT20") and league != "hnd":
        return
    people = info.get("registry", {}).get("people", {})
    year = season_year(info)
    seen = set()
    for inn in data.get("innings", []):
        if inn.get("super_over"):
            continue
        order = []
        for over in inn.get("overs", []):
            ph = phase_of(int(over["over"]))
            for d in over.get("deliveries", []):
                ex = d.get("extras", {})
                bat = people.get(d["batter"], d["batter"])
                bwl = people.get(d["bowler"], d["bowler"])
                for who in (d["batter"], d.get("non_striker")):
                    if who and who not in order:
                        order.append(who)
                sb = stats[(bat, league, year)]
                seen.add((bat, league, year))
                r = d["runs"]["batter"]
                if "wides" not in ex:
                    sb.bballs += 1
                    sb.bruns += r
                    if ph == "death":
                        sb.death_balls += 1
                        sb.death_runs += r
                sw = stats[(bwl, league, year)]
                seen.add((bwl, league, year))
                conceded = r + ex.get("wides", 0) + ex.get("noballs", 0)
                sw.wruns += conceded
                sw.phase_runs[ph] += conceded
                if "wides" not in ex and "noballs" not in ex:
                    sw.wballs += 1
                    sw.phase_balls[ph] += 1
                for w in d.get("wickets", []):
                    out = people.get(w["player_out"], w["player_out"])
                    stats[(out, league, year)].outs += 1
                    if w.get("kind") not in ("run out", "retired hurt", "retired out", "obstructing the field"):
                        sw.wkts += 1
        for pos, who in enumerate(order):
            stats[(people.get(who, who), league, year)].positions.append(pos + 1)
    for key in seen:
        stats[key].matches += 1


def load_cricsheet(cache):
    stats = defaultdict(Sample)
    found = []
    for league in LEAGUES:
        z = cache / f"{league}_json.zip"
        if not z.exists():
            continue
        found.append(league)
        with zipfile.ZipFile(z) as f:
            for name in f.namelist():
                if name.endswith(".json"):
                    read_match(json.loads(f.read(name)), league, stats)
    return stats, found


def load_names(cache):
    """Full name -> Cricsheet person id: our overrides first, then Cricsheet's own alternative names."""
    ids = {}
    over = HERE / "cricsheet_ids.csv"
    if over.exists():
        with open(over, encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
        for r in rows:
            ids[r["Name"]] = r["Identifier"]
    alt = cache / "names.csv"
    if alt.exists():
        with open(alt, encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
        for r in rows:
            ids.setdefault(r["name"], r["identifier"])
    people = cache / "people.csv"
    if people.exists():
        with open(people, encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
        for r in rows:
            ids.setdefault(r["name"], r["identifier"])
    return ids


def recency_weight(year, latest):
    return 1.0 if year >= latest else 0.6 if year == latest - 1 else 0.3


def cricsheet_player(p, pid, stats, latest_ipl, captains):
    """Ratings, tags and the Last*/T20* columns from the samples of one person."""
    role = p["Role"]
    rows = [(lg, yr, s) for (who, lg, yr), s in stats.items() if who == pid]
    if not rows:
        return None
    br = bb = bo = wr = wb = wk = 0.0
    dr = db = 0.0
    phase = defaultdict(float)
    phase_b = defaultdict(float)
    positions = []
    for lg, yr, s in rows:
        w = LEAGUES[lg] * recency_weight(yr, latest_ipl)
        br += w * s.bruns; bb += w * s.bballs; bo += w * s.outs
        wr += w * s.wruns; wb += w * s.wballs; wk += w * s.wkts
        dr += w * s.death_runs; db += w * s.death_balls
        for ph in PAR:
            phase[ph] += w * s.phase_runs[ph]
            phase_b[ph] += w * s.phase_balls[ph]
        positions += s.positions
    bat = shrink(bat_calc(br, bb, bo), ROLE_BAT_PRIOR.get(role, 40), bb, 350)
    # Economy against the par of the phases actually bowled: runs above par, over the balls bowled.
    above = sum(phase[ph] - phase_b[ph] * PAR[ph] / 6 for ph in PAR)
    adj_runs = wb * PAR_ALL / 6 + above
    bowl = shrink(bowl_calc(adj_runs, wb, wk), ROLE_BOWL_PRIOR.get(role, 20), wb, 500)

    tags = set()
    if positions and role in ("BAT", "WK", "AR"):
        top = sum(1 for x in positions if x <= 2) / len(positions)
        mid = sum(1 for x in positions if 3 <= x <= 4) / len(positions)
        low = sum(1 for x in positions if x >= 5) / len(positions)
        sr = 100 * br / bb if bb else 0
        dsr = 100 * dr / db if db else 0
        if top >= 0.5:
            tags.add("Opener")
        if mid >= 0.5 and sr < 140:
            tags.add("Anchor")
        if low >= 0.5 and (dsr >= 165 or sr >= 150):
            tags.add("Finisher")
    if role == "WK" and bat >= 68:
        tags.add("KeeperBat")
    style = p["BowlStyle"]
    if role in ("PACE", "AR") and is_pace(style) and wb >= 60:
        if style.lower().startswith("left-arm"):
            tags.add("LeftArmPace")
        if phase_b["pp"] / wb >= 0.4 and 6 * phase["pp"] / max(phase_b["pp"], 1) <= PAR["pp"] + 0.3:
            tags.add("PowerplayPace")
        if phase_b["death"] / wb >= 0.3 and 6 * phase["death"] / max(phase_b["death"], 1) <= PAR["death"]:
            tags.add("DeathPace")
    if role in ("SPIN", "AR"):
        k = spin_kind(style)
        if k:
            tags.add(k)
    if p["Name"] in MYSTERY:
        tags.add("Mystery")
    if p["Name"] in CAPTAINS or p["Name"] in captains:
        tags.add("Captain")

    last = [s for lg, yr, s in rows if lg == "ipl" and yr == latest_ipl]
    last_cols = None
    if last:
        s = last[0]
        last_cols = {"LastMatches": s.matches, "LastRuns": s.bruns,
                     "LastSR": f"{100 * s.bruns / s.bballs:.2f}" if s.bballs else "",
                     "LastWkts": s.wkts, "LastEcon": f"{6 * s.wruns / s.wballs:.2f}" if s.wballs else ""}
    t20 = [s for lg, yr, s in rows if lg != "ipl"]
    t20_cols = None
    if t20:
        m = sum(s.matches for s in t20); r = sum(s.bruns for s in t20); b = sum(s.bballs for s in t20)
        w = sum(s.wkts for s in t20); rr = sum(s.wruns for s in t20); bw = sum(s.wballs for s in t20)
        t20_cols = {"T20Matches": m, "T20Runs": r, "T20SR": f"{100 * r / b:.2f}" if b else "", "T20Wkts": w,
                    "T20Econ": f"{6 * rr / bw:.2f}" if bw else ""}
    return clamp_rating(bat), clamp_rating(bowl), tags, last_cols, t20_cols


# ---- Main --------------------------------------------------------------------------------------------------------

NEW_COLUMNS = ["Tags", "LastMatches", "LastRuns", "LastSR", "LastWkts", "LastEcon", "BatRatingV1", "BowlRatingV1"]


def run(csv_path, cache, dry=False, out=sys.stdout):
    with open(csv_path, encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    header = list(rows[0].keys())
    for c in NEW_COLUMNS:
        if c not in header:
            header.append(c)
    captains = set()
    teams = HERE / "Teams.csv"
    if teams.exists():
        with open(teams, encoding="utf-8-sig") as f:
            captains = {r["Captain2026"] for r in csv.DictReader(f)}

    stats, leagues = (load_cricsheet(cache) if cache.exists() else (None, []))
    ids = load_names(cache) if leagues else {}
    latest = max((yr for (_, lg, yr) in stats if lg == "ipl"), default=0) if stats else 0
    print(f"ratings v2: {'cricsheet ' + ', '.join(leagues) if leagues else 'fallback (no Cricsheet cache at ' + str(cache) + ')'}", file=out)

    matched = 0
    for p in rows:
        for c in NEW_COLUMNS:
            p.setdefault(c, "")
            if p[c] is None:
                p[c] = ""
        if not p["BatRatingV1"]:
            p["BatRatingV1"], p["BowlRatingV1"] = p["BatRating"], p["BowlRating"]
        res = None
        if leagues and p["Name"] in ids:
            res = cricsheet_player(p, ids[p["Name"]], stats, latest, captains)
            matched += res is not None
        if res is None:
            res = fallback_player(p, captains)
        bat, bowl, tags, last, t20 = res
        p["BatRating"], p["BowlRating"] = str(bat), str(bowl)
        p["Tags"] = "|".join(t for t in TAGS if t in tags)
        for cols in (last, t20):
            if cols:
                for k, v in cols.items():
                    p[k] = str(v)
    if leagues:
        print(f"  {matched}/{len(rows)} players matched to Cricsheet; the rest use the fallback", file=out)
    if not dry:
        buf = io.StringIO()
        w = csv.DictWriter(buf, fieldnames=header, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
        Path(csv_path).write_text(buf.getvalue(), encoding="utf-8")
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", default=str(HERE / "cache"))
    ap.add_argument("--csv", default=str(HERE / "Players.csv"))
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    rows = run(a.csv, Path(a.cache), a.dry_run)
    top = sorted(rows, key=lambda r: -max(int(r["BatRating"]), int(r["BowlRating"])))[:12]
    for r in top:
        print(f"  {r['Name']:<24} {r['Role']:<4} bat {r['BatRating']:>2} bowl {r['BowlRating']:>2}  {r['Tags']}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Voice the commentary, player calls and field sounds with ElevenLabs.

Writes raw 22.05 kHz mono s16le clips under Content/Audio, which the game plays by path:
  Commentary/<key>.pcm  every filled line and situation call the CommentaryDirector can say
  Vocal/<Name>.pcm      the players' shouts (CricketAudioDirector::EVocal)
  Sfx/<Cue>.pcm         recorded cues that replace the synthesized ones (CricketAudio::ECue)

The lines, squads, regions, verbs and pronunciations are read from the C++ sources, so this
never drifts from what the game can say. Existing clips are skipped: re-running only fills gaps.

The API key comes from ELEVENLABS_API_KEY or ~/.config/elevenlabs/key (never the repo).

Usage:
  eleven.py --plan                 clip and character counts per tier, no API calls
  eleven.py --voices               list the account's voices
  eleven.py --budget 2000          voice commentary, highest priority first, up to 2000 characters
  eleven.py --vocals --sfx         player calls and recorded cues
Listen to a clip: ffplay -autoexit -f s16le -ar 22050 -ch_layout mono Content/Audio/Commentary/<key>.pcm
"""
import argparse, array, json, os, re, subprocess, sys, time, urllib.error, urllib.request, zlib
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'Source', 'CRICKET26', 'Cricket')
OUT = os.path.join(ROOT, 'Content', 'Audio')
API = 'https://api.elevenlabs.io/v1'
RATE = 22050

# Premade ElevenLabs voices (original identities, no impersonation). Override with --pbp/--analyst.
PBP_VOICE = 'onwK4e9ZLuTAKqWW03F9'      # Daniel: British broadcaster
ANALYST_VOICE = 'JBFqnCBsd6RMkjVDRZzb'  # George: warm, measured
VOCAL_VOICES = ['IKne3meq5aSn9XLyUdCD', 'N2lVS1w4EtoT3dr4eOWO']  # Charlie, Callum: players on the field


def clip_key(text):
    """Mirror of CricketCommentary::ClipKey."""
    return '_'.join(re.findall(r'[a-z0-9]+', text.lower().replace("'", '')))


def read(name):
    with open(os.path.join(SRC, name)) as f:
        return f.read()


def parse_sources():
    comm = read('CricketCommentary.cpp')
    lines = [dict(text=t, id=i, analyst=sp == 'Analyst', excite=float(e), tags=tags.split(','))
             for t, i, sp, e, tags in re.findall(
                 r'Make\(TEXT\("((?:[^"\\]|\\.)*)"\), TEXT\("(\w+)"\), P::(\w+), ([\d.]+)f, \d+, \d+, TEXT\("([^"]*)"\)', comm)]
    lexicon = re.findall(r'\{ TEXT\("([^"]+)"\), TEXT\("([^"]+)"\), TEXT\("[^"]+"\) \}', comm)
    region_fn = comm[comm.index('FString Region('):]
    regions = re.findall(r'TEXT\("([a-z ]+|[a-z]+-[a-z]+)"\)', region_fn[:region_fn.index('\n}')])
    director = read('CricketCommentaryDirector.cpp')
    verbs = sorted(set(re.findall(r'Verb = TEXT\("([a-z-]+)"\)', director)))
    game = read('SuperOverGameMode.cpp')
    squads = game[game.index('ASuperOverGameMode::DefaultSquads()'):]
    squads = squads[:squads.index('\n}')]
    teams = {}
    for block in re.split(r'FCricketTeam \w+;', squads)[1:]:
        name = re.search(r'\.Name = TEXT\("([^"]+)"\)', block).group(1)
        batters = re.findall(r'MakePlayer\(TEXT\("([^"]+)"\)', block.split('.Bowler')[0])
        teams[name] = batters
    return lines, dict(lexicon), regions, verbs, teams


def bodies(line, regions, verbs, teams):
    """Every filled text the director can produce for a line (FillLine, verb, multi runs, capital)."""
    t = line['text']
    fills = []
    for team, batters in teams.items():
        strikers = batters if '{S}' in t or '{NS}' in t else [None]
        for s in strikers:
            for ns in ([b for b in batters if b != s] if '{NS}' in t else [None]):
                fills.append((team, s, ns))
    if '{B}' not in t:  # the team only matters through {B}
        fills = list(dict.fromkeys((None, s, ns) for _, s, ns in fills))
    out = []
    for team, s, ns in fills:
        for r in (regions if '{R}' in t else [None]):
            for v in (verbs if '%s' in t else [None]):
                for runs in (['two', 'three'] if 'multi' in line['tags'] else [None]):
                    x = t
                    if team: x = x.replace('{B}', team)
                    if s: x = x.replace('{S}', s)
                    if ns: x = x.replace('{NS}', ns)
                    if r: x = x.replace('{R}', r)
                    if v: x = x.replace('%s', v)
                    if runs: x += runs + '.'
                    out.append(x[0].upper() + x[1:])
    return list(dict.fromkeys(out))


def suffixes():
    """The director's situation calls, voiced as separate clips (CricketCommentaryDirector SelectLine)."""
    out = [('Scores level! Another Super Over.', 0.9)]
    for n in range(1, 41):
        tense = 0.9 if n <= 6 else 0.6 if n <= 12 else 0.4
        out.append((f'{n} needed off the last ball.', max(tense, 0.7)))
        out += [(f'{n} needed from {m}.', tense * (0.8 if m > 3 else 1.0)) for m in range(2, 7)]
        out.append((f'Target {n}.', 0.3))
    return out


def emotion(excite, analyst):
    # eleven_v3 audio tags; they steer delivery and are not spoken. Routine balls stay untagged (and cheaper).
    if analyst: return '[thoughtful] '
    if excite >= 0.9: return '[shouting] '
    if excite >= 0.7: return '[excited] '
    if excite >= 0.45: return '[energetic] '
    return ''


def spoken(text, lexicon):
    for display, say in lexicon.items():
        if display != say:
            text = re.sub(r'\b' + re.escape(display) + r'\b', say, text)
    return text


def tier(line, text):
    t = line['text'] if line else ''
    if not line: return 3 if re.match(r'(\d+) ', text) and int(text.split()[0]) > 12 else 1
    if '%s' in t: return 5
    if '{R}' in t and ('{S}' in t or '{NS}' in t): return 4
    if '{R}' in t: return 2
    return 0


def plan(lines, lexicon, regions, verbs, teams):
    """(tier, -excitement, key, voiced text, voice kind) for every commentary clip, best first."""
    jobs = {}
    for line in lines:
        for body in bodies(line, regions, verbs, teams):
            text = emotion(line['excite'], line['analyst']) + spoken(body, lexicon)
            jobs.setdefault(clip_key(body), (tier(line, body), -line['excite'], clip_key(body), text, 'analyst' if line['analyst'] else 'pbp'))
    for s, e in suffixes():
        jobs.setdefault(clip_key(s), (tier(None, s), -e, clip_key(s), emotion(e, False) + s, 'pbp'))
    return sorted(jobs.values())


def api_key():
    key = os.environ.get('ELEVENLABS_API_KEY')
    path = os.path.expanduser('~/.config/elevenlabs/key')
    if not key and os.path.exists(path):
        with open(path) as f: key = f.read().strip()
    if not key: sys.exit('No API key: put it in ~/.config/elevenlabs/key or ELEVENLABS_API_KEY.')
    return key


def call(path, key, body=None, fmt=None):
    url = API + path + (f'?output_format={fmt}' if fmt else '')
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, headers={'xi-api-key': key, 'Content-Type': 'application/json'})
    for attempt in range(6):
        try:
            with urllib.request.urlopen(req, timeout=120) as r: return r.read()
        except urllib.error.HTTPError as e:
            msg = e.read().decode(errors='replace')
            if e.code == 429 or e.code >= 500:  # rate or concurrency limit: back off and retry
                time.sleep(2 ** attempt)
                continue
            raise RuntimeError(f'{e.code} {msg[:300]}')
    raise RuntimeError('gave up after retries')


def finish(pcm, peak, trim=True):
    """Trim leading/trailing silence (keeping a short tail) and normalise the peak."""
    a = array.array('h', pcm[:len(pcm) // 2 * 2])
    if not a: return b''
    if trim:
        loud = [i for i, v in enumerate(a) if abs(v) > 400]
        if loud: a = a[max(0, loud[0] - RATE // 100):min(len(a), loud[-1] + RATE // 12)]
    top = max(1, max(abs(v) for v in a))
    g = peak * 32767 / top
    return array.array('h', (max(-32768, min(32767, int(v * g))) for v in a)).tobytes()


def write(rel, pcm):
    path = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + '.tmp', 'wb') as f: f.write(pcm)
    os.replace(path + '.tmp', path)


def tts(key, voice, text, stability, seed_from):
    body = dict(text=text, model_id='eleven_v3', seed=zlib.crc32(seed_from.encode()) % 2**31,
                voice_settings=dict(stability=stability, similarity_boost=0.8, style=0.0, use_speaker_boost=True))
    return call(f'/text-to-speech/{voice}', key, body, f'pcm_{RATE}')


VOCALS = {
    'Howzat': '[shouting] Howzaaat!',
    'Run': '[shouting] Yes! Run, run!',
    'No': '[shouting] No! Stay there!',
    'Wait': '[shouting] Wait!',
    'CatchCall': '[shouting] Mine! Mine!',
    'Celebrate': '[shouting] [excited] Yesss! Come on!',
    'Frustrated': '[frustrated] Ahh, no!',
}

SFX = {  # cue: (prompt, seconds, loop)
    'BatMiddle': ('Perfectly timed cricket shot: one crisp, ringing crack of a willow bat on a leather ball, close up, no crowd, no music', 0.6, False),
    'BatCrack': ('One solid crack of a willow cricket bat hitting a leather ball, close up, no crowd, no music', 0.6, False),
    'BatToe': ('Mistimed cricket shot off the toe of the bat: one dull wooden knock, no ring, no crowd', 0.5, False),
    'EdgeTick': ('A faint thin edge of a cricket ball off the bat: one tiny wooden click, no crowd', 0.5, False),
    'Bounce': ('A hard leather cricket ball pitching once on a dry, hard pitch: one short thud, no crowd', 0.5, False),
    'Stumps': ('A cricket ball smashing into wooden stumps, bails clattering away, one impact, no crowd', 1.0, False),
    'PadThud': ('A cricket ball thudding into a batting leg pad: one soft padded thump, no crowd', 0.5, False),
    'KeeperGlove': ('A cricket ball caught in a wicketkeeper\'s leather gloves: one short leather snap, no crowd', 0.5, False),
    'CatchPop': ('A cricket ball caught cleanly in two bare hands in the outfield: one dull slap, no crowd', 0.5, False),
    'ThrowRelease': ('A cricket fielder releasing a hard flat throw: a subtle short cloth rustle and release, no whoosh, no voice', 0.5, False),
    'Footstep': ('One single footstep of a spiked cricket shoe on firm grass, close, quiet', 0.5, False),
    'Crowd': ('Large cricket stadium crowd ambience: steady murmur, distant chatter, occasional light claps, no music, no announcer', 30.0, True),
}


def ffmpeg_pcm(mp3):
    return subprocess.run(['ffmpeg', '-v', 'error', '-i', 'pipe:0', '-f', 's16le', '-ac', '1', '-ar', str(RATE), 'pipe:1'],
                          input=mp3, capture_output=True, check=True).stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--plan', action='store_true')
    ap.add_argument('--voices', action='store_true')
    ap.add_argument('--budget', type=int, default=0, help='characters of commentary to voice this run')
    ap.add_argument('--vocals', action='store_true')
    ap.add_argument('--sfx', action='store_true')
    ap.add_argument('--pbp', default=PBP_VOICE)
    ap.add_argument('--analyst', default=ANALYST_VOICE)
    ap.add_argument('--stability', type=float, default=0.5, help='eleven_v3: 0 creative, 0.5 natural, 1 robust')
    ap.add_argument('--jobs', type=int, default=2, help='parallel requests (plan concurrency limit)')
    a = ap.parse_args()

    lines, lexicon, regions, verbs, teams = parse_sources()
    jobs = plan(lines, lexicon, regions, verbs, teams)
    have = lambda k: os.path.exists(os.path.join(OUT, 'Commentary', k + '.pcm'))
    if a.plan or not (a.voices or a.budget or a.vocals or a.sfx):
        print(f'{len(lines)} lines, {len(regions)} regions, {len(verbs)} verbs, teams {teams}')
        total = 0
        for t in range(6):
            js = [j for j in jobs if j[0] == t]
            chars = sum(len(j[3]) for j in js)
            total += chars
            print(f'tier {t}: {len(js):5d} clips {chars:7d} chars (cumulative {total}), {sum(have(j[2]) for j in js)} voiced')
        return

    key = api_key()
    if a.voices:
        for v in json.loads(call('/voices', key))['voices']:
            print(v['voice_id'], v['name'], v.get('category', ''), json.dumps(v.get('labels', {})))

    def run(fn, items):
        with ThreadPoolExecutor(a.jobs) as pool:
            for name, err in pool.map(fn, items):
                print(('FAIL ' + err + ' ' if err else 'ok   ') + name, flush=True)

    if a.budget:
        todo, spent = [], 0
        for j in jobs:
            if have(j[2]): continue
            if spent + len(j[3]) > a.budget: break
            todo.append(j)
            spent += len(j[3])
        print(f'voicing {len(todo)} clips, {spent} characters')

        def voice(j):
            try:
                write(f'Commentary/{j[2]}.pcm', finish(tts(key, a.analyst if j[4] == 'analyst' else a.pbp, j[3], a.stability, j[2]), 0.89))
                return j[2], None
            except Exception as e:
                return j[2], str(e)
        run(voice, todo)

    if a.vocals:
        def vocal(item):
            name, text = item
            try:
                voice = VOCAL_VOICES[zlib.crc32(name.encode()) % len(VOCAL_VOICES)]
                write(f'Vocal/{name}.pcm', finish(tts(key, voice, text, 0.0, name), 0.7))
                return name, None
            except Exception as e:
                return name, str(e)
        run(vocal, [(n, t) for n, t in VOCALS.items() if not os.path.exists(os.path.join(OUT, 'Vocal', n + '.pcm'))])

    if a.sfx:
        def sfx(item):
            name, (prompt, secs, loop) = item
            try:
                mp3 = call('/sound-generation', key, dict(text=prompt, duration_seconds=secs, prompt_influence=0.5,
                                                          loop=loop, model_id='eleven_text_to_sound_v2'), 'mp3_44100_128')
                write(f'Sfx/{name}.pcm', finish(ffmpeg_pcm(mp3), 0.5 if loop else 0.7, trim=not loop))
                return name, None
            except Exception as e:
                return name, str(e)
        run(sfx, [i for i in SFX.items() if not os.path.exists(os.path.join(OUT, 'Sfx', i[0] + '.pcm'))])


if __name__ == '__main__':
    main()

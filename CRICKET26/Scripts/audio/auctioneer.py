#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10,<3.13"
# dependencies = ["kokoro-onnx", "soundfile"]
# ///
"""The auctioneer's voice, recorded locally with Kokoro (open weights, Apache 2.0, no account or credits).

The game stitches every line from pieces (AuctionCalls::Voice): the fixed words of each phrasing, and each name and
price said into it. The pieces come from the game itself: the CRICKET26.Auction.Voice test writes every one to
Saved/AuctionScript.txt, so run it first (Scripts/run_tests.sh CRICKET26.Auction.Voice). Each piece becomes a raw
22.05 kHz mono s16le clip at Content/Audio/Auction/<clip key>.pcm. Existing clips are skipped.

The model lives outside the repo, in ~/.cache/kokoro (kokoro-v1.0.onnx and voices-v1.0.bin from the kokoro-onnx
releases on GitHub).

Usage:
  auctioneer.py                 record every missing clip
  auctioneer.py --only "Sold"   record just the pieces containing this text (for auditioning a voice)
  auctioneer.py --voice bf_emma --speed 1.05
"""
import argparse, os, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eleven import OUT, RATE, ROOT, clip_key, finish, write  # noqa: E402

import soundfile  # noqa: E402
from kokoro_onnx import Kokoro  # noqa: E402

MODEL = os.path.expanduser('~/.cache/kokoro')
SCRIPT = os.path.join(ROOT, 'Saved', 'AuctionScript.txt')

# What Kokoro's phonemiser gets wrong, spelled the way it should sound.
SAY_AS = {'IPL': 'I P L', 'RTM': 'R T M'}
# Name clips are separate from captions. These respellings keep the on-screen names unchanged.
NAMES = {
    'Ruturaj Gaikwad': 'Roo-too-raaj Guy-kwad',
    'Vaibhav Sooryavanshi': 'Vay-bhav Soor-ya-vun-shee',
    'Yuzvendra Chahal': 'Yooz-vendra Chuh-hal',
    'Virat Kohli': 'Vee-raat Koh-lee',
    'Jasprit Bumrah': 'Jus-preet Boom-rah',
    'Ravindra Jadeja': 'Ruh-vin-dra Juh-day-juh',
    'Shreyas Iyer': 'Shray-us Eye-yer',
    'Venkatesh Iyer': 'Ven-kuh-taysh Eye-yer',
    'Yashasvi Jaiswal': 'Yush-us-vee Jays-wal',
    'Shubman Gill': 'Shoob-mun Gill',
    'Suryakumar Yadav': 'Soor-ya-koo-mar Yaa-duv',
    'Bhuvneshwar Kumar': 'Bhoo-vuh-naysh-war Koo-mar',
    'Sai Sudharsan': 'Sai Soo-dar-sun',
    'Mohammed Siraj': 'Muh-hum-mud See-raaj',
    'Kagiso Rabada': 'Kuh-ghee-so Ruh-baa-duh',
    'Heinrich Klaasen': 'Hine-rik Klaa-sun',
    'Rachin Ravindra': 'Ruh-chin Ruh-vin-dra',
    'Ajinkya Rahane': 'Uh-jink-yuh Ruh-haa-nay',
    'Ishan Kishan': 'Ee-shaan Kee-shaan',
    'Kuldeep Yadav': 'Kool-deep Yaa-duv',
    'Rinku Singh': 'Rin-koo Singh',
    'Tilak Varma': 'Tee-luk Var-ma',
}


def spoken(piece):
    """The piece as the voice reads it: leading punctuation dropped, the ending kept for its intonation."""
    text = piece.strip().lstrip(',.:;!? ')
    if text in NAMES:
        return NAMES[text]
    for word, say in SAY_AS.items():
        text = ' '.join(say if w.strip(',.?!') == word else w for w in text.split(' '))
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--voice', default='bf_emma')
    ap.add_argument('--speed', type=float, default=1.05)
    ap.add_argument('--only', default='')
    ap.add_argument('--refresh-names', action='store_true', help='replace recorded clips for names in NAMES')
    args = ap.parse_args()
    if not os.path.exists(SCRIPT):
        sys.exit(f'No {SCRIPT}: run Scripts/run_tests.sh CRICKET26.Auction.Voice first.')
    with open(SCRIPT, encoding='utf-8') as f:
        pieces = [p.rstrip('\n') for p in f if p.strip()]
    todo = [p for p in pieces if args.only in p and (args.refresh_names and p in NAMES or
            not os.path.exists(os.path.join(OUT, 'Auction', clip_key(p) + '.pcm')))]
    print(f'{len(pieces)} pieces, {len(todo)} to record with {args.voice}')
    tts = Kokoro(os.path.join(MODEL, 'kokoro-v1.0.onnx'), os.path.join(MODEL, 'voices-v1.0.bin'))
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, 'clip.wav')
        for n, piece in enumerate(todo, 1):
            samples, rate = tts.create(spoken(piece), voice=args.voice, speed=args.speed, lang='en-gb')
            soundfile.write(wav, samples, rate)
            pcm = subprocess.run(['ffmpeg', '-v', 'error', '-i', wav, '-f', 's16le', '-ac', '1', '-ar', str(RATE), 'pipe:1'],
                                 capture_output=True, check=True).stdout
            write(os.path.join('Auction', clip_key(piece) + '.pcm'), finish(pcm, 0.7))
            if n % 100 == 0 or n == len(todo):
                print(f'{n}/{len(todo)}')


if __name__ == '__main__':
    main()

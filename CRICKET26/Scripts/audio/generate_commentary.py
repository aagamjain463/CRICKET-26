#!/usr/bin/env python3
import os, sys, re, subprocess, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'Scripts', 'audio'))
from eleven import parse_sources, bodies, suffixes, clip_key

OUT = os.path.join(ROOT, 'Content', 'Audio', 'Commentary')
os.makedirs(OUT, exist_ok=True)

lines, lexicon, regions, verbs, teams = parse_sources()

# We want high coverage:
# 1. All suffixes (situation calls)
# 2. All lines without player/region placeholders
# 3. For lines with placeholders, generate for all default squad batters and main regions
targets = {}

for s, _ in suffixes():
    k = clip_key(s)
    targets[k] = s

main_regions = ['cover', 'midwicket', 'long on', 'long off', 'point', 'square leg', 'fine leg', 'third man', 'deep square leg']
main_verbs = ['driven', 'pulled', 'cut', 'lofted', 'pushed', 'flicked']

for l in lines:
    b_list = bodies(l, main_regions, main_verbs, teams)
    for b in b_list:
        k = clip_key(b)
        targets[k] = b

print(f"Total target clips to ensure: {len(targets)}")

# Filter to missing clips
missing = {k: v for k, v in targets.items() if not os.path.exists(os.path.join(OUT, f"{k}.pcm"))}
print(f"Missing clips to generate: {len(missing)}")

def generate_one(pair):
    k, phrase = pair
    pcm_path = os.path.join(OUT, f"{k}.pcm")
    if os.path.exists(pcm_path):
        return
    aiff_path = f"/tmp/comm_{k}_{os.getpid()}.aiff"
    try:
        # Generate with Mac say
        subprocess.run(['say', '-v', 'Daniel', '-r', '185', phrase, '-o', aiff_path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        # Convert to 22050 mono raw s16le PCM
        subprocess.run(['ffmpeg', '-y', '-i', aiff_path, '-f', 's16le', '-ar', '22050', '-ac', '1', pcm_path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    except Exception as e:
        print(f"Error generating {k}: {e}", file=sys.stderr)
    finally:
        if os.path.exists(aiff_path):
            try: os.remove(aiff_path)
            except: pass

if missing:
    t0 = time.time()
    workers = min(12, os.cpu_count() or 4)
    print(f"Generating using {workers} workers...")
    with ThreadPoolExecutor(max_workers=workers) as ex:
        # Process in batches to show progress
        items = list(missing.items())
        total = len(items)
        done = 0
        batch_size = 50
        for i in range(0, total, batch_size):
            batch = items[i:i+batch_size]
            list(ex.map(generate_one, batch))
            done += len(batch)
            elapsed = time.time() - t0
            rate = done / max(elapsed, 0.001)
            print(f"Progress: {done}/{total} ({rate:.1f} clips/s)")
    t1 = time.time()
    print(f"Done in {t1 - t0:.1f}s")
else:
    print("All target clips already exist!")

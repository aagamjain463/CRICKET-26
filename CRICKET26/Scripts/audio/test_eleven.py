"""Regression checks for eleven.py: clip keys match the game, and every line the director can say gets a clip.
Usage: python3 Scripts/audio/test_eleven.py (prints TESTS PASS, stops at the first failing assert)"""
import array, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eleven as E  # noqa: E402

# Same cases as CRICKET26.Commentary.Voicing, so the Python and C++ keys agree.
assert E.clip_key("It's all over! Home XI win.") == 'its_all_over_home_xi_win'
assert E.clip_key('Slog-swept past point, 7 needed from 3.') == 'slog_swept_past_point_7_needed_from_3'

lines, lexicon, regions, verbs, teams = E.parse_sources()
assert len(lines) >= 100 and len(regions) == 9 and len(verbs) == 12, (len(lines), regions, verbs)
assert teams['Home XI'] == ['Opener', 'Finisher', 'Allrounder'], teams
assert lexicon['Home XI'] == 'Home Eleven' and lexicon['LBW'] == 'leg before'

jobs = {j[2]: j for j in E.plan(lines, lexicon, regions, verbs, teams)}
# Director outputs, as the game fills them: multi runs finished, defence won by the fielding side, suffixes split.
for said in ['Driven to cover, and they come back for two.', 'Defended! Away XI hold their nerve and take the Super Over.',
             "Straight to the fielder at long-off. No run.", '7 needed from 3.', '1 needed off the last ball.', 'Target 13.']:
    assert E.clip_key(said) in jobs, said
# Voiced with the spoken form, keyed by the display form; analyst lines get the analyst voice.
j = jobs[E.clip_key('Struck on the pad, and the finger goes up. Opener is LBW.')]
assert 'leg before' in j[3] and 'LBW' not in j[3], j
assert all(j[4] == 'analyst' for j in jobs.values() if j[2].startswith('the_plan_is_clear'))
# Every line is voiceable at least once.
for line in lines:
    assert E.bodies(line, regions, verbs, teams), line['id']

# Trimming drops silence but keeps the call; the peak is normalised.
pcm = array.array('h', [0] * 5000 + [1000, -2000, 1500] * 100 + [0] * 5000).tobytes()
out = array.array('h', E.finish(pcm, 0.5))
assert 300 <= len(out) < 5000 and max(abs(v) for v in out) == int(0.5 * 32767), (len(out), max(out))
print('TESTS PASS')

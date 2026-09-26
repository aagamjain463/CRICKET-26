# Regression checks for author_bowl.py: the monotone key interpolation, the key tables' mirror, then every bowling
# action baked and validated on the players' skeleton, right and left arm.
# Usage: Blender -b -P test_author_bowl.py -- [rig Body.fbx]   (prints TESTS PASS, or stops at the failing assert)
import bpy, sys, os
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import author_bowl as AB  # noqa: E402
import bowls  # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
RIG = argv[0] if argv else os.path.expanduser('~/Downloads/mocap/rig/Body.fbx')

# pchip: through every key, never past a neighbouring key (a planted foot's held keys hold it dead still), flat
# beyond the ends.
ts, vs = np.array([0.0, 1.0, 2.0, 3.0]), np.array([0.0, 0.0, 1.0, 1.0])
t = np.linspace(-0.5, 3.5, 81)
v = AB.pchip(ts, vs, t)
assert np.allclose(AB.pchip(ts, vs, ts), vs)
assert v.min() >= 0.0 and v.max() <= 1.0 and np.all(np.diff(v) >= -1e-12)
assert np.all(v[t <= 1.0] == 0.0) and np.all(v[t >= 2.0] == 1.0)
assert np.allclose(AB.pchip(np.array([0.0]), np.array([4.0]), t), 4.0)

# mirror: the left-armer's sides swap and every lateral value flips; the rest is untouched.
m = bowls.mirror({'armR_a': [(0, 90)], 'footL_r': [(0, 0.1)], 'handR_palm_f': [(0, 0.5)], 'ch_yaw': [(0, 30)],
                  'hip_side': [(0, 5)], 'hip_f': [(0, 1)], 'look_r': [(0, 0.5)]})
assert m == {'armL_a': [(0, 90)], 'footR_r': [(0, -0.1)], 'handL_palm_f': [(0, 0.5)], 'ch_yaw': [(0, -30)],
             'hip_side': [(0, -5)], 'hip_f': [(0, 1)], 'look_r': [(0, -0.5)]}

# Every action, both arms: valid, released from high over the front foot, the pelvis travelling forward the whole
# way (the game reads the run back from it) and the head held on the batter.
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'AnimQA', 'bowl_test')
rel = AB.Bowl.frame_of(0.0) - 1
for kind in bowls.KEYS:
    for hand in 'RL':
        st, fails = AB.author(kind, hand, RIG, out)
        assert not fails, (kind, hand, fails)
        s = hand.lower()
        pel = [w['pelvis'].to_translation() for w in st.snap]
        assert all(b.y < a.y for a, b in zip(pel, pel[1:])), (kind, hand, 'the pelvis must run forward (-Y) every frame')
        ball = st.snap[rel]['hand_' + s].to_translation()
        assert ball.z > 1.9 and abs(ball.x) < 0.45, (kind, hand, ball)
        # The arm comes over near vertical: the hand never crosses far to the other arm's side (a right-armer's is -X).
        assert ball.x * (-1 if hand == 'R' else 1) > -0.15, (kind, hand, ball)
print('TESTS PASS')

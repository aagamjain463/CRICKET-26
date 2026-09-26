# Regression checks for author_stroke.py: the planner's search, its legality report, the smoothing and the arm
# measures, then the golden straight drive planned, baked and validated end to end.
# Usage: Blender -b -P test_author_stroke.py -- [rig Body.fbx]   (prints TESTS PASS, or stops at the failing assert)
import bpy, sys, os, math
import numpy as np
from mathutils import Vector, Quaternion

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import author_stroke as A  # noqa: E402
import strokes, validate_stroke  # noqa: E402

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
RIG = argv[0] if argv else os.path.expanduser('~/Downloads/mocap/rig/Body.fbx')

# viterbi: the cheapest path over a 2-axis grid, each axis's move priced on its own.
costs = [np.array([[0, 9], [9, 9]]), np.array([[9, 9], [9, 0]]), np.array([[9, 9], [9, 0]])]
move = np.array([[0, 1], [1, 0]])
assert A.viterbi(costs, (move, move)) == [(0, 0), (1, 1), (1, 1)]
# A move priced out of reach is never taken while another path exists.
far = np.array([[0, 1e5], [1e5, 0]])
assert A.viterbi([np.array([0., 5.]), np.array([5., 0.])], (far,)) == [(0,), (0,)]

# legal_motion: a state is kept only if a legal motion runs through it; a jump no step allows is reported.
legal = [np.array([True, True, False]), np.array([False, True, True]), np.array([False, False, True])]
near = lambda k: (np.abs(np.arange(3)[:, None] - np.arange(3)[None, :]) <= 1,)
on, bad = A.legal_motion(legal, near)
assert not bad and on[0].tolist() == [True, True, False] and on[1].tolist() == [False, True, True]
assert A.legal_motion(legal, lambda k: (np.eye(3, dtype=bool),))[1] == [(2, 2, 'too fast a change')]
on, bad = A.legal_motion([np.array([True, False, False]), np.array([False, False, True])], near)
assert bad == [(1, 1, 'too fast a change')], bad
on, bad = A.legal_motion([np.array([True, False]), np.array([False, False]), np.array([True, False])],
                         lambda k: (np.ones((2, 2), bool),))
assert bad == [(1, 1, 'no legal pose')], bad

# smooth: sigma 0 keeps a frame as it is; a constant stays constant, even at the clip's ends.
x = np.array([0., 10, 10, 20, 20])
assert np.allclose(A.smooth(x, np.zeros(5)), x)
assert np.allclose(A.smooth(np.full(5, 3.0), np.full(5, 1.5)), 3.0)
assert np.all(np.diff(A.smooth(x, np.full(5, 1.5))) >= 0)  # a rising path stays rising: no overshoot

# The arm measures on the striker's own skeleton.
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=RIG)
arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
A.STROKE = 'StraightDrive'
st = A.Stroke(arm, strokes.KEYS['StraightDrive'])
st.build_controls()
r = st.rig
qc = st.pose_body(1)
spine = (r.world['spine_01'].to_translation(), r.world['neck_01'].to_translation())
L = st.limb['arml']
sh = r.world['upperarm_l'].to_translation()
up, lat = qc @ A.Z, (qc @ Vector((1, 0, 0)))
lat = lat if (sh - spine[0]).dot(lat) > 0 else -lat
fwd = up.cross(lat) if up.cross(lat).dot(qc @ Vector((0, -1, 0))) > 0 else lat.cross(up)


def measure(u_dir, bend):
    """The left arm's angles with the humerus along u_dir, the elbow bent 90 deg toward bend."""
    elbow = sh + u_dir.normalized() * L['a']
    wrist = elbow + (bend - u_dir.normalized() * bend.dot(u_dir.normalized())).normalized() * L['b']
    el, end, short, s2 = A.two_bone(sh, L['a'], L['b'], wrist, elbow - (sh + wrist) / 2)
    qu, qf = A.limb_rotations(L, sh, el, end, s2)
    return A.arm_anatomy(L, qc, spine, sh, el, end, qu, qf, qf)


# Hanging arm, forearm forward: the neutral shoulder, humeral rotation zero.
assert abs(measure(-up, fwd)['humeral']) < 2.0, measure(-up, fwd)
# Straight up (a high finish): the humeral angle has meaning there and turns smoothly with the forearm, where a swing
# from the hanging arm would spin through 180 deg.
tip = [measure((up + fwd * 0.02 * math.cos(a) + lat * 0.02 * math.sin(a)).normalized(), fwd)['humeral']
       for a in np.radians(np.arange(0, 360, 30))]
assert max(tip) - min(tip) < 20.0, tip
# An elbow raised above the neck is clear of the trunk, whatever its section.
assert measure(up, fwd)['torso'] > 1.0

# The golden drive: planned with no illegal span, and every validator check passes.
st.plan_arms(strokes.CONTACT['StraightDrive'])
assert not st.illegal, st.illegal
st.shortfall = {}
st.bake()
assert validate_stroke.run(st, strokes.CONTACT['StraightDrive']) == 0
print('TESTS PASS')

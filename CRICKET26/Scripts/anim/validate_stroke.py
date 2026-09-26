# Anatomy and contact checks on a baked stroke (author_stroke.py): reads the solved skeleton frame by frame and
# prints one line per check with its worst frame. Any FAIL means the clip is not fit to ship.
import math
from mathutils import Vector, Quaternion, Matrix

FPS = 60
LIMITS = dict(
    elbow_flex=(0.0, 150.0),      # deg; 0 is straight, never negative (hyperextended)
    knee_flex=(0.0, 140.0),
    # Arm joint angles (author_stroke.arm_anatomy), clinical ranges of motion.
    twist=(-90.0, 90.0),          # forearm pronation/supination carried by the twist bones
    humeral=(-80.0, 90.0),        # humerus axial rotation, + internal
    wflex=(-65.0, 75.0),          # wrist flexion +, extension -
    wdev=(-20.0, 35.0),           # wrist ulnar deviation +, radial -
    torso=(1.0, 99.0),            # elbow against the trunk's section: under 1 is inside the body
    bone_jump=(0.0, 20.0),        # deg per frame for any limb bone (a hand against the bat it holds): a flip or pop
    bat_turn=(0.0, 33.0),         # deg per frame of the bat itself, 2000 deg/s: the fastest real swings
    hitch=(0.0, 1.0),             # m/s the hands speed up again after slowing from contact: a stall in the follow-through
    hand_off_handle=(0.0, 0.005), # m of reach shortfall
    grip_slip=(0.0, 0.002),       # m the handle moves in either hand from where that hand first held it
    grip_turn=(0.0, 0.5),         # deg the handle turns in either hand: a real grip is fixed for the whole stroke
    planted_slide=(0.0, 0.003),   # m per frame of a planted ball of the foot
    sink=(-0.005, 9.0),           # m of the ball and heel against their rest height
)


def angle(q):
    """A rotation's size in degrees, 0..180, whichever sign the quaternion carries."""
    return math.degrees(2.0 * math.acos(min(1.0, abs(q.w))))


def run(st, contact_t=None):
    r = st.rig
    snaps = st.snap
    pos = lambda k, n: snaps[k][n].to_translation()
    # Each bone's rotation away from its rest pose, in world axes (what the author solved for).
    rot = lambda k, n: snaps[k][n].to_quaternion() @ r.rest[n].to_quaternion().inverted()
    worst = {}

    def note(name, value, k, where=''):
        lo, hi = LIMITS[name]
        bad = max(lo - value, value - hi, 0.0)
        w = worst.get(name)
        if w is None or bad > w[0] or (bad == 0 and w[0] == 0 and abs(value) > abs(w[1])):
            worst[name] = (bad, value, k, where)

    import author_stroke as A
    # The bat's frame each frame: the hands are judged against it, so a fast swing is not taken for a popped wrist.
    batq = [Matrix((a, f, a.cross(f))).transposed().to_quaternion() for _, _, a, f in st.frames]
    grip0 = {}
    for k in range(len(snaps)):
        for s in 'lr':
            S, E, H = pos(k, 'upperarm_' + s), pos(k, 'lowerarm_' + s), pos(k, 'hand_' + s)
            note('elbow_flex', math.degrees((E - S).angle(H - E)), k)
            # Hyperextension: the elbow must sit on the side it bends toward at rest.
            L = st.limb['arm' + s]
            h = rot(k, 'upperarm_' + s) @ L['h0']
            if (E - S).cross(H - E).dot(h) < -1e-6:
                note('elbow_flex', -1.0, k)
            T, K, F = pos(k, 'thigh_' + s), pos(k, 'calf_' + s), pos(k, 'foot_' + s)
            note('knee_flex', math.degrees((K - T).angle(F - K)), k)
            spine = (pos(k, 'spine_01'), pos(k, 'neck_01'))
            j = A.arm_anatomy(L, rot(k, 'spine_05'), spine, S, E, H, rot(k, 'upperarm_' + s), rot(k, 'lowerarm_' + s), rot(k, 'hand_' + s))
            for name in ('twist', 'humeral', 'wflex', 'wdev', 'torso'):
                note(name, j[name], k, s)
        for n in ('upperarm_l', 'upperarm_r', 'lowerarm_l', 'lowerarm_r', 'hand_l', 'hand_r', 'thigh_l', 'thigh_r',
                  'calf_l', 'calf_r', 'foot_l', 'foot_r', 'spine_05', 'head'):
            if k:
                q0, q1 = rot(k - 1, n), rot(k, n)
                if n.startswith('hand_'):
                    q0, q1 = batq[k - 1].inverted() @ q0, batq[k].inverted() @ q1
                note('bone_jump', angle(q0.rotation_difference(q1)), k, n)
        if k:
            note('bat_turn', angle(batq[k - 1].rotation_difference(batq[k])), k)
        # The bat in each hand's own frame: constant when the grip holds.
        for s in 'lr':
            h = snaps[k]['hand_' + s]
            held = (h.to_quaternion().inverted() @ (st.frames[k][1] - h.to_translation()),
                    h.to_quaternion().inverted() @ batq[k])
            first = grip0.setdefault(s, held)
            note('grip_slip', (held[0] - first[0]).length, k, s)
            note('grip_turn', angle(first[1].rotation_difference(held[1])), k, s)
        for s in 'lr':
            ball, heel = pos(k, 'ball_' + s), pos(k, 'foot_' + s)
            note('sink', ball.z - r.head0('ball_' + s).z, k)
            note('sink', heel.z - r.head0('foot_' + s).z, k)
            lift = st.c('foot%s_lift' % s.upper(), k + 1)
            if k and lift < 1e-4 and st.c('foot%s_lift' % s.upper(), k) < 1e-4:
                d = (ball - pos(k - 1, 'ball_' + s))
                note('planted_slide', Vector((d.x, d.y)).length, k, s)
    # Each arm at every key: reach against its length and its joint angles.
    for t, _ in st.keys:
        k = round(t * FPS)
        row = []
        for s in 'lr':
            L = st.limb['arm' + s]
            S, E, H = pos(k, 'upperarm_' + s), pos(k, 'lowerarm_' + s), pos(k, 'hand_' + s)
            j = A.arm_anatomy(L, rot(k, 'spine_05'), (pos(k, 'spine_01'), pos(k, 'neck_01')), S, E, H,
                              rot(k, 'upperarm_' + s), rot(k, 'lowerarm_' + s), rot(k, 'hand_' + s))
            row.append('%s %3.0f%% short %4.1fcm flex %3.0f hum %4.0f twist %4.0f wrist %4.0f/%4.0f torso %.1f' % (
                s, 100 * (H - S).length / (L['a'] + L['b']), 100 * st.shortfall['arm' + s][k], j['flex'], j['humeral'],
                j['twist'], j['wflex'], j['wdev'], j['torso']))
        print('KEY %.2f  %s  grip (%.2f %.2f %.2f)' % ((t, '   '.join(row)) + tuple(st.frames[k][1] * Vector((1, -1, 1)))))
    for side, v in st.shortfall.items():
        k = max(range(len(v)), key=lambda i: v[i])
        note('hand_off_handle' if side.startswith('arm') else 'sink', v[k] if side.startswith('arm') else 0.0, k)
        if side.startswith('leg') and v[k] > 0.005:
            worst['leg_reach_' + side] = (v[k], v[k], k, '')
    if contact_t is not None:
        # From contact the hands only slow down until the finish (0.4 s): speeding up again is a visible stall.
        k0 = round(contact_t * FPS)
        slowest = None
        for k in range(k0 + 1, min(k0 + round(0.4 * FPS), len(st.frames))):
            v = (st.frames[k][1] - st.frames[k - 1][1]).length * FPS
            slowest = v if slowest is None else min(slowest, v)
            note('hitch', v - slowest, k)
    fails = 0
    for name, (bad, value, k, where) in sorted(worst.items()):
        ok = bad <= 1e-9
        fails += not ok
        print('CHECK %-16s %s worst %8.3f at frame %d (%.2f s) %s' % (name, 'ok  ' if ok else 'FAIL', value, k + 1, k / FPS, where))
    if contact_t is not None:
        k = round(contact_t * FPS)
        _, g, a, f = st.frames[k]
        sweet = g + a * A.SWEET_FROM_GRIP
        head = pos(k, 'head')
        print('CONTACT frame %d sweet spot (%.2f, %.2f, %.2f) m, eyes %.2f m above and %.2f m behind it, bat %.0f deg off vertical'
              % (k + 1, sweet.x, -sweet.y, sweet.z, head.z - sweet.z, sweet.x - head.x, math.degrees(a.angle(Vector((0, 0, -1))))))
    print('VALIDATE %s' % ('PASS' if not fails else '%d FAIL' % fails))
    return fails

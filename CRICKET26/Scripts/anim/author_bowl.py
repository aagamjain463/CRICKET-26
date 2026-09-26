# Authors a bowling action as a baked AnimSequence on the players' own MetaHuman skeleton, in Blender, from the
# key tables in bowls.py. Source motion like the strokes (author_stroke.py), whose rig and limb solvers it reuses:
#   - pelvis, spine and head: forward kinematics; the eyes held on the batter, the neck taking part of the turn;
#   - legs: the analytic two-bone solve to each keyed ball of the foot;
#   - arms: the shoulder-to-wrist line keyed round the shoulder, the elbow toward its keyed pole, the palm turned
#     toward its keyed direction by the forearm (pronation) and the wrist's flexion on top.
# The pelvis carries the whole run: the game reads its travel back from the clip (CricketPose::BowlTravel) and holds
# the bowler's actor at the release point while the clip plays. The release is at START s before the clip's end
# minus END, i.e. CricketPose::BowlClipRelease into the clip.
# Usage: Blender -b -P author_bowl.py -- <Pace|OffSpin|LegSpin> <R|L> <rig Body.fbx> <out dir>
import bpy, sys, os, math
import numpy as np
from mathutils import Vector, Quaternion

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import author_stroke as A  # noqa: E402
import bowls  # noqa: E402
from author_stroke import Z, FPS, two_bone, twist_angle, rot_between, smooth  # noqa: E402

# Finger curls (deg per joint): the bowling hand round the ball, the other hand relaxed.
CURL_BALL = {'thumb': (25, 20, 15), 'index': (20, 25, 20), 'middle': (20, 25, 20), 'ring': (45, 45, 30),
             'pinky': (55, 45, 30)}
CURL_OPEN = {'thumb': (5, 10, 5), 'index': (10, 15, 10), 'middle': (12, 18, 10), 'ring': (15, 20, 12),
             'pinky': (18, 20, 12)}
# Checks on the baked clip (validate()).
HEAD_TURN = 8.0     # deg per frame the head may turn in the world: steady, no snap or bob
HEAD_ROLL = 25.0    # deg the eyes' line may tilt off level
LEGAL_ELBOW = 15.0  # deg the bowling elbow may straighten from the arm's horizontal to the release (ICC law 21.2)
BONE_JUMP = 50.0    # deg per frame for any limb bone: the arm's whip (about 30) plus the wrist's snap at release
TURN_RATE = math.radians(20)  # per frame the forearm may pronate or supinate: 1200 deg/s
SLIDE = 0.004       # m per frame a planted ball of the foot may move


def pchip(ts, vs, t):
    """Fritsch-Carlson monotone cubic through (ts, vs), sampled at t (held flat past the ends)."""
    if len(ts) == 1:
        return np.full(len(t), vs[0])
    h, d = np.diff(ts), np.diff(vs) / np.diff(ts)
    m = np.empty(len(ts))
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, len(ts) - 1):
        w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
        m[i] = 0.0 if d[i - 1] * d[i] <= 0 else (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    tc = np.clip(t, ts[0], ts[-1])
    i = np.clip(np.searchsorted(ts, tc, side='right') - 1, 0, len(ts) - 2)
    u = (tc - ts[i]) / h[i]
    return (vs[i] * (2 * u**3 - 3 * u**2 + 1) + m[i] * h[i] * (u**3 - 2 * u**2 + u)
            + vs[i + 1] * (-2 * u**3 + 3 * u**2) + m[i + 1] * h[i] * (u**3 - u**2))


def B(f, r, z=0.0):
    """Bowler frame (along the run, toward the bowling arm of a right-armer, up) to Blender world: the body faces
    -Y at rest and its right is -X."""
    return Vector((-r, -f, z))


def body_q(yaw, bend, side):
    """World rotation: turned yaw deg toward the right, bent forward, then leaning toward the right (deg)."""
    qy = Quaternion(Z, math.radians(-yaw))
    face, right = qy @ Vector((0, -1, 0)), qy @ Vector((-1, 0, 0))
    qb = Quaternion(Z.cross(face).normalized(), math.radians(bend))
    qs = Quaternion(Z.cross(right).normalized(), math.radians(side))
    return qs @ qb @ qy


class Bowl(A.Stroke):
    def __init__(self, arm, tracks, bowl_side):
        super().__init__(arm, [])
        self.tracks = tracks
        self.bowl_side = bowl_side  # 'r' or 'l': the hand with the ball

    def build_controls(self):
        """Every channel sampled per frame by a monotone cubic through its keys: no overshoot past a key, so a
        planted foot's held keys hold it still and nothing swings past its extreme."""
        self.last = self.frame_of(bowls.END)
        t = bowls.START + np.arange(self.last) / FPS
        self.curves = {n: pchip(np.array([k[0] for k in tr]), np.array([k[1] for k in tr], float), t)
                       for n, tr in self.tracks.items()}
        self.drop = np.zeros(self.last)

    def c(self, name, fr, default=0.0):
        v = self.curves.get(name)
        return float(v[fr - 1]) if v is not None else default

    @staticmethod
    def frame_of(t):
        return 1 + round((t - bowls.START) * FPS)

    def vec(self, name, fr):
        return B(self.c(name + '_f', fr), self.c(name + '_r', fr), self.c(name + '_z', fr))

    def pose(self, fr):
        r = self.rig
        r.reset()
        c = lambda n: self.c(n, fr)
        qh = body_q(c('hip_yaw'), c('hip_bend'), c('hip_side'))
        qc = body_q(c('ch_yaw'), c('ch_bend'), c('ch_side'))
        r.delta['pelvis'] = qh
        r.place['pelvis'] = r.head0('pelvis') + B(c('hip_f'), c('hip_r'), c('hip_z') - self.drop[fr - 1])
        twist = qc @ qh.inverted()
        for n, share in (('spine_01', .15), ('spine_02', .35), ('spine_03', .55), ('spine_04', .8), ('spine_05', 1.0)):
            r.delta[n] = Quaternion().slerp(twist, share) @ qh
        for s, sign in (('l', 1), ('r', -1)):
            lat, face = qc @ Vector((sign, 0, 0)), qc @ Vector((0, -1, 0))
            r.delta['clavicle_' + s] = Quaternion(lat.cross(qc @ Z), math.radians(c('clav%s_up' % s.upper()))) @ qc
        r.solve()
        # Head: eyes on the batter, kept near level (the eyes' line mostly the horizon's, not the chest's).
        eyes = r.world['head'].to_translation()
        look = (self.vec('look', fr) - eyes).normalized()
        cface = qc @ Vector((0, -1, 0))
        ang = cface.angle(look)
        if ang > math.radians(85):
            look = cface.slerp(look, math.radians(85) / ang)
        up = (Z * 0.85 + (qc @ Z) * 0.15).normalized()
        qhead = A.rot_between_frames(Vector((0, -1, 0)), Z, look, up)
        r.delta['neck_01'] = qc.slerp(qhead, 0.3)
        r.delta['neck_02'] = qc.slerp(qhead, 0.6)
        r.delta['head'] = qhead
        r.solve()
        # Legs. The pelvis sinks where a keyed foot is out of the leg's reach (need; bake() smooths it into drop).
        need = 0.0
        for s in 'lr':
            S = s.upper()
            yaw = Quaternion(Z, math.radians(-c('foot%s_yaw' % S)))
            b0, a0 = r.head0('ball_' + s), r.head0('foot_' + s)
            toe = yaw @ Vector((b0 - a0).to_2d().to_3d()).normalized()
            q = Quaternion((-toe).cross(Z).normalized(), math.radians(c('foot%s_heel' % S))) @ yaw
            ball = B(c('foot%s_f' % S), c('foot%s_r' % S), b0.z + c('foot%s_lift' % S))
            L = self.limb['leg' + s]
            hip = r.world['thigh_' + s].to_translation()
            outward = qh @ Vector((1 if s == 'l' else -1, 0, 0))
            ankle = ball + q @ (a0 - b0)
            flat = (ankle - hip).to_2d().length
            reach = (L['a'] + L['b']) * 0.99
            if flat < reach:  # beyond that no sinking helps: the keys themselves are wrong
                need = max(need, hip.z - ankle.z - math.sqrt(reach * reach - flat * flat))
            knee, end, short, side = two_bone(hip, L['a'], L['b'], ankle, toe + outward * 0.25)
            self.shortfall.setdefault('leg' + s, []).append(short)
            self._limb(('thigh_' + s, 'calf_' + s), L, hip, knee, end, side)
            r.delta['foot_' + s] = q
            r.delta['ball_' + s] = yaw
        self.need[fr - 1] = need
        r.solve()
        # Arms.
        fwd, down = B(1, 0), -Z
        for s in 'lr':
            S = s.upper()
            L = self.limb['arm' + s]
            out = B(0, 1 if s == 'r' else -1)
            a, p = math.radians(c('arm%s_a' % S)), math.radians(c('arm%s_p' % S))
            line = down * math.cos(a) + (fwd * math.cos(p) + out * math.sin(p)) * math.sin(a)
            shoulder = r.world['upperarm_' + s].to_translation()
            wrist = shoulder + line * (L['a'] + L['b']) * min(c('arm%s_reach' % S), 0.9995)
            elbow, end, _, side = two_bone(shoulder, L['a'], L['b'], wrist, self.vec('arm%s_pole' % S, fr))
            qu, qf = self._limb(('upperarm_' + s, 'lowerarm_' + s), L, shoulder, elbow, end, side)
            # The palm turned about the forearm toward its keyed direction, within the forearm's range.
            fdir = qf @ L['f0']
            want = self.vec('hand%s_palm' % S, fr)
            n = want.length
            want = want - fdir * want.dot(fdir)
            palm = qf @ self.hand[s]['palm']
            tw = self.turn.get(s, 0.0)
            if want.length > 0.35 * n:  # a palm keyed nearly along the forearm says nothing about its turn
                # Unwrapped onto the last frame's turn, so a palm keyed round past the forearm's range holds at
                # the limit instead of flipping to the other one
                step = (math.atan2(palm.cross(want).dot(fdir), palm.dot(want.normalized())) - tw + math.pi) % (2 * math.pi) - math.pi
                tw += max(-TURN_RATE, min(TURN_RATE, step))  # and no faster than a forearm turns
            self.turn[s] = tw
            tw = max(-math.radians(85), min(math.radians(85), tw))
            qhand = Quaternion(fdir, tw) @ qf @ Quaternion(L['flex_axis'], math.radians(c('hand%s_flex' % S)))
            r.delta['hand_' + s] = qhand
            u = qu @ L['u0']
            tu = twist_angle(rot_between(L['u0'], u).inverted() @ qu, L['u0'])
            r.delta['upperarm_twist_01_' + s] = Quaternion(u, -tu * 0.5) @ qu
            r.delta['upperarm_twist_02_' + s] = Quaternion(u, -tu * 0.25) @ qu
            tf = twist_angle(qf.inverted() @ qhand, L['f0'])
            r.delta['lowerarm_twist_01_' + s] = Quaternion(fdir, tf * 2 / 3) @ qf
            r.delta['lowerarm_twist_02_' + s] = Quaternion(fdir, tf / 3) @ qf
            palm = qhand @ self.hand[s]['palm']
            for fname, angles in (CURL_BALL if s == self.bowl_side else CURL_OPEN).items():
                tot = 0.0
                for k, ang in enumerate(angles):
                    bn = '%s_%02d_%s' % (fname, k + 1, s)
                    if bn not in r.rest:
                        continue
                    nxt = '%s_%02d_%s' % (fname, k + 2, s)
                    d = (r.head0(nxt) - r.head0(bn)) if nxt in r.rest else (r.head0(bn) - r.head0('%s_%02d_%s' % (fname, k, s)))
                    tot += ang
                    r.delta[bn] = qhand @ Quaternion(d.normalized().cross(self.hand[s]['palm']).normalized(), math.radians(tot))
        r.solve()

    def bake(self, name):
        self.need, self.turn = np.zeros(self.last), {}
        for fr in range(1, self.last + 1):
            self.pose(fr)
        grown = np.array([self.need[max(k - 6, 0):k + 7].max() for k in range(self.last)]).clip(0, None)
        self.drop = smooth(grown, [3.0] * self.last)
        self.shortfall, self.snap, self.turn = {}, [], {}
        for fr in range(1, self.last + 1):
            self.pose(fr)
            self.rig.key(fr)
            self.snap.append({n: m.copy() for n, m in self.rig.world.items()})
        self.rig.arm.animation_data.action.name = name
        sc = bpy.context.scene
        sc.frame_start, sc.frame_end = 1, self.last
        sc.render.fps, sc.render.fps_base = FPS, 1.0


def validate(st):
    """One line per check with its worst frame; returns the failures."""
    snaps, s = st.snap, st.bowl_side
    pos = lambda k, n: snaps[k][n].to_translation()
    t_of = lambda k: bowls.START + k / FPS
    fails, lines = [], []

    def check(name, value, limit, k, ok):
        lines.append('%s %-14s %7.2f (limit %.2f) at %.2f s' % ('ok  ' if ok else 'FAIL', name, value, limit, t_of(k)))
        if not ok:
            fails.append(name)

    # The head: turns slowly in the world, eyes near level.
    rq = lambda k, n: snaps[k][n].to_quaternion()
    turn = max((A.angle(rq(k - 1, 'head').rotation_difference(rq(k, 'head'))), k) for k in range(1, len(snaps)))
    check('head_turn', turn[0], HEAD_TURN, turn[1], turn[0] <= HEAD_TURN)
    rest_q = st.rig.rest['head'].to_quaternion()
    roll = max((math.degrees(math.asin(min(1.0, abs(((rq(k, 'head') @ rest_q.inverted()) @ Vector((1, 0, 0))).z)))), k)
               for k in range(len(snaps)))
    check('head_roll', roll[0], HEAD_ROLL, roll[1], roll[0] <= HEAD_ROLL)
    # Limb bones never pop.
    limbs = [n + x for n in ('upperarm_', 'lowerarm_', 'hand_', 'thigh_', 'calf_', 'foot_') for x in 'lr']
    jump = max((A.angle(rq(k - 1, n).rotation_difference(rq(k, n))), k, n) for k in range(1, len(snaps)) for n in limbs)
    check('bone_jump ' + jump[2], jump[0], BONE_JUMP, jump[1], jump[0] <= BONE_JUMP)
    # A legal action: the elbow straightens no more than 15 deg from the arm's horizontal (behind) to the release.
    flex = lambda k: math.degrees((pos(k, 'lowerarm_' + s) - pos(k, 'upperarm_' + s)).angle(pos(k, 'hand_' + s) - pos(k, 'lowerarm_' + s)))
    rel = Bowl.frame_of(0.0) - 1
    horiz = next(k for k in range(rel, 0, -1) if pos(k, 'hand_' + s).z < pos(k, 'upperarm_' + s).z)
    straighten = max(flex(k) for k in range(horiz, rel + 1)) - flex(rel)
    check('elbow_extend', straighten, LEGAL_ELBOW, rel, straighten <= LEGAL_ELBOW)
    check('release_high', pos(rel, 'hand_' + s).z, 1.9, rel, pos(rel, 'hand_' + s).z >= 1.9)
    # Feet: a planted ball stays put and nothing sinks through the ground.
    slide, sink = (0.0, 0), (0.0, 0)
    for x in 'lr':
        z0 = st.rig.head0('ball_' + x).z
        for k in range(1, len(snaps)):
            fr = k + 1
            if st.c('foot%s_lift' % x.upper(), fr) < 1e-4 and st.c('foot%s_lift' % x.upper(), fr - 1) < 1e-4:
                slide = max(slide, ((pos(k, 'ball_' + x) - pos(k - 1, 'ball_' + x)).length, k))
            sink = max(sink, (z0 - pos(k, 'ball_' + x).z, k))
    check('planted_slide', slide[0], SLIDE, slide[1], slide[0] <= SLIDE)
    check('sink', sink[0], 0.01, sink[1], sink[0] <= 0.01)
    short = max((v[k], k) for v in st.shortfall.values() for k in range(len(v)))
    check('reach_short', short[0], 0.01, short[1], short[0] <= 0.01)
    print('\n'.join(lines))
    return fails


def author(kind, hand, rig, out):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=rig)
    arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    tracks = bowls.KEYS[kind] if hand == 'R' else bowls.mirror(bowls.KEYS[kind])
    st = Bowl(arm, tracks, hand.lower())
    st.build_controls()
    name = 'Bowl_%s_%s' % (kind, hand)
    st.bake(name)
    fails = validate(st)
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, name + '.blend'))
    for o in bpy.context.scene.objects:
        o.select_set(o.type == 'ARMATURE')
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(filepath=os.path.join(out, name + '.fbx'), use_selection=True, object_types={'ARMATURE'},
                             add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                             bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, armature_nodetype='NULL')
    print('WROTE', os.path.join(out, name + '.fbx'), 'FAILS' if fails else 'VALID', ' '.join(fails))
    return st, fails


if __name__ == '__main__':
    argv = sys.argv[sys.argv.index('--') + 1:]
    author(argv[0], argv[1], argv[2], argv[3])

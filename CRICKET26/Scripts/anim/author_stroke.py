# Authors a batting stroke as a baked AnimSequence on the striker's own MetaHuman skeleton, in Blender.
#
# This is the source motion: keyed like a hand animation, not invented at runtime. A stroke is a table of keys
# (strokes.py) on a few animator controls: the bat (grip, swing angle, face), each foot (ball of the foot, toe
# yaw, heel raise, lift), the pelvis, the chest, the clavicles, where each elbow points (its pole, in the chest's
# frame) and where the eyes look. Blender's F-curves interpolate the keys. Every frame the skeleton is then fitted
# to the controls:
#   - pelvis, spine (the chest's turn and bend shared up the vertebrae), neck and head: forward kinematics;
#   - hands: rigidly on the handle, the top hand above the bottom, each with the grip's measured hand frame;
#   - legs: an analytic two-bone solve in the plane of the authored pole;
#   - arms: planned over the whole stroke at once (plan_arms). Each frame offers a grid of hand rolls about the
#     handle and elbow swivels about the shoulder-wrist line; every state outside the arm's clinical range
#     (validate_stroke.LIMITS: humeral rotation, forearm twist, wrist, the elbow against the trunk) is ruled out,
#     and so is every step between frames that turns a bone faster than an arm can. The cheapest legal path is
#     taken and smoothed. Where no legal motion exists the frames are reported ILLEGAL: the keys must change
#     there, the solver never bends the anatomy to make them fit;
#   - the forearm's axial turn goes to the twist bones (two thirds at the wrist), not the wrist;
#   - planted feet: the ball of the foot is keyed, so it cannot slide; the heel pivots about it.
# The baked action is checked (validate_stroke.py) and written as FBX for Unreal (import_stroke.sh).
# Regression checks: test_author_stroke.py.
#
# Usage: Blender -b -P author_stroke.py -- <stroke> <rig Body.fbx> <out dir> [--mirror]
import bpy, sys, os, math, importlib
import numpy as np
from mathutils import Vector, Matrix, Quaternion

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strokes  # noqa: E402
importlib.reload(strokes)
from validate_stroke import LIMITS, angle  # noqa: E402

FPS = 60
Z = Vector((0, 0, 1))
GRIP_HALF = 0.045       # each hand's centre from the grip centre, along the handle (CricketPose)
SWEET_FROM_GRIP = 0.55  # CricketPose::SweetFromGrip
GRIP_FROM_TOP = 0.12
BAT_LENGTH = 0.85
GRIP_DIAG = 50.0        # deg between the handle and the knuckle line (calibrated with GRIP_ROLL)
POLE_WEIGHT = 0.1       # cost per degree of an elbow off its authored pole (a degree of wrist bend costs 1)
FACE_FREEDOM = 60       # deg the solver may roll the face off its key, away from contact
LIMIT_WEIGHT = 20.0     # cost per degree past a validator limit
SWIVEL_RATE = 0.2       # cost per squared degree an arm bone turns in one frame
ROLL_STEP_MAX = 10      # deg the solver may roll the face in one frame
SMOOTH_FRAMES = 1.5     # sigma of the smoothing that turns the grid path into a continuous motion
OFF_PATH = 1e5          # cost of an arm state from which no legal motion runs through the whole clip
# Comfortable range of each arm_anatomy angle (lo, hi, deg) and its cost per degree inside it; beyond it a degree
# costs 4 more. Wrist and forearm from clinical ranges of motion, a little inside them; the elbow kept off
# straight, where its swivel and so the split of twist between humerus and forearm is undefined.
COMFORT = dict(flex=(12, 150, 0.0), twist=(-75, 75, 0.3), wflex=(-50, 60, 0.5), wdev=(-15, 30, 1.0), humeral=(-60, 75, 0.2))
TORSO = (0.21, 0.17, 0.14)  # m: the trunk's half width, depth in front and behind the spine, plus an elbow's radius
NECK_CLEAR = 0.10  # m above the base of the neck at which a raised elbow has cleared the trunk whatever its section
# Axial rotation has no meaning for an arm pointing one way (a swing-twist singularity). Measured by swinging from the
# hanging arm, that way is straight up, which a high finish reaches, and the angle there spins wildly. Swinging via the
# arm abducted in the scapular plane (this many degrees forward of the side) puts it behind the back on the other side,
# where no arm can point; for an arm in that plane, hanging to overhead, both measures agree.
SCAPULAR_PLANE = 35.0
CURL = {'thumb': (15, 25, 25), 'index': (50, 65, 40), 'middle': (55, 70, 40), 'ring': (60, 70, 40), 'pinky': (65, 70, 40)}


def W(f, o, z=0.0):
    """Batter frame (toward the bowler, toward the off side, up) to Blender world. The body faces -Y at rest."""
    return Vector((f, -o, z))


def frame(d, h):
    d = d.normalized()
    h = (h - d * h.dot(d)).normalized()
    return Matrix((d, h, d.cross(h))).transposed()


def rot_between_frames(d0, h0, d1, h1):
    return (frame(d1, h1) @ frame(d0, h0).inverted()).to_quaternion()


def body_rotation(yaw, bend, side):
    """World rotation: turned yaw deg toward the bowler, bent forward, then leaning toward the bowler (deg)."""
    qy = Quaternion(Z, math.radians(yaw))
    face = qy @ Vector((0, -1, 0))
    lat = qy @ Vector((1, 0, 0))
    qb = Quaternion(Z.cross(face).normalized(), math.radians(bend))
    qs = Quaternion(Z.cross(lat).normalized(), math.radians(side))
    return qs @ qb @ qy


class Rig:
    """The imported skeleton's rest pose, and a pose built bone by bone as world-space deltas from it."""

    def __init__(self, arm):
        self.arm = arm
        # Rest frames in metres (the FBX armature carries a 0.01 scale): rigid, so poses are pure rotations.
        M = arm.matrix_world
        self.scale = M.to_scale()[0]
        self.rest = {b.name: Matrix.Translation(M @ b.head_local) @ (M.to_quaternion() @ b.matrix_local.to_quaternion()).to_matrix().to_4x4()
                     for b in arm.data.bones}
        self.parent = {b.name: (b.parent.name if b.parent else None) for b in arm.data.bones}
        self.order = [b.name for b in arm.data.bones]  # parents before children
        self.world = {}
        self.delta = {}
        self.place = {}

    def head0(self, n):
        return self.rest[n].to_translation()

    def reset(self):
        self.delta, self.place, self.world = {}, {}, {}

    def solve(self):
        """World matrices for every bone: each authored bone gets its delta (and head, for the pelvis); others follow."""
        for n in self.order:
            p = self.parent[n]
            local = (self.rest[p].inverted() @ self.rest[n]) if p else self.rest[n]
            carried = (self.world[p] @ local) if p else local
            if n in self.delta:
                head = self.place.get(n, carried.to_translation())
                m = Matrix.Translation(head) @ self.delta[n].to_matrix().to_4x4() @ self.rest[n].to_quaternion().to_matrix().to_4x4()
                self.world[n] = m
            else:
                self.world[n] = carried

    def key(self, fr):
        for n in self.order:
            pb = self.arm.pose.bones[n]
            p = self.parent[n]
            local = (self.rest[p].inverted() @ self.rest[n]) if p else self.rest[n]
            basis = ((self.world[p] @ local) if p else local).inverted() @ self.world[n]
            if n not in self.delta:
                continue
            loc, q, _ = basis.decompose()
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = q
            pb.keyframe_insert('rotation_quaternion', frame=fr, group=n)
            if n in self.place:
                pb.location = loc / self.scale
                pb.keyframe_insert('location', frame=fr, group=n)


def two_bone(root, a, b, target, bend_dir):
    """The middle joint of a limb a then b long from root toward target, bent toward bend_dir. Returns (joint,
    end, reach shortfall)."""
    d = target - root
    dist = d.length
    short = max(dist - (a + b) * 0.9995, 0.0)
    dist = min(max(dist, abs(a - b) + 1e-3), (a + b) * 0.9995)
    u = d.normalized()
    x = (a * a - b * b + dist * dist) / (2 * dist)
    side = (bend_dir - u * bend_dir.dot(u)).normalized()
    joint = root + u * x + side * math.sqrt(max(a * a - x * x, 0.0))
    return joint, root + u * dist, short, side


def limb_rotations(L, root, joint, end, side):
    """World rotations of a limb's two bones from rest: each bone along its segment, the hinge axis carried from
    rest, so the joint bends only the way it does at rest."""
    dd = (end - root).normalized()
    h = side.cross(dd).normalized()
    qu = rot_between_frames(L['u0'], L['h0'], (joint - root).normalized(), h)
    qf = rot_between_frames(L['f0'], L['h0'], (end - joint).normalized(), h)
    return qu, qf


def arm_anatomy(L, qc, spine, shoulder, elbow, wrist, qu, qf, qhand):
    """One arm's joint angles in anatomical terms (degrees), for the solver's cost and the validator:
      flex     elbow flexion, 0 straight;
      humeral  the humerus's axial rotation against a hanging arm with the forearm forward, in the chest's frame,
               + internal (the swing-twist of the ISB shoulder angles, so the arm's elevation does not count);
               the swing is taken through the scapular plane, see SCAPULAR_PLANE;
      twist    the forearm's turn carried by the twist bones, + as the grip rolls the hand;
      wflex    wrist flexion +, extension -;   wdev  ulnar deviation +, radial -;
      torso    the elbow's place against the trunk's section, an ellipse: under 1 is inside the body."""
    rel = qf.inverted() @ qhand
    tw = twist_angle(rel, L['f0'])
    ax, ang = (rel @ Quaternion(L['f0'], -tw)).to_axis_angle()
    ang = ang - 2 * math.pi if ang > math.pi else ang
    rv = ax * ang
    down, back = qc @ -Z, qc @ Vector((0, 1, 0))
    qref = rot_between_frames(L['u0'], L['h0'], down, back.cross(down))
    u = (elbow - shoulder).normalized()
    a, b = spine
    lat = qc @ Vector((1, 0, 0))
    lat = lat if (shoulder - a).dot(lat) > 0 else -lat
    plane = Quaternion(down.cross(lat), math.radians(SCAPULAR_PLANE)) @ lat  # abducted in the scapular plane
    via = rot_between(down, plane)
    hum = twist_angle(rot_between(plane, u).inverted() @ qu @ qref.inverted() @ via.inverted(), plane)
    t = (elbow - a).dot(b - a) / (b - a).length_squared
    v = qc.inverted() @ (elbow - (a + (b - a) * max(0.0, min(1.0, t))))
    # The trunk ends at the neck: an elbow raised above it (a high finish) is clear of the chest as it rises.
    above = max(t - 1.0, 0.0) * (b - a).length
    torso = math.hypot(v.x / TORSO[0], v.y / (TORSO[1] if v.y < 0 else TORSO[2]), above / NECK_CLEAR)
    return dict(flex=math.degrees(u.angle(wrist - elbow)), humeral=math.degrees(hum) * L['inward'], twist=math.degrees(tw),
                wflex=math.degrees(rv.dot(L['flex_axis'])), wdev=math.degrees(rv.dot(L['dev_axis'])) * L['ulnar'], torso=torso)


def viterbi(costs, trans):
    """The cheapest path through a grid of states per frame. costs[k] is an array over the state axes at frame k;
    trans[a] is the square cost of moving along axis a from one frame to the next, added over the axes, so the
    whole grid is searched jointly one axis at a time; a callable trans(k) gives frame k's own matrices. Returns
    the chosen index tuple per frame."""
    acc, back = costs[0], []
    for k in range(1, len(costs)):
        steps = []
        for a, t in enumerate(trans(k) if callable(trans) else trans):
            x = np.moveaxis(acc, a, 0)
            m = x[:, None] + t.reshape(t.shape + (1,) * (x.ndim - 1))  # m[i, j, ...]: from i to j along axis a
            steps.append(np.moveaxis(m.argmin(0), 0, a))
            acc = np.moveaxis(m.min(0), 0, a)
        acc = acc + costs[k]
        back.append(steps)
    idx = list(np.unravel_index(acc.argmin(), acc.shape))
    path = [tuple(idx)]
    for steps in reversed(back):
        for a in reversed(range(len(steps))):
            idx[a] = steps[a][tuple(idx)]
        path.append(tuple(idx))
    return path[::-1]


def smooth(x, sigma):
    """A per-frame sequence under a Gaussian of sigma[k] frames at each frame k (0 keeps x[k]), cut at the ends."""
    j = np.arange(len(x))
    w = [np.exp(-0.5 * ((j - k) / s) ** 2) if s > 0 else (j == k).astype(float) for k, s in enumerate(sigma)]
    return np.array([wk @ x / wk.sum() for wk in w])


def legal_motion(legal, steps):
    """Which grid states lie on some motion that is legal at every frame and moves legally between frames: forward
    and backward reachability through legal[k] (boolean grids), steps(k) giving per axis the square boolean matrix of
    allowed moves from frame k-1 to k. Where no legal state is reachable the search restarts there, so every broken
    window is reported. Returns (per frame the states on a legal motion, (first, last) spans of frames no legal
    motion reaches: either no legal state at all or none within one legal step of the frame before)."""
    def advance(reach, k, back):
        for a, m in enumerate(steps(k)):
            m = m.T if back else m
            x = np.moveaxis(reach, a, 0).astype(np.int32)
            reach = np.moveaxis(np.tensordot(m.T.astype(np.int32), x, axes=1) > 0, 0, a)
        return reach
    n = len(legal)
    fwd = [legal[0]]
    bad = [] if legal[0].any() else [0]
    # After a frame with no legal pose at all, the next starts afresh from its own legal states.
    for k in range(1, n):
        r = (advance(fwd[-1], k, False) if fwd[-1].any() else True) & legal[k]
        if not r.any():
            bad.append(k)
        fwd.append(r if r.any() else legal[k])
    bwd = [None] * n
    bwd[-1] = legal[-1]
    for k in range(n - 2, -1, -1):
        r = (advance(bwd[k + 1], k + 1, True) if bwd[k + 1].any() else True) & legal[k]
        bwd[k] = r if r.any() else legal[k]
    on = [f & b for f, b in zip(fwd, bwd)]
    spans = []
    for k in bad:
        if spans and spans[-1][1] == k - 1:
            spans[-1][1] = k
        else:
            spans.append([k, k])
    return on, [(a, b, 'no legal pose' if not any(legal[k].any() for k in range(a, b + 1)) else 'too fast a change')
                for a, b in spans]


def rot_between(a, b):
    return a.rotation_difference(b)


def twist_angle(q, axis):
    """Signed rotation of q about axis (swing-twist), radians."""
    v = Vector((q.x, q.y, q.z))
    t = 2.0 * math.atan2(v.dot(axis), q.w)
    return (t + math.pi) % (2 * math.pi) - math.pi


class Stroke:
    def __init__(self, arm, keys, mirror=False):
        self.rig = Rig(arm)
        self.keys = keys
        self.mirror = mirror
        r = self.rig
        # Hands: the grip frame measured off each hand's rest bones (as CricketAnimInstance's BuildRig does).
        self.hand = {}
        for s in 'lr':
            h = r.head0('hand_' + s)
            idx, pky, mid = r.head0('index_01_' + s), r.head0('pinky_01_' + s), r.head0('middle_01_' + s)
            across = (pky - idx).normalized()
            out = (mid - h) - across * (mid - h).dot(across)
            out.normalize()
            # Palm normal by handedness (the rest fingers are too straight for their curl to tell): out x across is
            # the palm on a right hand and the back of the hand on a left one.
            palm = out.cross(across) * (-1 if s == 'l' else 1)
            assert palm.x * (1 if s == 'l' else -1) < 0, 'rest palms should face the thighs'
            grip = (idx + pky) * 0.5 + out * 0.015 + palm * 0.028
            self.hand[s] = dict(across=across, palm=palm, offset=grip - h, out=out)
        self.limb = {}
        for s in 'lr':
            S, E, H = r.head0('upperarm_' + s), r.head0('lowerarm_' + s), r.head0('hand_' + s)
            dd = (H - S).normalized()
            side = ((E - S) - dd * (E - S).dot(dd)).normalized()
            L = self.limb['arm' + s] = dict(a=(E - S).length, b=(H - E).length, u0=(E - S).normalized(), f0=(H - E).normalized(),
                                            h0=side.cross(dd).normalized())
            # Wrist axes at rest: flexion tips the hand toward the palm; deviation turns it about the palm normal.
            palm = self.hand[s]['palm']
            L['flex_axis'] = L['f0'].cross(palm).normalized()
            L['dev_axis'] = (palm - L['f0'] * palm.dot(L['f0'])).normalized()
            L['ulnar'] = 1 if L['dev_axis'].cross(L['f0']).dot(self.hand[s]['across']) > 0 else -1
            # A turn about the hanging arm's axis (down) swings a left forearm toward the body, a right one away.
            L['inward'] = 1 if s == 'l' else -1
            T, K, A = r.head0('thigh_' + s), r.head0('calf_' + s), r.head0('foot_' + s)
            dd = (A - T).normalized()
            fwd = Vector((0, -1, 0))
            side = (fwd - dd * fwd.dot(dd)).normalized()
            self.limb['leg' + s] = dict(a=(K - T).length, b=(A - K).length, u0=(K - T).normalized(), f0=(A - K).normalized(),
                                        h0=side.cross(dd).normalized())
        self.shortfall = {}

    # --- controls -------------------------------------------------------------------------------------------
    def build_controls(self):
        """One empty whose custom properties are the animator's channels, keyed from the stroke table."""
        ctl = bpy.data.objects.new('C_Stroke', None)
        bpy.context.scene.collection.objects.link(ctl)
        names = sorted({k for _, ch in self.keys for k in ch})
        for n in names:
            ctl[n] = 0.0
        for t, ch in self.keys:
            fr = 1 + round(t * FPS)
            for n, v in ch.items():
                ctl[n] = float(v)
                ctl.keyframe_insert('["%s"]' % n, frame=fr)
        for fc in ctl.animation_data.action.fcurves if hasattr(ctl.animation_data.action, 'fcurves') else []:
            for kp in fc.keyframe_points:
                kp.interpolation = 'BEZIER'
                kp.handle_left_type = kp.handle_right_type = 'AUTO_CLAMPED'
        self.ctl = ctl
        self.last = 1 + round(self.keys[-1][0] * FPS)
        self.curves = {}
        for fc in iter_fcurves(ctl):
            self.curves[fc.data_path[2:-2]] = fc

    def c(self, name, fr, default=0.0):
        fc = self.curves.get(name)
        return fc.evaluate(fr) if fc else default

    # --- one frame ------------------------------------------------------------------------------------------
    def bat(self, fr):
        c = lambda n: self.c(n, fr)
        th, ph, ps = math.radians(c('bat_th')), math.radians(c('bat_ph')), math.radians(c('bat_ps'))
        along = W(math.cos(ps), math.sin(ps), 0)
        axis = along * math.sin(th) - Z * math.cos(th)
        face0 = along * math.cos(th) + Z * math.sin(th)
        face = Quaternion(axis, ph) @ face0
        grip = W(c('bat_f'), c('bat_o'), c('bat_z'))
        return grip, axis.normalized(), face.normalized()

    def pose_body(self, fr):
        """Pelvis, spine, clavicles, head and legs for one frame. Returns the chest's rotation."""
        r = self.rig
        r.reset()
        c = lambda n, d=0.0: self.c(n, fr, d)
        # Pelvis and spine.
        qh = body_rotation(c('hip_yaw'), c('hip_tilt'), c('hip_side'))
        qc = body_rotation(c('ch_yaw'), c('ch_bend'), c('ch_side'))
        r.delta['pelvis'] = qh
        r.place['pelvis'] = r.head0('pelvis') + W(c('hip_f'), c('hip_o'), -c('drop'))
        twist = qc @ qh.inverted()
        for n, share in (('spine_01', .15), ('spine_02', .35), ('spine_03', .55), ('spine_04', .8), ('spine_05', 1.0)):
            r.delta[n] = Quaternion().slerp(twist, share) @ qh
        # Clavicles: raised and drawn forward about the chest.
        for s, sign in (('l', 1), ('r', -1)):
            lat = qc @ Vector((sign, 0, 0))
            face = qc @ Vector((0, -1, 0))
            up = qc @ Z
            q = Quaternion(lat.cross(up), math.radians(c('clav%s_up' % s.upper()))) @ Quaternion(lat.cross(face), math.radians(c('clav%s_fwd' % s.upper())))
            r.delta['clavicle_' + s] = q @ qc
        r.solve()
        # Head: eyes on the look point, the neck taking part of the turn; at most 85 deg from the chest.
        eyes = r.world['head'].to_translation()
        look = (W(c('look_f'), c('look_o'), c('look_z')) - eyes).normalized()
        cface = qc @ Vector((0, -1, 0))
        ang = cface.angle(look)
        if ang > math.radians(85):
            look = cface.slerp(look, math.radians(85) / ang) if hasattr(cface, 'slerp') else look
        up = (Z * 0.7 + (qc @ Z) * 0.3).normalized()
        qhead = rot_between_frames(Vector((0, -1, 0)), Z, look, up)
        r.delta['neck_01'] = qc.slerp(qhead, 0.3)
        r.delta['neck_02'] = qc.slerp(qhead, 0.6)
        r.delta['head'] = qhead
        r.solve()

        # Feet and legs.
        for s in 'lr':
            S = s.upper()
            yaw = Quaternion(Z, math.radians(c('foot%s_yaw' % S)))
            b0, a0 = r.head0('ball_' + s), r.head0('foot_' + s)
            toe = yaw @ Vector((b0 - a0).to_2d().to_3d()).normalized()
            q = Quaternion((-toe).cross(Z).normalized(), math.radians(c('foot%s_heel' % S))) @ yaw
            ball = W(c('foot%s_f' % S), c('foot%s_o' % S), b0.z + c('foot%s_lift' % S))
            ankle = ball + q @ (a0 - b0)
            L = self.limb['leg' + s]
            hip = r.world['thigh_' + s].to_translation()
            outward = qh @ Vector((1 if s == 'l' else -1, 0, 0))
            knee, end, short, side = two_bone(hip, L['a'], L['b'], ankle, toe + outward * 0.25)
            self.shortfall.setdefault('leg' + s, []).append(short)
            self._limb(('thigh_' + s, 'calf_' + s), L, hip, knee, end, side)
            r.delta['foot_' + s] = q
            r.delta['ball_' + s] = yaw
        r.solve()
        return qc

    def hand_on_handle(self, s, grip, axis, face):
        """The hand's rotation and wrist position for its place on the handle: the grip is fixed for the stroke."""
        hd = self.hand[s]
        top = s == 'l'
        palm_t = Quaternion(axis, math.radians(strokes.GRIP_ROLL[s])) @ (-face if top else face)
        # The handle lies diagonally across the palm, from the heel of the hand (up the handle) to the index
        # finger (toward the blade), GRIP_DIAG off the knuckle line: the cocked wrist of a real grip.
        g = math.radians(GRIP_DIAG)
        toe = -hd['across'] * math.cos(g) + hd['out'] * math.sin(g)
        qhand = rot_between_frames(toe, hd['palm'], axis, palm_t)
        handle = grip - axis * GRIP_HALF if top else grip + axis * GRIP_HALF
        return qhand, handle - qhand @ hd['offset']

    def arm_cost(self, s, qc, shoulder, wrist, qhand, bend_dir, pole, spine):
        """How far one elbow placement is from a comfortable arm (arm_anatomy against COMFORT), plus its distance
        from the authored pole. Returns (cost, upper arm rotation, forearm rotation, inside every validator limit)."""
        L = self.limb['arm' + s]
        elbow, end, short, side = two_bone(shoulder, L['a'], L['b'], wrist, bend_dir)
        qu, qf = limb_rotations(L, shoulder, elbow, end, side)
        j = arm_anatomy(L, qc, spine, shoulder, elbow, end, qu, qf, qhand)
        cost = 2000 * short + 300 * max(1.0 - j['torso'], 0) + LIMIT_WEIGHT * 100 * max(LIMITS['torso'][0] - j['torso'], 0)
        for k, (lo, hi, w) in COMFORT.items():
            x = j[k]
            cost += w * abs(x) + 4 * max(lo - x, x - hi, 0)
            # Past the clinical limit the validator enforces, a pose costs as much as a hand off the handle.
            cost += LIMIT_WEIGHT * max(LIMITS[k][0] - x, x - LIMITS[k][1], 0) if k in LIMITS else 0
        d = (wrist - shoulder).normalized()
        pole_perp = pole - d * pole.dot(d)
        if pole_perp.length > 1e-6:
            cost += POLE_WEIGHT * math.degrees(side.angle(pole_perp))
        legal = short <= LIMITS['hand_off_handle'][1] and LIMITS['elbow_flex'][0] <= j['flex'] <= LIMITS['elbow_flex'][1] \
            and all(LIMITS[k][0] <= j[k] <= LIMITS[k][1] for k in ('twist', 'humeral', 'wflex', 'wdev', 'torso'))
        return cost, qu, qf, legal

    def plan_arms(self, contact_t=None):
        """For every frame, the bat's face roll away from its key and each elbow's swivel about the shoulder-wrist
        line, chosen together for the cheapest arm_cost with smooth motion: one dynamic program over the whole clip
        and all three at once, so the face can never roll where an arm would have to flip to follow it.
        Deterministic. At contact the face stays as keyed."""
        r = self.rig
        frames = list(range(1, self.last + 1))
        rolls = np.arange(-FACE_FREEDOM, FACE_FREEDOM + 1, 5)
        swivels = np.radians(np.arange(0, 360, 10))
        basis = {}  # each arm's swivel zero, carried frame to frame about the shoulder-wrist line so it cannot jump
        costs, bases, spin, legal = [], [], [], []  # spin: per arm, each swivel's bone rotations with the face as keyed
        for fr in frames:
            qc = self.pose_body(fr)
            grip, axis, face = self.bat(fr)
            spine = (r.world['spine_01'].to_translation(), r.world['neck_01'].to_translation())
            t = (fr - 1) / FPS
            w = 0.2 + (20.0 * math.exp(-((t - contact_t) / 0.05) ** 2) if contact_t is not None else 0.0)
            arm_cost, frame_bases, frame_spin, arm_legal = {}, {}, {}, {}
            for s in 'lr':
                S = s.upper()
                shoulder = r.world['upperarm_' + s].to_translation()
                d = (self.hand_on_handle(s, grip, axis, face)[1] - shoulder).normalized()
                e1 = basis.get(s, d.cross(Z if abs(d.z) < 0.95 else Vector((1, 0, 0))))
                e1 = (e1 - d * e1.dot(d)).normalized()
                basis[s] = e1
                e2 = d.cross(e1)
                pole = qc @ W(self.c('pole%s_f' % S, fr), self.c('pole%s_o' % S, fr), self.c('pole%s_z' % S, fr))
                frame_bases[s] = (e1, e2)
                rows = [[self.arm_cost(s, qc, shoulder, wrist, qhand, e1 * math.cos(p) + e2 * math.sin(p), pole, spine) for p in swivels]
                        for qhand, wrist in (self.hand_on_handle(s, grip, axis, Quaternion(axis, math.radians(dph)) @ face)
                                             for dph in rolls)]
                arm_cost[s] = np.array([[c[0] for c in row] for row in rows])
                arm_legal[s] = np.array([[c[3] for c in row] for row in rows])
                frame_spin[s] = [np.array([tuple(c[i]) for c in rows[len(rolls) // 2]]) for i in (1, 2)]
            costs.append(w * np.abs(rolls)[:, None, None] + arm_cost['l'][:, :, None] + arm_cost['r'][:, None, :])
            bases.append(frame_bases)
            spin.append(frame_spin)
            legal.append(arm_legal['l'][:, :, None] & arm_legal['r'][:, None, :])
        # A steady roll is cheap. A swivel change costs the square of how far the upper arm and forearm turn in
        # that frame (what the validator's bone_jump measures), so a flip (a pop no arm can make) costs more than
        # any pose, while a swivel that follows a fast-moving hand costs nothing.
        roll_step = 2.0 * np.abs(rolls[:, None] - rolls[None, :])

        def turn(k, s):
            deg = 0
            for a, b in zip(spin[k - 1][s], spin[k][s]):
                deg = deg + np.degrees(2 * np.arccos(np.clip(np.abs(a @ b.T), 0.0, 1.0))) ** 2
            return SWIVEL_RATE * deg

        # Where some motion keeps every frame inside the limits with no bone turning faster than the validator allows,
        # the plan is held to it; where none exists the keys themselves are at fault, and the report says where.
        steps = lambda k: (np.abs(rolls[:, None] - rolls[None, :]) <= ROLL_STEP_MAX, step_ok(k, 'l'), step_ok(k, 'r'))

        def step_ok(k, s):
            deg = [np.degrees(2 * np.arccos(np.clip(np.abs(a @ b.T), 0.0, 1.0))) for a, b in zip(spin[k - 1][s], spin[k][s])]
            return np.maximum(*deg) <= LIMITS['bone_jump'][1]
        on_path, self.illegal = legal_motion(legal, steps)
        for k, ok in enumerate(on_path):
            if ok.any():
                costs[k] = costs[k] + np.where(ok, 0.0, OFF_PATH)
        # Legal states alone are not enough: the step between two of them must be legal too.
        path = np.array(viterbi(costs, lambda k: tuple(t + np.where(ok, 0.0, OFF_PATH) for t, ok in
                                                       zip((roll_step, turn(k, 'l'), turn(k, 'r')), steps(k)))))
        # The grid's steps (5 deg of roll, 10 of swivel) would show as a stutter, a bone turning 10 deg one frame
        # and none the next: the chosen path is smoothed into a continuous one within about a step of it. Where the
        # smoothed motion breaks a limit the grid path kept, the smoothing is halved there until it does not.
        sigma = np.full(len(frames), SMOOTH_FRAMES)
        for _ in range(6):
            roll = smooth(rolls[path[:, 0]].astype(float), sigma)
            sw = {s: smooth(np.unwrap(swivels[path[:, i]]), sigma) for s, i in (('l', 1), ('r', 2))}
            self.plan = {fr: (roll[k], {s: bases[k][s][0] * math.cos(sw[s][k]) + bases[k][s][1] * math.sin(sw[s][k]) for s in 'lr'})
                         for k, fr in enumerate(frames)}
            bad = self.plan_faults()
            if not bad:
                break
            for k in bad:
                sigma[max(k - 3, 0):k + 4] *= 0.5
            sigma[sigma < 0.3] = 0.0

    def plan_faults(self):
        """Frames (0-based) where the plan puts an arm outside the validator's limits or turns a bone too fast."""
        r, bad, prev = self.rig, set(), None
        for fr, (dph, bends) in sorted(self.plan.items()):
            qc = self.pose_body(fr)
            grip, axis, face = self.bat(fr)
            face = Quaternion(axis, math.radians(dph)) @ face
            spine = (r.world['spine_01'].to_translation(), r.world['neck_01'].to_translation())
            cur = {}
            for s in 'lr':
                qhand, wrist = self.hand_on_handle(s, grip, axis, face)
                _, qu, qf, legal = self.arm_cost(s, qc, r.world['upperarm_' + s].to_translation(), wrist, qhand, bends[s],
                                                 Vector((0, 0, 0)), spine)
                cur[s] = (qu, qf)
                if not legal or prev and max(angle(a.rotation_difference(b)) for a, b in zip(prev[s], cur[s])) \
                        > LIMITS['bone_jump'][1]:
                    bad.add(fr - 1)
            prev = cur
        return sorted(bad)

    def pose(self, fr):
        r = self.rig
        qc = self.pose_body(fr)
        grip, axis, face = self.bat(fr)
        dph, bends = self.plan[fr]
        face = Quaternion(axis, math.radians(dph)) @ face
        for s in 'lr':
            hd = self.hand[s]
            qhand, wrist = self.hand_on_handle(s, grip, axis, face)
            shoulder = r.world['upperarm_' + s].to_translation()
            L = self.limb['arm' + s]
            elbow, end, short, side = two_bone(shoulder, L['a'], L['b'], wrist, bends[s])
            self.shortfall.setdefault('arm' + s, []).append(short)
            qu, qf = self._limb(('upperarm_' + s, 'lowerarm_' + s), L, shoulder, elbow, end, side)
            r.delta['hand_' + s] = qhand
            # The upper arm's own axial turn is half undone near the shoulder, as a deltoid does, so it cannot wrap.
            u = qu @ L['u0']
            tu = twist_angle(rot_between(L['u0'], u).inverted() @ qu, L['u0'])
            r.delta['upperarm_twist_01_' + s] = Quaternion(u, -tu * 0.5) @ qu
            r.delta['upperarm_twist_02_' + s] = Quaternion(u, -tu * 0.25) @ qu
            # The forearm's axial turn, measured against the forearm, goes to its twist bones.
            tw = twist_angle(qf.inverted() @ qhand, L['f0'])
            r.delta['lowerarm_twist_01_' + s] = Quaternion(qf @ L['f0'], tw * 2 / 3) @ qf
            r.delta['lowerarm_twist_02_' + s] = Quaternion(qf @ L['f0'], tw / 3) @ qf
            # Fingers closed round the handle, each about its own hinge.
            for fname, angles in CURL.items():
                tot = 0.0
                for k, a in enumerate(angles):
                    bn = '%s_%02d_%s' % (fname, k + 1, s)
                    if bn not in r.rest:
                        continue
                    nxt = '%s_%02d_%s' % (fname, k + 2, s)
                    d = (r.head0(nxt) - r.head0(bn)) if nxt in r.rest else (r.head0(bn) - r.head0('%s_%02d_%s' % (fname, k, s)))
                    axis_f = d.normalized().cross(hd['palm']).normalized()
                    tot += a
                    r.delta[bn] = qhand @ Quaternion(axis_f, math.radians(tot))
        r.solve()
        return grip, axis, face

    def _limb(self, bones, L, root, joint, end, side):
        qu, qf = limb_rotations(L, root, joint, end, side)
        self.rig.delta[bones[0]] = qu
        self.rig.delta[bones[1]] = qf
        return qu, qf

    def bake(self):
        arm = self.rig.arm
        self.frames, self.snap = [], []
        for fr in range(1, self.last + 1):
            g, a, f = self.pose(fr)
            self.rig.key(fr)
            self.frames.append((fr, g, a, f))
            self.snap.append({n: m.copy() for n, m in self.rig.world.items()})
        arm.animation_data.action.name = 'Bat_' + STROKE
        sc = bpy.context.scene
        sc.frame_start, sc.frame_end = 1, self.last
        sc.render.fps = FPS


def iter_fcurves(obj):
    act = obj.animation_data.action
    if hasattr(act, 'fcurves'):
        yield from act.fcurves
        return
    for layer in act.layers:  # Blender 4.4+ layered actions
        for strip in layer.strips:
            for bag in strip.channelbags:
                yield from bag.fcurves


def make_bat(stroke):
    """A plain bat and the contact ball for the renders, keyed from the baked frames."""
    bpy.ops.mesh.primitive_cube_add(size=1)
    blade = bpy.context.object
    blade.name = 'Bat'
    blade.scale = (0.108, 0.04, BAT_LENGTH - 0.29)
    bpy.ops.object.transform_apply(scale=True)
    blade.color = (0.85, 0.75, 0.5, 1)
    for fr, g, a, f in stroke.frames:
        up = -a
        m = frame(up, f)  # columns: up the handle, face, across
        centre = g + a * (0.29 - GRIP_FROM_TOP + 0.5 * (BAT_LENGTH - 0.29))
        rot = Matrix((m.col[2], m.col[1], m.col[0])).transposed()  # x across, y face, z up the handle
        blade.matrix_world = Matrix.Translation(centre) @ rot.to_4x4()
        blade.keyframe_insert('location', frame=fr)
        blade.keyframe_insert('rotation_euler', frame=fr)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.017, depth=0.29)
    handle = bpy.context.object
    handle.name = 'Handle'
    handle.color = (0.05, 0.05, 0.05, 1)
    for fr, g, a, f in stroke.frames:
        handle.matrix_world = Matrix.Translation(g + a * (0.145 - GRIP_FROM_TOP)) @ frame(-a, f).to_4x4() @ Matrix.Rotation(math.radians(90), 4, 'Y')
        handle.keyframe_insert('location', frame=fr)
        handle.keyframe_insert('rotation_euler', frame=fr)
    cf = strokes.CONTACT.get(STROKE)
    if cf is not None:
        fr = 1 + round(cf * FPS)
        g, a = stroke.frames[fr - 1][1], stroke.frames[fr - 1][2]
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.036, location=g + a * SWEET_FROM_GRIP)
        bpy.context.object.color = (0.8, 0.05, 0.05, 1)
        bpy.context.object.name = 'Ball'


if __name__ == '__main__':
    argv = sys.argv[sys.argv.index('--') + 1:]
    STROKE, RIG, OUT = argv[0], argv[1], argv[2]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=RIG)
    arm = next(o for o in bpy.context.scene.objects if o.type == 'ARMATURE')
    st = Stroke(arm, strokes.KEYS[STROKE])
    st.build_controls()
    st.plan_arms(strokes.CONTACT.get(STROKE))
    for a, b, why in st.illegal:
        print('ILLEGAL frames %d-%d (%.2f-%.2f s): %s for the arms; re-key there' % (a + 1, b + 1, a / FPS, b / FPS, why))
    st.shortfall = {}
    st.bake()
    for k, v in st.shortfall.items():
        print('REACH %s max shortfall %.1f cm' % (k, 100 * max(v)))
    make_bat(st)
    # The kit and gear beside the rig (the body is cut away under them), driven by the same armature, for review.
    for extra in ('Kit.fbx', 'Gear.fbx'):
        path = os.path.join(os.path.dirname(RIG), extra)
        if not os.path.exists(path):
            continue
        before = set(bpy.data.objects)
        bpy.ops.import_scene.fbx(filepath=path)
        for o in set(bpy.data.objects) - before:
            if o.type == 'MESH':
                for m in o.modifiers:
                    if m.type == 'ARMATURE':
                        m.object = arm
                mw = o.matrix_world.copy()
                o.parent = arm
                o.matrix_world = mw
        for o in set(bpy.data.objects) - before:
            if o.type != 'MESH':
                bpy.data.objects.remove(o)
    # Importing an FBX sets the scene's frame rate to the file's; the stroke's own rate times the export.
    bpy.context.scene.render.fps, bpy.context.scene.render.fps_base = FPS, 1.0
    os.makedirs(OUT, exist_ok=True)
    import validate_stroke  # noqa: E402
    validate_stroke.run(st, strokes.CONTACT.get(STROKE))
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, 'Bat_%s.blend' % STROKE))
    # The action alone, on the armature, for Unreal.
    for o in bpy.context.scene.objects:
        o.select_set(o.type == 'ARMATURE')
    bpy.context.view_layer.objects.active = arm
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, 'Bat_%s.fbx' % STROKE), use_selection=True, object_types={'ARMATURE'},
                             add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
                             bake_anim_force_startend_keying=True, bake_anim_simplify_factor=0.0, armature_nodetype='NULL')
    print('WROTE', os.path.join(OUT, 'Bat_%s.fbx' % STROKE))

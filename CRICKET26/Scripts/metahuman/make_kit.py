# Blender script: makes a player's cricket kit (collared shirt, trousers, shoes) and batting gear (pads, gloves, helmet)
# from his own full body.
#
# Each piece is cut from the body exported without its garment (Scripts/metahuman/make_kit.sh builds and exports
# it), smoothed so the fabric does not show the muscles under it, and pushed out from the skin. Every vertex keeps
# the body's skin weights, so the kit follows the body through any pose with no simulation, and fits each body shape.
# The pieces carry the materials Kit_Shirt, Kit_Trousers and Kit_Shoes, which the game paints in team colours. The
# gear (worn only by the batters) is a second mesh with Gear_Pads, Gear_Gloves, Gear_Helmet and Gear_Grille; the
# helmet is rigid on the head bone. The keeper's gear is a third, with lighter pads and big webbed gloves, and the
# umpire's wide-brimmed hat (Gear_Hat) a fourth.
#
# All of it is generated here from the MetaHuman body, an original design, except the helmet when HELMET_GLB names
# "Cricket Helmet" by Helindu (Sketchfab, CC-BY 4.0), which must then be credited.
#
# Run: Blender -b --python make_kit.py -- BODY.fbx KIT.fbx GEAR.fbx KEEPER.fbx HAT.fbx
import os
import sys
import bmesh
import bpy
import math
from mathutils import Matrix, Vector

BODY, KIT, GEAR, KEEPER, HAT = sys.argv[sys.argv.index("--") + 1:][:5]
FACE = (sys.argv[sys.argv.index("--") + 1:][5:] or [""])[0]  # the face mesh (KIT_STEP=face), to fit the hat to the scalp

# Offsets from the skin in cm. Outer layers sit further out where they overlap: the shirt hem over the trousers,
# the trouser hem over the shoes.
SHIRT, TROUSERS, SHOES = 1.8, 1.4, 1.2
SLEEVE = 0.55        # sleeve length as a fraction of shoulder to elbow
FLARE = 1.2          # extra trouser width at the ankle, for a straight leg instead of a skin-tight one
COLLAR = 2.5         # collar height in cm
THICKNESS = 0.35     # fabric thickness, seen at the hems

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=BODY)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
body = next(o for o in bpy.data.objects if o.type == 'MESH')
to_local = body.matrix_world.inverted() @ arm.matrix_world


def joint(name):
    return to_local @ arm.data.bones[name].head_local


pelvis, waist, chest, neck_joint = joint("pelvis"), joint("spine_02"), joint("spine_04"), joint("neck_01")
foot = {s: joint(f"foot_{s}") for s in "lr"}
knee = {s: joint(f"calf_{s}") for s in "lr"}
shoulder = {s: joint(f"upperarm_{s}") for s in "lr"}
elbow = {s: joint(f"lowerarm_{s}") for s in "lr"}
ankle_z = max(f.z for f in foot.values())
knee_z = sum(k.z for k in knee.values()) / 2

groups = {g.index: g.name for g in body.vertex_groups}
ARM_PARTS = ("upperarm", "lowerarm", "hand", "wrist", "elbow", "thumb", "index", "middle", "ring", "pinky")


def part_of(v, parts):
    """The vertex's weight on bones named for any of parts. The toes are named for fingers (indextoe_01_l,
    ringtoe_01_l), so they are left out."""
    for g in v.groups:
        n = groups.get(g.group, "")
        if n.startswith(parts) and "toe" not in n:
            yield n, g.weight


def arm_weight(v):
    """How much of the vertex follows an arm: (weight, side)."""
    w = {"l": 0.0, "r": 0.0}
    for n, weight in part_of(v, ARM_PARTS):
        w[n[-1]] = w.get(n[-1], 0.0) + weight
    side = max(w, key=w.get)
    return w[side], side


def along(p, a, b):
    d = b - a
    return (p - a).dot(d) / d.length_squared


def region(v):
    w, side = arm_weight(v)
    if w > 0.5:
        return "shirt" if along(v.co, shoulder[side], elbow[side]) < SLEEVE else None
    z = v.co.z
    if z < ankle_z + 5.0:
        return "shoes"
    if z < waist.z:
        return "trousers"
    return "shirt"


def piece(kind, keep, offset, smooth):
    o = body.copy()
    o.data = body.data.copy()
    o.name = f"Kit_{kind}"
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not keep(v.index)], context='VERTS')
    # The body arrives split along its UV seams; welded, the seams smooth and push out with the rest, not open as cracks.
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.01)
    bm.normal_update()
    skin = {v: (v.co.copy(), v.normal.copy()) for v in bm.verts}
    inner = [v for v in bm.verts if not v.is_boundary]
    for _ in range(smooth):
        bmesh.ops.smooth_vert(bm, verts=inner, factor=0.5, use_axis_x=True, use_axis_y=True, use_axis_z=True)
    bm.normal_update()
    for v in bm.verts:
        v.co += v.normal * offset(v.co)
    # Smoothing pulls the fabric into hollows (inner thighs, toes), so push any vertex that ended up closer to its
    # skin point than the offset back out along the skin's normal.
    for v, (at, n) in skin.items():
        gap = (v.co - at).dot(n)
        if gap < offset(at):
            v.co += n * (offset(at) - gap)
    return o, bm


def finish(o, bm, material, thickness=THICKNESS):
    bm.to_mesh(o.data)
    bm.free()
    if thickness:
        # A plain offset along the vertex normals. bmesh's solidify evens the thickness out by the angle at each
        # vertex, and where the surface folds back on itself (between the fingers) that throws vertices metres out.
        m = o.modifiers.new("Solidify", 'SOLIDIFY')
        m.thickness, m.offset, m.use_even_offset = thickness, -1.0, False
        with bpy.context.temp_override(object=o, active_object=o):
            bpy.ops.object.modifier_apply(modifier=m.name)
    o.data.materials.clear()
    # Reused by name: a second new one would come out as "Gear_Pads.001", a slot the game does not know.
    o.data.materials.append(bpy.data.materials.get(material) or bpy.data.materials.new(material))
    for p in o.data.polygons:
        p.material_index = 0
    return o


regions = [region(v) for v in body.data.vertices]
# The trousers run down over the top of the shoes and up under the shirt hem; the shirt hangs to the hips.
trouser_keep = lambda i: (regions[i] == "trousers" or (regions[i] == "shoes" and body.data.vertices[i].co.z > ankle_z + 1.0)
                          or (regions[i] == "shirt" and body.data.vertices[i].co.z < waist.z + 4.0 and arm_weight(body.data.vertices[i])[0] < 0.5))
shirt_keep = lambda i: regions[i] == "shirt" or (regions[i] == "trousers" and body.data.vertices[i].co.z > pelvis.z - 4.0)
shoe_keep = lambda i: regions[i] == "shoes"

o, bm = piece("Shirt", shirt_keep, lambda p: SHIRT, 12)
# The collar: the neckline (the opening round the neck joint) turned up, hugging the neck.
flat = lambda v: Vector((v.co.x - neck_joint.x, v.co.y - neck_joint.y, 0.0))
collarbone = joint("clavicle_l").z
neck = [e for e in bm.edges if e.is_boundary and all(v.co.z > collarbone - 4.0 and flat(v).length < 20.0 for v in e.verts)]
if neck:
    made = bmesh.ops.extrude_edge_only(bm, edges=neck)
    for v in (g for g in made["geom"] if isinstance(g, bmesh.types.BMVert)):
        v.co += Vector((0.0, 0.0, COLLAR)) - flat(v).normalized() * 0.4
shirt = finish(o, bm, "Kit_Shirt")


def trouser_offset(p):
    return TROUSERS + FLARE * min(max((knee_z - p.z) / (knee_z - ankle_z), 0.0), 1.0)


trousers = finish(*piece("Trousers", trouser_keep, trouser_offset, 8), "Kit_Trousers")
# A shoe is the convex hull of the foot, pushed out: a smooth closed shape over the toes. The hull reuses the foot's
# own vertices, so it keeps their skin weights.
o, bm = piece("Shoes", shoe_keep, lambda p: 0.0, 0)
bmesh.ops.delete(bm, geom=bm.edges[:], context='EDGES_FACES')
for side in (1, -1):
    hull = bmesh.ops.convex_hull(bm, input=[v for v in bm.verts if (v.co.x - pelvis.x) * side > 0 and not v.link_faces])
    bmesh.ops.delete(bm, geom=list({g for g in hull["geom_interior"] + hull["geom_unused"] if isinstance(g, bmesh.types.BMVert)}), context='VERTS')
bm.normal_update()
for v in bm.verts:
    v.co += v.normal * SHOES
shoes = finish(o, bm, "Kit_Shoes")



def export(parts, name, path):
    for o in parts:
        print(f"KIT {o.name}: {len(o.data.vertices)} vertices")
    with bpy.context.temp_override(active_object=parts[0], selected_editable_objects=parts):
        bpy.ops.object.join()
    parts[0].name = name
    bpy.ops.object.select_all(action='DESELECT')
    parts[0].select_set(True)
    arm.select_set(True)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                             bake_anim=False, use_mesh_modifiers=False)
    bpy.data.objects.remove(parts[0])
    print(f"KIT written {path}")


export([shirt, trousers, shoes], "Kit", KIT)

# Batting pads, modelled round each leg rather than cut from it: a grid over the front of the leg, from the top of the
# shoe to the thigh, following the leg's measured width and depth. Seven rounded vertical canes run down the shin, a knee
# roll of three horizontal bolsters crosses the knee, narrower canes run up the thigh, and flat wings wrap the sides,
# with grooves between them all. Straps round the back of the leg hold it on. Below the knee the pad follows the calf
# bone, above it the thigh bone, blended across the knee.
FORWARD = Vector((0.0, -1.0, 0.0))
thigh = {s: joint(f"thigh_{s}") for s in "lr"}
KNEE_ROLL = (-5.0, 6.0)          # the knee roll's span about the knee joint, cm
PAD_TOP = 20.0                   # the pad's top above the knee joint, cm
SHIN_SPAN, THIGH_SPAN = 118, 80  # the pad's half-width round the leg from straight ahead, degrees
WING = 92                        # beyond this the shin section is a flat wing, degrees
STRAPS = (-30.0, -15.0, 13.0)    # heights of the straps' centres about the knee joint, cm
STRAP_WIDTH = 3.5


def leg_axis(s, z):
    a, b = (foot[s], knee[s]) if z < knee[s].z else (knee[s], thigh[s])
    return a.lerp(b, min(max((z - a.z) / (b.z - a.z), 0.0), 1.0))


def leg_shape(s):
    """The leg's half-width across, and its front and back from the axis, per cm of height (smoothed), in cm."""
    rows = {}
    for v in body.data.vertices:
        p = v.co
        if arm_weight(v)[0] > 0.5 or (p.x - pelvis.x) * (foot[s].x - pelvis.x) <= 0:
            continue
        d = p - leg_axis(s, p.z)
        r = rows.setdefault(round(p.z), [0.0, 0.0, 0.0])
        r[0], r[1], r[2] = max(r[0], abs(d.x)), max(r[1], -d.y), max(r[2], d.y)
    # From just above the ankle: lower down the heel and toes would drag the pad's edge out. Below it the pad keeps
    # the lowest measured shape.
    lo, hi = round(ankle_z + 3.0), round(knee[s].z + PAD_TOP + 2.0)
    raw = [rows.get(z, [6.0, 6.0, 6.0]) for z in range(lo, hi + 1)]
    return lo, [[sum(raw[j][k] for j in range(max(0, i - 5), min(len(raw), i + 6))) / len(raw[max(0, i - 5):i + 6])
                 for k in range(3)] for i in range(len(raw))]


def around(s, z, deg, out):
    """The point `out` cm outside the leg at height z, `deg` degrees round from straight ahead."""
    lo, shape = legs[s]
    # Between the measured heights, blended, so the pad has no steps.
    i = min(max(z - lo, 0.0), len(shape) - 1.001)
    t = i - int(i)
    w, front, back = (a + (b - a) * t for a, b in zip(shape[int(i)], shape[int(i) + 1]))
    a = math.radians(deg) * (1 if s == "l" else -1)
    depth = front if math.cos(a) > 0 else back
    return leg_axis(s, z) + Vector(((w + out) * math.sin(a), -(depth + out) * math.cos(a), 0.0))


def hump(t):
    """0 at whole t, 1 halfway between: a round bolster with a narrow groove each side. The groove's walls are steep
    but not vertical, so the pad's thickness does not fold through itself there."""
    return 1.0 - (1.0 - abs(math.sin(t * math.pi))) ** 2


def bone_weights(bm, o, s):
    layer = bm.verts.layers.deform.verify()
    calf = (o.vertex_groups.get(f"calf_{s}") or o.vertex_groups.new(name=f"calf_{s}")).index
    upper = (o.vertex_groups.get(f"thigh_{s}") or o.vertex_groups.new(name=f"thigh_{s}")).index
    for v in bm.verts:
        w = min(max((v.co.z - knee[s].z + 3.0) / 6.0, 0.0), 1.0)
        v[layer].clear()
        v[layer][calf], v[layer][upper] = 1.0 - w, w


def outward(bm, s):
    """Turns the faces out from the leg. The right leg's grid is the left's mirrored, so it winds the other way, and
    solidify would thicken it outwards, through the pad's own grooves."""
    bm.faces.ensure_lookup_table()
    f = bm.faces[len(bm.faces) // 2]
    c = f.calc_center_median()
    if f.normal.dot(c - leg_axis(s, c.z)) < 0.0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces[:])


def grid(bm, rows, cols, point, keep):
    """A quad grid of point(i, j) over rows x cols, with faces only where keep(i, j) holds at all four corners."""
    vs = [[bm.verts.new(point(i, j)) for j in range(cols)] for i in range(rows)]
    uv = bm.loops.layers.uv.verify()
    for i in range(rows - 1):
        for j in range(cols - 1):
            if all(keep(i + a, j + b) for a, b in ((0, 0), (0, 1), (1, 1), (1, 0))):
                f = bm.faces.new((vs[i][j], vs[i][j + 1], vs[i + 1][j + 1], vs[i + 1][j]))
                f.smooth = True
                for loop, (a, b) in zip(f.loops, ((0, 0), (0, 1), (1, 1), (1, 0))):
                    loop[uv].uv = ((j + b) / (cols - 1), (i + a) / (rows - 1) * 2.0)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.normal_update()


legs = {s: leg_shape(s) for s in "lr"}


def span(s, z):
    """The pad's half-width round the leg at height z, in degrees: the shin's to the knee roll's top, in to the thigh's
    over 3 cm, then tapering to the top."""
    above = z - knee[s].z - KNEE_ROLL[1]
    if above <= 0.0:
        return SHIN_SPAN
    if above < 3.0:
        return SHIN_SPAN + (THIGH_SPAN - SHIN_SPAN) * above / 3.0
    return THIGH_SPAN * (1.0 - 0.25 * (above - 3.0) / (PAD_TOP - KNEE_ROLL[1] - 3.0))


def pad(name, s, slim):
    o = body.copy()
    o.data = bpy.data.meshes.new(name)
    o.name = name
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new()
    k = knee[s].z
    bottom, top = ankle_z - 1.0, k + PAD_TOP
    rows, cols = int(top - bottom) * 2 + 1, 73

    def offset(z, u):
        above = z - k
        d = SHIN_SPAN * u
        if KNEE_ROLL[0] < above < KNEE_ROLL[1]:
            # The roll flattens into the wings' edge over the last 20 degrees, so it does not stand off the side.
            side = min(1.0, (SHIN_SPAN - abs(d)) / 20.0)
            return 2.2 + (0.8 + 1.0 * hump((above - KNEE_ROLL[0]) / (KNEE_ROLL[1] - KNEE_ROLL[0]) * 3)) * side
        # A groove where the canes meet the knee roll.
        edge = min(1.0, min(abs(above - KNEE_ROLL[0]), abs(above - KNEE_ROLL[1])) / 1.5)
        if above > 0:
            return 2.6 + 1.1 * hump((u + 1.0) / 2.0 * 5) * edge
        if abs(d) > WING:
            return 2.2
        return 2.6 + 1.1 * hump((d + WING) / (2 * WING) * 7) * edge

    def point(i, j):
        # Each column runs from the bottom edge, which curves up at the sides off the shoe, to the top, and each row
        # spans the pad's width at its height, so the outline is smooth rather than cut from the grid in steps.
        u = 2.0 * j / (cols - 1) - 1.0
        low = bottom + 4.0 * u * u
        z = low + (top - low) * i / (rows - 1)
        d = span(s, z) * u
        return around(s, z, d, offset(z, u) - slim)

    keep = lambda i, j: True
    grid(bm, rows, cols, point, keep)
    outward(bm, s)
    bone_weights(bm, o, s)
    return o, bm


def strap(name, s):
    o = body.copy()
    o.data = bpy.data.meshes.new(name)
    o.name = name
    bpy.context.scene.collection.objects.link(o)
    bm = bmesh.new()
    for h in STRAPS:
        z0 = knee[s].z + h - STRAP_WIDTH / 2
        # Round the back of the leg from one edge of the pad to the other, level with it there and standing off the
        # calf behind.
        a = span(s, z0 + STRAP_WIDTH / 2)
        grid(bm, 4, 41, lambda i, j: around(s, z0 + STRAP_WIDTH * i / 3, a + (360 - 2 * a) * j / 40,
                                            TROUSERS + 0.8 + 0.6 * math.sin(math.pi * j / 40)),
             lambda i, j: True)
    outward(bm, s)
    bone_weights(bm, o, s)
    return o, bm


def spread(o, s):
    """The mean distance of o's vertices from the leg's axis, in cm."""
    return sum((v.co - leg_axis(s, v.co.z)).xy.length for v in o.data.vertices) / len(o.data.vertices)


def both(make, material, thickness, *args):
    parts = [finish(*make(f"{material}_{s}", s, *args), material, thickness) for s in "lr"]
    # The legs' pieces are mirror images, so they stand off their legs alike. A gap means one was thickened the
    # wrong way (see outward).
    l, r = (spread(p, s) for p, s in zip(parts, "lr"))
    print(f"KIT {material}: {l:.2f} cm off the left leg, {r:.2f} off the right")
    assert abs(l - r) < 0.5, f"{material} thickened unevenly"
    with bpy.context.temp_override(active_object=parts[0], selected_editable_objects=parts):
        bpy.ops.object.join()
    return parts[0]


pads = both(pad, "Gear_Pads", 1.2, -2.0)
straps = both(strap, "Gear_Straps", 0.4)

# Batting gloves: the hands padded out, with a gauntlet over the wrist.
HAND_PARTS = ("hand", "wrist", "thumb", "index", "middle", "ring", "pinky")
hand = {s: joint(f"hand_{s}") for s in "lr"}


def hand_weight(v):
    return sum(weight for _, weight in part_of(v, HAND_PARTS))


def glove_keep(i):
    v = body.data.vertices[i]
    w, side = arm_weight(v)
    return w > 0.5 and (hand_weight(v) > 0.5 or (v.co - hand[side]).length < 8.0)


# Sausage rolls down the back of each finger and thumb, ROLL cm long.
ROLL = 1.4


def finger(f, s):
    """The finger's joints, knuckle to tip."""
    b = [arm.data.bones[f"{f}_0{i}_{s}"] for i in (1, 2, 3)]
    return [to_local @ x.head_local for x in b] + [to_local @ b[-1].tail_local]


fingers = [finger(f, s) for f in ("thumb", "index", "middle", "ring", "pinky") for s in "lr"]


def finger_roll(p):
    """How far the finger rolls raise the glove at p: nothing more than 2.5 cm off a finger's bones."""
    near, at = 2.5, None
    for chain in fingers:
        run = 0.0
        for a, b in zip(chain, chain[1:]):
            t = min(max((p - a).dot(b - a) / (b - a).length_squared, 0.0), 1.0)
            d = (p - a.lerp(b, t)).length
            if d < near:
                near, at = d, run + t * (b - a).length
            run += (b - a).length
    return 0.0 if at is None else 0.35 * hump(at / ROLL)


def glove_offset(p):
    side = "l" if (p - hand["l"]).length < (p - hand["r"]).length else "r"
    return 0.8 + (0.7 if along(p, hand[side], elbow[side]) > 0.02 else 0.0)


o, bm = piece("Gloves", glove_keep, glove_offset, 2)
# The body's fingers are too coarse for rolls this short, so the glove is split finer first.
bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=1, use_grid_fill=True)
bm.normal_update()
for v in bm.verts:
    v.co += v.normal * finger_roll(v.co)
gloves = finish(o, bm, "Gear_Gloves", 0.5)
far = max(min((v.co - hand[s]).length for s in "lr") for v in gloves.data.vertices)
assert far < 35.0, f"Gloves reach {far:.0f} cm from the hands"
# The keeper's: slimmer pads, and gloves twice as padded, smoothed into mitts that web the thumb to the fingers.
# The fitted parametric trousers sit farther from the skin than the bare body used to measure the pads.
keeper_pads = both(pad, "Gear_Pads", 1.0, -2.0)
keeper_gloves = finish(*piece("KeeperGloves", glove_keep, lambda p: glove_offset(p) + 1.0, 8), "Gear_Gloves", 0.6)


def rigid(name, build):
    """A mesh made by build(bm, centre) that moves with the head bone."""
    o = body.copy()
    o.data = bpy.data.meshes.new(name)
    o.name = name
    bpy.context.scene.collection.objects.link(o)
    group = (o.vertex_groups.get("head") or o.vertex_groups.new(name="head")).index
    bm = bmesh.new()
    build(bm)
    layer = bm.verts.layers.deform.verify()
    for v in bm.verts:
        v[layer][group] = 1.0
    return o, bm


# The helmet: a shell over the skull, open at the face, and a grille of bars in front of it.
head = joint("head")
CENTRE, RADII = head + Vector((0.0, -2.0, 9.5)), Vector((11.0, 12.5, 12.0))


def shell(bm):
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=20, radius=1.0)
    for v in bm.verts:
        v.co = CENTRE + Vector((v.co.x * RADII.x, v.co.y * RADII.y, v.co.z * RADII.z))
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z - CENTRE.z < -8.0
                               or (v.co.y - CENTRE.y < -0.3 * RADII.y and v.co.z - CENTRE.z < 1.5)], context='VERTS')
    bm.normal_update()


def bar(bm, a, b, thick=0.45):
    d = b - a
    m = Matrix.LocRotScale((a + b) / 2, d.to_track_quat('X', 'Z'), (d.length + thick, thick, thick))
    bmesh.ops.create_cube(bm, size=1.0, matrix=m)


def grille(bm):
    at = lambda deg, dz: CENTRE + Vector((math.sin(math.radians(deg)) * (RADII.x + 1.5),
                                         -math.cos(math.radians(deg)) * (RADII.y + 1.5), dz))
    for dz in (0.5, -5.0, -10.5, -15.0):
        span = 70 if dz > -12 else 45
        for deg in range(-span, span, 10):
            bar(bm, at(deg, dz), at(deg + 10, dz))
    for deg in (-35, 0, 35):
        bar(bm, at(deg, 0.5), at(deg, -15.0 if abs(deg) < 40 else -10.5))
    # The side struts joining the grille to the shell.
    for deg in (-70, 70):
        bar(bm, at(deg, 0.5), at(deg, -10.5))


def scalp(base):
    """The face mesh's vertices above height base, in the body's space; none without the face."""
    if not os.path.isfile(FACE):
        return []
    old = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=FACE)
    new = [o for o in bpy.data.objects if o not in old]
    points = [body.matrix_world.inverted() @ o.matrix_world @ v.co for o in new if o.type == 'MESH' for v in o.data.vertices]
    for o in new:
        bpy.data.objects.remove(o)
    return [p for p in points if p.z > base]


def hat(bm):
    """The umpire's sun hat: a crown over the scalp and a wide brim sloping down a little all round."""
    top = CENTRE + Vector((0.0, 1.0, -1.0))
    # 12 cm tall at least: at 9 the MetaHuman scalp stood 8-9.5 cm above the crown's base and poked through the dome.
    # With the face mesh the crown grows until the whole scalp is 1 cm inside it (a bigger head needs a bigger hat).
    radii = Vector((10.0, 11.5, 12.0))
    inside = [math.sqrt(sum(((p - top)[i] / radii[i]) ** 2 for i in range(3))) for p in scalp(top.z)]
    radii *= max([1.0] + [e + 1.0 / min(radii) for e in inside])
    print(f"KIT hat crown {radii.x:.1f} x {radii.y:.1f} x {radii.z:.1f} cm over {len(inside)} scalp points")
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=12, radius=1.0)
    for v in bm.verts:
        v.co = top + Vector((v.co.x * radii.x, v.co.y * radii.y, v.co.z * radii.z))
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < top.z - 0.01], context='VERTS')
    rim = sorted((v for v in bm.verts if v.is_boundary), key=lambda v: math.atan2(v.co.y - top.y, v.co.x - top.x))
    outer = [bm.verts.new(top + Vector(((v.co.x - top.x) * 1.45, (v.co.y - top.y) * 1.4, -1.8))) for v in rim]
    for i in range(len(rim)):
        j = (i + 1) % len(rim)
        bm.faces.new((rim[i], rim[j], outer[j], outer[i]))
    bm.normal_update()


# The helmet: "Cricket Helmet" by Helindu (Sketchfab, CC-BY 4.0) when HELMET_GLB names its .glb, thinned to game
# detail and fitted where the shell above sits; else that shell and grille.
HELMET_GLB = os.environ.get("HELMET_GLB", "")
SCAN_FACES = 8000  # most faces kept per part: the model's grille bars are 100k-face tubes


def scanned():
    """The helmet model's shell (with its ear guards) and its grille (with the straps), as bmesh builders."""
    old = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=HELMET_GLB)
    new = [o for o in bpy.data.objects if o not in old]
    parts = [o for o in new if o.type == 'MESH']
    is_shell = lambda o: o.data.materials[0].name != "Material.001"  # the grey steel of the grille and straps
    # The model faces +x, in metres. Its shell scales to the made shell's width with a little room, its back and top
    # against the made shell's, turned to face -y as the body does.
    corners = [o.matrix_world @ Vector(c) for o in parts if o.data.materials[0].name == "Material" for c in o.bound_box]
    lo = Vector(tuple(min(c[i] for c in corners) for i in range(3)))
    hi = Vector(tuple(max(c[i] for c in corners) for i in range(3)))
    back_top = CENTRE + Vector((0.0, RADII.y + 1.0, RADII.z + 1.0))
    fit = (Matrix.Translation(back_top) @ Matrix.Rotation(-math.pi / 2, 4, 'Z')
           @ Matrix.Scale(2 * (RADII.x + 1.5) / (hi.y - lo.y), 4) @ Matrix.Translation(-Vector((lo.x, (lo.y + hi.y) / 2, hi.z))))
    for o in parts:
        o.modifiers.new("thin", 'DECIMATE').ratio = min(1.0, SCAN_FACES / len(o.data.polygons))
    depsgraph = bpy.context.evaluated_depsgraph_get()

    def build(shell):
        def fill(bm):
            for o in parts:
                if is_shell(o) == shell:
                    e = o.evaluated_get(depsgraph)
                    me = e.to_mesh()
                    me.transform(fit @ o.matrix_world)
                    bm.from_mesh(me)
                    e.to_mesh_clear()
            bm.normal_update()
        return fill
    shell_bm, grille_bm = rigid("Helmet", build(True)), rigid("Grille", build(False))
    for o in new:
        bpy.data.objects.remove(o)
    return shell_bm, grille_bm


if os.path.isfile(HELMET_GLB):
    made_shell, made_grille = scanned()
    helmet, bars = finish(*made_shell, "Gear_Helmet", 0.0), finish(*made_grille, "Gear_Grille", 0.0)
else:
    helmet = finish(*rigid("Helmet", shell), "Gear_Helmet", 1.0)
    bars = finish(*rigid("Grille", grille), "Gear_Grille", 0.0)
sunhat = finish(*rigid("Hat", hat), "Gear_Hat", 0.4)
bpy.data.objects.remove(body)
export([pads, straps, gloves, helmet, bars], "Gear", GEAR)
export([keeper_pads, keeper_gloves], "Keeper", KEEPER)
export([sunhat], "Hat", HAT)

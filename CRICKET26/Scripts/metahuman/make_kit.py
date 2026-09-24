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
# All of it is generated here from the MetaHuman body: an original design, no third-party asset.
#
# Run: Blender -b --python make_kit.py -- BODY.fbx KIT.fbx GEAR.fbx KEEPER.fbx HAT.fbx
import sys
import bmesh
import bpy
import math
from mathutils import Matrix, Vector

BODY, KIT, GEAR, KEEPER, HAT = sys.argv[sys.argv.index("--") + 1:][:5]

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


def arm_weight(v):
    """How much of the vertex follows an arm: (weight, side)."""
    w = {"l": 0.0, "r": 0.0}
    for g in v.groups:
        n = groups.get(g.group, "")
        if n.startswith(ARM_PARTS):
            w[n[-1]] = w.get(n[-1], 0.0) + g.weight
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
    if thickness:
        bmesh.ops.solidify(bm, geom=bm.faces[:], thickness=thickness)
    bm.to_mesh(o.data)
    bm.free()
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

# Batting pads: the front two thirds of each shin from the shoe to above the knee, pushed well out, with raised canes.
FORWARD = Vector((0.0, -1.0, 0.0))
thigh = {s: joint(f"thigh_{s}") for s in "lr"}
pad_top = knee_z + 0.3 * (sum(t.z for t in thigh.values()) / 2 - knee_z)


def leg_side(p):
    return "l" if (p.x - pelvis.x) * (foot["l"].x - pelvis.x) > 0 else "r"


def round_leg(p):
    """Horizontal direction from the leg's axis to p."""
    s = leg_side(p)
    a, b = (foot[s], knee[s]) if p.z < knee[s].z else (knee[s], thigh[s])
    d = p - a.lerp(b, min(max((p.z - a.z) / (b.z - a.z), 0.0), 1.0))
    d.z = 0.0
    return d.normalized() if d.length else FORWARD


def pad_keep(i):
    v = body.data.vertices[i]
    return (arm_weight(v)[0] < 0.5 and ankle_z + 2.0 < v.co.z < pad_top and round_leg(v.co).dot(FORWARD) > -0.35)


def pad_offset(p):
    d = round_leg(p)
    angle = math.atan2(d.cross(FORWARD).z, d.dot(FORWARD))
    return 3.2 + 0.6 * (0.5 + 0.5 * math.cos(angle * 10.0))


pads = finish(*piece("Pads", pad_keep, pad_offset, 20), "Gear_Pads", 1.2)

# Batting gloves: the hands padded out, with a gauntlet over the wrist.
HAND_PARTS = ("hand", "wrist", "thumb", "index", "middle", "ring", "pinky")
hand = {s: joint(f"hand_{s}") for s in "lr"}


def hand_weight(v):
    return sum(g.weight for g in v.groups if groups.get(g.group, "").startswith(HAND_PARTS))


def glove_keep(i):
    v = body.data.vertices[i]
    w, side = arm_weight(v)
    return w > 0.5 and (hand_weight(v) > 0.5 or (v.co - hand[side]).length < 8.0)


def glove_offset(p):
    side = "l" if (p - hand["l"]).length < (p - hand["r"]).length else "r"
    return 0.8 + (0.7 if along(p, hand[side], elbow[side]) > 0.02 else 0.0)


gloves = finish(*piece("Gloves", glove_keep, glove_offset, 2), "Gear_Gloves", 0.5)
# The keeper's: slimmer pads, and gloves twice as padded, smoothed into mitts that web the thumb to the fingers.
keeper_pads = finish(*piece("KeeperPads", pad_keep, lambda p: pad_offset(p) - 1.2, 20), "Gear_Pads", 1.0)
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


def hat(bm):
    """The umpire's sun hat: a low crown and a wide brim sloping down a little all round."""
    top = CENTRE + Vector((0.0, 1.0, -1.0))
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=12, radius=1.0)
    for v in bm.verts:
        v.co = top + Vector((v.co.x * 10.0, v.co.y * 11.5, v.co.z * 9.0))
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < top.z - 0.01], context='VERTS')
    rim = sorted((v for v in bm.verts if v.is_boundary), key=lambda v: math.atan2(v.co.y - top.y, v.co.x - top.x))
    outer = [bm.verts.new(top + Vector(((v.co.x - top.x) * 1.65, (v.co.y - top.y) * 1.6, -2.5))) for v in rim]
    for i in range(len(rim)):
        j = (i + 1) % len(rim)
        bm.faces.new((rim[i], rim[j], outer[j], outer[i]))
    bm.normal_update()


helmet = finish(*rigid("Helmet", shell), "Gear_Helmet", 1.0)
sunhat = finish(*rigid("Hat", hat), "Gear_Hat", 0.4)
bars = finish(*rigid("Grille", grille), "Gear_Grille", 0.0)
bpy.data.objects.remove(body)
export([pads, gloves, helmet, bars], "Gear", GEAR)
export([keeper_pads, keeper_gloves], "Keeper", KEEPER)
export([sunhat], "Hat", HAT)

# Blender script: makes a player's cricket kit (collared shirt, trousers, shoes) from his own full body.
#
# Each piece is cut from the body exported without its garment (Scripts/metahuman/make_kit.sh builds and exports
# it), smoothed so the fabric does not show the muscles under it, and pushed out from the skin. Every vertex keeps
# the body's skin weights, so the kit follows the body through any pose with no simulation, and fits each body shape.
# The pieces carry the materials Kit_Shirt, Kit_Trousers and Kit_Shoes, which the game paints in team colours.
#
# All of it is generated here from the MetaHuman body: an original design, no third-party asset.
#
# Run: Blender -b --python make_kit.py -- BODY.fbx KIT.fbx
import sys
import bmesh
import bpy
from mathutils import Vector

BODY, KIT = sys.argv[sys.argv.index("--") + 1:][:2]

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


def finish(o, bm, material):
    bmesh.ops.solidify(bm, geom=bm.faces[:], thickness=THICKNESS)
    bm.to_mesh(o.data)
    bm.free()
    o.data.materials.clear()
    o.data.materials.append(bpy.data.materials.new(material))
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

for o in (shirt, trousers, shoes):
    print(f"KIT {o.name}: {len(o.data.vertices)} vertices")
bpy.data.objects.remove(body)
with bpy.context.temp_override(active_object=shirt, selected_editable_objects=[shirt, trousers, shoes]):
    bpy.ops.object.join()
shirt.name = "Kit"
bpy.ops.object.select_all(action='DESELECT')
shirt.select_set(True)
arm.select_set(True)
bpy.ops.export_scene.fbx(filepath=KIT, use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                         bake_anim=False, use_mesh_modifiers=False)
print(f"KIT written {KIT}")

# Blender script: makes the spectator the stadium's crowd is built from, as a static mesh in three levels of detail.
#
# The fan is a MetaHuman player's body (exported by Scripts/metahuman/make_kit.sh) with a simple head and hair,
# posed sitting down. Everything the crowd's material needs is baked into the mesh:
#   - vertex colour marks the region: red the shirt, green the skin, blue the hair, black the trousers and shoes;
#   - the UV channels hold how far each vertex moves (in Unreal centimetres) into two cheering poses, so the
#     material can blend any fan into either with no skeleton: UV0 = (A.x, A.y), UV1 = (A.z, B.x), UV2 = (B.y, B.z).
#     Pose A stands up with both arms high; pose B stays seated and waves one arm.
# The fan faces +X (Unreal's forward) and the origin is the floor under the seat.
#
# All of it is generated here from the engine's MetaHuman body: an original design, no third-party asset.
#
# Run: Blender -b --python make_fan.py -- BODY.fbx OUT_PREFIX   (writes OUT_PREFIX_LOD0.fbx .. _LOD2.fbx)
import math
import sys
import bmesh
import bpy
from mathutils import Matrix, Vector

BODY, OUT = sys.argv[sys.argv.index("--") + 1:][:2]
LOD_TRIS = (1400, 360, 90)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=BODY)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
body = next(o for o in bpy.data.objects if o.type == 'MESH')
bones = arm.data.bones
# Armature space is in centimetres with z up; the body's forward is along y, one way or the other: the toes say which.
foot = bones["foot_l"].head_local
to_arm = arm.matrix_world.inverted() @ body.matrix_world
toes = [to_arm @ v.co for v in body.data.vertices if (to_arm @ v.co).z < 4.0]
FWD = 1.0 if sum(p.y for p in toes) / len(toes) > foot.y else -1.0

# The head: an ellipsoid on the head bone, its crown and back as hair. MetaHuman bodies stop at the neck.
head_at = bones["head"].head_local + Vector((0.0, 2.0 * FWD, 5.5))
bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=1.0, location=(0, 0, 0))
head = bpy.context.active_object
head.data.transform(Matrix.Diagonal((7.8, 9.5, 11.5, 1.0)))
head.data.transform(Matrix.Translation(head_at))
head.matrix_world = arm.matrix_world  # its coordinates are armature space
hg = head.vertex_groups.new(name="head")
hg.add(list(range(len(head.data.vertices))), 1.0, 'REPLACE')
# And the neck, down into the collar.
neck_base = bones["neck_01"].head_local - Vector((0.0, 0.0, 4.0))
bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=5.8, depth=(head_at - neck_base).length, location=(neck_base + head_at) / 2,
                                    rotation=(head_at - neck_base).to_track_quat('Z', 'Y').to_euler(), end_fill_type='NOTHING')
neck = bpy.context.active_object
neck.data.transform(neck.matrix_basis)
neck.matrix_world = arm.matrix_world
ng = neck.vertex_groups.new(name="head")
ng.add(list(range(len(neck.data.vertices))), 1.0, 'REPLACE')

groups = {g.index: g.name for g in body.vertex_groups}


def weight_of(v, prefixes):
    return sum(g.weight for g in v.groups if groups.get(g.group, "").startswith(prefixes))


waist_z = bones["spine_02"].head_local.z
ankle_z = foot.z + 5.0
shoulder = {s: bones[f"upperarm_{s}"].head_local for s in "lr"}
elbow = {s: bones[f"lowerarm_{s}"].head_local for s in "lr"}


def body_region(v):
    p = to_arm @ v.co
    arm_w = weight_of(v, ("upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky"))
    if arm_w > 0.5:
        side = "l" if p.x > 0 else "r"
        d = elbow[side] - shoulder[side]
        return "shirt" if (p - shoulder[side]).dot(d) / d.length_squared < 0.55 else "skin"
    if weight_of(v, ("neck", "head")) > 0.5:
        return "skin"
    return "trousers" if p.z < waist_z else "shirt"


REGION = {"shirt": (1, 0, 0, 1), "skin": (0, 1, 0, 1), "hair": (0, 0, 1, 1), "trousers": (0, 0, 0, 1)}


def paint(o, region_of):
    layer = o.data.color_attributes.new("Region", 'BYTE_COLOR', 'POINT')
    for v in o.data.vertices:
        layer.data[v.index].color = REGION[region_of(v)]


paint(body, body_region)
paint(neck, lambda v: "skin")
paint(head, lambda v: "hair" if (v.co.z - head_at.z > 1.5 or (v.co.y - head_at.y) * FWD < -3.0 and v.co.z - head_at.z > -6.0) else "skin")

# Weld the body along its UV seams so the decimated mesh does not crack open, then join the head on.
bm = bmesh.new()
bm.from_mesh(body.data)
bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.01)
bm.to_mesh(body.data)
bm.free()
bpy.ops.object.select_all(action='DESELECT')
head.select_set(True)
neck.select_set(True)
body.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.ops.object.join()
fan = body


def rotate(name, axis, degrees):
    """Turns a bone about an armature-space axis through its head, children and all."""
    pb = arm.pose.bones[name]
    head_pos = pb.matrix.to_translation()
    turn = Matrix.Translation(head_pos) @ Matrix.Rotation(math.radians(degrees), 4, axis) @ Matrix.Translation(-head_pos)
    pb.matrix = turn @ pb.matrix
    bpy.context.view_layer.update()


def aim(name, child, direction):
    """Turns a bone about its head so that its child joint lies along an armature-space direction. (Imported bones'
    tails point wherever the FBX's joint axes did, not along the limb.)"""
    pb = arm.pose.bones[name]
    head_pos = pb.matrix.to_translation()
    along = arm.pose.bones[child].matrix.to_translation() - head_pos
    turn = along.rotation_difference(direction.normalized()).to_matrix().to_4x4()
    pb.matrix = Matrix.Translation(head_pos) @ turn @ Matrix.Translation(-head_pos) @ pb.matrix
    bpy.context.view_layer.update()


LEFT = 1.0 if bones["thigh_l"].head_local.x > 0 else -1.0


def pose(seated, arms):
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    X, Y = Vector((1, 0, 0)), Vector((0, 1, 0))
    if seated:
        for s in "lr":
            rotate(f"thigh_{s}", X, 88.0 * FWD)
            rotate(f"calf_{s}", X, -85.0 * FWD)
    for s, side in (("l", 1.0), ("r", -1.0)):
        out = side * LEFT  # the x of a direction away from the body on this side
        if arms[s] == "rest":
            # Arms from the A pose down to the sides, forearms forward onto the lap.
            rotate(f"upperarm_{s}", Y, 32.0 * side)
            rotate(f"upperarm_{s}", X, 12.0 * FWD)
            rotate(f"lowerarm_{s}", X, 65.0 * FWD)
        elif arms[s] == "up":  # fists up, elbows out wide, clear of the head
            aim(f"upperarm_{s}", f"lowerarm_{s}", Vector((out * 0.8, 0.1 * FWD, 0.6)))
            aim(f"lowerarm_{s}", f"hand_{s}", Vector((out * 0.1, 0.15 * FWD, 1.0)))
        else:  # "wave": one arm high and out to the side, a little forward
            aim(f"upperarm_{s}", f"lowerarm_{s}", Vector((out * 0.5, 0.3 * FWD, 0.8)))
            aim(f"lowerarm_{s}", f"hand_{s}", Vector((out * 0.35, 0.2 * FWD, 1.0)))
    deps = bpy.context.evaluated_depsgraph_get()
    ev = fan.evaluated_get(deps)
    m = arm.matrix_world.inverted() @ fan.matrix_world
    pts = [m @ v.co for v in ev.to_mesh().vertices]
    ev.to_mesh_clear()
    return pts


seat = pose(True, {"l": "rest", "r": "rest"})
stand = pose(False, {"l": "up", "r": "up"})
wave = pose(True, {"l": "wave", "r": "rest"})

# Armature space to Unreal: forward along +X, right along +Y, up along z, centimetres. The floor is the lowest point
# of the seated fan, the origin sits under the pelvis.
pelvis = bones["pelvis"].head_local
floor = min(p.z for p in seat)
floor_stand = min(p.z for p in stand)


def to_unreal(p, floor_z):
    return Vector(((p.y - pelvis.y) * FWD, -(p.x - pelvis.x) * LEFT, p.z - floor_z))


# The fan stands up in front of the seat, a step forward.
step = Vector((14.0, 0.0, 0.0))
rest = [to_unreal(p, floor) for p in seat]
off_a = [to_unreal(p, floor_stand) + step - r for p, r in zip(stand, rest)]
off_b = [to_unreal(p, floor) - r for p, r in zip(wave, rest)]

# Bake the seated pose into the mesh, drop the skin, and store the offsets. Unreal reads each UV's V upside down
# (v becomes 1 - v), so V holds 1 - value; the material undoes it.
for pb in arm.pose.bones:
    pb.matrix_basis = Matrix.Identity(4)
fan.modifiers.clear()
fan.parent = None
fan.matrix_world = Matrix.Identity(4)
fan.vertex_groups.clear()
for v, p in zip(fan.data.vertices, rest):
    v.co = (p.x, -p.y, p.z)  # the FBX importer mirrors y into Unreal's left-handed axes
bpy.data.objects.remove(arm)
while fan.data.uv_layers:
    fan.data.uv_layers.remove(fan.data.uv_layers[0])
channels = [(lambda i: (off_a[i].x, off_a[i].y)), (lambda i: (off_a[i].z, off_b[i].x)), (lambda i: (off_b[i].y, off_b[i].z))]
for k, value in enumerate(channels):
    uv = fan.data.uv_layers.new(name=f"Pose{k}")
    for loop in fan.data.loops:
        u, v = value(loop.vertex_index)
        uv.data[loop.index].uv = (u, 1.0 - v)
fan.data.materials.clear()
fan.data.materials.append(bpy.data.materials.new("Fan"))
for poly in fan.data.polygons:
    poly.material_index = 0
    poly.use_smooth = True

base_tris = sum(len(p.vertices) - 2 for p in fan.data.polygons)
for lod, tris in enumerate(LOD_TRIS):
    o = fan.copy()
    o.data = fan.data.copy()
    bpy.context.scene.collection.objects.link(o)
    dec = o.modifiers.new("Decimate", 'DECIMATE')
    dec.ratio = min(1.0, tris / base_tris)
    bpy.ops.object.select_all(action='DESELECT')
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier="Decimate")
    # Centimetres here, so export at 0.01: Unreal reads the file's metres back as centimetres.
    path = f"{OUT}_LOD{lod}.fbx"
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={'MESH'}, global_scale=0.01, apply_unit_scale=True,
                             use_mesh_modifiers=False, mesh_smooth_type='FACE', axis_forward='Y', axis_up='Z', colors_type='LINEAR')
    got = sum(len(p.vertices) - 2 for p in o.data.polygons)
    print(f"FAN written {path}: {got} triangles")
    bpy.data.objects.remove(o)

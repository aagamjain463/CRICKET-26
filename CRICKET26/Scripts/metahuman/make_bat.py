# Blender side of the bat (see make_kit.sh): an original English-willow bat, written to the FBX given after "--".
#
# Axes as the game places the bat (PlaceBat in SuperOverGameMode.cpp): the origin at the middle of the blade, +Z
# down the bat toward the toe, +Y out of the hitting face, X across the blade. Units are cm, exported at 0.01 so
# Unreal reads cm. The blade is lofted from cross sections: a nearly flat face, square edges, and a spine on the
# back that is deepest at the sweet spot. The handle is round with a ribbed rubber grip. The stickers are the
# fictional "KESTREL" brand, as panels and raised letters, so the bat needs no textures.
import math
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

OUT = sys.argv[sys.argv.index("--") + 1]
BLADE, HANDLE = 56.0, 29.0  # CricketPose::BatLength - HandleLength, and HandleLength
WIDTH, FACE = 10.8, 2.0     # blade width; the face sits 2 cm in front of the axis
EDGE, SPINE = 3.9, 6.4      # depth at the edges and at the peak of the spine
GRIP_R, CANE_R = 1.75, 1.65
RING = 48                   # points round each cross section

bpy.ops.wm.read_factory_settings(use_empty=True)


WILLOW, GRIP, STICKER, LETTERS = (bpy.data.materials.new(n) for n in ("Bat_Willow", "Bat_Grip", "Bat_Sticker", "Bat_Letters"))


def spine(z):
    """Back depth of the blade at z (0 at the shoulder, BLADE at the toe): shallow at the shoulder, peaking a
    little below the middle where the ball is met, easing off toward the toe."""
    u = z / BLADE
    peak = math.exp(-((u - 0.62) / 0.33) ** 2)
    return EDGE + (SPINE - EDGE) * peak, EDGE - 0.6 + 0.6 * peak


def blade_section(z):
    """Points round the blade at z. The face is flat with a slight crown; the back rises from the edges to the
    spine. Width narrows into the shoulders and rounds off at the toe."""
    peak, edge = spine(z)
    hw = WIDTH / 2
    if z < 5.0:  # shoulders: from the handle's width to the full blade in a quarter circle
        hw = GRIP_R + (hw - GRIP_R) * math.sqrt(1 - (1 - z / 5.0) ** 2)
        peak = GRIP_R * 2 + (peak - GRIP_R * 2) * (z / 5.0)
        edge = min(edge, peak)
    if z > BLADE - 2.0:  # rounded toe corners
        hw *= math.sqrt(max(0.0, 1 - ((z - BLADE + 2.0) / 2.6) ** 2))
    pts = []
    for i in range(RING):
        a = 2 * math.pi * i / RING
        c, s = math.cos(a), math.sin(a)
        x = hw * math.copysign(abs(c) ** 0.3, c)
        t = min(1.0, abs(x) / max(hw, 1e-3))
        face_y = FACE + 0.15 * (1 - t * t)
        back_y = FACE - (edge + (peak - edge) * (1 - t * t) ** 1.4)
        k = 0.5 + 0.5 * math.copysign(abs(s) ** 0.3, s)
        pts.append((x, back_y + (face_y - back_y) * k, z))
    return pts


def round_section(z, r, ribs=0.0):
    """A handle ring, centred where the shoulders take it up (the face minus the grip radius)."""
    r *= 1 + ribs * math.cos(8 * z)
    return [(r * math.cos(2 * math.pi * i / RING), FACE - GRIP_R + r * math.sin(2 * math.pi * i / RING), z)
            for i in range(RING)]


# Stations from the top of the handle (z = -HANDLE) to the toe (z = BLADE), each with its section and material.
stations = []
for i in range(0, 151):  # handle, grip ribs every ~0.8 cm, bare cane below the grip where the splice shows
    z = -HANDLE + (HANDLE - 0.5) * i / 150
    if z < -2.5:
        stations.append((round_section(z, GRIP_R, 0.035), 1))
    else:
        stations.append((round_section(z, CANE_R), 0))
for i in range(0, 90):
    z = BLADE * i / 89
    stations.append(([(x, y, z) for x, y, _ in blade_section(z)], 0))

bm = bmesh.new()
rings = [[bm.verts.new(p) for p in pts] for pts, _ in stations]
for (_, m), a, b in zip(stations, rings, rings[1:]):
    for i in range(RING):
        f = bm.faces.new((a[i], a[(i + 1) % RING], b[(i + 1) % RING], b[i]))
        f.material_index = m
# Caps: a domed top on the handle and a flat toe.
top = bm.verts.new((0.0, FACE - GRIP_R, -HANDLE - 0.6))
for i in range(RING):
    bm.faces.new((rings[0][(i + 1) % RING], rings[0][i], top)).material_index = 1
toe = bm.faces.new(rings[-1])
toe.material_index = 0
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.001)
bm.normal_update()
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

# Sticker panels: the back of the blade from 8 to 44 cm, and a band across the face below the shoulders.
for f in bm.faces:
    c = f.calc_center_median()
    if f.material_index == 0 and f.normal.y < -0.3 and 8.0 < c.z < 44.0 and abs(c.x) < 2.9:
        f.material_index = 2
    if f.material_index == 0 and f.normal.y > 0.7 and 6.0 < c.z < 13.0:
        f.material_index = 2

me = bpy.data.meshes.new("Bat")
bm.to_mesh(me)
bm.free()
bat = bpy.data.objects.new("Bat", me)
bpy.context.collection.objects.link(bat)
for m in (WILLOW, GRIP, STICKER, LETTERS):
    me.materials.append(m)
for p in me.polygons:
    p.use_smooth = True


def letters(text, size, at, across, up):
    """Raised brand letters shrink-wrapped onto the bat, 0.05 cm proud of it."""
    cu = bpy.data.curves.new(text, "FONT")
    cu.body = text
    cu.size = size
    cu.align_x = cu.align_y = "CENTER"
    cu.extrude = 0.0
    o = bpy.data.objects.new(text, cu)
    bpy.context.collection.objects.link(o)
    x, y = Vector(across), Vector(up)
    m = Matrix((x, y, x.cross(y))).transposed().to_4x4()
    m.translation = at
    o.matrix_world = m
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    bpy.ops.object.convert(target="MESH")
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.subdivide(number_cuts=3)
    bpy.ops.object.mode_set(mode="OBJECT")
    o.select_set(False)
    w = o.modifiers.new("wrap", "SHRINKWRAP")
    w.target, w.wrap_method, w.offset = bat, "NEAREST_SURFACEPOINT", 0.05
    s = o.modifiers.new("thick", "SOLIDIFY")
    s.thickness, s.offset = 0.05, 1.0
    bpy.context.view_layer.objects.active = o
    for mod in list(o.modifiers):
        bpy.ops.object.modifier_apply(modifier=mod.name)
    o.data.materials.clear()
    o.data.materials.append(LETTERS)
    return o


# The name runs down the spine, read with the toe to the right; the face carries it across below the shoulders.
back = letters("KESTREL", 4.2, (0.0, FACE - SPINE - 0.5, 26.0), (0, 0, 1), (-1, 0, 0))
front = letters("KESTREL", 1.5, (0.0, FACE + 0.6, 9.5), (1, 0, 0), (0, 0, -1))

for o in (bat, back, front):
    o.select_set(True)
bpy.context.view_layer.objects.active = bat
bpy.ops.object.join()
# Modelled from the shoulder; move the origin to the middle of the blade. Unreal's import negates Y, so turn the
# bat half round too: its face then comes out at +Y there.
bat.scale = (0.01, 0.01, 0.01)
bat.location = (0.0, 0.0, -0.01 * BLADE / 2)
bat.rotation_euler = (0.0, 0.0, math.pi)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.export_scene.fbx(filepath=OUT, use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                         use_mesh_modifiers=False, mesh_smooth_type="FACE", axis_forward="Y", axis_up="Z")
print(f"KIT written {OUT}")

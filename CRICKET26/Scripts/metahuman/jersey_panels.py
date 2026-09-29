# Paints the cricket jersey's panels onto the UV layout of Epic's crew-neck outfit shirt (OA_CrewnecktTk), for the
# game to lay under the name, number and sponsor it prints on each shirt (SuperOverGameMode::ShirtPrint).
#
# Every texel of the shirt's first UV set is given the point of the shirt it covers, so the panels are drawn by where
# they sit on the body: raglan sleeves from the collar to the armpit, side panels under the arms, a collar band round
# the neck opening, cuff bands at the sleeve ends and fine diagonal pinstripes across the front and back, the look of
# a sublimated T20 shirt. The texture's blue channel is the team's second colour (the print's third colour), green
# the gold of the collar (its second) and alpha how strongly they show; the cuffs are left in the shirt's colour.
#
# Run: Blender -b -P Scripts/metahuman/jersey_panels.py -- shirt.obj out.png [size]
#   (the shirt, /Game/MetaHumans/Outfits/OA_CrewnecktTk/meshes/m_med_nrw_top_crewnecktt_nrm, written out as an OBJ by
#   Scripts/metahuman/jersey_ue.py; make_jersey.sh runs the lot).
import sys
import bmesh
import bpy
import numpy as np

args = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = args[0], args[1]
SIZE = int(args[2]) if len(args) > 2 else 1024

bpy.ops.wm.read_factory_settings(use_empty=True)
if SRC.lower().endswith(".obj"):
    bpy.ops.wm.obj_import(filepath=SRC, forward_axis="Y", up_axis="Z")   # written in Unreal's axes, Z up
else:
    bpy.ops.import_scene.fbx(filepath=SRC)
ob = next(o for o in bpy.context.scene.objects if o.type == "MESH")
bm = bmesh.new()
bm.from_mesh(ob.data)
bm.transform(ob.matrix_world)
uv = bm.loops.layers.uv[0]

# Body axes: Z is up, sideways is the wider of the other two (the sleeves reach out), and the front is +Y, the way
# every MetaHuman mesh faces.
co = np.array([v.co[:] for v in bm.verts])
ext = co.max(0) - co.min(0)
up = 2
side = int(np.argmax(ext[:2]))
depth = 1 - side
M = 100.0 if ext[up] > 5 else 1.0                # units per metre: Unreal writes centimetres
centre = (co.max(0) + co.min(0)) / 2
top = co[:, up].max()
height = ext[up]

# The neck opening: boundary edges once seams split by the UVs are welded, or, where Epic folds the hem inside, the
# sharp crease of the fold, near the top.
weld = bm.copy()
bmesh.ops.remove_doubles(weld, verts=weld.verts, dist=1e-4)
edge = np.array([(e.verts[0].co + e.verts[1].co)[:] for e in weld.edges
                 if e.is_boundary or (e.is_manifold and e.calc_face_angle(0) > 2.0)]) / 2
weld.free()
neck = edge[edge[:, up] > top - 0.15 * M]
front = 1.0

# Texel positions: each triangle rasterized into UV space with its points interpolated.
pos = np.zeros((SIZE, SIZE, 3), np.float32)
hit = np.zeros((SIZE, SIZE), bool)
for face in bm.calc_loop_triangles():
    t = np.array([l[uv].uv[:] for l in face]) * SIZE
    p = np.array([l.vert.co[:] for l in face])
    x0, y0 = np.floor(t.min(0)).astype(int)
    x1, y1 = np.ceil(t.max(0)).astype(int) + 1
    x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, SIZE), min(y1, SIZE)
    if x0 >= x1 or y0 >= y1:
        continue
    gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
    d = (t[1, 1] - t[2, 1]) * (t[0, 0] - t[2, 0]) + (t[2, 0] - t[1, 0]) * (t[0, 1] - t[2, 1])
    if abs(d) < 1e-12:
        continue
    a = ((t[1, 1] - t[2, 1]) * (gx - t[2, 0]) + (t[2, 0] - t[1, 0]) * (gy - t[2, 1])) / d
    b = ((t[2, 1] - t[0, 1]) * (gx - t[2, 0]) + (t[0, 0] - t[2, 0]) * (gy - t[2, 1])) / d
    c = 1 - a - b
    inside = (a >= -0.01) & (b >= -0.01) & (c >= -0.01)
    rows, cols = gy[inside].astype(int), gx[inside].astype(int)
    pos[rows, cols] = a[inside, None] * p[0] + b[inside, None] * p[1] + c[inside, None] * p[2]
    hit[rows, cols] = True
bm.free()

x = np.abs(pos[..., side] - centre[side])        # out from the spine to either side
z = top - pos[..., up]                           # down from the top of the collar
f = (pos[..., depth] - centre[depth]) * front    # forward of the body's middle
half = ext[side] / 2
armpit = 0.2 * M                                 # the armpit's drop below the top of the shoulders
# Half the chest's width: the widest the shirt gets below the sleeves; the neck's, where the raglan seam starts.
torso = np.abs(co[co[:, up] < top - 0.33 * M, side] - centre[side]).max()
neck_half = np.abs(neck[:, side] - centre[side]).max()

def near(points, radius):
    """Texels within radius of any of points (chunked, as the grid is large)."""
    out = np.zeros(hit.shape, bool)
    flat = pos[hit]
    m = np.zeros(len(flat), bool)
    for i in range(0, len(points), 64):
        d = np.linalg.norm(flat[:, None, :] - points[None, i:i + 64, :], axis=2).min(1)
        m |= d < radius
    out[hit] = m
    return out

# Raglan: a seam from the side of the collar diagonally down to the armpit; the sleeve is everything outside it.
raglan = x > 0.8 * neck_half + (torso - 0.8 * neck_half) * np.clip(z / armpit, 0, 1)
sleeve = raglan | (x > torso)
# Side panels: a band down each flank under the arm, where the body turns from front to back.
flank = np.arctan2(np.abs(f), x) < 0.45
side_panel = (~sleeve) & flank & (z > armpit * 0.9)
collar = near(neck, 0.015 * M)
# The cuffs: Epic's hems fold inside, so there is no opening to find; instead the last 3 cm of each sleeve along the
# arm, from the top of the shoulder out to the sleeve's tip.
cuff = np.zeros(hit.shape, bool)
for s in (1, -1):
    tip = co[s * (co[:, side] - centre[side]) > half - 0.015 * M].mean(0)
    shoulder = centre.copy()
    shoulder[side] += s * torso
    shoulder[up] = top
    arm = (tip - shoulder) / np.linalg.norm(tip - shoulder)
    reach = ((co[s * (co[:, side] - centre[side]) > 0] - shoulder) @ arm).max()
    cuff |= hit & (s * (pos[..., side] - centre[side]) > 0) & ((pos - shoulder) @ arm > reach - 0.03 * M)
# Pinstripes: thin diagonals across the front and back, faint, for the sublimated weave of a modern shirt.
stripe = (~sleeve) & (~side_panel) & (np.mod((pos[..., side] - centre[side]) * 0.8 + pos[..., up], 0.045 * M) < 0.006 * M)

alpha = np.zeros(hit.shape, np.float32)
alpha[stripe] = 0.35
alpha[sleeve | side_panel] = 1.0
alpha[cuff] = 0.0                                # the shirt's own colour banding the second-colour sleeve
alpha[collar] = 1.0
alpha[~hit] = 0.0
# Grow the painted texels a few pixels over the UV island edges so no seam shows the base colour through.
for _ in range(4):
    grown = alpha.copy()
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        grown = np.maximum(grown, np.roll(np.roll(alpha, dy, 0), dx, 1) * ~hit)
    alpha = grown
img = np.zeros((SIZE, SIZE, 4), np.float32)
img[..., 2] = ~collar
img[..., 1] = collar                             # the collar gold, the print's second colour
img[..., 3] = alpha
out = bpy.data.images.new("panels", SIZE, SIZE, alpha=True)
out.pixels = img.ravel()
out.filepath_raw = OUT
out.file_format = "PNG"
out.save()
# Where the lettering goes, for ShirtPrint: the UV of the middle of the back and chest a few drops (in metres) below
# the top of the shoulders.
for label, sign in (("back", -1), ("front", 1)):
    for drop in (0.06, 0.1, 0.15, 0.2, 0.28):
        score = np.where(hit & (sign * f > 0), np.abs(pos[..., side] - centre[side]) + np.abs(z - drop * M), 1e9)
        row, col = np.unravel_index(np.argmin(score), score.shape)
        print(f"JERSEY {label} drop {drop}: u {col / SIZE:.3f} v {1 - row / SIZE:.3f} (Unreal v)")
print(f"JERSEY side={side} front={front:+.0f} size={ext} torso={torso:.1f} neck={len(neck)} cuff={cuff[hit].mean():.2f} "
      f"coverage={hit.mean():.2f} sleeve={sleeve[hit].mean():.2f} side={side_panel[hit].mean():.2f}")

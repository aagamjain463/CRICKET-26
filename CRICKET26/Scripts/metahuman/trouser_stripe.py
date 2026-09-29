# Turns Epic's slim jeans (WI_OA_Jeans_slm, the only free trousers) into cricket trousers by repainting their mask.
# The jeans' mask marks the leather back-pocket label in its blue channel, which the material tints with "Leather
# Tint"; the label is cleared and the blue channel given instead a stripe down the outside of each leg, from the
# waistband to the hem, for SuperOverGameMode::TintOutfit to tint in the team's second colour: the piping of a T20 kit.
# The red (wear), green (stitching) and alpha (fold relief) channels are kept.
#
# Every texel of the trousers' UVs is given the point it covers, as in jersey_panels.py, so the stripe is drawn by where
# it sits on the body.
#
# Run: Blender -b -P Scripts/metahuman/trouser_stripe.py -- trousers.obj mask_in.tga out.png
#   (the trousers as built, written as an OBJ by jersey_ue.py with JERSEY_SLOT=jeans, and their mask as exported from
#   the editor; make_jersey.sh runs the lot).
import sys
import bmesh
import bpy
import numpy as np

SRC, MASK, OUT = sys.argv[sys.argv.index("--") + 1:][:3]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=SRC, forward_axis="Y", up_axis="Z")   # written in Unreal's axes, Z up
ob = next(o for o in bpy.context.scene.objects if o.type == "MESH")
bm = bmesh.new()
bm.from_mesh(ob.data)
uv = bm.loops.layers.uv[0]

mask = bpy.data.images.load(MASK)
mask.colorspace_settings.name = "Non-Color"
SIZE = mask.size[0]
img = np.array(mask.pixels[:], np.float32).reshape(SIZE, SIZE, 4)

co = np.array([v.co[:] for v in bm.verts])
M = 100.0 if np.ptp(co[:, 2]) > 5 else 1.0      # units per metre: Unreal writes centimetres
top = co[:, 2].max()
cx = (co[:, 0].max() + co[:, 0].min()) / 2

# Texel positions: each triangle rasterized into UV space with its points interpolated.
pos = np.zeros((SIZE, SIZE, 3), np.float32)
hit = np.zeros((SIZE, SIZE), bool)
for face in bm.calc_loop_triangles():
    t = np.array([l[uv].uv[:] for l in face]) * SIZE
    p = np.array([l.vert.co[:] for l in face])
    x0, y0 = np.maximum(np.floor(t.min(0)).astype(int), 0)
    x1, y1 = np.minimum(np.ceil(t.max(0)).astype(int) + 1, SIZE)
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

# The outside of each leg (or, above the crotch, of the hips) down its length, from the points in 2 cm slices: the
# stripe runs where the cloth is level with the leg's middle front to back, on its outermost surface (not the pocket
# bags and inseams inside it), both smoothed between slices so its edges run straight.
BAND = 0.02 * M
stripe = np.zeros(hit.shape, bool)
for s in (1, -1):
    leg = co[s * (co[:, 0] - cx) > 0]
    bins = np.floor((top - leg[:, 2]) / BAND).astype(int)
    ks = np.unique(bins)
    drop = (ks + 0.5) * BAND
    mid = np.array([(leg[bins == k, 1].max() + leg[bins == k, 1].min()) / 2 for k in ks])
    outer = np.array([(s * leg[bins == k, 0]).max() for k in ks])
    z = top - pos[..., 2]
    stripe |= (hit & (s * (pos[..., 0] - cx) > 0)
               & (np.abs(pos[..., 1] - np.interp(z, drop, mid)) < 0.007 * M)
               & (s * pos[..., 0] > np.interp(z, drop, outer) - 0.03 * M))
stripe &= pos[..., 2] < top - 0.045 * M          # below the waistband
img[..., 2] = stripe                              # the label gone, the piping in its place
out = bpy.data.images.new("mask", SIZE, SIZE, alpha=True)
out.colorspace_settings.name = "Non-Color"
out.pixels = img.ravel()
out.filepath_raw = OUT
out.file_format = "PNG"
out.save()
print(f"TROUSERS stripe={stripe[hit].mean():.3f} coverage={hit.mean():.2f} size={np.ptp(co, 0)}")

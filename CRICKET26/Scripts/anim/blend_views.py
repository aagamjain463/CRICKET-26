# Renders an animated FBX (or the open .blend) from several fixed cameras, one tile per sampled frame, into one
# contact sheet per view: the raw clip with nothing layered over it, for frame-by-frame inspection.
# Usage: Blender -b [file.blend] -P blend_views.py -- <in.fbx|-> <out_prefix> <step_frames> [first last] [views]
#   views: comma list of azimuths in degrees around the hips (0 = camera at +Y looking along -Y, 90 = at -X looking
#   along +X), default 0,90,180,270,45
import bpy, sys, math, os
from mathutils import Vector

argv = sys.argv[sys.argv.index('--') + 1:]
src, out, step = argv[0], argv[1], int(argv[2])
if src != '-':
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=src)
sc = bpy.context.scene
arm = next(o for o in sc.objects if o.type == 'ARMATURE')
act = arm.animation_data.action
first = int(argv[3]) if len(argv) > 4 else int(act.frame_range[0])
last = int(argv[4]) if len(argv) > 4 else int(act.frame_range[1])
views = [float(v) for v in argv[5].split(',')] if len(argv) > 5 else [0, 90, 180, 270, 45]

hips = next(b for b in arm.pose.bones if b.name.split(':')[-1].lower() in ('hips', 'pelvis'))
centre = Vector()
for f in range(first, last + 1):
    sc.frame_set(f)
    centre += arm.matrix_world @ hips.head
centre /= (last - first + 1)
centre.z = max(centre.z, 0.9)

sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.light = 'STUDIO'
sc.display.shading.color_type = 'OBJECT'
sc.render.resolution_x, sc.render.resolution_y = 300, 380
sc.render.film_transparent = False
sc.world = sc.world or bpy.data.worlds.new('W')
# A ground grid for judging foot slide: 10 cm squares.
if 'Floor' not in bpy.data.objects:
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=40, y_subdivisions=40, size=4, location=(centre.x, centre.y, 0))
    fl = bpy.context.object
    fl.name = 'Floor'
    fl.display_type = 'WIRE'
cam_data = bpy.data.cameras.new('C')
cam_data.lens = 40
cam = bpy.data.objects.new('C', cam_data)
sc.collection.objects.link(cam)
sc.camera = cam

import subprocess  # Blender's Python has no PIL: the system python3 tiles the frames with sheet.py
sheet = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sheet.py')
for az in views:
    a = math.radians(az)
    d = Vector((math.sin(a), -math.cos(a), 0))  # the view direction
    cam.location = centre - d * 4.2 + Vector((0, 0, 0.35))
    cam.rotation_euler = (centre - cam.location).to_track_quat('-Z', 'Y').to_euler()
    frames = '%s_az%03d_f%%03d.png' % (out, int(az))
    for f in range(first, last + 1, step):
        sc.frame_set(f)
        sc.render.filepath = frames % f
        bpy.ops.render.render(write_still=True)
    subprocess.run(['/usr/bin/env', 'python3', sheet, '%s_az%03d.png' % (out, int(az)), str(first), str(last), str(step),
                    '0,0,%d,%d' % (sc.render.resolution_x, sc.render.resolution_y), frames])
    # Raw playback: with MOVIE=1,0.5,0.25 (and a step of 1) the frames also become a movie at each speed.
    for speed in [float(v) for v in os.environ.get('MOVIE', '').split(',') if v]:
        movie = '%s_az%03d_x%s.mp4' % (out, int(az), ('%g' % speed).replace('.', ''))
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-framerate', '%g' % (sc.render.fps * speed / step),
                        '-start_number', str(first), '-i', frames, '-r', '60', '-pix_fmt', 'yuv420p', movie])
        print('MOVIE %s' % movie)
    for f in range(first, last + 1, step):
        os.remove(frames % f)
    print('SHEET %s_az%03d.png' % (out, int(az)))

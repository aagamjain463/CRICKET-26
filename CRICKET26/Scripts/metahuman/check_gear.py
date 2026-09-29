"""Inspect existing FBX gear in Blender; does not save changes."""
import sys
import bpy

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=sys.argv[sys.argv.index('--') + 1])
for obj in bpy.data.objects:
    if obj.type != 'MESH':
        continue
    xs = [v.co.x for v in obj.data.vertices]
    ys = [v.co.y for v in obj.data.vertices]
    zs = [v.co.z for v in obj.data.vertices]
    print('VISUAL_GEAR', obj.name, len(xs), obj.data.materials[0].name if obj.data.materials else 'none',
          [(round(min(a), 1), round(max(a), 1)) for a in (xs, ys, zs)],
          [(m.type, m.name) for m in obj.modifiers])
    for index, material in enumerate(obj.data.materials):
        vertices = {v for poly in obj.data.polygons if poly.material_index == index for v in poly.vertices}
        values = [obj.data.vertices[i].co for i in vertices]
        bad = [(i, [(obj.vertex_groups[g.group].name, round(g.weight, 2)) for g in obj.data.vertices[i].groups])
               for i in vertices if sum(g.weight for g in obj.data.vertices[i].groups) < 0.99]
        print('VISUAL_PART', material.name, len(vertices),
              [(round(min(v[k] for v in values), 1), round(max(v[k] for v in values), 1)) for k in range(3)],
              'bad_weights', len(bad), bad[:5])

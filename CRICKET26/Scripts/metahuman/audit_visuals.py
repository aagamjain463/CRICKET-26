"""Read-only UE character inventory. Run with -run=pythonscript -script=... -nullrhi.

Writes Saved/PlayerVisualQA/assets.json; never saves or rebuilds Unreal assets.
"""
import json
from pathlib import Path
import unreal


def path(obj):
    return obj.get_path_name() if obj else None


def prop(obj, name):
    try:
        return str(obj.get_editor_property(name))
    except Exception:
        return None


lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
data_lib = unreal.SubobjectDataBlueprintFunctionLibrary
report = {}
for asset in sorted(lib.list_assets('/Game/MetaHumans/Source', recursive=False)):
    name = asset.rsplit('/', 1)[-1].split('.')[0]
    bp = unreal.load_asset(f'/Game/MetaHumans/{name}/BP_{name}')
    if not bp:
        continue
    record = {'blueprint': path(bp), 'components': [], 'meshes': [], 'materials': []}
    seen = set()
    for handle in sub.k2_gather_subobject_data_for_blueprint(bp):
        obj = data_lib.get_object_for_blueprint(data_lib.get_data(handle), bp)
        if not obj:
            continue
        if obj.get_path_name() in seen:
            continue
        seen.add(obj.get_path_name())
        item = {'name': obj.get_name(), 'class': obj.get_class().get_name()}
        for key in ('forced_lod', 'min_lod', 'num_lods', 'components_to_sync',
                    'custom_lod_mapping', 'skeletal_mesh_asset', 'groom_asset',
                    'binding_asset', 'use_cards', 'visible', 'enable_simulation'):
            value = prop(obj, key)
            if value is not None:
                item[key] = value
        if isinstance(obj, unreal.GroomComponent):
            groom = obj.get_editor_property('groom_asset')
            if groom:
                item['groom_lods'] = [[{
                    'geometry': str(lod.get_editor_property('geometry_type')),
                    'screen_size': lod.get_editor_property('screen_size'),
                    'curve_decimation': lod.get_editor_property('curve_decimation'),
                } for lod in group.get_editor_property('lods')]
                    for group in groom.get_editor_property('hair_groups_lod')]
        record['components'].append(item)
    for asset_path in lib.list_assets(f'/Game/MetaHumans/{name}', recursive=True):
        # Read only relevant mesh and material assets, not DNA or texture bulk data.
        leaf = asset_path.rsplit('/', 1)[-1]
        if leaf.startswith(('SKM_', 'MH_')) and '/Grooms/' not in asset_path:
            obj = unreal.load_asset(asset_path)
            if isinstance(obj, unreal.SkeletalMesh):
                record['meshes'].append({
                    'path': path(obj), 'skeleton': path(obj.skeleton),
                    'lod_count': unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_lod_count(obj),
                    'materials': [{'slot': str(m.material_slot_name), 'material': path(m.material_interface)}
                                  for m in obj.materials],
                    'post_process_anim_blueprint': prop(obj, 'post_process_anim_blueprint'),
                })
        elif leaf.startswith('MI_') and '/Grooms/' not in asset_path:
            obj = unreal.load_asset(asset_path)
            if isinstance(obj, unreal.MaterialInstanceConstant):
                record['materials'].append({
                    'path': path(obj), 'parent': path(obj.parent),
                    'scalars': {str(n): mel.get_material_instance_scalar_parameter_value(obj, n)
                                for n in mel.get_scalar_parameter_names(obj)},
                })
    report[name] = record
    unreal.log(f'VISUAL_AUDIT {name}: {len(record["components"])} components, {len(record["meshes"])} meshes')

output = Path(unreal.Paths.project_saved_dir()) / 'PlayerVisualQA' / 'assets.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2) + '\n')
unreal.log(f'VISUAL_AUDIT written {output}')

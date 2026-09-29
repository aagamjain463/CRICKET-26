"""Export current fitted bodies for cricket apparel; import isolated, reversible assets.

PREMIUM_STEP=export|import UnrealEditor-Cmd ... -run=pythonscript -script=...
The original MetaHuman blueprints, rigs and wardrobe remain the source of truth.
"""
import os
from pathlib import Path
import unreal

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
root = Path(unreal.Paths.project_saved_dir()) / 'PremiumPlayers' / 'mesh'
root.mkdir(parents=True, exist_ok=True)
names = os.environ.get('PREMIUM_NAMES', 'MH_Home_Opener,MH_Home_Finisher,MH_Home_Allrounder,MH_Home_Quick,MH_Away_Hitter,MH_Away_Anchor,MH_Away_KeeperBat,MH_Away_WristSpinner,MH_Umpire_1,MH_Umpire_2').split(',')

for name in names:
    body = unreal.load_asset(f'/Game/MetaHumans/{name}/Body/SKM_{name}_BodyMesh')
    assert body, name
    if os.environ['PREMIUM_STEP'] == 'export':
        task = unreal.AssetExportTask()
        task.object = body
        task.filename = str(root / f'{name}_Body.fbx')
        task.automated = task.replace_identical = True
        task.prompt = False
        task.options = unreal.FbxExportOption()
        task.options.level_of_detail = task.options.collision = False
        assert unreal.Exporter.run_asset_export_task(task), name
        task.object = unreal.load_asset(f'/Game/MetaHumans/{name}/Face/SKM_{name}_FaceMesh')
        task.filename = str(root / f'{name}_Face.fbx')
        assert unreal.Exporter.run_asset_export_task(task), name
        unreal.log(f'PREMIUM exported {name}')
    else:
        ui = unreal.FbxImportUI()
        ui.import_mesh = ui.import_as_skeletal = True
        ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
        ui.skeleton = body.skeleton
        ui.import_materials = ui.import_textures = ui.import_animations = ui.create_physics_asset = False
        ui.skeletal_mesh_import_data.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
        task = unreal.AssetImportTask()
        task.filename = str(root / f'{name}_Kit.fbx')
        assert Path(task.filename).is_file(), task.filename
        task.destination_path = '/Game/MetaHumans/CricketKit'
        task.destination_name = f'SKM_{name}_Kit'
        task.automated = task.replace_existing = task.save = True
        task.options = ui
        tools.import_asset_tasks([task])
        assert task.imported_object_paths, name
        unreal.log(f'PREMIUM imported {name}')

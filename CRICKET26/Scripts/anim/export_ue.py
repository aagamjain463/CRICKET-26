# Unreal side of Scripts/anim/export_rig.sh: writes the striker's kit and gear (on the MetaHuman body skeleton) and
# the mannequin idle to FBX, so strokes can be authored in Blender on the exact skeleton the game plays.
import os
import unreal

OUT = os.environ["EXPORT_DIR"]
ASSETS = {
    # The game's body mesh has the skin under the kit cut away; export_rig.sh takes the whole body from the kit build.
    "Kit.fbx": ("/Game/MetaHumans/MH_Home_Opener/Kit/SKM_MH_Home_Opener_Kit", unreal.SkeletalMeshExporterFBX),
    "Gear.fbx": ("/Game/MetaHumans/MH_Home_Opener/Kit/SKM_MH_Home_Opener_Gear", unreal.SkeletalMeshExporterFBX),
    "Idle.fbx": ("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle", unreal.AnimSequenceExporterFBX),
}
os.makedirs(OUT, exist_ok=True)
for name, (path, exporter) in ASSETS.items():
    asset = unreal.load_asset(path)
    if not asset:
        unreal.log_error(f"EXPORT missing {path}")
        continue
    t = unreal.AssetExportTask()
    t.object = asset
    t.filename = os.path.join(OUT, name)
    t.exporter = exporter()
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    opt = unreal.FbxExportOption()
    opt.collision = False
    opt.level_of_detail = False
    t.options = opt
    ok = unreal.Exporter.run_asset_export_task(t)
    skel = asset.skeleton if hasattr(asset, "skeleton") else None
    unreal.log(f"EXPORT {name} {ok} skeleton={skel.get_path_name() if skel else None}")

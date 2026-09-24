# Unreal side of the cricket kit (see make_kit.sh).
#
# KIT_STEP=export: builds each player once more without the preset garment into /Game/MetaHumans/Bare (the
#   garment's hidden-face map cuts the skin under it out of the real build, and the kit needs the whole body),
#   and exports that body to $KIT_DIR/<Name>_Body.fbx. The source character is not saved, so it keeps its garment.
# KIT_STEP=import: imports $KIT_DIR/<Name>_Kit.fbx and <Name>_Gear.fbx onto the player's own skeleton as
#   /Game/MetaHumans/<Name>/Kit/SKM_<Name>_Kit and SKM_<Name>_Gear, and makes the fabric material
#   /Game/MetaHumans/Kit/M_Kit.
import os
import unreal

STEP, DIR = os.environ["KIT_STEP"], os.environ["KIT_DIR"]
NAMES = os.environ["KIT_NAMES"].split(",")
BARE = "/Game/MetaHumans/Bare"
lib = unreal.EditorAssetLibrary


def body_mesh(root, name):
    return unreal.load_asset(f"{root}/{name}/Body/SKM_{name}_BodyMesh")


def export(name):
    if not lib.does_directory_exist(f"{BARE}/{name}"):
        sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
        c = unreal.load_asset(f"/Game/MetaHumans/Source/{name}")
        if not sub.try_add_object_to_edit(c):
            unreal.log_error(f"KIT {name}: cannot open for editing")
            return
        try:
            instance = c.get_editor_property("internal_collection").get_editor_property("default_instance")
            instance.set_single_slot_selection("Outfits", unreal.MetaHumanPaletteItemKey())
            p = unreal.MetaHumanCharacterEditorBuildParameters()
            p.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
            p.pipeline_quality = unreal.MetaHumanQualityLevel.MEDIUM
            p.absolute_build_path = BARE
            p.common_folder_path = f"{BARE}/Common"
            p.enable_wardrobe_item_validation = False
            sub.build_meta_human(character=c, params=p)
        finally:
            sub.remove_object_to_edit(c)
    t = unreal.AssetExportTask()
    t.object = body_mesh(BARE, name)
    t.filename = f"{DIR}/{name}_Body.fbx"
    t.automated = t.replace_identical = True
    t.prompt = False
    o = unreal.FbxExportOption()
    o.level_of_detail = o.collision = False
    t.options = o
    unreal.log(f"KIT {name}: body exported {unreal.Exporter.run_asset_export_task(t)}")


def kit_material():
    if lib.does_asset_exist("/Game/MetaHumans/Kit/M_Kit"):
        return
    mel = unreal.MaterialEditingLibrary
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Kit", "/Game/MetaHumans/Kit", unreal.Material,
                                                                  unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)  # a game cannot compile the usage in later
    colour = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -400, 0)
    colour.set_editor_property("parameter_name", "Color")
    colour.set_editor_property("default_value", unreal.LinearColor(0.2, 0.3, 0.8, 1.0))
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Matte polyester: rough, with little specular.
    for value, prop, y in ((0.8, unreal.MaterialProperty.MP_ROUGHNESS, 200), (0.3, unreal.MaterialProperty.MP_SPECULAR, 300)):
        c = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -400, y)
        c.set_editor_property("r", value)
        mel.connect_material_property(c, "", prop)
    mel.recompile_material(m)
    lib.save_loaded_asset(m)


def import_kit(name, part):
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.skeleton = body_mesh("/Game/MetaHumans", name).skeleton
    ui.import_materials = ui.import_textures = ui.import_animations = False
    ui.create_physics_asset = False
    ui.skeletal_mesh_import_data.set_editor_property("import_morph_targets", False)
    t = unreal.AssetImportTask()
    t.filename = f"{DIR}/{name}_{part}.fbx"
    t.destination_path = f"/Game/MetaHumans/{name}/Kit"
    t.destination_name = f"SKM_{name}_{part}"
    t.automated = t.replace_existing = t.save = True
    t.options = ui
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    unreal.log(f"KIT {name}: {part} imported {list(t.imported_object_paths)}")


if STEP == "import":
    kit_material()
for n in NAMES:
    if STEP == "export":
        export(n)
    else:
        for part in ("Kit", "Gear"):
            import_kit(n, part)

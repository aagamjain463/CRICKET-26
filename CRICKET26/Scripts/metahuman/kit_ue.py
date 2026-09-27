# Unreal side of the cricket kit (see make_kit.sh).
#
# KIT_STEP=material: rebuilds the kit's fabric material in place, e.g. after changing it here.
# KIT_STEP=export: builds each player once more without the preset garment into /Game/MetaHumans/Bare (the
#   garment's hidden-face map cuts the skin under it out of the real build, and the kit needs the whole body),
#   and exports that body to $KIT_DIR/<Name>_Body.fbx. The source character is not saved, so it keeps its garment.
# KIT_STEP=import: imports $KIT_DIR/<Name>_<Part>.fbx for the parts Kit, Gear, Keeper and Hat onto the player's own
#   skeleton as /Game/MetaHumans/<Name>/Kit/SKM_<Name>_<Part>, and makes the fabric material
#   /Game/MetaHumans/Kit/M_Kit. It also imports the bat, $KIT_DIR/Bat.fbx, as /Game/MetaHumans/Kit/SM_Bat with its
#   willow material M_Bat.
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


TEX = "/MetaHumanCharacter/Optional/Clothing/Common/Materials/Textures/Clothing"
# Fabric normals from the MetaHuman garments: a knit weave tiled small and memory wrinkles tiled large over the body's UVs
# (the kit keeps them). Pads get vertical ribs by bump from the pre-skinned position (X across the shin, cm), which
# needs the normal in world space.
FABRIC = """float3 n = float3((K.xy * 0.6 + W.xy * 0.35) * Fabric, 1);
n = normalize(mul(normalize(n), (float3x3)Parameters.TangentToWorld));
float h = Ribs * 0.25 * sqrt(abs(sin(P.x * 3.14159 / 3.2)));
float3 dpdx = ddx(WP), dpdy = ddy(WP);
float3 r1 = cross(dpdy, n), r2 = cross(n, dpdx);
float det = dot(dpdx, r1);
return normalize(abs(det) * n - sign(det) * (ddx(h) * r1 + ddy(h) * r2));"""


def kit_material(rebuild=False):
    mel = unreal.MaterialEditingLibrary
    if lib.does_asset_exist("/Game/MetaHumans/Kit/M_Kit"):
        if not rebuild:
            return
        m = unreal.load_asset("/Game/MetaHumans/Kit/M_Kit")  # the kit meshes use it: rebuilt in place
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Kit", "/Game/MetaHumans/Kit", unreal.Material,
                                                                      unreal.MaterialFactoryNew())
    m.set_editor_property("used_with_skeletal_mesh", True)  # a game cannot compile the usage in later
    m.set_editor_property("tangent_space_normal", False)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLOTH)
    # Through attributes: the cloth amount's pin (custom data 0, "ClearCoat") is hidden from Python otherwise.
    m.set_editor_property("use_material_attributes", True)
    attrs = mel.create_material_expression(m, unreal.MaterialExpressionMakeMaterialAttributes, 0, 0)
    mel.connect_material_property(attrs, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)

    def out(e, pin):
        mel.connect_material_expressions(e, "", attrs, pin)

    def node(cls, x, y, **props):
        e = mel.create_material_expression(m, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def param(name, value, y):
        return node(unreal.MaterialExpressionScalarParameter, -1200, y, parameter_name=name, default_value=value)

    colour = node(unreal.MaterialExpressionVectorParameter, -400, 0, parameter_name="Color",
                  default_value=unreal.LinearColor(0.2, 0.3, 0.8, 1.0))
    out(colour, "BaseColor")
    fabric, ribs = param("Fabric", 1.0, 400), param("Ribs", 0.0, 500)
    uv = node(unreal.MaterialExpressionTextureCoordinate, -1400, 100)

    def tiled(tex, tile, y):
        scale = node(unreal.MaterialExpressionMultiply, -1100, y)
        scale.set_editor_property("const_b", tile)
        mel.connect_material_expressions(uv, "", scale, "A")
        t = node(unreal.MaterialExpressionTextureSample, -900, y, texture=unreal.load_asset(f"{TEX}/{tex}"),
                 sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        mel.connect_material_expressions(scale, "", t, "UVs")
        return t

    knit, wrinkle = tiled("Micros/micro_knit_front_N", 90.0, 0), tiled("Wrinkles/memory_wrinkles_normal", 3.0, 200)
    skinless = node(unreal.MaterialExpressionPreSkinnedPosition, -1200, 600)
    across = node(unreal.MaterialExpressionVertexInterpolator, -1000, 600)
    mel.connect_material_expressions(skinless, "", across, "")
    wp = node(unreal.MaterialExpressionWorldPosition, -1000, 700,
              world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    names = ["K", "W", "P", "WP", "Fabric", "Ribs"]
    code = node(unreal.MaterialExpressionCustom, -500, 300, code=FABRIC,
                output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for n in names:
        i = unreal.CustomInput()
        i.set_editor_property("input_name", n)
        inputs.append(i)
    code.set_editor_property("inputs", inputs)
    for n, e in zip(names, (knit, wrinkle, across, wp, fabric, ribs)):
        mel.connect_material_expressions(e, "", code, n)
    out(code, "Normal")
    # Shade in the grooves between the ribs.
    groove = node(unreal.MaterialExpressionCustom, -500, 800, code="return 1 - Ribs * 0.45 * (1 - sqrt(abs(sin(P.x * 3.14159 / 3.2))));",
                  output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    gi = []
    for n in ("P", "Ribs"):
        i = unreal.CustomInput()
        i.set_editor_property("input_name", n)
        gi.append(i)
    groove.set_editor_property("inputs", gi)
    mel.connect_material_expressions(across, "", groove, "P")
    mel.connect_material_expressions(ribs, "", groove, "Ribs")
    out(groove, "AmbientOcclusion")
    # Cloth : a soft sheen at grazing angles, lighter than the dye; none on the helmet's shell.
    fuzz = node(unreal.MaterialExpressionAdd, -300, 100, const_b=0.08)
    mel.connect_material_expressions(colour, "", fuzz, "A")
    out(fuzz, "SubsurfaceColor")
    cloth = node(unreal.MaterialExpressionMultiply, -300, 450, const_b=0.7)
    mel.connect_material_expressions(fabric, "", cloth, "A")
    out(cloth, "ClearCoat")
    rough = node(unreal.MaterialExpressionLinearInterpolate, -300, 550, const_a=0.35, const_b=0.85)
    mel.connect_material_expressions(fabric, "", rough, "Alpha")
    out(rough, "Roughness")
    mel.recompile_material(m)
    lib.save_loaded_asset(m)
    unreal.log(f"KIT material rebuilt, {mel.get_num_material_expressions(m)} expressions")


def bat_material():
    if lib.does_asset_exist("/Game/MetaHumans/Kit/M_Bat"):
        return
    mel = unreal.MaterialEditingLibrary
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Bat", "/Game/MetaHumans/Kit", unreal.Material,
                                                                  unreal.MaterialFactoryNew())
    # Pale willow with darker grain lines running down the blade, a few wavering, from the mesh's own position
    # (cm, X across the blade, Z along it), so the bat needs no texture.
    grain = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -400, 0)
    grain.set_editor_property("code", "float g = sin(P.x * 5.3 + sin(P.z * 0.09) * 1.4 + sin(P.x * 1.7) * 2.0);\n"
                                      "g = pow(saturate(0.5 + 0.5 * g), 6) + 0.35 * pow(saturate(0.5 + 0.5 * sin(P.x * 13.1 + P.z * 0.02)), 8);\n"
                                      "return lerp(float3(0.80, 0.63, 0.40), float3(0.58, 0.40, 0.21), saturate(g));")
    grain.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    p = unreal.CustomInput()
    p.set_editor_property("input_name", "P")
    grain.set_editor_property("inputs", [p])
    pos = mel.create_material_expression(m, unreal.MaterialExpressionLocalPosition, -700, 0)
    mel.connect_material_expressions(pos, "", grain, "P")
    mel.connect_material_property(grain, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -400, 200)
    rough.set_editor_property("r", 0.45)  # oiled and polished
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    lib.save_loaded_asset(m)


def import_bat():
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    ui.import_materials = ui.import_textures = ui.import_animations = False
    ui.static_mesh_import_data.set_editor_property("combine_meshes", True)
    ui.static_mesh_import_data.set_editor_property("auto_generate_collision", False)
    t = unreal.AssetImportTask()
    t.filename = f"{DIR}/Bat.fbx"
    t.destination_path = "/Game/MetaHumans/Kit"
    t.destination_name = "SM_Bat"
    t.automated = t.replace_existing = t.save = True
    t.options = ui
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    b = unreal.load_asset("/Game/MetaHumans/Kit/SM_Bat").get_bounding_box()
    unreal.log(f"KIT bat imported, bounds {b.min} to {b.max}")


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


if STEP == "material":  # KIT_STEP=material: rebuilds M_Kit alone
    kit_material(rebuild=True)
    NAMES = []
if STEP == "keeper":
    for n in NAMES:
        import_kit(n, "Keeper")
    NAMES = []
if STEP == "gear":
    for n in NAMES:
        import_kit(n, "Gear")
    NAMES = []
if STEP == "face":  # KIT_STEP=face: exports each face mesh, which make_kit.py fits the hat's crown over
    for n in NAMES:
        t = unreal.AssetExportTask()
        t.object = unreal.load_asset(f"/Game/MetaHumans/{n}/Face/SKM_{n}_FaceMesh")
        t.filename = f"{DIR}/{n}_Face.fbx"
        t.automated = t.replace_identical = True
        t.prompt = False
        o = unreal.FbxExportOption()
        o.level_of_detail = o.collision = False
        t.options = o
        unreal.log(f"KIT {n}: face exported {unreal.Exporter.run_asset_export_task(t)}")
    NAMES = []
if STEP == "hat":
    for n in NAMES:
        import_kit(n, "Hat")
    NAMES = []
if STEP == "import":
    kit_material()
    bat_material()
    import_bat()
for n in NAMES:
    if STEP == "export":
        export(n)
    else:
        for part in ("Kit", "Gear", "Keeper", "Hat"):
            import_kit(n, part)

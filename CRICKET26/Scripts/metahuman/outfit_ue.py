# Dresses the MetaHuman players in Epic's free parametric MetaHuman outfits (Fab .mhpkg packages, Standard License):
# a tucked long-sleeve T-shirt, slim trousers and running shoes. Parametric garments resize to each body in MetaHuman
# Creator, so they fit with real folds, seams and a collar, where the Blender-cut kit (make_kit.py) is a smooth shell.
#
# Two steps, both headless:
#   OUTFIT_PKGS="a.mhpkg:b.mhpkg"   imports the packages into /Game/MetaHumans/Outfits (their WI_* wardrobe items).
#   OUTFIT_WEAR="MH_Home_Opener ..."  swaps each named character's outfit for OUTFIT_ITEMS and rebuilds it in place.
#     The character's rig and textures are already saved in /Game/MetaHumans/Source, so no cloud step runs.
#   OUTFIT_PRINT="MH_Home_Opener ..."  turns on the shirt's print (the name, number and sponsor the game draws into it,
#     SuperOverGameMode::ShirtPrint). A static switch, so it can only be set in the editor.
#
# Run: OUTFIT_WEAR=MH_Home_Opener UnrealEditor-Cmd CRICKET26.uproject -run=pythonscript -script=Scripts/metahuman/outfit_ue.py \
#        -AllowCommandletRendering -dpcvars=TextureGraph.AllowCommandlets=1
import os
import unreal

DEST = "/Game/MetaHumans/Outfits"
BUILD = "/Game/MetaHumans"
ITEMS = os.environ.get("OUTFIT_ITEMS", "WI_OA_TshirtTkLngSlv WI_OA_Jeans_slm WI_OA_RunningShoes").split()
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

for path in filter(None, os.environ.get("OUTFIT_PKGS", "").split(":")):
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = DEST
    t.automated = t.replace_existing = t.save = True
    tools.import_asset_tasks([t])
    unreal.log(f"OUTFIT imported {os.path.basename(path)}: {list(t.imported_object_paths)}")
lib.save_directory(DEST, only_if_is_dirty=True, recursive=True)


def wear(name):
    sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
    c = unreal.load_asset(f"{BUILD}/Source/{name}")
    if not sub.try_add_object_to_edit(c):
        unreal.log_error(f"OUTFIT {name}: cannot open for editing")
        return
    try:
        col = c.get_editor_property("internal_collection")
        inst = col.get_editor_property("default_instance")
        inst.set_single_slot_selection("Outfits", unreal.MetaHumanPaletteItemKey())
        for item in ITEMS:
            wi = unreal.load_asset(f"{DEST}/{item}")
            if not wi:
                unreal.log_warning(f"OUTFIT {name}: no {item}, skipped")
                continue
            keys = col.get_item_keys_for_wardrobe_item(wi)
            key = keys[0] if keys else col.try_add_item_from_wardrobe_item("Outfits", wi)  # the key, or None
            sel = unreal.MetaHumanPipelineSlotSelection(slot_name="Outfits", selected_item=key)
            unreal.log(f"OUTFIT {name}: {item} selected {key is not None and inst.try_add_slot_selection(sel)}")
        lib.save_loaded_asset(c)
        p = unreal.MetaHumanCharacterEditorBuildParameters()
        p.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
        p.pipeline_quality = unreal.MetaHumanQualityLevel.MEDIUM
        p.absolute_build_path = BUILD
        p.common_folder_path = f"{BUILD}/Common"
        p.enable_wardrobe_item_validation = False
        sub.build_meta_human(character=c, params=p)
        lib.save_directory(f"{BUILD}/{name}", only_if_is_dirty=True, recursive=True)
        lib.save_directory(f"{BUILD}/Common", only_if_is_dirty=True, recursive=True)
        unreal.log(f"OUTFIT {name}: built")
    finally:
        sub.remove_object_to_edit(c)


for name in os.environ.get("OUTFIT_WEAR", "").split():
    wear(name)


for name in os.environ.get("OUTFIT_PRINT", "").split():
    for path in lib.list_assets(f"{BUILD}/{name}/Clothing", recursive=False):
        if ("Tshirt" in path or "Crewneckt" in path) and "/MI_" in path:
            mi = unreal.load_asset(path)
            # Returns False even when it sets the switch, so the value is read back instead.
            mel = unreal.MaterialEditingLibrary
            mel.set_material_instance_static_switch_parameter_value(mi, "bDoPrintGraphic", True)
            lib.save_loaded_asset(mi)
            on = mel.get_material_instance_static_switch_parameter_value(mi, "bDoPrintGraphic")
            unreal.log(f"OUTFIT {name}: print on {mi.get_name()} {on}")

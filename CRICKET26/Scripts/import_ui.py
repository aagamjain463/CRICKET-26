import unreal

for name in ("Hero_Batter_v2", "Franchise_Stadium", "Stadium_Night", "Cutout_Batter", "Cutout_Bowler", "Cutout_Allrounder", "Cutout_Finisher", "Cutout_Spinner", "Card_Gold", "Card_Elite", "Streaks", "Shine",
             "Shell_Bg", "VS_Bg", "Tile_Gold", "Tile_Royal", "Tile_Ember", "Tile_Night", "Crest_Home", "Crest_Away", "Ovr_Badge", "Ring", "Logo"):
    source = unreal.Paths.project_content_dir() + f"UI/Source/CRICKET26_{name}.png"
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = "/Game/UI"
    task.destination_name = f"T_CRICKET26_{name}"
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(f"/Game/UI/T_CRICKET26_{name}")
    if not texture:
        raise RuntimeError(f"UI artwork import failed: {name}")
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    unreal.EditorAssetLibrary.save_loaded_asset(texture)

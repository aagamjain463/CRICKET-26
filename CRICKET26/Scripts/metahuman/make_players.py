# Builds the game's MetaHuman players from the MetaHuman Creator presets that ship with the engine.
#
# Each squad player (ASuperOverGameMode::DefaultSquads) and each umpire starts from a different preset, wearing
# the preset's default garment, is face-rigged and textured by Epic's MetaHuman cloud service, and is assembled
# with the Optimized pipeline, which gives game-ready LODs. The game loads the result from
# /Game/MetaHumans/<Name> and paints the garment in the team colour.
#
# Licence: MetaHuman presets and the characters made from them are covered by the MetaHuman licence (with the
# Unreal Engine EULA for use in an Unreal Engine project), garment and hair included. Nothing here is
# a real player's likeness.
#
# The cloud steps need an Epic account signed in to the MetaHuman service: the first run opens the Epic login
# page in the browser, and the owner of the account has to sign in there. Later runs reuse the saved login.
# Each character takes about 5 minutes of cloud rigging and 2 to 6 of building.
#
# Run: Scripts/metahuman/make_players.sh   (skips characters that are already built)
#      MHMAKE_ONLY=MH_Away_Hitter Scripts/metahuman/make_players.sh   (one character)
import os
import unreal

# (asset name, preset)
PLAYERS = [
    ("MH_Home_Opener", "Isaiah"),
    ("MH_Home_Finisher", "Jorge"),
    ("MH_Home_Allrounder", "Mateo"),
    ("MH_Home_Quick", "Bruce"),
    ("MH_Away_Hitter", "Omari"),
    ("MH_Away_Anchor", "Kelvin"),
    ("MH_Away_KeeperBat", "Lorenzo"),
    ("MH_Away_WristSpinner", "Dominic"),
    ("MH_Umpire_1", "Walter"),
    ("MH_Umpire_2", "Victor"),
]
SOURCE = "/Game/MetaHumans/Source"
BUILD = "/Game/MetaHumans"

sub = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def prepare(character):
    # A build needs both the face rig and the high resolution textures, and can_build_meta_human is the only
    # scriptable check of either, so textures come first and the rig is requested only while it still fails.
    # Both come from Epic's cloud and a dropped connection loses the download, so each attempt starts fresh
    # jobs; three cover a flaky network without hammering the service.
    for attempt in range(3):
        if not character.has_high_resolution_textures:
            tex = unreal.MetaHumanCharacterTextureRequestParams()
            tex.blocking = True
            tex.report_progress = False
            sub.request_texture_sources(character, tex)
        if sub.can_build_meta_human(character, False):
            return True
        params = unreal.MetaHumanCharacterAutoRiggingRequestParams()
        params.blocking = True
        params.report_progress = False
        params.rig_type = unreal.MetaHumanRigType.JOINTS_ONLY  # no blend shapes: the cheap face rig a game needs
        sub.request_auto_rigging(character, params)
        if sub.can_build_meta_human(character, False):
            return True
        unreal.log_warning(f"MHMAKE {character.get_name()}: attempt {attempt + 1} to rig and texture failed")
    return False


def make(name, preset):
    if lib.does_directory_exist(f"{BUILD}/{name}"):
        unreal.log(f"MHMAKE {name}: already built")
        return True
    path = f"{SOURCE}/{name}"
    fresh = not lib.does_asset_exist(path)
    character = tools.duplicate_asset(name, SOURCE, unreal.load_asset(
        f"/MetaHumanCharacter/Optional/Presets/{preset}.{preset}")) if fresh else unreal.load_asset(path)
    if not sub.try_add_object_to_edit(character):
        unreal.log_error(f"MHMAKE {name}: cannot open for editing")
        return False
    try:
        ready = prepare(character)
        lib.save_loaded_asset(character)  # keeps a finished rig and textures even if the build fails
        if not ready or not sub.can_build_meta_human(character, True):
            return False
        params = unreal.MetaHumanCharacterEditorBuildParameters()
        params.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
        params.pipeline_quality = unreal.MetaHumanQualityLevel.MEDIUM
        params.absolute_build_path = BUILD
        params.common_folder_path = f"{BUILD}/Common"
        params.enable_wardrobe_item_validation = False
        sub.build_meta_human(character=character, params=params)
        # A commandlet exits without saving what the build made.
        lib.save_directory(BUILD, only_if_is_dirty=True, recursive=True)
        if not lib.does_directory_exist(f"{BUILD}/{name}"):
            unreal.log_error(f"MHMAKE {name}: the build made nothing")
            return False
        unreal.log(f"MHMAKE {name}: built")
        return True
    finally:
        sub.remove_object_to_edit(character)


chosen = [p for p in PLAYERS if os.environ.get("MHMAKE_ONLY", p[0]) == p[0]]
built = sum(make(*p) for p in chosen)
unreal.log(f"MHMAKE done: {built} of {len(chosen)} players ready")

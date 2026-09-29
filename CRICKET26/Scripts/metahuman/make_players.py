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
#      MHMAKE_REBUILD="MH_Home_Opener ..." Scripts/metahuman/make_players.sh   (fresh rebuild after a preset change)
import hashlib
import os
import unreal

# (asset name, preset, skin tone, hair, height in metres). Every player and umpire looks Indian: faces from the
# presets that read South Asian under a medium brown skin, and tones in the Indian band of MetaHuman Creator's
# skin-tone chart (U 0.46 to 0.56, light to medium brown, never the darkest). U runs from light to dark (the
# presets span 0.2 to 0.95) and V is the undertone. The stadium's sun and sky read a tone a little darker than
# the Creator's studio does. physique() below keeps every body an athletic medium (never bulky, never lean) at
# the listed height; the quick is tall, the spinner shortest, as real sides are. Hair is a MetaHuman groom
# (WI_Hair_*); short sport cuts only, explicitly locked so no preset's long cut
# (PulledBack, Layered, BobLayered, HairLoss) can slip back in. Beards are in
# SPORT_BEARD below: boxed full, short stubble or clean — the three looks real
# squads wear — never PencilThin, MuttonChops, ChinStrap or SoulpatchStrip.
PLAYERS = [
    ("MH_Home_Opener", "Mateo", (0.50, 0.70), "WI_Hair_S_Casual", 1.78),
    ("MH_Home_Finisher", "Jorge", (0.48, 0.76), "WI_Hair_S_BrushCut", 1.75),
    ("MH_Home_Allrounder", "Dominic", (0.52, 0.72), "WI_Hair_S_Casual", 1.82),
    ("MH_Home_Quick", "Trey", (0.54, 0.66), "WI_Hair_S_BuzzCut", 1.90),
    ("MH_Away_Hitter", "Orlando", (0.50, 0.68), "WI_Hair_S_BrushCut", 1.84),
    ("MH_Away_Anchor", "Victor", (0.46, 0.70), "WI_Hair_S_CurlyFade", 1.78),
    ("MH_Away_KeeperBat", "Mikel", (0.48, 0.66), "WI_Hair_S_Casual", 1.76),
    ("MH_Away_WristSpinner", "Lorenzo", (0.54, 0.72), "WI_Hair_S_Clean", 1.74),
    ("MH_Umpire_1", "Walter", (0.48, 0.70), "WI_Hair_S_BrushCut", 1.80),
    ("MH_Umpire_2", "Kelvin", (0.52, 0.68), "WI_Hair_S_BrushCut", 1.78),
]
# (beard, mustache) wardrobe items per identity; None keeps the preset's own.
# Stubble reads as five-o'clock rugged at match distance; S_Full is the boxed
# short beard modern squads wear; clean stays clean for variety. Umpire 1 keeps
# his distinguished grey wavy set.
SPORT_BEARD = {
    "MH_Home_Opener": ("WI_Beard_S_Full", "WI_Mustache_S_Full"),
    "MH_Home_Finisher": (None, None),
    "MH_Home_Allrounder": ("WI_Beard_S_Stubble", "WI_Mustache_S_Stubble"),
    "MH_Home_Quick": ("WI_Beard_S_Stubble", "WI_Mustache_S_Stubble"),
    "MH_Away_Hitter": ("WI_Beard_S_Stubble", "WI_Mustache_S_Stubble"),
    "MH_Away_Anchor": ("WI_Beard_S_Full", "WI_Mustache_S_Stubble"),
    "MH_Away_KeeperBat": ("WI_Beard_S_Stubble", "WI_Mustache_S_Stubble"),
    "MH_Away_WristSpinner": ("WI_Beard_S_Stubble", "WI_Mustache_S_Stubble"),
    "MH_Umpire_1": (None, None),
    "MH_Umpire_2": ("WI_Beard_S_Stubble", "WI_Mustache_S_Stubble"),
}
# The auction room: the auctioneer at the podium and the franchise staff at the tables (MHMAKE_SET=auction). They
# are dressed afterwards by outfit_ue.py (OUTFIT_WEAR), which the game tints in each franchise's colour.
AUCTION = [
    ("MH_Auctioneer", "Sunita", (0.55, 0.80), None, 1.62),
    ("MH_Staff_Asha", "Asha", (0.54, 0.62), "WI_Hair_L_Straight", 1.60),
    ("MH_Staff_Mateo", "Mateo", (0.52, 0.72), None, 1.78),
    ("MH_Staff_Orlando", "Orlando", (0.48, 0.70), None, 1.80),
    ("MH_Staff_Trey", "Trey", (0.54, 0.66), "WI_Hair_S_Casual", 1.85),
    ("MH_Staff_Jorge", "Jorge", (0.47, 0.78), "WI_Hair_S_SideSweptFringe", 1.76),
    ("MH_Staff_Dominic", "Dominic", (0.56, 0.74), None, 1.79),
    ("MH_Staff_Grace", "Grace", (0.48, 0.64), None, 1.64),
    ("MH_Staff_Vivian", "Vivian", (0.46, 0.72), None, 1.66),
    ("MH_Staff_Celeste", "Celeste", (0.52, 0.76), None, 1.62),
    ("MH_Staff_Lorenzo", "Lorenzo", (0.50, 0.68), "WI_Hair_S_Clean", 1.77),
    ("MH_Staff_Jelena", "Jelena", (0.47, 0.70), "WI_Hair_S_LowPonytail", 1.65),
]
if os.environ.get("MHMAKE_SET") == "auction":
    PLAYERS = AUCTION
SOURCE = "/Game/MetaHumans/Source"
BUILD = "/Game/MetaHumans"
GROOMS = "/MetaHumanCharacter/Optional/Grooms/Bindings/Hair"
BEARDS = "/MetaHumanCharacter/Optional/Grooms/Bindings/Beards"
MUSTACHES = "/MetaHumanCharacter/Optional/Grooms/Bindings/Mustaches"
OUTFITS = "/Game/MetaHumans/Outfits"
# Epic's free parametric outfit (outfit_ue.py imports it), fitted to each body with real folds: a short-sleeved
# crew-neck shirt for players, a long-sleeved one for umpires, slim trousers and running shoes. The game paints
# them as the kit (SuperOverGameMode::TintOutfit). The auction room is dressed by outfit_ue.py instead.
KIT = ["WI_OA_CrewnecktTk", "WI_OA_Jeans_slm", "WI_OA_Runningshoes"]
UMPIRE_KIT = ["WI_OA_TshirtTkLngSlv", "WI_OA_Jeans_slm", "WI_OA_Runningshoes"]
# The iris of Epic's Dominic preset, a dark brown, for every face: several presets have hazel or grey-green eyes,
# and a custom sclera tint or panda-smudge eye make-up read as yellow eyes and bruised sockets under the sun.
BROWN_IRIS = dict(pattern=unreal.MetaHumanCharacterEyesIrisPattern.IRIS006, primary_color_u=0.9, primary_color_v=0.56,
                  secondary_color_u=0.98, secondary_color_v=0.4, global_tint=unreal.LinearColor(0.14, 0.104, 0.096, 1.0))

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


def built(name):
    # The skin is baked last: a build killed before it (out of memory, say) leaves the folder, blueprint and meshes
    # of a character with a grey face and body, so the folder alone does not mean it was built.
    return all(lib.does_asset_exist(f"{BUILD}/{name}/{part}") for part in ("Face/Baked/T_Head_LOD3_BC_VT", "Body/Baked/T_Body_BC_VT"))


def stale(name):
    """Whether the character was saved after its skin was baked, so the build lags its settings."""
    content = unreal.Paths.project_content_dir()
    source = os.path.join(content, "MetaHumans", "Source", name + ".uasset")
    baked = os.path.join(content, "MetaHumans", name, "Body", "Baked", "T_Body_BC_VT.uasset")
    return os.path.getmtime(source) > os.path.getmtime(baked)


def physique(character, name, height_m):
    """Athletic medium body at height_m: target height plus mid-range girths (never bulky, never lean).

    Girth targets sit near the middle of each constraint's range with a small deterministic per-player
    variation (fraction 0.45 to 0.55), so bodies read as fit athletes without cloning each other. Returns
    whether anything changed. Defensive: unknown constraint names are left alone, and a missing body API
    keeps the preset's body with a warning instead of failing the build.
    """
    try:
        cons = sub.get_body_constraints(character)
    except Exception as e:
        unreal.log_warning(f"MHMAKE {name}: cannot read body constraints ({e}), keeping preset body")
        return False
    if not cons:
        return False
    frac = 0.45 + (int(hashlib.md5(name.encode()).hexdigest()[:4], 16) % 11) / 100.0
    changed = False
    for c in cons:
        try:
            label = str(c.get_editor_property("name"))
        except Exception:
            continue
        low = label.lower()
        try:
            target = float(c.get_editor_property("target_measurement"))
            mn = float(c.get_editor_property("min_measurement"))
            mx = float(c.get_editor_property("max_measurement"))
        except Exception:
            continue
        if mx <= mn:
            continue
        want = None
        if "height" in low or "stature" in low:
            want = height_m
        elif any(k in low for k in ("weight", "bulk", "girth", "chest", "waist", "hip", "thigh", "calf",
                                    "upperarm", "forearm", "shoulder", "muscle", "fat", "bmi", "belly")):
            want = mn + frac * (mx - mn)
        if want is None:
            continue
        want = max(mn, min(mx, want))
        if abs(target - want) > 1e-4:
            c.set_editor_property("target_measurement", want)
            try:
                c.set_editor_property("is_active", True)
            except Exception:
                pass
            changed = True
    if changed:
        try:
            sub.set_body_constraints(character, cons)
            sub.commit_body_state(character)
        except Exception as e:
            unreal.log_warning(f"MHMAKE {name}: could not commit body ({e})")
            return False
    return changed


def tone(character, uv):
    """Sets the skin to the (U, V) point on the skin-tone chart; returns whether it changed."""
    settings = character.get_editor_property("skin_settings")
    skin = settings.get_editor_property("skin")
    if abs(skin.u - uv[0]) < 1e-3 and abs(skin.v - uv[1]) < 1e-3:
        return False
    skin.u, skin.v = uv
    settings.set_editor_property("skin", skin)
    sub.commit_skin_settings(character, settings)
    return True


def style(character, hair):
    """Selects the hair groom (None keeps the preset's); returns whether it changed."""
    if not hair:
        return False
    col = character.get_editor_property("internal_collection")
    inst = col.get_editor_property("default_instance")
    wi = unreal.load_asset(f"{GROOMS}/{hair}")
    keys = col.get_item_keys_for_wardrobe_item(wi)
    key = keys[0] if keys else col.try_add_item_from_wardrobe_item("Hair", wi)
    now = [d.selection.selected_item for d in inst.get_slot_selection_data() if str(d.selection.slot_name) == "Hair"]
    if now and now[0].export_text() == key.export_text():
        return False
    inst.set_single_slot_selection("Hair", key)
    return True


def facial(character, name):
    """Selects the sport beard/mustache from SPORT_BEARD (None keeps preset's)."""
    pair = SPORT_BEARD.get(name, (None, None))
    changed = False
    for slot, folder, item in (("Beard", BEARDS, pair[0]), ("Mustache", MUSTACHES, pair[1])):
        if not item:
            continue
        try:
            col = character.get_editor_property("internal_collection")
            inst = col.get_editor_property("default_instance")
            wi = unreal.load_asset(f"{folder}/{item}")
            if wi is None:
                unreal.log_warning(f"MHMAKE {name}: missing wardrobe item {item}")
                continue
            keys = col.get_item_keys_for_wardrobe_item(wi)
            key = keys[0] if keys else col.try_add_item_from_wardrobe_item(slot, wi)
            now = [d.selection.selected_item for d in inst.get_slot_selection_data()
                   if str(d.selection.slot_name) == slot]
            if now and now[0].export_text() == key.export_text():
                continue
            inst.set_single_slot_selection(slot, key)
            changed = True
        except Exception as e:
            unreal.log_warning(f"MHMAKE {name}: cannot set {slot} {item} ({e})")
    return changed


def eyes(character):
    """Dark brown irises, a natural sclera and no eye make-up; returns whether anything changed."""
    changed = False
    settings = character.get_editor_property("eyes_settings")
    for side in ("eye_left", "eye_right"):
        eye = settings.get_editor_property(side)
        iris = eye.get_editor_property("iris")
        for field, value in BROWN_IRIS.items():
            now = iris.get_editor_property(field)
            same = now == value if not isinstance(value, float) else abs(now - value) < 1e-3
            if not same:
                iris.set_editor_property(field, value)
                changed = True
        sclera = eye.get_editor_property("sclera")
        if sclera.get_editor_property("use_custom_tint"):
            sclera.set_editor_property("use_custom_tint", False)
            eye.set_editor_property("sclera", sclera)
            changed = True
        eye.set_editor_property("iris", iris)
        settings.set_editor_property(side, eye)
    if changed:
        sub.commit_eyes_settings(character, settings)
    makeup = character.get_editor_property("makeup_settings")
    eye_makeup = makeup.get_editor_property("eyes")
    if eye_makeup.get_editor_property("type") != unreal.MetaHumanCharacterEyeMakeupType.NONE:
        eye_makeup.set_editor_property("type", unreal.MetaHumanCharacterEyeMakeupType.NONE)
        makeup.set_editor_property("eyes", eye_makeup)
        sub.commit_makeup_settings(character, makeup)
        changed = True
    return changed


def dress(character, items):
    """Selects the outfit's wardrobe items (none keeps the preset garment); returns whether it changed."""
    if not items:
        return False
    col = character.get_editor_property("internal_collection")
    inst = col.get_editor_property("default_instance")
    keys = []
    for item in items:
        wi = unreal.load_asset(f"{OUTFITS}/{item}")
        found = col.get_item_keys_for_wardrobe_item(wi)
        keys.append(found[0] if found else col.try_add_item_from_wardrobe_item("Outfits", wi))
    now = {d.selection.selected_item.export_text() for d in inst.get_slot_selection_data() if str(d.selection.slot_name) == "Outfits"}
    if now == {k.export_text() for k in keys}:
        return False
    inst.set_single_slot_selection("Outfits", unreal.MetaHumanPaletteItemKey())
    for key in keys:
        inst.try_add_slot_selection(unreal.MetaHumanPipelineSlotSelection(slot_name="Outfits", selected_item=key))
    return True


def look(character, name, uv, hair, height_m):
    changed = tone(character, uv)
    changed = physique(character, name, height_m) or changed
    changed = eyes(character) or changed
    changed = facial(character, name) or changed
    if name not in {a[0] for a in AUCTION}:
        changed = dress(character, UMPIRE_KIT if name.startswith("MH_Umpire") else KIT) or changed
    return style(character, hair) or changed


# A preset change in PLAYERS above does not alter an already-duplicated Source asset, so those players
# need a fresh rebuild: MHMAKE_REBUILD="MH_Home_Opener MH_Home_Allrounder ..." Scripts/metahuman/make_players.sh
# (MHMAKE_FRESH=1 rebuilds everyone). Tone, hair, beard and physique changes are picked up in place.
REBUILD = set(filter(None, os.environ.get("MHMAKE_REBUILD", "").replace(",", " ").split()))
if os.environ.get("MHMAKE_FRESH"):
    REBUILD |= {p[0] for p in PLAYERS}


def make(name, preset, uv, hair, height_m):
    path = f"{SOURCE}/{name}"
    if name in REBUILD:
        if lib.does_directory_exist(f"{BUILD}/{name}"):
            lib.delete_directory(f"{BUILD}/{name}")
        if lib.does_asset_exist(path):
            lib.delete_asset(path)
        unreal.log(f"MHMAKE {name}: preset changed, rebuilding fresh from {preset}")
    fresh = not lib.does_asset_exist(path)
    if not fresh and built(name):
        # Built already: rebuilt in place only if its skin tone, hair or physique changed, which keeps an outfit or kit added since.
        character = unreal.load_asset(path)
        if not sub.try_add_object_to_edit(character):
            unreal.log_error(f"MHMAKE {name}: cannot open for editing")
            return False
        try:
            # A tone saved by a run whose build then failed leaves the source newer than the baked skin.
            if not look(character, name, uv, hair, height_m) and not stale(name):
                unreal.log(f"MHMAKE {name}: already built")
                return True
            # A new tone drops the textures, which come from the cloud again.
            ready = prepare(character)
            lib.save_loaded_asset(character)
            return ready and build(name, character)
        finally:
            sub.remove_object_to_edit(character)
    if lib.does_directory_exist(f"{BUILD}/{name}"):
        unreal.log_warning(f"MHMAKE {name}: an unfinished build, rebuilding")
        lib.delete_directory(f"{BUILD}/{name}")
    character = tools.duplicate_asset(name, SOURCE, unreal.load_asset(
        f"/MetaHumanCharacter/Optional/Presets/{preset}.{preset}")) if fresh else unreal.load_asset(path)
    if not sub.try_add_object_to_edit(character):
        unreal.log_error(f"MHMAKE {name}: cannot open for editing")
        return False
    try:
        look(character, name, uv, hair, height_m)
        ready = prepare(character)
        lib.save_loaded_asset(character)  # keeps a finished rig and textures even if the build fails
        return ready and build(name, character)
    finally:
        sub.remove_object_to_edit(character)


def build(name, character):
    if not sub.can_build_meta_human(character, True):
        return False
    params = unreal.MetaHumanCharacterEditorBuildParameters()
    params.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
    params.pipeline_quality = unreal.MetaHumanQualityLevel.MEDIUM
    params.absolute_build_path = BUILD
    params.common_folder_path = f"{BUILD}/Common"
    params.enable_wardrobe_item_validation = False
    sub.build_meta_human(character=character, params=params)
    # The shirt's print (the kit's panels, name, number and sponsor, SuperOverGameMode::ShirtPrint) is a static
    # switch the build resets, so it is turned on again here.
    mel = unreal.MaterialEditingLibrary
    for path in lib.list_assets(f"{BUILD}/{name}/Clothing", recursive=False):
        if ("Tshirt" in path or "Crewneckt" in path) and "/MI_" in path:
            mel.set_material_instance_static_switch_parameter_value(unreal.load_asset(path), "bDoPrintGraphic", True)
    # A commandlet exits without saving what the build made.
    lib.save_directory(BUILD, only_if_is_dirty=True, recursive=True)
    if not built(name):
        unreal.log_error(f"MHMAKE {name}: the build did not finish")
        return False
    unreal.log(f"MHMAKE {name}: built")
    return True


chosen = [p for p in PLAYERS if os.environ.get("MHMAKE_ONLY", p[0]) == p[0]]
ready = sum(make(*p) for p in chosen)
unreal.log(f"MHMAKE done: {ready} of {len(chosen)} players ready")

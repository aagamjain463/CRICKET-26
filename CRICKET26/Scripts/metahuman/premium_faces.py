"""Premium face overhaul for CRICKET 26 player + umpire MetaHumans.

What it does (all reversible, no geometry rebuild, no cloud needed):
  - Skin: restores pore microdetail, tightens highlights, warms subsurface, deepens
    contact AO so close-ups read as real athletes instead of wax.
  - Eyes: removes milky cloudy layer, natural daylight pupil, calms neon iris
    saturation, cuts green turf transmission, sharpens catchlight. This fixes the
    glowing-green-eyes seen in close captures.
  - Lashes/brows/beard/scalp: near-black Indian hair melanin, natural roughness.
    Umpires keep their distinguished grey (only roughness touched).
  - Teeth: cleaner premium value, less plastic specular, more micro detail.
  - Eye shell: deeper socket shadow so eyes sit in the face.
  - LODSync: bakes dense card LOD 2 for Hair/Beard/Mustache/Eyebrows close-ups
    into every blueprint (runtime already does this live; baking makes captures
    and packaged builds identical).

Run:
  PREMIUM_FACES=1 /Users/Shared/Epic\\ Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd \
    "$ROOT/CRICKET26.uproject" -run=pythonscript -script="$ROOT/Scripts/metahuman/premium_faces.py" \
    -unattended -nosplash -NoZen -nosound -abslog="$ROOT/Saved/PremiumFaces.log"

  PREMIUM_FACES=DRYRUN ... for a read-only audit (no saves).

Progress lines start with PREMIUM_FACE. Exit is non-zero on any hard failure.
Licence: touches only project MetaHuman instances (MetaHuman licence, UE EULA).
No real-player likeness is introduced; presets remain the source.
"""

import os
import sys
import unreal

DRYRUN = os.environ.get("PREMIUM_FACES", "1") == "DRYRUN"

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary

NAMES = [
    "MH_Home_Opener", "MH_Home_Finisher", "MH_Home_Allrounder", "MH_Home_Quick",
    "MH_Away_Hitter", "MH_Away_Anchor", "MH_Away_KeeperBat", "MH_Away_WristSpinner",
    "MH_Umpire_1", "MH_Umpire_2",
]
UMPIRES = {"MH_Umpire_1", "MH_Umpire_2"}

# --- Premium skin: close-up pore detail, tight sun/floodlight response, warm blood, deep creases.
SKIN_SCALARS = {
    "Micro Skin Normal Strength": 1.0,   # was 0.7: pores visible at 1.5 m close-ups
    "Micro Skin Tiling": 36.0,           # was 44: pores resolve instead of vanishing
    "Micro Skin Roughess": 0.95,         # was 1.05: tighter athletic sheen, not gloss
    "Normal Global Strength Post-Bake": 1.15,  # was 1.0: keeps baked facial structure
    "Normal Flatten": 0.25,              # was 0.333: less flattening of baked normals
    "Spec Adjust": 1.0,                  # was 0.9: lively sun/floodlight response
    "Roughness Adjust": 0.92,            # was 0.95: subtle premium skin tightness
    "Fake SSS Strength": 0.9,            # was 0.8: warm translucency, less wax
    "Material AO Power": 1.15,           # was 1.0: deeper sockets, nostrils, lips
    "Fake AO Strength": 1.1,             # was 1.0: grounds eyes/mouth at close range
}

# --- Premium eyes: the green-glow fix. Cloudy 2.0 made Opener milky; dilation 0.95
# hid the iris; saturation 1.7 went neon under sun; transmission bled turf green.
EYE_SCALARS = {
    "Cloudy Eye Intensity": 0.35,
    "Pupil Dilation": 0.45,              # natural daylight pupil, iris reads
    "Iris Global Saturation": 1.15,      # was 1.7: natural dark brown, not neon
    "Iris Shadow Details Amount": 0.9,   # was 0.75: iris fibre detail
    "Sclera Transmission Spread": 0.08,  # was 0.12: less turf-green bleed
    "Sclera Irritation Veins Opacity": 0.22,  # was ~0.166: premium vein detail
    "Cornea Roughness": 0.05,            # was 0.075: sharper catchlight
    "Fake Reflection Intensity": 0.25,   # was 0.15: lively eyes under floodlights
}

LASH_SCALARS = {
    "HairMelanin": 0.85,   # was 0.3 brown: near-black Indian lashes
    "HairRedness": 0.12,   # was 0.28: removes ginger cast
    "Roughness": 0.35,     # was 0.25: less plastic shine
}

TEETH_SCALARS = {
    "Teeth Basecolor Value": 0.82,       # was 0.75: clean premium, not bleached
    "Teeth Roughness": 0.22,             # was 0.17: less plastic
    "Teeth Micro Normal Strength": 0.35,  # was 0.2: enamel detail at close range
}

EYESHELL_SCALARS = {
    "ShadowMultiply": 0.45,  # was 0.25: eyes sit in the socket, kills washout
}

# Groom hair: near-black athletic hair. Baked groom MIs carry the color in the
# lowercase hairMelanin/hairRedness scalars (verified via editor audit); the
# capitalised Melanin/Redness variants are kept too for other MI generations.
# Umpires keep grey (skip melanin there).
GROOM_HAIR = {"hairMelanin": 0.92, "hairRedness": 0.06, "HairRoughness": 0.42,
              "Melanin": 0.92, "Redness": 0.06, "Roughness": 0.42,
              "RoughnessOverall": 0.42, "HairMelanin": 0.92, "HairRedness": 0.06,
              "HairRoughness": 0.42}
GROOM_FACIAL = {"hairMelanin": 0.88, "hairRedness": 0.08, "HairRoughness": 0.5,
                "Melanin": 0.88, "Redness": 0.08, "Roughness": 0.5,
                "RoughnessOverall": 0.5, "HairMelanin": 0.88, "HairRedness": 0.08,
                "HairRoughness": 0.5}
GROOM_ROUGH_ONLY = {"Roughness": 0.5, "RoughnessOverall": 0.5, "HairRoughness": 0.5}


def set_scalars(mi, values, label):
    """Set scalar params that exist; returns count changed. Never adds params."""
    changed = 0
    try:
        names = set(str(n) for n in mel.get_scalar_parameter_names(mi))
    except Exception as e:
        unreal.log_warning(f"PREMIUM_FACE {label}: cannot list scalars ({e})")
        return 0
    for key, want in values.items():
        if key not in names:
            continue
        try:
            now = mel.get_material_instance_scalar_parameter_value(mi, key)
        except Exception:
            continue
        if now is None or abs(float(now) - float(want)) < 1e-4:
            continue
        if DRYRUN:
            unreal.log(f"PREMIUM_FACE {label}: {key} {now} -> {want} (dry run)")
            changed += 1
            continue
        try:
            mel.set_material_instance_scalar_parameter_value(mi, key, float(want))
            changed += 1
        except Exception as e:
            unreal.log_warning(f"PREMIUM_FACE {label}: cannot set {key} ({e})")
    return changed


def tune_asset(path, values, short):
    obj = unreal.load_asset(path)
    if obj is None:
        unreal.log_warning(f"PREMIUM_FACE {short}: missing {path}")
        return None
    if not isinstance(obj, unreal.MaterialInstanceConstant):
        unreal.log_warning(f"PREMIUM_FACE {short}: not a material instance: {path}")
        return None
    n = set_scalars(obj, values, short)
    if n:
        unreal.log(f"PREMIUM_FACE {short}: {n} params")
    return n


def tune_grooms(name):
    """Blacken scalp/beard/brow hair; umpires keep grey, get roughness only."""
    total = 0
    for path in lib.list_assets(f"/Game/MetaHumans/{name}/Grooms", recursive=False):
        if "/MI_" not in path:
            continue
        leaf = path.rsplit("/", 1)[-1]
        is_umpire = name in UMPIRES
        if is_umpire:
            values = GROOM_ROUGH_ONLY
        elif "Eyebrows" in leaf or "Beard" in leaf or "Goatee" in leaf or "Mustache" in leaf:
            values = GROOM_FACIAL
        elif "Hair" in leaf or "Fuzz" in leaf:
            values = GROOM_HAIR
        else:
            values = GROOM_FACIAL
        obj = unreal.load_asset(path)
        if not isinstance(obj, unreal.MaterialInstanceConstant):
            continue
        total += set_scalars(obj, values, f"{name}/Grooms/{leaf.split('.')[0]}")
    return total


def bake_dense_cards(name):
    """Bake groom card LOD 2 for close-ups into the blueprint LODSync mapping."""
    bp = unreal.load_asset(f"/Game/MetaHumans/{name}/BP_{name}")
    if bp is None:
        unreal.log_warning(f"PREMIUM_FACE {name}: missing blueprint")
        return False
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    data_lib = unreal.SubobjectDataBlueprintFunctionLibrary
    changed = False
    try:
        handles = sub.k2_gather_subobject_data_for_blueprint(bp)
    except Exception as e:
        unreal.log_warning(f"PREMIUM_FACE {name}: cannot gather subobjects ({e})")
        return False
    for handle in handles:
        try:
            obj = data_lib.get_object_for_blueprint(data_lib.get_data(handle), bp)
        except Exception:
            continue
        if not isinstance(obj, unreal.LODSyncComponent):
            continue
        try:
            mapping = obj.get_editor_property("custom_lod_mapping")
        except Exception:
            continue
        dirty = False
        for part in ("Hair", "Beard", "Mustache", "Eyebrows"):
            if part in mapping:
                entry = mapping[part]
                try:
                    nums = list(entry.get_editor_property("mapping"))
                except Exception:
                    continue
                if nums and nums[0] == 3:
                    nums[0] = 2
                    try:
                        entry.set_editor_property("mapping", nums)
                        dirty = True
                        unreal.log(f"PREMIUM_FACE {name}: {part} cards 3 -> 2 (dense close-up)")
                    except Exception as e:
                        unreal.log_warning(f"PREMIUM_FACE {name}: cannot set {part} mapping ({e})")
        if dirty:
            try:
                obj.set_editor_property("custom_lod_mapping", mapping)
                changed = True
            except Exception as e:
                unreal.log_warning(f"PREMIUM_FACE {name}: cannot commit LOD mapping ({e})")
    return changed


failures = []
touched = 0
for name in NAMES:
    unreal.log(f"PREMIUM_FACE {name}: starting")
    # Face + body skin share the same premium treatment (body uses LOD0 parent,
    # face LOD3; LOD5to7 inherits from LOD3 but carries its own overrides).
    for rel in (f"/Game/MetaHumans/{name}/Face/Materials/MI_Face_Skin_Baked_LOD3_VT",
                f"/Game/MetaHumans/{name}/Face/Materials/MI_Face_Skin_Baked_LOD5to7_VT",
                f"/Game/MetaHumans/{name}/Body/Materials/MI_Body_Baked_VT"):
        if lib.does_asset_exist(rel):
            r = tune_asset(rel, SKIN_SCALARS, f"{name}/{rel.rsplit('/', 1)[-1]}")
            if r is None:
                failures.append(rel)
            else:
                touched += r
    for rel in (f"/Game/MetaHumans/{name}/Face/Materials/MI_EyeL_Baked",
                f"/Game/MetaHumans/{name}/Face/Materials/MI_EyeR_Baked"):
        if lib.does_asset_exist(rel):
            r = tune_asset(rel, EYE_SCALARS, f"{name}/{rel.rsplit('/', 1)[-1]}")
            if r is None:
                failures.append(rel)
            else:
                touched += r
    rel = f"/Game/MetaHumans/{name}/Face/Materials/MI_Face_EyelashesHiLODs"
    if lib.does_asset_exist(rel):
        r = tune_asset(rel, LASH_SCALARS, f"{name}/MI_Face_EyelashesHiLODs")
        if r is None:
            failures.append(rel)
        else:
            touched += r
    rel = f"/Game/MetaHumans/{name}/Face/Materials/MI_Teeth_Baked"
    if lib.does_asset_exist(rel):
        r = tune_asset(rel, TEETH_SCALARS, f"{name}/MI_Teeth_Baked")
        if r is None:
            failures.append(rel)
        else:
            touched += r
    rel = f"/Game/MetaHumans/{name}/Face/Materials/MI_Face_EyeShell"
    if lib.does_asset_exist(rel):
        r = tune_asset(rel, EYESHELL_SCALARS, f"{name}/MI_Face_EyeShell")
        if r is None:
            failures.append(rel)
        else:
            touched += r
    touched += tune_grooms(name)
    if bake_dense_cards(name):
        touched += 1

if not DRYRUN:
    lib.save_directory("/Game/MetaHumans", only_if_is_dirty=True, recursive=True)

unreal.log(f"PREMIUM_FACE done: {touched} tweaks across {len(NAMES)} identities, {len(failures)} failures")
if failures:
    for f in failures:
        unreal.log_error(f"PREMIUM_FACE FAILED: {f}")
    sys.exit(1)

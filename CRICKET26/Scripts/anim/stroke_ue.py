# Unreal side of Scripts/anim/import_stroke.sh: imports the strokes author_stroke.py baked (Bat_<Stroke>.fbx) onto the
# striker's own skeleton, as /Game/Anims/Strokes/Bat_<Stroke>. No retarget: they were authored on this skeleton.
import glob, os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strokes  # noqa: E402

SRC = os.environ["STROKE_DIR"]
DEST = "/Game/Anims/Strokes"
KIT = "/Game/MetaHumans/MH_Home_Opener/Kit/SKM_MH_Home_Opener_Kit"

skeleton = unreal.load_asset(KIT).skeleton
tools = unreal.AssetToolsHelpers.get_asset_tools()
for path in sorted(glob.glob(os.path.join(SRC, "Bat_*.fbx"))):
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ui.import_animations = True
    ui.skeleton = skeleton
    ui.import_materials = ui.import_textures = ui.create_physics_asset = False
    data = ui.anim_sequence_import_data
    data.set_editor_property("use_default_sample_rate", False)
    data.set_editor_property("custom_sample_rate", 60)  # author_stroke.FPS
    data.set_editor_property("snap_to_closest_frame_boundary", True)
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = DEST
    t.destination_name = os.path.splitext(os.path.basename(path))[0]
    t.automated = t.replace_existing = t.save = True
    t.options = ui
    tools.import_asset_tasks([t])
    seq = unreal.load_asset(f"{DEST}/{t.destination_name}")
    if not seq:
        unreal.log_error(f"STROKE failed {path}")
        continue
    # The clip must last as long as its keys (a wrong frame rate in the FBX stretches it), and the pelvis at the first
    # frame confirms the units came through (the stance pelvis is about 90 cm up).
    keys = strokes.KEYS.get(t.destination_name[len("Bat_"):])
    length = seq.get_play_length()
    if keys and abs(length - keys[-1][0]) > 1.5 / 60:
        unreal.log_error(f"STROKE {t.destination_name}: {length:.2f} s long, its keys end at {keys[-1][0]:.2f} s")
    pelvis = unreal.AnimationLibrary.get_bone_pose_for_time(seq, "pelvis", 0.0, False)
    unreal.log(f"STROKE {t.destination_name}: {length:.2f} s, pelvis local {pelvis.translation}")

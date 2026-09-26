# Unreal side of Scripts/anim/import_stroke.sh: imports the strokes author_stroke.py baked (Bat_<Stroke>.fbx) onto the
# striker's own skeleton, as /Game/Anims/Strokes/Bat_<Stroke>, or (STROKE_PREFIX=Bowl_) the bowling actions
# author_bowl.py baked, as /Game/Anims/Bowling/Bowl_<Type>_<Arm>. No retarget: they were authored on this skeleton.
import glob, os, sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strokes, bowls  # noqa: E402

SRC = os.environ["STROKE_DIR"]
PREFIX = os.environ.get("STROKE_PREFIX", "Bat_")
DEST = "/Game/Anims/Bowling" if PREFIX == "Bowl_" else "/Game/Anims/Strokes"
KIT = "/Game/MetaHumans/MH_Home_Opener/Kit/SKM_MH_Home_Opener_Kit"

skeleton = unreal.load_asset(KIT).skeleton
tools = unreal.AssetToolsHelpers.get_asset_tools()
for path in sorted(glob.glob(os.path.join(SRC, PREFIX + "*.fbx"))):
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
    keys = strokes.KEYS.get(t.destination_name[len(PREFIX):])
    want = bowls.END - bowls.START if PREFIX == "Bowl_" else keys[-1][0] if keys else None
    length = seq.get_play_length()
    if want and abs(length - want) > 1.5 / 60:
        unreal.log_error(f"STROKE {t.destination_name}: {length:.2f} s long, its keys run {want:.2f} s")
    pelvis = unreal.AnimationLibrary.get_bone_pose_for_time(seq, "pelvis", 0.0, False)
    unreal.log(f"STROKE {t.destination_name}: {length:.2f} s, pelvis local {pelvis.translation}")

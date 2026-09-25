# Unreal side of Scripts/anim/import_anims.sh: brings motion-capture clips onto the mannequin skeleton.
#
# Two sources, each in its own folder under $ANIM_DIR, each with its own skeleton:
#   fbx/     DeepMotion Animate 3D takes of the batting video ("Adult Male (UE)" character, Mixamo-style bone names).
#   mixamo/  Mixamo library clips for running and fielding (one Mixamo character; one file with its skin).
# Neither plays on the mannequin directly. For each source this imports the largest file's character as
# /Game/Anims/<Source>/SK_<Source>, every <Name>.fbx as an animation on it, and then retargets them all through IK Rigs
# that Unreal characterises automatically (IK_<Source>, IK_Mannequin, RTG_<Source>) into /Game/Anims/Mocap/<Name>, on
# SK_Mannequin, which the players' bodies already play.
import glob
import os
import unreal

DIR = os.environ["ANIM_DIR"]
DEST = "/Game/Anims/Mocap"
MANNY = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def import_fbx(path, src, name, skeleton=None):
    ui = unreal.FbxImportUI()
    ui.import_mesh = skeleton is None
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeleton is None else unreal.FBXImportType.FBXIT_ANIMATION
    ui.import_animations = skeleton is not None
    ui.skeleton = skeleton
    ui.import_materials = ui.import_textures = ui.create_physics_asset = False
    # DeepMotion bakes at 30 fps, but the importer reads the file's rate as 12 and then refuses the 2 s take.
    data = ui.anim_sequence_import_data
    data.set_editor_property("use_default_sample_rate", False)
    data.set_editor_property("custom_sample_rate", 30)
    data.set_editor_property("snap_to_closest_frame_boundary", True)
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = src
    t.destination_name = name
    t.automated = t.replace_existing = t.save = True
    t.options = ui
    tools.import_asset_tasks([t])
    return list(t.imported_object_paths)


# A commandlet cannot delete an asset something references, so reruns reuse and reset the rigs and retargeter.
def load_or_create(src, name, cls, factory):
    return unreal.load_asset(f"{src}/{name}") or tools.create_asset(name, src, cls, factory)


def ik_rig(src, name, mesh):
    rig = load_or_create(src, name, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(mesh)
    for chain in c.get_retarget_chains():
        c.remove_retarget_chain(chain.chain_name)
    ok = c.apply_auto_generated_retarget_definition()
    unreal.log(f"ANIM {name}: auto retarget definition {ok}, chains {[str(x.chain_name) for x in c.get_retarget_chains()]}")
    lib.save_loaded_asset(rig)
    return rig


target = unreal.load_asset(MANNY)


def retarget(source_name, files):
    """Imports one source's clips on their own skeleton and retargets them to the mannequin; returns the new assets."""
    src = f"/Game/Anims/{source_name}"
    import_fbx(max(files, key=os.path.getsize), src, f"SK_{source_name}")  # the one with the skin
    source = unreal.load_asset(f"{src}/SK_{source_name}")
    anims = []
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        unreal.log(f"ANIM {name}: imported {import_fbx(f, src, name, source.skeleton)}")
        anims.append(lib.find_asset_data(f"{src}/{name}"))
    src_rig, tgt_rig = ik_rig(src, f"IK_{source_name}", source), ik_rig(src, "IK_Mannequin", target)
    rtg = load_or_create(src, f"RTG_{source_name}", unreal.IKRetargeter, unreal.IKRetargetFactory())
    r = unreal.IKRetargeterController.get_controller(rtg)
    r.remove_all_ops()
    r.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
    r.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, tgt_rig)
    r.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, source)
    r.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, target)
    r.add_default_ops()
    r.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src_rig)
    r.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, tgt_rig)
    r.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Both sources' characters are bound in a T-pose and the mannequin in an A-pose: pose the mannequin's arms to match.
    r.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    lib.save_loaded_asset(rtg)

    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.assets_to_retarget = anims
    inputs.source_mesh = source
    inputs.target_mesh = target
    inputs.ik_retarget_asset = rtg
    inputs.target_path = DEST
    inputs.overwrite_existing_files = True
    inputs.include_referenced_assets = False
    return unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)


def files_in(sub):
    return sorted(glob.glob(f"{DIR}/{sub}/*.fbx"))


anim_lib = unreal.AnimationLibrary
takes = retarget("DeepMotion", files_in("fbx")) if files_in("fbx") else []
library = retarget("Mixamo", files_in("mixamo")) if files_in("mixamo") else []
for a in library:
    seq = unreal.load_asset(str(a.package_name))
    if str(a.asset_name).startswith("Run_"):
        # A run cycle travels forward on its root; the game moves the actor, so the loop must run on the spot.
        frames = anim_lib.get_num_frames(seq)
        keys = [anim_lib.get_bone_pose_for_frame(seq, "root", f, False) for f in range(frames + 1)]
        drift = keys[-1].translation - keys[0].translation
        drift.z = 0.0
        c = seq.controller
        c.open_bracket(unreal.Text("In place"))
        c.set_bone_track_keys("root", [k.translation - drift * (f / frames) for f, k in enumerate(keys)],
                              [k.rotation for k in keys], [k.scale3d for k in keys])
        c.close_bracket()
        unreal.log(f"ANIM {a.asset_name}: in place, stride speed {drift.length() / seq.get_play_length() / 100:.2f} m/s")
    lib.save_asset(str(a.package_name))  # the batch retarget leaves its new assets unsaved
    unreal.log(f"ANIM retargeted {a.package_name}: {seq.get_play_length():.2f} s on {seq.get_skeleton().get_name()}")


def component(seq, bone, frame):
    """A bone's component-space transform at a frame, composed from the local poses up to the root."""
    t = unreal.Transform()
    for b in anim_lib.find_bone_path_to_root(seq, bone):
        t = t * anim_lib.get_bone_pose_for_frame(seq, b, frame, False)
    return t


def flat(d):
    return unreal.Vector(d.x, d.y, 0.0).normal()


def feet_line(seq):
    """Horizontal direction from the right foot to the left at the first frame, toward the bowler in a stance."""
    return flat(component(seq, "foot_l", 0).translation - component(seq, "foot_r", 0).translation)


# Each take was filmed from its own side, so each clip comes out of DeepMotion facing its own way and standing off
# the origin. Turn every clip about the root so its stance's feet line up the way the mannequin's own left points
# (toward the bowler for a batter, who stands side-on facing the off side), and slide it so the pelvis starts over
# the origin: the game can then play any clip at the actor's feet.
ref = unreal.AnimPoseExtensions.get_reference_pose(target.skeleton)
hip = lambda bone: unreal.AnimPoseExtensions.get_bone_pose(ref, bone, unreal.AnimPoseSpaces.WORLD).translation
left = flat(hip("thigh_l") - hip("thigh_r"))
for a in takes:
    seq = unreal.load_asset(str(a.package_name))
    have = feet_line(seq)
    yaw = unreal.MathLibrary.atan2(left.y, left.x) - unreal.MathLibrary.atan2(have.y, have.x)
    turn = unreal.Rotator(yaw=unreal.MathLibrary.radians_to_degrees(yaw)).quaternion()
    start = turn.rotate_vector(component(seq, "pelvis", 0).translation)
    shift = unreal.Vector(start.x, start.y, 0.0)
    frames = anim_lib.get_num_frames(seq)
    pos, rot, scale = [], [], []
    for f in range(frames + 1):
        r = anim_lib.get_bone_pose_for_frame(seq, "root", f, False)
        pos.append(turn.rotate_vector(r.translation) - shift)
        rot.append(turn * r.rotation)
        scale.append(r.scale3d)
    c = seq.controller
    c.open_bracket(unreal.Text("Face the clip"))
    c.set_bone_track_keys("root", pos, rot, scale)
    c.close_bracket()
    lib.save_asset(str(a.package_name))
    unreal.log(f"ANIM retargeted {a.package_name}: {seq.get_play_length():.2f} s on {seq.get_skeleton().get_name()}, "
               f"turned {unreal.MathLibrary.radians_to_degrees(yaw):.0f} deg, slid {shift.length():.0f} cm")

# The Unreal half of the jersey panels (make_jersey.sh). With JERSEY_OBJ set, writes the crew-neck shirt (or the
# garment JERSEY_SLOT names) as built for the player JERSEY_PLAYER, its points and first UVs, as an OBJ for jersey_panels.py (FBX export of the outfit meshes
# crashes the editor). The built shirt is its own mesh, fitted to the body, and its UVs are not the source garment's; with
# JERSEY_PNG set, imports the baked panels as /Game/MetaHumans/Outfits/JERSEY_NAME for SuperOverGameMode::ShirtPrint.
import os
import unreal

PLAYER = os.environ.get("JERSEY_PLAYER", "MH_Home_Opener")
SLOT = os.environ.get("JERSEY_SLOT", "crewneckt")    # the garment, by its first material slot: "jeans" for the trousers

if os.environ.get("JERSEY_OBJ"):
    Q = unreal.GeometryScript_MeshQueries
    shirt = next(m for m in map(unreal.load_asset, unreal.EditorAssetLibrary.list_assets(f"/Game/MetaHumans/{PLAYER}/Clothing", recursive=False))
                 if isinstance(m, unreal.SkeletalMesh) and SLOT in str(m.materials[0].material_slot_name).lower())
    mesh = unreal.DynamicMesh()
    mesh, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        shirt, mesh, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    lines, faces = [], []
    for v in range(mesh.get_vertex_count()):
        p, _ = Q.get_vertex_position(mesh, v)
        lines.append(f"v {p.x:.4f} {p.y:.4f} {p.z:.4f}")
    for t in range(mesh.get_triangle_count()):
        tri = Q.get_triangle_indices(mesh, t)
        tri = tri if isinstance(tri, unreal.IntVector) else tri[0]
        uv = Q.get_triangle_u_vs(mesh, 0, t)
        # OBJ counts v up from the bottom of the texture, Unreal down from the top.
        lines += [f"vt {c.x:.5f} {1 - c.y:.5f}" for c in uv[:3]]
        faces.append(f"f {tri.x + 1}/{3 * t + 1} {tri.y + 1}/{3 * t + 2} {tri.z + 1}/{3 * t + 3}")
    open(os.environ["JERSEY_OBJ"], "w").write("\n".join(lines + faces) + "\n")
    if os.environ.get("JERSEY_TGA"):
        # The garment's own mask, for trouser_stripe.py to repaint.
        task = unreal.AssetExportTask()
        task.object = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(shirt.materials[0].material_interface, "Mask")
        task.filename = os.environ["JERSEY_TGA"]
        task.automated = True
        task.prompt = False
        unreal.log(f"JERSEY mask {task.object.get_path_name()} exported {unreal.Exporter.run_asset_export_task(task)}")
    unreal.log(f"JERSEY {shirt.get_name()} {outcome} {mesh.get_triangle_count()} triangles, {Q.get_num_uv_sets(mesh)} UV sets, "
               f"to {os.environ['JERSEY_OBJ']}")

if os.environ.get("JERSEY_PNG"):
    task = unreal.AssetImportTask()
    task.filename = os.environ["JERSEY_PNG"]
    task.destination_path = "/Game/MetaHumans/Outfits"
    task.destination_name = os.environ["JERSEY_NAME"]
    task.replace_existing = True
    task.automated = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset(f"/Game/MetaHumans/Outfits/{os.environ['JERSEY_NAME']}")
    # Masks, not colours, and drawn by the canvas the moment a player is dressed, so kept whole in memory.
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    unreal.log(f"JERSEY imported {tex.get_path_name()}")

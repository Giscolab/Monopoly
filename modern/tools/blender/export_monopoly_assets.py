import argparse
import hashlib
import json
import math
import struct
import sys
import tempfile
import shutil
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


CONTRACT_VERSION = 1
RETAIL_BOARD_UNITS_PER_METRE = 200.0


def retail_case_rectangles():
    """Measured boardmed.obj tabletop bands in production-decoder units.

    The X bands are one unit below the nominal BoardGeometry coordinates on
    the far/right edges. Preserve that decoded rounding, rather than deriving
    tile centres from the gameplay edge anchors or the outer frame bounds.
    """
    rectangles = []
    for side in range(4):
        for slot in range(10):
            if slot == 0:
                x = (0.0, 645.0) if side < 2 else (4214.0, 4859.0)
                z = (0.0, 645.0) if side in (0, 3) else (4215.0, 4860.0)
            elif side == 0:
                x, z = (0.0, 645.0), (660.0 + 395 * (slot - 1), 1040.0 + 395 * (slot - 1))
            elif side == 1:
                x, z = (659.0 + 395 * (slot - 1), 1039.0 + 395 * (slot - 1)), (4215.0, 4860.0)
            elif side == 2:
                x, z = (4214.0, 4859.0), (3820.0 - 395 * (slot - 1), 4200.0 - 395 * (slot - 1))
            else:
                x, z = (3819.0 - 395 * (slot - 1), 4199.0 - 395 * (slot - 1)), (0.0, 645.0)
            rectangles.append((x[0], x[1], z[0], z[1]))
    return rectangles


def retail_board_measurement(path):
    contents = path.read_bytes()
    vertices = [tuple(map(float, line.split()[1:4]))
                for line in contents.decode("utf-8").splitlines() if line.startswith("v ")]
    if not vertices or not all(math.isfinite(value) for point in vertices for value in point):
        raise RuntimeError("retail board OBJ has no finite decoded vertices")
    low = [min(point[axis] for point in vertices) for axis in range(3)]
    high = [max(point[axis] for point in vertices) for axis in range(3)]
    if low != [-84.0, -64.0, -84.0] or high != [4944.0, 50.0, 4944.0]:
        raise RuntimeError("retail board OBJ does not match measured classic-medium HMD bounds")
    tabletop = {(point[0], point[2]) for point in vertices if abs(point[1]) < 0.00001}
    rectangles = retail_case_rectangles()
    for index, (xmin, xmax, zmin, zmax) in enumerate(rectangles):
        # Some textured/decorated corners are triangulated with an interior
        # point instead of every rectangle corner. At least two opposite band
        # corners must still occur in the actual decoded tabletop mesh.
        if sum((x, z) in tabletop for x in (xmin, xmax) for z in (zmin, zmax)) < 2:
            raise RuntimeError(f"retail tabletop does not contain case {index:02d} band")
    return {"minimum": low, "maximum": high,
            "obj_sha256": hashlib.sha256(contents).hexdigest(),
            "case_rectangles": rectangles}


def runtime_board_bases(collection_names, retail_obj):
    measurement = retail_board_measurement(retail_obj)
    units = RETAIL_BOARD_UNITS_PER_METRE
    center = [(measurement["minimum"][axis] + measurement["maximum"][axis]) / 2.0
              for axis in (0, 2)]
    bases = {}
    records = []
    for index, collection_name in enumerate(collection_names[2:]):
        collection = bpy.data.collections[collection_name]
        source_root = root_for_collection(collection)
        if source_root.matrix_world.determinant() <= 0:
            raise RuntimeError(f"case {index:02d}: reflected source root cannot preserve readable lettering")
        xmin, xmax, zmin, zmax = measurement["case_rectangles"][index]
        raw_center = [(xmin + xmax) / 2.0, 0.0, (zmin + zmax) / 2.0]
        target = ((raw_center[0] - center[0]) / units,
                  -(raw_center[2] - center[1]) / units, source_root.matrix_world.translation.z)
        yaw = -90.0 + 90.0 * (index // 10)
        # Each case is rotated, never reflected. Its local X is property width,
        # local Y is inward depth. Corners have a square nominal 3m footprint.
        width = 645.0 if index % 10 == 0 else 380.0
        scale = (width / ((3.0 if index % 10 == 0 else 2.0) * units),
                 645.0 / (3.0 * units), 1.0)
        target_matrix = (Matrix.Translation(Vector(target)) @
                         Matrix.Rotation(math.radians(yaw), 4, "Z") @
                         Matrix.Diagonal((*scale, 1.0)))
        basis = target_matrix @ source_root.matrix_world.inverted()
        if basis.determinant() <= 0:
            raise RuntimeError(f"case {index:02d}: alignment unexpectedly reflects geometry")
        mapped = basis @ source_root.matrix_world.translation
        if (mapped - Vector(target)).length > 0.00001:
            raise RuntimeError(f"case {index:02d}: alignment centre is incorrect")
        # Source Blender Z becomes glTF Y. Compress raised tile lettering and
        # icons about the measured cream floor, then lower that floor to zero.
        # Positive node scale preserves winding and allows the production GLB
        # loader to transform normals correctly by inverse transpose.
        height_basis = (Matrix.Translation(Vector((0, 0, -0.006))) @
                        Matrix.Translation(Vector((0, 0, 0.006))) @
                        Matrix.Diagonal((1.0, 1.0, 0.1, 1.0)) @
                        Matrix.Translation(Vector((0, 0, -0.006))))
        basis = height_basis @ basis
        for source in collection.all_objects:
            if source.name in bases:
                raise RuntimeError(f"case object belongs to multiple cases: {source.name}")
            bases[source.name] = basis
        records.append({"case": index, "center_decoded_units": raw_center,
                        "rectangle_decoded_xz": [xmin, xmax, zmin, zmax],
                        "center_blender_metres": list(target),
                        "yaw_blender_degrees": yaw, "local_scale": list(scale)})

    # Fit only the recovered board/frame surface uniformly to the measured
    # outer bounds. Case placement and width/pitch have their own measurements.
    low, high = [float("inf")] * 2, [float("-inf")] * 2
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for source in bpy.data.collections[collection_names[0]].all_objects:
        if source.type not in {"MESH", "FONT", "CURVE", "SURFACE"}:
            continue
        evaluated = source.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        try:
            for vertex in mesh.vertices:
                p = source.matrix_world @ vertex.co
                for axis, value in enumerate((p.x, p.y)):
                    low[axis], high[axis] = min(low[axis], value), max(high[axis], value)
        finally:
            evaluated.to_mesh_clear()
    span = [high[axis] - low[axis] for axis in range(2)]
    if not all(math.isfinite(value) and value > 0 for value in span) or abs(span[0] - span[1]) > 0.0001:
        raise RuntimeError("recovered board surface must have finite square bounds")
    frame_scale = (measurement["maximum"][0] - measurement["minimum"][0]) / (units * span[0])
    base_basis = (Matrix.Diagonal((frame_scale, frame_scale, 1.0, 1.0)) @
                  Matrix.Translation(Vector((-(low[0] + high[0]) / 2.0, -(low[1] + high[1]) / 2.0, 0.0))))
    # Blender appends .001 to the temporary copy because the authoring object
    # remains alive. Identify the original object before that export suffix.
    pavement_name = collection_names[0] + " / pavement"
    if pavement_name not in {source.name for source in bpy.data.collections[collection_names[0]].all_objects}:
        raise RuntimeError("measured inner pavement is missing from the recovered board collection")
    for collection_name in collection_names[:2]:
        for source in bpy.data.collections[collection_name].all_objects:
            height_basis = Matrix.Translation(Vector((0, 0, -0.006)))
            if collection_name == collection_names[0] and source.name == pavement_name:
                # This specific inner pavement overlaps the retail house feet.
                # Keep its measured top 0.0101 m below the printed floor so
                # leave depth clearance beneath Case surfaces. Preserve the
                # normal transform and horizontal placement and dimensions.
                height_basis = (height_basis @ Matrix.Translation(Vector((0, 0, -0.0041))) @
                                Matrix.Diagonal((1.0, 1.0, 0.001, 1.0)) @
                                Matrix.Translation(Vector((0, 0, -0.0815))))
            bases[source.name] = height_basis @ base_basis
    return bases, {"alignment_contract_version": 1,
                   "retail_mesh_data_id": 0x00080003,
                   "retail_units_per_metre": units,
                   "retail_local_offset_decoded_units": [center[0], 0.0, center[1]],
                   "retail_sequence_scale": 0.1,
                   "required_board_edition": "Europe",
                   "required_board_country": 1,
                   "required_label_language": 3,
                   "required_currency_system": 12,
                   "retail_obj_sha256": measurement["obj_sha256"],
                   "retail_outer_bounds_decoded_units": {"min": measurement["minimum"], "max": measurement["maximum"]},
                   "board_surface_uniform_xy_scale": frame_scale,
                   "height_adaptation": {"global_drop_blender_metres": 0.006,
                       "case_height_anchor_blender_metres": 0.006,
                       "case_height_scale": 0.1,
                       "inner_pavement_object": pavement_name,
                       "inner_pavement_source_top_blender_metres": 0.0815,
                       "inner_pavement_target_top_before_drop_blender_metres": -0.0041,
                       "inner_pavement_floor_clearance_blender_metres": 0.0101,
                       "inner_pavement_height_scale": 0.001,
                       "horizontal_alignment_preserved": True,
                       "normal_transform": "positive nonsingular node scale; inverse transpose"},
                   "cases": records,
                   "geometry_reflected": False}


def opaque_runtime_material(source):
    """Copy authored image paths, masking only the genuine alpha mascot print."""
    original = next((node for node in source.node_tree.nodes
                     if node.type == "BSDF_PRINCIPLED"), None) if source.use_nodes else None
    color = list(original.inputs["Base Color"].default_value) if original else list(source.diffuse_color)
    alpha = float(original.inputs["Alpha"].default_value) if original else color[3]
    transmission = float(original.inputs["Transmission Weight"].default_value) if original else 0.0
    alpha_linked = bool(original and original.inputs["Alpha"].is_linked)
    source_name = source.get("source_original_material_name", source.name)
    masked = source_name == "MP · mascot_print"
    if masked and not alpha_linked:
        raise RuntimeError("mascot print must retain its authored image alpha path")
    copied = source.copy()
    copied.name = source_name + (" · runtime alpha mask" if masked else " · runtime opaque factors")
    copied.use_nodes = True
    if copied.node_tree == source.node_tree:
        raise RuntimeError("runtime material copy unexpectedly shares authoring nodes")
    if original:
        shader = next(node for node in copied.node_tree.nodes if node.type == "BSDF_PRINCIPLED")
    else:
        copied.node_tree.nodes.clear()
        shader = copied.node_tree.nodes.new("ShaderNodeBsdfPrincipled")
        output = copied.node_tree.nodes.new("ShaderNodeOutputMaterial")
        copied.node_tree.links.new(shader.outputs["BSDF"], output.inputs["Surface"])
    alpha_path = shader.inputs["Alpha"].links[0].from_socket if masked else None
    # Disconnect alpha/transmission only. Rebuilding the entire material would
    # destroy authored image maps (notably the mascot print's base colour).
    for name in ("Alpha", "Transmission Weight"):
        for link in tuple(shader.inputs[name].links):
            copied.node_tree.links.remove(link)
    color[3] = 1.0
    copied.diffuse_color = color
    shader.inputs["Base Color"].default_value = color
    shader.inputs["Alpha"].default_value = 1.0
    shader.inputs["Transmission Weight"].default_value = 0.0
    if masked:
        # The glTF exporter recognizes GREATER_THAN as alphaMode MASK. Preserve
        # the real image alpha input rather than inventing a silhouette texture.
        clip = copied.node_tree.nodes.new("ShaderNodeMath")
        clip.operation = "GREATER_THAN"
        clip.inputs[1].default_value = 0.5
        copied.node_tree.links.new(alpha_path, clip.inputs[0])
        copied.node_tree.links.new(clip.outputs[0], shader.inputs["Alpha"])
    if original:
        for name in ("Metallic", "Roughness", "Emission Color", "Emission Strength"):
            shader.inputs[name].default_value = original.inputs[name].default_value
    else:
        shader.inputs["Metallic"].default_value = source.metallic
        shader.inputs["Roughness"].default_value = source.roughness
    return copied, {"source_material": source.name,
                    "source_alpha_factor": alpha,
                    "source_transmission_factor": transmission,
                    "source_alpha_linked": alpha_linked,
                    "opaque_alpha_approximation": not masked and (alpha < 1.0 or alpha_linked),
                    "alpha_mask_approximation": masked,
                    "alpha_cutoff": 0.5 if masked else None,
                    "opaque_glass_approximation": transmission > 0.0,
                    "runtime_alpha_mode": "MASK" if masked else "OPAQUE",
                    "image_node_paths_preserved": original is not None,
                    "procedural_nodes_exported": False}

TOKEN_ASSETS = {
    "Pion automobile": ("race_car", 1),
    "Pion terrier": ("dog", 2),
    "Pion haut-de-forme": ("top_hat", 3),
    "Pion bottine": ("boot", 7),
    "Pion cuirasse": ("ship", 6),
    "Pion de a coudre": ("thimble", 8),
}


def board_collections():
    names = ["01 · Plateau carré", "02 · Identité centrale"]
    for index in range(40):
        matches = sorted(
            c.name for c in bpy.data.collections
            if c.name.startswith(f"Case {index:02d} · ")
        )
        if len(matches) != 1:
            raise RuntimeError(f"board square {index}: expected one collection")
        names.extend(matches)
    return names


def reflect_runtime_print(mesh, source):
    """Bake retail LH print parity into evaluated copies, preserving placement.

    FONT outlines and the genuine mascot image lie in source-local XY.
    Only their local Y is reflected about the unchanged local bounds midpoint.
    Faces are reversed so geometric normals retain their original outward side.
    UVs remain attached to vertices, reflecting the mascot drawing in its frame.
    """
    if source.type != "FONT" and source.name != "02 · Identité centrale / mascot_print":
        return None
    if not mesh.vertices:
        raise RuntimeError(f"runtime print {source.name}: missing evaluated vertices")
    before = [[min(vertex.co[axis] for vertex in mesh.vertices),
               max(vertex.co[axis] for vertex in mesh.vertices)] for axis in range(3)]
    center = (before[1][0] + before[1][1]) * 0.5
    transform = Matrix.Identity(4)
    transform[1][1], transform[1][3] = -1.0, 2.0 * center
    mesh.transform(transform)
    mesh.flip_normals()
    mesh.update()
    after = [[min(vertex.co[axis] for vertex in mesh.vertices),
              max(vertex.co[axis] for vertex in mesh.vertices)] for axis in range(3)]
    if max(abs(a-b) for old,new in zip(before,after) for a,b in zip(old,new)) > 0.00001:
        raise RuntimeError(f"runtime print {source.name}: reflection moved its bounds")
    return {"source_object": source.name, "source_type": source.type,
            "reflection_axis": "local Y", "local_bounds_midpoint_y": center,
            "placement_matrix_unchanged": True, "local_bounds_preserved": True,
            "winding_reversed": True, "source_uvs_preserved": True}


def export_static_group(collection_names, kind, slug, output_dir, local_root=None,
                       object_bases=None, alignment=None, procedural_baker=None,
                       smooth_house_edges=False):
    """Export evaluated copies only; retain the recovered scene untouched."""
    sources = {}
    for name in collection_names:
        collection = bpy.data.collections.get(name)
        if collection is None:
            raise RuntimeError(f"missing Blender collection: {name}")
        for obj in collection.all_objects:
            if obj.type in {"MESH", "FONT", "CURVE", "SURFACE"}:
                sources[obj.name] = obj
            elif obj.type != "EMPTY":
                raise RuntimeError(f"unsupported static object: {obj.name} ({obj.type})")
    if not sources:
        raise RuntimeError(f"{slug}: no geometry")

    basis = Matrix.Identity(4)
    if local_root is not None:
        basis = local_root.matrix_world.inverted()
    temporary = bpy.data.collections.new(f"EXPORT_{slug}")
    bpy.context.scene.collection.children.link(temporary)
    root = bpy.data.objects.new(slug, None)
    temporary.objects.link(root)
    root.matrix_world = Matrix.Identity(4)
    root["asset_kind"] = kind
    root["asset_slug"] = slug
    root["asset_contract_version"] = CONTRACT_VERSION
    if alignment is not None:
        root["board_alignment_contract_version"] = alignment["alignment_contract_version"]
        root["retail_mesh_data_id"] = alignment["retail_mesh_data_id"]
        root["retail_units_per_metre"] = alignment["retail_units_per_metre"]
        root["retail_local_offset_decoded_units"] = alignment["retail_local_offset_decoded_units"]
        root["retail_sequence_scale"] = alignment["retail_sequence_scale"]
        root["retail_obj_sha256"] = alignment["retail_obj_sha256"]
        root["retail_case_centers_decoded_units"] = [
            value for case in alignment["cases"] for value in case["center_decoded_units"]]
        root["retail_case_center_count"] = len(alignment["cases"])
        root["required_board_country"] = alignment["required_board_country"]
        root["required_currency_system"] = alignment["required_currency_system"]
        root["runtime_material_policy"] = "mascot image alpha masked at 0.5; other alpha/transmission approximated as opaque"
        root["alpha_mask_approximation"] = True
        root["mascot_alpha_cutoff"] = 0.5
    created_meshes = []
    created_materials = {}
    material_records = []
    reflected_prints = []
    house_records = []
    reference_low = [float("inf")] * 3
    reference_high = [float("-inf")] * 3
    low = [float("inf")] * 3
    high = [float("-inf")] * 3
    try:
        depsgraph = bpy.context.evaluated_depsgraph_get()
        for name, source in sorted(sources.items()):
            if procedural_baker:
                depsgraph = bpy.context.evaluated_depsgraph_get()
            evaluated = source.evaluated_get(depsgraph)
            if smooth_house_edges:
                reference = bpy.data.meshes.new_from_object(evaluated, depsgraph=depsgraph)
                try:
                    for vertex in reference.vertices:
                        p = basis @ source.matrix_world @ vertex.co
                        for axis, value in enumerate((p.x, p.z, -p.y)):
                            reference_low[axis] = min(reference_low[axis], value)
                            reference_high[axis] = max(reference_high[axis], value)
                finally:
                    bpy.data.meshes.remove(reference)
                copy = source.copy()
                copy.name = "HOUSE_POLISH_" + name
                copy.data = source.data.copy()
                temporary.objects.link(copy)
                copied_mesh = copy.data
                try:
                    bevels = [modifier for modifier in copy.modifiers if modifier.type == "BEVEL"]
                    if not bevels or copy.type != "MESH":
                        raise RuntimeError(f"{name}: house polish requires an authored mesh bevel")
                    records = []
                    for modifier in bevels:
                        records.append({"width": modifier.width, "profile": modifier.profile,
                                        "source_segments": modifier.segments, "segments": 4})
                        modifier.segments = 4
                        modifier.harden_normals = True
                        modifier.face_strength_mode = "FSTR_ALL"
                    for polygon in copied_mesh.polygons:
                        polygon.use_smooth = True
                    weighted = copy.modifiers.new("House export weighted normals", "WEIGHTED_NORMAL")
                    weighted.keep_sharp = True
                    weighted.use_face_influence = True
                    bpy.context.view_layer.update()
                    depsgraph = bpy.context.evaluated_depsgraph_get()
                    mesh = bpy.data.meshes.new_from_object(copy.evaluated_get(depsgraph), depsgraph=depsgraph)
                    mesh.calc_loop_triangles()
                    largest = max(polygon.area for polygon in mesh.polygons)
                    planar = [polygon for polygon in mesh.polygons if polygon.area >= largest * 0.2]
                    error = max((math.degrees(mesh.corner_normals[loop].vector.angle(polygon.normal))
                                 for polygon in planar for loop in polygon.loop_indices), default=0.0)
                    house_records.append({"object": name, "bevels": records,
                        "triangles": len(mesh.loop_triangles), "large_face_count": len(planar),
                        "large_face_max_normal_angle_degrees": error})
                    if error > 0.1:
                        bpy.data.meshes.remove(mesh)
                        raise RuntimeError(f"{name}: large planar face normal drift {error} degrees")
                finally:
                    bpy.data.objects.remove(copy, do_unlink=True)
                    if copied_mesh.users == 0:
                        bpy.data.meshes.remove(copied_mesh)
            else:
                mesh = bpy.data.meshes.new_from_object(evaluated, depsgraph=depsgraph)
            if procedural_baker:
                for index, slot in enumerate(source.material_slots):
                    material = slot.material.copy()
                    material["source_original_material_name"] = slot.material.name
                    procedural_baker.materials.append(material)
                    mesh.materials[index] = material
            created_meshes.append(mesh)
            if alignment is not None:
                for index, material in enumerate(mesh.materials):
                    if material is None:
                        continue
                    if material.name not in created_materials:
                        copied, record = opaque_runtime_material(material)
                        created_materials[material.name] = copied
                        material_records.append(record)
                    mesh.materials[index] = created_materials[material.name]
            obj = bpy.data.objects.new(name, mesh)
            temporary.objects.link(obj)
            obj.parent = root
            obj.matrix_world = source.matrix_world
            if procedural_baker:
                procedural_baker.bake(obj)
                mesh = obj.data
            if alignment is not None:
                reflection = reflect_runtime_print(mesh, source)
                if reflection:
                    reflected_prints.append(reflection)
            obj.matrix_world = (object_bases.get(name, basis) if object_bases else basis) @ source.matrix_world
            for vertex in mesh.vertices:
                p = obj.matrix_world @ vertex.co
                # Metadata uses the same standard glTF Y-up axes as the export.
                for axis, value in enumerate((p.x, p.z, -p.y)):
                    low[axis] = min(low[axis], value)
                    high[axis] = max(high[axis], value)
        if not all(math.isfinite(value) for value in low + high):
            raise RuntimeError(f"{slug}: empty or non-finite geometry bounds")
        bounds = {"min": low, "max": high}
        if smooth_house_edges:
            drift = max(abs(a-b) for a,b in zip(low+high, reference_low+reference_high))
            if drift > 1e-6:
                raise RuntimeError(f"house polish changes authored bounds by {drift} metres")
        root["bounds_min_y_up"] = low
        root["bounds_max_y_up"] = high
        root["authoring_units"] = "metres"
        output = output_dir / ("board" if kind == "board" else "buildings") / f"{slug}.glb"
        output.parent.mkdir(parents=True, exist_ok=True)
        result = bpy.ops.export_scene.gltf(
            filepath=str(output), export_format="GLB", export_materials="EXPORT",
            # Evaluated text tangents are unstable across repeated Blender
            # exports. This derivative has no authored normal maps; the modern
            # shader uses its supported UV-derivative basis if tangents are absent.
            export_texcoords=True, export_normals=True, export_tangents=alignment is None,
            export_cameras=False, export_lights=False, export_animations=False,
            export_skins=False, export_morph=False, export_extras=True,
            export_yup=True, export_apply=False, use_selection=False,
            collection=temporary.name,
        )
        if "FINISHED" not in result:
            raise RuntimeError(f"{slug}: glTF export failed: {result}")
        if alignment is not None:
            contents = output.read_bytes()
            document = json.loads(contents[20:20 + struct.unpack_from("<I", contents, 12)[0]])
            for material in document.get("materials", []):
                mascot = material.get("name") == "MP · mascot_print · runtime alpha mask"
                expected = "MASK" if mascot else "OPAQUE"
                if material.get("alphaMode", "OPAQUE") != expected:
                    raise RuntimeError("runtime derivative alpha mode does not match its material policy")
                if mascot and (material.get("alphaCutoff", 0.5) != 0.5 or
                               "baseColorTexture" not in material.get("pbrMetallicRoughness", {})):
                    raise RuntimeError("mascot mask must retain the authored colour/alpha texture and cutoff")
        manifest = {
            "asset_contract_version": CONTRACT_VERSION,
            "asset_kind": kind, "asset_slug": slug,
            "source_collections": collection_names,
            "geometry_object_count": len(sources),
            "bounds_y_up_metres": bounds,
            "materials": "Unbaked Principled factors; procedural textures are not preserved",
            "animations": False,
        }
        if smooth_house_edges:
            manifest["house_edge_polish"] = {"source_scene_unchanged": True,
                "source_blend_sha256": hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),
                "bounds_max_drift_metres": drift, "objects": house_records,
                "normal_policy": "four-segment authored bevels, hardened and face-influenced weighted normals"}
        if alignment is not None:
            manifest["retail_alignment"] = alignment
            manifest["materials"] = "Authored mascot image alpha masked at 0.5; other Principled alpha/transmission approximated as opaque; procedural textures are not preserved"
            manifest["runtime_material_adaptations"] = material_records
            manifest["authoring_materials_preserved"] = True
            manifest["tangent_policy"] = "omitted; runtime supports UV-derivative basis"
        if alignment is not None:
            manifest["runtime_print_parity"] = {
                "policy": "source-local Y reflected on copied FONT/mascot meshes for retail LH projection",
                "source_scene_unchanged": True, "whole_board_reflected": False,
                "reflected_print_count": len(reflected_prints), "objects": reflected_prints}
        if procedural_baker:
            manifest["source_blend_sha256"] = hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()
            manifest["procedural_export_validation"] = procedural_baker.validate_export(output)
            manifest["materials"] = "Authored mascot alpha masked at 0.5; actual linked procedural graphs baked; constant factors retained; other alpha/transmission approximated as opaque"
            manifest["procedural_material_bake"] = procedural_baker.records
            manifest["procedural_material_bake_policy"] = "actual linked source graphs baked before calibration; constant factors and existing images retained"
            manifest["procedural_bake_texture_directory"] = "board-baked-textures"
        output.with_suffix(".json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        print("EXPORTED_STATIC", slug, "bytes", output.stat().st_size, "bounds", bounds)
    finally:
        for obj in list(temporary.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(temporary)
        for mesh in created_meshes:
            if mesh.users == 0:
                bpy.data.meshes.remove(mesh)
        for material in created_materials.values():
            if material.users == 0:
                bpy.data.materials.remove(material)


def root_for_collection(collection):
    roots = [obj for obj in collection.objects if obj.parent is None]
    if len(roots) != 1:
        raise RuntimeError(
            f"{collection.name}: expected one root object, got {len(roots)}"
        )
    return roots[0]


def select_collection(collection):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in collection.all_objects:
        obj.select_set(True)
    root = root_for_collection(collection)
    bpy.context.view_layer.objects.active = root
    return root


def export_token(collection_name, slug, legacy_index, output_dir):
    collection = bpy.data.collections.get(collection_name)
    if collection is None:
        raise RuntimeError(f"missing Blender collection: {collection_name}")

    root = select_collection(collection)
    original_matrix = root.matrix_world.copy()
    tracked_keys = (
        "asset_kind",
        "asset_slug",
        "legacy_token_index",
        "asset_contract_version",
    )
    saved_properties = {
        key: root[key] for key in tracked_keys if key in root
    }
    introduced = {key for key in tracked_keys if key not in root}

    try:
        root.matrix_world = Matrix.Identity(4)
        root["asset_kind"] = "token"
        root["asset_slug"] = slug
        root["legacy_token_index"] = legacy_index
        root["asset_contract_version"] = CONTRACT_VERSION

        output = output_dir / "tokens" / f"{slug}.glb"
        output.parent.mkdir(parents=True, exist_ok=True)

        result = bpy.ops.export_scene.gltf(
            filepath=str(output),
            export_format="GLB",
            export_materials="EXPORT",
            export_texcoords=True,
            export_normals=True,
            export_tangents=True,
            export_attributes=True,
            export_cameras=False,
            export_lights=False,
            export_animations=True,
            export_morph=True,
            export_morph_normal=True,
            export_morph_tangent=False,
            export_morph_animation=True,
            export_skins=True,
            export_extras=True,
            export_yup=True,
            export_apply=False,
            use_selection=False,
            collection=collection.name,
        )

        if "FINISHED" not in result:
            raise RuntimeError(
                f"{collection_name}: glTF export failed: {result}"
            )
        print(
            "EXPORTED",
            slug,
            "legacy_token",
            legacy_index,
            "bytes",
            output.stat().st_size,
            output,
        )
    finally:
        root.matrix_world = original_matrix
        for key in introduced:
            if key in root:
                del root[key]
        for key, value in saved_properties.items():
            root[key] = value
        bpy.ops.object.select_all(action="DESELECT")


def blender_arguments():
    import sys
    if "--" not in sys.argv:
        return []
    return sys.argv[sys.argv.index("--") + 1:]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        required=True,
        help="Directory receiving generated GLB assets",
    )
    parser.add_argument("--include-board", action="store_true", help="Export board and 40 squares, excluding scenery")
    parser.add_argument("--align-retail-board", action="store_true",
                        help="Export a separate paris_board_runtime derivative fitted to retail cells")
    parser.add_argument("--retail-board-obj", type=Path,
                        default=Path(__file__).resolve().parents[2] / "build/retail-reference-obj/boardmed.obj",
                        help="production-decoder boardmed OBJ used to verify measured alignment")
    parser.add_argument("--include-house", action="store_true", help="Export one grounded gameplay-house prototype")
    parser.add_argument("--smooth-house-edges", action="store_true",
                        help="House-only build candidate: four-segment authored bevels and weighted normals on copies")
    parser.add_argument("--skip-tokens", action="store_true", help="Export only explicitly requested static groups")
    parser.add_argument("--bake-procedural", action="store_true", help="Bake source procedural graphs on copies; stage build-only candidates transactionally")
    args = parser.parse_args(blender_arguments())

    output_dir = Path(args.output).resolve()
    if args.smooth_house_edges:
        if not args.skip_tokens or not args.include_house or args.include_board or args.align_retail_board or args.bake_procedural:
            raise RuntimeError("house edge polish requires --skip-tokens --include-house only")
        if not output_dir.is_relative_to(Path(__file__).resolve().parents[2] / "build"):
            raise RuntimeError("house edge candidates must remain under modern/build")
    staging = None
    baker = None
    requested_output = output_dir
    source_path = Path(bpy.data.filepath)
    before = hashlib.sha256(source_path.read_bytes()).hexdigest() if args.bake_procedural or args.smooth_house_edges else None
    if args.bake_procedural:
        if not args.skip_tokens or not args.align_retail_board or args.include_board or args.include_house:
            raise RuntimeError("procedural board bake requires --skip-tokens --align-retail-board only")
        if not output_dir.is_relative_to(Path(__file__).resolve().parents[2] / "build"):
            raise RuntimeError("procedural candidates must remain under modern/build")
        output_dir.mkdir(parents=True, exist_ok=True)
        staging = tempfile.TemporaryDirectory(prefix=".procedural-stage-", dir=output_dir)
        output_dir = Path(staging.name)
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from bake_monopoly_materials import ProceduralExportBaker
        baker = ProceduralExportBaker(output_dir / "board-baked-textures")
    for collection_name, (slug, legacy_index) in ({} if args.skip_tokens else TOKEN_ASSETS).items():
        export_token(
            collection_name,
            slug,
            legacy_index,
            output_dir,
        )

    if args.include_board:
        export_static_group(board_collections(), "board", "paris_board", output_dir)
    if args.align_retail_board:
        collections = board_collections()
        bases, alignment = runtime_board_bases(collections, args.retail_board_obj)
        export_static_group(collections, "board", "paris_board_runtime", output_dir,
                            object_bases=bases, alignment=alignment, procedural_baker=baker)
    if args.include_house:
        name = "Maison jeu avant 00"
        collection = bpy.data.collections.get(name)
        if collection is None:
            raise RuntimeError(f"missing Blender collection: {name}")
        export_static_group([name], "building", "house", output_dir, root_for_collection(collection),
                            smooth_house_edges=args.smooth_house_edges)
    if args.smooth_house_edges and hashlib.sha256(source_path.read_bytes()).hexdigest() != before:
        raise RuntimeError("recovered source changed during house edge export")

    missing = [
        ("cannon", 0),
        ("iron", 4),
        ("horse", 5),
        ("wheelbarrow", 9),
        ("moneybag", 10),
    ]
    print("MISSING_TOKEN_ASSETS", missing)
    if baker:
        baker.close()
        if hashlib.sha256(source_path.read_bytes()).hexdigest() != before:
            raise RuntimeError("recovered source changed during procedural export")
        for path in sorted(output_dir.rglob("*")):
            if path.is_file():
                destination = requested_output / path.relative_to(output_dir)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)
        staging.cleanup()


if __name__ == "__main__":
    main()

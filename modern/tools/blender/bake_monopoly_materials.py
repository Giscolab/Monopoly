"""Bake recovered procedural material subsets on disposable evaluated copies.

Run with Blender --background --factory-startup --disable-autoexec --python
this_script -- --source recovered.blend --output-root modern/build/material-bake.
The source and canonical assets are never saved or replaced.
Recovered tokens and the house prototype have factor-only graphs. For a real
procedural bake select --collection "01 · Plateau carré" --object
"01 · Plateau carré / asphalt" --slug board_asphalt.
"""
import argparse
from array import array
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Matrix


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--collection", default="Pion bottine")
    parser.add_argument("--slug", default="boot")
    parser.add_argument("--resolution", type=int, choices=(512, 1024), default=512)
    parser.add_argument("--inspect-only", action="store_true")
    parser.add_argument("--inspect-materials", action="store_true")
    parser.add_argument("--object", action="append", default=[], help="Limit to exact mesh name(s) in collection")
    parser.add_argument("--allow-factor-bake", action="store_true", help="Explicit pipeline self-test; no procedural claim")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def principled(material):
    if not material or not material.use_nodes:
        raise RuntimeError("bake requires a node material")
    outputs = [node for node in material.node_tree.nodes
               if node.type == "OUTPUT_MATERIAL" and node.is_active_output]
    if len(outputs) != 1 or not outputs[0].inputs["Surface"].is_linked:
        raise RuntimeError(f"{material.name}: missing active surface")
    shader = outputs[0].inputs["Surface"].links[0].from_node
    if shader.type != "BSDF_PRINCIPLED":
        raise RuntimeError(f"{material.name}: only direct Principled surface is supported")
    return shader, outputs[0]


def inspect(collection, object_names=()):
    records = []
    for obj in sorted(collection.all_objects, key=lambda item: item.name):
        if obj.type != "MESH":
            continue
        if object_names and obj.name not in object_names:
            continue
        materials = []
        for material in obj.data.materials:
            shader, _ = principled(material)
            materials.append({
                "name": material.name,
                "nodes": sorted(node.bl_idname for node in material.node_tree.nodes),
                "channels": {name: {"linked": shader.inputs[name].is_linked,
                    "sources": [link.from_node.bl_idname for link in shader.inputs[name].links]}
                    for name in ("Base Color", "Metallic", "Roughness", "Normal")},
            })
        records.append({"object": obj.name, "vertices": len(obj.data.vertices),
            "uv_layers": [layer.name for layer in obj.data.uv_layers], "materials": materials})
    return records


def image(name, resolution, color_space):
    result = bpy.data.images.new(name, width=resolution, height=resolution, alpha=True, float_buffer=False)
    result.colorspace_settings.name = color_space
    return result


def target(material, target_image):
    nodes = material.node_tree.nodes
    for node in nodes:
        node.select = False
    node = nodes.new("ShaderNodeTexImage")
    node.image = target_image
    node.select = True
    nodes.active = node


def save_image(image_data, destination):
    image_data.filepath_raw = str(destination)
    image_data.file_format = "PNG"
    image_data.save()
    pixels = array("f", [0.0]) * (image_data.size[0] * image_data.size[1] * 4)
    image_data.pixels.foreach_get(pixels)
    covered = [pixel for pixel in range(image_data.size[0] * image_data.size[1])
               if pixels[pixel * 4 + 3] > 0]
    ranges = [[min(pixels[p * 4 + channel] for p in covered),
               max(pixels[p * 4 + channel] for p in covered)]
              for channel in range(3)] if covered else []
    return {"file": destination.name, "sha256": digest(destination),
        "pixel_sha256": hashlib.sha256(pixels.tobytes()).hexdigest(),
        "size": list(image_data.size), "color_space": image_data.colorspace_settings.name,
        "covered_pixel_count": len(covered), "covered_rgb_ranges": ranges}


def select_object(obj):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def sanitize_evaluated_mesh(mesh):
    """Retain visible triangles and authored split normals; discard collapsed
    evaluated modifier faces before UV unwrap/tangent generation. The source
    mesh is never changed. One hundred-millionth square metre is far below
    the 512/1024 texture's texel scale on this metre-authored scene.
    """
    mesh.calc_loop_triangles()
    minimum_area = 1e-8
    triangles = []
    removed = []
    for triangle in mesh.loop_triangles:
        points = [mesh.vertices[index].co for index in triangle.vertices]
        area = (points[1] - points[0]).cross(points[2] - points[0]).length / 2
        (removed if area <= minimum_area else triangles).append((triangle, area))
    if not triangles:
        raise RuntimeError("evaluated source contains no noncollapsed triangles")
    result = bpy.data.meshes.new(mesh.name + " · noncollapsed")
    result.from_pydata([vertex.co[:] for vertex in mesh.vertices], [],
        [list(triangle.vertices) for triangle, _ in triangles])
    for material in mesh.materials:
        result.materials.append(material)
    split_normals = []
    for polygon, (triangle, _) in zip(result.polygons, triangles):
        original = mesh.polygons[triangle.polygon_index]
        polygon.material_index = original.material_index
        polygon.use_smooth = original.use_smooth
        split_normals.extend(mesh.corner_normals[loop].vector[:] for loop in triangle.loops)
    result.normals_split_custom_set(split_normals)
    for old_uv in mesh.uv_layers:
        new_uv = result.uv_layers.new(name=old_uv.name)
        for triangle_index, (triangle, _) in enumerate(triangles):
            for corner, loop in enumerate(triangle.loops):
                new_uv.data[triangle_index * 3 + corner].uv = old_uv.data[loop].uv
        new_uv.active_render = old_uv.active_render
    result.update()
    evidence = {"minimum_triangle_area_local_m2": minimum_area,
        "source_triangle_count": len(mesh.loop_triangles), "removed_collapsed_triangles": len(removed),
        "removed_area_sum_local_m2": sum(area for _, area in removed),
        "retained_triangle_count": len(triangles), "preserved_split_normals": True}
    return result, evidence


def bake_channel(obj, originals, channel, resolution):
    results = []
    materials = []
    for index, original in enumerate(originals):
        material = original.copy()
        shader, output = principled(material)
        baked = image(f"{obj.name}_{index}_{channel}", resolution,
            "sRGB" if channel == "Base Color" else "Non-Color")
        target(material, baked)
        if channel != "Normal":
            socket = shader.inputs[channel]
            emission = material.node_tree.nodes.new("ShaderNodeEmission")
            if socket.is_linked:
                material.node_tree.links.new(socket.links[0].from_socket, emission.inputs["Color"])
            else:
                value = socket.default_value
                emission.inputs["Color"].default_value = (
                    tuple(value) if channel == "Base Color" else (value, value, value, 1.0))
            for link in list(output.inputs["Surface"].links):
                material.node_tree.links.remove(link)
            material.node_tree.links.new(emission.outputs[0], output.inputs["Surface"])
        obj.data.materials[index] = material
        materials.append(material)
        results.append(baked)
    select_object(obj)
    status = bpy.ops.object.bake(type="NORMAL" if channel == "Normal" else "EMIT",
        margin=8, margin_type="EXTEND", use_clear=True, use_selected_to_active=False,
        normal_space="TANGENT", normal_r="POS_X", normal_g="POS_Y", normal_b="POS_Z",
        uv_layer="BakeUV")
    if "FINISHED" not in status:
        raise RuntimeError(f"{obj.name}: {channel} bake failed: {status}")
    for index, original in enumerate(originals):
        obj.data.materials[index] = original
    for material in materials:
        bpy.data.materials.remove(material)
    return results


def rebuild_material(original, color, packed, normal):
    old, _ = principled(original)
    material = bpy.data.materials.new(original.name + " · baked")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    shader = next(node for node in nodes if node.type == "BSDF_PRINCIPLED")
    # Preserve factors not baked. Inputs requiring another bake are rejected
    # rather than silently losing linked authoring semantics.
    for socket in old.inputs:
        if socket.name in {"Base Color", "Metallic", "Roughness", "Normal"}:
            continue
        if socket.is_linked:
            raise RuntimeError(f"{original.name}: unsupported linked {socket.name}")
        if socket.name in shader.inputs:
            try:
                shader.inputs[socket.name].default_value = socket.default_value
            except (AttributeError, TypeError):
                pass
    shader.inputs["Base Color"].default_value = (1, 1, 1, 1)
    shader.inputs["Metallic"].default_value = 1
    shader.inputs["Roughness"].default_value = 1
    for baked, input_name in ((color, "Base Color"),):
        node = nodes.new("ShaderNodeTexImage")
        node.image = baked
        node.interpolation = "Linear"
        node.extension = "EXTEND"
        material.node_tree.links.new(node.outputs["Color"], shader.inputs[input_name])
    node = nodes.new("ShaderNodeTexImage")
    node.image = packed
    node.interpolation = "Linear"
    node.extension = "EXTEND"
    separate = nodes.new("ShaderNodeSeparateColor")
    separate.mode = "RGB"
    material.node_tree.links.new(node.outputs["Color"], separate.inputs["Color"])
    material.node_tree.links.new(separate.outputs["Green"], shader.inputs["Roughness"])
    material.node_tree.links.new(separate.outputs["Blue"], shader.inputs["Metallic"])
    if normal is not None:
        node = nodes.new("ShaderNodeTexImage")
        node.image = normal
        node.interpolation = "Linear"
        node.extension = "EXTEND"
        mapping = nodes.new("ShaderNodeNormalMap")
        mapping.space = "TANGENT"
        mapping.uv_map = "BakeUV"
        material.node_tree.links.new(node.outputs["Color"], mapping.inputs["Color"])
        material.node_tree.links.new(mapping.outputs["Normal"], shader.inputs["Normal"])
    return material


def bake_object(obj, resolution, directory):
    originals = list(obj.data.materials)
    if not originals:
        raise RuntimeError(f"{obj.name}: no material")
    for material in originals:
        shader, _ = principled(material)
        if shader.inputs["Alpha"].is_linked or shader.inputs["Alpha"].default_value != 1:
            raise RuntimeError("alpha materials need a separate alpha bake")
    colors = bake_channel(obj, originals, "Base Color", resolution)
    metallic = bake_channel(obj, originals, "Metallic", resolution)
    roughness = bake_channel(obj, originals, "Roughness", resolution)
    needs_normals = [principled(m)[0].inputs["Normal"].is_linked for m in originals]
    normals = bake_channel(obj, originals, "Normal", resolution) if any(needs_normals) else None
    records = []
    pixel_count = resolution * resolution
    for index, original in enumerate(originals):
        packed = image(f"{obj.name}_{index}_metallic_roughness", resolution, "Non-Color")
        metals = array("f", [0.0]) * (pixel_count * 4)
        roughs = array("f", [0.0]) * (pixel_count * 4)
        values = array("f", [0.0]) * (pixel_count * 4)
        metallic[index].pixels.foreach_get(metals)
        roughness[index].pixels.foreach_get(roughs)
        for pixel in range(pixel_count):
            offset = pixel * 4
            values[offset:offset + 4] = array("f", (1.0, roughs[offset], metals[offset], 1.0))
        packed.pixels.foreach_set(values)
        normal = normals[index] if normals is not None and needs_normals[index] else None
        prefix = f"{obj.name.replace('/', '_')}_{index}"
        maps = {"base_color": save_image(colors[index], directory / f"{prefix}_base.png"),
            "metallic_roughness": save_image(packed, directory / f"{prefix}_mr.png")}
        if normal is not None:
            maps["normal"] = save_image(normal, directory / f"{prefix}_normal.png")
        obj.data.materials[index] = rebuild_material(original, colors[index], packed, normal)
        records.append({"source_material": original.name, "normal_source_linked": needs_normals[index],
            "packed_channels": {"R": "reserved constant 1 (no occlusion map)", "G": "roughness", "B": "metallic"},
            "maps": maps})
    bake_uv = obj.data.uv_layers.get("BakeUV")
    for layer in list(obj.data.uv_layers):
        if layer != bake_uv:
            obj.data.uv_layers.remove(layer)
    bake_uv.active_render = True
    obj.data.uv_layers.active_index = 0
    return records


def glb_evidence(path):
    data = path.read_bytes()
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise RuntimeError("missing GLB JSON chunk")
    document = json.loads(data[20:20 + json_length])
    binary_offset = 20 + json_length + 8
    binary = data[binary_offset:]
    geometry = hashlib.sha256()
    triangle_count = 0
    minimum_uv_area = float("inf")
    def accessor_values(index):
        accessor = document["accessors"][index]
        view = document["bufferViews"][accessor["bufferView"]]
        dimensions = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[accessor["type"]]
        component = {5121: "B", 5123: "H", 5125: "I", 5126: "f"}[accessor["componentType"]]
        stride = view.get("byteStride", dimensions * struct.calcsize(component))
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return [struct.unpack_from("<" + component * dimensions, binary, offset + row * stride)
                for row in range(accessor["count"])]
    for mesh in document.get("meshes", []):
        for primitive in mesh["primitives"]:
            for _, accessor_index in sorted(primitive["attributes"].items()):
                accessor = document["accessors"][accessor_index]
                view = document["bufferViews"][accessor["bufferView"]]
                geometry.update(binary[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]])
            accessor = document["accessors"][primitive["indices"]]
            view = document["bufferViews"][accessor["bufferView"]]
            geometry.update(binary[view.get("byteOffset", 0):view.get("byteOffset", 0) + view["byteLength"]])
            if "TEXCOORD_0" not in primitive["attributes"]:
                raise RuntimeError("baked glTF primitive needs UV0")
            uv = accessor_values(primitive["attributes"]["TEXCOORD_0"])
            indices = [value[0] for value in accessor_values(primitive["indices"])]
            for offset in range(0, len(indices), 3):
                a, b, c = [uv[index] for index in indices[offset:offset + 3]]
                area = abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) / 2
                if not math.isfinite(area) or area == 0:
                    raise RuntimeError("export contains degenerate UV triangles; normal basis is undefined")
                minimum_uv_area = min(minimum_uv_area, area)
                triangle_count += 1
    if any("uri" in image_record for image_record in document.get("images", [])):
        raise RuntimeError("GLB export unexpectedly contains external image paths")
    return {"glb_sha256": digest(path), "geometry_sha256": geometry.hexdigest(),
        "validated_uv_triangle_count": triangle_count, "minimum_uv_triangle_area": minimum_uv_area,
        "tangent_strategy": "absent authored tangents; runtime derives basis from nondegenerate UV0",
        "nodes": len(document.get("nodes", [])), "meshes": len(document.get("meshes", [])),
        "materials": document.get("materials", []), "images": document.get("images", [])}


def bake_collection(args, source, before, collection, inspection):
    output = args.output_root.resolve()
    build_root = Path(__file__).resolve().parents[2] / "build"
    if not output.is_relative_to(build_root):
        raise RuntimeError("bake outputs must stay under modern/build; canonical assets are protected")
    if not re.fullmatch(r"[a-z0-9_-]+", args.slug):
        raise RuntimeError("slug must be a simple lower-case file name")
    output.mkdir(parents=True, exist_ok=True)
    linked = any(channel["linked"] for record in inspection for material in record["materials"]
                 for channel in material["channels"].values())
    if not linked and not args.allow_factor_bake:
        raise RuntimeError("selected source has factor-only materials; baking is unnecessary (self-test needs --allow-factor-bake)")
    source_scene = bpy.context.scene
    scene = bpy.data.scenes.new("MATERIAL_BAKE_SUBSET")
    bpy.context.window.scene = scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 1
    scene.cycles.seed = 0
    scene.cycles.use_animated_seed = False
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.render.threads_mode = "FIXED"
    scene.render.threads = 1
    scene.render.bake.use_selected_to_active = False
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    root_sources = [obj for obj in collection.objects if obj.parent is None]
    if len(root_sources) != 1:
        raise RuntimeError("selected collection requires exactly one source root")
    source_root = root_sources[0]
    original_world = {obj.name: obj.matrix_world.copy() for obj in collection.all_objects}
    # Evaluate in the source scene, then bake in a scene containing only copies.
    bpy.context.window.scene = source_scene
    depsgraph = bpy.context.evaluated_depsgraph_get()
    meshes = []
    geometry_cleanup = []
    for name in sorted(record["object"] for record in inspection):
        original = bpy.data.objects[name]
        evaluated = original.evaluated_get(depsgraph)
        mesh = bpy.data.meshes.new_from_object(evaluated, depsgraph=depsgraph)
        cleaned, cleanup = sanitize_evaluated_mesh(mesh)
        bpy.data.meshes.remove(mesh)
        mesh = cleaned
        geometry_cleanup.append({"source_object": name, **cleanup})
        obj = bpy.data.objects.new(name + " · bake", mesh)
        scene.collection.objects.link(obj)
        obj.matrix_world = original_world[name]
        obj["source_name"] = name
        meshes.append(obj)
    bpy.context.window.scene = scene
    material_records = []
    for obj in meshes:
        select_object(obj)
        previous_uv = obj.data.uv_layers.active
        uv = obj.data.uv_layers.new(name="BakeUV")
        obj.data.uv_layers.active = uv
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.03,
            area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
        bpy.ops.object.mode_set(mode="OBJECT")
        if previous_uv is not None:
            previous_uv.active_render = True
        material_records.append({"source_object": obj["source_name"],
            "materials": bake_object(obj, args.resolution, output)})
    root = bpy.data.objects.new(args.slug, None)
    scene.collection.objects.link(root)
    root.matrix_world = Matrix.Identity(4)
    for obj in meshes:
        world = obj.matrix_world.copy()
        obj.parent = root
        obj.matrix_world = source_root.matrix_world.inverted() @ world
    output_glb = output / f"{args.slug}.glb"
    status = bpy.ops.export_scene.gltf(filepath=str(output_glb), export_format="GLB",
        # Blender's Mikk exporter can emit zero tangents on thin bevel corners
        # even after nondegenerate geometry/UV validation. The renderer supports
        # a derivative basis; omit authored tangents instead of exporting zeros.
        export_materials="EXPORT", export_texcoords=True, export_normals=True, export_tangents=False,
        export_animations=False, export_skins=False, export_morph=False, export_cameras=False,
        export_lights=False, export_extras=True, export_yup=True, export_apply=False,
        use_selection=False, use_active_scene=True)
    if "FINISHED" not in status:
        raise RuntimeError(f"embedded GLB export failed: {status}")
    after = digest(source)
    if after != before:
        raise RuntimeError("source blend hash changed")
    manifest = {"source_sha256": before, "source_unchanged": True,
        "source_collection": collection.name, "source_inspection": inspection,
        "blender_version": bpy.app.version_string, "resolution": args.resolution,
        "bake": {"engine": "Cycles CPU", "samples": 1, "seed": 0, "threads": 1,
                 "margin_pixels": 8, "normal_space": "TANGENT +X +Y +Z",
                 "factor_only_self_test": not linked},
        "objects": material_records, "geometry_cleanup": geometry_cleanup,
        "export": glb_evidence(output_glb)}
    (output / f"{args.slug}.bake.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("BAKE_COMPLETE", json.dumps(manifest["export"], ensure_ascii=False))


def main():
    args = arguments()
    source = args.source.resolve(strict=True)
    before = digest(source)
    bpy.ops.wm.open_mainfile(filepath=str(source), load_ui=False, use_scripts=False)
    if args.inspect_materials:
        records = []
        for material in sorted(bpy.data.materials, key=lambda item: item.name):
            shader, _ = principled(material)
            linked = [name for name in ("Base Color", "Metallic", "Roughness", "Normal")
                      if shader.inputs[name].is_linked]
            if linked:
                records.append({"material": material.name, "linked_channels": linked,
                    "nodes": sorted(node.bl_idname for node in material.node_tree.nodes),
                    "objects": [{"object": obj.name, "collections": [c.name for c in obj.users_collection]}
                                for obj in bpy.data.objects if obj.type == "MESH" and material.name in
                                [m.name for m in obj.data.materials if m]]})
        print("BAKE_LINKED_MATERIALS", json.dumps(records, ensure_ascii=False))
        return
    if args.inspect_only and args.collection == "*":
        records = {collection.name: inspect(collection) for collection in bpy.data.collections
                   if collection.name.startswith("Pion ")}
        print("BAKE_INSPECTION", json.dumps(records, ensure_ascii=False))
        return
    collection = bpy.data.collections.get(args.collection)
    if collection is None:
        raise RuntimeError(f"missing source collection: {args.collection}")
    records = inspect(collection, args.object)
    print("BAKE_INSPECTION", json.dumps(records, ensure_ascii=False))
    if args.inspect_only:
        return
    if not records:
        raise RuntimeError("selected collection/object filter contains no meshes")
    bake_collection(args, source, before, collection, records)


if __name__ == "__main__":
    main()

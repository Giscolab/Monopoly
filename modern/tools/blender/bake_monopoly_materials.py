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
import shutil
import struct
import sys
import tempfile
import time
import uuid
from pathlib import Path

import bpy
from mathutils import Matrix, Vector


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
    parser.add_argument("--compare-render", action="store_true",
                        help="Render a fixed-camera original-graph/baked-map pair and record measured image differences")
    parser.add_argument("--export-bundle", action="store_true",
                        help="Stage and validate the aligned board and all three baked environment chunks as one build-only bundle")
    parser.add_argument("--retail-board-obj", type=Path,
                        default=Path(__file__).resolve().parents[2] / "build/retail-reference-obj/boardmed.obj",
                        help="Decoded retail board OBJ used by the aligned bundle export")
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
                    for name in ("Base Color", "Metallic", "Roughness", "Normal", "Emission Color", "Emission Strength", "Alpha")},
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
    mesh is never changed. Area and minimum-altitude limits discard modifier
    slivers at float precision, far below a baked texel on this metre scene.
    """
    mesh.calc_loop_triangles()
    minimum_area = 1e-8
    minimum_altitude = 2e-7
    triangles = []
    removed = []
    for triangle in mesh.loop_triangles:
        points = [mesh.vertices[index].co for index in triangle.vertices]
        area = (points[1] - points[0]).cross(points[2] - points[0]).length / 2
        longest_edge = max((points[1] - points[0]).length,
                           (points[2] - points[1]).length,
                           (points[0] - points[2]).length)
        altitude = 2 * area / longest_edge if longest_edge else 0
        (removed if area <= minimum_area or altitude <= minimum_altitude else triangles).append((triangle, area))
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
        "minimum_triangle_altitude_local_metres": minimum_altitude,
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
    shader.inputs["Base Color"].default_value = (1, 1, 1, 1) if color else old.inputs["Base Color"].default_value
    shader.inputs["Metallic"].default_value = 1 if packed else old.inputs["Metallic"].default_value
    shader.inputs["Roughness"].default_value = 1 if packed else old.inputs["Roughness"].default_value
    for baked, input_name in (((color, "Base Color"),) if color else ()):
        node = nodes.new("ShaderNodeTexImage")
        node.image = baked
        node.interpolation = "Linear"
        node.extension = "EXTEND"
        material.node_tree.links.new(node.outputs["Color"], shader.inputs[input_name])
    if packed:
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


class ProceduralExportBaker:
    """Bounded source-space bake for existing exporters' disposable copies.
    A private scene avoids evaluating the full authoring scene during each bake.
    Every object keeps its own Generated-coordinate domain; maps are not reused
    across geometrically different objects merely because a material is shared.
    """
    def __init__(self, directory, resolution=512):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.resolution = resolution
        self.records, self.materials, self.images, self.meshes = [], [], [], []
        self.started = time.monotonic()
        self.scene = bpy.data.scenes.new("PROCEDURAL_EXPORT_BAKE")
        self.scene.render.engine = "CYCLES"
        self.scene.cycles.device = "CPU"
        self.scene.cycles.samples = 1
        self.scene.cycles.seed = 0
        self.scene.cycles.use_animated_seed = False
        self.scene.cycles.use_adaptive_sampling = False
        self.scene.cycles.use_denoising = False
        self.scene.render.threads_mode = "FIXED"
        self.scene.render.threads = 1

    @staticmethod
    def procedural(socket):
        todo = [link.from_node for link in socket.links]
        seen = set()
        while todo:
            node = todo.pop()
            if node.name in seen:
                continue
            seen.add(node.name)
            if node.type in {"TEX_NOISE", "TEX_VORONOI", "TEX_WAVE", "TEX_MUSGRAVE"}:
                return True
            for inp in node.inputs:
                todo.extend(link.from_node for link in inp.links)
        return False

    def bake(self, obj):
        originals = list(obj.data.materials)
        if not originals:
            return False
        shaders = [principled(material)[0] for material in originals]
        channels = [{name for name in ("Base Color", "Metallic", "Roughness", "Normal")
                     if self.procedural(shader.inputs[name])} for shader in shaders]
        if not any(channels):
            return False
        # Re-UVing image/alpha-bearing mixed objects needs a separate atlas.
        # The audited recovered procedural objects use only Generated/Noise.
        for shader in shaders:
            if shader.inputs["Alpha"].is_linked or shader.inputs["Alpha"].default_value != 1:
                raise RuntimeError("procedural export bake cannot re-UV an alpha material")
        count = sum(len(c) - len(c & {"Metallic", "Roughness"}) + bool(c & {"Metallic", "Roughness"}) for c in channels)
        if len(self.images) + count > 192 or (len(self.images) + count) * self.resolution ** 2 * 4 > 240 * 1024 ** 2:
            raise RuntimeError("procedural bake image budget exceeded")
        if time.monotonic() - self.started > 600:
            raise RuntimeError("procedural bake time budget exceeded")
        old_scene = bpy.context.window.scene
        old_collections = tuple(obj.users_collection)
        old_parent = obj.parent
        old_world = obj.matrix_world.copy()
        old_mesh = obj.data
        mesh, cleanup = sanitize_evaluated_mesh(old_mesh)
        self.meshes.append(mesh)
        obj.data = mesh
        for collection in old_collections:
            collection.objects.unlink(obj)
        obj.parent = None
        obj.matrix_world = old_world
        self.scene.collection.objects.link(obj)
        bpy.context.window.scene = self.scene
        try:
            select_object(obj)
            previous_uv_name = mesh.uv_layers.active.name if mesh.uv_layers.active else None
            uv = mesh.uv_layers.new(name="BakeUV")
            mesh.uv_layers.active = uv
            bpy.ops.object.mode_set(mode="EDIT")
            bpy.ops.mesh.select_all(action="SELECT")
            bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.03,
                area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
            bpy.ops.object.mode_set(mode="OBJECT")
            if previous_uv_name:
                mesh.uv_layers[previous_uv_name].active_render = True
            results = {}
            for channel in ("Base Color", "Metallic", "Roughness", "Normal"):
                if any(channel in c for c in channels):
                    if channel == "Normal":
                        # Tangent-space NORMAL uses the active-render UV basis,
                        # whereas uv_layer selects the bake destination. Both
                        # must be the final BakeUV rather than an old source UV.
                        mesh.uv_layers["BakeUV"].active_render = True
                    results[channel] = bake_channel(obj, originals, channel, self.resolution)
            records = []
            for index, (original, needed) in enumerate(zip(originals, channels)):
                if not needed:
                    continue
                color = results["Base Color"][index] if "Base Color" in needed else None
                normal = results["Normal"][index] if "Normal" in needed else None
                packed = None
                if needed & {"Metallic", "Roughness"}:
                    raise RuntimeError("linked metallic/roughness procedural graphs require qualified packed bake")
                maps = {}
                for label, result in (("base_color", color), ("normal", normal)):
                    if result:
                        prefix = f"{len(self.records):03d}_{index}_{label}"
                        maps[label] = save_image(result, self.directory / f"{prefix}.png")
                        self.images.append(result)
                material = rebuild_material(original, color, packed, normal)
                self.materials.append(material)
                obj.data.materials[index] = material
                records.append({"source_material": original.name, "channels_baked": sorted(needed),
                    "metallic_roughness": "authored constant factors retained", "maps": maps})
            for name in [layer.name for layer in mesh.uv_layers]:
                if name != "BakeUV":
                    mesh.uv_layers.remove(mesh.uv_layers[name])
            mesh.uv_layers["BakeUV"].active_render = True
            mesh.uv_layers.active_index = 0
            self.records.append({"source_object": obj.name, "coordinate_domain": "evaluated source object before calibration",
                "resolution": self.resolution, "geometry_cleanup": cleanup, "materials": records})
            return True
        finally:
            self.scene.collection.objects.unlink(obj)
            obj.parent = old_parent
            obj.matrix_world = old_world
            for collection in old_collections:
                collection.objects.link(obj)
            bpy.context.window.scene = old_scene

    def validate_export(self, path):
        evidence = glb_evidence(path, normal_mapped_only=True)
        return {key: value for key, value in evidence.items()
                if key not in {"materials", "images"}}

    def close(self):
        bpy.data.scenes.remove(self.scene)
        for material in self.materials:
            if material.users == 0:
                bpy.data.materials.remove(material)
        for mesh in self.meshes:
            if mesh.users == 0:
                bpy.data.meshes.remove(mesh)
        for result in self.images:
            if result.users == 0:
                bpy.data.images.remove(result)


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
    for name in [layer.name for layer in obj.data.uv_layers]:
        if name != "BakeUV":
            obj.data.uv_layers.remove(obj.data.uv_layers[name])
    bake_uv = obj.data.uv_layers.get("BakeUV")
    bake_uv.active_render = True
    obj.data.uv_layers.active_index = 0
    return records


def glb_evidence(path, normal_mapped_only=False):
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
            if normal_mapped_only and "normalTexture" not in document["materials"][primitive["material"]]:
                continue
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
        "uv_validation_scope": "normal-mapped primitives" if normal_mapped_only else "all primitives",
        "tangent_strategy": "absent authored tangents; runtime derives basis from nondegenerate UV0",
        "nodes": len(document.get("nodes", [])), "meshes": len(document.get("meshes", [])),
        "materials": document.get("materials", []), "images": document.get("images", [])}


def compare_original_and_baked(scene, meshes, output, procedural_only=False):
    """Compare real authoring graphs and baked maps on identical evaluated
    geometry, camera and lighting. This measures an isolated Blender bake;
    it does not claim production renderer or full recovered-scene parity.
    """
    low = Vector((float("inf"),) * 3)
    high = Vector((-float("inf"),) * 3)
    for obj in meshes:
        for vertex in obj.data.vertices:
            point = obj.matrix_world @ vertex.co
            for axis in range(3):
                low[axis] = min(low[axis], point[axis])
                high[axis] = max(high[axis], point[axis])
    center = (low + high) * 0.5
    radius = max((high - low).length * 0.5, 0.001)
    camera_data = bpy.data.cameras.new("BAKE_COMPARE_CAMERA")
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = radius * 2.1
    camera_data.clip_start = max(radius * 0.0001, 0.00001)
    camera_data.clip_end = radius * 20
    camera = bpy.data.objects.new("BAKE_COMPARE_CAMERA", camera_data)
    scene.collection.objects.link(camera)
    camera.location = center + Vector((0.75, -1.15, 1.75)) * radius
    camera.rotation_euler = (center - camera.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = camera
    world = bpy.data.worlds.new("BAKE_COMPARE_WORLD")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.12, 0.12, 0.12, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.5
    scene.world = world
    sun_data = bpy.data.lights.new("BAKE_COMPARE_SUN", "SUN")
    sun_data.energy = 2
    sun_data.angle = 0.1
    sun = bpy.data.objects.new("BAKE_COMPARE_SUN", sun_data)
    scene.collection.objects.link(sun)
    sun.rotation_euler = (Vector((-0.3, 0.4, -1))).to_track_quat("-Z", "Y").to_euler()
    scene.cycles.samples = 16
    scene.cycles.seed = 0
    scene.cycles.use_denoising = False
    scene.render.resolution_x = scene.render.resolution_y = 256
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = "PNG"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    slots = [(obj, list(obj.data.materials)) for obj in meshes]

    def render(name):
        path = output / f"{name}.png"
        scene.render.filepath = str(path)
        status = bpy.ops.render.render(write_still=True)
        if "FINISHED" not in status:
            raise RuntimeError(f"comparison render failed: {status}")
        result = bpy.data.images.load(str(path), check_existing=False)
        values = array("f", [0.0]) * (256 * 256 * 4)
        result.pixels.foreach_get(values)
        bpy.data.images.remove(result)
        # Cycles writes timing metadata into PNGs. Pixel hashes qualify repeat
        # visual content separately from the exact captured-file provenance.
        return values, {"file": path.name, "sha256": digest(path),
                        "pixel_sha256": hashlib.sha256(values.tobytes()).hexdigest()}

    try:
        baked, baked_image = render("baked_materials")
        for obj in meshes:
            original = bpy.data.objects[obj["source_name"]]
            for index, material in enumerate(original.data.materials):
                if procedural_only:
                    shader, _ = principled(material)
                    if not any(ProceduralExportBaker.procedural(shader.inputs[name])
                               for name in ("Base Color", "Metallic", "Roughness", "Normal")):
                        continue
                obj.data.materials[index] = material
        original, original_image = render("original_materials")
    finally:
        for obj, materials in slots:
            for index, material in enumerate(materials):
                obj.data.materials[index] = material
    differences = [abs(a - b) for index, (a, b) in enumerate(zip(original, baked))
                   if index % 4 != 3]
    background = original[:3]
    foreground_pixels = [pixel for pixel in range(256 * 256)
        if any(abs(original[pixel * 4 + channel] - background[channel]) > 1 / 255 or
               abs(baked[pixel * 4 + channel] - background[channel]) > 1 / 255
               for channel in range(3))]
    foreground_differences = [abs(original[pixel * 4 + channel] - baked[pixel * 4 + channel])
        for pixel in foreground_pixels for channel in range(3)]
    if not foreground_differences:
        raise RuntimeError("comparison camera produced no material foreground pixels")
    return {"scope": "isolated real authoring graph versus baked maps on identical evaluated geometry; not engine/full-scene parity",
        "material_swap_scope": "procedural families only; existing constant/image/opaque adaptations held fixed" if procedural_only else "all source material slots",
        "original": original_image, "baked": baked_image,
        "resolution": [256, 256], "samples": 16, "seed": 0,
        "camera_location": list(camera.location), "camera_target": list(center),
        "orthographic_scale": camera_data.ortho_scale,
        "mean_absolute_channel_difference": sum(differences) / len(differences),
        "maximum_channel_difference": max(differences),
        "foreground_pixel_count": len(foreground_pixels),
        "foreground_mean_absolute_channel_difference": sum(foreground_differences) / len(foreground_differences),
        "difference_units": "normalized reloaded PNG RGB channels",
        "ao_map_created": False}


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
        previous_uv_name = obj.data.uv_layers.active.name if obj.data.uv_layers.active else None
        uv = obj.data.uv_layers.new(name="BakeUV")
        obj.data.uv_layers.active = uv
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.03,
            area_weight=0.0, correct_aspect=True, scale_to_bounds=False)
        bpy.ops.object.mode_set(mode="OBJECT")
        if previous_uv_name is not None:
            obj.data.uv_layers[previous_uv_name].active_render = True
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
    comparison = compare_original_and_baked(scene, meshes, output) if args.compare_render else None
    if digest(source) != before:
        raise RuntimeError("source blend hash changed during comparison")
    manifest = {"source_sha256": before, "source_unchanged": True,
        "source_collection": collection.name, "source_inspection": inspection,
        "blender_version": bpy.app.version_string, "resolution": args.resolution,
        "bake": {"engine": "Cycles CPU", "samples": 1, "seed": 0, "threads": 1,
                 "margin_pixels": 8, "normal_space": "TANGENT +X +Y +Z",
                 "factor_only_self_test": not linked},
        "objects": material_records, "geometry_cleanup": geometry_cleanup,
        "export": glb_evidence(output_glb)}
    if comparison is not None:
        manifest["original_baked_comparison"] = comparison
    (output / f"{args.slug}.bake.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("BAKE_COMPLETE", json.dumps(manifest["export"], ensure_ascii=False))


def export_bundle(args, source, source_digest):
    """Publish an owned, dedicated asset directory only after all four pass.

    The existing token/house asset directory is never merged or replaced.
    """
    output = args.output_root.resolve()
    build = Path(__file__).resolve().parents[2] / "build"
    if output == build or not output.is_relative_to(build):
        raise RuntimeError("procedural bundle output must be a dedicated directory under modern/build")
    if args.compare_render:
        raise RuntimeError("bundle comparison renders use the separate qualification workflow")
    marker = "procedural_bundle.json"
    allowed = {"board", "environment", "board-baked-textures", "environment-baked-textures", marker}
    if output.exists():
        if not (output / marker).is_file() or any(p.name not in allowed for p in output.iterdir()):
            raise RuntimeError("refusing to replace an asset directory not owned by this bundle exporter")
        prior = json.loads((output / marker).read_text(encoding="utf-8"))
        if prior.get("procedural_bundle_contract_version") != 1:
            raise RuntimeError("unrecognized existing procedural bundle")
    output.parent.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import export_monopoly_assets
    import export_environment
    previous_argv = sys.argv
    backup = None
    with tempfile.TemporaryDirectory(prefix=".procedural-bundle-", dir=output.parent) as temporary:
        staged = Path(temporary)
        try:
            sys.argv = [__file__, "--", "--output", str(staged), "--skip-tokens",
                        "--align-retail-board", "--bake-procedural",
                        "--retail-board-obj", str(args.retail_board_obj.resolve(strict=True))]
            export_monopoly_assets.main()
            sys.argv = [__file__, "--", "--source", str(source), "--output", str(staged),
                        "--bake-procedural"]
            export_environment.main()
        finally:
            sys.argv = previous_argv
        relatives = ["board/paris_board_runtime.glb", "environment/paris_fountain.glb",
                     "environment/paris_morris_column.glb", "environment/paris_station.glb"]
        assets = []
        decoded_bytes = 0
        for relative in relatives:
            path = staged / relative
            evidence = glb_evidence(path, normal_mapped_only=True)
            manifest = json.loads(path.with_suffix(".json").read_text(encoding="utf-8"))
            if manifest["source_blend_sha256"] != source_digest:
                raise RuntimeError("bundle source provenance mismatch")
            image_count = len(evidence["images"])
            data = path.read_bytes()
            json_length = struct.unpack_from("<I", data, 12)[0]
            document = json.loads(data[20:20 + json_length])
            binary = data[28 + json_length:]
            for image_record in document["images"]:
                view = document["bufferViews"][image_record["bufferView"]]
                offset = view.get("byteOffset", 0)
                if binary[offset:offset + 8] != b"\x89PNG\r\n\x1a\n":
                    raise RuntimeError("bundle image budget requires the actual exported PNG dimensions")
                width, height = struct.unpack_from(">II", binary, offset + 16)
                if not width or not height:
                    raise RuntimeError("invalid embedded image dimensions")
                decoded_bytes += width * height * 4
            assets.append({"file": relative, "sha256": digest(path), "bytes": path.stat().st_size,
                           "embedded_images": image_count,
                           "validated_normal_uv_triangles": evidence["validated_uv_triangle_count"]})
        if decoded_bytes > 240 * 1024 ** 2 or digest(source) != source_digest:
            raise RuntimeError("bundle image budget exceeded or source changed")
        report = {"procedural_bundle_contract_version": 1, "source_blend_sha256": source_digest,
                  "texture_resolution": [512, 512], "decoded_image_budget_bytes": decoded_bytes,
                  "scope": "actual source procedural base-colour/normal graphs; existing constant factors and opaque transmission approximations retained",
                  "assets": assets}
        (staged / marker).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if output.exists():
            backup = output.with_name(f".{output.name}.previous-{uuid.uuid4().hex}")
            output.rename(backup)
        try:
            staged.rename(output)
        except Exception:
            if backup is not None:
                backup.rename(output)
            raise
        if backup is not None:
            if not backup.resolve().is_relative_to(build) or backup.parent != output.parent:
                raise RuntimeError("unsafe bundle backup cleanup path")
            shutil.rmtree(backup)
        print("EXPORTED_PROCEDURAL_BUNDLE", json.dumps(report), flush=True)


def main():
    args = arguments()
    source = args.source.resolve(strict=True)
    before = digest(source)
    bpy.ops.wm.open_mainfile(filepath=str(source), load_ui=False, use_scripts=False)
    if args.export_bundle:
        export_bundle(args, source, before)
        return
    if args.inspect_materials:
        records = []
        for material in sorted(bpy.data.materials, key=lambda item: item.name):
            shader, _ = principled(material)
            linked = [name for name in ("Base Color", "Metallic", "Roughness", "Normal", "Emission Color", "Emission Strength", "Alpha")
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

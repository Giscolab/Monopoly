"""Deterministic new sculptures, not recovered retail/original geometry.

blender -b --python reconstruct_missing_tokens.py -- --output modern/build/modern-assets
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import struct

import bpy
from mathutils import Vector

VERSION = 1
TOKENS = {"cannon": 0, "iron": 4, "horse": 5, "wheelbarrow": 9, "moneybag": 10}
objects = []


def canonicalize_float_accessors(path):
    """Canonical vertex/triangle order and normals for equivalent BMesh output."""
    data = bytearray(path.read_bytes())
    json_length = struct.unpack_from("<I", data, 12)[0]
    document = json.loads(data[20:20+json_length])
    binary_start = 28 + json_length
    widths = {"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4,"MAT4":16}
    for accessor in document.get("accessors", []):
        if accessor["componentType"] != 5126:
            continue
        view = document["bufferViews"][accessor["bufferView"]]
        width = widths[accessor["type"]]
        stride = view.get("byteStride", width*4)
        start = binary_start + view.get("byteOffset",0) + accessor.get("byteOffset",0)
        for index in range(accessor["count"]):
            for component in range(width):
                offset = start + index*stride + component*4
                value = struct.unpack_from("<f",data,offset)[0]
                rounded = round(value,5)
                struct.pack_into("<f",data,offset,0.0 if rounded == 0 else rounded)

    def layout(index):
        accessor = document["accessors"][index]
        view = document["bufferViews"][accessor["bufferView"]]
        component = {5126:"f",5123:"H",5125:"I"}[accessor["componentType"]]
        width = widths[accessor["type"]]
        fmt = "<" + component*width
        start = binary_start + view.get("byteOffset",0) + accessor.get("byteOffset",0)
        return accessor,start,view.get("byteStride",struct.calcsize(fmt)),fmt

    def read(index):
        accessor,start,stride,fmt = layout(index)
        return [struct.unpack_from(fmt,data,start+i*stride) for i in range(accessor["count"])]

    new_binary, new_accessors, new_views = bytearray(), [], []

    def append(values, component_type, kind):
        new_binary.extend(b"\0"*((-len(new_binary))%4))
        offset = len(new_binary)
        fmt = "<" + {5126:"f",5123:"H",5125:"I"}[component_type]*widths[kind]
        for value in values:
            new_binary.extend(struct.pack(fmt,*value))
        view = len(new_views)
        new_views.append({"buffer":0,"byteOffset":offset,"byteLength":len(new_binary)-offset})
        index = len(new_accessors)
        new_accessors.append({"bufferView":view,"componentType":component_type,
                              "count":len(values),"type":kind,
                              "min":[min(v[i] for v in values) for i in range(widths[kind])],
                              "max":[max(v[i] for v in values) for i in range(widths[kind])]})
        return index

    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            attributes = {name:read(index) for name,index in primitive["attributes"].items()}
            positions = attributes["POSITION"]
            names = sorted(name for name in attributes if name != "NORMAL")
            keys = [tuple(attributes[name][i] for name in names) for i in range(len(positions))]
            first = {}
            for old,key in enumerate(keys):
                first.setdefault(key,old)
            ordered_keys = sorted(first)
            order = [first[key] for key in ordered_keys]
            canonical = {key:index for index,key in enumerate(ordered_keys)}
            source_indices = [value[0] for value in read(primitive["indices"])]
            triangles = []
            for i in range(0,len(source_indices),3):
                tri = tuple(canonical[keys[source_indices[i+j]]] for j in range(3))
                triangles.append(min(tri[j:]+tri[:j] for j in range(3)))
            triangles.sort()
            sorted_positions = [positions[old] for old in order]
            accumulated = {}
            for tri in triangles:
                a,b,c = [Vector(sorted_positions[i]) for i in tri]
                normal = (b-a).cross(c-a)
                for i in tri:
                    key = sorted_positions[i]
                    accumulated[key] = accumulated.get(key,Vector((0,0,0))) + normal
            new_attributes = {}
            for name in sorted(primitive["attributes"]):
                if name == "NORMAL":
                    values = []
                    for position in sorted_positions:
                        normal = accumulated.get(position,Vector((0,0,1)))
                        # Collapsed bevel/cap triangles can cancel exactly.
                        # Their deterministic unit fallback keeps every decoded
                        # vertex normal valid, including unused duplicate verts.
                        normal = normal.normalized() if normal.length_squared > 1e-12 else Vector((0,0,1))
                        values.append(tuple(0.0 if round(x,5) == 0 else round(x,5) for x in normal))
                else:
                    values = [attributes[name][old] for old in order]
                new_attributes[name] = append(values,5126,"VEC3" if name in {"POSITION","NORMAL"} else "VEC2")
            indices = [(index,) for tri in triangles for index in tri]
            primitive["indices"] = append(indices,5123 if len(order) <= 65535 else 5125,"SCALAR")
            primitive["attributes"] = new_attributes
    document["accessors"],document["bufferViews"] = new_accessors,new_views
    new_binary.extend(b"\0"*((-len(new_binary))%4))
    document["buffers"] = [{"byteLength":len(new_binary)}]
    binary = new_binary
    encoded = json.dumps(document,separators=(",",":"),ensure_ascii=True).encode("utf-8")
    encoded += b" "*((-len(encoded))%4)
    path.write_bytes(struct.pack("<III",0x46546c67,2,28+len(encoded)+len(binary)) +
                     struct.pack("<II",len(encoded),0x4e4f534a) + encoded +
                     struct.pack("<II",len(binary),0x004e4942) + binary)


def finish(obj, name, material, bevel=0):
    obj.name = name
    obj.data.materials.append(material)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = obj.modifiers.new("Soft cast edges", "BEVEL")
        mod.width, mod.segments = bevel, 3
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for face in obj.data.polygons:
        face.use_smooth = True
    objects.append(obj)
    return obj


def ellipsoid(name, location, scale, material):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, location=location)
    obj = bpy.context.object
    obj.scale = scale
    return finish(obj, name, material)


def box(name, location, scale, material):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    obj = bpy.context.object
    obj.scale = scale
    return finish(obj, name, material, .025)


def rod(name, start, end, radius, material, top=None):
    delta = Vector(end) - Vector(start)
    bpy.ops.mesh.primitive_cone_add(vertices=32, radius1=radius,
                                   radius2=radius if top is None else top,
                                   depth=delta.length, location=(Vector(start)+Vector(end))/2)
    obj = bpy.context.object
    obj.rotation_euler = delta.to_track_quat("Z", "Y").to_euler()
    return finish(obj, name, material, .008)


def ring(name, location, major, minor, material, rotation=(0, 0, 0)):
    bpy.ops.mesh.primitive_torus_add(major_segments=48, minor_segments=12,
                                   location=location, major_radius=major, minor_radius=minor,
                                   rotation=rotation)
    return finish(bpy.context.object, name, material)


def sculpt(slug, metal, inset):
    if slug == "cannon":
        # Cast carriage, two spoked wheels, raised barrel with recessed muzzle.
        box("Footplate", (-.05,0,.018), (.50,.23,.036), metal)
        box("Carriage", (-.07, 0, .14), (.24, .11, .12), metal)
        rod("Axle", (-.08, -.10, .15), (-.08, .10, .15), .025, metal)
        for y in [-.095, .095]:
            ring("Wheel rim", (-.08, y, .15), .105, .014, metal, (math.pi/2, 0, 0))
            for angle in range(0, 180, 30):
                a = math.radians(angle)
                rod("Wheel spoke", (-.08-.095*math.cos(a), y, .15-.095*math.sin(a)),
                    (-.08+.095*math.cos(a), y, .15+.095*math.sin(a)), .009, metal)
        start, end = Vector((-.15, 0, .18)), Vector((.22, 0, .57))
        delta = end-start
        rod("Barrel", start, end, .045, metal, .029)
        rotation = delta.to_track_quat("Z", "Y").to_euler()
        ring("Muzzle lip", end, .029, .007, metal, rotation)
        rod("Muzzle recess", end, end+delta.normalized()*.003, .021, inset)
        ellipsoid("Breech", start, (.05, .048, .05), metal)
    elif slug == "iron":
        # Pointed old flatiron sole and raised bridge handle.
        outline = [(-.50,-.34),(.28,-.34),(.60,0),(.28,.34),(-.50,.34)]
        vertices = [(x,y,z) for z in [0,.035] for x,y in outline]
        faces = [(4,3,2,1,0),(5,6,7,8,9)] + [(i,(i+1)%5,(i+1)%5+5,i+5) for i in range(5)]
        mesh = bpy.data.meshes.new("Pointed sole")
        mesh.from_pydata(vertices, [], faces)
        obj = bpy.data.objects.new("Pointed sole", mesh)
        bpy.context.collection.objects.link(obj)
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        finish(obj, "Pointed sole", metal, .010)
        box("Iron body", (-.08,0,.065), (.70,.50,.06), metal)
        rod("Handle back", (-.42,0,.07), (-.32,0,.49), .023, metal)
        rod("Handle front", (.32,0,.07), (.33,0,.47), .023, metal)
        rod("Handle grip", (-.32,0,.49), (.33,0,.47), .026, metal)
    elif slug == "horse":
        # Horse and rider: connected chrome sculpture, no implied rig.
        box("Sculpture pedestal", (0,0,.025), (.76,.60,.05), metal)
        body=ellipsoid("Body", (-.06,0,.43), (.25,.15,.13), metal)
        body.rotation_euler.y=-.45
        ellipsoid("Chest", (.13,0,.55), (.10,.15,.14), metal)
        rod("Neck", (.16,0,.57), (.23,0,.77), .08, metal, .055)
        ellipsoid("Head", (.28,0,.81), (.105,.058,.06), metal)
        for y in [-.045,.045]:
            rod("Ear", (.24,y,.84), (.21,y,.92), .022, metal, .006)
        for y in [-.14,.14]:
            rod("Hind leg", (-.23,y,.35), (-.30,y,.09), .035, metal, .025)
            box("Grounded hoof", (-.28,y,.072), (.11,.07,.044), metal)
            rod("Raised foreleg", (.14,y,.55), (.27,y,.65), .030, metal, .023)
            rod("Bent foreleg", (.27,y,.65), (.38,y,.58), .023, metal)
            box("Raised hoof", (.39,y,.57), (.085,.06,.04), metal)
        rod("Tail", (-.27,0,.40), (-.38,0,.28), .034, metal, .020)
        ellipsoid("Saddle", (-.05,0,.55), (.11,.17,.035), inset)
        ellipsoid("Rider torso", (-.08,0,.69), (.065,.06,.12), metal)
        ellipsoid("Rider head", (-.11,0,.86), (.055,.050,.06), metal)
        for y in [-.12,.12]:
            rod("Rider leg", (-.05,y*.55,.59), (-.13,y,.39), .026, metal)
            rod("Rider arm", (-.04,y*.5,.74), (.14,y*.45,.72), .021, metal)
    elif slug == "wheelbarrow":
        # Open tapered tray, visible single front wheel and two support legs.
        lower = [(-.25,-.16,.25),(.23,-.16,.25),(.23,.16,.25),(-.25,.16,.25)]
        upper = [(-.37,-.25,.43),(.31,-.25,.43),(.31,.25,.43),(-.37,.25,.43)]
        mesh = bpy.data.meshes.new("Open tray")
        mesh.from_pydata(lower+upper, [], [(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
        obj = bpy.data.objects.new("Open tray", mesh)
        bpy.context.collection.objects.link(obj)
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        finish(obj, "Open tray", metal)
        mod = obj.modifiers.new("Cast tray thickness", "SOLIDIFY")
        mod.thickness = .016
        bpy.ops.object.modifier_apply(modifier=mod.name)
        ring("Front wheel", (.39,0,.14), .115, .024, metal, (math.pi/2,0,0))
        rod("Wheel axle", (.39,-.09,.14), (.39,.09,.14), .032, metal)
        for y in [-.18,.18]:
            rod("Frame and handle", (.42,y*.3,.14), (-.65,y,.36), .022, metal)
            rod("Support leg", (-.21,y,.26), (-.27,y,.015), .025, metal)
        for angle in range(0,180,45):
            a = math.radians(angle)
            rod("Wheel spoke", (.39-.10*math.cos(a),0,.14-.10*math.sin(a)),
                (.39+.10*math.cos(a),0,.14+.10*math.sin(a)), .012, metal)
    elif slug == "moneybag":
        ellipsoid("Sack", (0,0,.25), (.325,.34,.25), metal)
        rod("Gathered neck", (0,0,.43), (0,0,.53), .105, metal, .075)
        for x in [-.07,.07]:
            knot=ellipsoid("Folded cloth tie", (x,0,.565), (.105,.087,.070), metal)
            knot.rotation_euler.y=.30 if x < 0 else -.30
        ring("Tied cord", (0,0,.49), .084, .012, inset)
        ellipsoid("Cord knot", (0,-.09,.49), (.027,.026,.023), metal)
        # Raised currency mark on the forward face.
        bpy.ops.object.text_add(location=(0,-.337,.23), rotation=(math.pi/2,0,0))
        text = bpy.context.object
        text.data.body, text.data.align_x, text.data.align_y = "$", "CENTER", "CENTER"
        text.data.size, text.data.extrude, text.data.bevel_depth = .20,.005,.002
        bpy.ops.object.convert(target="MESH")
        finish(bpy.context.object, "Raised dollar", metal)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--source", help="Optional read-only reference .blend; recorded by digest, never loaded or modified")
    parser.add_argument("--authoring", default="modern/build/authoring")
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--retail-reference-dir", help="Optional diagnostic OBJ directory for a height-matched comparison render")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    output, authoring = Path(args.output).resolve(), Path(args.authoring).resolve()
    reference_digest = hashlib.sha256(Path(args.source).read_bytes()).hexdigest() if args.source else None
    output.mkdir(parents=True, exist_ok=True)
    authoring.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system, scene.unit_settings.scale_length = "METRIC", 1
    metal = bpy.data.materials.new("Reconstructed polished chrome")
    metal.use_nodes = True
    bsdf = metal.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (.56,.59,.63,1)
    bsdf.inputs["Metallic"].default_value, bsdf.inputs["Roughness"].default_value = 1,.23
    inset = metal.copy()
    inset.name = "Reconstructed recessed metal"
    inset.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value = (.09,.10,.12,1)
    groups, manifest = {}, []
    for slug, index in TOKENS.items():
        objects.clear()
        sculpt(slug, metal, inset)
        collection = bpy.data.collections.new(f"Reconstructed {slug}")
        scene.collection.children.link(collection)
        root = bpy.data.objects.new(slug, None)
        collection.objects.link(root)
        root["asset_kind"], root["asset_slug"] = "token", slug
        root["legacy_token_index"], root["asset_contract_version"] = index, 1
        root["reconstructed"], root["reconstruction_version"] = True, VERSION
        root["provenance"] = "New procedural sculpture; not recovered retail geometry"
        root["float_accessor_decimal_places"] = 5
        if reference_digest:
            root["reference_blend_sha256"] = reference_digest
        for obj in objects:
            # Freeze quad diagonals before glTF's beauty triangulation; that
            # heuristic otherwise changes coplanar sphere faces between runs.
            bpy.context.view_layer.objects.active = obj
            for vertex in obj.data.vertices:
                vertex.co = tuple(round(value, 5) for value in vertex.co)
            triangulate = obj.modifiers.new("Deterministic triangles", "TRIANGULATE")
            triangulate.quad_method = "FIXED"
            triangulate.ngon_method = "CLIP"
            bpy.ops.object.modifier_apply(modifier=triangulate.name)
            for previous in list(obj.users_collection):
                previous.objects.unlink(obj)
            collection.objects.link(obj)
            obj.parent = root
        bpy.context.view_layer.update()
        low = [min((o.matrix_world @ v.co)[i] for o in objects for v in o.data.vertices) for i in range(3)]
        high = [max((o.matrix_world @ v.co)[i] for o in objects for v in o.data.vertices) for i in range(3)]
        # Ground geometry, leaving the scene root identity.
        for obj in objects:
            obj.location.z -= low[2]
        bpy.context.view_layer.update()
        bounds = {"min": [low[0],0,-high[1]], "max": [high[0],high[2]-low[2],-low[1]]}
        root["bounds_min_y_up"], root["bounds_max_y_up"] = bounds["min"], bounds["max"]
        root["authoring_units"] = "metres"
        groups[slug] = list(objects)
        path = output / "tokens" / f"{slug}.glb"
        path.parent.mkdir(parents=True, exist_ok=True)
        result = bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB",
            collection=collection.name, use_selection=False, export_materials="EXPORT",
            export_normals=True, export_texcoords=True, export_cameras=False,
            export_lights=False, export_animations=False, export_skins=False,
            export_morph=False, export_extras=True, export_yup=True, export_apply=False)
        if "FINISHED" not in result:
            raise RuntimeError(f"failed export {slug}")
        canonicalize_float_accessors(path)
        item = {"slug":slug,"legacy_token_index":index,"reconstructed":True,
                "version":VERSION,"bounds_y_up_metres":bounds,"bytes":path.stat().st_size}
        manifest.append(item)
        print("RECONSTRUCTED", json.dumps(item))
    (authoring / "missing_tokens_v1.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    bpy.ops.wm.save_as_mainfile(filepath=str(authoring / "missing_tokens_v1.blend"))
    if args.render:
        label_material = bpy.data.materials.new("Studio label graphite")
        label_material.use_nodes = True
        label_material.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value = (.01,.012,.016,1)
        for column,(slug,group) in enumerate(groups.items()):
            for obj in group:
                obj.location.x += (column-2)*1.45
                if args.retail_reference_dir:
                    obj.location.y -= .65
            bpy.ops.object.text_add(location=((column-2)*1.45,-.57,.005))
            label=bpy.context.object
            label.data.body, label.data.align_x, label.data.size = slug,"CENTER",.12
            label.data.materials.append(label_material)
            if args.retail_reference_dir:
                label.location.y -= .65
                reference = Path(args.retail_reference_dir) / f"{slug}.obj"
                bpy.ops.wm.obj_import(filepath=str(reference), forward_axis="NEGATIVE_Z", up_axis="Y")
                imported = list(bpy.context.selected_objects)
                for obj in imported:
                    obj.rotation_euler.z += math.pi/2
                bpy.context.view_layer.update()
                points = [obj.matrix_world @ vertex.co for obj in imported for vertex in obj.data.vertices]
                lo = [min(p[i] for p in points) for i in range(3)]
                hi = [max(p[i] for p in points) for i in range(3)]
                height = next(item["bounds_y_up_metres"]["max"][1] for item in manifest if item["slug"] == slug)
                factor = height/(hi[2]-lo[2])
                for obj in imported:
                    obj.data.materials.clear()
                    obj.data.materials.append(metal)
                    # Diagnostic copy only: orient and uniformly height-match.
                    matrix = obj.matrix_world.copy()
                    for vertex in obj.data.vertices:
                        point = matrix @ vertex.co
                        vertex.co = ((point.x-(lo[0]+hi[0])/2)*factor+(column-2)*1.45,
                                     (point.y-(lo[1]+hi[1])/2)*factor+.75,
                                     (point.z-lo[2])*factor)
                    obj.matrix_world.identity()
                bpy.ops.object.text_add(location=((column-2)*1.45,.30,.005))
                label=bpy.context.object
                label.data.body,label.data.align_x,label.data.size=f"retail {slug}","CENTER",.10
                label.data.materials.append(label_material)
        floor = bpy.data.materials.new("Studio floor")
        floor.use_nodes = True
        floor.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value = (.07,.085,.11,1)
        floor.node_tree.nodes.get("Principled BSDF").inputs["Roughness"].default_value = .75
        bpy.ops.mesh.primitive_plane_add(size=200)
        bpy.context.object.data.materials.append(floor)
        bpy.context.scene.world = bpy.data.worlds.new("Studio")
        scene.world.use_nodes=True
        scene.world.node_tree.nodes["Background"].inputs[0].default_value=(.20,.23,.29,1)
        scene.world.node_tree.nodes["Background"].inputs[1].default_value=.35
        scene.view_settings.exposure = -.5
        for location,power,size in [((0,-3,5),1500,5),((0,4,4),2000,4),((-5,0,3),900,3)]:
            bpy.ops.object.light_add(type="AREA",location=location)
            light=bpy.context.object
            light.data.energy,light.data.shape,light.data.size=power,"DISK",size
            light.rotation_euler=(-light.location).to_track_quat("-Z","Y").to_euler()
        bpy.ops.object.camera_add(location=(2,-7,6 if args.retail_reference_dir else 4))
        camera=bpy.context.object
        camera.rotation_euler=(Vector((0,0,.28))-camera.location).to_track_quat("-Z","Y").to_euler()
        camera.data.type,camera.data.ortho_scale="ORTHO",8
        scene.camera=camera
        scene.render.engine="CYCLES"
        scene.cycles.samples=24
        scene.cycles.use_denoising=True
        scene.render.resolution_x,scene.render.resolution_y,scene.render.resolution_percentage=1600,900 if args.retail_reference_dir else 600,100
        scene.render.filepath=str(authoring/("missing_tokens_v1_retail_comparison.png" if args.retail_reference_dir else "missing_tokens_v1_contact.png"))
        bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    main()

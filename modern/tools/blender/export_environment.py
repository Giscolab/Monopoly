"""Opt-in local Paris landmark exports; recovered authoring file stays read-only."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile
import shutil

import bpy
from mathutils import Matrix

LANDMARKS = {
    "fountain": "FONTAINE — bassins et jets",
    "station": "GARE — façade horloge et trois verrières",
    "morris_column": "Colonne Morris",
}
Y_UP = Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))


def material_factor(socket):
    # A mean ramp swatch approximates a procedural colour without pretending
    # an arbitrary Noise/Bump graph is supported by standard glTF.
    if socket.is_linked and socket.links[0].from_node.type == "VALTORGB":
        colours = [tuple(e.color) for e in socket.links[0].from_node.color_ramp.elements]
        return tuple(sum(c[i] for c in colours)/len(colours) for i in range(4))
    value = socket.default_value
    return float(value) if isinstance(value,(float,int)) else tuple(value)


def simple_material(source, cache):
    if source is None:
        return None
    if source.name in cache:
        return cache[source.name]
    if not source.use_nodes:
        raise RuntimeError(f"material has no Principled graph: {source.name}")
    principled = [n for n in source.node_tree.nodes if n.type == "BSDF_PRINCIPLED"]
    if len(principled) != 1:
        raise RuntimeError(f"ambiguous Principled graph: {source.name}")
    source_bsdf = principled[0]
    material = bpy.data.materials.new(f"Environment factors / {source.name}")
    material.use_nodes = True
    material.use_backface_culling = source.use_backface_culling
    # Build the graph explicitly: factory node names can be localized or
    # customized by Blender preferences loaded during a CMake invocation.
    material.node_tree.nodes.clear()
    target = material.node_tree.nodes.new("ShaderNodeBsdfPrincipled")
    output = material.node_tree.nodes.new("ShaderNodeOutputMaterial")
    material.node_tree.links.new(target.outputs["BSDF"], output.inputs["Surface"])
    for name in ["Base Color","Metallic","Roughness","Emission Color","Emission Strength"]:
        target.inputs[name].default_value = material_factor(source_bsdf.inputs[name])
    # First environment slice is opaque; glass/water transmission and all
    # procedural bump/detail require a later dedicated bake/renderer slice.
    colour = tuple(target.inputs["Base Color"].default_value)
    target.inputs["Base Color"].default_value = (*colour[:3],1)
    target.inputs["Alpha"].default_value = 1
    target.inputs["Transmission Weight"].default_value = 0
    material["source_material"] = source.name
    material["procedural_detail_baked"] = False
    material["opaque_factor_approximation"] = True
    cache[source.name] = material
    return material


def export_landmark(slug, source_digest, output, materials, procedural_baker=None):
    bake_record_start = len(procedural_baker.records) if procedural_baker else 0
    name = LANDMARKS[slug]
    collection = bpy.data.collections.get(name)
    if collection is None:
        raise RuntimeError(f"missing landmark collection: {name}")
    roots = [obj for obj in collection.objects if obj.parent is None]
    if len(roots) != 1 or roots[0].type != "EMPTY":
        raise RuntimeError(f"{name}: expected one authoring EMPTY root")
    original = roots[0].matrix_world.copy()
    inverse = original.inverted()
    placement = Y_UP @ original @ Y_UP.inverted()
    temporary = bpy.data.collections.new(f"EXPORT_environment_{slug}")
    bpy.context.scene.collection.children.link(temporary)
    root = bpy.data.objects.new(slug,None)
    temporary.objects.link(root)
    root.matrix_world = Matrix.Identity(4)
    root["asset_kind"],root["asset_slug"],root["asset_contract_version"] = "environment",slug,1
    root["provenance"] = "Recovered Paris decorative collection; local evaluated copy"
    root["source_collection"],root["source_blend_sha256"] = name,source_digest
    root["authoring_units"] = "metres"
    root["authoring_placement_y_up"] = [placement[row][column] for column in range(4) for row in range(4)]
    meshes, excluded, sources = [], [], []
    bounds_min,bounds_max = [float("inf")]*3,[float("-inf")]*3
    world_min,world_max = [float("inf")]*3,[float("-inf")]*3
    try:
        graph = bpy.context.evaluated_depsgraph_get()
        for source in sorted(collection.all_objects,key=lambda obj:obj.name):
            if any(c.name.startswith(("Case ","Pion ","Maison jeu ")) for c in source.users_collection):
                raise RuntimeError(f"gameplay geometry in landmark: {source.name}")
            if source.type not in {"MESH","FONT","CURVE","SURFACE"}:
                if source.type not in {"EMPTY","LIGHT","CAMERA"}:
                    raise RuntimeError(f"unsupported landmark object: {source.name}")
                excluded.append({"name":source.name,"type":source.type})
                continue
            if procedural_baker:
                graph = bpy.context.evaluated_depsgraph_get()
            mesh = bpy.data.meshes.new_from_object(source.evaluated_get(graph),depsgraph=graph)
            if procedural_baker:
                for index,slot in enumerate(source.material_slots):
                    material = slot.material.copy()
                    material["source_original_material_name"] = slot.material.name
                    procedural_baker.materials.append(material)
                    mesh.materials[index] = material
            meshes.append(mesh)
            obj = bpy.data.objects.new(source.name,mesh)
            temporary.objects.link(obj)
            obj.parent = root
            obj.matrix_world = source.matrix_world
            baked = procedural_baker.bake(obj) if procedural_baker else False
            mesh = obj.data
            source_materials = list(mesh.materials)
            if not baked:
                mesh.materials.clear()
                for material in source_materials:
                    mesh.materials.append(simple_material(material,materials))
            obj.matrix_world = inverse @ source.matrix_world
            sources.append(source.name)
            for vertex in mesh.vertices:
                local = Y_UP @ obj.matrix_world @ vertex.co
                world = Y_UP @ source.matrix_world @ vertex.co
                for i in range(3):
                    bounds_min[i],bounds_max[i] = min(bounds_min[i],local[i]),max(bounds_max[i],local[i])
                    world_min[i],world_max[i] = min(world_min[i],world[i]),max(world_max[i],world[i])
        if not sources or not all(math.isfinite(v) for v in bounds_min+bounds_max):
            raise RuntimeError(f"empty/nonfinite landmark: {slug}")
        root["bounds_min_y_up"],root["bounds_max_y_up"] = bounds_min,bounds_max
        path = output / "environment" / f"paris_{slug}.glb"
        path.parent.mkdir(parents=True,exist_ok=True)
        result = bpy.ops.export_scene.gltf(filepath=str(path),export_format="GLB",
            collection=temporary.name,use_selection=False,export_materials="EXPORT",
            export_normals=True,export_texcoords=True,export_cameras=False,
            export_tangents=False,
            export_lights=False,export_animations=False,export_morph=False,
            export_skins=False,export_extras=True,export_yup=True,export_apply=False)
        if "FINISHED" not in result:
            raise RuntimeError(f"failed export {slug}")
        manifest = {"slug":slug,"source_collection":name,"source_blend_sha256":source_digest,
                    "asset_contract_version":1,"geometry_objects":sources,"excluded":excluded,
                    "local_bounds_y_up_metres":{"min":bounds_min,"max":bounds_max},
                    "original_world_bounds_y_up_metres":{"min":world_min,"max":world_max},
                    "original_world_matrix_y_up_column_major":list(root["authoring_placement_y_up"]),
                    "materials":"Opaque Principled factors; mean ramp colours; no procedural bump/transmission bake",
                    "bytes":path.stat().st_size}
        if procedural_baker:
            manifest["procedural_export_validation"] = procedural_baker.validate_export(path)
            manifest["materials"] = "Actual source procedural base-colour/normal graphs baked before calibration; other factors retained as opaque"
            manifest["procedural_material_bake"] = procedural_baker.records[bake_record_start:]
            manifest["procedural_bake_texture_directory"] = "environment-baked-textures"
        path.with_suffix(".json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print("EXPORTED_ENVIRONMENT",json.dumps(manifest,ensure_ascii=True))
    finally:
        for obj in list(temporary.objects):
            bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.collections.remove(temporary)
        for mesh in meshes:
            if mesh.users == 0:
                bpy.data.meshes.remove(mesh)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source",required=True,help="Read-only recovered .blend")
    parser.add_argument("--output",required=True)
    parser.add_argument("--landmark",action="append",choices=sorted(LANDMARKS),help="Repeat for selected landmarks; default all three")
    parser.add_argument("--bake-procedural",action="store_true",help="Bake actual source graphs to build-only staged candidates")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    source,output = Path(args.source).resolve(),Path(args.output).resolve()
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    bpy.ops.wm.open_mainfile(filepath=str(source))
    materials = {}
    baker = None
    staging = None
    requested_output = output
    if args.bake_procedural:
        if not output.is_relative_to(Path(__file__).resolve().parents[2] / "build"):
            raise RuntimeError("procedural candidates must remain under modern/build")
        output.mkdir(parents=True,exist_ok=True)
        staging = tempfile.TemporaryDirectory(prefix=".procedural-stage-",dir=output)
        output = Path(staging.name)
        sys.path.insert(0,str(Path(__file__).resolve().parent))
        from bake_monopoly_materials import ProceduralExportBaker
        baker = ProceduralExportBaker(output / "environment-baked-textures")
    try:
        for slug in sorted(set(args.landmark or LANDMARKS)):
            export_landmark(slug,digest,output,materials,baker)
    finally:
        for material in materials.values():
            if material.users == 0:
                bpy.data.materials.remove(material)
    if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        raise RuntimeError("reference authoring file changed during export")
    if baker:
        baker.close()
        for path in sorted(output.rglob("*")):
            if path.is_file():
                destination = requested_output / path.relative_to(output)
                destination.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(path,destination)
        staging.cleanup()


if __name__ == "__main__":
    main()

"""Author a new red gameplay hotel, distinct from recovered Paris scenery.

Blender -b --python-exit-code 1 --python export_gameplay_hotel.py -- --source <recovered.blend>
--contract <modern-hotel-contract.json> --output <build/modern-assets>
--authoring <build/authoring> [--probe <production-executable>]
[--retail-reference <diagnostic-hotel.obj> --render]
The recovered file is read only. The block, pitched roof and chimney are new
procedural geometry guided by the measured envelope and inspected silhouette.
GLB/report publication follows candidate bounds, normal and optional probe
validation; validation failures preserve the previous published asset.
"""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_ship_variants import accessor,digest,read_glb,write_accessor,write_glb


def mesh_object(name,vertices,faces,material,root):
    triangles = [(face[0],face[i],face[i+1]) for face in faces for i in range(1,len(face)-1)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices,[],triangles)
    mesh.materials.append(material)
    mesh.update()
    uv = mesh.uv_layers.new(name="UVMap")
    for face in mesh.polygons:
        axis = max(range(3),key=lambda index:abs(face.normal[index]))
        for loop in face.loop_indices:
            x,y,z = mesh.vertices[mesh.loops[loop].vertex_index].co
            uv.data[loop].uv = ((y+.45)/.9,z/.775) if axis==0 else ((x+.325)/.65,z/.775) if axis==1 else ((x+.325)/.65,(y+.45)/.9)
    obj = bpy.data.objects.new(name,mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = root
    return obj


def box_vertices(width,depth,height):
    return [(-width/2,-depth/2,0),(width/2,-depth/2,0),(width/2,depth/2,0),(-width/2,depth/2,0),
            (-width/2,-depth/2,height),(width/2,-depth/2,height),(width/2,depth/2,height),(-width/2,depth/2,height)]


def author(red,contract,provenance):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    material = bpy.data.materials.new("Gameplay hotel · recovered red factors · opaque")
    material.use_nodes = True
    bsdf = next(node for node in material.node_tree.nodes if node.type=="BSDF_PRINCIPLED")
    bsdf.inputs["Base Color"].default_value = red["base_color"]
    bsdf.inputs["Metallic"].default_value = red["metallic"]
    bsdf.inputs["Roughness"].default_value = red["roughness"]
    root = bpy.data.objects.new("Gameplay hotel · reconstructed prototype",None)
    bpy.context.scene.collection.objects.link(root)
    for key,value in provenance.items():
        root[key] = value
    root["asset_kind"],root["asset_slug"],root["reconstructed"] = "building","hotel",True
    root["authoring_units"] = "metres"
    root["legacy_mesh_data_id"] = int(contract["legacyMeshDataId"],16)
    root["bounds_min_y_up"] = contract["authoringBoundsMetresYUp"]["minimum"]
    root["bounds_max_y_up"] = contract["authoringBoundsMetresYUp"]["maximum"]
    mesh_object("Hotel · block body",box_vertices(.6,.9,.5),
                [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],material,root)
    mesh_object("Hotel · pitched roof",[(-.325,-.45,.5),(.325,-.45,.5),(0,-.45,.7),
                                       (-.325,.45,.5),(.325,.45,.5),(0,.45,.7)],
                [(0,1,2),(3,5,4),(0,2,5,3),(1,4,5,2),(0,3,4,1)],material,root)
    ring = [(round(.1*math.cos(math.pi/2+i*math.tau/6),7),round(.1*math.sin(math.pi/2+i*math.tau/6),7)) for i in range(6)]
    chimney = [(x,y,z) for z in (.64,.775) for x,y in ring]
    faces = [tuple(reversed(range(6))),tuple(range(6,12))]+[(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
    mesh_object("Hotel · central chimney",chimney,faces,material,root)
    return root


def canonicalize_and_measure(path):
    doc,binary = read_glb(path)
    positions,normals = [],[]
    for mesh in doc["meshes"]:
        for primitive in mesh["primitives"]:
            for name in ("POSITION","NORMAL"):
                index = primitive["attributes"][name]
                values = accessor(doc,binary,index)
                write_accessor(doc,binary,index,values)
                (positions if name=="POSITION" else normals).append(accessor(doc,binary,index))
            if "TEXCOORD_0" not in primitive["attributes"]:
                raise RuntimeError("hotel primitive lacks authored UVs")
    points,normals = np.concatenate(positions),np.concatenate(normals)
    lengths = np.linalg.norm(normals,axis=1)
    if not np.isfinite(points).all() or not np.isfinite(lengths).all() or np.max(np.abs(lengths-1))>2e-6:
        raise RuntimeError("hotel geometry or normals are invalid")
    if any(material.get("alphaMode","OPAQUE")!="OPAQUE" for material in doc["materials"]):
        raise RuntimeError("hotel requires opaque PBR materials")
    write_glb(path,doc,binary)
    return {"minimum":points.min(axis=0).tolist(),"maximum":points.max(axis=0).tolist()},doc


def temporary_file(destination,suffix=".tmp"):
    handle,name = tempfile.mkstemp(prefix=".hotel-",suffix=suffix,dir=destination.parent)
    os.close(handle)
    return Path(name)


def publish_validated(candidate,path,report,diagnostic):
    """Prepare reports/backups first, then publish; roll back interrupted replacements."""
    prepared,backups,changed = {path:candidate},{},[]
    try:
        contents = json.dumps(report,indent=2)+"\n"
        for destination in (path.with_suffix(".json"),diagnostic):
            temporary = temporary_file(destination)
            prepared[destination] = temporary
            temporary.write_text(contents,encoding="utf-8")
        for destination in prepared:
            if destination.exists():
                backup = temporary_file(destination)
                backups[destination] = backup
                shutil.copyfile(destination,backup)
        for destination,temporary in prepared.items():
            os.replace(temporary,destination)
            changed.append(destination)
    except Exception:
        for destination in reversed(changed):
            if destination in backups:
                os.replace(backups[destination],destination)
            else:
                destination.unlink()
        raise
    finally:
        for temporary in (*prepared.values(),*backups.values()):
            if temporary.exists():
                temporary.unlink()


def render_comparison(retail_reference,authoring,material_color):
    scene = bpy.context.scene
    root = next(obj for obj in scene.objects if obj.type=="EMPTY")
    root.location.x = -.6
    if retail_reference:
        bpy.ops.wm.obj_import(filepath=str(retail_reference),forward_axis="NEGATIVE_Y",up_axis="Z")
        for obj in list(bpy.context.selected_objects):
            if obj.type!="MESH":
                continue
            for vertex in obj.data.vertices:
                x,y,z = vertex.co
                vertex.co = (x/200+.6,-z/200,y/200)
            obj.matrix_world.identity()
            material = bpy.data.materials.new("Diagnostic retail hotel · red")
            material.use_nodes = True
            bsdf = material.node_tree.nodes["Principled BSDF"]
            bsdf.inputs["Base Color"].default_value = material_color
            bsdf.inputs["Roughness"].default_value = .4
            obj.data.materials.clear()
            obj.data.materials.append(material)
    for x,label in [(-.6,"new gameplay hotel"),(.6,"retail diagnostic")]:
        bpy.ops.object.text_add(location=(x,-.8,.003))
        text = bpy.context.object
        text.data.body,text.data.align_x,text.data.size = label,"CENTER",.075
    floor = bpy.data.materials.new("Studio floor")
    floor.use_nodes = True
    floor.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (.12,.14,.18,1)
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.002))
    bpy.context.object.data.materials.append(floor)
    scene.world = bpy.data.worlds.new("Studio")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes["Background"].inputs[0].default_value = (.3,.34,.4,1)
    scene.world.node_tree.nodes["Background"].inputs[1].default_value = .4
    for position,power,size in [((0,-3,4),800,4),((1,3,3),900,3)]:
        bpy.ops.object.light_add(type="AREA",location=position)
        light = bpy.context.object
        light.data.energy,light.data.size = power,size
        light.rotation_euler = (Vector((0,0,.3))-light.location).to_track_quat("-Z","Y").to_euler()
    bpy.ops.object.camera_add(location=(1.8,-3.5,2.6))
    scene.camera = bpy.context.object
    scene.camera.rotation_euler = (Vector((0,0,.3))-scene.camera.location).to_track_quat("-Z","Y").to_euler()
    scene.camera.data.type,scene.camera.data.ortho_scale = "ORTHO",2.8
    scene.render.engine = "CYCLES"
    scene.cycles.samples,scene.cycles.use_denoising = 48,True
    scene.render.resolution_x,scene.render.resolution_y,scene.render.resolution_percentage = 1500,1000,100
    scene.render.filepath = str(authoring/"gameplay_hotel_v1_retail_comparison.png")
    bpy.ops.render.render(write_still=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source","contract","output","authoring"):
        parser.add_argument(f"--{name}",required=True,type=Path)
    parser.add_argument("--probe",type=Path)
    parser.add_argument("--retail-reference",type=Path)
    parser.add_argument("--render",action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    source,contract_path,output,authoring = (path.resolve() for path in (args.source,args.contract,args.output,args.authoring))
    for folder in (output,authoring):
        if "source" in {part.casefold() for part in folder.parts}:
            raise RuntimeError("generated files must remain outside immutable Source/")
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    expected = {"minimum":[-.325,0,-.45],"maximum":[.325,.775,.45]}
    if contract["authoringBoundsMetresYUp"]!=expected or contract["unitsPerMeter"]!=200 or contract["yawDegrees"]!=0:
        raise RuntimeError("unsupported gameplay hotel authoring contract")
    original_hash = digest(source)
    bpy.ops.wm.open_mainfile(filepath=str(source))
    house = bpy.data.collections.get("Maison jeu avant 00")
    material = bpy.data.materials.get("MP · red")
    if house is None or material is None:
        raise RuntimeError("recovered house prototype/red material absent")
    bsdf = next(node for node in material.node_tree.nodes if node.type=="BSDF_PRINCIPLED")
    red = {"base_color":[round(float(value),6) for value in bsdf.inputs["Base Color"].default_value],
           "metallic":round(float(bsdf.inputs["Metallic"].default_value),6),
           "roughness":round(float(bsdf.inputs["Roughness"].default_value),6)}
    audit = {"existing_house_collection":house.name,"house_geometry":[obj.name for obj in house.all_objects if obj.type=="MESH"],
             "recovered_hotels_are_scenery":[collection.name for collection in bpy.data.collections if "HOTEL" in collection.name],
             "source_material":material.name,"principled_factors":red}
    provenance = {"source_blend_sha256":original_hash,"contract_sha256":digest(contract_path),
                  "provenance":"new procedural gameplay hotel; recovered red material factors; not recovered original hotel geometry"}
    author(red,contract,provenance)
    authoring.mkdir(parents=True,exist_ok=True)
    derived = authoring/"gameplay_hotel_v1.blend"
    if derived==source:
        raise RuntimeError("refusing to overwrite recovered blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(derived))
    path = output/"buildings/hotel.glb"
    path.parent.mkdir(parents=True,exist_ok=True)
    candidate = temporary_file(path,".glb")
    try:
        bpy.ops.export_scene.gltf(filepath=str(candidate),export_format="GLB",export_materials="EXPORT",
            export_cameras=False,export_lights=False,export_animations=False,export_skins=False,
            export_morph=False,export_extras=True,export_normals=True,export_texcoords=True,
            export_tangents=False,export_yup=True,export_apply=False)
        bounds,doc = canonicalize_and_measure(candidate)
        error = max(abs(bounds[side][axis]-expected[side][axis])*200 for side in expected for axis in range(3))
        if error>contract["boundsToleranceRaw"]:
            raise RuntimeError(f"authored hotel bounds exceed raw tolerance: {error}")
        report = {"asset_kind":"building","asset_slug":"hotel","reconstructed":True,**provenance,
                  "source_audit":audit,"bounds_y_up_metres":bounds,"max_bounds_error_raw":error,
                  "root_identity":True,"mesh_count":len(doc["meshes"]),"material_count":len(doc["materials"]),
                  "material_policy":"opaque recovered Principled red factors; deterministic authored UVs; no external images",
                  "bytes":candidate.stat().st_size,"sha256":digest(candidate)}
        if args.probe:
            result = subprocess.run([str(args.probe.resolve()),"--gltf",str(candidate),"200","0","0","0","0","--keep-ground"],capture_output=True,text=True,check=True)
            fields = dict(line.split("\t",1) for line in result.stdout.splitlines() if "\t" in line)
            fields["gltf"] = str(path)
            actual = {"minimum":[float(value) for value in fields["minimum_xyz"].split(",")],
                      "maximum":[float(value) for value in fields["maximum_xyz"].split(",")]}
            probe_error = max(abs(actual[side][axis]-contract["retailBoundsRaw"][side][axis]) for side in actual for axis in range(3))
            if probe_error>contract["boundsToleranceRaw"]:
                raise RuntimeError(f"production hotel bounds exceed tolerance: {probe_error}")
            report["production_probe"],report["production_max_bounds_error_raw"] = fields,probe_error
        if digest(source)!=original_hash:
            raise RuntimeError("recovered authoring source changed")
        publish_validated(candidate,path,report,authoring/"gameplay_hotel_v1_qualification.json")
    finally:
        if candidate.exists():
            candidate.unlink()
    print("GAMEPLAY_HOTEL",json.dumps(report))
    if args.render:
        render_comparison(args.retail_reference.resolve() if args.retail_reference else None,authoring,red["base_color"])


if __name__=="__main__":
    main()

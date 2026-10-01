"""Separate measured presentation furniture; gameplay board remains unchanged.

Blender -b --python-exit-code 1 --python export_presentation_assets.py --
--source <recovered.blend> --board <paris_board_runtime.glb> --probe <CPU probe>
--output <modern/build/modern-assets> [--authoring <modern/build/authoring> --render]
All generated files stay under modern/build. Materials use quiet opaque PBR
factors; no procedural noise, unsupported maps, animation or recovered edits.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_ship_variants import accessor,digest,read_glb,write_accessor,write_glb

SUPPORT_TOP = -.345


def material(name,color,metallic,roughness):
    value = bpy.data.materials.new(name)
    value.use_nodes = True
    node = next(node for node in value.node_tree.nodes if node.type=="BSDF_PRINCIPLED")
    node.inputs["Base Color"].default_value = (*color,1)
    node.inputs["Metallic"].default_value = metallic
    node.inputs["Roughness"].default_value = roughness
    return value


def perimeter(width,depth,radius,segments=10):
    points = []
    for x,y,start in [(width/2-radius,depth/2-radius,0),(-width/2+radius,depth/2-radius,90),
                      (-width/2+radius,-depth/2+radius,180),(width/2-radius,-depth/2+radius,270)]:
        for index in range(segments+1):
            angle = math.radians(start+index*90/segments)
            points.append((round(x+radius*math.cos(angle),6),round(y+radius*math.sin(angle),6)))
    return points


def mesh_object(name,vertices,faces,mat,root,bevel=0):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices,[],faces)
    mesh.materials.append(mat)
    mesh.update()
    obj = bpy.data.objects.new(name,mesh)
    bpy.context.scene.collection.objects.link(obj)
    if bevel:
        modifier = obj.modifiers.new("Soft furniture edges","BEVEL")
        modifier.width,modifier.segments,modifier.limit_method = bevel,3,"ANGLE"
        modifier = obj.modifiers.new("Broad planar normals","WEIGHTED_NORMAL")
        modifier.keep_sharp = True
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    result = bpy.data.meshes.new_from_object(evaluated,depsgraph=bpy.context.evaluated_depsgraph_get())
    bpy.data.objects.remove(obj,do_unlink=True)
    obj = bpy.data.objects.new(name,result)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = root
    uv = result.uv_layers.new(name="UVMap")
    for face in result.polygons:
        axis = max(range(3),key=lambda index:abs(face.normal[index]))
        for loop in face.loop_indices:
            p = result.vertices[result.loops[loop].vertex_index].co
            uv.data[loop].uv = (p.y/40+.5,p.z/4+.5) if axis==0 else (p.x/40+.5,p.z/4+.5) if axis==1 else (p.x/40+.5,p.y/40+.5)
    return obj


def slab(name,width,depth,radius,bottom,top,mat,root,bevel=0):
    ring = perimeter(width,depth,radius)
    count = len(ring)
    vertices = [(x,y,z) for z in (bottom,top) for x,y in ring]
    # Explicit fans avoid exporter-dependent concave/ngon triangulation.
    vertices.extend([(0,0,bottom),(0,0,top)])
    faces = []
    for i in range(count):
        following = (i+1)%count
        faces.extend([(2*count,following,i),(2*count+1,count+i,count+following),
                      (i,following,count+following,count+i)])
    return mesh_object(name,vertices,faces,mat,root,bevel)


def inlay(name,width,depth,radius,strip,bottom,top,mat,root):
    outer,inner = perimeter(width,depth,radius),perimeter(width-2*strip,depth-2*strip,radius-strip)
    count = len(outer)
    vertices = [(x,y,z) for z in (bottom,top) for ring in (outer,inner) for x,y in ring]
    faces = []
    for i in range(count):
        j = (i+1)%count
        faces.extend([(2*count+i,2*count+j,3*count+j,3*count+i),
                      (i,count+i,count+j,j),(i,j,2*count+j,2*count+i),
                      (count+i,3*count+i,3*count+j,count+j)])
    return mesh_object(name,vertices,faces,mat,root)


def probe(path,executable):
    result = subprocess.run([str(executable),"--gltf",str(path),"1","0","0","0","0","--keep-ground"],
                            capture_output=True,text=True,check=True)
    return dict(line.split("\t",1) for line in result.stdout.splitlines() if "\t" in line)


def export(root,path,provenance,executable):
    objects = [root,*root.children]
    points = [obj.matrix_world @ vertex.co for obj in root.children for vertex in obj.data.vertices]
    bounds = {"minimum":[min((p.x,p.z,-p.y)[axis] for p in points) for axis in range(3)],
              "maximum":[max((p.x,p.z,-p.y)[axis] for p in points) for axis in range(3)]}
    root["bounds_min_y_up"],root["bounds_max_y_up"] = bounds["minimum"],bounds["maximum"]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = root
    path.parent.mkdir(parents=True,exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path),export_format="GLB",use_selection=True,
        export_materials="EXPORT",export_cameras=False,export_lights=False,export_animations=False,
        export_skins=False,export_morph=False,export_extras=True,export_texcoords=True,
        export_normals=True,export_tangents=False,export_yup=True,export_apply=False)
    doc,binary = read_glb(path)
    for mesh in doc["meshes"]:
        for primitive in mesh["primitives"]:
            for name in ("POSITION","NORMAL"):
                index = primitive["attributes"][name]
                values = accessor(doc,binary,index)
                if name=="NORMAL" and np.max(np.abs(np.linalg.norm(values,axis=1)-1))>2e-6:
                    raise RuntimeError("presentation normal is not finite/unit")
                write_accessor(doc,binary,index,values)
            if "TEXCOORD_0" not in primitive["attributes"]:
                raise RuntimeError("presentation primitive lacks UVs")
    if any(mat.get("alphaMode","OPAQUE")!="OPAQUE" for mat in doc["materials"]):
        raise RuntimeError("presentation requires opaque PBR")
    write_glb(path,doc,binary)
    measured = probe(path,executable)
    if float(measured["maximum_xyz"].split(",")[1])>=0:
        raise RuntimeError("presentation furniture overlaps the playing surface")
    report = {"asset_kind":"presentation","asset_slug":root["asset_slug"],**provenance,
              "bounds_y_up_metres":bounds,"root_identity":True,"opaque_pbr":True,
              "materials":"dark teal felt / dark walnut / rough brushed-brass factors; no noisy procedural detail",
              "production_cpu_probe":measured,"bytes":path.stat().st_size,"sha256":digest(path)}
    path.with_suffix(".json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    return report


def render(board,authoring):
    bpy.ops.import_scene.gltf(filepath=str(board))
    scene = bpy.context.scene
    scene.world = bpy.data.worlds.new("Presentation studio")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes["Background"].inputs[0].default_value = (.23,.27,.31,1)
    scene.world.node_tree.nodes["Background"].inputs[1].default_value = .45
    for position,power,size in [((-12,-8,26),38000,26),((18,12,19),28000,20)]:
        bpy.ops.object.light_add(type="AREA",location=position)
        light = bpy.context.object
        light.data.energy,light.data.size = power,size
        light.rotation_euler = (-light.location).to_track_quat("-Z","Y").to_euler()
    bpy.ops.object.camera_add(location=(28,-34,34))
    scene.camera = bpy.context.object
    scene.camera.rotation_euler = (Vector((0,0,-.5))-scene.camera.location).to_track_quat("-Z","Y").to_euler()
    scene.camera.data.type,scene.camera.data.ortho_scale = "ORTHO",48
    scene.render.engine = "CYCLES"
    scene.cycles.device,scene.cycles.samples,scene.cycles.use_denoising = "CPU",24,True
    scene.render.resolution_x,scene.render.resolution_y,scene.render.resolution_percentage = 1600,1200,100
    scene.render.filepath = str(authoring/"presentation_v1_board_comparison.png")
    bpy.ops.render.render(write_still=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source","board","probe","output"):
        parser.add_argument(f"--{name}",required=True,type=Path)
    parser.add_argument("--authoring",type=Path)
    parser.add_argument("--render",action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    source,board,executable,output = (path.resolve() for path in (args.source,args.board,args.probe,args.output))
    build = Path(__file__).resolve().parents[2]/"build"
    authoring = args.authoring.resolve() if args.authoring else build/"authoring"
    for directory in (output,authoring):
        directory.relative_to(build.resolve())
    source_hash,board_hash = digest(source),digest(board)
    board_metrics = probe(board,executable)
    board_min = [float(value) for value in board_metrics["minimum_xyz"].split(",")]
    board_max = [float(value) for value in board_metrics["maximum_xyz"].split(",")]
    if max(abs((board_max[axis]-board_min[axis])-25.14) for axis in (0,2))>.001 or abs(board_min[1]+.346)>.001:
        raise RuntimeError("presentation requires the measured 25.14m runtime board with underside -.346m")
    bpy.ops.wm.read_factory_settings(use_empty=True)
    walnut = material("Presentation · dark walnut",(.035,.017,.008),0,.34)
    felt = material("Presentation · dark teal felt",(.008,.042,.037),0,.9)
    brass = material("Presentation · brushed brass",(.40,.265,.105),.88,.39)
    provenance = {"source_blend_sha256":source_hash,"board_glb_sha256":board_hash,
                  "provenance":"new optional presentation furniture; measured existing board remains unchanged",
                  "authoring_units":"metres","support_top_y_metres":SUPPORT_TOP}
    roots = {}
    for slug in ("table","plinth"):
        root = bpy.data.objects.new(f"Presentation · {slug}",None)
        bpy.context.scene.collection.objects.link(root)
        root["asset_kind"],root["asset_slug"] = "presentation",slug
        for key,value in provenance.items():
            root[key] = value
        roots[slug] = root
    slab("Table · walnut perimeter",38.5,34.5,1.3,-1.435,-1.035,walnut,roots["table"],.065)
    slab("Table · quiet teal felt",37.9,33.9,1.05,-1.03,-1.0,felt,roots["table"],.008)
    inlay("Table · fine brass border",38.02,34.02,1.11,.025,-1.034,-1.029,brass,roots["table"])
    slab("Plinth · rounded walnut base",27,27,.65,-1.0,-.347,walnut,roots["plinth"],.045)
    slab("Plinth · discreet board seat",25.26,25.26,.1,-.352,SUPPORT_TOP,walnut,roots["plinth"],.002)
    inlay("Plinth · thin brass inlay",26.62,26.62,.46,.025,-.350,SUPPORT_TOP,brass,roots["plinth"])
    reports = {slug:export(root,output/"presentation"/f"{slug}.glb",provenance,executable) for slug,root in roots.items()}
    if digest(source)!=source_hash or digest(board)!=board_hash:
        raise RuntimeError("protected recovered source or runtime board changed")
    authoring.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(authoring/"presentation_v1.blend"))
    (authoring/"presentation_v1_qualification.json").write_text(json.dumps(reports,indent=2)+"\n",encoding="utf-8")
    print("PRESENTATION_READY",json.dumps({slug:{"bytes":report["bytes"],"sha256":report["sha256"],"bounds":report["bounds_y_up_metres"]} for slug,report in reports.items()}))
    if args.render:
        render(board,authoring)


if __name__=="__main__":
    main()

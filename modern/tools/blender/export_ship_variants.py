"""Transfer qualified retail ship-state displacements to recovered highpoly GLB.

The immutable base GLB retains its exact material, vertex and index layout.
No gameplay placement, per-state recentering, or source-file writes occur.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
import numpy as np
from mathutils import Vector

SCALE, OFFSET_Z = 87.55, 6.63


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_glb(path,require_identity=True):
    data = path.read_bytes()
    magic,version,length = struct.unpack_from("<III",data)
    if (magic,version,length) != (0x46546c67,2,len(data)):
        raise RuntimeError("invalid base GLB header")
    size,kind = struct.unpack_from("<II",data,12)
    if kind != 0x4e4f534a:
        raise RuntimeError("base GLB has no JSON chunk")
    doc = json.loads(data[20:20+size])
    binary_size,binary_kind = struct.unpack_from("<II",data,20+size)
    if binary_kind != 0x004e4942:
        raise RuntimeError("base GLB has no BIN chunk")
    binary = bytearray(data[28+size:28+size+binary_size])
    if require_identity and any(k in node for node in doc["nodes"] for k in ["matrix","translation","rotation","scale"]):
        raise RuntimeError("base ship requires identity node transforms")
    if doc.get("animations") or doc.get("skins") or doc.get("images"):
        raise RuntimeError("base ship is not the qualified untextured static subset")
    return doc,binary


def accessor(doc,binary,index):
    item = doc["accessors"][index]
    view = doc["bufferViews"][item["bufferView"]]
    if item["componentType"] != 5126 or item["type"] != "VEC3" or "sparse" in item:
        raise RuntimeError("ship vector accessor must be dense float VEC3")
    start = view.get("byteOffset",0)+item.get("byteOffset",0)
    stride = view.get("byteStride",12)
    return np.asarray([struct.unpack_from("<fff",binary,start+i*stride)
                       for i in range(item["count"])],dtype=np.float64)


def write_accessor(doc,binary,index,values):
    item = doc["accessors"][index]
    view = doc["bufferViews"][item["bufferView"]]
    start = view.get("byteOffset",0)+item.get("byteOffset",0)
    stride = view.get("byteStride",12)
    values = np.round(values,6)
    values[values == 0] = 0
    if not np.isfinite(values).all():
        raise RuntimeError("nonfinite transferred ship vectors")
    for i,value in enumerate(values):
        struct.pack_into("<fff",binary,start+i*stride,*value)
    if "min" in item:
        item["min"] = values.min(axis=0).tolist()
    if "max" in item:
        item["max"] = values.max(axis=0).tolist()


def write_glb(path,doc,binary):
    encoded = json.dumps(doc,separators=(",",":"),ensure_ascii=True).encode()
    encoded += b" "*((-len(encoded))%4)
    path.write_bytes(struct.pack("<III",0x46546c67,2,28+len(encoded)+len(binary))+
                     struct.pack("<II",len(encoded),0x4e4f534a)+encoded+
                     struct.pack("<II",len(binary),0x004e4942)+binary)


class Transfer:
    def __init__(self,rest,squash):
        # Duplicate retail positions must agree on displacement; otherwise
        # their indexed correspondence cannot define a spatial deformation.
        unique = {}
        for a,b in zip(rest,squash):
            key = tuple(a)
            if key in unique and not np.array_equal(unique[key],b):
                raise RuntimeError("duplicate rest controls have conflicting targets")
            unique[key] = b
        self.rest = np.asarray(sorted(unique),dtype=np.float64)
        self.target = np.asarray([unique[tuple(a)] for a in self.rest],dtype=np.float64)
        self.center = self.rest.mean(axis=0)
        self.radius = float(np.linalg.norm(self.rest-self.center,axis=1).max())
        design = self.design(self.rest)
        self.affine,_,rank,_ = np.linalg.lstsq(design,self.target,rcond=None)
        if rank != 4:
            raise RuntimeError("retail controls do not span a 3D affine frame")
        self.residual = self.target-design@self.affine

    def design(self,points):
        return np.column_stack(((points-self.center)/self.radius,np.ones(len(points))))

    def __call__(self,points):
        result = []
        for start in range(0,len(points),2048):
            chunk = points[start:start+2048]
            squared = ((chunk[:,None,:]-self.rest[None,:,:])**2).sum(axis=2)
            weights = 1/np.maximum(squared,1e-20)
            weights /= weights.sum(axis=1)[:,None]
            mapped = self.design(chunk)@self.affine + weights@self.residual
            exact = squared.min(axis=1)<1e-16
            mapped[exact] = self.target[squared.argmin(axis=1)[exact]]
            result.append(mapped)
        return np.concatenate(result)

    def jacobian(self,points):
        epsilon = .01  # Retail engine units; far below a source vertex spacing.
        columns = []
        for axis in range(3):
            shift = np.zeros(3)
            shift[axis] = epsilon
            columns.append((self(points+shift)-self(points-shift))/(2*epsilon))
        return np.stack(columns,axis=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source",required=True,help="Read-only recovered authoring .blend provenance")
    parser.add_argument("--base-glb",required=True,help="Previously exported recovered ship.glb")
    parser.add_argument("--correspondence",required=True,help="Qualified retail18/1B indexed controls JSON")
    parser.add_argument("--output",required=True,help="Generated modern asset root")
    parser.add_argument("--render",action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    source,base,controls = Path(args.source).resolve(),Path(args.base_glb).resolve(),Path(args.correspondence).resolve()
    reference = json.loads(controls.read_text(encoding="utf-8"))
    rest = np.asarray(reference["rest_positions_engine"],dtype=np.float64)
    squash = np.asarray(reference["squash_positions_engine"],dtype=np.float64)
    if rest.shape != squash.shape or rest.shape not in {(110,3),(111,3)} or not np.isfinite(rest).all() or not np.isfinite(squash).all():
        raise RuntimeError("unqualified retail control shape")
    if not reference["evidence"]["identical_triangle_multisets"]:
        raise RuntimeError("retail indexed topology correspondence is unproven")
    referenced = sorted({i for tri in reference["rest_triangles"] for i in tri})
    if len(referenced) != 110 or min(referenced)<0 or max(referenced)>=len(rest):
        raise RuntimeError("unexpected retail rendered control coverage")
    transfer = Transfer(rest[referenced],squash[referenced])
    error = np.linalg.norm(transfer(rest[referenced])-squash[referenced],axis=1)
    if error.max()>1e-8:
        raise RuntimeError("retail displacement controls were not reproduced")
    original,binary = read_glb(base)
    primitives = [p for m in original["meshes"] for p in m["primitives"]]
    position_ids = sorted({p["attributes"]["POSITION"] for p in primitives})
    baseline = min(accessor(original,binary,i)[:,1].min() for i in position_ids)
    output = Path(args.output).resolve()/"tokens"/"ship_variants"
    output.mkdir(parents=True,exist_ok=True)
    report = {"method":"least-squares affine plus interpolatory inverse-squared-distance residual",
              "source_blend_sha256":digest(source),"base_glb_sha256":digest(base),
              "correspondence_sha256":digest(controls),"controls":len(transfer.rest),
              "ignored_unreferenced_control_indices":sorted(set(range(len(rest)))-set(referenced)),
              "control_max_error_engine_units":float(error.max()),
              "control_affine_max_residual_engine_units":float(np.linalg.norm(transfer.residual,axis=1).max()),
              "common_rest_ground_baseline_y_metres":float(baseline),
              "shared_calibration":{"units_per_metre":SCALE,"yaw_degrees":-90,"offset":[0,0,OFFSET_Z],"ground_to_zero":False},
              "states":{}}
    for state in ["rest","squash"]:
        doc,data = copy.deepcopy(original),bytearray(binary)
        touched_positions,touched_normals = set(),{}
        positions_all,displacements_all,determinants = [],[],[]
        for primitive in primitives:
            p_id,n_id = primitive["attributes"]["POSITION"],primitive["attributes"]["NORMAL"]
            points = accessor(original,binary,p_id)
            engine = np.column_stack((-points[:,2]*SCALE,(points[:,1]-baseline)*SCALE,points[:,0]*SCALE+OFFSET_Z))
            mapped = engine if state == "rest" else transfer(engine)
            local = np.column_stack(((mapped[:,2]-OFFSET_Z)/SCALE,mapped[:,1]/SCALE,-mapped[:,0]/SCALE))
            if p_id not in touched_positions:
                write_accessor(doc,data,p_id,local)
                touched_positions.add(p_id)
                positions_all.append(local)
                displacements_all.append(np.linalg.norm(mapped-engine,axis=1))
            if n_id in touched_normals:
                if touched_normals[n_id] != p_id:
                    raise RuntimeError("normal accessor is shared across different geometry")
                continue
            touched_normals[n_id] = p_id
            normals = accessor(original,binary,n_id)
            if state == "squash":
                jacobian = transfer.jacobian(engine)
                determinant = np.linalg.det(jacobian)
                if not np.isfinite(determinant).all() or determinant.min()<.1:
                    raise RuntimeError("deformation Jacobian is nonfinite/folded/singular")
                determinants.extend(determinant.tolist())
                engine_normals = np.column_stack((-normals[:,2],normals[:,1],normals[:,0]))
                transformed = np.linalg.solve(np.transpose(jacobian,(0,2,1)),engine_normals[:,:,None])[:,:,0]
                normals = np.column_stack((transformed[:,2],transformed[:,1],-transformed[:,0]))
            lengths = np.linalg.norm(normals,axis=1)
            if not np.isfinite(lengths).all() or lengths.min()<1e-8:
                raise RuntimeError("transferred normals are invalid")
            write_accessor(doc,data,n_id,normals/lengths[:,None])
        points = np.concatenate(positions_all)
        root_id = doc["scenes"][doc.get("scene",0)]["nodes"][0]
        root = doc["nodes"][root_id]
        root["name"] = f"ship_{state}"
        root.setdefault("extras",{}).update({"variant_state":state,"legacy_mesh_data_id":0x80018 if state=="rest" else 0x8001b,
            "shape_transfer":"qualified indexed retail controls; common rest frame",
            "common_rest_ground_baseline_y_metres":float(baseline),
            "bounds_min_y_up":points.min(axis=0).tolist(),"bounds_max_y_up":points.max(axis=0).tolist(),
            "source_blend_sha256":report["source_blend_sha256"],"base_glb_sha256":report["base_glb_sha256"],
            "correspondence_sha256":report["correspondence_sha256"]})
        path = output/f"{state}.glb"
        write_glb(path,doc,data)
        report["states"][state] = {"bytes":path.stat().st_size,"sha256":digest(path),
            "bounds_y_up_metres":{"min":points.min(axis=0).tolist(),"max":points.max(axis=0).tolist()},
            "max_displacement_engine_units":float(np.concatenate(displacements_all).max()),
            "jacobian_det_min":min(determinants) if determinants else 1,
            "jacobian_det_max":max(determinants) if determinants else 1}
    (output/"shape_transfer.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("SHIP_VARIANTS",json.dumps(report))
    if args.render:
        render_comparison(output,reference)


def render_comparison(output,reference):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    chrome = bpy.data.materials.new("Diagnostic retail chrome")
    chrome.use_nodes=True
    bsdf=chrome.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value=(.5,.56,.59,1)
    bsdf.inputs["Metallic"].default_value=.94
    bsdf.inputs["Roughness"].default_value=.21
    label_material=bpy.data.materials.new("Diagnostic labels")
    label_material.use_nodes=True
    label_material.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=(.01,.012,.016,1)
    for column,state in enumerate(["rest","squash"]):
        bpy.ops.import_scene.gltf(filepath=str(output/f"{state}.glb"))
        for obj in list(bpy.context.selected_objects):
            if obj.parent is None:
                obj.location.x += (column-.5)*3.6
                obj.location.y -= 1.0
        vertices = [((p[2]-OFFSET_Z)/SCALE+(column-.5)*3.6,p[0]/SCALE+1.25,p[1]/SCALE)
                    for p in reference[f"{state}_positions_engine"]]
        mesh=bpy.data.meshes.new(f"retail_{state}")
        mesh.from_pydata(vertices,[],reference["rest_triangles"])
        mesh.materials.append(chrome)
        obj=bpy.data.objects.new(f"retail_{state}",mesh)
        scene.collection.objects.link(obj)
        for row,tag in [(-1.65,"modern"),(.55,"retail")]:
            bpy.ops.object.text_add(location=((column-.5)*3.6,row,.01))
            label=bpy.context.object
            label.data.body,label.data.align_x,label.data.size=f"{tag} {state}","CENTER",.13
            label.data.materials.append(label_material)
    floor=bpy.data.materials.new("Studio floor")
    floor.use_nodes=True
    floor.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=(.07,.085,.11,1)
    floor.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value=.75
    bpy.ops.mesh.primitive_plane_add(size=200)
    bpy.context.object.data.materials.append(floor)
    scene.world=bpy.data.worlds.new("Studio")
    scene.world.use_nodes=True
    scene.world.node_tree.nodes["Background"].inputs[0].default_value=(.20,.23,.29,1)
    scene.world.node_tree.nodes["Background"].inputs[1].default_value=.35
    for location,power,size in [((0,-3,5),1500,5),((0,4,4),2000,4)]:
        bpy.ops.object.light_add(type="AREA",location=location)
        light=bpy.context.object
        light.data.energy,light.data.shape,light.data.size=power,"DISK",size
        light.rotation_euler=(-light.location).to_track_quat("-Z","Y").to_euler()
    bpy.ops.object.camera_add(location=(2,-8,8))
    camera=bpy.context.object
    camera.rotation_euler=(Vector((0,0,.5))-camera.location).to_track_quat("-Z","Y").to_euler()
    camera.data.type,camera.data.ortho_scale="ORTHO",8
    scene.camera=camera
    scene.render.engine="CYCLES"
    scene.cycles.samples=32
    scene.cycles.use_denoising=True
    scene.render.resolution_x,scene.render.resolution_y,scene.render.resolution_percentage=1400,1000,100
    scene.render.filepath=str(output/"ship_variant_comparison.png")
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    main()

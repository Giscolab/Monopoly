"""Indexed token state transfer using production-decoded correspondence contracts.

Reuses the ship GLB/transfer helpers. Default dog/horse idle packs are atomic:
failed packs remain diagnostic candidates. Opt-in --contract-states selects all
observed targets with the catalogue frame; --allow-partial-qualification emits
only individually qualified pose_<tag>.glb files to a separate build output.
Those files do not establish complete-sequence or in-game qualification.
Example expansion arguments (after Blender's --): --token ship --source
<recovered.blend> --base-glb <ship.glb> --correspondence <production.json>
--output <build/complete-modern-variants> --diagnostics <build/qualification/ship>
--contract-states --allow-partial-qualification --render.
"""
import argparse
import copy
import json
from pathlib import Path
import struct
import sys

import bpy
import numpy as np
from mathutils import Vector,Matrix,Quaternion

sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_ship_variants import Transfer,accessor,digest,read_glb,write_accessor,write_glb

PROFILES = {"dog":{"scale":154.80,"yaw_degrees":90,"offset":[-.5,0,-15.05967734],"rest_tag":0x38,
                   "states":[0x40,0x41,0x42,0x43],"control_count":75,"triangle_count":146},
            "horse":{"scale":211.87215,"yaw_degrees":-90,"offset":[-1,1,-4.84202],"rest_tag":0x94,
                     "states":[0x94,0xa8,0xa9,0xaa,0xab,0xac],"control_count":182,"triangle_count":330},
            "race_car":{"scale":122.94,"yaw_degrees":-90,"offset":[-2,0,13.02],"rest_tag":0x32,
                        "states":[],"control_count":92,"triangle_count":147},
            "top_hat":{"scale":86.15,"yaw_degrees":-90,"offset":[0,0,0],"rest_tag":0x8d,
                       "states":[],"control_count":73,"triangle_count":144},
            "ship":{"scale":87.55,"yaw_degrees":-90,"offset":[0,0,6.63],"rest_tag":0x18,
                    "states":[],"control_count":110,"triangle_count":148},
            "boot":{"scale":181.06,"yaw_degrees":-90,"offset":[0,0,25.94],"rest_tag":0xbf,
                    "states":[],"control_count":66,"triangle_count":130},
            "thimble":{"scale":130.89,"yaw_degrees":-90,"offset":[0,0,0],"rest_tag":0xcc,
                       "states":[],"control_count":92,"triangle_count":180},
            "moneybag":{"scale":275.57889,"yaw_degrees":-90,"offset":[-1.44884,-4,-6],"rest_tag":0x06,
                        "states":[],"control_count":74,"triangle_count":134,"catalogue_grounded":True},
            "iron":{"scale":135.72687,"yaw_degrees":-90,"offset":[.5,0,-6.49181],"rest_tag":0xad,
                    "states":[],"control_count":82,"triangle_count":150,"catalogue_grounded":True}}


def bake_node_transforms(doc,binary):
    """Normalize copied TRS instances; preserve positions, normals and winding."""
    doc,data = copy.deepcopy(doc),bytearray(binary)
    worlds = {}

    def visit(index,parent):
        node = doc["nodes"][index]
        if "matrix" in node:
            values = node["matrix"]
            local = Matrix(tuple(tuple(values[c*4+r] for c in range(4)) for r in range(4)))
        else:
            rotation = node.get("rotation",[0,0,0,1])
            quaternion = Quaternion((rotation[3],*rotation[:3]))
            local = Matrix.Translation(node.get("translation",[0,0,0])) @ quaternion.to_matrix().to_4x4() @ Matrix.Diagonal((*node.get("scale",[1,1,1]),1))
        world = parent @ local
        if index in worlds:
            raise RuntimeError("node occurs twice in the active scene")
        worlds[index] = np.asarray(world,dtype=np.float64)
        for child in node.get("children",[]):
            visit(child,world)
    for root in doc["scenes"][doc.get("scene",0)]["nodes"]:
        visit(root,Matrix.Identity(4))

    def append(values):
        data.extend(b"\0"*((-len(data))%4))
        offset = len(data)
        import struct
        for value in values:
            data.extend(struct.pack("<fff",*value))
        view_id = len(doc["bufferViews"])
        doc["bufferViews"].append({"buffer":0,"byteOffset":offset,"byteLength":len(values)*12})
        accessor_id = len(doc["accessors"])
        doc["accessors"].append({"bufferView":view_id,"componentType":5126,"type":"VEC3","count":len(values),
                                  "min":values.min(axis=0).tolist(),"max":values.max(axis=0).tolist()})
        return accessor_id

    meshes,determinants = [],[]
    max_error = 0
    for index,world in sorted(worlds.items()):
        node = doc["nodes"][index]
        if "mesh" not in node:
            continue
        determinant = float(np.linalg.det(world[:3,:3]))
        if not np.isfinite(determinant) or determinant<=0:
            raise RuntimeError("TRS normalization requires positive nonsingular transforms")
        determinants.append(determinant)
        mesh = copy.deepcopy(doc["meshes"][node["mesh"]])
        for primitive in mesh["primitives"]:
            points = accessor(doc,binary,primitive["attributes"]["POSITION"])
            expected = points @ world[:3,:3].T + world[:3,3]
            baked = expected.astype(np.float32).astype(np.float64)
            max_error = max(max_error,float(np.abs(baked-expected).max()))
            normals = accessor(doc,binary,primitive["attributes"]["NORMAL"])
            normals = normals @ np.linalg.inv(world[:3,:3])
            lengths = np.linalg.norm(normals,axis=1)
            if not np.isfinite(lengths).all() or lengths.min()<1e-8:
                raise RuntimeError("TRS normalization produced invalid normals")
            primitive["attributes"]["POSITION"] = append(baked)
            primitive["attributes"]["NORMAL"] = append(normals/lengths[:,None])
        node["mesh"] = len(meshes)
        meshes.append(mesh)
    doc["meshes"] = meshes
    for node in doc["nodes"]:
        for key in ["matrix","translation","rotation","scale"]:
            node.pop(key,None)
    doc["buffers"][0]["byteLength"] = len(data)
    return doc,data,{"baked_mesh_instances":len(meshes),"max_float32_position_error_metres":max_error,
                     "node_transform_determinant_min":min(determinants),"node_transform_determinant_max":max(determinants)}


def create_pose(original,binary,profile,baseline,transfer,tag,provenance):
    doc,data = copy.deepcopy(original),bytearray(binary)
    scale = profile["scale"]
    offset = profile["offset"]
    sign = profile["yaw_degrees"]/90
    points_all,displacement,determinants,normal_lengths = [],[],[],[]
    touched_positions,touched_normals = set(),{}
    singular_count = 0
    face_folds,degenerate_faces = 0,0
    for mesh in original["meshes"]:
        for primitive in mesh["primitives"]:
            p_id,n_id = primitive["attributes"]["POSITION"],primitive["attributes"]["NORMAL"]
            points = accessor(original,binary,p_id)
            engine = np.column_stack((sign*points[:,2]*scale+offset[0],(points[:,1]-baseline)*scale+offset[1],
                                       -sign*points[:,0]*scale+offset[2]))
            mapped = transfer(engine)
            if profile.get("catalogue_grounded") or profile.get("verify_faces"):
                # Discrete faces must also retain orientation relative to the
                # transported source normal; vertex Jacobians alone can miss
                # folds between widely spaced samples.
                item = original["accessors"][primitive["indices"]]
                view = original["bufferViews"][item["bufferView"]]
                component = {5121:"B",5123:"H",5125:"I"}[item["componentType"]]
                width = struct.calcsize("<"+component)
                start = view.get("byteOffset",0)+item.get("byteOffset",0)
                indices = np.asarray([struct.unpack_from("<"+component,binary,start+i*view.get("byteStride",width))[0]
                                      for i in range(item["count"])]).reshape((-1,3))
                old_tri,new_tri = engine[indices],mapped[indices]
                old_cross = np.cross(old_tri[:,1]-old_tri[:,0],old_tri[:,2]-old_tri[:,0])
                new_cross = np.cross(new_tri[:,1]-new_tri[:,0],new_tri[:,2]-new_tri[:,0])
                source_area,new_area = np.linalg.norm(old_cross,axis=1),np.linalg.norm(new_cross,axis=1)
                valid = (source_area>1e-10)&(new_area>1e-10)
                degenerate_faces += int(((source_area>1e-10)&~valid).sum())
                face_j = transfer.jacobian(old_tri.mean(axis=1))
                face_safe = valid & (np.abs(np.linalg.det(face_j))>1e-8)
                transported = np.zeros_like(old_cross)
                transported[face_safe] = np.linalg.solve(np.transpose(face_j[face_safe],(0,2,1)),old_cross[face_safe,:,None])[:,:,0]
                face_folds += int((valid & (~face_safe | (np.einsum("ij,ij->i",transported,new_cross)<=0))).sum())
            local = np.column_stack((-sign*(mapped[:,2]-offset[2])/scale,(mapped[:,1]-offset[1])/scale,
                                      sign*(mapped[:,0]-offset[0])/scale))
            if p_id not in touched_positions:
                if not (profile.get("catalogue_grounded") and tag==profile["rest_tag"] and baseline==0):
                    write_accessor(doc,data,p_id,local)
                touched_positions.add(p_id)
                points_all.append(local)
                displacement.extend(np.linalg.norm(mapped-engine,axis=1).tolist())
            if n_id in touched_normals:
                if touched_normals[n_id] != p_id:
                    raise RuntimeError("shared normal accessor cannot describe distinct geometry")
                continue
            touched_normals[n_id] = p_id
            normals = accessor(original,binary,n_id)
            engine_normals = np.column_stack((sign*normals[:,2],normals[:,1],-sign*normals[:,0]))
            jacobian = transfer.jacobian(engine)
            determinant = np.linalg.det(jacobian)
            determinants.extend(determinant.tolist())
            safe = np.isfinite(determinant) & (np.abs(determinant)>1e-8)
            singular_count += int((~safe).sum())
            # Singular points use source normals in diagnostic captures only;
            # the pack is disqualified and cannot be published.
            transformed = engine_normals.copy()
            transformed[safe] = np.linalg.solve(np.transpose(jacobian[safe],(0,2,1)),engine_normals[safe,:,None])[:,:,0]
            normals = np.column_stack((-sign*transformed[:,2],transformed[:,1],sign*transformed[:,0]))
            lengths = np.linalg.norm(normals,axis=1)
            if not np.isfinite(lengths).all() or lengths.min()<1e-8:
                raise RuntimeError("invalid transformed candidate normals")
            normal_lengths.extend(lengths.tolist())
            if not (profile.get("catalogue_grounded") and tag==profile["rest_tag"] and baseline==0):
                write_accessor(doc,data,n_id,normals/lengths[:,None])
    points = np.concatenate(points_all)
    determinants = np.asarray(determinants)
    qualified = bool(np.isfinite(points).all() and np.isfinite(determinants).all() and determinants.min()>=.1 and singular_count==0)
    if profile.get("catalogue_grounded") or profile.get("verify_faces"):
        qualified = qualified and face_folds==0 and degenerate_faces==0
    root = doc["nodes"][doc["scenes"][doc.get("scene",0)]["nodes"][0]]
    root["name"] = f"{root.get('extras',{}).get('asset_slug','token')}_idle_{tag:04x}"
    root.setdefault("extras",{}).update(provenance)
    root["extras"].update({"variant_state":f"idle_{tag:04x}","legacy_mesh_data_id":0x80000+tag,
        "variant_qualified":qualified,"shape_transfer":"qualified indexed controls; shared representative frame",
        "common_rest_ground_baseline_y_metres":baseline,
        "bounds_min_y_up":points.min(axis=0).tolist(),"bounds_max_y_up":points.max(axis=0).tolist()})
    report = {"qualified":qualified,"jacobian_det_min":float(determinants.min()),
              "jacobian_det_max":float(determinants.max()),"nonpositive_jacobian_vertices":int((determinants<=0).sum()),
              "low_jacobian_vertices":int((determinants<=.1).sum()),"singular_jacobian_vertices":singular_count,
              "max_displacement_engine_units":max(displacement),
              "bounds_y_up_metres":{"min":points.min(axis=0).tolist(),"max":points.max(axis=0).tolist()}}
    if profile.get("catalogue_grounded") or profile.get("verify_faces"):
        report.update({"discrete_face_folds":face_folds,"new_degenerate_faces":degenerate_faces})
    if profile.get("catalogue_grounded"):
        report["representative_payload_preserved"] = tag==profile["rest_tag"] and baseline==0
    return doc,data,report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--token",required=True,choices=sorted(PROFILES))
    parser.add_argument("--source",required=True)
    parser.add_argument("--base-glb",required=True)
    parser.add_argument("--correspondence",required=True)
    parser.add_argument("--output",required=True)
    parser.add_argument("--diagnostics",required=True,help="Build directory for candidates and qualification evidence")
    parser.add_argument("--render",action="store_true")
    parser.add_argument("--contract-states",action="store_true",help="Use every authoritative production-contract target with catalogue calibration; pose filenames")
    parser.add_argument("--allow-partial-qualification",action="store_true",help="Contract-state diagnostics: publish only individually qualified poses; never activate a runtime pack")
    parser.add_argument("--states",nargs="+",type=lambda value:int(value,0),help="Explicit subset of authoritative contract targets; requires --contract-states")
    parser.add_argument("--verify-faces",action="store_true",help="Additional discrete fold qualification for diagnostic expansions; default exports stay unchanged")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    if args.allow_partial_qualification and not args.contract_states:
        parser.error("partial qualification requires --contract-states")
    if args.states and not args.contract_states:
        parser.error("explicit states require --contract-states")
    source,base,controls = Path(args.source).resolve(),Path(args.base_glb).resolve(),Path(args.correspondence).resolve()
    profile = copy.deepcopy(PROFILES[args.token])
    if args.verify_faces:
        profile["verify_faces"] = True
    reference = json.loads(controls.read_text(encoding="utf-8"))
    if args.contract_states:
        if not reference["evidence"].get("production_geometry_decoder",False):
            raise RuntimeError("contract-state expansion requires the production geometry decoder")
        profile["states"] = sorted(int(key,16) for key in reference["target_positions_engine"])
        if args.states:
            if not set(args.states).issubset(profile["states"]):
                parser.error("requested states are absent from authoritative contract")
            profile["states"] = sorted(set(args.states))
    if not profile["states"]:
        parser.error("this profile requires --contract-states")
    rest = np.asarray(reference["rest_positions_engine"],dtype=np.float64)
    triangles = reference["rest_triangles"]
    referenced = sorted({i for tri in triangles for i in tri})
    if rest.shape != (profile["control_count"],3) or len(triangles)!=profile["triangle_count"]:
        raise RuntimeError("unexpected representative control topology")
    evidence = reference["evidence"]
    production_geometry = evidence.get("production_geometry_decoder",False) or evidence.get("referenced_positions_match_production_obj",False)
    if not evidence["identical_triangle_multisets"] or not production_geometry:
        raise RuntimeError("unqualified indexed correspondence")
    normalize_nodes = args.token=="horse" or profile.get("catalogue_grounded",False)
    original,binary = read_glb(base,require_identity=not normalize_nodes)
    normalization = None
    if normalize_nodes:
        original,binary,normalization = bake_node_transforms(original,binary)
    position_ids = {p["attributes"]["POSITION"] for m in original["meshes"] for p in m["primitives"]}
    base_points = np.concatenate([accessor(original,binary,i) for i in sorted(position_ids)])
    baseline = float(base_points[:,1].min())
    center = (base_points.min(axis=0)+base_points.max(axis=0))/2
    control_center = (rest[referenced].min(axis=0)+rest[referenced].max(axis=0))/2
    sign = profile["yaw_degrees"]/90
    if not args.contract_states:
        profile["offset"][0] = float(control_center[0]-sign*center[2]*profile["scale"])
        profile["offset"][2] = float(control_center[2]+sign*center[0]*profile["scale"])
    provenance = {"source_blend_sha256":digest(source),"base_glb_sha256":digest(base),"correspondence_sha256":digest(controls)}
    report = {"token":args.token,"rest_tag":profile["rest_tag"],"controls":len(referenced),
              "method":"affine plus exact interpolatory inverse-squared-distance residual",
              "common_rest_ground_baseline_y_metres":baseline,
              "shared_calibration":{"units_per_metre":profile["scale"],"yaw_degrees":profile["yaw_degrees"],"offset":profile["offset"],"ground_to_zero":False},
              "calibration_derivation":{"base_gltf_bounds_center":center.tolist(),"retail_rest_bounds_center":control_center.tolist()},
              **provenance,"poses":{}}
    if normalization:
        report["node_transform_normalization"] = normalization
    if profile.get("catalogue_grounded"):
        # Production groundToZero subtracts the geometry minimum BEFORE
        # applying localOffset: moneybag's catalogue offsetY=-4 is retained.
        # The exported common rest baseline removes the need to ground each
        # variant. Keeping the same offset preserves static rest and all
        # target vertical motion without per-pose floor normalization.
        report["calibration_derivation"].update({"catalogue_static_ground_to_zero":True,
            "raw_retail_transfer_offset":profile["offset"],"variant_common_ground_to_zero":False,
            "catalogue_ground_then_offset_order":True,"retail_to_grounded_y_translation":0})
    candidates = {}
    for tag in profile["states"]:
        key = f"0x{tag:x}"
        target = np.asarray(reference["target_positions_engine"][key],dtype=np.float64)
        evidence = reference["evidence"]["targets"][key]
        if target.shape!=rest.shape or not np.isfinite(target).all() or not evidence["identical_triangle_multisets"]:
            raise RuntimeError(f"unqualified target {key}")
        try:
            transfer = Transfer(rest[referenced],target[referenced])
            error = np.linalg.norm(transfer(rest[referenced])-target[referenced],axis=1)
            if error.max()>1e-8:
                raise RuntimeError(f"target controls not reproduced: {key}")
            doc,data,pose_report = create_pose(original,binary,profile,baseline,transfer,tag,provenance)
        except RuntimeError as error:
            if not args.allow_partial_qualification:
                raise
            report["poses"][key] = {"qualified":False,"exclusion_reason":str(error)}
            continue
        if args.contract_states:
            root = doc["nodes"][doc["scenes"][doc.get("scene",0)]["nodes"][0]]
            root["name"] = f"{args.token}_pose_{tag:04x}"
            root["extras"]["variant_state"] = f"pose_{tag:04x}"
        pose_report["control_max_error_engine_units"] = float(error.max())
        report["poses"][key] = pose_report
        candidates[tag] = (doc,data)
    diagnostic = Path(args.diagnostics).resolve()
    diagnostic.mkdir(parents=True,exist_ok=True)
    all_qualified = all(pose["qualified"] for pose in report["poses"].values())
    report["pack_numerically_qualified"] = all_qualified
    partial = args.allow_partial_qualification
    folder = Path(args.output).resolve()/"tokens"/f"{args.token}_variants" if all_qualified or partial else diagnostic/"candidates"
    folder.mkdir(parents=True,exist_ok=True)
    for tag,(doc,data) in candidates.items():
        if partial and not report["poses"][f"0x{tag:x}"]["qualified"]:
            continue
        path = folder/f"{'pose' if args.contract_states else 'idle'}_{tag:04x}.glb"
        write_glb(path,doc,data)
        report["poses"][f"0x{tag:x}"].update({"bytes":path.stat().st_size,"sha256":digest(path)})
    if args.contract_states:
        report.update({"scope":"all observed production-contract targets; numerical qualification only, no sequence activation",
                       "individually_published_tags":[tag for tag in profile["states"] if report["poses"][f"0x{tag:x}"]["qualified"]],
                       "excluded_tags":[tag for tag in profile["states"] if not report["poses"][f"0x{tag:x}"]["qualified"]]})
        if args.states:
            report["scope"] = "explicit observed production-contract subset; numerical qualification only, no sequence activation"
    (diagnostic/"qualification.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("TOKEN_VARIANTS",json.dumps(report))
    if args.render:
        if args.contract_states:
            states = [tag for tag in profile["states"] if report["poses"][f"0x{tag:x}"]["qualified"]]
            for start in range(0,len(states),8):
                page = copy.deepcopy(profile)
                page["states"] = states[start:start+8]
                render_comparison(folder,diagnostic,reference,page,"pose",f"{args.token}_observed_{start//8+1:02d}.png")
        else:
            render_comparison(folder,diagnostic,reference,profile)
    if not all_qualified and not partial:
        raise RuntimeError(f"{args.token} idle transfer failed; diagnostic candidates retained, runtime pack not published")


def render_comparison(folder,diagnostic,reference,profile,prefix="idle",image_name=None):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    chrome = bpy.data.materials.new("Diagnostic retail chrome")
    chrome.use_nodes=True
    bsdf=chrome.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value=(.5,.56,.59,1)
    bsdf.inputs["Metallic"].default_value=.94
    bsdf.inputs["Roughness"].default_value=.21
    label_material=bpy.data.materials.new("Diagnostic labels")
    label_material.use_nodes=True
    label_material.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=(.01,.012,.016,1)
    scale,offset = profile["scale"],profile["offset"]
    sign = profile["yaw_degrees"]/90
    columns = len(profile["states"])
    for column,tag in enumerate(profile["states"]):
        x=(column-(columns-1)/2)*3
        bpy.ops.import_scene.gltf(filepath=str(folder/f"{prefix}_{tag:04x}.glb"))
        for obj in list(bpy.context.selected_objects):
            if obj.parent is None:
                obj.location.x += x
                obj.location.y -= .9
        vertices=[(-sign*(p[2]-offset[2])/scale+x,-sign*(p[0]-offset[0])/scale+1.1,(p[1]-offset[1])/scale)
                  for p in reference["target_positions_engine"][f"0x{tag:x}"]]
        mesh=bpy.data.meshes.new(f"retail_{tag:04x}")
        mesh.from_pydata(vertices,[],reference["rest_triangles"])
        mesh.materials.append(chrome)
        scene.collection.objects.link(bpy.data.objects.new(f"retail_{tag:04x}",mesh))
        for row,kind in [(-1.55,"modern"),(.5,"retail")]:
            bpy.ops.object.text_add(location=(x,row,.01))
            label=bpy.context.object
            label.data.body,label.data.align_x,label.data.size=f"{kind} {tag:04x}","CENTER",.14
            label.data.materials.append(label_material)
    floor=bpy.data.materials.new("Studio floor")
    floor.use_nodes=True
    floor.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value=(.07,.085,.11,1)
    bpy.ops.mesh.primitive_plane_add(size=200)
    bpy.context.object.data.materials.append(floor)
    scene.world=bpy.data.worlds.new("Studio")
    scene.world.use_nodes=True
    scene.world.node_tree.nodes["Background"].inputs[0].default_value=(.20,.23,.29,1)
    scene.world.node_tree.nodes["Background"].inputs[1].default_value=.35
    for location,power,size in [((0,-4,6),2200,8),((0,4,5),2800,7)]:
        bpy.ops.object.light_add(type="AREA",location=location)
        light=bpy.context.object
        light.data.energy,light.data.shape,light.data.size=power,"DISK",size
        light.rotation_euler=(-light.location).to_track_quat("-Z","Y").to_euler()
    bpy.ops.object.camera_add(location=(2,-12,10))
    camera=bpy.context.object
    camera.rotation_euler=(Vector((0,0,.4))-camera.location).to_track_quat("-Z","Y").to_euler()
    camera.data.type,camera.data.ortho_scale="ORTHO",columns*3+.5
    scene.camera=camera
    scene.render.engine="CYCLES"
    scene.cycles.samples=32
    scene.cycles.use_denoising=True
    scene.render.resolution_x,scene.render.resolution_y,scene.render.resolution_percentage=columns*450,1000,100
    token = "horse" if profile["rest_tag"]==0x94 else "dog"
    scene.render.filepath=str(diagnostic/(image_name or f"{token}_idle_retail_comparison.png"))
    bpy.ops.render.render(write_still=True)


if __name__ == "__main__":
    main()

"""Pack production-decoded ClassicMedium USA board, without geometry reconstruction.

Blender -b --python-exit-code 1 --python export_retail_board.py --
--source <build/retail-textured-board/boardmed.obj> --probe <CPU probe>
--output <build/modern-assets> [--render]
Run the production probe --dump-textured-board first. Raw engine Y-up units,
identity root, unitsPerMeter=1, yaw=0, offset=0, groundToZero=false.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import bpy
from mathutils import Vector


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def glb(path):
    data = path.read_bytes()
    magic,version,length = struct.unpack_from('<III',data)
    if (magic,version,length)!=(0x46546c67,2,len(data)):
        raise RuntimeError('invalid GLB')
    size,kind = struct.unpack_from('<II',data,12)
    if kind!=0x4e4f534a:
        raise RuntimeError('missing GLB JSON')
    document = json.loads(data[20:20+size])
    size_bin,kind_bin = struct.unpack_from('<II',data,20+size)
    if kind_bin!=0x004e4942:
        raise RuntimeError('missing GLB BIN')
    return document,data[28+size:28+size+size_bin]


def vectors(doc,binary,index):
    acc = doc['accessors'][index]
    view = doc['bufferViews'][acc['bufferView']]
    if acc['componentType']!=5126 or acc['type']!='VEC3' or 'sparse' in acc:
        raise RuntimeError('expected dense float vectors')
    offset = view.get('byteOffset',0)+acc.get('byteOffset',0)
    stride = view.get('byteStride',12)
    return [struct.unpack_from('<fff',binary,offset+i*stride) for i in range(acc['count'])]


def indices(doc,binary,index):
    acc = doc['accessors'][index]; view = doc['bufferViews'][acc['bufferView']]
    formats = {5121:'B',5123:'H',5125:'I'}
    fmt = formats[acc['componentType']]
    start = view.get('byteOffset',0)+acc.get('byteOffset',0)
    step = struct.calcsize(fmt)
    return [struct.unpack_from('<'+fmt,binary,start+i*step)[0] for i in range(acc['count'])]


def oriented_triangle(corners):
    value=tuple(corners)
    return min(value,value[1:]+value[:1],value[2:]+value[:2])


def obj_contract(source):
    positions=[]; uvs=[]; triangles=[]; textured=[]; material=None
    texture_materials=set(); current=None
    for line in source.with_suffix('.mtl').read_text().splitlines():
        parts=line.split()
        if parts and parts[0]=='newmtl': current=parts[1]
        elif parts and parts[0]=='map_Kd': texture_materials.add(current)
    for line in source.read_text().splitlines():
        parts=line.split()
        if parts and parts[0]=='v':
            positions.append(tuple(float(v) for v in parts[1:4]))
        elif parts and parts[0]=='vt':
            uvs.append(tuple(float(v) for v in parts[1:3]))
        elif parts and parts[0]=='usemtl': material=parts[1]
        elif parts and parts[0]=='f':
            if len(parts)!=4: raise RuntimeError('production dump must be triangulated')
            corners=[tuple(int(i)-1 for i in v.split('/')) for v in parts[1:]]
            triangles.append(tuple(c[0] for c in corners))
            if material in texture_materials: textured.append((material,corners))
    from collections import Counter
    normalized=lambda p:tuple(round(v,4) for v in p)
    topology=Counter(oriented_triangle([normalized(positions[i]) for i in tri]) for tri in triangles)
    texture_proof=Counter((mat,oriented_triangle([(normalized(positions[c[0]]),
        (round(uvs[c[1]][0],6),round(1-uvs[c[1]][1],6))) for c in tri])) for mat,tri in textured)
    return positions,topology,texture_proof


def material_colors(path):
    colors={}; current=None
    for line in path.read_text().splitlines():
        parts=line.split()
        if parts and parts[0]=='newmtl': current=parts[1]
        elif parts and parts[0]=='Kd': colors[current]=[float(v) for v in parts[1:4]]
    return colors


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('source','probe','output'):
        parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--render',action='store_true')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    source,probe,output=(p.resolve() for p in (args.source,args.probe,args.output))
    build=Path(__file__).resolve().parents[2]/'build'
    source.relative_to(build.resolve()); output.relative_to(build.resolve())
    contract_path=source.with_name('boardmed_contract.json')
    contract=json.loads(contract_path.read_text())
    if not contract.get('production_geometry_decoder') or contract['data_id']!=0x80003:
        raise RuntimeError('requires production ClassicMedium contract')
    protected={str(p):digest(p) for p in source.parent.iterdir() if p.is_file()}
    positions,retail_triangles,retail_uvs=obj_contract(source)
    colors=material_colors(source.with_suffix('.mtl'))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.wm.obj_import(filepath=str(source),forward_axis='NEGATIVE_Z',up_axis='Y',
                          use_split_objects=False,use_split_groups=False)
    objects=[obj for obj in bpy.context.scene.objects if obj.type=='MESH']
    if not objects: raise RuntimeError('empty board import')
    for obj in objects:
        bpy.context.view_layer.objects.active=obj
        obj.select_set(True)
        bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
        obj['provenance']='production decoded retail ClassicMedium; no reconstructed geometry'
        obj['data_id']=0x80003
        for mat in obj.data.materials:
            mat.use_nodes=True
            node=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
            node.inputs['Metallic'].default_value=0
            node.inputs['Roughness'].default_value=.72
            mat.surface_render_method='DITHERED'
            # Remove any alpha link: retail decoded DIB textures are fully opaque.
            for link in list(node.inputs['Alpha'].links): mat.node_tree.links.remove(link)
            node.inputs['Alpha'].default_value=1
    destination=output/'board'/'usa_board_runtime.glb'
    destination.parent.mkdir(parents=True,exist_ok=True)
    candidate=destination.with_name('.usa_board_runtime.candidate.glb')
    try:
        bpy.ops.export_scene.gltf(filepath=str(candidate),export_format='GLB',use_selection=True,
            export_yup=True,export_animations=False,export_extras=True,export_cameras=False,
            export_lights=False,export_materials='EXPORT')
        doc,binary=glb(candidate)
        if any(k in n for n in doc['nodes'] for k in ('matrix','translation','rotation','scale')):
            raise RuntimeError('USA export requires identity nodes')
        # Blender's two proper axis rotations can introduce float roundoff;
        # restore only exact production integer positions within .001 raw unit.
        source_positions=set(positions)
        max_axis_roundoff=0.0
        for mesh in doc['meshes']:
            for prim in mesh['primitives']:
                index=prim['attributes']['POSITION']
                acc=doc['accessors'][index]; view=doc['bufferViews'][acc['bufferView']]
                start=view.get('byteOffset',0)+acc.get('byteOffset',0)
                stride=view.get('byteStride',12)
                binary=bytearray(binary)
                for i,value in enumerate(vectors(doc,binary,index)):
                    exact=tuple(float(round(v)) for v in value)
                    error=max(abs(a-b) for a,b in zip(exact,value))
                    if exact not in source_positions or error>.001:
                        raise RuntimeError('axis conversion differs from production positions')
                    max_axis_roundoff=max(max_axis_roundoff,error)
                    struct.pack_into('<fff',binary,start+i*stride,*exact)
                if 'min' in acc: acc['min']=[round(v) for v in acc['min']]
                if 'max' in acc: acc['max']=[round(v) for v in acc['max']]
        for mat in doc['materials']:
            mat['alphaMode']='OPAQUE'
            mat.pop('alphaCutoff',None)
            pbr=mat['pbrMetallicRoughness']
            pbr['metallicFactor']=0; pbr['roughnessFactor']=.72
            pbr['baseColorFactor']=colors[mat['name']]+[1]
        # Canonical JSON only: embedded image bytes and authored attributes untouched.
        encoded=json.dumps(doc,separators=(',',':'),ensure_ascii=True).encode()
        encoded+=b' '*((-len(encoded))%4)
        candidate.write_bytes(struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary))+
            struct.pack('<II',len(encoded),0x4e4f534a)+encoded+
            struct.pack('<II',len(binary),0x004e4942)+binary)
        from collections import Counter
        exported=Counter(); exported_uvs=Counter(); all_positions=[]
        for mesh in doc['meshes']:
            for prim in mesh['primitives']:
                pos=vectors(doc,binary,prim['attributes']['POSITION']); all_positions+=pos
                normals=vectors(doc,binary,prim['attributes']['NORMAL'])
                if any(not all(__import__('math').isfinite(v) for v in n) or sum(v*v for v in n)<.9 for n in normals):
                    raise RuntimeError('invalid exported normals')
                mat=doc['materials'][prim['material']]
                uv_values=None
                if 'baseColorTexture' in mat['pbrMetallicRoughness']:
                    acc=doc['accessors'][prim['attributes']['TEXCOORD_0']]
                    view=doc['bufferViews'][acc['bufferView']]
                    if acc['componentType']!=5126 or acc['type']!='VEC2':
                        raise RuntimeError('requires float texture UVs')
                    offset=view.get('byteOffset',0)+acc.get('byteOffset',0)
                    stride=view.get('byteStride',8)
                    uv_values=[struct.unpack_from('<ff',binary,offset+j*stride) for j in range(acc['count'])]
                idx=indices(doc,binary,prim['indices'])
                for i in range(0,len(idx),3):
                    exported[oriented_triangle([tuple(round(v,4) for v in pos[j]) for j in idx[i:i+3]])]+=1
                    if uv_values is not None:
                        exported_uvs[(mat['name'],oriented_triangle([(tuple(round(v,4) for v in pos[j]),
                            tuple(round(v,6) for v in uv_values[j])) for j in idx[i:i+3]]))]+=1
        if exported_uvs!=retail_uvs:
            raise RuntimeError('GLB textured triangle UVs differ from production runtime UVs')
        if exported!=retail_triangles:
            raise RuntimeError('GLB triangle positions differ from exact production OBJ')
        minimum=[min(p[i] for p in all_positions) for i in range(3)]
        maximum=[max(p[i] for p in all_positions) for i in range(3)]
        if max(abs(a-b) for values,target in ((minimum,contract['minimum']),(maximum,contract['maximum'])) for a,b in zip(values,target))>.001:
            raise RuntimeError('raw bounds differ from production contract')
        check=subprocess.run([str(probe),'--gltf',str(candidate),'1','0','0','0','0','--keep-ground'],capture_output=True,text=True)
        if check.returncode: raise RuntimeError(check.stdout+check.stderr)
        if any(digest(Path(p))!=value for p,value in protected.items()):
            raise RuntimeError('protected production dump changed')
        report={'provenance':'production MeshRuntime ClassicMedium default pose, exact textured triangle export',
            'contract':contract,'source_hashes':protected,'minimum':minimum,'maximum':maximum,
            'exact_triangle_positions':True,'exact_textured_triangle_uvs':True,'axis_float_roundoff_max_raw':max_axis_roundoff,'opaque':True,'metallic':0,'roughness':.72,
            'probe':check.stdout,'bytes':candidate.stat().st_size,'sha256':digest(candidate)}
        os.replace(candidate,destination)
        destination.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
        print('USA_BOARD_READY',report['sha256'],report['bytes'],minimum,maximum)
    finally:
        if candidate.exists(): candidate.unlink()
    if args.render:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=str(destination))
        scene=bpy.context.scene
        world=bpy.data.worlds.new('Retail board studio'); scene.world=world; world.use_nodes=True
        world.node_tree.nodes['Background'].inputs[1].default_value=.7
        midpoint=[(a+b)*.5 for a,b in zip(minimum,maximum)]
        center=Vector((midpoint[0],-midpoint[2],midpoint[1]))
        span=max(maximum[0]-minimum[0],maximum[2]-minimum[2])
        bpy.ops.object.camera_add(location=center+Vector((0,-span*.2,span*1.5)))
        scene.camera=bpy.context.object; scene.camera.rotation_euler=(center-scene.camera.location).to_track_quat('-Z','Y').to_euler()
        scene.camera.data.type='ORTHO'; scene.camera.data.ortho_scale=span*1.2
        scene.camera.data.clip_end=span*5
        bpy.ops.object.light_add(type='AREA',location=center+Vector((0,0,span)))
        bpy.context.object.data.energy=span*span*18; bpy.context.object.data.size=span
        scene.render.engine='CYCLES'; scene.cycles.device='CPU'; scene.cycles.samples=16
        scene.render.resolution_x=1200; scene.render.resolution_y=1200; scene.render.resolution_percentage=100
        scene.render.filepath=str(build/'authoring'/'usa_board_runtime.png')
        Path(scene.render.filepath).parent.mkdir(parents=True,exist_ok=True)
        bpy.ops.render.render(write_still=True)


if __name__=='__main__': main()

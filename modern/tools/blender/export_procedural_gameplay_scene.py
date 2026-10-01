"""Recovered procedural board/city adapted to USA gameplay through exact retail printing.

No recovered blend or retail archive is modified. Qualified baked board cache
supplies actual recovered sculpted foundation, road/card-pad and central logo
geometry/materials. USA case printing uses production-decoded OBJ triangles/UVs,
clipped exactly to the forty measured retail cells. City assets remain separate.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import struct
import tempfile
from collections import defaultdict
import numpy as np

import bpy
from mathutils import Matrix,Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_monopoly_assets import retail_board_measurement,retail_case_rectangles,reflect_runtime_print
from export_environment import simple_material,Y_UP
from bake_monopoly_materials import sanitize_evaluated_mesh


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def probe(path,executable):
    result=subprocess.run([str(executable),'--gltf',str(path),'200','0','2430','0','2430','--keep-ground'],capture_output=True,text=True)
    if result.returncode: raise RuntimeError(result.stdout+result.stderr)
    return dict(line.split('\t',1) for line in result.stdout.splitlines() if '\t' in line)


def compact_static_board(path):
    """Merge exact exported vertex streams by render-equivalent material/schema.

    This operates on bytes after Blender export: no UV interpolation, normal
    recomputation, vertex welding or geometry simplification is introduced.
    """
    data=path.read_bytes();json_size,json_kind=struct.unpack_from('<II',data,12)
    if json_kind!=0x4e4f534a:raise RuntimeError('GLB JSON chunk required')
    doc=json.loads(data[20:20+json_size]);bin_size,bin_kind=struct.unpack_from('<II',data,20+json_size)
    if bin_kind!=0x004e4942:raise RuntimeError('GLB binary chunk required')
    binary=data[28+json_size:28+json_size+bin_size]
    if any(any(key in node for key in ('matrix','translation','rotation','scale','children')) for node in doc['nodes']):
        raise RuntimeError('static board consolidation requires baked identity roots')
    materials=[];material_map={};signatures={}
    for index,material in enumerate(doc['materials']):
        semantic={k:v for k,v in material.items() if k not in ('name','extras')}
        signature=json.dumps(semantic,sort_keys=True,separators=(',',':'))
        if signature not in signatures:
            signatures[signature]=len(materials);materials.append(material)
        material_map[index]=signatures[signature]
    dimensions={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}
    components={5121:(1,'B'),5123:(2,'H'),5125:(4,'I'),5126:(4,'f')}
    def stream(index):
        accessor=doc['accessors'][index]
        if accessor.get('sparse'):raise RuntimeError('sparse board accessors unsupported')
        view=doc['bufferViews'][accessor['bufferView']]
        size=components[accessor['componentType']][0]*dimensions[accessor['type']]
        stride=view.get('byteStride',size);start=view.get('byteOffset',0)+accessor.get('byteOffset',0)
        payload=b''.join(binary[start+i*stride:start+i*stride+size] for i in range(accessor['count']))
        if len(payload)!=accessor['count']*size:raise RuntimeError('truncated board accessor')
        schema={k:accessor[k] for k in ('componentType','type','normalized') if k in accessor}
        return schema,payload,accessor['count']
    groups={};before=0;triangles=0
    for mesh in doc['meshes']:
        for primitive in mesh['primitives']:
            before+=1
            if primitive.get('mode',4)!=4 or primitive.get('targets'):raise RuntimeError('static triangles required')
            attributes={key:stream(index) for key,index in primitive['attributes'].items()}
            vertex_counts={item[2] for item in attributes.values()}
            if len(vertex_counts)!=1:raise RuntimeError('board attribute count mismatch')
            count=vertex_counts.pop()
            key=(material_map[primitive['material']],tuple((name,json.dumps(attributes[name][0],sort_keys=True)) for name in sorted(attributes)))
            group=groups.setdefault(key,{'attributes':{name:bytearray() for name in attributes},'count':0,'indices':[],'schemas':{name:item[0] for name,item in attributes.items()}})
            index_schema,index_bytes,index_count=stream(primitive['indices'])
            if index_count%3:raise RuntimeError('board index stream not triangles')
            code=components[index_schema['componentType']][1]
            indices=struct.unpack('<'+code*index_count,index_bytes)
            if max(indices)>=count:raise RuntimeError('board vertex index outside accessor')
            group['indices'].extend(index+group['count'] for index in indices)
            for name,item in attributes.items():group['attributes'][name].extend(item[1])
            group['count']+=count;triangles+=index_count//3
    compact=bytearray();views=[];accessors=[]
    def append_view(payload,target=None):
        compact.extend(b'\0'*((-len(compact))%4));view={'buffer':0,'byteOffset':len(compact),'byteLength':len(payload)}
        if target:view['target']=target
        views.append(view);compact.extend(payload);return len(views)-1
    for image in doc.get('images',[]):
        view=doc['bufferViews'][image['bufferView']];start=view.get('byteOffset',0)
        image['bufferView']=append_view(binary[start:start+view['byteLength']])
    primitives=[]
    for (material,_),group in groups.items():
        attributes={}
        for name,payload in group['attributes'].items():
            accessor={**group['schemas'][name],'bufferView':append_view(payload,34962),'count':group['count']}
            if name=='POSITION':
                values=np.frombuffer(payload,dtype='<f4').reshape(-1,3)
                accessor['min']=values.min(axis=0).tolist();accessor['max']=values.max(axis=0).tolist()
            attributes[name]=len(accessors);accessors.append(accessor)
        index_view=append_view(struct.pack('<'+'I'*len(group['indices']),*group['indices']),34963)
        indices=len(accessors);accessors.append({'bufferView':index_view,'componentType':5125,'type':'SCALAR','count':len(group['indices'])})
        primitives.append({'attributes':attributes,'indices':indices,'material':material,'mode':4})
    doc['materials']=materials;doc['bufferViews']=views;doc['accessors']=accessors
    doc['meshes']=[{'name':'Recovered procedural USA board consolidated static materials','primitives':primitives}]
    doc['nodes']=[{'name':'USA procedural board identity root','mesh':0}];doc['scenes']=[{'nodes':[0]}];doc['scene']=0
    doc['buffers']=[{'byteLength':len(compact)}]
    encoded=json.dumps(doc,ensure_ascii=False,separators=(',',':')).encode('utf-8');encoded+=b' '*((-len(encoded))%4)
    compact.extend(b'\0'*((-len(compact))%4))
    path.write_bytes(struct.pack('<III',0x46546c67,2,28+len(encoded)+len(compact))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(compact),0x004e4942)+compact)
    return {'original_primitives':before,'consolidated_primitives':len(primitives),'unique_render_materials':len(materials),
        'triangles_preserved':triangles,'vertex_attribute_bytes_preserved':True,'embedded_image_bytes_preserved':True,
        'method':'exact exported attribute concatenation; indexed winding preserved; no normal/UV recalculation'}


def export_selected(path,objects,report,executable):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects: obj.select_set(True)
    path.parent.mkdir(parents=True,exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
        export_yup=True,export_materials='EXPORT',export_normals=True,export_texcoords=True,
        export_tangents=False,export_animations=False,export_cameras=False,export_lights=False,
        export_extras=True,export_skins=False,export_morph=False)
    if report.get('vector_print_contract'):
        report['static_batch_consolidation']=compact_static_board(path)
    report['production_cpu_probe']=probe(path,executable)
    report['sha256']=digest(path); report['bytes']=path.stat().st_size
    path.with_suffix('.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return report


def decoded_paper_color(obj):
    """Area-weighted dominant decoded texel, converted explicitly sRGB to linear."""
    votes=defaultdict(float);image_buffers={}
    uv=obj.data.uv_layers.active
    for polygon in obj.data.polygons:
        material=obj.data.materials[polygon.material_index]
        textures=[n for n in material.node_tree.nodes if n.type=='TEX_IMAGE']
        if not textures:continue # Solid frame triangles contain no printed paper.
        if len(textures)!=1:raise RuntimeError('ambiguous decoded original USA bitmap')
        image=textures[0].image
        if image.name not in image_buffers:
            image_buffers[image.name]=np.array(image.pixels[:],dtype=np.float32).reshape(image.size[1],image.size[0],4)
        pixels=image_buffers[image.name];height,width=pixels.shape[:2]
        points=[obj.data.vertices[v].co for v in polygon.vertices]
        triangle_uv=[uv.data[loop].uv for loop in polygon.loop_indices]
        area=(points[1]-points[0]).cross(points[2]-points[0]).length*.5
        samples=[]
        for a in range(12):
            for b in range(12-a):
                u=(a+.333)/12;v=(b+.333)/12
                samples.append(triangle_uv[0]*(1-u-v)+triangle_uv[1]*u+triangle_uv[2]*v)
        for sample in samples:
            x=max(0,min(width-1,int(sample.x*width)));y=max(0,min(height-1,int(sample.y*height)))
            rgb=tuple(int(round(float(c)*255)) for c in pixels[y,x,:3])
            votes[rgb]+=area/len(samples)
    if not votes:raise RuntimeError('USA background sampling failed')
    srgb=max(sorted(votes),key=lambda color:votes[color])
    linear=tuple((c/255)/12.92 if c/255<=.04045 else ((c/255+.055)/1.055)**2.4 for c in srgb)
    return srgb,linear


def vector_print(source,labels_path,selected):
    contract=json.loads(labels_path.read_text(encoding='utf-8'))
    squares=contract.get('squares',[])
    if (contract.get('label_contract_version')!=1 or contract.get('edition')!='USA' or
        contract.get('language_id')!=1 or contract.get('city')!=0 or not contract.get('immutable_rules') or len(squares)!=40):
        raise RuntimeError('authoritative USA square label contract required')
    for i,square in enumerate(squares):
        if (square.get('index')!=i or square.get('type')!=i or square.get('name_message_id')!=1001+i or
            not square.get('name_utf8','').strip() or square['name_utf8'].startswith(('*','#')) or
            bool(square.get('price_text_usd'))!=bool(square.get('ownable'))):
            raise RuntimeError(f'invalid authoritative label {i}')
    with bpy.data.libraries.load(str(source),link=False) as (available,loaded):
        if 'Nom de case' not in available.objects:raise RuntimeError('recovered case font missing')
        loaded.objects=['Nom de case']
    reference=loaded.objects[0]
    if reference.type!='FONT':raise RuntimeError('recovered case font invalid')
    ink=bpy.data.materials.new('USA vector charcoal ink');ink.use_nodes=True
    ink.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.018,.023,.020,1)
    ink.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.82
    records=[]
    for square,rect in zip(squares,retail_case_rectangles()):
        i=square['index']
        if i%10==0:continue
        original=next(obj for obj in selected if obj.name==f'USA case {i:02d}')
        paper_srgb,paper_linear=decoded_paper_color(original)
        paper=bpy.data.materials.new(f'USA decoded paper {i:02d}');paper.use_nodes=True
        paper.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(*paper_linear,1)
        paper.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.72
        center=Vector(((rect[0]+rect[1])/400-12.15,-(rect[2]+rect[3])/400+12.15,0))
        rotation=Matrix.Rotation(math.radians(-90+90*(i//10)),4,'Z')
        def place(mesh,name,material):
            mesh.transform(Matrix.Translation(center)@rotation);mesh.materials.append(material);mesh.update()
            obj=bpy.data.objects.new(name,mesh);bpy.context.scene.collection.objects.link(obj);selected.append(obj)
        def patch(low,high,label):
            mesh=bpy.data.meshes.new(label);mesh.from_pydata([(-.915,low,.0005),(.915,low,.0005),(.915,high,.0005),(-.915,high,.0005)],[],[(0,1,2,3)])
            place(mesh,label,paper)
        def letters(text,cy,maxheight,label):
            curve=bpy.data.curves.new(label,'FONT');curve.body=text;curve.font=reference.data.font
            curve.align_x='CENTER';curve.size=.42;curve.space_line=.95;curve.resolution_u=5
            obj=bpy.data.objects.new(label,curve);bpy.context.scene.collection.objects.link(obj)
            bpy.context.view_layer.update();mesh=bpy.data.meshes.new_from_object(obj.evaluated_get(bpy.context.evaluated_depsgraph_get()))
            lo=Vector(tuple(min(v.co[a] for v in mesh.vertices) for a in range(3)))
            hi=Vector(tuple(max(v.co[a] for v in mesh.vertices) for a in range(3)))
            scale=min(1,1.72/(hi.x-lo.x),maxheight/(hi.y-lo.y))
            midpoint=(lo+hi)*.5
            mesh.transform(Matrix.Scale(scale,4)@Matrix.Translation(-midpoint))
            reflect_runtime_print(mesh,obj)
            # Production GPU projection establishes the same upright baseline
            # as the untouched retail icons. Rotate glyphs about their centre.
            mesh.transform(Matrix.Rotation(math.pi,4,'Z'))
            mesh.transform(Matrix.Translation((0,cy,.0015)))
            bpy.data.objects.remove(obj,do_unlink=True)
            place(mesh,label,ink)
        words=square['name_utf8'].strip().upper().split();lines=[];line=''
        for word in words:
            if line and len(line+' '+word)>13:lines.append(line);line=word
            else:line=(line+' '+word).strip()
        if line:lines.append(line)
        lot=0<=square['group']<=7
        # Long legacy labels extend toward the cell middle. Lots contain no
        # illustration; railroad/tax headers end before their original icons.
        low,high=(-1.105,1.20) if lot else ((.25,1.56) if square['group'] in (8,12) else (.73,1.56))
        patch(low,high,f'USA name paper {i:02d}')
        letters('\n'.join(lines),.725 if lot else 1.145,.85 if lot else .73,f'USA vector name {i:02d}')
        if square['ownable']:
            patch(-1.57,-1.11,f'USA price paper {i:02d}')
            letters(square['price_text_usd'],-1.34,.34,f'USA vector price {i:02d}')
        records.append({'case':i,'message_id':square['name_message_id'],'name':square['name_utf8'],
            'price':square['price_text_usd'],'paper_srgb8':paper_srgb,'paper_linear':paper_linear,'glyph_local_rotation_degrees':180,'name_mask_local_y':[low,high],'yaw_blender_degrees':-90+90*(i//10)})
    bpy.data.objects.remove(reference,do_unlink=True)
    return {'label_contract_sha256':digest(labels_path),'source_font':'Nom de case',
        'vector_names':len(records),'vector_prices':sum(bool(r['price']) for r in records),
        'corners_preserved':[0,10,20,30],'source_icons_stripes_and_uv_preserved':True,'cases':records}


def board(source_cache,retail_obj,output,executable,source_hash,source,labels):
    measurement=retail_board_measurement(retail_obj)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(source_cache))
    selected=[]
    for obj in list(bpy.context.scene.objects):
        keep=obj.type=='MESH' and (obj.name.startswith('01 · Plateau carré /') or
            obj.name.startswith('02 · Identité centrale /') or obj.name.startswith('BOLD MONOPOLY'))
        if not keep:
            bpy.data.objects.remove(obj,do_unlink=True); continue
        world=obj.matrix_world.copy(); obj.parent=None; obj.data.transform(world); obj.matrix_world=Matrix.Identity(4)
        if obj.name.startswith('01 · Plateau carré / cream'):
            # The recovered solid cream support must sit beneath live cell prints.
            low=min(v.co.z for v in obj.data.vertices); high=max(v.co.z for v in obj.data.vertices)
            for vertex in obj.data.vertices: vertex.co.z=-.10+(vertex.co.z-low)/(high-low)*.098
            obj.data.update()
        selected.append(obj)
    if len(selected)!=12: raise RuntimeError('expected twelve recovered foundation/central mesh copies')
    vertices=[];uvs=[];faces=[];current=None;groups={}
    for line in retail_obj.read_text().splitlines():
        parts=line.split()
        if not parts: continue
        if parts[0]=='v': vertices.append(tuple(float(v) for v in parts[1:4]))
        elif parts[0]=='vt': uvs.append(tuple(float(v) for v in parts[1:3]))
        elif parts[0]=='usemtl': current=parts[1]
        elif parts[0]=='f':
            corners=[tuple(int(i)-1 for i in v.split('/')) for v in parts[1:]]
            if len(corners)!=3: raise RuntimeError('retail board must be triangulated')
            if all(abs(vertices[c[0]][1])<.00001 for c in corners):
                faces.append((current,[(vertices[c[0]][0],vertices[c[0]][2],uvs[c[1]][0],uvs[c[1]][1]) for c in corners]))
    colors={};images={}
    for line in retail_obj.with_suffix('.mtl').read_text().splitlines():
        parts=line.split()
        if not parts:continue
        if parts[0]=='newmtl': current=parts[1]
        elif parts[0]=='Kd': colors[current]=tuple(float(v) for v in parts[1:4])
        elif parts[0]=='map_Kd': images[current]=retail_obj.parent/parts[1]
    materials={}
    for name,color in colors.items():
        mat=bpy.data.materials.new('USA live print / '+name);mat.use_nodes=True
        shader=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        shader.inputs['Base Color'].default_value=(*color,1)
        shader.inputs['Metallic'].default_value=0;shader.inputs['Roughness'].default_value=.72
        if name in images:
            texture=mat.node_tree.nodes.new('ShaderNodeTexImage');texture.image=bpy.data.images.load(str(images[name]))
            texture.extension='EXTEND';mat.node_tree.links.new(texture.outputs['Color'],shader.inputs['Base Color'])
        materials[name]=mat
    def clip(poly,axis,bound,greater):
        result=[]
        if not poly:return result
        prior=poly[-1]; inside=lambda p:p[axis]>=bound-1e-8 if greater else p[axis]<=bound+1e-8
        for point in poly:
            a,b=inside(prior),inside(point)
            if a!=b:
                amount=(bound-prior[axis])/(point[axis]-prior[axis])
                result.append(tuple(prior[i]+amount*(point[i]-prior[i]) for i in range(4)))
            if b:result.append(point)
            prior=point
        return result
    records=[]
    for index,rect in enumerate(retail_case_rectangles()):
        positions=[];polygons=[];loop_uv=[];material_indices=[];slots=[]
        for name,triangle in faces:
            polygon=triangle
            for axis,bound,greater in ((0,rect[0],True),(0,rect[1],False),(1,rect[2],True),(1,rect[3],False)):
                polygon=clip(polygon,axis,bound,greater)
            if len(polygon)<3:continue
            if name not in slots:slots.append(name)
            for corner in range(1,len(polygon)-1):
                tri=[polygon[0],polygon[corner],polygon[corner+1]]
                if abs((tri[1][0]-tri[0][0])*(tri[2][1]-tri[0][1])-(tri[1][1]-tri[0][1])*(tri[2][0]-tri[0][0]))<1e-5:continue
                start=len(positions)
                positions.extend(((p[0]-2430)/200,-(p[1]-2430)/200,0) for p in tri)
                polygons.append((start,start+1,start+2));loop_uv.extend((p[2],p[3]) for p in tri)
                material_indices.append(slots.index(name))
        if not polygons:raise RuntimeError(f'case {index} lacks exact retail printing')
        mesh=bpy.data.meshes.new(f'USA case {index:02d} production print');mesh.from_pydata(positions,[],polygons)
        uv=mesh.uv_layers.new(name='ProductionUSAUV')
        for loop,point in zip(uv.data,loop_uv):loop.uv=point
        for name in slots:mesh.materials.append(materials[name])
        for polygon,slot in zip(mesh.polygons,material_indices):polygon.material_index=slot
        mesh.update()
        obj=bpy.data.objects.new(f'USA case {index:02d}',mesh);bpy.context.scene.collection.objects.link(obj);selected.append(obj)
        lows=[min(p[a] for p in positions) for a in range(2)]; highs=[max(p[a] for p in positions) for a in range(2)]
        raw=[lows[0]*200+2430,highs[0]*200+2430,-highs[1]*200+2430,-lows[1]*200+2430]
        if max(abs(a-b) for a,b in zip(raw,rect))>.001:raise RuntimeError(f'case {index} print bounds mismatch')
        records.append({'case':index,'rectangle_decoded_xz':list(rect),'center_decoded_units':[(rect[0]+rect[1])/2,0,(rect[2]+rect[3])/2], 'triangle_count':len(polygons),'source_uv_interpolation_exact':True})
    vector_contract=vector_print(source,labels,selected)
    report={'vector_print_contract':vector_contract,'source_blend_sha256':source_hash,'source_baked_board_sha256':digest(source_cache),
        'provenance':'actual recovered procedural foundation/roads/central sculpture with production USA printing on40cases',
        'recovered_geometry_object_count':12,'retail_print_case_count':40,'cases':records,
        'excluded':'French case fonts/glyphs, French central motto; no lost resource fabrication',
        'units_per_meter':200,'yaw_degrees':0,'local_offset':[2430,0,2430],'ground_to_zero':False,
        'bounds_contract_metres':{'minimum':[-12.57,-.346,-12.57],'maximum':[12.57,.115,12.57]},
        'retail_source_obj_sha256':digest(retail_obj)}
    return export_selected(output/'board'/'usa_procedural_runtime.glb',selected,report,executable)


CITY_GROUPS={
 'buildings_front_a':lambda n:n in {'PARIS_AVANT_00','PARIS_AVANT_01'},
 'buildings_front_b':lambda n:n in {'PARIS_AVANT_02','PARIS_AVANT_03'},
 'buildings_back':lambda n:n.startswith('PARIS_ARRIERE_'),
 'buildings_east':lambda n:n.startswith('PARIS_EST_'),
 'buildings_west_a':lambda n:n in {'PARIS_OUEST_00','PARIS_OUEST_01'},
 'buildings_west_b':lambda n:n=='PARIS_OUEST_02',
 'skyline':lambda n:n.startswith('SKYLINE_'),
 'street_furniture':lambda n:n.startswith(('Reverbere ','Banc parisien ','Corbeille ')),
 'trees_a':lambda n:n.startswith('Tilleul ') and int(n.rsplit(' ',1)[1])<5,
 'trees_b':lambda n:n.startswith('Tilleul ') and 5<=int(n.rsplit(' ',1)[1])<10,
 'trees_c':lambda n:n.startswith('Tilleul ') and 10<=int(n.rsplit(' ',1)[1])<15,
 'trees_d':lambda n:n.startswith('Tilleul ') and 15<=int(n.rsplit(' ',1)[1]),
 'people_vehicles':lambda n:n.startswith(('Passant ','Automobile rue ')),
 'prison':lambda n:n.startswith('PRISON —'),
 'grand_hotel':lambda n:n=='GRAND_HOTEL',
 'red_hotel':lambda n:n=='HOTEL_ROUGE_PISCINE',
 'landmarks':lambda n:n in {'FONTAINE — bassins et jets','GARE — façade horloge et trois verrières','Colonne Morris'},
}


def city(source,output,executable,source_hash,requested=None):
    bpy.ops.wm.open_mainfile(filepath=str(source))
    materials={}; reports={};dg=bpy.context.evaluated_depsgraph_get()
    yaw=Y_UP.inverted()@Matrix.Rotation(math.radians(-90),4,'Y')@Y_UP
    for slug,predicate in CITY_GROUPS.items():
        if requested and slug not in requested:continue
        collections=sorted((c for c in bpy.data.collections if predicate(c.name)),key=lambda c:c.name)
        if not collections:raise RuntimeError('missing city group '+slug)
        copies=[];records=[];excluded=[]
        for collection in collections:
            roots=[o for o in collection.objects if o.parent is None]
            if len(roots)!=1 or roots[0].type!='EMPTY':raise RuntimeError('ambiguous city root '+collection.name)
            original=roots[0].matrix_world.copy(); pos=Y_UP@original.translation
            target=Y_UP.inverted()@Vector((-pos.z,pos.y,-pos.x))
            placement=Matrix.Translation(target)@yaw@Matrix.Translation(-original.translation)
            records.append({'source_collection':collection.name,'source_root_y_up':list(pos),'retail_center_y_up_metres':[-pos.z,pos.y,-pos.x],'local_yaw_degrees':-90})
            for src in sorted(collection.all_objects,key=lambda o:o.name):
                if src.type=='EMPTY':continue
                if src.type in {'LIGHT','CAMERA'}:
                    excluded.append({'name':src.name,'type':src.type,'reason':'runtime owns lighting/camera'})
                    continue
                if src.type not in {'MESH','FONT','CURVE','SURFACE'}:raise RuntimeError('unsupported city object '+src.name)
                raw=bpy.data.meshes.new_from_object(src.evaluated_get(dg),depsgraph=dg)
                mesh,cleanup=sanitize_evaluated_mesh(raw);bpy.data.meshes.remove(raw)
                reflect_runtime_print(mesh,src)
                mesh.transform(placement@src.matrix_world);mesh.update()
                originals=list(mesh.materials);mesh.materials.clear()
                for mat in originals:mesh.materials.append(simple_material(mat,materials))
                obj=bpy.data.objects.new('Procedural city / '+src.name,mesh);bpy.context.scene.collection.objects.link(obj)
                obj['source_object']=src.name;obj['source_collection']=collection.name;copies.append(obj)
        report={'source_blend_sha256':source_hash,'asset_kind':'procedural_city','slug':slug,'collections':records,
            'geometry_objects':len(copies),'excluded':excluded,'root_identity':True,'units_per_meter':200,'yaw_degrees':0,
            'environment_definition_position':[0,0,0],'global_retall_placement_baked':True,
            'materials':'authored PBR factors/ramp means; procedural bump/transmission not baked in this city slice',
            'rules':'decorative recovered Paris city, independent of USA board rules; no French case labels'}
        path=output/'environment'/f'procedural_city_{slug}.glb'
        print('QUALIFY_CITY',slug,len(copies),flush=True)
        reports[slug]=export_selected(path,copies,report,executable)
        for obj in copies:
            mesh=obj.data;bpy.data.objects.remove(obj,do_unlink=True);bpy.data.meshes.remove(mesh)
    return reports


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('source','baked-board-cache','retail-obj','probe','output'):
        parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--labels-file',type=Path,help='authoritative probe JSON; defaults beside retail OBJ')
    parser.add_argument('--skip-city',action='store_true')
    parser.add_argument('--skip-board',action='store_true')
    parser.add_argument('--city-groups',nargs='+',choices=sorted(CITY_GROUPS))
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    source=args.source.resolve();cache=args.baked_board_cache.resolve();retail=args.retail_obj.resolve();output=args.output.resolve()
    build=Path(__file__).resolve().parents[2]/'build';output.relative_to(build.resolve());cache.relative_to(build.resolve());retail.relative_to(build.resolve())
    labels=(args.labels_file or retail.with_name('boardmed_labels.json')).resolve()
    protected={str(p):digest(p) for p in ((source,cache,retail,labels) if not args.skip_board else (source,cache,retail))}
    output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.procedural-gameplay-',dir=output) as temporary:
        staging=Path(temporary)
        reports={}
        prior=output/'procedural_gameplay_manifest.json'
        if (args.skip_board or args.city_groups) and prior.is_file():
            reports=json.loads(prior.read_text(encoding='utf-8'))
            existing=[reports.get('board',{})]+list(reports.get('city',{}).values())
            if any(r.get('source_blend_sha256')!=protected[str(source)] for r in existing if r):
                raise RuntimeError('cannot merge targeted exports from a different recovered source')
        if not args.skip_board:
            reports['board']=board(cache,retail,staging,args.probe.resolve(),protected[str(source)],source,labels)
        if not args.skip_city:
            reports.setdefault('city',{}).update(city(source,staging,args.probe.resolve(),protected[str(source)],args.city_groups))
        if not reports:raise RuntimeError('empty procedural export request')
        if any(digest(Path(p))!=h for p,h in protected.items()):raise RuntimeError('protected source/cache changed')
        for p in sorted(staging.rglob('*')):
            if p.is_file():
                destination=output/p.relative_to(staging);destination.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(p,destination)
        (output/'procedural_gameplay_manifest.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('PROCEDURAL_GAMEPLAY_READY',json.dumps({k:v['sha256'] for k,v in reports.get('city',{}).items()}))


if __name__=='__main__':main()

"""Offline production-contract authoring, repeat qualification and curated staging.

Run with ordinary Python, not Blender's --python. Inputs are the recovered blend,
existing base GLBs, production contracts (or their tool/retail-root inputs) and
the production probe. Generated contracts include only the 58 reviewed states.
Two fresh Blender processes per token generate isolated candidates. Only the
explicit 58 reviewed states below may be staged; thimble CD and moneybag08/09
remain excluded. Additional profiles receive discrete face qualification.
No DAT parser, retail geometry export, sequence activation or source edits occur.
Failed runs retain diagnostics and leave staging untouched. Existing base and
default ship/dog/horse assets are protected by before/after hashes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys


REVIEWED_TAGS = {
    "race_car": (0x32,0x33,0x34,0x35,0x36),
    "dog": (0x38,0x3a,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x4c,0x4d,0x4e,0x4f,
            0x50,0x51,0x5c,0x5d,0x5f,0x63,0x64,0x65,0x6a,0x6b,0x6c,0x6d,0x87),
    "top_hat": (0x8d,0x90,0x91),
    "ship": (0x18,0x19,0x1b,0x1c,0x1d,0x1e),
    "boot": (0xbf,0xc0,0xc1,0xc6,0xc8,0xc9,0xcb),
    "thimble": (0xcc,0xce),
    "moneybag": (0x06,0x07,0x0c,0x0d),
    "iron": (0xad,0xae,0xaf),
    "horse": (0x94,0x97),
}
EXPECTED_TARGET_COUNTS = dict(race_car=5,dog=57,top_hat=3,ship=6,boot=8,thimble=5,moneybag=6,iron=3,horse=2)
NEW_PROFILES = {"moneybag","iron","horse"}
REPRESENTATIVE_RAW_Y = {"moneybag":-4,"iron":0,"horse":1}
VISUAL_EXCLUSIONS = {"thimble": {0xcd:"compressed dimple surface develops pronounced spikes"},
                     "moneybag": {0x08:"42 sampled folded vertices in reviewed diagnostics",0x09:"97 sampled folded vertices in reviewed diagnostics"}}
PROBE_INVARIANTS = ("vertices","triangles","batches","base_color_map_bindings",
                    "metallic_roughness_map_bindings","normal_map_bindings",
                    "emissive_map_bindings","occlusion_map_bindings","unique_images")


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_reviews(paths):
    """Normalize existing manifests and the reviewed diagnostic states format."""
    combined = {"tokens":{}}
    for path in paths:
        path = path.resolve()
        document = json.loads(path.read_text(encoding="utf-8"))
        tokens = document.get("tokens",{})
        if "states" in document:
            for state in document["states"]:
                if not state["qualified"]:
                    continue
                slug,key = state["token"],f"0x{int(state['tag'],16):x}"
                reference = path.parent/"first/tokens"/f"{slug}_variants"/f"pose_{int(key,16):04x}.glb"
                tokens.setdefault(slug,{"files":{}})["files"][key] = {
                    "sha256":state["sha256"],"production_probe":{"gltf":str(reference)}}
        for slug,entry in tokens.items():
            target = combined["tokens"].setdefault(slug,{"files":{}})["files"]
            for key,state in entry["files"].items():
                key = f"0x{int(key,16):x}"
                if int(key,16) in VISUAL_EXCLUSIONS.get(slug,{}):
                    continue
                if key in target and target[key]["sha256"]!=state["sha256"]:
                    raise RuntimeError(f"conflicting review bindings: {slug} {key}")
                target[key] = state
    return combined if paths else None


def fingerprint_worker(paths_json,output_json):
    """Called inside Blender; reuse the existing GLB reader rather than duplicate it."""
    sys.path.insert(0,str(Path(__file__).resolve().parent/"blender"))
    from export_ship_variants import read_glb,accessor
    from export_token_variants import bake_node_transforms
    import numpy as np
    def significant(value):
        if isinstance(value,dict):
            return {key:significant(item) for key,item in value.items()
                    if key not in ("extras","name","generator","copyright")}
        if isinstance(value,list):
            return [significant(item) for item in value]
        return value
    fingerprints = {}
    request = json.loads(Path(paths_json).read_text(encoding="utf-8"))
    filenames = request if isinstance(request,list) else request["paths"]
    for filename in filenames:
        doc,binary = read_glb(Path(filename))
        canonical = json.dumps(significant(doc),sort_keys=True,separators=(",",":"),allow_nan=False).encode("utf-8")
        fingerprints[filename] = {"render_json_sha256":hashlib.sha256(canonical).hexdigest(),
                                  "binary_payload_sha256":hashlib.sha256(binary).hexdigest()}
    if isinstance(request,dict):
        checks = {}
        for check in request["representative_checks"]:
            base,base_binary = read_glb(Path(check["base"]),require_identity=False)
            base,base_binary,_ = bake_node_transforms(base,base_binary)
            rest,rest_binary = read_glb(Path(check["candidate"]))
            def vectors(doc,binary,name):
                return np.concatenate([accessor(doc,binary,primitive["attributes"][name])
                                       for mesh in doc["meshes"] for primitive in mesh["primitives"]])
            position_error = float(np.abs(vectors(base,base_binary,"POSITION")-vectors(rest,rest_binary,"POSITION")).max())*check["scale"]
            normal_error = float(np.abs(vectors(base,base_binary,"NORMAL")-vectors(rest,rest_binary,"NORMAL")).max())
            tolerance = 0 if check["slug"] in {"moneybag","iron"} else .001
            if position_error>tolerance or normal_error>(0 if tolerance==0 else 2e-6):
                raise RuntimeError(f"representative geometry/normal preservation failed: {check['slug']}")
            checks[check["slug"]] = {"max_position_error_raw":position_error,"max_normal_component_error":normal_error,
                                      "position_tolerance_raw":tolerance,"static_and_variant_floor_raw":check["floor"]}
        fingerprints["representative_geometry_checks"] = checks
    Path(output_json).write_text(json.dumps(fingerprints,indent=2)+"\n",encoding="utf-8")


def invoke(command,log):
    with log.open("w",encoding="utf-8") as stream:
        result = subprocess.run([str(value) for value in command],stdout=stream,
                                stderr=subprocess.STDOUT,timeout=1800)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); inspect {log}")


def probe(path,calibration,executable,log):
    invoke([executable,"--gltf",path,calibration["units_per_metre"],
            calibration["yaw_degrees"],*calibration["offset"],"--keep-ground"],log)
    fields = dict(line.split("\t",1) for line in log.read_text(encoding="utf-8").splitlines() if "\t" in line)
    if any(key not in fields for key in PROBE_INVARIANTS):
        raise RuntimeError(f"incomplete production probe output: {log}")
    return fields


def protected_snapshot(base_assets,output):
    return {path:digest(path) for root in {base_assets,output}
            for path in (root/"tokens").rglob("*.glb") if not path.name.startswith("pose_") or
            path.parent.name in {f"{slug}_variants" for slug in REVIEWED_TAGS if slug not in NEW_PROFILES}}


def assert_preserved(snapshot):
    if any(not path.is_file() or digest(path)!=value for path,value in snapshot.items()):
        raise RuntimeError("existing base/default GLB changed during qualification")


def stage_transaction(files,manifest,output,run):
    """Prepare every file first; roll back completed replacements on exceptions."""
    prepared,backups,changed = {},{},[]
    try:
        for relative,source in files.items():
            destination = output/relative
            destination.parent.mkdir(parents=True,exist_ok=True)
            handle,name = tempfile.mkstemp(prefix=".qualified-",suffix=".tmp",dir=destination.parent)
            os.close(handle)
            temporary = Path(name)
            prepared[destination] = temporary
            shutil.copyfile(source,temporary)
            if digest(source)!=digest(temporary):
                raise RuntimeError(f"staging copy mismatch: {relative}")
        manifest_path = output/"complete_token_variants.json"
        handle,name = tempfile.mkstemp(prefix=".qualified-manifest-",suffix=".tmp",dir=output)
        os.close(handle)
        prepared[manifest_path] = Path(name)
        Path(name).write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
        for destination in prepared:
            if destination.exists():
                # Keep backups beside destinations, so rollback is atomic even
                # when diagnostics/work files are on a different Windows drive.
                handle,name = tempfile.mkstemp(prefix=".qualified-backup-",suffix=".tmp",dir=destination.parent)
                os.close(handle)
                backup = Path(name)
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
        for temporary in [*prepared.values(),*backups.values()]:
            if temporary.exists():
                temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("blender","source","base-assets","probe","output","work-dir"):
        parser.add_argument(f"--{name}",required=True,type=Path)
    parser.add_argument("--contracts",type=Path,help="Existing production contracts for the nine profiles")
    parser.add_argument("--contract-tool",type=Path,help="Production TokenDeformationContract executable; paired with --retail-root")
    parser.add_argument("--retail-root",type=Path,help="Read-only retail runtime-data directory")
    parser.add_argument("--render",action="store_true",help="Fresh paired diagnostic pages on the first export pass")
    parser.add_argument("--review-index",type=Path,action="append",default=[],help="Repeatable: previous qualification index, diagnostic states index or staging manifest; bind hashes or rendering fingerprints")
    args = parser.parse_args()
    args.blender,args.source,args.base_assets,args.probe,args.output,args.work_dir = (
        value.resolve() for value in (args.blender,args.source,args.base_assets,args.probe,args.output,args.work_dir))
    if bool(args.contract_tool)!=bool(args.retail_root) or bool(args.contracts)==bool(args.contract_tool):
        parser.error("provide either --contracts or paired --contract-tool/--retail-root")
    for directory in (args.output,args.work_dir):
        if "source" in {part.casefold() for part in directory.parts}:
            parser.error("output and work directories must remain outside immutable Source/")
    for path in (args.blender,args.source,args.probe):
        if not path.is_file():
            parser.error(f"missing input: {path}")
    exporter = Path(__file__).resolve().parent/"blender/export_token_variants.py"
    args.work_dir.mkdir(parents=True,exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="qualified-",dir=args.work_dir))
    print(f"qualification diagnostics: {run}",flush=True)
    generated_contracts = bool(args.contract_tool)
    if generated_contracts:
        args.contract_tool,args.retail_root = args.contract_tool.resolve(),args.retail_root.resolve()
        if not args.contract_tool.is_file() or not args.retail_root.is_dir():
            parser.error("production contract executable and retail directory must exist")
        args.contracts = run/"production-contracts"
        args.contracts.mkdir()
        for slug,tags in REVIEWED_TAGS.items():
            invoke([args.contract_tool,args.retail_root,f"0x{0x80000+tags[0]:x}",
                    ",".join(f"0x{0x80000+tag:x}" for tag in tags),
                    args.contracts/f"{slug}_contract_production.json"],run/f"{slug}-contract.log")
    else:
        args.contracts = args.contracts.resolve()
    inputs = [args.source,exporter,exporter.parent/"export_ship_variants.py"]
    for slug in REVIEWED_TAGS:
        inputs.extend([args.base_assets/"tokens"/f"{slug}.glb",args.contracts/f"{slug}_contract_production.json"])
    if any(not path.is_file() for path in inputs):
        parser.error("all nine base GLBs and production contracts must exist")
    input_hashes = {path:digest(path) for path in inputs}
    snapshot = protected_snapshot(args.base_assets,args.output)
    review = load_reviews(args.review_index)
    manifest = {"schema":1,"scope":"58 explicitly reviewed states; no sequence eligibility implied",
                "visual_exclusions":{slug:{f"0x{tag:x}":reason for tag,reason in states.items()} for slug,states in VISUAL_EXCLUSIONS.items()},
                "contract_scope":"reviewed58" if generated_contracts else "observed/contracts",
                "fingerprint_policy":"canonical rendering JSON (excluding extras/names/generator/copyright) plus exact BIN payload; existing shared GLB reader",
                "source_blend_sha256":digest(args.source),"tokens":{}}
    files = {}
    candidate_paths,review_paths = [],{}
    try:
        for slug,tags in REVIEWED_TAGS.items():
            base = args.base_assets/"tokens"/f"{slug}.glb"
            contract = args.contracts/f"{slug}_contract_production.json"
            evidence = json.loads(contract.read_text(encoding="utf-8"))["evidence"]
            if not evidence.get("production_geometry_decoder"):
                raise RuntimeError(f"{slug} contract is not production decoded")
            for phase in ("first","repeat"):
                command = [args.blender,"--background","--python-exit-code","1","--python",exporter,"--","--token",slug,
                           "--source",args.source,"--base-glb",base,"--correspondence",contract,
                           "--output",run/phase,"--diagnostics",run/phase/"qualification"/slug,
                           "--contract-states","--allow-partial-qualification"]
                if args.render and phase=="first":
                    command.append("--render")
                if slug in NEW_PROFILES:
                    command.append("--verify-faces")
                invoke(command,run/f"{slug}-{phase}.log")
            first = json.loads((run/"first/qualification"/slug/"qualification.json").read_text(encoding="utf-8"))
            repeat = json.loads((run/"repeat/qualification"/slug/"qualification.json").read_text(encoding="utf-8"))
            expected_count = len(tags) if generated_contracts else EXPECTED_TARGET_COUNTS[slug]
            if first!=repeat or len(first["poses"])!=expected_count:
                raise RuntimeError(f"{slug} qualification reports differ or observed target coverage changed")
            if first["source_blend_sha256"]!=digest(args.source) or first["base_glb_sha256"]!=digest(base) or first["correspondence_sha256"]!=digest(contract):
                raise RuntimeError(f"{slug} candidate provenance mismatch")
            calibration = first["shared_calibration"]
            if slug in NEW_PROFILES and (calibration["offset"][1]!=REPRESENTATIVE_RAW_Y[slug] or calibration["ground_to_zero"]):
                raise RuntimeError(f"{slug} shared representative ground/offset contract changed")
            base_metrics = probe(base,calibration,args.probe,run/f"{slug}-base-probe.log")
            entry = {"base_glb_sha256":digest(base),"contract_sha256":digest(contract),"calibration":calibration,
                     "observed_targets":len(first["poses"]),"numerical_exclusions":first["excluded_tags"],"files":{}}
            for tag in tags:
                key = f"0x{tag:x}"
                pose = first["poses"].get(key,{})
                if not pose.get("qualified") or pose["jacobian_det_min"]<.1 or pose["control_max_error_engine_units"]>1e-8 or pose["nonpositive_jacobian_vertices"] or pose["singular_jacobian_vertices"]:
                    raise RuntimeError(f"reviewed state no longer qualifies: {slug} {key}")
                if tag in VISUAL_EXCLUSIONS.get(slug,{}) or (slug in NEW_PROFILES and (pose.get("discrete_face_folds")!=0 or pose.get("new_degenerate_faces")!=0)):
                    raise RuntimeError(f"excluded or discretely folded state: {slug} {key}")
                relative = Path("tokens")/f"{slug}_variants"/f"pose_{tag:04x}.glb"
                candidate = run/"first"/relative
                candidate_hash = digest(candidate)
                if candidate_hash!=digest(run/"repeat"/relative) or candidate_hash!=pose["sha256"]:
                    raise RuntimeError(f"non-deterministic candidate: {slug} {key}")
                if review and review["tokens"][slug]["files"][key]["sha256"]!=candidate_hash:
                    previous = review["tokens"][slug]["files"][key]
                    if "render_fingerprint" not in previous:
                        old_path = Path(previous.get("production_probe",{}).get("gltf",""))
                        if not old_path.is_file() or digest(old_path)!=previous["sha256"]:
                            raise RuntimeError(f"candidate differs from review-index without a trusted rendering reference: {slug} {key}")
                        review_paths[(slug,key)] = old_path
                metrics = probe(candidate,calibration,args.probe,run/f"{slug}-{tag:04x}-probe.log")
                if any(metrics[field]!=base_metrics[field] for field in PROBE_INVARIANTS):
                    raise RuntimeError(f"source geometry/material counts changed: {slug} {key}")
                files[relative] = candidate
                candidate_paths.append(str(candidate))
                entry["files"][key] = {"relative_path":relative.as_posix(),"sha256":candidate_hash,
                                      "bytes":candidate.stat().st_size,"jacobian_det_min":pose["jacobian_det_min"]}
            manifest["tokens"][slug] = entry
            print(f"{slug}: {len(tags)} reviewed states ready",flush=True)
        if len(files)!=58 or any(digest(path)!=value for path,value in input_hashes.items()):
            raise RuntimeError("qualification inputs changed or reviewed staging set is incomplete")
        fingerprint_inputs = run/"fingerprint-inputs.json"
        fingerprint_output = run/"render-fingerprints.json"
        representative_checks = [{"slug":slug,"base":str(args.base_assets/"tokens"/f"{slug}.glb"),
                                  "candidate":str(files[Path("tokens")/f"{slug}_variants"/f"pose_{REVIEWED_TAGS[slug][0]:04x}.glb"]),
                                  "scale":manifest["tokens"][slug]["calibration"]["units_per_metre"],"floor":REPRESENTATIVE_RAW_Y[slug]}
                                 for slug in sorted(NEW_PROFILES)]
        fingerprint_inputs.write_text(json.dumps({"paths":candidate_paths+[str(path) for path in review_paths.values()],
                                                 "representative_checks":representative_checks}),encoding="utf-8")
        invoke([args.blender,"--background","--python-exit-code","1","--python",Path(__file__).resolve(),"--","--fingerprint-worker",
                fingerprint_inputs,fingerprint_output],run/"render-fingerprint.log")
        fingerprints = json.loads(fingerprint_output.read_text(encoding="utf-8"))
        manifest["representative_geometry_checks"] = fingerprints["representative_geometry_checks"]
        for slug,entry in manifest["tokens"].items():
            for key,state in entry["files"].items():
                current = fingerprints[str(files[Path(state["relative_path"])])]
                state["render_fingerprint"] = current
                if review:
                    previous = review["tokens"][slug]["files"][key]
                    old = previous.get("render_fingerprint")
                    if old is None and (slug,key) in review_paths:
                        old = fingerprints[str(review_paths[(slug,key)])]
                    if old is not None and current!=old:
                        raise RuntimeError(f"rendering geometry/material differs from review-index: {slug} {key}")
                    state["review_binding"] = "exact file hash" if previous["sha256"]==state["sha256"] else "identical render fingerprint; provenance-only JSON differences"
        assert_preserved(snapshot)
        if any(output_path in snapshot and digest(candidate)!=snapshot[output_path]
               for relative,candidate in files.items() for output_path in [args.output/relative]):
            raise RuntimeError("existing recovered-six reviewed pose hashes would change")
        args.output.mkdir(parents=True,exist_ok=True)
        stage_transaction(files,manifest,args.output,run)
        assert_preserved(snapshot)
    except Exception:
        (run/"FAILED.txt").write_text("Qualification/staging failed; inspect logs. No incomplete candidate pack is approved.\n",encoding="utf-8")
        raise
    print(f"staged {len(files)} reviewed poses: {args.output/'complete_token_variants.json'}",flush=True)


if __name__ == "__main__":
    if "--fingerprint-worker" in sys.argv:
        start = sys.argv.index("--fingerprint-worker")
        fingerprint_worker(*sys.argv[start+1:start+3])
    else:
        main()

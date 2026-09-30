#!/usr/bin/env python3
"""Offline World3D compiler; dependencies stay in the ignored build cache."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

MODERN = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name("world3d_shader_toolchain.json")


def run(args: list[str | Path], *, capture: bool = False) -> str:
    command = [str(arg) for arg in args]
    result = subprocess.run(command, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout or ""


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def unpack(archive: Path, destination: Path, *, source_only: bool = False) -> None:
    # Extract only regular files/directories; no archive links or path escapes.
    destination.mkdir(parents=True, exist_ok=True)
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                target = (destination / member.filename).resolve()
                if not target.is_relative_to(destination.resolve()):
                    raise ValueError("archive path escapes cache")
                if member.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(bundle.read(member))
    else:
        with tarfile.open(archive) as bundle:
            for member in bundle:
                # SPIRV-Cross's large golden fixture corpus contains Windows
                # MAX_PATH names. It is not needed by the CLI build (tests OFF).
                parts = Path(member.name).parts
                if source_only and len(parts) > 1 and (
                        parts[1] == "reference" or
                        parts[1].startswith(("shaders", "tests"))):
                    continue
                target = (destination / member.name).resolve()
                if not target.is_relative_to(destination.resolve()):
                    raise ValueError("archive path escapes cache")
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    stream = bundle.extractfile(member)
                    if stream is None:
                        raise ValueError("archive file cannot be read")
                    with stream:
                        target.write_bytes(stream.read())
                    target.chmod(member.mode & 0o777)


def download(spec: dict, cache: Path) -> Path:
    archive = cache / (spec["sha256"] + ".archive")
    if not archive.exists() or digest(archive) != spec["sha256"]:
        print("Downloading", spec["url"], flush=True)
        with urllib.request.urlopen(spec["url"], timeout=120) as response:
            contents = response.read()
        if hashlib.sha256(contents).hexdigest() != spec["sha256"]:
            raise ValueError("download SHA-256 does not match toolchain lock")
        archive.write_bytes(contents)
    return archive


def bootstrap(cache: Path, lock: dict, cmake: str = "cmake") -> None:
    machine = platform.machine().lower()
    system = platform.system()
    if system == "Windows":
        arch = "arm64" if machine in {"arm64", "aarch64"} else "x64"
        if machine not in {"amd64", "x86_64", "arm64", "aarch64"}:
            raise ValueError("DXC bootstrap supports Windows x64/arm64 only")
        key = "windows"
    elif system == "Linux" and machine in {"amd64", "x86_64"}:
        key, arch = "linux", "x64"
    else:
        raise ValueError("No pinned official DXC binary for this host; supply --dxc "
                         "and --spirv-cross built from the locked sources")
    unpack(download(lock["dxc"][key], cache), cache / "dxc")
    spec = lock["spirv_cross"]
    source = cache / ("SPIRV-Cross-" + spec["revision"])
    extracted = source / ".monopoly-extracted"
    if not extracted.exists():
        unpack(download(spec, cache), cache, source_only=True)
        extracted.write_text(spec["sha256"] + "\n", encoding="utf-8")
    build = cache / "spirv-cross-build"
    run([cmake, "-S", source, "-B", build,
         "-DSPIRV_CROSS_CLI=ON", "-DSPIRV_CROSS_ENABLE_TESTS=OFF",
         "-DSPIRV_CROSS_ENABLE_C_API=OFF", "-DCMAKE_BUILD_TYPE=Release"])
    run([cmake, "--build", build, "--config", "Release", "--target", "spirv-cross"])
    executable = build / ("Release/spirv-cross.exe" if system == "Windows" else "spirv-cross")
    if not executable.exists():
        executable = build / "spirv-cross.exe"  # Windows single-config generators.
    shutil.copy2(executable, cache / executable.name)
    (cache / "host.json").write_text(json.dumps({"arch": arch}), encoding="utf-8")


def verify_reflection(reflection: dict, stage: str, name: str = "World3D",
                      samplers: int | None = None) -> None:
    if samplers is None:
        samplers = 1 if name == "World3D" else 6
    expected_set = 1 if stage == "vert" else 3
    ubos = reflection.get("ubos", [])
    if len(ubos) != 1 or (ubos[0].get("set"), ubos[0].get("binding")) != (expected_set, 0):
        raise ValueError(f"{stage}: SDL uniform contract requires one buffer at set {expected_set}, binding 0")
    expected_textures = [(2, binding) for binding in range(samplers)] if stage == "frag" else []
    textures = reflection.get("textures", [])
    bindings = [(texture.get("set"), texture.get("binding")) for texture in textures]
    if sorted(bindings, key=repr) != sorted(expected_textures, key=repr):
        raise ValueError(f"{stage}: {name} requires combined image sampler bindings "
                         f"{expected_textures}; annotate each texture and sampler with "
                         "[[vk::combinedImageSampler]]")
    if name == "ModernPBR" and stage == "frag" and samplers == 6:
        for texture in textures:
            expected_type = "samplerCube" if texture["binding"] == 5 else "sampler2D"
            if texture.get("type") != expected_type:
                raise ValueError(f"frag: binding {texture['binding']} requires {expected_type}")
    if any(reflection.get(key) for key in ["separate_images", "separate_samplers", "ssbos", "images"]):
        raise ValueError(f"{stage}: unexpected resources outside the World3D SDL contract")


def compile_shaders(dxc: Path, cross: Path, source: Path, output: Path, lock: dict,
                    name: str = "World3D", external: bool = False,
                    samplers: int | None = None) -> None:
    if samplers is None:
        samplers = 1 if name == "World3D" else 6
    output.mkdir(parents=True, exist_ok=True)
    manifest: dict = {"toolchain": lock, "sources": {}, "sources_normalized_lf": {}, "outputs": {},
                      "fragment_samplers": samplers,
                      "external_compilers": external,
                      "compiler_hashes": {"dxc": digest(dxc), "spirv_cross": digest(cross)}}
    # Publish only after both stages and all formats pass contract checks.
    with tempfile.TemporaryDirectory(prefix="world3d-", dir=output.parent) as temporary:
        staged = Path(temporary)
        for stage, profile in [("vert", "vs_6_0"), ("frag", "ps_6_0")]:
            shader = source / f"{name}.{stage}.hlsl"
            manifest["sources"][shader.name] = digest(shader)
            manifest["sources_normalized_lf"][shader.name] = hashlib.sha256(
                shader.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
            base = f"{name}.{stage}"
            dxil, spv, msl = [staged / (base + extension) for extension in [".dxil", ".spv", ".msl"]]
            common = [dxc, "-T", profile, "-E", "main", "-O3", "-WX", shader]
            run(common + ["-Fo", dxil])
            run(common + ["-spirv", "-fspv-target-env=vulkan1.0", "-Fo", spv])
            if dxil.read_bytes()[:4] != b"DXBC" or spv.read_bytes()[:4] != b"\x03\x02\x23\x07":
                raise ValueError("compiler produced an invalid shader container")
            reflection = json.loads(run([cross, spv, "--reflect"], capture=True))
            verify_reflection(reflection, stage, name, samplers)
            run([cross, spv, "--msl", "--msl-version", "20100",
                 "--msl-decoration-binding", "--rename-entry-point", "main", "main0", stage,
                 "--output", msl])
            metal = msl.read_text(encoding="utf-8")
            if "main0(" not in metal or not re.search(r"\[\[buffer\(0\)\]\]", metal):
                raise ValueError("MSL entry point/uniform binding does not match SDL")
            if stage == "frag":
                for binding in range(samplers):
                    if any(token not in metal for token in [f"[[texture({binding})]]", f"[[sampler({binding})]]"]):
                        raise ValueError(f"MSL texture/sampler binding {binding} does not match SDL")
            for path in [dxil, spv, msl]:
                manifest["outputs"][path.name] = digest(path)
        for output_name in manifest["outputs"]:
            os.replace(staged / output_name, output / output_name)
        (output / f"{name}.manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=MODERN / "build-shader-tools")
    parser.add_argument("--source", type=Path, default=MODERN / "shaders")
    parser.add_argument("--name", choices=["World3D", "ModernPBR"], default="World3D")
    parser.add_argument("--samplers", type=int, choices=[0, 1, 5, 6],
                        help="fragment combined sampler count (World3D: 1; ModernPBR: 6; older PBR checkpoints: 0 or 5)")
    parser.add_argument("--output", type=Path, required=False)
    parser.add_argument("--bootstrap", action="store_true", help="download locked dependencies and build SPIRV-Cross locally")
    parser.add_argument("--bootstrap-only", action="store_true")
    parser.add_argument("--cmake", default="cmake", help="CMake executable used for the local dependency build")
    parser.add_argument("--dxc", type=Path, help="explicit external compiler; provenance is caller's responsibility")
    parser.add_argument("--spirv-cross", type=Path, help="explicit external compiler; provenance is caller's responsibility")
    args = parser.parse_args()
    if args.name == "World3D" and args.samplers not in {None, 1}:
        parser.error("World3D requires exactly one fragment sampler")
    if args.name == "ModernPBR" and args.samplers not in {None, 0, 5, 6}:
        parser.error("ModernPBR supports zero, five or six fragment samplers")
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    cache = args.cache.resolve()
    if args.bootstrap or args.bootstrap_only:
        cache.mkdir(parents=True, exist_ok=True)
        bootstrap(cache, lock, args.cmake)
    if args.bootstrap_only:
        return 0
    if args.output is None:
        parser.error("--output is required when compiling; source assets are never overwritten by default")
    arch = "arm64" if platform.machine().lower() in {"arm64", "aarch64"} else "x64"
    dxc = args.dxc or cache / "dxc" / "bin" / (f"{arch}/dxc.exe" if os.name == "nt" else "dxc")
    cross = args.spirv_cross or cache / ("spirv-cross.exe" if os.name == "nt" else "spirv-cross")
    if not dxc.is_file() or not cross.is_file():
        parser.error("shader compiler missing; run --bootstrap or specify both compiler paths")
    compile_shaders(dxc.resolve(), cross.resolve(), args.source.resolve(), args.output.resolve(), lock,
                    args.name, args.dxc is not None or args.spirv_cross is not None, args.samplers)
    print(args.name, "shaders compiled and resource contracts verified:", args.output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"World3D shader compilation failed: {error}", file=sys.stderr)
        raise SystemExit(1)

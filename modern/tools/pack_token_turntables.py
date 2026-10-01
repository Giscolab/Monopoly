#!/usr/bin/env python3
"""Losslessly package real GPU RGBA turntables; no image libraries required."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import tempfile
import zlib

SLUGS = ("cannon", "race_car", "dog", "top_hat", "iron", "horse", "ship",
         "boot", "thimble", "wheelbarrow", "moneybag")
WIDTH, HEIGHT, FRAMES = 768, 640, 28
RAW_BYTES = WIDTH * HEIGHT * 4
BUILD = Path(__file__).resolve().parents[1] / "build"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def chunk(kind: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def png_rgba(raw: bytes) -> bytes:
    compressor = zlib.compressobj(level=9)
    stride = WIDTH * 4
    compressed = b"".join(compressor.compress(b"\0" + raw[y * stride:(y + 1) * stride])
                          for y in range(HEIGHT)) + compressor.flush()
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", compressed) + chunk(b"IEND", b""))


def read_manifest(directory: Path, slug: str, token: int) -> tuple[bytes, dict, list[dict], bytes | None]:
    prefix = f"token-turntable-{slug}"
    path = directory / f"{prefix}.tsv"
    require(path.is_file() and path.stat().st_size <= 65536, f"Missing/oversized manifest: {path}")
    source = path.read_bytes()
    headers: dict[str, list[str]] = {}
    frames: list[dict] = []
    for line in source.decode("utf-8").splitlines():
        values = line.split("\t")
        if values[0] == "frame":
            require(len(values) == 12, f"Malformed frame row: {path}")
            require(values[::2] == ["frame", "yaw", "alpha_nonzero", "alpha_zero", "triangles", "file"],
                    "Invalid frame columns")
        else:
            require(values[0] and values[0] not in headers, f"Duplicate/empty header: {path}")
            headers[values[0]] = values[1:]
        if values[0] == "frame":
            fields = dict(zip(values[::2], values[1::2]))
            frame = int(fields["frame"])
            yaw = float(fields["yaw"])
            require(frame == len(frames) and frame < FRAMES, f"Unordered/duplicate frame: {path}")
            require(math.isfinite(yaw) and abs(yaw - 360 * frame / FRAMES) < 0.0001,
                    f"Wrong turntable yaw: {path} frame {frame}")
            require(fields["file"] == f"{prefix}-{frame:02d}", f"Wrong frame filename: {path}")
            nonzero, zero, triangles = (int(fields[k]) for k in ("alpha_nonzero", "alpha_zero", "triangles"))
            require(nonzero > 0 and zero > 0 and nonzero + zero == WIDTH * HEIGHT and triangles > 0,
                    f"Invalid capture statistics: {path} frame {frame}")
            frames.append({"frame": frame, "yaw_degrees": yaw, "alpha_nonzero": nonzero,
                           "alpha_zero": zero, "triangles": triangles, "source_stem": fields["file"]})
    require(headers.get("size") == [str(WIDTH), str(HEIGHT)] and headers.get("frames") == [str(FRAMES)],
            f"Wrong capture dimensions/frame count: {path}")
    require(headers.get("token") == [str(token), "slug", slug], f"Token identity mismatch: {path}")
    require(len(frames) == FRAMES, f"Incomplete turntable: {path}")
    log_source = None
    if not all(key in headers for key in ("backend", "studio_enabled", "msaa_samples")):
        log_path = directory / "capture.log"
        require(log_path.is_file() and log_path.stat().st_size <= 1024 * 1024,
                f"Missing/oversized associated capture log: {log_path}")
        log_source = log_path.read_bytes()
        rows = [line.split("\t") for line in log_source.decode("utf-8").splitlines()
                if line.startswith("turntable\t")]
        require(len(rows) == 1, f"Capture log must contain one actual turntable result: {log_path}")
        row = rows[0]
        require(len(row) == 12 and row[::2] ==
                ["turntable", "frames", "size", "backend", "studio_enabled", "msaa_samples"],
                f"Malformed actual turntable result: {log_path}")
        actual = dict(zip(row[::2], row[1::2]))
        require(actual["turntable"] == prefix and actual["frames"] == str(FRAMES) and
                actual["size"] == f"{WIDTH}x{HEIGHT}", f"Capture log identity mismatch: {log_path}")
        for key in ("backend", "studio_enabled", "msaa_samples"):
            require(key not in headers or headers[key] == [actual[key]], f"TSV/log mismatch: {key}")
            headers[key] = [actual[key]]
    require(len(headers.get("backend", [])) == 1 and headers["backend"][0], f"Missing actual backend: {path}")
    require(headers.get("studio_enabled") in (["0"], ["1"]), f"Missing actual studio status: {path}")
    samples = headers.get("msaa_samples", [])
    require(len(samples) == 1 and int(samples[0]) in (1, 2, 4, 8), f"Missing actual MSAA count: {path}")
    return source, headers, frames, log_source


def remove_staging(directory: Path, parent: Path) -> None:
    # Every recursive removal is a verified temporary sibling of the explicit pack.
    require(not directory.is_symlink() and directory.resolve().parent == parent.resolve() and
            directory.name.startswith(".token-turntables-"), "Unsafe staging cleanup path")
    directory.resolve().relative_to(BUILD.resolve())
    shutil.rmtree(directory)


def package(capture_root: Path, output: Path, asset_root: Path | None) -> None:
    capture_root = capture_root.resolve(strict=True)
    require(capture_root.is_dir(), "capture-root must be a directory")
    require(all((capture_root / slug).is_dir() for slug in SLUGS), "All eleven slug directories are required")
    require(not output.is_symlink(), "Output symlink is unsupported")
    output = output.resolve()
    require(not capture_root.is_relative_to(output), "Output cannot contain source captures")
    build = BUILD.resolve(strict=True)
    output.relative_to(build)
    require(output.name == "tokens" and output.parent.name == "presentation",
            "output must be a presentation/tokens directory inside modern/build")
    require(not any(part.lower() == "source" for part in output.parts), "Source is read-only")
    require(not output.is_symlink(), "Output symlink is unsupported")
    require(not output.exists() or output.is_dir(), "Existing output must be a directory")
    # Check all metadata before producing any staged images.
    manifests = [read_manifest(capture_root / slug, slug, token) for token, slug in enumerate(SLUGS)]
    assets: dict[str, str] = {}
    if asset_root is not None:
        asset_root = asset_root.resolve(strict=True)
        for slug in SLUGS:
            asset = asset_root / "tokens" / f"{slug}.glb"
            require(asset.is_file() and asset.stat().st_size <= 128 * 1024 * 1024,
                    f"Missing/oversized actual GLB: {asset}")
            assets[slug] = digest(asset.read_bytes())
    output.parent.mkdir(parents=True, exist_ok=True)
    lock = output.parent / ".token-turntables-pack.lock"
    lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    os.close(lock_fd)
    stage: Path | None = None
    backup: Path | None = None
    try:
        stage = Path(tempfile.mkdtemp(prefix=".token-turntables-stage-", dir=output.parent))
        for token, slug in enumerate(SLUGS):
            source_manifest, headers, frames, source_log = manifests[token]
            target = stage / slug
            target.mkdir()
            for frame in frames:
                raw_path = capture_root / slug / (frame["source_stem"] + ".rgba")
                require(raw_path.is_file() and raw_path.stat().st_size == RAW_BYTES,
                        f"Wrong/missing RGBA8 payload: {raw_path}")
                raw = raw_path.read_bytes()
                require(len(raw) == RAW_BYTES, f"Capture changed during read: {raw_path}")
                nonzero = sum(alpha != 0 for alpha in raw[3::4])
                require(nonzero == frame["alpha_nonzero"] and WIDTH * HEIGHT - nonzero == frame["alpha_zero"],
                        f"Actual alpha differs from capture manifest: {raw_path}")
                png = png_rgba(raw)
                filename = f'{frame["frame"]:02d}.png'
                (target / filename).write_bytes(png)
                if frame["frame"] == 0:
                    (target / "thumbnail.png").write_bytes(png)
                frame.update({"file": filename, "rgba_sha256": digest(raw), "png_sha256": digest(png)})
            manifest = {"schema": 1, "token": token, "slug": slug, "width": WIDTH, "height": HEIGHT,
                        "pixel_format": "RGBA8", "frame_count": FRAMES,
                        "conversion": "lossless PNG color type 6/filter 0; source alpha unchanged",
                        "scope": "actual GPU presentation turntable; no CNK/gameplay animation proof",
                        "source_manifest_sha256": digest(source_manifest),
                        "backend": headers["backend"][0], "studio_enabled": headers["studio_enabled"] == ["1"],
                        "msaa_samples": int(headers["msaa_samples"][0]), "capture_metadata": headers,
                        "thumbnail": {"file": "thumbnail.png", "identical_to_frame": 0}, "frames": frames}
            if source_log is not None:
                manifest["source_capture_log_sha256"] = digest(source_log)
                manifest["renderer_metadata_source"] = "associated capture.log actual turntable result"
            else:
                manifest["renderer_metadata_source"] = "source TSV headers"
            if slug in assets:
                manifest["glb_sha256"] = assets[slug]
            (target / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if output.exists():
            backup = Path(tempfile.mkdtemp(prefix=".token-turntables-backup-", dir=output.parent))
            backup.rmdir()
            output.rename(backup)
        try:
            stage.rename(output)
            stage = None
        except BaseException:
            if backup is not None:
                backup.rename(output)
                backup = None
            raise
        if backup is not None:
            remove_staging(backup, output.parent)
            backup = None
        print(f"Published {len(SLUGS)} tokens x {FRAMES} exact RGBA frames at {output}")
    finally:
        if stage is not None and stage.exists():
            remove_staging(stage, output.parent)
        lock.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, help="Actual modern asset root containing tokens/<slug>.glb")
    args = parser.parse_args()
    try:
        package(args.capture_root, args.output, args.asset_root)
    except (OSError, ValueError, KeyError, UnicodeError) as error:
        parser.exit(1, f"Token turntable pack rejected: {error}\n")


if __name__ == "__main__":
    main()

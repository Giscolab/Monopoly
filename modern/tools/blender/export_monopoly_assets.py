import argparse
from pathlib import Path

import bpy
from mathutils import Matrix


CONTRACT_VERSION = 1

TOKEN_ASSETS = {
    "Pion automobile": ("race_car", 1),
    "Pion terrier": ("dog", 2),
    "Pion haut-de-forme": ("top_hat", 3),
    "Pion bottine": ("boot", 6),
    "Pion cuirasse": ("ship", 7),
    "Pion de a coudre": ("thimble", 8),
}


def root_for_collection(collection):
    roots = [obj for obj in collection.objects if obj.parent is None]
    if len(roots) != 1:
        raise RuntimeError(
            f"{collection.name}: expected one root object, got {len(roots)}"
        )
    return roots[0]


def select_collection(collection):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in collection.all_objects:
        obj.select_set(True)
    root = root_for_collection(collection)
    bpy.context.view_layer.objects.active = root
    return root


def export_token(collection_name, slug, legacy_index, output_dir):
    collection = bpy.data.collections.get(collection_name)
    if collection is None:
        raise RuntimeError(f"missing Blender collection: {collection_name}")

    root = select_collection(collection)
    original_matrix = root.matrix_world.copy()
    tracked_keys = (
        "asset_kind",
        "asset_slug",
        "legacy_token_index",
        "asset_contract_version",
    )
    saved_properties = {
        key: root[key] for key in tracked_keys if key in root
    }
    introduced = {key for key in tracked_keys if key not in root}

    try:
        root.matrix_world = Matrix.Identity(4)
        root["asset_kind"] = "token"
        root["asset_slug"] = slug
        root["legacy_token_index"] = legacy_index
        root["asset_contract_version"] = CONTRACT_VERSION

        output = output_dir / "tokens" / f"{slug}.glb"
        output.parent.mkdir(parents=True, exist_ok=True)

        result = bpy.ops.export_scene.gltf(
            filepath=str(output),
            export_format="GLB",
            export_materials="EXPORT",
            export_texcoords=True,
            export_normals=True,
            export_tangents=True,
            export_attributes=True,
            export_cameras=False,
            export_lights=False,
            export_animations=True,
            export_morph=True,
            export_morph_normal=True,
            export_morph_tangent=False,
            export_morph_animation=True,
            export_skins=True,
            export_extras=True,
            export_yup=True,
            export_apply=False,
            use_selection=False,
            collection=collection.name,
        )

        if "FINISHED" not in result:
            raise RuntimeError(
                f"{collection_name}: glTF export failed: {result}"
            )
        print(
            "EXPORTED",
            slug,
            "legacy_token",
            legacy_index,
            "bytes",
            output.stat().st_size,
            output,
        )
    finally:
        root.matrix_world = original_matrix
        for key in introduced:
            if key in root:
                del root[key]
        for key, value in saved_properties.items():
            root[key] = value
        bpy.ops.object.select_all(action="DESELECT")


def blender_arguments():
    import sys
    if "--" not in sys.argv:
        return []
    return sys.argv[sys.argv.index("--") + 1:]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        required=True,
        help="Directory receiving generated GLB assets",
    )
    args = parser.parse_args(blender_arguments())

    output_dir = Path(args.output).resolve()
    for collection_name, (slug, legacy_index) in TOKEN_ASSETS.items():
        export_token(
            collection_name,
            slug,
            legacy_index,
            output_dir,
        )

    missing = [
        ("cannon", 0),
        ("iron", 4),
        ("horse", 5),
        ("wheelbarrow", 9),
        ("moneybag", 10),
    ]
    print("MISSING_TOKEN_ASSETS", missing)


if __name__ == "__main__":
    main()

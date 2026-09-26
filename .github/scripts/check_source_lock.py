#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys

EXPECTED_SOURCE_TREE = "dc0b23ed3aed178721143760e68da91d87a3884d"


def git(*args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout.strip()


def source_tree_from(spec: str) -> str:
    return git("rev-parse", f"{spec}:Source")


def fail(actual: str, where: str) -> int:
    print("ERROR: Source/ is immutable and no longer matches the original retail source.", file=sys.stderr)
    print(f"Expected tree: {EXPECTED_SOURCE_TREE}", file=sys.stderr)
    print(f"Actual tree:   {actual}", file=sys.stderr)
    print(f"Checked:       {where}", file=sys.stderr)
    print("Restore Source/ from master before committing or pushing.", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify that Source/ remains byte-for-byte immutable.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--index", action="store_true", help="Check the staged index and local Source/ worktree.")
    group.add_argument("--commit", metavar="REV", help="Check Source/ at a Git revision.")
    args = parser.parse_args()

    if args.index:
        status = git("status", "--porcelain", "--untracked-files=all", "--", "Source")
        if status:
            print("ERROR: local modifications exist under immutable Source/:", file=sys.stderr)
            print(status, file=sys.stderr)
            return 1
        index_root = git("write-tree")
        actual = source_tree_from(index_root)
        if actual != EXPECTED_SOURCE_TREE:
            return fail(actual, "staged index")
        print(f"Source lock OK: {actual}")
        return 0

    actual = source_tree_from(args.commit)
    if actual != EXPECTED_SOURCE_TREE:
        return fail(actual, args.commit)
    print(f"Source lock OK: {actual}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

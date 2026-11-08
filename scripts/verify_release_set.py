#!/usr/bin/env python3
"""Verify FreeBSD/Rocky/Debian candidates belong to one exact source revision.

This reads existing CI artifacts only. It never installs, deploys or runs the
built packages. CI run origin and signing must be authenticated separately.
"""
import argparse
import json
import re
from pathlib import Path
from types import SimpleNamespace

from release_manifest import verify

PLATFORMS = ("freebsd15", "rocky10", "debian13")
HEX_SHA = re.compile(r"[0-9a-f]{40}\Z")
CHECKSUM = re.compile(r"([0-9a-f]{64})  ((?:\./)?[A-Za-z0-9_.+~:-]+)\Z")


def check_sha256sums(root, metadata):
    expected = {entry["filename"]: entry["sha256"]
                for entry in metadata["packages"].values()}
    path = root / "SHA256SUMS"
    if path.is_symlink() or not path.is_file():
        raise ValueError("Missing or linked SHA256SUMS: " + str(root))
    hashes = {}
    for line in path.read_text(encoding="ascii").splitlines():
        m = CHECKSUM.fullmatch(line)
        if not m:
            raise ValueError("Malformed SHA256SUMS entry")
        name = m.group(2).removeprefix("./")
        if name in hashes:
            raise ValueError("Duplicate checksum entry")
        hashes[name] = m.group(1)
    if hashes != expected:
        raise ValueError("SHA256SUMS does not match the candidate manifest")


def check_build_info(root, source_sha):
    path = root / "BUILD-INFO.txt"
    if path.is_symlink() or not path.is_file():
        raise ValueError("Missing or linked BUILD-INFO.txt")
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if not sep or not re.fullmatch(r"[a-z][a-z0-9_]*", key) or key in values:
            raise ValueError("Invalid or duplicate BUILD-INFO key")
        values[key] = value
    if values.get("source_sha") != source_sha:
        raise ValueError("Build provenance commit differs from release set")


def verify_set(root, source_sha):
    if not HEX_SHA.fullmatch(source_sha):
        raise ValueError("A complete lowercase source SHA is required")
    base = Path(root).resolve(strict=True)
    if not base.is_dir():
        raise ValueError("Release set root must be a directory")
    versions = set()
    summary = {"schema": 1, "source_sha": source_sha, "platforms": {}}
    for platform in PLATFORMS:
        folder = base / platform
        if folder.is_symlink() or not folder.is_dir():
            raise ValueError("Missing/unsafe candidate platform: " + platform)
        metadata = verify(SimpleNamespace(
            manifest=str(folder / "RELEASE-MANIFEST.json"),
            source_sha=source_sha, platform=platform, version=None))
        versions.add(metadata["version"].split("-", 1)[0])
        check_sha256sums(folder, metadata)
        check_build_info(folder, source_sha)
        summary["platforms"][platform] = {
            "version": metadata["version"],
            "packages": {
                name: {"sha256": data["sha256"], "filename": data["filename"]}
                for name, data in metadata["packages"].items()
            },
        }
    if len(versions) != 1:
        raise ValueError("Incompatible base versions across OS packages")
    summary["version"] = versions.pop()
    return summary


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", required=True)
    p.add_argument("--source-sha", required=True)
    p.add_argument("--output")
    args = p.parse_args()
    try:
        result = verify_set(args.root, args.source_sha)
        if args.output:
            dest = Path(args.output)
            if dest.is_symlink() or dest.exists():
                raise ValueError("Refusing to overwrite release set receipt")
            with dest.open("x", encoding="utf-8") as stream:
                json.dump(result, stream, indent=2, sort_keys=True)
                stream.write("\n")
        print("PASS: all three OS release-candidate manifests agree on " +
              result["source_sha"] + " version " + result["version"])
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as err:
        p.exit(1, "Release set rejected: " + str(err) + "\n")


if __name__ == "__main__":
    main()

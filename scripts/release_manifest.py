#!/usr/bin/env python3
"""Create/verify release-candidate SHA256 manifests. Never deploy or modify hosts."""
import argparse
import hashlib
import json
import re
from pathlib import Path

SCHEMA = 1
PLATFORMS = {
    "debian13": {
        "liner-notes": r"liner-notes_[0-9][A-Za-z0-9.+~:-]*_amd64\.deb",
        "liner-notes-web": r"liner-notes-web_[0-9][A-Za-z0-9.+~:-]*_all\.deb",
        "liner-notes-link": r"liner-notes-link_[0-9][A-Za-z0-9.+~:-]*_all\.deb",
    },
    "rocky10": {
        "liner-notes": r"liner-notes-[0-9][A-Za-z0-9.+~:-]*\.x86_64\.rpm",
        "liner-notes-web": r"liner-notes-web-[0-9][A-Za-z0-9.+~:-]*\.x86_64\.rpm",
        "liner-notes-link": r"liner-notes-link-[0-9][A-Za-z0-9.+~:-]*\.x86_64\.rpm",
    },
    "freebsd15": {"liner-notes": r"liner-notes-[0-9][A-Za-z0-9.+~:-]*\.pkg"},
}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact(root, filename):
    if not isinstance(filename, str) or filename != Path(filename).name or filename in ("", ".", ".."):
        raise ValueError("Invalid artifact filename")
    path = root / filename
    if path.is_symlink() or not path.is_file() or path.parent.resolve() != root or not path.stat().st_size:
        raise ValueError("Missing, empty or unsafe artifact " + filename)
    return path


def schema(doc):
    if not isinstance(doc, dict) or set(doc) != {"schema", "platform", "version", "source_sha", "packages"}:
        raise ValueError("Invalid manifest schema")
    if type(doc["schema"]) is not int or doc["schema"] != SCHEMA:
        raise ValueError("Unsupported release manifest version")
    platform, version, source = doc["platform"], doc["version"], doc["source_sha"]
    if platform not in PLATFORMS or not isinstance(version, str) or not re.fullmatch(r"[0-9][A-Za-z0-9.+~:-]*", version):
        raise ValueError("Invalid platform or package version")
    if not isinstance(source, str) or not re.fullmatch(r"[0-9a-f]{40}", source):
        raise ValueError("Invalid full source SHA")
    packages = doc["packages"]
    if not isinstance(packages, dict) or set(packages) != set(PLATFORMS[platform]):
        raise ValueError("Missing or unknown binary package")
    for name, item in packages.items():
        if not isinstance(item, dict) or set(item) != {"filename", "size", "sha256"}:
            raise ValueError("Invalid package entry")
        filename = item["filename"]
        if not isinstance(filename, str) or not re.fullmatch(PLATFORMS[platform][name], filename):
            raise ValueError("Unexpected package filename / architecture for " + name)
        if type(item["size"]) is not int or item["size"] < 1:
            raise ValueError("Invalid package size")
        if not isinstance(item["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", item["sha256"]):
            raise ValueError("Invalid checksum")


def create(args):
    root = Path(args.directory).resolve(strict=True)
    if not root.is_dir():
        raise ValueError("Missing artifact directory")
    names = {}
    for entry in args.package:
        name, delimiter, filename = entry.partition("=")
        if not delimiter or name in names:
            raise ValueError("Invalid or duplicate --package")
        names[name] = filename
    if set(names) != set(PLATFORMS[args.platform]):
        raise ValueError("Incomplete package set")
    packages = {}
    for name, filename in sorted(names.items()):
        path = artifact(root, filename)
        packages[name] = {"filename": filename, "size": path.stat().st_size, "sha256": sha256(path)}
    doc = {"schema": SCHEMA, "platform": args.platform, "version": args.version,
           "source_sha": args.source_sha, "packages": packages}
    schema(doc)
    output = root / "RELEASE-MANIFEST.json"
    if output.is_symlink() or output.exists():
        raise ValueError("Refusing to overwrite release manifest")
    with output.open("x", encoding="utf-8") as stream:
        json.dump(doc, stream, indent=2, sort_keys=True)
        stream.write("\n")
    return doc


def verify(args):
    path = Path(args.manifest)
    if path.is_symlink() or not path.is_file():
        raise ValueError("Untrusted manifest path")
    root = path.parent.resolve(strict=True)
    with path.open(encoding="utf-8") as stream:
        doc = json.load(stream)
    schema(doc)
    if args.platform is not None and doc["platform"] != args.platform:
        raise ValueError("Platform mismatch")
    if args.source_sha is not None and doc["source_sha"] != args.source_sha:
        raise ValueError("Source revision mismatch")
    if args.version is not None and doc["version"] != args.version:
        raise ValueError("Release version mismatch")
    for name, info in doc["packages"].items():
        path = artifact(root, info["filename"])
        if path.stat().st_size != info["size"] or sha256(path) != info["sha256"]:
            raise ValueError("Artifact failed checksum: " + name)
    return doc


def main():
    p = argparse.ArgumentParser(description=__doc__)
    commands = p.add_subparsers(dest="command", required=True)
    creation = commands.add_parser("create")
    creation.add_argument("--platform", choices=PLATFORMS, required=True)
    creation.add_argument("--version", required=True)
    creation.add_argument("--source-sha", required=True)
    creation.add_argument("--directory", required=True)
    creation.add_argument("--package", action="append", default=[])
    inspection = commands.add_parser("verify")
    inspection.add_argument("--manifest", required=True)
    inspection.add_argument("--platform", choices=PLATFORMS)
    inspection.add_argument("--source-sha")
    inspection.add_argument("--version")
    args = p.parse_args()
    try:
        result = create(args) if args.command == "create" else verify(args)
    except (ValueError, OSError, json.JSONDecodeError) as err:
        p.exit(1, "Release manifest rejected: " + str(err) + "\n")
    print("PASS: %s %s %s" % (result["platform"], result["version"], result["source_sha"]))


if __name__ == "__main__":
    main()

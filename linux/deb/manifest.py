#!/usr/bin/env python3
"""Generate complete, disjoint Debian package file manifests from a staging tree."""
from pathlib import Path
import sys

PACKAGES = ("liner-notes", "liner-notes-web", "liner-notes-link")
REQUIRED = {
    "liner-notes": ("usr/sbin/liner-notes", "usr/lib/systemd/system/liner-notes.service"),
    "liner-notes-web": ("usr/sbin/liner-notes-web", "usr/lib/systemd/system/liner-notes-web.service"),
    "liner-notes-link": ("usr/bin/liner-notes-link", "usr/lib/systemd/system/liner-notes-link.service"),
}


def owner(path):
    if path.startswith(("etc/liner-notes/web/", "usr/share/liner-notes/web/")) or path in (
        "usr/sbin/liner-notes-web", "usr/lib/systemd/system/liner-notes-web.service"
    ):
        return "liner-notes-web"
    if path in ("usr/bin/liner-notes-link", "usr/lib/systemd/system/liner-notes-link.service"):
        return "liner-notes-link"
    return "liner-notes"


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: manifest.py STAGED_ROOT")
    root = Path(sys.argv[1]).resolve(strict=True)
    if not root.is_dir():
        raise SystemExit("staging root must be a directory")
    payload = {name: [] for name in PACKAGES}
    for entry in sorted(root.rglob("*")):
        if entry.is_symlink():
            raise SystemExit(f"symlink in staging tree: {entry}")
        if not entry.is_file():
            continue
        rel = entry.relative_to(root).as_posix()
        payload[owner(rel)].append(rel)
    for name in PACKAGES:
        missing = set(REQUIRED[name]) - set(payload[name])
        if missing:
            raise SystemExit(f"{name} missing: {sorted(missing)}")
        (Path("debian") / f"{name}.install").write_text(
            "\n".join(payload[name]) + "\n", encoding="utf8"
        )


if __name__ == "__main__":
    main()

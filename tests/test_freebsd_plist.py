#!/usr/bin/env python3
"""Ensure the FreeBSD port plist enumerates all installed Markdown/Python files.

The port's do-install stages every top-level *.md and scripts/*.py. Before
running private FreeBSD CI, catch any newly added file missing from pkg-plist
using a GitHub-hosted PR check. No VM access or package installs.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PLIST = ROOT / "ports/comms/liner-notes/pkg-plist"


def main():
    content = PLIST.read_text(encoding="utf-8").splitlines()
    expected = {
        f"%%PORTDOCS%%%%DOCSDIR%%/{p.name}"
        for p in ROOT.glob("*.md") if p.is_file()
    }
    expected.update({
        f"%%DATADIR%%/scripts/{p.name}"
        for p in (ROOT / "scripts").glob("*.py") if p.is_file()
    })
    actual = set(content)
    missing = sorted(expected - actual)
    if missing:
        for entry in missing:
            print(f"UNLISTED FreeBSD stage file: {entry}", file=sys.stderr)
        raise SystemExit(1)
    print(f"PASS: pkg-plist covers {len(expected)} staged Markdown/Python files")


if __name__ == "__main__":
    main()

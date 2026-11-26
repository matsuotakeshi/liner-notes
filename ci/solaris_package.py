#!/usr/bin/env python3
"""Build and smoke-test a Solaris IPS package in a private alternate image."""
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import sys

from solaris_ci import ROOT, configure_build, run

PUBLISHER = "linernotes"
NAME = "application/liner-notes"
ARCHIVE = "liner-notes-solaris11.4-amd64.p5p"
RECEIPT = "SOLARIS-PACKAGE.json"


def write_manifest(stage, output, version, sha):
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)+", version) or not re.fullmatch(r"[a-f0-9]{40}", sha):
        raise ValueError("Invalid version or source commit")
    lines = [
        f"set name=pkg.fmri value=pkg://{PUBLISHER}/{NAME}@{version}",
        'set name=pkg.summary value="Liner Notes Bulletin Board System"',
        "set name=variant.arch value=i386",
        f"set name=linernotes.source-sha value={sha}",
    ]
    for source in sorted(stage.rglob("*")):
        path = source.relative_to(stage).as_posix()
        if source.is_symlink() or not re.fullmatch(r"[a-zA-Z0-9_./-]+", path):
            raise ValueError("Unsafe IPS payload path: " + path)
        if path not in {"opt", "opt/liner-notes"} and not path.startswith("opt/liner-notes/"):
            raise ValueError("Unexpected IPS payload location: " + path)
        if source.is_dir():
            if path != "opt":
                lines.append(f"dir path={path} owner=root group=bin mode=0755")
        elif source.is_file():
            mode = source.stat().st_mode & 0o777
            if mode not in (0o640, 0o644, 0o755):
                raise ValueError("Unexpected IPS payload file mode: " + path)
            lines.append(f"file {path} path={path} owner=root group=bin mode={mode:04o}")
        else:
            raise ValueError("Unexpected IPS payload file: " + path)
    output.write_text("\n".join(lines) + "\n", encoding="ascii")


def build():
    if platform.system() != "SunOS" or os.geteuid() == 0:
        raise ValueError("Use the non-root Solaris test account")
    from native_remote import verify_os
    verify_os("solaris")
    manifest_info = json.loads((ROOT / "docs/manual/manifest.json").read_text(encoding="utf8"))
    version = manifest_info["version"]
    sha = (ROOT / ".ci-source-sha").read_text(encoding="ascii").strip()
    work = ROOT / "build/solaris-package"
    work.mkdir(mode=0o700, parents=True)
    stage = work / "proto"
    image = work / "image"
    repo = work / "repo"
    manifest = work / "liner-notes.p5m"
    archive = work / ARCHIVE

    run(configure_build() + ["-j2", "all"])
    run([sys.executable, "scripts/install-solaris.py", "--destdir", str(stage), "--python", sys.executable])
    # Keep the live configuration tree completely outside package ownership.
    # IPS can salvage local files if it removes the directory containing them.
    defaults = stage / "opt/liner-notes/share/liner-notes/config"
    defaults.mkdir(mode=0o755)
    for sample in (stage / "etc/liner-notes").glob("*.sample"):
        shutil.move(sample, defaults / sample.name)
    shutil.rmtree(stage / "etc")
    write_manifest(stage, manifest, version, sha)
    run(["pkgrepo", "create", str(repo)])
    run(["pkgrepo", "-s", str(repo), "add-publisher", PUBLISHER])
    run(["pkgsend", "-s", str(repo), "publish", "-d", str(stage), str(manifest)])
    run(["pkgrecv", "-s", str(repo), "-a", "-d", str(archive), f"{NAME}@{version}"])
    run(["pkg", "image-create", "-U", "-p", f"{PUBLISHER}={archive.as_uri()}", str(image)])
    run(["pkg", "-R", str(image), "install", f"pkg://{PUBLISHER}/{NAME}@{version}"])

    installed = image / "opt/liner-notes"
    runtime = installed / "sbin/liner-notes"
    settings = image / "etc/liner-notes/solaris.json"
    if settings.parent.exists() or not (installed / "share/liner-notes/config/solaris.json.sample").is_file():
        raise ValueError("Found active configuration in the installed package")
    run(["/usr/sbin/svccfg", "validate", str(installed / "share/smf/liner-notes.xml")])
    run([sys.executable, "tests/test_runtime.py", str(runtime)])
    run([sys.executable, "tests/test_board.py", str(runtime)])
    run([sys.executable, str(installed / "share/liner-notes/scripts/maintenance.py"), "--help"])
    settings.parent.mkdir(parents=True)
    settings.write_text("local configuration", encoding="ascii")
    run(["pkg", "-R", str(image), "uninstall", f"pkg://{PUBLISHER}/{NAME}"])
    if runtime.exists() or settings.read_text(encoding="ascii") != "local configuration":
        raise ValueError("IPS uninstall did not preserve the expected package/configuration boundary")
    receipt = {"source_sha": sha, "version": version, "archive": ARCHIVE,
               "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
               "acceptance": "passed"}
    (work / RECEIPT).write_text(json.dumps(receipt, indent=2) + "\n", encoding="ascii")


if __name__ == "__main__":
    build()

#!/usr/bin/env python3
"""Stage Solaris programs and disabled SMF manifest without changing host services."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def install(destdir, prefix="/opt/liner-notes", python="/usr/bin/python3.11"):
    # A mandatory non-root staging area prevents implicit live deployment.
    requested = Path(destdir).absolute()
    # Inspect the original spelling before resolve() removes symlink ancestors.
    # In particular, /.. must not bypass the live-filesystem-root boundary.
    if not destdir or any(part.is_symlink() for part in (requested, *requested.parents)):
        raise ValueError("Use an explicit, non-root staging directory without symlink ancestors")
    stage = requested.resolve()
    if stage == Path(stage.anchor):
        raise ValueError("Use an explicit, non-root staging directory")
    for value in (prefix, python):
        if not re.fullmatch(r"/[A-Za-z0-9_./-]+", value) or ".." in Path(value).parts:
            raise ValueError("Use absolute target paths without whitespace or parent traversal")
    share = prefix + "/share/liner-notes"
    config = "/etc/liner-notes"

    def directory(path):
        if path.exists():
            if not path.is_dir() or path.is_symlink():
                raise ValueError("Directory destination is not a regular directory")
            return
        directory(path.parent)
        path.mkdir(mode=0o755)
        path.chmod(0o755)

    def put(path, data, mode=0o644, sample=False):
        out = stage / path.lstrip("/")
        for part in (out, *out.parents):
            if part.is_symlink():
                raise ValueError("Refusing symlink destination")
            if part == stage:
                break
        if sample:
            put(path + ".sample", data, mode)
            if out.exists():
                if not out.is_file():
                    raise ValueError("Configuration destination is not a regular file")
                return
        directory(out.parent)
        if out.exists() and not out.is_file():
            raise ValueError("Destination is not a regular file")
        fd, name = tempfile.mkstemp(prefix=".install-", dir=out.parent)
        try:
            with os.fdopen(fd, "wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            os.chmod(name, mode)
            os.replace(name, out)
        finally:
            if os.path.exists(name):
                os.unlink(name)

    for source, target in (("runtime", "sbin/liner-notes"), ("memberctl", "bin/liner-notes-memberctl"),
                           ("vaultctl", "bin/liner-notes-vaultctl"), ("dbconvert", "bin/liner-notes-dbconvert")):
        put(prefix + "/" + target, (ROOT / source).read_bytes(), 0o755)
    for source in (ROOT / "scripts").glob("*.py"):
        put(share + "/scripts/" + source.name, source.read_bytes(), 0o755 if source.name == "door_guess.py" else 0o644)
    put(share + "/scripts/bbs_transport.py", (ROOT / "webui/bbs_transport.py").read_bytes())
    put(share + "/scripts/check-resume.py", (ROOT / "ports/test-resume.py").read_bytes())
    put(share + "/scripts/door_clans.sh", (ROOT / "scripts/door_clans.sh").read_bytes(), 0o755)
    def wrapper(name, script, setup=""):
        text = "#!/bin/sh\n" + setup + "exec " + shlex.quote(python) + " -B " + shlex.quote(share + "/scripts/" + script) + ' "$@"\n'
        put(prefix + "/" + name, text.encode(), 0o755)
    wrapper("libexec/liner-notes-smf", "solaris_service.py")
    wrapper("bin/liner-notes-admin", "maintenance.py", "export LINER_NOTES_TOOLS_DIR=" + shlex.quote(prefix + "/bin") + "\n")
    wrapper("bin/liner-notes-check-resume", "check-resume.py")
    wrapper("bin/liner-notes-link", "link_service.py")
    if (ROOT / "scripts/doctor.py").exists():
        wrapper("bin/liner-notes-doctor", "doctor.py")
    for asset_directory in ("help", "menus"):
        for source in (ROOT / asset_directory).rglob("*.txt"):
            put(share + "/" + source.relative_to(ROOT).as_posix(), source.read_bytes(), sample=True)
    for name in ("banner", "sysinfo", "goodbye", "welcome", "systemnews", "birthday", "greeting-06", "greeting-12", "greeting-18"):
        put(share + "/" + name + ".txt", (ROOT / (name + ".txt")).read_bytes(), sample=True)
    for name in ("limits", "vault-limits", "doors"):
        data = (ROOT / (name + ".example.conf")).read_bytes()
        if name == "doors":
            data = data.replace(b"/usr/local/libexec/liner-notes/scripts", (share + "/scripts").encode())
            data = data.replace(b"/var/db/liner_notes/games", b"/var/opt/liner-notes/games")
        put(config + "/" + name + ".conf", data, sample=True)
    for name in ("link.toml", "link-accounts.conf"):
        source = "link.example.toml" if name.endswith("toml") else "link-accounts.example.conf"
        data = (ROOT / source).read_bytes()
        if name == "link.toml":
            data = data.replace(b"/var/db/liner-notes-link", b"/var/opt/liner-notes/link")
            data = data.replace(b"/var/run/liner-notes-link", b"/var/opt/liner-notes/link")
            data = data.replace(b"/usr/local/etc/liner-notes", config.encode())
            data = data.replace(b"port = 23\n", b"port = 2222\n")
        put(config + "/" + name, data, 0o640, sample=True)
    args = ["--telnet", "--menu", "--lang", "ja", "--bind", "127.0.0.1", "--timezone", "Asia/Tokyo",
            "--limits", config + "/limits.conf", "--menu-dir", share + "/menus", "--help-dir", share + "/help",
            "--banner", share + "/banner.txt", "--login-text-dir", share, "--sysinfo", share + "/sysinfo.txt",
            "--goodbye", share + "/goodbye.txt", "--database", "/var/opt/liner-notes/liner.sqlite",
            "--user-log", "/var/log/liner-notes/user.log", "--chat-log", "/var/log/liner-notes/chat.log",
            "2222", "/var/opt/liner-notes/notes.dat", "/var/opt/liner-notes/members.dat"]
    launch = {"program": prefix + "/sbin/liner-notes", "working_directory": "/var/opt/liner-notes", "arguments": args}
    put(config + "/solaris.json", (json.dumps(launch, indent=2) + "\n").encode(), 0o640, sample=True)
    xml = (ROOT / "solaris/liner-notes.xml").read_text(encoding="utf8").replace("@PREFIX@", prefix)
    put(prefix + "/share/smf/liner-notes.xml", xml.encode(), sample=True)
    manual = ROOT / "docs/manual"
    manifest = json.loads((manual / "manifest.json").read_text(encoding="utf8"))
    for page in manifest["pages"]:
        put(prefix + "/share/doc/liner-notes/manual/" + page["id"] + ".md", (manual / (page["id"] + ".md")).read_bytes())
    for name in ("LICENSE", "THIRD_PARTY.md"):
        put(share + "/" + name, (ROOT / name).read_bytes())
    print("Solaris staging complete. Accounts, services and data were not changed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destdir", required=True)
    parser.add_argument("--prefix", default="/opt/liner-notes")
    parser.add_argument("--python", default="/usr/bin/python3.11")
    args = parser.parse_args()
    try:
        install(args.destdir, args.prefix, args.python)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")

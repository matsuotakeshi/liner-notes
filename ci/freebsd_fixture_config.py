"""Public text defaults for rootless pkg tests; never execute @sample scripts."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_TEXT = re.compile(r"(?:menus/[a-z-]+|help/(?:ja|en)/[a-z]+|banner|sysinfo|goodbye|welcome|systemnews|birthday|greeting-[0-9]{2})\.txt")


def text_samples():
    paths = []
    for line in (ROOT / "ports/comms/liner-notes/pkg-plist").read_text(encoding="utf8").splitlines():
        prefix = "@sample %%DATADIR%%/"
        if not line.startswith(prefix):
            continue
        value = line[len(prefix):]
        if not value.endswith(".sample") or not PUBLIC_TEXT.fullmatch(value[:-7]):
            raise ValueError("Unexpected public package sample")
        paths.append(value[:-7])
    if len(paths) != len(set(paths)) or not {"menus/ja.txt", "menus/en.txt", "help/ja/index.txt", "help/en/index.txt"} <= set(paths):
        raise ValueError("Missing or duplicate required package text samples")
    return paths


def prepare_assets(root):
    root = Path(root).resolve(strict=True)
    assets = root / "usr/local/share/liner-notes"
    pending = []
    for name in text_samples():
        target = assets / name
        sample = target.with_name(target.name + ".sample")
        # Check every component, including intermediate parents, before reading
        # or creating anything. The source is the exact package's allowlist.
        for directory in sample.parents:
            if directory == root:
                break
            if directory.is_symlink() or not directory.is_dir() or not directory.resolve().is_relative_to(root):
                raise ValueError("Unsafe private fixture assets")
        if target.is_symlink() or sample.is_symlink() or not sample.is_file():
            raise ValueError("Unsafe or missing public package sample")
        if sample.stat().st_size > 32768 or sample.stat().st_nlink != 1:
            raise ValueError("Oversized or linked package sample")
        if target.exists():
            if not target.is_file() or target.stat().st_nlink != 1:
                raise ValueError("Unsafe existing private text")
            continue  # Existing operator edits survive update/recovery.
        pending.append((target, sample))
    for target, sample in pending:
        with target.open("xb") as stream:
            stream.write(sample.read_bytes())
    # Empty rc variables receive host defaults through :=. Even optional paths
    # must explicitly name the private fixture's assets.
    return {key: str(assets / name) for key, name in (
        ("liner_notes_banner", "banner.txt"),
        ("liner_notes_sysinfo", "sysinfo.txt"),
        ("liner_notes_goodbye", "goodbye.txt"))}

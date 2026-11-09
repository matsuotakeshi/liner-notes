"""Explicit defaults for rootless pkg tests with maintainer scripts disabled.

This does not verify @sample/package scripts; the separate VNET jail must do so.
Only the fresh private fixture is writable, never host configuration.
"""
from pathlib import Path


def prepare_assets(root):
    root = Path(root).resolve(strict=True)
    assets = root/'usr/local/share/liner-notes'
    menus = assets/'menus'
    for directory in (assets, menus):
        if directory.is_symlink() or not directory.is_dir() or not directory.resolve().is_relative_to(root):
            raise ValueError('Unsafe private fixture assets')
    for language in ('ja', 'en'):
        target = menus/(language+'.txt')
        sample = menus/(language+'.txt.sample')
        if target.is_symlink() or sample.is_symlink() or not sample.is_file():
            raise ValueError('Unsafe or missing package menu sample')
        if target.exists():
            if not target.is_file():
                raise ValueError('Unsafe existing private menu')
            continue  # Edited menus survive package update/recovery.
        with target.open('xb') as stream:
            stream.write(sample.read_bytes())
    # Empty rc variables receive installed absolute defaults via :=. Use
    # explicit private paths even for optional/missing documents.
    return {key: str(assets/name) for key, name in (
        ('liner_notes_banner', 'banner.txt'),
        ('liner_notes_sysinfo', 'sysinfo.txt'),
        ('liner_notes_goodbye', 'goodbye.txt'))}

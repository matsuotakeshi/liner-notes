"""Staged Linux installation preserves local assets and rejects symlinks."""
from pathlib import Path
import importlib.util,os,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('install_linux',ROOT/'scripts/install-linux.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
with tempfile.TemporaryDirectory(prefix='install-linux-') as tmp:
    base=Path(tmp);stage=base/'stage'
    installer.install(str(stage),'/usr/local',sys.executable)
    assets=[stage/'etc/liner-notes/liner-notes.env',stage/'usr/local/share/liner-notes/banner.txt',stage/'usr/local/share/liner-notes/help/ja/read.txt',stage/'usr/local/lib/systemd/system/liner-notes.service']
    for p in assets:p.write_text('local customization\n')
    installer.install(str(stage),'/usr/local',sys.executable)
    for p in assets:
        assert p.read_text()=='local customization\n'
        assert p.with_name(p.name+'.sample').read_text()!='local customization\n'
    unchanged=stage/'usr/local/share/liner-notes/help/ja/content.txt'
    unchanged.write_bytes(b'previous default\n')
    unchanged.with_name(unchanged.name+'.sample').write_bytes(b'previous default\n')
    installer.install(str(stage),'/usr/local',sys.executable)
    assert unchanged.read_bytes()==(ROOT/'help/ja/content.txt').read_bytes()
    assert assets[2].read_text()=='local customization\n'
    docs=stage/'usr/local/share/doc/liner-notes'
    assert (docs/'manual/boards.md').read_bytes()==(ROOT/'docs/manual/boards.md').read_bytes()
    assert (docs/'index.html').is_file() or (docs/'index.md').is_file()
    assert not list(docs.glob('STAGE*.md')) and not (docs/'development').exists()
    assert not (stage/'var').exists(),'Installer must not create or alter instance data'
    assert (stage/'usr/local/sbin/liner-notes').read_bytes()==(ROOT/'runtime').read_bytes()
    assert os.access(stage/'usr/local/share/liner-notes/scripts/door_guess.py',os.X_OK)
    assert os.access(stage/'usr/local/share/liner-notes/scripts/door_clans.sh',os.X_OK)
    wrapper=stage/'usr/local/bin/liner-notes-doctor'
    assert wrapper.is_file() and '--prefix /usr/local' in wrapper.read_text()
    assert (stage/'usr/local/share/liner-notes/scripts/doctor.py').read_bytes()==(ROOT/'scripts/doctor.py').read_bytes()
    assert not (stage/'usr/local/bin/liner-notes-ssh').exists(),'FreeBSD-specific Telnet wrapper must not be installed on Linux'
    for p in (stage/'usr/local/bin').glob('*'):
        assert os.access(p,os.X_OK),p
        if p.read_bytes().startswith(b'#!/bin/sh'):subprocess.run(['sh','-n',p],check=True)
    unsafe=base/'unsafe';unsafe.mkdir();(unsafe/'usr').symlink_to(base,target_is_directory=True)
    try:installer.install(str(unsafe),'/usr/local',sys.executable)
    except ValueError:pass
    else:raise AssertionError('Symlink destination accepted')
print('PASS Linux staged install, repeat updates preserve config/help/banner/unit, no data mutation, wrapper syntax, symlink rejection')

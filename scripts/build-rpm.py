#!/usr/bin/env python3
"""Build RPM/SRPM in a new workspace without installing or starting anything."""
import argparse
import os
from pathlib import Path
import subprocess
import tarfile
import io

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    top = a.output.absolute()
    top.mkdir(mode=0o700)  # Never reuse a buildroot containing someone else's data.
    for name in ('SOURCES', 'SPECS', 'BUILD', 'BUILDROOT', 'RPMS', 'SRPMS'):
        (top / name).mkdir()
    spec = (ROOT / 'linux/rpm/liner-notes.spec').read_text(encoding='utf8')
    version = next(line.split()[1] for line in spec.splitlines() if line.startswith('Version:'))
    stem = 'liner-notes-' + version
    manifest = ROOT / 'SOURCE-MANIFEST'
    if manifest.exists():
        tracked = manifest.read_text(encoding='utf8').splitlines()
    else:
        tracked = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT).decode().split('\0')
    included=[]
    with tarfile.open(top / 'SOURCES' / (stem + '.tar.gz'), 'w:gz') as archive:
        for name in sorted(set(tracked) - {'','SOURCE-MANIFEST'}):
            if Path(name).is_absolute() or '..' in Path(name).parts:
                raise ValueError('Unsafe source manifest entry')
            path = ROOT / name
            if path.is_symlink():
                raise ValueError('Unexpected source symlink: ' + name)
            if path.is_file():
                archive.add(path, arcname=stem + '/' + name, recursive=False)
                included.append(name)
        data=('\n'.join(included)+'\n').encode('utf8')
        info=tarfile.TarInfo(stem+'/SOURCE-MANIFEST');info.size=len(data);info.mode=0o644
        archive.addfile(info,io.BytesIO(data))
    (top / 'SPECS/liner-notes.spec').write_text(spec, encoding='utf8')
    subprocess.run(['rpmbuild', '-ba', '--define', '_topdir ' + str(top), str(top / 'SPECS/liner-notes.spec')], check=True)


if __name__ == '__main__':
    main()

"""Copy real ELF dependencies into a new rootless pkg fixture; never bypass pkg.

This is not a chroot or a host installation. pkg scans the fixture /lib and
/usr/lib for base providers, while third-party registrations come from its
private dependency DB. All copied files are independent, checked byte copies.
"""
import hashlib
import os
from pathlib import Path
import re
import stat
import struct
import subprocess

NAME = re.compile(r'lib[A-Za-z0-9_+.-]+\.so(?:\.[A-Za-z0-9_.-]+)?\Z')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def elf_dependencies(path, readelf):
    with path.open('rb') as stream:
        header = stream.read(64)
    require(len(header) == 64 and header[:6] == b'\x7fELF\x02\x01'
            and struct.unpack_from('<HH', header, 16) == (3, 62),
            'Expected an amd64 ELF shared library')
    result = subprocess.run([str(readelf), '-d', str(path)], capture_output=True,
                            text=True, timeout=30, check=True, env=dict(os.environ, LC_ALL='C'))
    require(not result.stderr and len(result.stdout) < 1024 * 1024, 'Unexpected ELF inspection diagnostics')
    sonames = re.findall(r'\(SONAME\)[^\n]*\[([^\]]+)\]', result.stdout)
    needed = re.findall(r'\(NEEDED\)[^\n]*\[([^\]]+)\]', result.stdout)
    require(len(sonames) == 1 and all(NAME.fullmatch(n) for n in sonames + needed),
            'Missing or unsafe library SONAME/NEEDED metadata')
    return sonames[0], needed


def seed_libraries(root, required, library_dirs=('/lib', '/usr/lib', '/usr/local/lib'),
                   readelf='/usr/bin/readelf'):
    require(isinstance(required, list) and 0 < len(required) <= 64
            and all(isinstance(n, str) and NAME.fullmatch(n) for n in required), 'Unsafe required library list')
    root = Path(root).resolve(strict=True)
    directories = [Path(p).absolute() for p in library_dirs]
    require(root.is_dir() and all(p.is_dir() and not p.is_symlink() for p in directories),
            'Library roots must be existing real directories')
    # A caller can supply fixture directories for hosted tests; production uses
    # exactly the three system paths above and preserves their relative layout.
    prefixes = ('lib', 'usr/lib', 'usr/local/lib')
    require(len(directories) == len(prefixes), 'Expected base and ports library directories')
    pending, records, total = list(required), {}, 0
    while pending:
        name = pending.pop(0)
        if name in records:
            continue
        require(len(records) < 128, 'ELF dependency closure exceeds budget')
        matches = [(i, directory/name) for i, directory in enumerate(directories)
                   if (directory/name).exists() or (directory/name).is_symlink()]
        require(matches, 'Required library has no real host provider: '+name)
        index, alias = matches[0]
        source = alias.resolve(strict=True)
        require(any(source.is_relative_to(p.resolve()) for p in directories), 'Library symlink escapes allowed roots')
        before = source.stat()
        require(stat.S_ISREG(before.st_mode) and not before.st_mode & 0o022
                and 0 < before.st_size <= 64 * 1024**2, 'Unsafe library file or size')
        soname, dependencies = elf_dependencies(source, readelf)
        require(soname == name, 'Library SONAME differs from the required provider')
        flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
        with os.fdopen(os.open(source, flags), 'rb') as stream:
            info = os.fstat(stream.fileno())
            require((info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns) ==
                    (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns), 'Library changed during inspection')
            content = stream.read(before.st_size + 1)
        after = source.stat()
        require(len(content) == before.st_size and alias.resolve(strict=True) == source
                and (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) ==
                    (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns), 'Library changed during copy')
        total += len(content)
        require(total <= 512 * 1024**2, 'Library copy budget exceeded')
        relative = Path(prefixes[index])/name
        destination = root/relative
        parent = root
        for part in relative.parent.parts:
            parent = parent/part
            parent.mkdir(exist_ok=True)
            require(parent.is_dir() and not parent.is_symlink() and parent.resolve().is_relative_to(root),
                    'Fixture library directory escapes its private root')
        with destination.open('xb') as stream:
            stream.write(content)
        destination.chmod(0o444)
        records[name] = {'source': str(source), 'path': str(relative), 'size': len(content),
                         'sha256': hashlib.sha256(content).hexdigest()}
        pending.extend(dependencies)
    verify_libraries(root, records)
    return records


def verify_libraries(root, records):
    for record in records.values():
        for path in (Path(record['source']), Path(root)/record['path']):
            require(path.is_file() and not path.is_symlink()
                    and path.stat().st_size == record['size']
                    and hashlib.sha256(path.read_bytes()).hexdigest() == record['sha256'],
                    'Host/fixture dependency bytes changed')

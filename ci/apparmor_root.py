#!/usr/bin/python3 -I
"""Exact-main, Debian test-host-only AppArmor file-boundary acceptance."""
import argparse
import errno
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import stat
import subprocess
import tempfile
import time
import urllib.request

REPO = 'matsuotakeshi/liner-notes'
BASE = Path('/var/lib/liner-notes-apparmor-ci')
SECURITY = Path('/sys/kernel/security/apparmor')
ENV = {'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'LC_ALL': 'C.UTF-8', 'HOME': '/root'}
BBS = 'var/lib/liner-notes/liner.sqlite'
KEY = 'etc/liner-notes/ssh_host_ed25519_key'
LINK_KEY = 'var/lib/liner-notes-link/identity/identity.key'
CASES = {
    'liner-notes': (BBS, ('r', LINK_KEY)),
    'liner-notes-web': ('etc/liner-notes/web/web.toml', ('r', BBS), ('r', KEY), ('r', LINK_KEY)),
    'liner-notes-link': ('var/lib/liner-notes-link/link.sqlite', ('r', BBS), ('w', BBS)),
    'liner-notes-admin': (BBS, ('r', LINK_KEY)),
    'liner-notes-transfer': ('var/lib/liner-notes/spool/payload', ('r', BBS)),
    'liner-notes-door': ('var/lib/liner-notes-door-slots/example/save', ('r', BBS), ('w', KEY)),
    'liner-notes-door-broker': ('etc/liner-notes/door-broker.json', ('r', BBS)),
    'liner-notes-door-launcher': ('etc/liner-notes/door-isolation.json', ('r', BBS)),
}
PROBE = r"""
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    char label[256] = {0};
    int fd = open("/proc/self/attr/current", O_RDONLY);
    if (fd < 0) return 3;
    ssize_t length = read(fd, label, sizeof(label) - 1);
    close(fd);
    if (length < 0) return 4;
    label[strcspn(label, "\n")] = 0;
    if (strcmp(label, argv[1])) return 5;
    fd = open(argv[3], !strcmp(argv[2], "w") ? O_WRONLY : O_RDONLY);
    if (fd < 0) {
        if (errno != EACCES) return 6;
        puts("DENIED");
    } else {
        close(fd);
        puts("ALLOWED");
    }
    return 0;
}
"""


def run(*args, timeout=30):
    return subprocess.run([str(a) for a in args], env=ENV, stdin=subprocess.DEVNULL,
                          check=True, capture_output=True, text=True, timeout=timeout).stdout


def get(url, limit):
    request = urllib.request.Request(url, headers={'User-Agent': 'liner-notes-apparmor-ci'})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError('Source response too large')
    return data.decode('utf8')


def head():
    return json.loads(get('https://api.github.com/repos/' + REPO +
                          '/git/ref/heads/main', 65536))['object']['sha']


def trusted_base():
    for path in reversed((BASE, *BASE.parents)):
        path.mkdir(exist_ok=True, mode=0o711)
        info = path.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError('Unsafe acceptance directory ancestry')


def assemble(source, names, root, probe):
    """Retarget only project-owned data paths and profile names to the test tree."""
    text = '\n'.join(re.sub(r'(?m)^(?:abi .*|include <tunables/global>|'
                            r'  include if exists <local/.*>)\n', '', content)
                     for content in source)
    text = re.sub(r'(?m)^(  (?:audit deny )?)(/(?:etc|var/lib|var/log|run)/liner-notes[^ ,]*)',
                  lambda m: m[1] + root.as_posix() + m[2], text)
    text = re.sub(r'(?m)(^profile |-> |peer=)(liner-notes[a-z-]*)(?=[ ,])',
                  lambda m: m[1] + names[m[2]], text)
    text = text.replace('  include <abstractions/base>\n',
                        '  include <abstractions/base>\n'
                        f'  {probe} mr,\n  /proc/[0-9]*/attr/current r,\n')
    return 'abi <abi/3.0>,\ninclude <tunables/global>\n' + text


def fixture(root, probe):
    os.chmod(root, 0o711)
    for allowed, *denied in CASES.values():
        for name in (allowed, *(p for _, p in denied)):
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
            target.write_text('synthetic acceptance data\n', encoding='ascii')
            target.chmod(0o666)
    code = root / 'probe.c'
    code.write_text(PROBE, encoding='ascii')
    run('/usr/bin/clang', '-Wall', '-Wextra', '-Werror', str(code), '-o', probe)
    probe.chmod(0o755)


def probe_run(profile, unit, probe, operation, target, result):
    opts = ['--property=User=nobody', '--property=NoNewPrivileges=yes',
            '--property=RuntimeMaxSec=10']
    if profile:
        opts += ['--property=AssertSecurity=apparmor', '--property=AppArmorProfile=' + profile]
    label = profile + ' (enforce)' if profile else 'unconfined'
    stdout = run('/usr/bin/systemd-run', '--quiet', '--wait', '--collect', '--pipe',
                 '--unit=' + unit, *opts, '--', probe, label, operation, target)
    if stdout.strip() != result:
        raise ValueError(f'{unit} {operation}: expected {result}, got {stdout.strip()!r}')


def ensure_no_processes(names):
    for entry in Path('/proc').iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            label = (entry / 'attr/apparmor/current').read_text().split(' ', 1)[0]
        except FileNotFoundError:
            continue
        except OSError as error:
            # Some kernel tasks have no LSM context.
            if error.errno == errno.EINVAL:
                continue
            raise
        if label in names:
            raise RuntimeError('A fixture process still owns confinement; retaining loaded policy')


def main():
    import fcntl

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sha')
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.sha):
        parser.error('A full lowercase SHA is required')
    os.environ.clear()
    os.environ.update(ENV)
    os.umask(0o022)
    if (os.geteuid() or '10.0.0.59' not in run('/usr/bin/hostname', '-I').split()
            or not re.search(r'^ID=debian$', Path('/etc/os-release').read_text(), re.M)
            or not re.search(r'^VERSION_ID="?13"?$', Path('/etc/os-release').read_text(), re.M)):
        raise ValueError('Designated Debian 13 acceptance host required')
    if Path('/sys/module/apparmor/parameters/enabled').read_text().strip() != 'Y':
        raise ValueError('Existing enabled AppArmor kernel required')
    if args.sha != head():
        raise ValueError('Expected the current reviewed main SHA')
    def stop(signum, _frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    trusted_base()
    with (BASE/'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        root = Path(tempfile.mkdtemp(prefix='ln-aa-', dir=BASE))
        # Only remove the isolated tree after successful policy teardown.
        accept(root, args.sha)
        shutil.rmtree(root)


def accept(root, sha):
    prefix = 'ln-aa-ci-' + secrets.token_hex(6)
    names = {role: prefix + '-' + role for role in CASES}
    if any(name in (SECURITY/'profiles').read_text() for name in names.values()):
        raise ValueError('Fixture profile already loaded')
    base = 'https://raw.githubusercontent.com/' + REPO + '/' + sha + '/linux/apparmor/'
    source = [get(base + 'profiles/' + name, 16384) for name in CASES]
    abstraction = root/'abstractions'
    abstraction.mkdir(mode=0o755)
    (abstraction/'liner-notes-python').write_text(get(base + 'abstractions/liner-notes-python', 16384))
    probe = root/'probe'
    fixture(root, probe)
    for i, (operation, path) in enumerate(sorted({
        ('r', allowed) for allowed, *_ in CASES.values()
    } | {entry for _, *denied in CASES.values() for entry in denied})):
        probe_run(None, prefix + '-dac-' + str(i), probe, operation, root/path, 'ALLOWED')
    policy = root/'policy'
    policy.write_text(assemble(source, names, root, probe), encoding='utf8')
    loader = ('/usr/sbin/apparmor_parser', '-K', '-I', root, '-I', '/etc/apparmor.d')
    run(*loader, '-Q', policy)
    if sha != head():
        raise ValueError('main advanced before policy load')
    since = int(time.time())
    try:
        run(*loader, '-a', policy)
        loaded = set((SECURITY/'profiles').read_text().splitlines())
        for role, (allowed, *denied) in CASES.items():
            name = names[role]
            if name + ' (enforce)' not in loaded:
                raise ValueError('Missing Enforce label for ' + name)
            probe_run(name, name + '-read', probe, 'r', root/allowed, 'ALLOWED')
            for i, (operation, path) in enumerate(denied):
                probe_run(name, name + '-deny-' + str(i), probe, operation, root/path, 'DENIED')
        journal = run('/usr/bin/journalctl', '-k', '--since=@' + str(since), '--no-pager', '-o', 'cat')
        for role, (_, *denied) in CASES.items():
            for mode, path in denied:
                if not any('apparmor="DENIED"' in line and f'profile="{names[role]}"' in line
                           and f'name="{root/path}"' in line
                           and re.search(r'denied_mask="[^"]*' + mode, line)
                           for line in journal.splitlines()):
                    raise ValueError('Missing kernel AppArmor denial evidence for ' + role)
        features = SECURITY/'features'
        print(json.dumps({'source_sha': sha, 'apparmor_file_boundaries_verified': True,
                          'roles': sorted(CASES), 'kernel': os.uname().release,
                          'lsm': Path('/sys/kernel/security/lsm').read_text().strip(),
                          'unix_feature_advertised': (features/'network/af_unix').exists(),
                          'network_feature_advertised': (features/'network/af_mask').exists(),
                          'parser': run('/usr/sbin/apparmor_parser', '--version').splitlines()[0]}))
    finally:
        ensure_no_processes(set(names.values()))
        loaded = (SECURITY/'profiles').read_text()
        if any(name in loaded for name in names.values()):
            run(*loader, '-R', policy)


if __name__ == '__main__':
    main()

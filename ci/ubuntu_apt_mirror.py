"""Use an alternate Ubuntu mirror on disposable GitHub-hosted amd64 runners."""
import os
from pathlib import Path
import platform
import re

# Listed by Ubuntu Japanese Team; HTTPS package archive, not an ISO mirror.
MIRROR = 'https://ftp.jaist.ac.jp/pub/Linux/ubuntu/'
SOURCES = Path('/etc/apt/sources.list.d/ubuntu.sources')
KNOWN = {
    'mirror+file:/etc/apt/apt-mirrors.txt',
    'mirror+file:/etc/apt/apt-security-mirrors.txt',
    MIRROR.rstrip('/'),
    'https://mirrors.edge.kernel.org/ubuntu',
}


def rewrite_sources(text):
    """Change only recognized Ubuntu URI fields; keep suites and trust intact."""
    count = 0

    def replace(match):
        nonlocal count
        uris = match.group(1).split()
        if not uris:
            raise ValueError('Empty Ubuntu URI field')
        for uri in uris:
            uri = uri.rstrip('/')
            if uri not in KNOWN and not re.fullmatch(
                    r'https?://(?:[a-z0-9.-]+\.)?(?:archive|security)\.ubuntu\.com/ubuntu', uri):
                raise ValueError('Unrecognized Ubuntu source; leaving configuration unchanged')
        count += 1
        return 'URIs: ' + MIRROR

    result = re.sub(r'^URIs:[ \t]*(.*)$', replace, text, flags=re.MULTILINE)
    if not count:
        raise ValueError('No Ubuntu URI fields found')
    return result


def check_environment(env, release, machine):
    if (env.get('GITHUB_ACTIONS') != 'true' or
            env.get('RUNNER_ENVIRONMENT') != 'github-hosted' or
            release.get('ID') != 'ubuntu' or release.get('VERSION_ID') != '24.04' or
            machine != 'x86_64'):
        raise ValueError('Alternate mirror is restricted to GitHub-hosted Ubuntu 24.04 amd64')


def main():
    check_environment(os.environ, platform.freedesktop_os_release(), platform.machine())
    if os.geteuid() != 0:
        raise ValueError('Root is required to update the disposable runner sources')
    original = SOURCES.read_text(encoding='utf-8')
    updated = rewrite_sources(original)  # Validate every URI before writing anything.
    backup = SOURCES.with_name('ubuntu.sources.liner-notes-original')
    if not backup.exists():
        backup.write_text(original, encoding='utf-8')
    SOURCES.write_text(updated, encoding='utf-8')
    print('Ubuntu APT mirror: ' + MIRROR, flush=True)


if __name__ == '__main__':
    main()

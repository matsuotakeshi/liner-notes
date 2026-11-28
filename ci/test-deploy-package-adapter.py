#!/usr/bin/env python3
"""Exercise actual offline CD package commands in the existing disposable CI."""
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from deploy_policy import package_commands, require
from deploy_host import preflight_debian_upgrade

require(os.geteuid() == 0 and Path('/.dockerenv').exists() and Path.cwd() == Path('/build/liner-notes'), 'Disposable CI container required')
platform, *files = sys.argv[1:]
require(platform in {'rocky10', 'debian13'}, 'Linux container target required')
require(os.environ.get('CI_DISPOSABLE_ROCKY' if platform == 'rocky10' else 'CI_DISPOSABLE_DEBIAN') == '1', 'Disposable CI marker required')
packages = {}
staging = Path('/tmp/liner-notes-deploy-package-fixture')
staging.mkdir(mode=0o700)
for filename in files:
    path = Path(filename)
    if platform == 'rocky10':
        name = subprocess.check_output(['/usr/bin/rpm', '-qp', '--qf', '%{NAME}', str(path)], text=True)
    else:
        name = subprocess.check_output(['/usr/bin/dpkg-deb', '-f', str(path), 'Package'], text=True).strip()
        version = subprocess.check_output(['/usr/bin/dpkg-deb', '-f', str(path), 'Version'], text=True).strip()
    require(name not in packages, 'Duplicate package')
    (staging / path.name).symlink_to(path)
    packages[name] = {'filename': path.name}
    if platform == 'debian13':
        packages[name]['version'] = version
if platform == 'debian13':
    preflight_debian_upgrade(staging, packages)
# Production CD still admits the three standard packages. This disposable
# transaction also verifies co-updating the exact-version optional policy DEB.
optional = packages.pop('liner-notes-apparmor', None) if platform == 'debian13' else None
for argv in package_commands(platform, packages, staging):
    if optional:
        argv.append(str(staging / optional['filename']))
    subprocess.run(argv, check=True)

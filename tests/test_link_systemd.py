#!/usr/bin/env python3
"""Exercise the shipped unit in a disposable GitHub-hosted Linux runner."""
import json
import os
from pathlib import Path
import pwd
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
from link_transport import generate


def command(*args):
    return subprocess.check_output(args, text=True, timeout=90).strip()


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def run():
    if (os.geteuid() != 0 or os.environ.get('GITHUB_ACTIONS') != 'true'
            or os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted'):
        raise SystemExit('This test requires the disposable hosted runner')

    account = pwd.getpwnam('nobody')
    with tempfile.TemporaryDirectory(prefix='ln-link-ci-', dir='/var/lib') as tmp:
        directory = Path(tmp)
        directory.chmod(0o755)
        state = directory/'state'
        state.mkdir(mode=0o700)
        code = directory/'scripts'
        shutil.copytree(ROOT/'scripts', code)
        shutil.copyfile(ROOT/'webui/bbs_transport.py', code/'bbs_transport.py')

        identity = state/'identity'
        own = generate(identity, 'Hosted CI node')
        peer = state/'peer'
        generate(peer, 'Hosted CI offline peer')
        secret = state/'password'
        secret.write_text('disposable-unregistered-link-member\n')
        secret.chmod(0o600)
        cfg = directory/'link.toml'
        identifier = '1'*64
        unit = directory.name + '.service'
        runtime = '/run/' + directory.name
        socket_path = Path(runtime)/'control.sock'
        cfg.write_text(f'''[node]
identity = "{identity}"
database = "{state/'events.sqlite'}"
port = {port()}
control_socket = "{socket_path}"
[bbs]
port = {port()}
login = "fixturelink"
password_file = "{secret}"
[[boards]]
id = "{identifier}"
local = 1
[[origins]]
certificate = "{peer/'identity.crt'}"
boards = ["{identifier}"]
[[peers]]
certificate = "{peer/'identity.crt'}"
boards = ["{identifier}"]
host = "127.0.0.1"
port = {port()}
''')
        for entry in [state, *state.rglob('*')]:
            os.chown(entry, account.pw_uid, account.pw_gid)

        # Exercise the packaged unit directly with fixture-only paths and NSS
        # identities. Keep its hardening, dependency and restart settings.
        text = (ROOT/'linux/liner-notes-link.service').read_text()
        text = text.replace('User=liner_notes_link', 'User=nobody')
        text = text.replace('Group=liner_notes_linkctl', f'Group={account.pw_gid}')
        text = text.replace('/usr/bin/liner-notes-link', f'{sys.executable} -B {code/"link_service.py"}')
        text = text.replace('/etc/liner-notes/link.toml', str(cfg))
        text = text.replace('RuntimeDirectory=liner-notes-link', 'RuntimeDirectory='+directory.name)
        text = text.replace('/var/lib/liner-notes-link /run/liner-notes-link', f'{state} {runtime}')
        config = Path('/run/systemd/system')/unit

        def ready(previous=None):
            end = time.monotonic() + 30
            while time.monotonic() < end:
                pid = int(command('systemctl', 'show', unit, '-p', 'MainPID', '--value'))
                if pid and pid != previous and socket_path.is_socket():
                    with socket.socket(socket.AF_UNIX) as client:
                        client.settimeout(2)
                        try:
                            client.connect(str(socket_path))
                        except (FileNotFoundError, ConnectionRefusedError):
                            continue
                        client.sendall(b'{"action":"status"}\n')
                        with client.makefile('rb') as stream:
                            reply = json.loads(stream.readline())
                    assert reply['node'] == own
                    assert Path(f'/proc/{pid}').stat().st_uid == account.pw_uid
                    return pid
                time.sleep(.2)
            raise AssertionError('Hosted systemd link fixture did not start')

        try:
            with config.open('x') as stream:
                stream.write(text)
            command('systemctl', 'daemon-reload')
            command('systemd-analyze', 'verify', config)
            assert command('systemctl', 'show', unit, '-p', 'UnitFileState', '--value') == 'disabled'
            assert command('systemctl', 'show', unit, '-p', 'ActiveState', '--value') == 'inactive'
            command('systemctl', 'start', unit)
            first = ready()
            command('systemctl', 'start', unit)
            assert ready() == first
            command('systemctl', 'restart', unit)
            second = ready(first)
            os.kill(second, 9)
            ready(second)
            command('systemctl', 'stop', unit)
            assert command('systemctl', 'show', unit, '-p', 'ActiveState', '--value') == 'inactive'
            assert command('systemctl', 'show', unit, '-p', 'MainPID', '--value') == '0'
            assert not socket_path.exists()
            assert (state/'events.sqlite').is_file()
            print('PASS packaged systemd link unit syntax, opt-in lifecycle, crash restart, dedicated account and node identity')
        except Exception:
            print(command('journalctl', '--no-pager', '-u', unit, '-n', '100'), file=sys.stderr)
            raise
        finally:
            if config.exists():
                command('systemctl', 'stop', unit)
                config.unlink()
                command('systemctl', 'daemon-reload')


if __name__ == '__main__':
    run()

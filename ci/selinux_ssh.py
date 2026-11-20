#!/usr/bin/env python3
"""SSH Enforcing fixture. Client checks are also run in Hosted CI without MAC."""
import argparse
import hashlib
import os
import re
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests')]


def check_bootstrap_ssh_audit(errors):
    """Require exactly the Hosted probe's failed login and three valid logins.

    Slot reuse is scheduling-dependent; the event sequence and complete line
    grammar are not. Never accept arbitrary stderr or print a malformed line
    that might contain credentials.
    """
    expected = ['rejected', 'accepted', 'accepted', 'accepted']
    lines = errors.splitlines()
    if len(lines) != len(expected):
        raise AssertionError('SSH audit event count mismatch')
    statuses = []
    for line in lines:
        match = re.fullmatch(r'SSH auth (accepted|rejected) slot=(0|[1-9][0-9]*)', line)
        if match is None:
            raise AssertionError('Malformed or unexpected SSH stderr')
        statuses.append(match.group(1))
    if statuses != expected:
        raise AssertionError('SSH audit event sequence mismatch')


def prepare_client():
    # No system Python/packages are modified. Run only inside the helper's new,
    # root-owned source copy after exact-main admission, never from a PR on LAN.
    env = ROOT.parent / 'ssh-client-venv'
    if env.exists():
        raise ValueError('Refuse to reuse an old SSH client environment')
    subprocess.run([sys.executable, '-m', 'venv', str(env)], check=True, timeout=60)
    python = env / 'bin/python'
    subprocess.run([str(python), '-m', 'pip', 'install', '--disable-pip-version-check', '--no-cache-dir',
                    '-r', str(ROOT / 'tests/ssh-requirements.txt')], check=True, timeout=300)
    return python


def key_fixture(fixture):
    directory = fixture.directory('ssh-host-keys', 'liner_notes_ssh_secret_t')
    # Root-generated fixture keys, never installed or reused as production keys.
    host, client = directory / 'host', fixture.path / 'ssh-client'
    for key in (host, client):
        subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(key)], check=True, timeout=15)
    host.chmod(0o600)
    os.chown(host, fixture.account.pw_uid, fixture.account.pw_gid)
    fixture.label(directory, 'liner_notes_ssh_secret_t', True)
    import base64
    blob = base64.b64decode(client.with_suffix('.pub').read_text().split()[1])
    keys = fixture.text('ssh-members', '1 ' + hashlib.sha256(blob).hexdigest() + '\n', 'liner_notes_etc_t')
    keys.chmod(0o600)
    os.chown(keys, fixture.account.pw_uid, fixture.account.pw_gid)
    # World-readable canary proves MAC denial independently of Unix mode 0600.
    canary = directory / 'mac-canary'
    canary.write_text('synthetic fixture, not key material', encoding='ascii')
    canary.chmod(0o644)
    directory.chmod(0o755)
    fixture.label(canary, 'liner_notes_ssh_secret_t')
    port = fixture.port('liner_notes_ssh_port_t')
    return dict(host=host, client=client, keys=keys, canary=canary, port=port)


def probe(port, host, private, *, door=False):
    import paramiko
    from ssh_screen import Screen, MAIL_PROMPT
    expected = Path(str(host) + '.pub').read_text().split()[1]
    key = paramiko.Ed25519Key.from_private_key_file(str(private))

    def connect(password=None, public=None, shell=True):
        transport = paramiko.Transport(('127.0.0.1', port))
        try:
            transport.start_client(timeout=10)
            assert transport.get_remote_server_key().get_base64() == expected
            if public is not None:
                transport.auth_publickey('alice', public)
            else:
                transport.auth_password('alice', password)
            assert transport.is_authenticated()
            if not shell:
                return transport, None
            channel = transport.open_session(timeout=10)
            channel.get_pty(term='xterm', width=80, height=24)
            channel.invoke_shell()
            screen = Screen(channel)
            screen.until()
            return transport, screen
        except BaseException:
            transport.close()
            raise

    denied = False
    try:
        unexpected, _ = connect(password='incorrect-fixture-password')
    except paramiko.AuthenticationException:
        denied = True
    else:
        unexpected.close()
    assert denied, 'Incorrect password was accepted'
    time.sleep(1.1)
    for credentials in ({'password': 'selinux-only-fixture-password'}, {'public': key}):
        transport, screen = connect(**credentials)
        try:
            assert b'PONG' in screen.cmd('PING')
            assert '日本語'.encode() in screen.cmd('ECHO 日本語')
            assert b'Persisted under Enforcing' in screen.cmd('READ 1')
            # Alice's sent-mail view is independent of Bob's unread state.
            assert b'Private mail under Enforcing' in screen.cmd('MAIL READ 1', MAIL_PROMPT)
            screen.cmd('0')
            channel = None
            try:
                channel = transport.open_session(timeout=10)
                channel.exec_command('id')
            except (paramiko.SSHException, EOFError, OSError):
                pass
            else:
                raise AssertionError('Extra SSH channel/exec was accepted')
            finally:
                if channel is not None:
                    channel.close()
        finally:
            transport.close()
        time.sleep(1.1)
    transport, _ = connect(password='selinux-only-fixture-password', shell=False)
    try:
        channel = transport.open_session(timeout=10)
        try:
            channel.exec_command('id')
        except (paramiko.SSHException, EOFError, OSError):
            pass
        else:
            raise AssertionError('First-channel SSH exec was accepted')
    finally:
        transport.close()
    time.sleep(1.1)
    if door:
        for keypress in ('q', 'x', None):
            transport, screen = connect(password='selinux-only-fixture-password')
            try:
                result = screen.cmd('DOOR 1', b'KEY> ')
                assert b'DENIED' in result, 'Door must reach its MAC rejection probe'
                import re
                from test_door_resize import gone
                pid = int(re.search(rb'CONFINED (\d+) DENIED', result)[1])
                if keypress is not None:
                    assert b'Back to BBS' in screen.cmd(keypress)
            finally:
                transport.close()
            gone(pid)
            time.sleep(1.1)
    print('PASS SSH password/key, host pinning, article/mail, UTF-8 and exec denial', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--door', action='store_true')
    args = parser.parse_args()
    probe(args.port, args.host, args.client, door=args.door)

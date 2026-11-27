"""Real SSH v2 + simultaneous Telnet, against the ordinary shared BBS worker.

Run in existing Hosted Linux CI with libssh enabled. No LAN or production access.
"""
from pathlib import Path
import hashlib
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
import paramiko
from test_board import Server
from test_runtime import Client as Raw
from test_members import auth, PASSWORD
from test_telnet import Client as Telnet
from ssh_screen import Screen, MAIL_PROMPT
from ssh_openssh import pty_ping


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def reject(fn):
    try:
        fn()
    except (paramiko.SSHException, EOFError, OSError):
        return
    raise AssertionError('forbidden SSH request succeeded')


def closed(transport, reason, seconds=6):
    deadline = time.monotonic() + seconds
    while transport.is_active() and time.monotonic() < deadline:
        time.sleep(.03)
    assert not transport.is_active(), reason


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='ln-ssh-') as tmp:
        root = Path(tmp)
        host = root/'host'
        subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(host)], check=True)
        key = paramiko.RSAKey.generate(2048)
        wrong_key = paramiko.RSAKey.generate(2048)
        private = root/'client-key'
        key.write_private_key_file(str(private)); private.chmod(0o600)
        keys = root/'member-keys'
        fingerprint = hashlib.sha256(key.asbytes()).hexdigest()
        keys.write_text(f'1 {fingerprint}\n'); keys.chmod(0o600)
        admins = root/'admins'; admins.write_text('1\n')
        game = root/'game'
        game.write_text('#!'+sys.executable+'''\nimport os,sys
print('SSH_DOOR_USER='+os.environ['LINER_USER'],flush=True)
print('SSH_DOOR_PID='+str(os.getpid()),flush=True)
print('DOOR> ',end='',flush=True)
for line in sys.stdin:
    if line.strip()=='quit':break
    if line.strip()=='crash':os._exit(17)
    w,h=os.get_terminal_size(0)
    print('PTY %d %d'%(w,h),flush=True)
    print('DOOR> ',end='',flush=True)
'''); game.chmod(0o700)
        doors = root/'doors'
        doors.write_text(f'1|0|30|{game}|{root}|SSH PTY test\n2|0|1|{game}|{root}|Timeout test\n')
        seed = Server(binary, root/'notes', members=root/'members')
        for name in ('alice', 'bob'):
            c = Raw(seed.port); auth(c, name, True); c.close()
        seed.close()
        ssh_port = port()
        options = ['--ssh-port', str(ssh_port), '--ssh-host-key', str(host),
                   '--ssh-member-keys', str(keys), '--ssh-timeout', '3',
                   '--ssh-attempts', '4', '--doors', str(doors),
                   '--ui-policy', 'MEMBERS', '--ui-allow', 'rtbbs,mmm,mash,ktbbs']
        server = Server(binary, root/'notes', members=root/'members', admins=admins,
                        telnet=True, menu=True, language='en', options=options)
        transports = []

        def connect(name='alice', password=PASSWORD, public=None):
            t = paramiko.Transport(('127.0.0.1', ssh_port)); transports.append(t)
            t.start_client(timeout=10)
            # Verify the dedicated host key, never trust arbitrary remote hosts.
            expected = (host.with_suffix('.pub')).read_text().split()[1]
            assert t.get_remote_server_key().get_base64() == expected
            if public is not None:
                t.auth_publickey(name, public)
            else:
                t.auth_password(name, password)
            assert t.is_authenticated()
            return t

        def screen(t, marker=b'] > '):
            ch = t.open_session(timeout=10); ch.get_pty(term='xterm', width=80, height=24); ch.invoke_shell()
            s = Screen(ch); s.until(marker); return s

        telnet = Telnet(server.port)
        try:
            telnet.until()
            reject(lambda: connect(password='wrong-password'))
            reject(lambda: connect(name='missing'))
            reject(lambda: connect(public=wrong_key))
            reject(lambda: connect(name='bob', public=key))
            a = screen(connect()); b = screen(connect('bob')); k = screen(connect(public=key))
            assert b'PONG' in a.cmd('PING')
            assert b'PONG' in b.cmd('PING')
            assert b'PONG' in telnet.cmd('PING')
            assert '日本語'.encode() in a.cmd('ECHO 日本語')
            assert b'alice' in k.cmd('ACCOUNT')
            a.cmd('POST SSH article', b'Body > ')
            a.cmd('SSH article body', b'Body > ')
            assert b'Saved article #1' in a.cmd('.')
            assert b'SSH article body' in a.cmd('READ 1')
            a.cmd('RAW', b'RAW\r\n')
            a.cmd('MAIL SEND bob ssh-mail', b'MAIL EDIT ')
            a.channel.sendall(b'private SSH mail\r.\r')
            a.until(b'MAIL SENT ')
            # Sending/reading mail retains the mail menu. Leave it before
            # restoring the main UI or starting a Door on either connection.
            a.cmd('0', b'MAIL CLOSED\r\n')
            a.cmd('MENU')
            # An unsolicited arrival notice redraws the MAIN prompt. It is
            # not the reply to MAIL READ: wait for the distinct mail prompt.
            mail = b.cmd('MAIL READ 1', MAIL_PROMPT)
            assert b'Mail 1  From:alice To:bob ' in mail, mail[-1500:]
            assert b'Subject: ssh-mail\r\n' in mail, mail[-1500:]
            assert b'private SSH mail' in mail, mail[-1500:]
            assert b'End mail\r\n' in mail, mail[-1500:]
            b.cmd('0')
            assert b'PONG' in a.cmd('PING')
            assert b'PONG' in b.cmd('PING')
            print('PASS SSH password/key account binding, UTF-8 and Telnet coexistence')

            # Restrictions apply after valid auth, not merely to anonymous clients.
            for request in ('exec', 'sftp', 'scp', 'x11', 'no-pty-shell', 'zero-pty', 'oversize-pty'):
                t = connect(); ch = t.open_session(timeout=10)
                if request == 'exec': reject(lambda: ch.exec_command('id'))
                elif request == 'sftp': reject(lambda: ch.invoke_subsystem('sftp'))
                elif request == 'scp': reject(lambda: ch.exec_command('scp -t /tmp/ln-forbidden'))
                elif request == 'x11': reject(lambda: ch.request_x11(auth_cookie='00'*16))
                elif request == 'zero-pty': reject(lambda: ch.get_pty(width=0, height=24))
                elif request == 'oversize-pty': reject(lambda: ch.get_pty(width=65536, height=24))
                else: reject(ch.invoke_shell)
                t.close()
            t = connect()
            reject(lambda: t.open_channel('direct-tcpip', ('127.0.0.1', server.port), ('127.0.0.1', 1234)))
            reject(lambda: t.request_port_forward('127.0.0.1', 0))
            s = screen(t)
            reject(lambda: t.open_session(timeout=5))
            assert t.global_request('tcpip-forward', ('127.0.0.1', 0), wait=True) is None
            print('PASS exec, subsystem, forwarding, extra channel and shell-without-PTY denied')
            # Auth success alone must not remove the bounded shell-start deadline.
            for phase in ('authenticated', 'channel', 'pty'):
                idle = connect()
                if phase != 'authenticated':
                    idle_channel = idle.open_session(timeout=10)
                    if phase == 'pty': idle_channel.get_pty(width=80, height=24)
                closed(idle, 'SSH startup deadline bypassed at '+phase)
                assert b'PONG' in telnet.cmd('PING')
            print('PASS authenticated/channel/PTY startup deadlines; Telnet remains responsive')

            # Actual OpenSSH client interoperability and no command execution.
            common = ['ssh', '-F', '/dev/null', '-p', str(ssh_port), '-i', str(private),
                      '-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes',
                      '-o', 'StrictHostKeyChecking=yes', '-o', f'UserKnownHostsFile={root/"known_hosts"}']
            (root/'known_hosts').write_text(f'[127.0.0.1]:{ssh_port} '+host.with_suffix('.pub').read_text())
            result = subprocess.run(common+['alice@127.0.0.1', 'id'], capture_output=True, timeout=12)
            exec_diagnostics = {'returncode': result.returncode, 'stderr': result.stderr[-4096:],
                                'stdout': result.stdout[-1500:]}
            print('OpenSSH exec-denial diagnostics:', exec_diagnostics, flush=True)
            assert result.returncode != 0 and b'uid=' not in result.stdout, exec_diagnostics
            pty_ping(common+['-tt', 'alice@127.0.0.1'])
            print('PASS OpenSSH via 80x24 PTY: BBS prompt and PING')

            # PTY is supplied by existing Door machinery, including live resizing.
            output = a.cmd('DOOR 1', b'DOOR> ')
            assert b'SSH_DOOR_USER=alice' in output
            a.channel.resize_pty(width=100, height=30)
            assert b'PTY 100 30' in a.cmd('size', b'DOOR> ')
            assert b'Back to BBS' in a.cmd('quit')
            a.cmd('DOOR 1', b'DOOR> ')
            assert b'Back to BBS' in a.cmd('crash')
            a.cmd('DOOR 2', b'DOOR> '); assert b'Back to BBS' in a.until()
            output = b.cmd('DOOR 1', b'DOOR> ')
            pid = int(re.search(rb'SSH_DOOR_PID=(\d+)', output)[1]); b.channel.get_transport().close()
            for _ in range(50):
                try: os.kill(pid, 0)
                except ProcessLookupError: break
                time.sleep(.1)
            else: raise AssertionError('Door survived SSH disconnect')
            print('PASS Door return, crash, timeout, resize and disconnect cleanup')

            # Core revocation must stop an already authenticated SSH Door too,
            # not merely deny the next password attempt.
            victim = screen(connect('bob'))
            output = victim.cmd('DOOR 1', b'DOOR> ')
            revoked_pid = int(re.search(rb'SSH_DOOR_PID=(\d+)', output)[1])
            a.cmd('SUSPEND 2')
            closed(victim.channel.get_transport(), 'suspended SSH Door stayed connected')
            for _ in range(50):
                try: os.kill(revoked_pid, 0)
                except ProcessLookupError: break
                time.sleep(.1)
            else: raise AssertionError('Door survived account suspension')
            reject(lambda: connect('bob'))
            a.cmd('RESUME 2')
            assert b'PONG' in screen(connect('bob')).cmd('PING')
            keys.write_text('')
            reject(lambda: connect(public=key))
            keys.write_text(f'1 {fingerprint}\n'); keys.chmod(0o666)
            reject(lambda: connect(public=key))
            keys.chmod(0o600)
            assert b'PONG' in screen(connect()).cmd('PING')
            # Partial handshake has a finite deadline and cannot starve Telnet.
            with socket.create_connection(('127.0.0.1', ssh_port), timeout=8) as stalled:
                time.sleep(3.3)
                while stalled.recv(8192): pass
            assert b'PONG' in telnet.cmd('PING')
            limited = paramiko.Transport(('127.0.0.1', ssh_port)); transports.append(limited)
            limited.start_client(timeout=10)
            for _ in range(5):
                reject(lambda: limited.auth_password('alice', 'wrong-password'))
            deadline = time.monotonic()+3
            while limited.is_active() and time.monotonic()<deadline: time.sleep(.05)
            assert not limited.is_active(), 'authentication attempts were not bounded'
            print('PASS key removal/unsafe permissions, handshake deadline and attempt limit')

            selected = connect('bob')
            mmm = screen(selected)
            mmm.cmd('UI mmm', b'UI preference saved.')
            mmm.until(b'MAIN>> ')
            selected.close()
            restored = connect('bob')
            mmm = screen(restored, b'MAIN>> ')
            mmm.cmd('games', b'GAMES>> ')
            assert b'SSH_DOOR_USER=bob' in mmm.cmd('1', b'DOOR> ')
            mmm.channel.resize_pty(width=100, height=30)
            assert b'PTY 100 30' in mmm.cmd('size', b'DOOR> ')
            assert b'SSH PTY test' in mmm.cmd('quit', b'GAMES>> ')
            mmm.cmd('.', b'MAIN>> ')
            switched = mmm.cmd('ui mash', b'UI preference saved.')
            assert b'MASH | 1 Mail' in switched, switched[-1500:]
            mmm.until(b'MAIN>> ')
            restored.close()
            selected = connect('bob')
            mash = screen(selected, b'MAIN>> ')
            assert b'Board map' in mash.cmd('?', b'MAIN>> ')
            assert b'SSH article body' in mash.cmd('open -b1 #1', b'OPEN> ')
            mash.channel.sendall(b'q')
            mash.until(b'NOTES>> ')
            mash.cmd('/', b'MAIN>> ')
            mash.cmd('6', b'OTHERS>> ')
            mash.cmd('4', b'GAMES>> ')
            assert b'SSH_DOOR_USER=bob' in mash.cmd('1', b'DOOR> ')
            assert b'SSH PTY test' in mash.cmd('quit', b'GAMES>> ')
            mash.cmd('.', b'MAIN>> ')
            mash.cmd('ui rtbbs', b'UI PREF SAVED rtbbs')
            mash.until()
            selected.close()
            assert b'PONG' in screen(connect('bob')).cmd('PING')
            kt = screen(connect('bob'))
            kt.cmd('UI ktbbs', b'UI preference saved.')
            kt.until(b'MAIN> ')
            kt.cmd('R 1', b'READ> ')
            assert b'SSH article body' in kt.cmd('I 1', b'ARTICLE> ')
            kt.channel.sendall(b'0')
            kt.until(b'READ> ')
            kt.cmd('0', b'MAIN> ')
            kt.cmd('UI rtbbs', b'UI PREF SAVED rtbbs')
            kt.until()
            kt.channel.get_transport().close()
            print('PASS MMM, MASH and KTBBS over SSH: shared articles, selection, and RTBBS restore')
        finally:
            telnet.close()
            for t in transports: t.close()
            server.proc.terminate()
            _, errors = server.proc.communicate(timeout=10)
            assert server.proc.returncode == 0, errors
            assert PASSWORD not in errors and fingerprint not in errors, 'credential leaked'
            assert all(line.startswith('SSH auth ') for line in errors.splitlines()), errors
        # New connection records keep the existing durable USERLOG format readable.
        again = Server(binary, root/'notes', members=root/'members')
        c = Raw(again.port); auth(c, 'alice'); c.send(b'PING\n'); c.expect(b'PONG'); c.close(); again.close()
        # A small configured cap is shared by Telnet and SSH, including clients
        # that disconnect mid-handshake. No host-wide connection flood is used.
        limits = root/'limits'; limits.write_text('connections=4\n')
        server = Server(binary, root/'notes', members=root/'members', telnet=True,
                        menu=True, language='en', admins=admins, options=options+['--limits', str(limits)])
        transports = []; telnet = Telnet(server.port)
        try:
            telnet.until()
            survivors = [screen(connect()) for _ in range(3)]
            assert survivors[0].cmd('CONNECTIONS').count(b'  Connected ')==4
            reject(connect)
            assert b'PONG' in telnet.cmd('PING')
            for survivor in survivors: assert b'PONG' in survivor.cmd('PING')
            survivors.pop().channel.get_transport().close()
            def wait_count(count):
                # Observe close processing on a surviving authenticated channel,
                # without racing connection retries or changing the limit.
                deadline = time.monotonic()+6
                while survivors[0].cmd('CONNECTIONS').count(b'  Connected ')!=count:
                    assert time.monotonic()<deadline, 'SSH connection slot was not released'
                    time.sleep(.03)
            wait_count(3)
            for _ in range(3):
                with socket.create_connection(('127.0.0.1', ssh_port), timeout=6) as aborted:
                    with aborted.makefile('rb') as banner:
                        assert banner.readline(256).startswith(b'SSH-2.0-')
                wait_count(3)
            replacement = screen(connect())
            assert b'PONG' in replacement.cmd('PING')
            print('PASS shared four-connection cap, aborted handshakes and SSH slot reuse')
        finally:
            telnet.close()
            for t in transports:t.close()
            server.proc.terminate();_, errors = server.proc.communicate(timeout=10)
            assert server.proc.returncode==0, errors
            assert PASSWORD not in errors and fingerprint not in errors, 'credential leaked'
    print('SSH integration passed')


if __name__ == '__main__':
    run(sys.argv[1])

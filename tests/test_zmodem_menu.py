"""Actual lrzsz over the runtime Telnet menu; no password in the helper."""
from concurrent.futures import ThreadPoolExecutor
from test_zmodem_workflows import spool_contents
import hashlib,os,pathlib,select,shlex,subprocess,sys,tempfile,time
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from transfer_connectors import LineConnector


def transfer(c,executable,args,directory,base_options=None,prompt=b'] > ',deadline_seconds=40,send_rate=None,escape_iac=True):
    def send(data):
        c.send(data.replace(b'\xff',b'\xff\xff') if escape_iac else data)
    wire_start=len(c.wire)  # Exclude login traffic from synthetic failure evidence.
    # A shared socketpair can reset on read if lsz exits with unread input.
    # Independent pipes keep the final protocol bytes readable in that case.
    proc=subprocess.Popen([executable]+(['--binary','--quiet','--escape','--syslog=off'] if base_options is None else base_options)+args,cwd=directory,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    peer_input=proc.stdin.fileno()
    peer_output=proc.stdout.fileno()
    os.set_blocking(peer_input,False)
    pending=bytearray(c.buf);c.buf=b'';deadline=time.monotonic()+deadline_seconds
    input_open=output_open=True
    try:
        while proc.poll() is None:
            if time.monotonic()>deadline:raise AssertionError('peer timed out')
            readers=[peer_output,c.sock] if output_open else [c.sock]
            writers=[peer_input] if input_open and pending else []
            reads,writes,_=select.select(readers,writers,[],.02)
            if peer_input in writes:
                try:
                    n=os.write(peer_input,pending);del pending[:n]
                except BlockingIOError:
                    pass
                except BrokenPipeError:
                    input_open=False
            if peer_output in reads:
                data=os.read(peer_output,65536)
                if data:
                    send(data)
                    if send_rate:time.sleep(len(data)/send_rate)
                else:
                    output_open=False
            if c.sock in reads:c.receive();pending.extend(c.buf);c.buf=b''
        # Drain the final OO/ack bytes emitted immediately before process exit.
        while output_open and select.select([peer_output],[],[],.05)[0]:
            data=os.read(peer_output,65536)
            if not data:break
            send(data)
        c.buf=bytes(pending)+c.buf
        error=proc.stderr.read()
        try:
            output=c.until(prompt)
        except (TimeoutError,AssertionError) as failure:
            raise AssertionError(('BBS response missing after transfer',proc.returncode,
                                  c.buf[-2048:],error[-2048:],
                                  c.wire[wire_start:][-2048:])) from failure
        return proc.returncode,output,error
    finally:
        if proc.poll() is None:proc.kill();proc.wait()
        proc.stdin.close()
        proc.stdout.close()
        proc.stderr.close()


def personal_file_checks(server, bob, sender, source, spool):
    main, gate, editor = b'MAIN>> ', b'LOGIN>> ', b'Editor>> '
    start = b'10240 bytes).\r\n'
    received = b'Personal file received.'
    preserved = b'Existing text kept.'

    def receive(client, name, data, command='pfile -b', sender_args=()):
        (source/name).write_bytes(data)
        client.cmd(command, start)
        return transfer(client, sender, [*sender_args, '--', name], source, prompt=main)

    def view(client):
        return client.cmd('pfile -t', main)

    def login(client, name):
        client.until()
        client.cmd('LOGIN '+name, b'> ')
        client.cmd(PASSWORD)
        client.cmd('UI mmm', b'UI preference saved.')
        client.until(main)

    assert b'pfile' in bob.cmd('pfile -?', main).lower()
    code, output, error = receive(bob, 'personal.txt', '一行目\nsecond\rthird'.encode())
    assert code == 0 and received in output, (code, output, error)
    assert '一行目\r\nsecond\r\nthird\r\n'.encode() in view(bob)
    exact = (b'a'*78+b'\r\n')*128
    code, output, error = receive(bob, 'personal-limit.txt', exact)
    assert len(exact) == 10240 and code == 0 and received in output, (code, output, error)
    assert exact in view(bob)

    for data in (exact+b'x', (b'a'*78+b'\n')*129, b'bad\xff', b'bad\x00',
                 b'bad\x1b[2J'):
        _, output, _ = receive(bob, 'personal-invalid.txt', data)
        assert preserved in output and exact in view(bob)

    bob.cmd('pfile -b', start)
    # During binary ownership, UI commands are wire data and cannot switch adapters.
    bob.send(b'UI rtbbs\r\n'+b'\x18'*8+b'\x08'*8)
    assert preserved in bob.until(main) and exact in view(bob)

    alice = Telnet(server.port)
    try:
        login(alice, 'alice')
        with ThreadPoolExecutor(max_workers=2) as pool:
            results = [
                pool.submit(receive, alice, 'personal-first.txt', b'personal note'),
                pool.submit(receive, bob, 'personal-second.txt', b'other member')]
            for result in results:
                code, output, error = result.result()
                assert code == 0 and received in output, (code, output, error)
        assert b'personal note' in view(alice) and b'other member' not in view(alice)
        assert b'other member' in view(bob) and b'personal note' not in view(bob)

        alice.cmd('post #1', b'Title> ')
        alice.cmd('Personal import', b'Text> ')
        alice.cmd('.', editor)
        alice.cmd('g', editor)
        assert b'personal note' in alice.cmd('p', editor)
        saved = alice.cmd('w', b'NOTES>> ')
        assert b'Saved #' in saved, saved
        alice.cmd('/', main)

        alice.cmd('mail', b'MAIL>> ')
        alice.cmd('send bob', b'Title> ')
        alice.cmd('Personal import', b'Text> ')
        alice.cmd('.', editor)
        alice.cmd('g', editor)
        assert b'personal note' in alice.cmd('p', editor)
        assert b'Mail sent' in alice.cmd('w', b'MAIL>> ')
        alice.cmd('.', main)

        alice.cmd('filer', b'FILER>> ')
        assert b'personal-first.txt' not in alice.cmd('list', b'FILER>> ')
        assert b'Protocol: text' in alice.cmd('protocol text', b'FILER>> ')
        alice.cmd('.', main)
        assert b'cannot receive' in alice.cmd('pfile -b', main)

        alice.cmd('filer', b'FILER>> ')
        alice.cmd('protocol xmodem', b'FILER>> ')
        alice.cmd('.', main)
        assert b'byte' in alice.cmd('pfile -b', main)
        xtext = b'xmodem text'
        code, output, error = receive(alice, 'personal-x.txt', xtext,
                                      f'pfile -b {len(xtext)}', ('--xmodem',))
        assert code == 0 and received in output, (code, output, error)
        assert xtext+b'\r\n' in view(alice)

        alice.cmd('filer', b'FILER>> ')
        alice.cmd('protocol ymodem', b'FILER>> ')
        alice.cmd('.', main)
        code, output, error = receive(alice, 'personal-y.txt', b'ymodem text',
                                      sender_args=('--ymodem',))
        assert code == 0 and received in output, (code, output, error)
        assert b'ymodem text\r\n' in view(alice)

        alice.cmd('filer', b'FILER>> ')
        alice.cmd('protocol zmodem', b'FILER>> ')
        alice.cmd('.', main)
        code, output, error = receive(alice, 'personal-empty.txt', b'')
        assert code == 0 and received in output, (code, output, error)
        assert b'ymodem text' not in view(alice)
        assert b'0 / 10240' in alice.cmd('pfile', main)
        assert not spool_contents(spool)

        bob.cmd('pfile -b', start)
        deadline = time.monotonic()+5
        while time.monotonic()<deadline and not spool_contents(spool):
            time.sleep(.02)
        assert spool_contents(spool), 'personal receive never opened staging'
        bob.close()
        deadline = time.monotonic()+5
        while time.monotonic()<deadline and spool_contents(spool):
            time.sleep(.02)
        assert not spool_contents(spool)
        with_client = Telnet(server.port)
        try:
            with_client.until()
            with_client.cmd('login bob', b'Password (8-128 bytes)> ')
            with_client.cmd(PASSWORD, main)
            assert b'0 / 10240' in with_client.cmd('pfile', main)
        finally:
            with_client.close()
        assert received in receive(alice, 'personal-logout.txt', b'wipe at logout')[1]
        alice.cmd('logout', gate)
        assert b'Login required' in alice.cmd('pfile -b', gate)
        alice.cmd('login bob', b'Password> ')
        alice.cmd(PASSWORD, main)
        assert b'0 / 10240' in alice.cmd('pfile', main)
    finally:
        alice.close()


def ssh_personal_check(binary, root, data, members, helper, spool, sender, source):
    import paramiko
    from ssh_screen import Screen
    from test_ssh_transport import port

    class BinaryScreen(Screen):
        def __init__(self, channel):
            super().__init__(channel)
            self.sock = channel
            self.wire = b''
        def send(self, data):
            self.channel.sendall(data)
        def receive(self):
            data = self.channel.recv(8192)
            assert data, 'unexpected SSH EOF'
            self.buf += data
            self.wire += data

    host = root/'pfile-ssh-host'
    subprocess.run(['ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-f', str(host)], check=True)
    ssh_port = port()
    # No vault or mail transfer state is needed for the private receive.
    options = ['--ui-default', 'mmm', '--ui-allow', 'mmm',
               '--ssh-port', str(ssh_port), '--ssh-host-key', str(host)]
    server = Server(binary, data, members=members, zhelper=helper, menu=True,
                    telnet=True, language='en', options=options)
    transport = None
    try:
        transport = paramiko.Transport(('127.0.0.1', ssh_port))
        transport.start_client(timeout=10)
        assert transport.get_remote_server_key().get_base64() == host.with_suffix('.pub').read_text().split()[1]
        transport.auth_password('alice', PASSWORD)
        server.expected_errors.append('SSH auth accepted slot=0')
        channel = transport.open_session(timeout=10)
        channel.get_pty(term='xterm', width=80, height=24)
        channel.invoke_shell()
        session = BinaryScreen(channel)
        main = b'MAIN>> '
        session.until(main)
        assert b'0 / 10240' in session.cmd('pfile', main)
        name = 'personal-ssh.txt'
        (source/name).write_bytes('SSHで受信\n'.encode())
        session.cmd('pfile -b', b'10240 bytes).\r\n')
        code, output, error = transfer(session, sender, ['--', name], source, prompt=main, escape_iac=False)
        assert code == 0 and b'Personal file received.' in output, (code, output, error)
        assert 'SSHで受信\r\n'.encode() in session.cmd('pfile -t', main)
        assert not spool_contents(spool)
    finally:
        if transport is not None:
            transport.close()
        server.close()


def run(binary,ctl,receiver,sender,rights=False):
    def failed(output):
        if b'Transfer timed out' in output:
            s.expected_errors.append('ZMODEM failure: TIMEOUT');return True
        return b'failed or cancelled' in output
    binary,ctl,receiver,sender=[str(pathlib.Path(x).resolve()) for x in (binary,ctl,receiver,sender)]
    root=pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='zmenu-') as directory:
        p=pathlib.Path(directory);vault=p/'vault';data=p/'bbs';members=p/'members';spool=p/'spool';spool.mkdir(mode=0o700);source=p/'source';source.mkdir();target=p/'target';target.mkdir()
        subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
        helper=p/'helper';helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(root/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','5'])+' -- "$@"\n');helper.chmod(0o700)
        s=Server(binary,data,members=members,vault=vault);a,b=Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        admins=p/'admins';admins.write_text('1\n')
        s=Server(binary,data,members=members,vault=vault,zhelper=helper,admins=admins,menu=True,telnet=True,language='en',
                 options=['--ui-policy','MEMBERS','--ui-allow','rtbbs,mmm']);c=Telnet(s.port);guest=Telnet(s.port)
        api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
        def downloaded(ident):
            api.protocol.send(f'FILESTATS {ident}');count=None
            while True:
                row=api.protocol.line()
                if row.startswith(b'DOWNLOADCOUNT '):count=int(row.split()[2])
                if row.startswith(b'END NEXT '):break
            return count
        try:
            guest.until();assert b'log in' in guest.cmd('ZUPLOAD').lower();guest.close()
            c.until();c.cmd('LOGIN bob',b'> ');c.cmd(PASSWORD);assert b'Vault (' in c.cmd('VAULT')
            for n,payload in enumerate([bytes(range(256))*257+b'\r\n\0\xff\x1b[2J',b'',b'UTF-8 filename']):
                name=f'payload-{n}.bin' if n<2 else '日本語 ファイル.bin';(source/name).write_bytes(payload)
                c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('test-description',b'1 GiB.\r\n')
                code,out,err=transfer(c,sender,['--',name],source);assert code==0 and b'published' in out,(code,out,err)
                ident=n+1;assert api.info(ident)==(len(payload),hashlib.sha256(payload).hexdigest())
                c.cmd(str(ident),b'0:back > ');c.cmd('Z',b'1 GiB.\r\n')
                code,out,err=transfer(c,receiver,['--restricted','--protect'],target,prompt=b'0:back > ');c.cmd('0');assert code==0 and b'sent (' in out,(code,out,err)
                assert (target/name).read_bytes()==payload
                assert downloaded(ident)==1
                assert b'PONG' in c.cmd('PING')
            assert not spool_contents(spool)
            batch_target=p/'batch-target';batch_target.mkdir()
            for ident in [1,2,3,1]:assert b'Selected' in c.cmd('BATCH ADD '+str(ident))
            assert b'3 files: 1,2,3' in c.cmd('BATCH LIST')
            c.cmd('BATCH REMOVE 2');c.cmd('BATCH ADD 2')
            c.cmd('BATCH SEND',b'16 GiB total.\r\n')
            code,out,err=transfer(c,receiver,['--restricted','--protect'],batch_target)
            assert code==0 and b'Batch sent: 3 files' in out,(code,out,err)
            for path in source.iterdir():assert (batch_target/path.name).read_bytes()==path.read_bytes()
            assert [downloaded(i) for i in (1,2,3)]==[2,2,2]
            assert b'BATCH COMPLETE' in c.cmd('BATCH SEND')
            c.cmd('BATCH CLEAR')
            for ident in (1,2,3):c.cmd('BATCH ADD '+str(ident))
            c.cmd('BATCH SEND',b'16 GiB total.\r\n')
            code,out,err=transfer(c,receiver,['--restricted','--protect'],batch_target)
            assert failed(out)
            c.cmd('BATCH SEND',b'16 GiB total.\r\n');c.send(b'\x18'*8+b'\x08'*8)
            assert failed(c.until())
            assert [downloaded(i) for i in (1,2,3)]==[2,2,2]  # skip/cancel must never count
            assert b'Selected 0' in c.cmd('BATCH CLEAR')
            assert b'BATCH ADD/REMOVE' in c.cmd('BATCH SEND')
            assert not spool_contents(spool)
            c.cmd('D 1',b'1 GiB.\r\n');code,out,err=transfer(c,receiver,['--restricted','--protect'],target);assert failed(out)
            c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('',b'1 GiB.\r\n');c.send(b'\x18'*8+b'\x08'*8);assert failed(c.until())
            assert not spool_contents(spool)
            c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('',b'1 GiB.\r\n');code,out,err=transfer(c,sender,['--','payload-0.bin','payload-1.bin'],source);assert failed(out)
            assert not spool_contents(spool)
            # A private-talk attempt while the engine owns the wire is refused.
            api.protocol.send('ONLINE');talk_connection=None
            for _ in range(32):
                row=api.protocol.line()
                if row==b'CHAT END':break
                if row.startswith(b'CHAT WHO ') and b'bob' in row:talk_connection=int(row.split()[2])
            assert talk_connection
            c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('',b'1 GiB.\r\n')
            api.protocol.request('TALK '+str(talk_connection),lambda x:x.startswith(b'CHAT TALK OPEN '))
            api.protocol.request('/SAY secret-during-transfer',lambda x:x.startswith(b'CHAT ERROR target'))
            api.protocol.request('//',lambda x:x==b'TALK CLOSED')
            s.expected_errors.append('ZMODEM failure: TIMEOUT')
            out=c.until();assert b'Transfer timed out' in out and b'secret-during-transfer' not in out # configured engine timeout
            assert b'PONG' in c.cmd('PING')
            # The global cap is checked before launching a ninth engine.
            waiting=[]
            try:
                for _ in range(8):
                    x=Telnet(s.port);waiting.append(x);x.until();x.cmd('LOGIN bob',b'> ');x.cmd(PASSWORD)
                for x in waiting:x.cmd('ZUPLOAD',b'1 GiB.\r\n')
                c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('',b'1 GiB.\r\n');assert failed(c.until())
                for x in waiting:x.send(b'\x18'*8+b'\x08'*8)
                for x in waiting:assert failed(x.until())
            finally:
                for x in waiting:x.close()
            assert b'test-description'   in c.cmd('FILE 1');assert b'payload-0' in c.cmd('S payload-0');c.cmd('C');c.cmd('0')
            api.protocol.request('FILEHIDE 1 CONFIRM',lambda x:x==b'FILE HIDDEN 1')
            c.cmd('ZDOWNLOAD 1',b'1 GiB.\r\n');assert failed(c.until())
            c.cmd('LANG ja');assert '保管庫'.encode() in c.cmd('VAULT');c.cmd('0')
            # Board-targeted reception is private from the first publication event.
            for path,read,write in [('-', 'member','member'),('private','member','none'),('private/docs','member','member')]:
                subprocess.run([ctl,'board-set',str(vault),path,read,write],check=True,capture_output=True)
            c.cmd('LANG en');c.cmd('VAULT')
            assert b'private/docs' in c.cmd('FILEBOARDS')
            c.cmd('UIN private/docs|board description',b'1 GiB.\r\n')
            code,out,err=transfer(c,sender,['--','payload-0.bin'],source);assert code==0 and b'published' in out,(code,out,err)
            assert b'payload-0.bin' in c.cmd('FILESIN private')
            c.cmd('UIN private|denied',b'1 GiB.\r\n');assert failed(c.until())
            assert not spool_contents(spool)
            c.cmd('0');c.cmd('LANG ja')
            # Exercise the same native helpers with the independent MMM menu.
            mmm = Telnet(s.port)
            try:
                mmm.until()
                mmm.cmd('LOGIN bob', b'> ')
                mmm.cmd(PASSWORD)
                mmm.cmd('UI mmm', b'UI preference saved.')
                mmm.until(b'MAIN>> ')
                mmm.cmd('filer', b'FILER>> ')
                payload = bytes(range(256)) * 8
                name = 'mmm-zmodem.bin'
                (source/name).write_bytes(payload)
                mmm.cmd('put MMM transfer', b'1 GiB.\r\n')
                code,out,err = transfer(mmm,sender,['--',name],source,prompt=b'FILER>> ')
                assert code==0 and b'Transfer complete' in out,(code,out,err)
                row = next(row for row in mmm.cmd('list', b'FILER>> ').splitlines() if name.encode() in row)
                ident = int(row.split()[0].lstrip(b'#'))
                mmm.cmd(f'get {ident}', b'1 GiB.\r\n')
                code,out,err = transfer(mmm,receiver,['--restricted','--protect'],target,prompt=b'FILER>> ')
                assert code==0 and b'Transfer complete' in out,(code,out,err)
                assert (target/name).read_bytes()==payload
                assert downloaded(ident)==1
                mmm.cmd('.', b'MAIN>> ')
                personal_file_checks(s, mmm, sender, source, spool)
            finally:
                mmm.close()

            # Authorization changes close the bound transfer; no partial upload becomes public.
            c.cmd('ZUPLOAD', '1GiBまで。\r\n'.encode());api.protocol.request('LEVEL 2 1' if rights else 'SUSPEND 2',lambda x:x.startswith(b'RIGHTS' if rights else b'SUSPENDED'))
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                if not c.sock.recv(4096):break
            else:raise AssertionError('revoked connection did not close')
        finally:c.close();api.close();s.close()
        time.sleep(.1);assert not spool_contents(spool),spool_contents(spool)
        if os.getenv('LINER_TEST_ZMODEM_SSH') == '1':
            ssh_personal_check(binary, p, data, members, helper, spool, sender, source)
    print('PASS: menu ZMODEM upload/download all bytes and empty files, original names, SHA-256, listing/search, JP/EN, skip/cancel, guest denial, revocation and spool cleanup')
if __name__=='__main__':run(*sys.argv[1:])

"""FreeBSD rc.d lifecycle using isolated config, ports, PID files and data."""
import os
import pathlib
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from test_telnet import Client


def run(binary,run_user=None,zhelper=None):
    if not sys.platform.startswith('freebsd'):
        print('SKIP: daemon service test requires FreeBSD'); return
    import pwd
    root=pathlib.Path(__file__).resolve().parents[1]
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='liner-daemon-') as d:
        base=pathlib.Path(d); p=base/"data's space $literal"; p.mkdir()
        account=pwd.getpwnam(run_user) if run_user else pwd.getpwuid(os.getuid())
        if run_user:
            assert os.getuid()==0, 'alternate run user requires root'
            base.chmod(0o755); os.chown(p,account.pw_uid,account.pw_gid)
        sock=socket.socket(); sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]; sock.close()
        config=base/'service.conf'
        settings={
            'liner_notes_enable':'YES', 'liner_notes_home':str(root),
            'liner_notes_program':binary, 'liner_notes_menu_dir':str(root/'menus'),
            'liner_notes_run_user':account.pw_name,
            'liner_notes_data_dir':str(p), 'liner_notes_port':str(port),
            'liner_notes_bind':'127.0.0.1', 'liner_notes_language':'en',
            'liner_notes_pidfile':str(base/'supervisor.pid'),
            'liner_notes_child_pidfile':str(base/'child.pid'),
            'liner_notes_logfile':str(base/'service.log'),
            'liner_notes_log_dir':str(base),
            'liner_notes_help_dir':str(root/'help'),
            'liner_notes_banner':str(root/'banner.txt'),
            'liner_notes_sysinfo':str(root/'sysinfo.txt'),
            'liner_notes_goodbye':str(root/'goodbye.txt'),
            'liner_notes_database':str(p/'liner.sqlite'),
            'liner_notes_timezone':'Asia/Tokyo',
            'liner_notes_limits':str(root/'limits.example.conf'),
        }
        vaultctl=root/'vaultctl'
        if vaultctl.exists() and not run_user:
            vault=p/'vault'; source=p/'sample'; source.write_bytes(b'daemon vault')
            subprocess.run([str(vaultctl),'init',str(vault)],check=True,capture_output=True)
            subprocess.run([str(vaultctl),'import',str(vault),str(source),'sample.txt','daemon test'],check=True,capture_output=True)
            settings['liner_notes_vault_dir']=str(vault)
        if zhelper:
            assert 'liner_notes_vault_dir' in settings
            settings['liner_notes_zmodem_helper']=str(pathlib.Path(zhelper).resolve())
        doors=base/'doors.conf';doors.write_text(f'1|0|10|/usr/bin/true|{p}|rc.d test game\n')
        settings['liner_notes_doors']=str(doors)
        def save():
            config.write_text(''.join(k+'='+shlex.quote(v)+'\n' for k,v in settings.items()))
        def service(action,ok=True):
            env=os.environ.copy(); env['LINER_NOTES_RC_CONF']=str(config)
            result=subprocess.run(['/bin/sh',str(root/'rc.d/liner_notes'),action],env=env,
                                  capture_output=True,text=True,timeout=40)
            if ok: assert result.returncode==0,(action,result.stdout,result.stderr,(base/'service.log').read_text() if (base/'service.log').exists() else '')
            return result
        def login():
            c=Client(port); c.until(); c.cmd('LOGIN daemonprobe',b'Password (8-128 bytes)> ')
            assert b'Logged in as daemonprobe' in c.cmd('daemon-test-secret-2468')
            return c
        save()
        try:
            assert service('status',False).returncode!=0
            service('start'); assert 'is running' in service('status').stdout
            original_pid=(base/'child.pid').read_text().strip()
            uid=int(subprocess.check_output(['ps','-o','uid=','-p',original_pid],text=True).strip())
            assert uid==account.pw_uid,(uid,account.pw_uid)
            tty=subprocess.check_output(['ps','-o','tty=','-p',original_pid],text=True).strip()
            assert tty in ['-','??'],tty
            assert service('start',False).returncode!=0
            assert (base/'child.pid').read_text().strip()==original_pid
            c=Client(port)
            try:
                assert b'Liner notes' in c.until()
                c.cmd('REGISTER daemonprobe',b'Password (8-128 bytes)> ')
                reply=c.cmd('daemon-test-secret-2468')
                assert b'Logged in as daemonprobe' in reply and b'daemon-test-secret' not in reply
                assert b'rc.d test game' in c.cmd('GAMES')
                assert b'Back to BBS' in c.cmd('DOOR 1')
                c.cmd('0') # GAME/GAMES now has its own persistent menu.
                if 'liner_notes_vault_dir' in settings:assert b'sample.txt' in c.cmd('FILES')
                if zhelper:
                    c.cmd('ZUPLOAD',b'1 GiB.\r\n');c.send(b'\x18'*8+b'\x08'*8)
                    assert b'failed or cancelled' in c.until()
                c.cmd('POST Daemon article',b'Body > ')
                c.cmd('Saved while detached',b'Body > ')
                assert b'Saved article #1' in c.cmd('.')
            finally:c.close()
            service('restart')
            assert (base/'child.pid').read_text().strip()!=original_pid
            c=login()
            try:assert b'Saved while detached' in c.cmd('READ 1')
            finally:c.close()
            service('stop')
            assert service('status',False).returncode!=0
            assert not (base/'supervisor.pid').exists() and not (base/'child.pid').exists()
            log=(base/'service.log').read_text()
            assert 'LISTEN 127.0.0.1' in log and 'daemon-test-secret' not in log
            assert 'Saved while detached' not in log
            snapshot={f:f.read_bytes() for f in p.iterdir() if f.is_file() and '-index.sqlite' not in f.name}
            print('PASS: detached rc.d start/status/restart/stop, duplicate prevention, quoted paths, login/post/read persistence and private logs')

            # A stale PID file must not prevent a later start.
            (base/'supervisor.pid').write_text('99999999\n')
            (base/'child.pid').write_text('99999999\n')
            service('start'); service('stop')
            assert all(f.read_bytes()==data for f,data in snapshot.items())
            settings['liner_notes_bind']='invalid-address'; save()
            assert service('start',False).returncode!=0
            assert service('status',False).returncode!=0
            assert 'invalid --bind' in (base/'service.log').read_text()
            settings['liner_notes_bind']='127.0.0.1'; save()
            occupied=socket.socket(); occupied.bind(('127.0.0.1',port)); occupied.listen()
            try:
                assert service('start',False).returncode!=0
                assert service('status',False).returncode!=0
                assert 'listen setup failed' in (base/'service.log').read_text()
            finally:occupied.close()
            assert all(f.read_bytes()==data for f,data in snapshot.items())
            service('start'); service('stop')
            settings.update(liner_notes_registration='closed',liner_notes_chat='NO',liner_notes_mail='NO',liner_notes_vault='NO');save()
            service('start');c=login()
            try:
                for action in ['CHAT','MAIL','FILES']:
                    assert b'disabled' in c.cmd(action),action
            finally:c.close();service('stop')
            settings.update(liner_notes_registration='approval',liner_notes_id_mode='auto',liner_notes_registration_form='detailed');save()
            service('start');c=Client(port)
            try:
                c.until();out=c.cmd('REGISTER',b': ');assert b'REALNAME' in out,out
                c.cmd('/CANCEL')
            finally:c.close();service('stop')
            for key in ['liner_notes_id_mode','liner_notes_registration_form']:
                previous=settings[key];settings[key]='invalid';save()
                assert service('start',False).returncode!=0
                settings[key]=previous
            settings.update(liner_notes_id_mode='free',liner_notes_registration_form='simple')
            settings['liner_notes_registration']='invalid';save()
            assert service('start',False).returncode!=0
            settings['liner_notes_registration']='closed';settings['liner_notes_chat']='maybe';save()
            assert service('start',False).returncode!=0
            settings['liner_notes_chat']='NO';save()
            print('PASS: rc registration and feature settings, invalid policy rejection')
            settings['liner_notes_enable']='NO'; save()
            service('start',False)  # rc.subr treats a disabled service as a no-op.
            assert not (base/'child.pid').exists()
            assert service('status',False).returncode!=0
            service('onestart'); service('onestop')
            print('PASS: stale PID recovery, failed bind/config startup returns failure, data preserved, enable/one-command semantics')
        finally:
            settings['liner_notes_enable']='YES'; save()
            service('stop',False)


if __name__=='__main__':
    run(sys.argv[1],sys.argv[2] if len(sys.argv)>2 else None)

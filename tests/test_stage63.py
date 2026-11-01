"""Multi-attachment durability/privacy, account lifecycle, connection status and full compaction."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from test_board import Server, post, read
from test_runtime import Client
from test_members import auth, PASSWORD
from test_stage58 import cmd, rows
from test_ui import Menu
from test_stage60 import password, enter
from test_audit import history

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from transfer_connectors import MailConnector, RemoteError
import maintenance


def run(binary, vaultctl, memberctl):
    binary, vaultctl, memberctl = [str(Path(p).resolve()) for p in (binary, vaultctl, memberctl)]
    with tempfile.TemporaryDirectory(prefix='stage63-') as tmp:
        p=Path(tmp); root=p/'live';root.mkdir();tools=p/'tools';tools.mkdir()
        for name, source in [('vaultctl',vaultctl),('memberctl',memberctl)]:
            shutil.copy2(source,tools/(name+('.exe' if os.name=='nt' else '')))
        def cli(*args):
            result=subprocess.run([vaultctl]+list(map(str,args)),capture_output=True)
            assert result.returncode==0,(args,result.stderr)
            return result.stdout
        cli('init',root/'vault')
        (root/'payload').write_bytes(b'public')
        cli('import',root/'vault',root/'payload','public.bin','first')
        for i in range(20):cli('describe',root/'vault',1,'description'+str(i))
        def start(where,options=None,menu=False):
            return Server(binary,where/'notes',members=where/'members',vault=where/'vault',admins=where/'admins' if (where/'admins').exists() else None,options=options,menu=menu,language='en')
        s=start(root)
        for name in ('admin','alice','bob','retiring','deleted'):
            c=Client(s.port);auth(c,name,True);c.close()
        s.close();(root/'admins').write_text('1\n')
        s=start(root); a=Client(s.port);b=Client(s.port);admin=Client(s.port);retiring=Client(s.port);deleted=Client(s.port)
        connector=None
        try:
            for c,name in [(a,'alice'),(b,'bob'),(admin,'admin'),(retiring,'retiring'),(deleted,'deleted')]:auth(c,name)
            assert cmd(a,'CONNECTIONS')==b'ERR admin required'
            status=rows(admin,'CONNECTIONS',b'END CONNECTIONS')
            fields=[line.split() for line in status if line.startswith(b'CONNECTION ')]
            assert len(fields)>=5 and all(len(line)==13 for line in fields),fields
            assert all(b':' in line[5] and line[4]==b'RAW' for line in fields)
            assert any(int(line[8])>0 and int(line[9])>0 for line in fields)
            assert post(a,'kept','article body')==1
            for i in range(4):
                assert cmd(admin,'HIDE 1')==b'HIDDEN SAVED 1'
                assert cmd(admin,'RESTORE 1')==b'RESTORED 1'
                assert cmd(a,f'RETITLE 1 latest-{i}')==b'RETITLED 1'
                assert cmd(admin,'BOARDOPTIONS 1 BOARD 1 0 0 1000')==b'BOARD OPTIONS SAVED'
                assert cmd(admin,'INFOEDIT 1')==b'BOARD INFO EDIT'
                admin.send(f'guide-{i}\n'.encode());assert cmd(admin,'/SAVE')==b'BOARD INFO SAVED'
                assert cmd(admin,'SIGOP 1 3')==b'SIGOP SET 1 3'
            password(admin,1,'Stage63-board-secret')
            enter(a,1,'Stage63-board-secret')
            assert cmd(a,'POST retained-draft').startswith(b'EDIT')
            a.send(b'retained body\n');assert cmd(a,'/SAVE')==b'DRAFT STORED 1'
            assert cmd(a,'/CANCEL')==b'CANCELLED'
            assert cmd(retiring,'PROFILESET MACHINE retired-private-machine')==b'PROFILE SAVED'
            assert cmd(admin,'DELETEUSER 1 CONFIRM')==b'ERR protected member'
            assert cmd(a,'DELETEUSER 3 CONFIRM')==b'ERR admin required'
            assert cmd(admin,'DELETEAPPLICATION 3 CONFIRM')==b'ERR member state'
            assert cmd(admin,'DELETEUSER 5 CONFIRM')==b'MEMBER REMOVED 5 KIND 4'
            # Every subsequent operation from the old session is rejected/disconnected.
            try:
                deleted.send(b'POST forbidden\n');reply=deleted.line()
            except (OSError,AssertionError):reply=b''
            assert not reply.startswith(b'EDIT')
            assert cmd(retiring,'RETIRE CONFIRM').startswith((b'MEMBER REMOVED',b'ERR session revoked'))
            for i in range(12):
                assert cmd(a,f'PROFILESET MACHINE machine-{i}')==b'PROFILE SAVED'
                assert cmd(a,'PAGER ON' if i%2 else 'PAGER OFF').startswith(b'PAGER')
                assert cmd(a,'PREFS SAVE').startswith(b'PREFS SAVED')
            connector=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
            ids=[]
            for i in range(17):
                payload=('attachment-'+str(i)).encode();ident=connector.begin(len(payload),hashlib.sha256(payload).hexdigest(),f'{i}.bin','attachment')
                connector.write(ident,0,payload);connector.finish(ident);ids.append(ident)
            assert cmd(a,'MAIL SEND bob multiple').startswith(b'MAIL EDIT')
            for ident in ids[:16]:assert cmd(a,f'/ATTACH {ident}').startswith(b'MAIL ATTACHMENT')
            assert cmd(a,f'/ATTACH {ids[16]}')==b'MAIL ERROR unavailable'
            assert cmd(a,f'/ATTACH {ids[0]}').startswith(b'MAIL ATTACHMENT') # idempotent
            assert cmd(a,f'/DETACH {ids[1]}').startswith(b'MAIL ATTACHMENT')
            assert cmd(a,f'/ATTACH {ids[16]}').startswith(b'MAIL ATTACHMENT')
            a.send(b'body with sixteen files\n');assert cmd(a,'/SAVE')==b'MAIL DRAFT SAVED'
        finally:
            if connector:connector.close()
            for c in (a,b,admin,retiring,deleted):c.close()
            s.close()
        # Pending requests have a distinct terminal state; IDs/names are reserved.
        s=start(root,options=['--registration','approval']);admin=Client(s.port)
        try:
            auth(admin,'admin')
            for name,action,ident in [('rejectme','REJECT',6),('deleteme','DELETEAPPLICATION',7)]:
                c=Client(s.port)
                try:
                    c.send(f'REGISTER {name}\n'.encode());c.expect(b'PASSWORD');c.send((PASSWORD+'\n').encode())
                    assert c.line()==b'APPLICATION SAVED'
                finally:c.close()
                result=cmd(admin,f'{action} {ident} CONFIRM')
                assert result==f'MEMBER REMOVED {ident} KIND {1 if action=="REJECT" else 2}'.encode(),result
            assert not any(line.startswith(b'APPLICATION ') for line in rows(admin,'APPLICATIONS'))
        finally:admin.close();s.close()
        s=start(root);a=Client(s.port);b=Client(s.port);admin=Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob');auth(admin,'admin')
            a.send(b'MAIL RESUME\n'); resume=[]
            while True:
                line=a.line();resume.append(line)
                if line.startswith(b'MAIL DRAFT '):break
            assert sum(line.startswith(b'MAIL ATTACHMENT ') for line in resume)==16
            assert cmd(a,'.')==b'MAIL SENT 1'
            b.send(b'MAIL READ 1\n'); received=[]
            while True:
                line=b.line();received.append(line)
                if line==b'MAIL END':break
            assert sum(line.startswith(b'MAIL ATTACHMENT ') for line in received)==16
            private=MailConnector('127.0.0.1',s.port,'admin',PASSWORD)
            receiver=MailConnector('127.0.0.1',s.port,'bob',PASSWORD)
            try:
                for ident in [ids[0],ids[16]]:assert receiver.read(ident,0)
                for ident in ids:
                    try:private.info(ident)
                    except RemoteError:pass
                    else:raise AssertionError('SYSOP can read another member attachment')
                try:receiver.info(ids[1])
                except RemoteError:pass
                else:raise AssertionError('detached file visible to recipient')
            finally:private.close();receiver.close()
        finally:a.close();b.close();admin.close();s.close()
        before=maintenance.inventory(root)[0]
        archive=p/'backup.tar.gz';maintenance.backup(root,'notes','members',archive)
        report=maintenance.restore(archive,p/'compact',compact=True,all_stores=True,tools_dir=tools)
        assert any(item.get('kind')=='members-snapshot' for item in report['changes'])
        assert any(item.get('kind')=='profile-snapshot' and item['saved_bytes']>0 for item in report['changes'])
        assert any(item.get('kind')=='vault-snapshot' and item['saved_bytes']>0 for item in report['changes'])
        assert maintenance.inventory(root)[0]==before
        q=p/'compact'
        assert b'retired-private-machine' not in (q/'members.profile').read_bytes()
        s=start(q);a=Client(s.port);b=Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob')
            assert cmd(a,'READ 1')==b'ERR board unavailable'
            enter(a,1,'Stage63-board-secret')
            assert read(a,1)[1:] == ('latest-3','article body\r\n')
            assert b'guide-3' in b'\n'.join(rows(a,'INFO 1',b'END INFO'))
            a.send(b'RESUME\n');draft=[]
            while True:
                line=a.line();draft.append(line)
                if line.startswith(b'DRAFT '):break
            assert cmd(a,'.')==b'SAVED 2'
            assert read(a,2)[2]=='retained body\r\n'
            audit=Client(s.port)
            try:
                auth(audit,'admin');events,_=history(audit)
                assert [e[4] for e in events]==['RETITLE','RESTORE'],events
            finally:audit.close()
            assert b'machine-11' in b'\n'.join(rows(a,'PROFILEID 2',b'END PROFILE'))
            for name in ('retiring','deleted','rejectme','deleteme'):
                c=Client(s.port)
                try:c.send(f'LOGIN {name}\n'.encode());c.expect(b'PASSWORD');c.send((PASSWORD+'\n').encode());assert c.line()==b'ERR authentication'
                finally:c.close()
            receiver=MailConnector('127.0.0.1',s.port,'bob',PASSWORD)
            try:assert receiver.read(ids[16],0)==b'attachment-16'
            finally:receiver.close()
            assert cmd(b,'MAIL DELETE 1').startswith(b'MAIL DELETE CONFIRM');assert cmd(b,'Y')==b'MAIL DELETED 1'
            for ident in ids[:1]+ids[2:]:assert not (q/'members.mailfiles'/f'{ident:08}.bin').exists()
            assert (q/'members.mailfiles'/f'{ids[1]:08}.bin').exists() # unused upload remains owner-managed
        finally:a.close();b.close();s.close()
        # The external administrator menu uses the expanded localized renderer.
        s=start(q,menu=True);m=Menu(s.port)
        try:
            m.until();m.cmd('LOGIN admin','> ');m.cmd(PASSWORD);m.cmd('PAGER OFF')
            screen=m.cmd('CONNECTIONS');assert 'Connected ' in screen and 'RX ' in screen,screen
        finally:m.close();s.close()
    print('PASS stage63: connection status, protected lifecycle, pending rejection/deletion, retire, 16 attachments, restart/draft/privacy/reclamation, full state compaction')


if __name__=='__main__':run(*sys.argv[1:])

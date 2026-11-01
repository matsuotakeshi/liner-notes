"""Private attachment transport, durable drafts, access and reference reclamation."""
import hashlib,pathlib,sys,tempfile,subprocess,importlib.util,json
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_talk import command
from test_mail import read
from test_ui import Menu
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import MailConnector,LineConnector,RemoteError
from notes_compact import compact

def denied(fn):
    try:fn()
    except RemoteError as e:assert str(e) in ('ERR no file','ERR vault denied','ERR vault disabled','ERR feature disabled','ERR session revoked'),str(e)
    else:raise AssertionError('private data accessible')

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='mail-attachments-') as d:
        p=pathlib.Path(d);data=p/'notes';members=p/'members';admins=p/'admins';admins.write_text('3\n')
        def start(**kw):return Server(binary,data,members=members,admins=admins,**kw)
        s=Server(binary,data,members=members);a,b,c=[Client(s.port) for _ in range(3)]
        try:
            for x,name in [(a,'alice'),(b,'bob'),(c,'carol')]:auth(x,name,True)
        finally:a.close();b.close();c.close();s.close()
        payload=bytes(range(256))*6;digest=hashlib.sha256(payload).hexdigest()
        s=start(options=['--disable','vault']);a,b=[Client(s.port) for _ in range(2)];ca,cb,cc=[MailConnector('127.0.0.1',s.port,n,PASSWORD) for n in ('alice','bob','carol')]
        try:
            auth(a,'alice');auth(b,'bob')
            ident=ca.begin(len(payload),digest,'秘密.bin','private');ca.write(ident,0,payload[:600]);assert ca.status(ident)[1]==600
            denied(lambda:cb.status(ident));denied(lambda:cc.status(ident))
            for offset in range(600,len(payload),600):ca.write(ident,offset,payload[offset:offset+600])
            assert ca.finish(ident)==(len(payload),digest)
            denied(lambda:cb.info(ident));denied(lambda:cc.read(ident,0))
            command(a,'MAIL SEND bob attached',b'MAIL EDIT ')
            command(a,f'/ATTACH {ident}',b'MAIL ATTACHMENT ');a.send(b'body\n');command(a,'/SAVE',b'MAIL DRAFT SAVED')
            denied(lambda:cb.info(ident)) # recipient cannot read the sender draft
            command(a,f'MAIL DROP {ident} CONFIRM',b'MAIL ERROR unavailable')
            command(a,'MAIL RESUME',b'MAIL ATTACHMENT ');assert a.line().startswith(b'MAIL DRAFT ')
            command(a,'.',b'MAIL SENT 1')
            assert b'MAIL ATTACHMENT' in read(b,1)
            assert cb.info(ident)==(len(payload),digest);assert cb.read(ident,0)==payload[:768]
            denied(lambda:cc.info(ident));denied(lambda:cc.read(ident,0)) # SYSOP cannot read
            command(b,'MAIL SEND alice not-owner',b'MAIL EDIT ');command(b,f'/ATTACH {ident}',b'MAIL ERROR unavailable');command(b,'/CANCEL',b'CANCELLED')
            command(a,'MAIL SEND bob shared',b'MAIL EDIT ');command(a,f'/ATTACH {ident}',b'MAIL ATTACHMENT ');a.send(((b'x'*1022+b'\r\n')*16).replace(b'\r\n',b'\n')+b'.\n');a.expect(b'MAIL SENT 2')
            command(b,'MAIL DELETE 1',b'MAIL DELETE CONFIRM');command(b,'Y',b'MAIL DELETED 1');assert cb.info(ident)==(len(payload),digest)
            # A saved draft retains the shared attachment across restart/compaction.
            command(a,'MAIL SEND bob saved',b'MAIL EDIT ');command(a,f'/ATTACH {ident}',b'MAIL ATTACHMENT ');command(a,'/SAVE',b'MAIL DRAFT SAVED')
        finally:a.close();b.close();ca.close();cb.close();cc.close();s.close()
        journal=p/'members.mail';original=journal.read_bytes();output=p/'mail.compacted';compact('mail',journal,output);journal.write_bytes(output.read_bytes())
        s=start();a,b=[Client(s.port) for _ in range(2)];ca,cb=[MailConnector('127.0.0.1',s.port,n,PASSWORD) for n in ('alice','bob')]
        try:
            auth(a,'alice');auth(b,'bob');assert cb.info(ident)==(len(payload),digest)
            assert len(read(b,2))>16000
            command(b,'MAIL DELETE 2',b'MAIL DELETE CONFIRM');command(b,'Y',b'MAIL DELETED 2');denied(lambda:cb.info(ident));assert ca.info(ident)==(len(payload),digest)
            command(a,'MAIL RESUME',b'MAIL ATTACHMENT ');assert a.line().startswith(b'MAIL DRAFT ')
            command(a,'/DETACH',b'MAIL ATTACHMENT 0');command(a,'/SAVE',b'MAIL DRAFT SAVED');denied(lambda:ca.info(ident))
            assert not (p/'members.mailfiles'/f'{ident:08}.bin').exists()
            # Unsent upload can be removed, including an unfinished reservation.
            staged=ca.begin(0,hashlib.sha256(b'').hexdigest(),'empty','');ca.finish(staged)
            command(a,f'MAIL DROP {staged} CONFIRM',b'MAIL ATTACHMENT REMOVED')
            active=ca.begin(0,hashlib.sha256(b'').hexdigest(),'cancel','');command(a,f'MAIL DROP {active} CONFIRM',b'MAIL ATTACHMENT REMOVED')
            assert ca.status(active)[0]=='CANCELLED'
            # Unknown source ID and malformed journal never gain access.
            denied(lambda:ca.info(1000))
        finally:a.close();b.close();ca.close();cb.close();s.close()
        # Simulate a crash after the durable hide but before unlink completion.
        blob=p/'members.mailfiles'/f'{ident:08}.bin';blob.write_bytes(payload)
        s=start();ca=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
        try:denied(lambda:ca.info(ident));assert not blob.exists()
        finally:ca.close();s.close()
        # Missing attachment storage must not reset IDs and expose a new file through old mail references.
        store=p/'members.mailfiles';backup=p/'saved-mailfiles';store.rename(backup)
        s=start();ca=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
        try:
            try:ca.begin(0,hashlib.sha256(b'').hexdigest(),'must-not-reuse-id','')
            except RemoteError as e:assert str(e)=='ERR vault storage'
            else:raise AssertionError('missing referenced store was recreated')
            assert not store.exists()
        finally:ca.close();s.close();backup.rename(store)
        for lang in ('ja','en'):
            s=start(menu=True,language=lang);m=Menu(s.port)
            try:
                m.until();m.cmd('LOGIN alice','> ');m.cmd(PASSWORD);m.cmd('PAGER OFF')
                assert 'MAIL' in m.cmd('HELP MAIL')
                assert ('メール専用' if lang=='ja' else 'Private mail') in m.cmd('MAIL FILES','0終了)> ' if lang=='ja' else '0 exit)> ')
            finally:m.close();s.close()
        # Both storage and transfer paths obey mail feature switch.
        s=start(options=['--disable','mail']);ca=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
        try:denied(lambda:ca.begin(0,hashlib.sha256(b'').hexdigest(),'disabled',''))
        finally:ca.close();s.close()
        saved=journal.read_bytes();journal.write_bytes(saved[:-1]);r=subprocess.run([binary,'0',str(data),str(members)],capture_output=True,timeout=10);assert r.returncode;journal.write_bytes(saved)
    print('PASS: private mail uploads, resume, owner/recipient-only reads, SYSOP denial, shared references, drafts, compaction/restart, deletion/reclamation and JP/EN menus')
if __name__=='__main__':run(sys.argv[1])

"""Configured vault quotas above former ID/event/total-byte limits."""
from pathlib import Path
import hashlib,struct,subprocess,sys,tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import MailConnector

def run(binary,ctl):
    binary,ctl=[str(Path(s).resolve()) for s in (binary,ctl)]
    with tempfile.TemporaryDirectory(prefix='quotas63-') as tmp:
        p=Path(tmp);v=p/'vault';source=p/'x';source.write_bytes(b'x')
        def cli(*args,ok=True):
            r=subprocess.run([ctl]+list(map(str,args)),capture_output=True)
            assert (r.returncode==0)==ok,(args,r.stdout,r.stderr)
            return r.stdout
        cli('init',v);cli('import',v,source,'x','initial')
        row=bytearray((v/'index.dat').read_bytes()[8:]);struct.pack_into('<I',row,4,2001);row[608:]=hashlib.sha256(row[:608]).digest()
        rows=bytearray(b'LNVLT001')+row
        for i in range(10001):
            event=bytearray(640);event[0]=ord('E');struct.pack_into('<I',event,4,2001)
            actor=b'local-operator';text=f'description-{i}'.encode();struct.pack_into('<II',event,16,len(actor),len(text));event[24:24+len(actor)]=actor;event[104:104+len(text)]=text;event[608:]=hashlib.sha256(event[:608]).digest();rows+=event
        (v/'index.dat').write_bytes(rows);(v/'00002001.bin').write_bytes(b'x')
        cli('info',v,2001) # Existing state stays readable after lowering quotas.
        cli('import',v,source,'denied','',ok=False)
        (v/'limits.conf').write_text('max_files=0\nmax_events=0\nmax_bytes=0\n')
        assert cli('import',v,source,'after','').startswith(b'2002 ')
        assert b'10003 ' in cli('audit',v)
        cli('compact-index',v,p/'new-index')
        assert (p/'new-index').stat().st_size < 8+5*640
        (p/'new-index').replace(v/'index.dat');assert cli('import',v,source,'next','').startswith(b'2003 ')
        (v/'limits.conf').write_text('max_files=10\nmax_events=1\nmax_bytes=1\n')
        cli('info',v,2003);cli('import',v,source,'blocked','',ok=False)
        for text in ('max_files=-1\n','unknown=0\n','max_files=0\nmax_files=2\n','max_bytes=18446744073709551616\n'):
            (v/'limits.conf').write_text(text);cli('info',v,2003,ok=False)
        (v/'limits.conf').write_text('max_files=0\nmax_events=0\nmax_bytes=0\n')
        # Physical usage above the former 256 MiB limit, without reading a huge payload.
        large=p/'large';cli('init',large);(large/'limits.conf').write_text('max_bytes=268435456\n')
        with (large/'00000001.bin').open('wb') as f:f.truncate(256*1024*1024)
        cli('import',large,source,'over','',ok=False)
        (large/'limits.conf').write_text('max_bytes=0\n')
        assert cli('import',large,source,'over','').startswith(b'2 ')
        s=Server(binary,p/'notes',members=p/'members');a=Client(s.port);b=Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        private=p/'members.mailfiles';cli('init',private)
        row=bytearray(row);publisher=b'mail-member:1';row[444:524]=b'\0'*80;row[444:444+len(publisher)]=publisher;struct.pack_into('<I',row,524,len(publisher));row[608:]=hashlib.sha256(row[:608]).digest()
        (private/'index.dat').write_bytes(b'LNVLT001'+row);(private/'00002001.bin').write_bytes(b'x')
        (private/'limits.conf').write_text('max_files=0\nmax_events=0\nmax_bytes=0\n')
        s=Server(binary,p/'notes',members=p/'members');a=Client(s.port)
        sender=MailConnector('127.0.0.1',s.port,'alice',PASSWORD);receiver=MailConnector('127.0.0.1',s.port,'bob',PASSWORD)
        try:
            auth(a,'alice');new=sender.begin(1,hashlib.sha256(b'y').hexdigest(),'new','');assert new==2002
            sender.write(new,0,b'y');sender.finish(new)
            assert cmd(a,'MAIL SEND bob above').startswith(b'MAIL EDIT')
            for ident in (2001,2002):assert cmd(a,f'/ATTACH {ident}').startswith(b'MAIL ATTACHMENT')
            a.send(b'body\n.\n');a.expect(b'MAIL SENT 1')
            assert receiver.read(2001,0)==b'x' and receiver.read(2002,0)==b'y'
        finally:a.close();sender.close();receiver.close();s.close()
    print('PASS stage63 quotas: IDs >2000, events >10000, physical total >256MiB, zero unlimited, lowering quotas, invalid config, private attachments')

if __name__=='__main__':run(*sys.argv[1:])

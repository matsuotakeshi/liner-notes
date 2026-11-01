import pathlib, tempfile, sys, time, subprocess, os, signal, struct
from test_board import Server, post, read
from test_runtime import Client
from test_members import auth, PASSWORD

def cmd(c,text,prefix):
    c.send((text+'\n').encode());row=c.line();assert row.startswith(prefix),(text,row,prefix);return row

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='rights-') as d:
        p=pathlib.Path(d);data=p/'notes';users=p/'members';admins=p/'admins';catalog=p/'boards'
        admins.write_text('1\n')
        s=Server(binary,data,members=users);a=Client(s.port);b=Client(s.port)
        try:
            auth(a,'alice',True);auth(b,'bob',True)
            cmd(b,'RIGHTS',b'RIGHTS 2 LEVEL 2 GROUPS - STATUS active')
            cmd(b,'LEVEL 2 5',b'ERR admin required')
        finally:a.close();b.close();s.close()
        catalog.write_text('1|0|all|member|General\n2|1|l3+g1|l4+g1|Private\n3|2|all|member|Inherited\n4|1|g32|g32|Last group\n5|1|l0|l0|Level zero\n')
        def server(options=None):return Server(binary,data,members=users,admins=admins,catalog=catalog,options=options)
        s=server();a=Client(s.port);b=Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob')
            cmd(b,'BOARD 2',b'ERR board unavailable')
            cmd(b,'BOARD 5',b'BOARD 5')
            cmd(a,'LEVEL 2 3',b'RIGHTS 2 LEVEL 3');b.close();b=Client(s.port);auth(b,'bob')
            cmd(b,'BOARD 2',b'ERR board unavailable')
            cmd(a,'GROUP 2 1',b'RIGHTS 2 LEVEL 3 GROUPS 1');b.close();b=Client(s.port);auth(b,'bob')
            cmd(b,'BOARD 2',b'BOARD 2');cmd(b,'POST denied',b'ERR posting denied')
            cmd(a,'LEVEL 2 4',b'RIGHTS 2 LEVEL 4');b.close();b=Client(s.port);auth(b,'bob')
            cmd(b,'BOARD 2',b'BOARD 2');article=post(b,'private','private text')
            cmd(a,'GROUP 2 32',b'RIGHTS 2 LEVEL 4 GROUPS 1,32');b.close();b=Client(s.port);auth(b,'bob')
            cmd(b,'BOARD 4',b'BOARD 4')
            cmd(a,'UNGROUP 2 1',b'RIGHTS 2 LEVEL 4 GROUPS 32')
            assert b'ERR session revoked' in b.line();b.close();b=Client(s.port);auth(b,'bob')
            cmd(b,'BOARD 2',b'ERR board unavailable');cmd(b,'BOARD 3',b'ERR board unavailable')
            cmd(b,f'READ {article}',b'ERR no article')
            cmd(a,'LEVEL 2 0',b'RIGHTS 2 LEVEL 0');b.close();b=Client(s.port);auth(b,'bob');cmd(b,'BOARD 5',b'BOARD 5')
            for command in ['LEVEL 2 6','GROUP 2 0','GROUP 2 101','LEVEL 2 -1','LEVEL 0 5','APPROVE 0','RIGHTS 2 junk']:
                cmd(a,command,b'ERR')
            cmd(a,'LEVEL 2 5',b'RIGHTS 2 LEVEL 5');b.close();b=Client(s.port);auth(b,'bob');cmd(b,'LEVEL 2 1',b'ERR admin required')
        finally:a.close();b.close();s.close()
        s=server(['--registration','approval']);a=Client(s.port);c=Client(s.port)
        try:
            auth(a,'alice');cmd(c,'REGISTER charlie',b'PASSWORD');cmd(c,PASSWORD,b'APPLICATION SAVED')
            cmd(c,'ACCOUNT',b'ERR login required');c.close();c=Client(s.port)
            cmd(c,'LOGIN charlie',b'PASSWORD');cmd(c,PASSWORD,b'ERR authentication')
            cmd(a,'APPLICATIONS',b'APPLICATIONS');a.expect(b'APPLICATION 3 charlie');a.expect(b'END NEXT 0')
            cmd(a,'APPROVE 3',b'RIGHTS 3 LEVEL 2 GROUPS - STATUS active');c.close();c=Client(s.port);auth(c,'charlie')
            cmd(c,'RIGHTS',b'RIGHTS 3 LEVEL 2 GROUPS - STATUS active')
            cmd(a,'RIGHTS 2',b'RIGHTS 2 LEVEL 5 GROUPS 32')
        finally:a.close();c.close();s.close()
        s=server(['--registration','closed','--disable','chat','--disable','mail','--disable','vault']);a=Client(s.port);c=Client(s.port)
        try:
            auth(a,'alice');cmd(c,'REGISTER david',b'PASSWORD');cmd(c,PASSWORD,b'ERR registration closed')
            for command in ['CHAT','TALK','ONLINE','MAIL','FILES','TRANSFER BINARY','FILESTATUS 1']:
                cmd(a,command,b'ERR feature disabled')
            post(a,'normal','board stays usable')
        finally:a.close();c.close();s.close()
        s=server();c=Client(s.port)
        try:auth(c,'charlie');cmd(c,'RIGHTS',b'RIGHTS 3 LEVEL 2 GROUPS - STATUS active')
        finally:c.close();s.close()
        from test_ui import Menu
        for language in ['ja','en']:
            s=Server(binary,data,members=users,admins=admins,catalog=catalog,menu=True,language=language,options=['--registration','approval','--disable','chat','--disable','mail'])
            a=Menu(s.port);c=Menu(s.port)
            try:
                a.until();c.until();a.cmd('LOGIN alice','> ');a.cmd(PASSWORD)
                assert ('レベル' if language=='ja' else 'level') in a.cmd('RIGHTS')
                for command in ['CHAT','MAIL']:
                    assert ('無効' if language=='ja' else 'disabled') in a.cmd(command)
                c.cmd('REGISTER pending'+language,'> ')
                assert ('承認' if language=='ja' else 'approval') in c.cmd(PASSWORD)
                assert ('登録申請' if language=='ja' else 'Pending applications') in a.cmd('APPLICATIONS')
                assert 'LEVEL' in a.cmd('HELP ADMINMEMBERS')
            finally:a.close();c.close();s.close()
        # A rights record with an invalid checksum must never grant access.
        original=users.read_bytes();users.write_bytes(original[:-1]+bytes([original[-1]^1]))
        result=subprocess.run([binary,'0',str(data),str(users),str(catalog)],capture_output=True,timeout=10)
        assert result.returncode!=0 and users.read_bytes()!=original
        users.write_bytes(original)
        bad=bytearray(original);offset=next(i for i in range(8,len(bad),128) if bad[i]==ord('L'));bad[offset+8]=6
        checksum=2166136261
        for byte in bad[offset:offset+124]:checksum=((checksum^byte)*16777619)&0xffffffff
        struct.pack_into('<I',bad,offset+124,checksum);users.write_bytes(bad)
        result=subprocess.run([binary,'0',str(data),str(users),str(catalog)],capture_output=True,timeout=10)
        assert result.returncode!=0 and users.read_bytes()==bad
        users.write_bytes(original)
        if os.name=='posix':
            import fault_io as resource
            limit=len(original)+64
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,members=users,admins=admins,catalog=catalog,preexec_fn=size_limit,options=["--user-log","-"]);a=Client(s.port)
            try:auth(a,'alice');cmd(a,'LEVEL 2 1',b'ERR member storage unavailable')
            finally:a.close();s.close()
            assert users.stat().st_size==limit
            result=subprocess.run([binary,'0',str(data),str(users),str(catalog)],capture_output=True,timeout=10)
            assert result.returncode!=0
            users.write_bytes(original)
        for invalid in ['l6','g0','g101','l2+','l2+l3','g1+g1','l2,g1']:
            catalog.write_text('1|0|'+invalid+'|member|General\n')
            result=subprocess.run([binary,'0',str(data),str(users),str(catalog)],capture_output=True,timeout=10)
            assert result.returncode!=0,invalid
        print('PASS: levels 0..5, groups 1..32, combined and ancestor ACLs, read/write distinction, revocation, persistence, approval and closed registration, disabled features and malformed storage/config rejection')

if __name__=='__main__':run(sys.argv[1])



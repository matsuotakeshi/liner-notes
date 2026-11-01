"""12,000 articles plus 12,000 mails: body loading and bounded RSS growth."""
from pathlib import Path
import json,os,subprocess,sys,tempfile,time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_mail import send,read as mail_read
from test_stage58 import cmd,rows

def rss(process):
    if os.name=='nt':
        import ctypes
        from ctypes import wintypes
        class Counters(ctypes.Structure):
            _fields_=[('cb',wintypes.DWORD),('faults',wintypes.DWORD)]+[(s,ctypes.c_size_t) for s in ('peak','working','pagedpeak','paged','nonpagedpeak','nonpaged','pagefile','pagefilepeak')]
        value=Counters();value.cb=ctypes.sizeof(value)
        call=ctypes.windll.psapi.GetProcessMemoryInfo;call.argtypes=[wintypes.HANDLE,ctypes.POINTER(Counters),wintypes.DWORD]
        assert call(wintypes.HANDLE(int(process._handle)),ctypes.byref(value),value.cb)
        return value.working
    return int(subprocess.check_output(['ps','-o','rss=','-p',str(process.pid)]).strip())*1024

def run(binary,generator):
    binary,generator=[str(Path(s).resolve()) for s in (binary,generator)]
    with tempfile.TemporaryDirectory(prefix='memory63-') as tmp:
        p=Path(tmp);s=Server(binary,p/'notes',members=p/'members');a=Client(s.port);b=Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True);baseline=rss(s.proc)
        finally:a.close();b.close();s.close()
        subprocess.run([generator,str(p/'notes'),str(p/'members.mail'),'12000'],check=True)
        s=Server(binary,p/'notes',members=p/'members');a=Client(s.port);b=Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob');loaded=rss(s.proc)
            assert loaded-baseline<48*1024*1024,(baseline,loaded)
            assert read(a,12000)[2].startswith('scaled-body')
            assert b'scaled-mail' in mail_read(b,12000)
            assert any(b'scaled' in x for x in rows(a,'FIND ALL * scaled-body'))
            assert post(a,'after','body',12000)==12001
            assert send(a,'bob','after','body')==12001
            assert cmd(a,'SEEN 12000')==b'SEEN 12000'
        finally:a.close();b.close();s.close()
        print(json.dumps(dict(articles=12000,mails=12000,body_bytes=4096,baseline_rss=baseline,loaded_rss=loaded,growth=loaded-baseline)))
    print('PASS stage63 memory: body storage on disk; 12000 article/mail read/search/reply/send/seen; RSS growth <48MiB')

if __name__=='__main__':run(*sys.argv[1:])

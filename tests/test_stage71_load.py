"""Bounded mixed workload; duration is explicit and results include the actual scope."""
from pathlib import Path
import concurrent.futures,hashlib,json,os,subprocess,sys,tempfile,threading,time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_mail import send
from test_stage58 import cmd,rows
from test_stage63_memory import rss
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from transfer_connectors import LineConnector

def run(binary,ctl,seconds,mode):
    binary,ctl=map(lambda x:str(Path(x).resolve()),(binary,ctl))
    with tempfile.TemporaryDirectory(prefix='mixed71-') as tmp:
        p=Path(tmp);vault=p/'vault';subprocess.run([ctl,'init',vault],check=True,capture_output=True)
        options=['--database-structured' if mode=='structured' else '--database',str(p/'liner.sqlite'),'--idle-seconds','0','--max-articles','0','--max-mail','0','--max-user-posts','0']
        s=Server(binary,p/'notes',members=p/'members',vault=vault,options=options)
        clients=[];idle=[];failures=[];stop=threading.Event();counts=dict(posts=0,mails=0,searches=0,uploads=0,downloads=0);lock=threading.Lock()
        try:
            for _ in range(128):clients.append(Client(s.port))
            assert b'PONG'==cmd(clients[-1],'PING')
            for c in clients:c.close()
            clients=[]
            for i in range(32):
                c=Client(s.port);auth(c,'load'+str(i),True);clients.append(c)
            for _ in range(32):idle.append(Client(s.port))
            baseline=rss(s.proc);peak=baseline;deadline=time.monotonic()+seconds
            def count(key):
                with lock:counts[key]+=1
            def writer(i):
                c=clients[i];last=0
                try:
                    while time.monotonic()<deadline and not stop.is_set():
                        ident=post(c,'load71 '+str(i),'payload '+str(i));assert ident>last;last=ident;count('posts')
                        assert read(c,ident)[2]=='payload '+str(i)+'\r\n'
                        assert send(c,'load'+str(i),'load71 private','private '+str(i))>0;count('mails')
                        assert cmd(c,'MAIL EXIT')==b'MAIL CLOSED'
                        assert rows(c,'FIND ALL * load71');count('searches')
                        assert cmd(c,'SEEN '+str(ident))==('SEEN '+str(ident)).encode()
                        time.sleep(.1)
                except BaseException as error:
                    with lock:failures.append(repr(error))
                    stop.set()
            def transfer(i):
                # Release the registration socket before opening this owner's transfer socket.
                clients[i].close();c=None
                try:
                    c=LineConnector('127.0.0.1',s.port,'load'+str(i),PASSWORD)
                    payload=bytes(range(256))*256;digest=hashlib.sha256(payload).hexdigest()
                    while time.monotonic()<deadline and not stop.is_set():
                        ident=c.begin(len(payload),digest,'load71-'+str(i)+'.bin','load test')
                        offset=0
                        while offset<len(payload):offset=c.write(ident,offset,payload[offset:offset+c.chunk_size])
                        assert c.finish(ident)==(len(payload),digest);count('uploads')
                        from transfer_connectors import download_begin,download_read,download_done
                        size,expected=download_begin(c,ident);data=bytearray()
                        while len(data)<size:data.extend(download_read(c,ident,len(data)))
                        assert hashlib.sha256(data).hexdigest()==expected==digest;download_done(c,ident);count('downloads')
                        time.sleep(.5)
                except BaseException as error:
                    with lock:failures.append(repr(error))
                    stop.set()
                finally:
                    if c:c.close()
            start=time.monotonic()
            with concurrent.futures.ThreadPoolExecutor(max_workers=32) as pool:
                jobs=[pool.submit(writer,i) for i in range(24)]+[pool.submit(transfer,i) for i in range(24,32)]
                while any(not j.done() for j in jobs):
                    peak=max(peak,rss(s.proc));time.sleep(1)
                for j in jobs:j.result()
            elapsed=time.monotonic()-start
            assert not failures,failures
            assert min(counts.values())>0,counts
            assert peak-baseline<128*1024*1024,(baseline,peak)
            print(json.dumps(dict(mode=mode,seconds=round(elapsed,2),peak_connections_checked=128,
                                  mixed_connections=64,posting_clients=24,transfer_clients=8,
                                  baseline_rss=baseline,peak_rss=peak,counts=counts)))
        finally:
            stop.set()
            for c in clients+idle:c.close()
            s.close()
        s=Server(binary,p/'notes',members=p/'members',vault=vault,options=options);c=Client(s.port)
        try:auth(c,'load0');assert rows(c,'FIND ALL * load71')
        finally:c.close();s.close()
    print('PASS mixed post/read/search/mail/upload/download, 128 connection admission, bounded memory and clean restart')

if __name__=='__main__':run(sys.argv[1],sys.argv[2],int(sys.argv[3]),sys.argv[4])

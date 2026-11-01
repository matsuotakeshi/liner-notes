"""Expanded account/board/file/line/connection limits, real persisted upper IDs."""
import pathlib,sys,tempfile,subprocess,struct,hashlib,socket
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_limits import checksum
from test_mail import send,read as mail_read
from test_talk import command
from test_members import PASSWORD
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/"scripts"))
from transfer_connectors import MailConnector

def run(binary,ctl):
 binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
 with tempfile.TemporaryDirectory(prefix='stage57-limit-') as d:
  p=pathlib.Path(d);board=p/'board';users=p/'users';vault=p/'vault';catalog=p/'catalog';admins=p/'admins'
  s=Server(binary,board,members=users);a=Client(s.port)
  try:auth(a,'alice',True)
  finally:a.close();s.close()
  raw=users.read_bytes();template=raw[8:136];rows=bytearray(raw[:8])
  for n in range(1,10000):
   row=bytearray(template);struct.pack_into('<I',row,4,n);row[8:40]=b'\0'*32;name=('alice' if n==1 else f'u{n:04d}').encode();row[8:8+len(name)]=name;checksum(row);rows+=row
  users.write_bytes(rows);catalog.write_text(''.join(f'{n}|0|all|member|Board{n}\n' for n in range(1,1025)));admins.write_text('9999\n')
  s=Server(binary,board,members=users,catalog=catalog,admins=admins);a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'u9999');auth(b,'alice');command(a,'MEMBER 9999',b'MEMBER 9999')
   a.send(b'BOARD 1024\n');assert b'1024' in a.line()
   assert post(a,'long-line','x'*4096)==1;assert read(a,1)[2]=='x'*4096+'\r\n'
   command(a,'SEEN 1',b'SEEN 1');assert send(a,'alice','high-sender','body')==1;assert b'u9999' in mail_read(b,1)
   command(a,'MAIL EXIT',b'MAIL CLOSED');command(a,'LOCATION upper-account',b'PROFILE SAVED')
   a.send(b'PROFILEID 9999\n');out=[]
   while True:
    row=a.line();out.append(row)
    if row==b'END PROFILE':break
   assert b'LOCATION upper-account' in out
   c=Client(s.port);c.send(b'REGISTER overflow\n');c.line();c.send(b'password-long\n');assert c.line().startswith(b'LOGGED IN overflow');c.close()
   clients=[Client(s.port) for _ in range(126)]
   for c in clients:c.send(b'PING\n');c.expect(b'PONG')
   excess=socket.create_connection(('127.0.0.1',s.port));excess.settimeout(2);assert excess.recv(1)==b'';excess.close()
   for c in clients:c.close()
  finally:a.close();b.close();s.close()
  # New maximum file ID survives loading, reads, delete and reload without reuse.
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True);source=p/'tiny';source.write_bytes(b'x')
  subprocess.run([ctl,'import',str(vault),str(source),'x',''],check=True,capture_output=True)
  raw=(vault/'index.dat').read_bytes();template=raw[8:648];rows=bytearray(raw[:8])
  for n in range(1,2001):
   row=bytearray(template);struct.pack_into('<I',row,4,n);row[608:]=hashlib.sha256(row[:608]).digest();rows+=row
  (vault/'index.dat').write_bytes(rows);(vault/'00002000.bin').write_bytes(b'x')
  out=subprocess.run([ctl,'export',str(vault),'2000',str(p/'exported')],capture_output=True);assert out.returncode==0,out.stderr
  private=pathlib.Path(str(users)+'.mailfiles');subprocess.run([ctl,'init',str(private)],check=True,capture_output=True)
  private_rows=bytearray(rows[:8])
  for n in range(1,2001):
   row=bytearray(rows[8+(n-1)*640:8+n*640]);publisher=b'mail-member:9999';row[444:524]=b'\0'*80;row[444:444+len(publisher)]=publisher;struct.pack_into('<I',row,524,len(publisher));row[608:]=hashlib.sha256(row[:608]).digest();private_rows+=row
  (private/'index.dat').write_bytes(private_rows);(private/'00002000.bin').write_bytes(b'x')
  s=Server(binary,board,members=users,catalog=catalog);a=Client(s.port);b=Client(s.port);api=MailConnector('127.0.0.1',s.port,'u9999',PASSWORD);recipient=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
  try:
   auth(a,'u9999');auth(b,'alice');assert api.info(2000)==(1,hashlib.sha256(b'x').hexdigest())
   command(a,'MAIL SEND alice upper-attachment',b'MAIL EDIT');command(a,'/ATTACH 2000',b'MAIL ATTACHMENT 2000');a.send(b'body\n.\n');a.expect(b'MAIL SENT 2')
   assert recipient.read(2000,0)==b'x';assert b'2000' in mail_read(b,2)
  finally:api.close();recipient.close();a.close();b.close();s.close()

  subprocess.run([ctl,'delete',str(vault),'2000','CONFIRM'],check=True,capture_output=True)
  denied=subprocess.run([ctl,'import',str(vault),str(source),'x',''],capture_output=True);assert denied.returncode!=0
  # Physical capacity is reclaimed; exactly 256 MiB succeeds, one extra byte fails.
  large=p/'large';limit=256*1024*1024
  with large.open('wb') as f:f.truncate(limit)
  store=p/'large-vault';subprocess.run([ctl,'init',str(store)],check=True,capture_output=True)
  (store/'limits.conf').write_text('max_bytes=268435456\n')
  out=subprocess.run([ctl,'import',str(store),str(large),'large.bin',''],capture_output=True);assert out.returncode==0,out.stderr
  assert (store/'00000001.bin').stat().st_size==limit
  assert subprocess.run([ctl,'import',str(store),str(source),'over.bin',''],capture_output=True).returncode!=0
  subprocess.run([ctl,'delete',str(store),'1','CONFIRM'],check=True,capture_output=True)
  out=subprocess.run([ctl,'import',str(store),str(source),'next.bin',''],capture_output=True);assert out.returncode==0 and out.stdout.startswith(b'2 '),out.stdout
  with large.open('ab') as f:f.write(b'x')
  assert subprocess.run([ctl,'import',str(store),str(large),'too-large',''],capture_output=True).returncode!=0
 print('PASS: member 9999, board 1024, 4096-byte input, 128 connections, file/attachment ID 2000, 256MiB file/capacity boundary and physical reclamation; monotonic IDs')
if __name__=='__main__':run(*sys.argv[1:])

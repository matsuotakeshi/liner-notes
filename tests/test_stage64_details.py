"""Stage64 actual 256 KiB content, registration review, readers and timed editor."""
from pathlib import Path
import sys,tempfile,time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_mail import send,read as read_mail
from test_stage58 import cmd,rows

def until(c,marker):
 c.sock.settimeout(15)
 while marker not in c.buf:
  
  try:data=c.sock.recv(8192)
  except TimeoutError:raise AssertionError(('waiting for',marker,'received',c.buf[:300],c.buf[-200:]))
  assert data,c.buf;c.buf+=data
 at=c.buf.index(marker)+len(marker);out,c.buf=c.buf[:at],c.buf[at:];return out

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='stage64-details-') as d:
  p=Path(d);board=p/'notes';members=p/'members';admins=p/'admins'
  s=Server(binary,board,members=members,language='en');a=Client(s.port);b=Client(s.port)
  body='\n'.join(['x'*4094]*63+['y'*4085+'END-LARGE']);expected=body.replace('\n','\r\n')+'\r\n';assert len(expected)==262144
  try:
   auth(a,'admin',True);auth(b,'alice',True)
   literal='FIND all MiXeD '+'x'*(4096-len('FIND all MiXeD '));assert post(a,'input4096',literal)==1;assert read(a,1)[2]==literal+'\r\n'
   assert post(a,'large',body)==2;assert read(a,2)[2]==expected
   assert cmd(a,'POST overflow').startswith(b'EDIT');a.send((body+'\nextra\n').encode());assert a.line().startswith(b'ERR body limit 262144')
   assert cmd(a,'/SAVE').startswith(b'DRAFT STORED');assert cmd(a,'/CANCEL')==b'CANCELLED'
   assert send(a,'alice','large-mail',body)==1;assert b'END-LARGE' in read_mail(b,1)
   assert send(a,'alice','second','secondbody')==2
   b.send(b'MAIL SCAN DESC\n');assert b'MAIL ITEM 2' in until(b,b'MAIL END\r\n')
   b.send(b'-1\n');assert b'MAIL ITEM 1' in until(b,b'MAIL END\r\n')
   assert cmd(b,'CONTINUE')==b'MAIL SCAN END'
   assert cmd(b,'MAIL GET 1')==b'MAIL IMPORTED TO BUFFER';cmd(b,'MAIL EXIT')
   assert cmd(b,'POST imported').startswith(b'EDIT');assert cmd(b,'/BUFFER')==b'BUFFER IMPORTED';assert cmd(b,'.')==b'SAVED 3';assert read(b,3)[2]==expected
   # SCAN preserves its starting subtree while it moves to a child board.
   a.send(b'MAIL EXIT\n');a.line();a.send(b'SCAN DESC ALL * *\n');assert b'ARTICLE 3' in until(a,b'END\r\n')
   a.send(b'-2\n');assert b'ARTICLE 1' in until(a,b'END\r\n');assert cmd(a,'CONTINUE')==b'SCAN END'
   # Receive mode ends on idle and never posts without confirmation.
   assert cmd(a,'EDITOR LINE')
   assert cmd(a,'POST receive').startswith(b'EDIT');a.send(b'U\n');assert b'Receiving text' in a.line()
   a.send(b'Mixed Needle Needle\n');until(a,b'Text received. Continue editing.\r\n')
   a.send(b'S Needle\n');assert b'Mixed Needle Needle' in a.line();assert a.line()==b'Matches: 2'
   a.send(b'0\n');assert b'W: write' in until(a,b'> ')
   assert cmd(a,'w')==b'SAVED 4';assert read(a,4)[2]=='Mixed Needle Needle\r\n'
  finally:a.close();b.close();s.close()
  admins.write_text('1\n');opts=['--registration','approval','--id-mode','auto','--registration-form','detailed']
  s=Server(binary,board,members=members,admins=admins,language='en',options=opts);a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'admin');b.send(b'REGISTER\n');until(b,b': ');assert cmd(b,'/cAnCeL')==b'CANCELLED';b.send(b'REGISTER\n')
   fields=['Jane Doe','Kana','Private Street','012345','2000-02-29','X68000','Handle','Tokyo','Coder','Music','https://example.org','Contact','Other BBS','Private note']
   for value in fields:until(b,b': ');b.send((value+'\n').encode())
   review=until(b,b'> ');assert b'Jane Doe' in review and b'Y:confirm' in review;b.send(b'Y\n')
   b.expect(b'PASSWORD');assert cmd(b,PASSWORD)==b'APPLICATION SAVED u000003'
   details=rows(a,'APPLICATION 3',b'END APPLICATION');assert b'REALNAME Jane Doe' in details and b'SYSOPNOTE Private note' in details,details
   assert cmd(a,'APPLICATIONSET 3 MACHINE FreeBSD')==b'PROFILE SAVED'
   assert cmd(a,'APPLICATIONSET 3 SYSOPNOTE Corrected note')==b'APPLICATION SAVED'
   assert cmd(a,'APPROVE 3').startswith(b'RIGHTS')
   time.sleep(1.05);auth(b,'u000003');assert b'MACHINE FreeBSD' in rows(b,'PROFILE',b'END PROFILE')
   assert cmd(b,'APPLICATION 3')==b'ERR admin required'
   assert cmd(a,'RESUME').startswith(b'DRAFT RESUMED');assert cmd(a,'.')==b'SAVED 5'
   assert read(a,5)[2]==expected
  finally:a.close();b.close();s.close()
  s=Server(binary,board,members=members,admins=admins,language='en');a=Client(s.port)
  try:
   auth(a,'u000003');assert b'MACHINE FreeBSD' in rows(a,'PROFILE',b'END PROFILE');assert read(a,5)[2]==expected
  finally:a.close();s.close()
 print('PASS stage64 4096-byte input, 256KiB article/mail/draft/buffer, reverse/relative reads, timed U, confirmation, generated IDs, detailed application and review/restart')
if __name__=='__main__':run(sys.argv[1])

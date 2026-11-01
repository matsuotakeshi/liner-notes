"""RTBBS follow-ups: scopes, privacy opt-in, copy, DOS matching, protocol replay."""
from pathlib import Path
import sys,tempfile,subprocess,hashlib,time
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd,rows
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from transfer_connectors import LineConnector

def run(binary,ctl):
 binary,ctl=map(lambda x:str(Path(x).resolve()),(binary,ctl))
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp);vault=p/'vault';users=p/'users';board=p/'notes';admins=p/'admins'
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
  s=Server(binary,board,members=users)
  for name in ('admin','sigop','alice','bob'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n')
  def start(policy=False):return Server(binary,board,members=users,admins=admins,vault=vault,options=['--rtbbs-member-policy'] if policy else [])
  s=start();clients=[Client(s.port) for _ in range(4)];a,g,u,b=clients
  try:
   for c,name in zip(clients,('admin','sigop','alice','bob')):auth(c,name)
   for spec in ('2|1|member|member|Forum|info','3|2|l2+g10|l2+g10|Child|info','4|1|member|member|Outside|info'):
    assert cmd(a,'BOARDSET '+spec).startswith(b'BOARD UPDATED')
   assert cmd(a,'SIGOP 2 2').startswith(b'SIGOP SET')
   g.close();g=Client(s.port);clients[1]=g;auth(g,'sigop')
   assert cmd(g,'BOARDSET 5|2|member|member|New|info').startswith(b'BOARD UPDATED')
   assert cmd(g,'BOARDSET 3|2|l2+g10|l2+g10|Renamed|info').startswith(b'BOARD UPDATED')
   assert cmd(g,'BOARDSET 4|2|member|member|Stolen|info').startswith(b'ERR')
   assert cmd(g,'BOARDSET 3|4|member|member|Escape|info').startswith(b'ERR')
   assert cmd(g,'SIGOP 5 4').startswith(b'SIGOP SET')
   assert cmd(g,'SIGOP 2 2').startswith(b'ERR')
   assert cmd(g,'SIGOP 4 2').startswith(b'ERR')
   assert cmd(g,'BOARD 3').startswith(b'BOARD 3')
   assert cmd(g,'GROUP 3 10').startswith(b'RIGHTS 3')
   assert cmd(g,'GROUP 3 11').startswith(b'ERR')
   assert cmd(g,'GROUP 1 10').startswith(b'ERR')
   u.close();u=Client(s.port);clients[2]=u;auth(u,'alice')
   for path in ('-','files','files/child','target','other'):
    assert cmd(a,'FILEBOARDSET '+path+'|member|member')==b'FILEBOARD UPDATED'
   for path in ('files','target'):assert cmd(a,'FILEBOARDSIGOP '+path+'|2')==b'FILEBOARD UPDATED'
   assert cmd(g,'FILEBOARDSET files/new|member|member')==b'FILEBOARD UPDATED'
   assert cmd(g,'FILEBOARDSIGOP files/child|4')==b'FILEBOARD UPDATED'
   assert cmd(g,'FILEBOARDSIGOP files|4')==b'ERR vault denied'
   assert cmd(g,'FILEBOARDSET other|none|none')==b'ERR vault denied'
   api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
   ids=[]
   try:
    for path,name in [('files','README'),('files','FOO.TXT'),('files/child','CHILD.TXT'),('other','OUT.TXT')]:
     # The upload category API and protocol metadata must coexist.
     api.upload_category=path
     data=('payload '+name).encode();ident=api.begin(len(data),hashlib.sha256(data).hexdigest(),name,'description '+name);api.write(ident,0,data);api.finish(ident);ids.append(ident)
   finally:api.close()
   assert ids==[1,2,3,4],ids
   assert cmd(g,'FILEASCII 1 ON')==b'FILE ATTRIBUTE CHANGED 1'
   assert cmd(g,'FILECOPY 1 other')==b'ERR vault denied'
   assert cmd(g,'FILECOPY 1 target')==b'FILE COPIED 1 5'
   details=rows(u,'FILE 5',b'END FILE');assert b'ASCII ON' in details and b'PROTOCOL COPY' in details,details
   assert cmd(u,'FILECOPY 1 target')==b'ERR vault denied'
   assert rows(u,'FILEBOARD files',b'END FILEBOARD')[0].startswith(b'FILEBOARDSETTING')
   assert cmd(u,'BATCH MATCH *.*')==b'BATCH 2 1,2'
   assert cmd(u,'BATCH MATCH F??.TXT')==b'BATCH 1 2'
   assert cmd(u,'BATCH MATCH F*.T??')==b'BATCH 1 2'
   assert cmd(u,'BATCH MATCH README.???')==b'BATCH 1 1'
   assert cmd(u,'PROFILESET ADDRESS private-address')==b'PROFILE SAVED'
   assert cmd(u,'PROFILESET BIRTHDAY 2000-01-02')==b'PROFILE SAVED'
   assert b'ADDRESS private-address' not in rows(g,'PROFILEID 3',b'END PROFILE')
   assert cmd(u,'USERLOG 2').startswith(b'ERR')
   result=rows(u,'PROFILEFIND GROUP 10');assert any(b'alice' in x for x in result),result
  finally:
   for c in clients:c.close()
   s.close()
  s=start(True);a,g,u=[Client(s.port) for _ in range(3)]
  try:
   for c,name in zip((a,g,u),('admin','sigop','alice')):auth(c,name)
   assert cmd(u,'PROFILESET ADDRESS changed').startswith(b'ERR profile administrator')
   assert cmd(u,'PROFILESET BIRTHDAY 2001-01-02').startswith(b'ERR profile administrator')
   assert cmd(u,'PROFILEFIND GROUP 10')==b'ERR profile search permission'
   assert cmd(g,'PROFILEFIND ADDRESS private')==b'ERR profile search permission'
   assert any(b'alice' in x for x in rows(g,'PROFILEFIND GROUP 10'))
   assert b'ADDRESS private-address' in rows(u,'PROFILE',b'END PROFILE')
   assert b'ADDRESS private-address' not in rows(g,'PROFILEID 3',b'END PROFILE')
   assert cmd(a,'MEMBERPROFILE 3 ADDRESS admin-change')==b'PROFILE SAVED'
   assert rows(u,'USERLOG 2')[0].startswith(b'USERLOG')
   assert b'PROTOCOL LINE' in rows(u,'FILE 1',b'END FILE')
   assert b'PROTOCOL COPY' in rows(u,'FILE 5',b'END FILE')
   assert cmd(u,'BATCH ADD 1')==b'BATCH 1 1';u.expect(b'BATCH FILE 1 PENDING')
   assert cmd(u,'LOGOUT')==b'LOGGED OUT';time.sleep(1.05);auth(u,'alice')
   assert cmd(u,'BATCH LIST')==b'BATCH 0 -'
  finally:a.close();g.close();u.close();s.close()
  output=p/'compacted'
  subprocess.run([ctl,'compact-index',str(vault),str(output)],check=True,capture_output=True)
 print('PASS stage69 role boundaries, implicit groups, privacy opt-in, current-board DOS match, copy and protocol replay/compact')

if __name__=='__main__':run(*sys.argv[1:])

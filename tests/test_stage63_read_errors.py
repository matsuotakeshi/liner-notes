"""A disk body read error must not produce a successful partial search/import."""
from pathlib import Path
import os,sys,tempfile
from test_board import Server,post
from test_runtime import Client
from test_members import auth
from test_mail import send
from test_stage58 import cmd


def run(binary):
    if os.name != 'posix':
        print('SKIP: forced external truncation requires POSIX advisory locks');return
    binary=str(Path(binary).resolve())
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)
        for kind in ('article','mail','draft'):
            base=p/kind;base.mkdir()
            s=Server(binary,base/'notes',members=base/'members');a=Client(s.port);b=Client(s.port)
            try:
                auth(a,'alice',True);auth(b,'bob',True)
                if kind=='article':post(a,'title','body-token')
                elif kind=='mail':send(a,'bob','title','body-token')
                else:
                    assert cmd(a,'MAIL SEND bob draft').startswith(b'MAIL EDIT')
                    a.send(b'body-token\n');assert cmd(a,'/SAVE')==b'MAIL DRAFT SAVED'
                # Simulate an external operator/disk fault ignoring the store lock.
                path=base/('notes' if kind=='article' else 'members.mail')
                with path.open('r+b') as f:f.truncate(8)
                result=cmd(b if kind=='mail' else a,{'article':'FIND ALL * body-token','mail':'MAIL SEARCH body-token','draft':'MAIL RESUME'}[kind])
                assert result==b'ERR storage unavailable',result
                if kind!='article':assert cmd(a,'MAIL 0')==b'MAIL CLOSED'
                assert cmd(a,'PING')==b'PONG'
            finally:a.close();b.close();s.close()
    print('PASS lazy body faults: article/mail searches reject disk read failure; peer commands survive')


if __name__=='__main__':run(sys.argv[1])

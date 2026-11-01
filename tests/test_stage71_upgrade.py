"""Create real 0.70 schema-2 data; open with 0.71 without an implicit migration."""
from pathlib import Path
import sqlite3,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows

def run(old,new):
    old,new=map(lambda p:str(Path(p).resolve()),(old,new))
    with tempfile.TemporaryDirectory(prefix='upgrade71-') as tmp:
        p=Path(tmp);db=p/'liner.sqlite';options=['--database-structured',str(db)]
        for binary,first in ((old,True),(new,False),(old,False)):
            s=Server(binary,p/'notes',members=p/'members',options=options);c=Client(s.port)
            try:
                auth(c,'alice',first)
                if first:
                    assert post(c,'Original','kept through update')==1
                    assert cmd(c,'PROFILESET HANDLE Original Handle')==b'PROFILE SAVED'
                else:
                    assert read(c,1)[2]=='kept through update\r\n'
                    assert any(b'Original Handle' in x for x in rows(c,'PROFILE',b'END PROFILE'))
                    if binary==new:assert post(c,'New','still schema two')==2
                    else:assert read(c,2)[2]=='still schema two\r\n'
            finally:c.close();s.close()
            with sqlite3.connect(db) as d:
                assert d.execute('SELECT version FROM storage_meta').fetchone()==(2,)
                assert not d.execute("SELECT name FROM sqlite_master WHERE name='profile_events'").fetchone()
    print('PASS actual 0.70 -> 0.71 -> 0.70 schema-2 reader compatibility, posts/profile preserved, no automatic schema change')

if __name__=='__main__':run(*sys.argv[1:])

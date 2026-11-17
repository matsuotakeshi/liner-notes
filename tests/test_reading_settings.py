"""SQLite/socket regression for private checkpoints and persistent menu guidance."""
from contextlib import closing, ExitStack
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from test_board import Server, post
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd


def check(client, command):
    client.send((command+"\n").encode())
    response=[]
    for _ in range(200):
        line=client.line();response.append(line)
        assert not line.startswith(b"ERR "), response
        if line.startswith(b"CHECK NEXT /"):
            return response
    raise AssertionError(response)


def run(binary,vaultctl):
    binary,vaultctl=(str(Path(path).resolve()) for path in (binary,vaultctl))
    with tempfile.TemporaryDirectory(prefix="reading-settings-") as directory:
        root=Path(directory);database=root/"liner.sqlite";admins=root/"admins"
        # Runtime deliberately refuses a missing/uninitialized vault.
        subprocess.run([vaultctl,"init",str(root/"vault")],check=True,capture_output=True)
        def start():
            return Server(binary,root/"notes",members=root/"members",language="en",vault=root/"vault",
                          admins=admins if admins.exists() else None,options=["--database",str(database)])
        def sql(statement,params=()):
            with closing(sqlite3.connect(database)) as db:
                result=db.execute(statement,params).fetchall();db.commit();return result
        with closing(start()) as server:
            for name in ("admin","alice","bob"):
                with closing(Client(server.port)) as client:auth(client,name,True)
        admins.write_text("1\n",encoding="utf-8")
        with closing(start()) as server, ExitStack() as stack:
            admin,alice,bob,guest=[stack.enter_context(closing(Client(server.port))) for _ in range(4)]
            for client,name in ((admin,"admin"),(alice,"alice"),(bob,"bob")):auth(client,name)
            assert cmd(guest,"CHECK ARTICLES")==b"ERR login required"
            assert cmd(guest,"MENUMODE EXPERT")==b"MENU MODE CHANGED"
            assert sql("SELECT * FROM menu_preferences")==[]
            for mode,number in (("BEGINNER",0),("STANDARD",1),("EXPERT",2)):
                assert cmd(alice,"MENUMODE "+mode)==b"MENU MODE SAVED"
                assert sql("SELECT value FROM menu_preferences WHERE id=2")==[(number,)]
            assert cmd(alice,"PREFS SAVE").startswith(b"PREFS SAVED")
            assert cmd(bob,"MENUMODE BEGINNER")==b"MENU MODE SAVED"
            assert cmd(alice,"PREFS LOAD").startswith(b"PREFS LOADED")
            assert sql("SELECT id,value FROM menu_preferences ORDER BY id")==[(2,2),(3,0)]
            for board,parent in ((2,1),(3,2)):
                assert cmd(admin,f"BOARDSET {board}|{parent}|all|member|Board{board}|scope")==f"BOARD UPDATED {board}".encode()
            for path in ("files","files/child"):
                assert cmd(admin,f"FILEBOARDSET {path}|member|member")==b"FILEBOARD UPDATED"
            assert cmd(alice,"BOARD 2").startswith(b"BOARD 2")
            post(alice,"checkpoint source","preserve article")
            assert sql("SELECT * FROM news")==[]
            assert sql("SELECT * FROM news_scoped")==[]
            check(alice,"CHECK ARTICLES HERE")
            assert sql("SELECT * FROM news_scoped")==[]  # begin is read-only
            assert cmd(alice,"CHECK CANCEL")==b"CHECK CANCELLED"
            assert sql("SELECT * FROM news_scoped")==[]
            check(alice,"CHECK ARTICLES HERE")
            assert cmd(alice,"CHECK DONE")==b"CHECKPOINT SAVED"
            original=sql("SELECT stamp FROM news_scoped WHERE user=2 AND kind=0 AND scope='2:0'")[0][0]
            assert original>0
            for command in ("CHECK ARTICLES SUBTREE","CHECK ARTICLES ALL","CHECK MAIL","CHECK FILES","CHECK FILES files"):
                check(alice,command)
                assert cmd(alice,"CHECK DONE")==b"CHECKPOINT SAVED"
            assert sql("SELECT kind FROM news WHERE user=2 ORDER BY kind")==[(0,),(1,),(2,)]
            assert sql("SELECT kind,scope FROM news_scoped WHERE user=2 ORDER BY kind,scope")==[(0,"2:0"),(0,"2:2"),(2,"files")]
            assert sql("SELECT * FROM news WHERE user=3")==[]
            # Disconnect an unfinished scope: no implicit commit in RAW/manual mode.
            with closing(Client(server.port)) as peer:
                auth(peer,"alice");check(peer,"CHECK FILES files/child")
            assert sql("SELECT * FROM news_scoped WHERE scope='files/child'")==[]
            snapshots=sql("SELECT user,kind,stamp FROM news ORDER BY user,kind")
            scoped=sql("SELECT user,kind,scope,stamp FROM news_scoped ORDER BY user,kind,scope")
        with closing(start()) as server, closing(Client(server.port)) as alice:
            auth(alice,"alice")
            assert sql("SELECT user,kind,stamp FROM news ORDER BY user,kind")==snapshots
            assert sql("SELECT user,kind,scope,stamp FROM news_scoped ORDER BY user,kind,scope")==scoped
            assert sql("SELECT value FROM menu_preferences WHERE id=2")==[(2,)]
            assert cmd(alice,"PREFS LOAD").startswith(b"PREFS LOADED")
            # Corrupt only the isolated test DB; failed reads must not start a scan.
            sql("UPDATE news SET stamp='invalid' WHERE user=2 AND kind=0")
            assert cmd(alice,"CHECK ARTICLES")==b"ERR settings storage"
            assert sql("SELECT stamp FROM news WHERE user=2 AND kind=0")==[("invalid",)]
            sql("UPDATE news SET stamp=? WHERE user=2 AND kind=0",(snapshots[0][2],))
            check(alice,"CHECK ARTICLES")
            assert cmd(alice,"CHECK CANCEL")==b"CHECK CANCELLED"
            sql("UPDATE menu_preferences SET value=99 WHERE id=2")
            assert cmd(alice,"PREFS LOAD")==b"ERR settings storage"
            assert cmd(alice,"MENUMODE STANDARD")==b"MENU MODE SAVED"
            assert cmd(alice,"PREFS LOAD").startswith(b"PREFS LOADED")
            sql("DROP TABLE news")
            assert cmd(alice,"CHECK ARTICLES")==b"ERR settings storage"
            assert cmd(alice,"PING")==b"PONG"
    print("PASS private reading scopes/menu modes, cancel/disconnect, restart and failed-storage replies")


if __name__=="__main__":run(sys.argv[1],sys.argv[2])

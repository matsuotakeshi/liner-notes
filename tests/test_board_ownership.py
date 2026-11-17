"""Management Core socket regression: delegated authority and durable ownership."""
from contextlib import closing, ExitStack
from pathlib import Path
import sys
import tempfile
from test_board import Server, post, read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd, rows


def expect(client, command, expected):
    actual=cmd(client,command)
    assert actual==expected,(command,actual,expected)


def full(client):
    expect(client,"POST over quota",b"EDIT end with . or /CANCEL")
    client.send(b"body\n.\n")
    client.expect(b"ERR board full")


def run(binary):
    binary=str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="board-ownership-") as directory:
        root=Path(directory);admins=root/"admins"
        def start():
            return Server(binary,root/"notes",members=root/"members",language="en",
                          admins=admins if admins.exists() else None,
                          options=["--max-user-posts","1"])
        with closing(start()) as server:
            for name in ("admin","parent","child","writer"):
                with closing(Client(server.port)) as c:auth(c,name,True)
        admins.write_text("1\n",encoding="ascii")
        with closing(start()) as server, ExitStack() as stack:
            def login(name):
                c=stack.enter_context(closing(Client(server.port)));auth(c,name);return c
            a=login("admin")
            for value in ("2|1|all|all|Parent|parent","3|2|all|all|Child|child","4|1|all|all|Outside|outside"):
                result=cmd(a,"BOARDSET "+value);assert result.startswith(b"BOARD UPDATED"),result
            expect(a,"SIGOP 2 2",b"SIGOP SET 2 2")
            parent=login("parent")
            expect(parent,"SIGOP 3 3",b"SIGOP SET 3 3")
            child=login("child");writer=login("writer")
            expect(child,"SIGOP 3 4",b"ERR catalog syntax")
            expect(parent,"SIGOP 4 3",b"ERR catalog syntax")
            expect(parent,"BOARDGUEST 4 ON",b"ERR admin required")
            expect(parent,"BOARDGUEST 3 ON",b"BOARD GUEST SAVED")
            expect(child,"BOARDGUEST 3 OFF",b"BOARD GUEST SAVED")
            expect(parent,"BOARDGUEST 3 ON",b"BOARD GUEST SAVED")
            result=cmd(writer,"BOARD 3");assert result.startswith(b"BOARD 3"),result
            first=post(writer,"ownership original","unchanged body")
            full(writer)
            expect(writer,f"AUTHOR {first} 3 CONFIRM",b"ERR admin required")
            expect(parent,f"AUTHOR {first} 3 CONFIRM",f"AUTHOR CHANGED {first} 3".encode())
            second=post(writer,"ownership second","second body")
            # Assigning the same author must not add another post to their quota.
            expect(parent,f"AUTHOR {first} 3 CONFIRM",f"AUTHOR CHANGED {first} 3".encode())
            result=cmd(child,"BOARD 3");assert result.startswith(b"BOARD 3"),result
            full(child)
            result=cmd(parent,"BOARD 3");assert result.startswith(b"BOARD 3"),result
            assert any(b"AUTHOR" in line for line in rows(parent,f"AUDIT ARTICLE {first}"))
            assert read(parent,first)[2]=="unchanged body\r\n"
            # Revoke direct assignment through its parent; use a fresh login so
            # this asserts persisted authority, not transport revocation timing.
            expect(parent,"SIGOP 3 NONE",b"SIGOP SET 3 0")
            child.close();child=login("child")
            expect(child,"BOARDGUEST 3 OFF",b"ERR admin required")
            expect(child,f"AUTHOR {first} 4 CONFIRM",b"ERR admin required")
            expect(parent,f"HIDE {first}",f"HIDDEN SAVED {first}".encode())
            expect(parent,f"AUTHOR {first} 4 CONFIRM",b"ERR admin required")
            expect(parent,f"RESTORE {first}",f"RESTORED {first}".encode())
        with closing(start()) as server, ExitStack() as stack:
            writer=stack.enter_context(closing(Client(server.port)));auth(writer,"writer")
            result=cmd(writer,"BOARD 3");assert result.startswith(b"BOARD 3"),result
            full(writer)
            assert read(writer,first)[2]=="unchanged body\r\n"
            assert read(writer,second)[2]=="second body\r\n"
            child=stack.enter_context(closing(Client(server.port)));auth(child,"child")
            result=cmd(child,"BOARD 3");assert result.startswith(b"BOARD 3"),result
            full(child)
            expect(child,"BOARDGUEST 3 OFF",b"ERR admin required")
            guest=stack.enter_context(closing(Client(server.port)))
            result=cmd(guest,"BOARD 3");assert result.startswith(b"BOARD 3"),result
            post(guest,"guest after restart","guest body")
    print("PASS: delegated board management, author quota/audit and restart persistence")


if __name__=="__main__":run(sys.argv[1])

"""Real-socket document and article import regression against an isolated SQLite DB."""
from contextlib import closing, ExitStack
from pathlib import Path
import sys
import tempfile
from test_board import Server, post
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd, rows
from test_mail import send as mail_send
from test_mail_draft import info as mail_draft


def preview(client, command="BUFFER"):
    assert cmd(client, command) == b"DOCUMENT EDIT"
    result = b"\n".join(rows(client, "/PREVIEW", b"END"))
    assert cmd(client, "/CANCEL") == b"CANCELLED"
    return result


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="editor-documents-") as directory:
        root = Path(directory)
        admins = root / "admins"
        def start():
            return Server(binary, root / "notes", members=root / "members",
                          admins=admins if admins.exists() else None, language="en",
                          options=["--database", str(root / "liner.sqlite")])
        with closing(start()) as server:
            for name in ("admin", "alice", "bob"):
                with closing(Client(server.port)) as client:
                    auth(client, name, True)
        admins.write_text("1\n", encoding="utf-8")
        with closing(start()) as server, ExitStack() as stack:
            a, b, c, guest = [stack.enter_context(closing(Client(server.port))) for _ in range(4)]
            for client, name in ((a, "admin"), (b, "alice"), (c, "bob")):
                auth(client, name)
            assert cmd(guest, "BUFFER") == b"ERR login required"
            assert cmd(guest, "BIOEDIT") == b"ERR login required"
            assert cmd(a, "BOARDSET 2|1|all|u1|Read only|Import without posting") == b"BOARD UPDATED 2"
            assert cmd(a, "BOARD 2").startswith(b"BOARD 2")
            parent = post(a, "parent", "Imported parent body")
            child = post(a, "child", "Imported child body", parent)
            assert cmd(b, "BOARD 2").startswith(b"BOARD 2")
            assert cmd(b, "POST blocked") == b"ERR posting denied"
            assert cmd(b, f"GET {parent}") == b"ARTICLE IMPORTED TO BUFFER"
            assert b"Imported parent body" in preview(b)
            assert b"Imported parent body" not in preview(a)
            assert b"Imported parent body" not in preview(c)
            assert cmd(b, "BIOEDIT") == b"DOCUMENT EDIT"
            for n in range(20):
                b.send((f"Biography line {n} " + "x"*50 + "\n").encode())
            assert cmd(b, "/SAVE") == b"PROFILE SAVED"
            assert b"Biography line 19" in preview(b, "BIOEDIT")
            assert b"Imported parent body" in preview(b)
            assert cmd(a, f"HIDE {parent}").startswith(b"HIDDEN SAVED")
            assert cmd(b, f"GET {child}") == b"ERR no article"
            assert cmd(a, f"GET {child}") == b"ERR no article"  # no admin import override
            assert b"Imported parent body" in preview(b)
            assert cmd(a, f"RESTORE {parent}").startswith(b"RESTORED")
            assert cmd(b, f"GET {child}") == b"ARTICLE IMPORTED TO BUFFER"
            assert cmd(b, "BOARD 1").startswith(b"BOARD 1")
            assert cmd(b, f"GET {child}") == b"ERR no article"  # current-board scope
            assert b"Imported child body" in preview(b)
            assert cmd(b, "POST saved article draft").startswith(b"EDIT ")
            b.send(b"article draft kept\n")
            saved = cmd(b, "/SAVE")
            assert saved.startswith(b"DRAFT STORED "), saved
            assert cmd(b, "/CANCEL") == b"CANCELLED"
            assert cmd(b, "MAIL SEND bob saved mail draft").startswith(b"MAIL EDIT ")
            b.send(b"mail draft kept\n")
            assert cmd(b, "/SAVE") == b"MAIL DRAFT SAVED"
            revision = mail_draft(b)
            assert cmd(b, "MAIL EXIT") == b"MAIL CLOSED"
            # Transfer the current editor body without ending that editor.
            assert cmd(b, "BUFFER CLEAR") == b"PROFILE SAVED"
            assert cmd(b, "BUFFER") == b"DOCUMENT EDIT"
            b.send(b"New shared Japanese "+"日本語".encode()+b"\n")
            assert cmd(b, "/TOBUFFER") == b"PROFILE SAVED"
            assert cmd(b, "/CANCEL") == b"CANCELLED"
            assert "日本語".encode() in preview(b)
            assert cmd(b, "DRAFT") == saved
            assert mail_draft(b) == revision
            assert cmd(b, "MAIL EXIT") == b"MAIL CLOSED"
            # Importing private mail changes only the common buffer, not either draft.
            message = mail_send(c, "alice", "buffer source", "Private imported mail")
            assert cmd(b, f"MAIL GET {message}") == b"MAIL IMPORTED TO BUFFER"
            assert cmd(b, "MAIL EXIT") == b"MAIL CLOSED"
            assert b"Private imported mail" in preview(b)
            assert b"Private imported mail" not in preview(a)
            assert cmd(b, "DRAFT") == saved
            assert mail_draft(b) == revision
            assert cmd(b, "MAIL EXIT") == b"MAIL CLOSED"
            assert cmd(a, f"WITHDRAW {parent} CONFIRM").startswith(b"WITHDRAW SAVED")
        with closing(start()) as server, closing(Client(server.port)) as b:
            auth(b, "alice")
            assert b"Private imported mail" in preview(b)
            assert b"Biography line 19" in preview(b, "BIOEDIT")
            assert cmd(b, "DRAFT") == saved
            assert mail_draft(b) == revision
            assert cmd(b, "MAIL EXIT") == b"MAIL CLOSED"
            assert cmd(b, "BOARD 2").startswith(b"BOARD 2")
            assert cmd(b, f"GET {child}") == b"ERR no article"
            assert b"Private imported mail" in preview(b)
            assert cmd(b, "BUFFER CLEAR") == b"PROFILE SAVED"
            assert b"Private imported mail" not in preview(b)
            assert b"Biography line 19" in preview(b, "BIOEDIT")
    print("PASS editor documents: private buffer/bio, read-only imports, hidden descendants, drafts and restart")


if __name__ == "__main__":
    run(sys.argv[1])

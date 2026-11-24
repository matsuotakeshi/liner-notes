"""Interactive MMM and existing protocol on the same listener and store."""

from pathlib import Path
import sys
import tempfile
from test_board import Server
from test_telnet import Client

GATE = b"LOGIN>> "
MAIN = b"MAIN>> "
NOTES = b"NOTES>> "
INDEX = b"INDEX> "
OPEN = b"OPEN> "
PASSWORD = b"Password> "


def key(client, ch, marker):
    client.send(ch)
    return client.until(marker)


def post(client, title, body, return_prompt, check_editor=False):
    client.send(title + b"\r\n")
    client.until(b"Text> ")
    client.cmd(body, b"Text> ")
    client.cmd(".", b"Editor>> ")
    if check_editor:
        client.cmd("a", b"Text> ")
        client.cmd("second line", b"Text> ")
        client.cmd(".", b"Editor>> ")
        client.cmd("r1,$ original updated", b"Editor>> ")
        assert b"updated" in client.cmd("l1,2", b"Editor>> ")
    assert b"Saved #" in client.cmd("w", return_prompt)


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="ln-mmm-ui-") as scratch:
        root = Path(scratch)
        catalog = root / "catalog"
        catalog.write_text("1|0|all|member|Public\n2|1|all|none|Read only\n"
                           "3|1|g1|g1|Private title\n", encoding="utf-8")
        options = ["--ui-default", "mmm", "--ui-allow", "rtbbs,mmm", "--ui-policy", "MEMBERS"]
        server = Server(binary, root / "notes", members=root / "members", menu=True,
                        catalog=catalog, telnet=True, language="en", options=options)
        clients = []

        def connect():
            client = Client(server.port)
            clients.append(client)
            assert b"RTBBS-PROBE" in client.until(GATE)
            return client

        try:
            mmm = connect()
            assert b"Login required" in mmm.cmd("open #1", GATE)
            assert b"unavailable" in mmm.cmd("ui rtbbs", GATE)
            assert b"register id" in mmm.cmd("register -?", GATE)
            mmm.cmd("register reader", PASSWORD)
            reply = mmm.cmd("test-secret-789", MAIN)
            assert b"test-secret-789" not in reply and b"Logged in: reader" in reply
            assert b"UI mmm / MEMBERS / DEFAULT mmm" in mmm.cmd("ui", MAIN)
            assert b"UI preference saved" in mmm.cmd("ui mmm", MAIN)
            assert b"#1" in mmm.cmd("2", NOTES)
            mmm.cmd("1", NOTES)
            child_list = mmm.cmd("3", NOTES)
            assert b"Read only" in child_list and b"Private title" not in child_list
            assert b"unavailable" in mmm.cmd("go #3", NOTES)
            mmm.cmd("1", NOTES)
            assert b"unavailable" in mmm.cmd("2", NOTES)
            mmm.cmd(".", NOTES)
            mmm.cmd(".", NOTES)
            mmm.cmd("1", NOTES)
            mmm.cmd("2", b"Title> ")
            post(mmm, b"base target", b"original", NOTES, check_editor=True)
            mmm.cmd("open -i #1", INDEX)
            assert b"updated" in key(mmm, b"1", OPEN)
            key(mmm, b"w", b"Title> ")
            post(mmm, b"response", b"from MMM", OPEN)
            assert b"from MMM" in key(mmm, b" ", OPEN)
            assert b"base target" in key(mmm, b"\x08", OPEN)
            key(mmm, b"i", INDEX)
            key(mmm, b"w", b"Title> ")
            post(mmm, b"other topic", b"base target appears only in the body", INDEX)
            key(mmm, b"f", b"Title search> ")
            reply = mmm.cmd("base target", OPEN)
            assert b"updated" in reply and b"other topic" not in reply
            assert b"No further" in key(mmm, b"F", OPEN)
            assert b"personal file" in key(mmm,b"s",OPEN)
            key(mmm,b"q",NOTES)
            assert b"updated" in mmm.cmd("pfile -t",NOTES)
            assert b"updated" in mmm.cmd("retitle 1 revised title",NOTES)
            assert b"unavailable" in mmm.cmd("withdraw 990 confirm",NOTES)
            mmm.cmd("post #1",b"Title> ")
            mmm.cmd("persistent draft",b"Text> ")
            mmm.cmd("draft text",b"Text> ")
            mmm.cmd(".",b"Editor>> ")
            assert b"draft saved" in mmm.cmd("s",b"Editor>> ")
            mmm.cmd("q",NOTES)
            assert b"draft text" in mmm.cmd("draft resume",b"Editor>> ") + mmm.cmd("p",b"Editor>> ")
            mmm.cmd("q",NOTES)
            assert b"draft removed" in mmm.cmd("draft drop confirm",NOTES)

            legacy = connect()
            legacy.send(b"RAW\r\n")
            legacy.until(b"\r\nRAW\r\n")
            token = "a" * 64
            assert b"CONNECTOR READY 1" in legacy.cmd("CONNECTOR OPEN " + token, b"CONNECTOR READY 1\r\n")
            assert b"ERR login required" in legacy.cmd("READ 1", b"ERR login required\r\n")
            legacy.cmd("LOGIN reader", b"PASSWORD\r\n")
            legacy.send(b"test-secret-789\r\n")
            assert b"LOGGED IN reader" in legacy.until(b"LOGGED IN reader\r\n")
            view = legacy.cmd("READ 1", b"END\r\n")
            assert b"updated" in view and b"revised title" in view
            assert b"updated" in mmm.cmd("withdraw 3 confirm",NOTES)
            legacy.cmd("READ 3", b"WITHDRAWN 3 PARENT 0\r\n")
            nonce = "1" * 32
            synced = legacy.cmd("CONNECTOR SYNC " + token + " " + nonce,
                                b"CONNECTOR STATE " + nonce.encode() + b" ")
            assert b"CONNECTOR STATE " + nonce.encode() in synced
            print("PASS: MMM login, hierarchy and ACL, note editor and reading keys, RAW connector")
        finally:
            for client in clients:
                client.close()
            server.close()


if __name__ == "__main__":
    run(sys.argv[1])

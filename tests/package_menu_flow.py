"""One installed-menu readback contract shared by hosted and native pkg tests."""
from pathlib import Path
import sys
from test_telnet import Client
from test_members import PASSWORD

MAIN_PROMPT = b"] > "
MAIL_PROMPT = b"0 exit)> "


def restore_command(archive, destination, tools_dir):
    return [sys.executable, str(Path(__file__).resolve().parents[1] / "scripts/maintenance.py"),
            "db-restore", "--tools-dir", str(tools_dir), str(archive), str(destination)]


def readback(port, after=None):
    client = Client(port)  # Keep the existing ten-second socket timeout.
    responses = {}
    phase = "connect"
    try:
        client.until(MAIN_PROMPT)
        phase = "login-name"
        client.cmd("LOGIN deployprobe", b"Password (8-128 bytes)> ")
        phase = "login-password"
        assert b"Logged in as deployprobe" in client.cmd(PASSWORD, MAIN_PROMPT)
        phase = "article"
        responses[phase] = client.cmd("READ 1", MAIN_PROMPT)
        assert b"package update article body" in responses[phase]
        phase = "article-exit"
        client.cmd("0", MAIN_PROMPT)
        phase = "mail"
        responses[phase] = client.cmd("MAIL READ 1", MAIL_PROMPT)
        assert b"package update mail body" in responses[phase]
        assert responses[phase].endswith(MAIL_PROMPT)
        assert MAIN_PROMPT not in responses[phase], "Mail unexpectedly used the main prompt"
        phase = "mail-exit"
        responses[phase] = client.cmd("MAIL 0", MAIN_PROMPT)
        phase = "main-ping"
        assert b"PONG" in client.cmd("PING", MAIN_PROMPT)
        if after is not None:
            phase = "additional-service-check"
            after(client)
        return responses
    except Exception as error:
        raise AssertionError("Installed-menu readback failed at " + phase +
                             "; received tail=" + repr(client.buf[-4096:])) from error
    finally:
        client.close()

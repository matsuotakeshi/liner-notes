"""Real Telnet/menu flow with the public assets used by the FreeBSD pkg fixture."""
from pathlib import Path
import sys
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ci"))
from freebsd_fixture_config import prepare_assets, text_samples
from test_board import Server, post
from test_runtime import Client
from test_members import auth
from test_mail import send
from package_menu_flow import readback, MAIL_PROMPT, restore_command


def run(binary, converter):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="package-menu-") as temporary:
        root = Path(temporary)
        assets = root / "usr/local/share/liner-notes"
        for name in text_samples():
            sample = assets / (name + ".sample")
            sample.parent.mkdir(parents=True, exist_ok=True)
            sample.write_bytes((ROOT / name).read_bytes())
        settings = prepare_assets(root)
        data = root / "data"; data.mkdir()
        catalog = data / "catalog.dat"
        catalog.write_text("1|0|all|member|Package update test\n", encoding="utf8")
        options = ["--database-structured", str(data / "liner.sqlite")]
        initial = Server(binary, data / "notes.dat", members=data / "members.dat",
                         catalog=catalog, options=options)
        client = Client(initial.port)
        try:
            auth(client, "deployprobe", True)
            post(client, "retained article", "package update article body")
            send(client, "deployprobe", "retained mail", "package update mail body")
        finally:
            client.close(); initial.close()
        rc_logs = root / "rc-logs"; rc_logs.mkdir()
        options += ["--user-log", str(rc_logs / "user.log"), "--chat-log", str(rc_logs / "chat.log"),
                    "--help-dir", str(assets / "help"), "--login-text-dir", str(assets),
                    "--banner", settings["liner_notes_banner"],
                    "--sysinfo", settings["liner_notes_sysinfo"],
                    "--goodbye", settings["liner_notes_goodbye"]]
        for phase in ("initial", "updated", "recovered"):
            server = Server(binary, data / "notes.dat", members=data / "members.dat", catalog=catalog,
                            options=options, menu=True, telnet=True, language="en", menu_dir=assets / "menus")
            try:
                responses = readback(server.port)
                assert responses["mail"].endswith(MAIL_PROMPT)
                print("PASS installed-menu " + phase + ": login/article/mail marker/body/exit/main PING")
            finally:
                server.close()
        print("PASS real mail prompt:", repr(responses["mail"][-160:]))
        # The source checkout has a converter too: an invalid explicit tools
        # directory must NOT silently fall back to that source executable.
        installed_tools = root / "usr/local/bin"
        installed_tools.mkdir(parents=True)
        installed_converter = installed_tools / "liner-notes-dbconvert"
        shutil.copy2(Path(converter).resolve(), installed_converter)
        installed_converter.chmod(0o755)
        assert installed_converter.parent != Path(binary).parent
        archive = root / "backup.tar.gz"
        command = [sys.executable, str(ROOT / "scripts/maintenance.py"), "db-backup", "--root", str(data),
                   "--database", str(data / "liner.sqlite"), "--board", "notes.dat", "--members", "members.dat", str(archive)]
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)
        bad = subprocess.run(restore_command(archive, root / "missing-tool-restore", root / "absent-tools"),
                             capture_output=True, text=True, timeout=120)
        assert bad.returncode != 0 and "Build/install dbconvert" in bad.stderr, bad
        restored = root / "restored"
        result = subprocess.run(restore_command(archive, restored, installed_tools),
                                check=True, capture_output=True, text=True, timeout=120)
        assert (restored / "liner.sqlite").is_file()
        restored_options = options.copy()
        restored_options[1] = str(restored / "liner.sqlite")
        server = Server(binary, restored / "notes.dat", members=restored / "members.dat", catalog=restored / "catalog.dat",
                        options=restored_options, menu=True, telnet=True, language="en", menu_dir=assets / "menus")
        try:
            readback(server.port)
        finally:
            server.close()
        print("PASS explicit installed converter: missing directory rejected, restored login/article/mail re-read")


if __name__ == "__main__": run(sys.argv[1], sys.argv[2])

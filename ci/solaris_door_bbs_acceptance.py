#!/usr/bin/python3.13 -I
"""Prepare one isolated Solaris authenticated Door test, using reviewed main only."""

import argparse
from contextlib import ExitStack
import fcntl
import grp
import json
import os
from pathlib import Path
import pwd
import re
import secrets
import shutil
import stat
import subprocess
import sys
import tempfile
import time


SLOT, BBS, BROKER, CONSUMER = 61001, 61002, 61003, 61004
PYTHON = "/usr/bin/python3.13"
ENV = {"PATH": "/usr/bin:/usr/sbin", "LC_ALL": "C"}
ROOT = Path(__file__).absolute().parents[1]


def trusted(path):
    for parent in (path, *path.parents):
        info = parent.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError("Root-owned fixture and all ancestors must be protected")


def run(*args, timeout=30, **options):
    return subprocess.run([str(arg) for arg in args], env=ENV, check=True,
                          capture_output=True, timeout=timeout, **options).stdout


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=18)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
            raise


def wait_until(predicate, seconds=5):
    deadline = time.monotonic() + seconds
    while not predicate():
        if time.monotonic() >= deadline:
            raise TimeoutError("Solaris BBS fixture did not reach the expected state")
        time.sleep(0.05)


def verify_source(sha):
    if not re.fullmatch("[0-9a-f]{40}", sha):
        raise ValueError("A full reviewed main SHA is required")
    for directory in ("ci", "scripts", "solaris", "tests"):
        trusted(ROOT / directory)
        for path in (ROOT / directory).glob("*.py"):
            trusted(path)
    for name in (".ci-source-sha", "runtime", "tests/solaris_auth_door_probe.c",
                 "liner-notes-solaris-door-child"):
        trusted(ROOT / name)
    if (ROOT / ".ci-source-sha").read_text().strip() != sha:
        raise ValueError("Reviewed source SHA marker mismatch")


def run_fixture(stage, report, boundary, fixture, slots, broker_module):
    root, state, broker_dir = (stage / name for name in ("root", "state", "broker"))
    for directory in (root, state, broker_dir):
        directory.mkdir(mode=0o700)
    stage.chmod(0o711)
    endpoint, broker_socket = stage / "isolation", broker_dir / "sock"
    broker_policy, launch_policy = stage / "broker.json", stage / "isolation.json"
    broker_policy.write_text(json.dumps(dict(bbs="solaris-fixture", bbs_uid=BBS,
        broker_uid=BROKER, socket=str(broker_socket), socket_gid=BROKER,
        door_uids=[SLOT], consumers={str(CONSUMER): [3]})))
    broker_policy.chmod(0o644)
    launch_policy.write_text(json.dumps(dict(bbs_uid=BBS, socket=str(endpoint),
        socket_gid=BBS, broker_config=str(broker_policy), state_dir=str(state),
        slots=[dict(door=3, uid=SLOT, level=0, seconds=30, root=str(root),
                    helper=str(ROOT / "liner-notes-solaris-door-child"))])))
    os.chown(broker_dir, BROKER, BROKER)
    broker_dir.chmod(0o750)

    game = stage / "game"
    run("/usr/bin/gcc", "-m64", "-std=c11", "-Wall", "-Wextra", "-Werror",
        ROOT / "tests/solaris_auth_door_probe.c", "-lsocket", "-lnsl", "-o", game)
    libraries = fixture.library_paths(run("/usr/bin/ldd", game).decode(),
                                     fixture.elf_interpreter(game.read_bytes()))
    if fixture.read_mount(root) is not None:
        raise ValueError("Unexpected preexisting mount")
    run("/usr/sbin/mount", "-F", "tmpfs", "-o", "nosuid,size=64m", "swap", root)
    try:
        fixture.require_nosuid(fixture.read_mount(root))
        root.chmod(0o755)
        total = 0
        created = []
        for library in libraries:
            source = Path(str(library)).resolve(strict=True)
            boundary.trusted(source, private=False)
            total += source.stat().st_size
            if not 0 < total < 32 * 1024 * 1024:
                raise ValueError("Unexpected fixture library size")
            target = fixture.library_destination(root, library, created)
            shutil.copyfile(source, target)
            target.chmod(0o555)
        shutil.copyfile(game, root / "door")
        (root / "door").chmod(0o555)
        channel = root / "channel"
        channel.mkdir(mode=0o750)
        os.chown(channel, CONSUMER, SLOT)
        channel.chmod(0o750)
        channel_socket = channel / "sock"
        marker = state / ("slot-" + str(SLOT))

        with ExitStack() as stack:
            def start(name, args, **options):
                log = stack.enter_context((stage / (name + ".log")).open("wb"))
                process = subprocess.Popen(list(map(str, args)), env=ENV, stdin=subprocess.DEVNULL,
                                           stdout=log, stderr=log, **options)
                stack.callback(stop, process)
                return process

            broker = start("broker", [PYTHON, "-E", "-s", "-S", "-B",
                ROOT / "scripts/door_broker.py", "--config", broker_policy],
                user=BROKER, group=BROKER, extra_groups=())
            wait_until(lambda: (broker_socket.is_socket()
                               and stat.S_IMODE(broker_socket.lstat().st_mode) == 0o660)
                       or broker.poll() is not None)
            broker_module.check_ready(broker_socket, BROKER)
            # The installed wrapper places these same modules in a flat,
            # protected directory. Keep development paths explicit here.
            boot = ("import runpy,sys; sys.path[:0]=sys.argv[1:3]; "
                    "sys.argv=sys.argv[3:]; runpy.run_path(sys.argv[0],run_name='__main__')")
            service = start("supervisor", [PYTHON, "-I", "-B", "-c", boot,
                ROOT / "scripts", ROOT / "solaris", ROOT / "solaris/door_solaris_supervisor.py",
                "run", launch_policy])
            wait_until(lambda: (endpoint.is_socket() and stat.S_IMODE(endpoint.lstat().st_mode) == 0o660)
                       or service.poll() is not None)
            broker_module.check_ready(endpoint, 0)

            for label in ("quit", "disconnect", "withheld", "broker-down"):
                consumer = None
                if label == "broker-down":
                    stop(broker)
                else:
                    consumer = start(label + "-consumer", [PYTHON, "-I", "-B",
                        ROOT / "tests/solaris_auth_door_consumer.py", channel_socket, broker_socket,
                        *(["--withhold"] if label == "withheld" else [])],
                        user=CONSUMER, group=BROKER, extra_groups=())
                    consumer_log = stage / (label + "-consumer.log")
                    wait_until(lambda: "CONSUMER READY" in consumer_log.read_text()
                               or consumer.poll() is not None)
                mode = label if label in ("quit", "disconnect") else "denied"
                result = run(PYTHON, "-E", "-s", "-B", ROOT / "tests/solaris_auth_door_menu.py",
                             ROOT / "runtime", endpoint, mode, user=BBS, group=BBS, extra_groups=())
                if result.strip() != ("BBS_DOOR_ACCEPTANCE " + mode).encode():
                    raise ValueError("Missing acceptance result from BBS")
                if consumer is not None:
                    expected = "TICKET WITHHELD" if label == "withheld" else "LEASE REVOKED"
                    if consumer.wait(timeout=8) or expected not in consumer_log.read_text():
                        raise ValueError("Missing consumer session evidence")
                    channel_socket.unlink()
                wait_until(lambda: marker.is_file() and marker.stat().st_size == 0
                           and not slots._active(SLOT))
                report["cases"].append(label)
            stop(service)
            if service.returncode or endpoint.exists():
                raise ValueError("Supervisor shutdown did not finish cleanly")
            report["supervisor_stopped"] = True
    finally:
        if any(slots._active(uid) for uid in (SLOT, BBS, BROKER, CONSUMER)):
            raise RuntimeError("Fixture identity still active; preserve its isolated mount")
        run("/usr/sbin/umount", root)
        report["mount_removed"] = fixture.read_mount(root) is None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-sha")
    parser.add_argument("--run-approved", action="store_true")
    args = parser.parse_args()
    if not args.run_approved:
        print("Plan: fixed test UIDs, loopback BBS, separate Broker and private 64 MiB nosuid tmpfs.")
        return
    if sys.platform != "sunos5" or os.getuid() != 0 or os.geteuid() != 0 or not args.source_sha:
        parser.error("Reviewed Solaris root test required")
    verify_source(args.source_sha)
    sys.path[:0] = [str(ROOT / name) for name in ("scripts", "solaris", "ci")]
    import door_broker
    import door_solaris_slot
    import solaris_door_boundary
    import solaris_door_jail

    lock = Path("/opt/liner-notes-ci/door-boundary/fixture.lock")
    solaris_door_boundary.trusted(lock)
    os.umask(0o077)
    with lock.open("r+b") as guard:
        fcntl.flock(guard, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for uid in (SLOT, BBS, BROKER, CONSUMER):
            for lookup in (pwd.getpwuid, grp.getgrgid):
                try:
                    lookup(uid)
                except KeyError:
                    continue
                raise ValueError("A fixture UID or GID is registered")
            if door_solaris_slot._active(uid):
                raise ValueError("A fixture UID is already in use")
        stage = Path(tempfile.mkdtemp(prefix="ln-door-bbs-", dir="/var/db"))
        report = dict(source_sha=args.source_sha, cases=[], mount_removed=False,
                      supervisor_stopped=False, directory=str(stage))
        try:
            run_fixture(stage, report, solaris_door_boundary, solaris_door_jail,
                        door_solaris_slot, door_broker)
        finally:
            (stage / "receipt.json").write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()

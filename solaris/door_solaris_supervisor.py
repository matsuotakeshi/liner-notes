"""Solaris authenticated Door supervisor and opt-in SMF start method."""

import argparse
from contextlib import ExitStack, suppress
import json
import os
from pathlib import Path
import select
import signal
import socket
import stat
import sys
import time

from door_broker import check_ready, configuration as broker_configuration, secure_path
from door_broker_core import positive
from door_isolation import accept_connection, reap_workers, start_worker
from door_solaris_service import serve_launch


SMF_FMRI = "svc:/application/liner-notes-door-isolation:default"


def read_policy(path):
    path = secure_path(path, 0)
    if path.stat().st_size > 65536:
        raise ValueError("Solaris Door policy size")
    policy = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(policy, dict) or set(policy) != {
        "bbs_uid", "socket", "socket_gid", "broker_config", "state_dir", "slots",
    }:
        raise ValueError("Solaris Door policy fields")

    broker = broker_configuration(policy["broker_config"])
    if positive(policy["bbs_uid"], 2**31 - 1) != broker["bbs_uid"]:
        raise ValueError("Solaris Door BBS identity")
    positive(policy["socket_gid"], 2**31 - 1)
    endpoint = Path(policy["socket"])
    if (str(endpoint) != policy["socket"] or ".." in endpoint.parts
            or endpoint.name in ("", ".") or len(os.fsencode(endpoint)) >= 100
            or endpoint == Path(broker["socket"])):
        raise ValueError("Solaris Door socket")
    secure_path(endpoint.parent, 0, directory=True)
    state = secure_path(policy["state_dir"], 0, directory=True)
    if state.stat().st_mode & 0o077:
        raise ValueError("Solaris Door state permissions")

    slots = policy["slots"]
    if not isinstance(slots, list) or not 1 <= len(slots) <= 128:
        raise ValueError("Solaris Door slot count")
    used, roots, helpers = set(), set(), set()
    approved_doors = {door for doors in broker["consumers"].values() for door in doors}
    for slot in slots:
        if not isinstance(slot, dict) or set(slot) != {
            "door", "uid", "level", "seconds", "root", "helper",
        }:
            raise ValueError("Solaris Door slot fields")
        door = positive(slot["door"], 86400)
        uid = positive(slot["uid"], 2**31 - 1)
        positive(slot["seconds"], 3600)
        if door not in approved_doors or type(slot["level"]) is not int or not 0 <= slot["level"] <= 5:
            raise ValueError("Solaris Door slot policy")
        root = secure_path(slot["root"], 0, directory=True)
        helper = secure_path(slot["helper"], 0)
        if (str(root) != slot["root"] or ".." in root.parts
                or uid in used or root in roots or state.is_relative_to(root)
                or root.is_relative_to(state) or endpoint.is_relative_to(root)
                or Path(broker["socket"]).is_relative_to(root)
                or any(root.is_relative_to(other) or other.is_relative_to(root) for other in roots)
                or not os.access(helper, os.X_OK)):
            raise ValueError("Solaris Door slot paths or UID")
        used.add(uid)
        roots.add(root)
        helpers.add(helper)
    if any(helper.is_relative_to(root) for helper in helpers for root in roots):
        raise ValueError("Solaris Door helper must live outside every execution root")
    if used != set(broker["door_uids"]):
        raise ValueError("Solaris Door Broker slot identities")
    policy["broker_uid"] = broker["broker_uid"]
    policy["broker_socket"] = broker["socket"]
    return policy


def run_supervisor(policy, ready=None):
    endpoint = Path(policy["socket"])
    children = set()
    stopping = False

    def stop(*_):
        nonlocal stopping
        stopping = True

    def remove_socket(info):
        with suppress(FileNotFoundError):
            if os.path.samestat(info, endpoint.lstat()):
                endpoint.unlink()

    with ExitStack() as stack:
        listener = stack.enter_context(socket.socket(socket.AF_UNIX, socket.SOCK_STREAM))
        old_mask = os.umask(0o077)
        try:
            # bind is exclusive. A stale socket requires explicit operator review.
            listener.bind(str(endpoint))
        finally:
            os.umask(old_mask)
        stack.callback(remove_socket, endpoint.lstat())
        os.chown(endpoint, -1, policy["socket_gid"])
        endpoint.chmod(0o660)
        listener.listen(32)
        listener.settimeout(0.25)
        # A socketpair provides a reliable peer-close notification on Solaris.
        worker_end, parent_end = socket.socketpair()
        watch, alive = worker_end.detach(), parent_end.detach()
        stack.callback(os.close, watch)
        stack.callback(os.close, alive)
        for sig in (signal.SIGTERM, signal.SIGINT):
            stack.callback(signal.signal, sig, signal.signal(sig, stop))
        try:
            if ready is not None:
                ready()
            while not stopping:
                reap_workers(children)
                connection = accept_connection(listener)
                if connection is not None:
                    with connection:
                        if not stopping:
                            start_worker(policy, connection, listener, watch, alive, children,
                                         handler=serve_launch)
        finally:
            listener.close()
            # Signal EOF before reaping. The socket pathname remains reserved until
            # all workers have finished revocation and contract cleanup.
            with socket.socket(fileno=os.dup(alive)) as peer:
                peer.shutdown(socket.SHUT_RDWR)
            while children:
                reap_workers(children)
                if children:
                    time.sleep(0.05)


def start_smf(policy):
    reader, writer = socket.socketpair()
    with reader, writer:
        child = os.fork()
        if child == 0:
            reader.close()

            def report_ready():
                writer.sendall(b"1")
                writer.close()

            result = 1
            try:
                run_supervisor(policy, report_ready)
                result = 0
            except (OSError, ValueError):
                print("Solaris Door supervisor could not start", file=sys.stderr, flush=True)
            finally:
                os._exit(result)
        writer.close()
        reader.settimeout(10)
        try:
            if reader.recv(1) == b"1":
                return
        except TimeoutError:
            with suppress(ProcessLookupError):
                os.kill(child, signal.SIGTERM)
        else:
            os.waitpid(child, 0)
        raise OSError("Solaris Door supervisor readiness failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("run", "check", "ready", "smf-start"))
    parser.add_argument("config")
    args = parser.parse_args()
    try:
        if sys.platform != "sunos5" or os.getuid() != 0 or os.geteuid() != 0:
            raise ValueError("Solaris root supervisor required")
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        policy = read_policy(args.config)
        if args.action == "check":
            return 0
        check_ready(policy["broker_socket"], policy["broker_uid"])
        if args.action == "ready":
            check_ready(policy["socket"], 0)
        elif args.action == "run":
            run_supervisor(policy)
        else:
            if os.environ.get("SMF_FMRI") != SMF_FMRI:
                raise ValueError("Solaris Door SMF context")
            start_smf(policy)
    except (OSError, ValueError, TypeError, KeyError):
        print("Solaris Door supervisor rejected startup or readiness; check policy and service logs.",
              file=sys.stderr)
        return 95
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

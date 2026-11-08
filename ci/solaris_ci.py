#!/usr/bin/env python3
"""Solaris native checks. Uses installed tools; never modifies the host OS."""
import argparse
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def run(command):
    print("+ " + shlex.join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def find_tool(names):
    for name in names:
        found = shutil.which(str(name))
        if found:
            return found
    raise RuntimeError("Required installed tool missing: " + ", ".join(map(str, names)))


def inventory():
    print(json.dumps({"platform": platform.platform(), "machine": platform.machine(),
                      "python": sys.version, "executable": sys.executable}, indent=2), flush=True)
    for command in (["uname", "-a"], ["/usr/bin/isainfo", "-kv"], ["id"]):
        run(command)
    if Path("/usr/bin/auths").is_file():
        run(["/usr/bin/auths"])
    print(Path("/etc/release").read_text(), flush=True)
    for name in ("gmake", "clang", "gcc", "pkg-config", "openssl", "lrz", "lsz"):
        print(name + ": " + str(shutil.which(name)), flush=True)
    for directory in ("/usr/gcc", "/usr/openssl", "/usr/include", "/usr/lib/amd64"):
        path = Path(directory)
        if path.is_dir():
            # Restrict inventory to dependency names; never print host configuration.
            print(directory + ": " + ", ".join(p.name for p in path.iterdir()
                  if directory in ("/usr/gcc", "/usr/openssl") or
                  any(x in p.name for x in ("sqlite", "crypto", "ssl", "pty"))), flush=True)


def build_arguments():
    make = find_tool(["gmake", "/usr/gnu/bin/make"])
    compilers = ["clang", "/usr/clang/bin/clang", "gcc"]
    compilers.extend(sorted(Path("/usr/gcc").glob("*/bin/gcc"), reverse=True))
    compiler = find_tool(compilers)
    cppflags = ["-D__EXTENSIONS__=1"]
    ldflags = []
    sqlite_flags = []
    sqlite_libs = ["-lsqlite3"]
    pkgconf = shutil.which("pkg-config")
    for package in ("openssl", "sqlite3"):
        if pkgconf and subprocess.run([pkgconf, "--exists", package]).returncode == 0:
            cflags = shlex.split(subprocess.check_output([pkgconf, "--cflags", package], text=True))
            libs = shlex.split(subprocess.check_output([pkgconf, "--libs", package], text=True))
            cppflags.extend(cflags)
            ldflags.extend(x for x in libs if x.startswith("-L"))
            if package == "sqlite3":
                sqlite_flags = cflags
                sqlite_libs = libs
        elif package == "openssl":
            for version in ("3", "1.1"):
                prefix = Path("/usr/openssl") / version
                lib = prefix / "lib/amd64"
                if (prefix / "include/openssl/evp.h").is_file() and lib.is_dir():
                    cppflags.append("-I" + str(prefix / "include"))
                    ldflags.append("-L" + str(lib))
                    break
    # Solaris pkg-config can return the 32-bit lib directory even for -m64.
    ldflags = ["-L" + str(Path(flag[2:]) / "amd64")
               if flag.startswith("-L") and (Path(flag[2:]) / "amd64").is_dir()
               else flag for flag in ldflags]
    # Libraries found through pkg-config must also be discoverable by the runtime.
    ldflags.extend("-Wl,-R," + flag[2:] for flag in list(ldflags) if flag.startswith("-L"))
    return [make, "CC=" + compiler, "PYTHON=" + sys.executable,
            "CFLAGS=-O2 -g -m64", "CPPFLAGS=" + " ".join(cppflags),
            "LDFLAGS=" + " ".join(ldflags), "PTY_LIBS=",
            "LDLIBS=-lsocket -lnsl", "SQLITE_CFLAGS=" + " ".join(sqlite_flags),
            "SQLITE_LIBS=" + " ".join(sqlite_libs)]


def test():
    flags = build_arguments()
    for assignment in flags[1:]:
        key, value = assignment.split("=", 1)
        os.environ[key] = value
    capabilities = {"core": "not-tested", "door": "not-tested",
                    "webui": "not-tested", "link": "not-tested", "zmodem": "not-tested"}
    try:
        run(flags + ["-j2", "all"])
        run([sys.executable, "tests/test_door_lock.py"])
        run([sys.executable, "tests/test_solaris_service.py"])
        run([sys.executable, "scripts/install-solaris.py", "--destdir", "build/solaris-stage", "--python", sys.executable])
        run(["/usr/sbin/svccfg", "validate", "build/solaris-stage/opt/liner-notes/share/smf/liner-notes.xml"])
        run(flags + ["check"])
        run([sys.executable, "tests/test_stage73_database.py", "./runtime"])
        capabilities["core"] = "passed"
        run(flags + ["test-doors"])
        capabilities["door"] = "passed"
        # Match existing Linux CI's isolated dependency setup. Reuse installed
        # cryptography, but never install into system Python or change the OS.
        if sys.version_info >= (3, 11):
            environment = ROOT / "build/solaris-python"
            run([sys.executable, "-m", "venv", "--system-site-packages", str(environment)])
            python = str(environment / "bin/python")
            run([python, "-m", "pip", "install", "--disable-pip-version-check", "--no-input",
                 "-r", "webui/requirements.txt", "-r", "link-requirements.txt"])
            run([python, "-c", "import aiohttp,cryptography;print('aiohttp',aiohttp.__version__,'cryptography',cryptography.__version__)"])
            web_flags = ["PYTHON=" + python if item.startswith("PYTHON=") else item for item in flags]
            run(web_flags + ["test-web"])
            capabilities["webui"] = "passed"
            run([python, "tests/test_link_store.py"])
            run([python, "tests/test_link_tls.py"])
            run([python, "tests/test_link_bbs.py", "./runtime"])
            run([python, "tests/test_link_service.py", "./runtime"])
            run([python, "tests/test_stage74_links.py"])
            run([python, "tests/test_stage74_link_bbs.py", "./runtime"])
            capabilities["link"] = "passed"
        else:
            capabilities["webui"] = "unverified: requires Python >=3.11"
            capabilities["link"] = "unverified: requires Python >=3.11"
        lrz, lsz = shutil.which("lrz"), shutil.which("lsz")
        if not (lrz and lsz):
            engine = ROOT / "build/solaris-lrzsz"
            try:
                run([sys.executable, "scripts/build-lrzsz.py", str(engine),
                     "--cc", os.environ["CC"], "--make", flags[0]])
            except subprocess.CalledProcessError:
                log = engine / "build.txt"
                if log.is_file():
                    print(log.read_text(errors="replace"), flush=True)
                raise
            lrz, lsz = str(engine / "local/bin/lrz"), str(engine / "local/bin/lsz")
        run(flags + ["test-zmodem", "LRZ=" + lrz, "LSZ=" + lsz])
        capabilities["zmodem"] = "passed"
    finally:
        # Included in the native log artifact even when a suite fails.
        print("SOLARIS_CAPABILITIES " + json.dumps(capabilities, sort_keys=True), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("inventory", "test"))
    args = parser.parse_args()
    if platform.system() != "SunOS" or platform.machine() not in ("i86pc", "i386", "amd64", "x86_64"):
        parser.error("Solaris x86-64 only")
    if args.action == "inventory":
        inventory()
    else:
        test()


if __name__ == "__main__":
    main()

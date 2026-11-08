#!/usr/bin/env python3
"""Solaris native checks. Uses installed tools; never modifies the host OS."""
import argparse
import importlib.util
import json
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
    for command in (["uname", "-a"], ["/usr/bin/isainfo", "-kv"]):
        run(command)
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
    # Libraries found through pkg-config must also be discoverable by the runtime.
    ldflags.extend("-Wl,-R," + flag[2:] for flag in list(ldflags) if flag.startswith("-L"))
    return [make, "CC=" + compiler, "PYTHON=" + sys.executable,
            "CFLAGS=-O2 -g -m64", "CPPFLAGS=" + " ".join(cppflags),
            "LDFLAGS=" + " ".join(ldflags), "PTY_LIBS=",
            "LDLIBS=-lsocket -lnsl", "SQLITE_CFLAGS=" + " ".join(sqlite_flags),
            "SQLITE_LIBS=" + " ".join(sqlite_libs)]


def test():
    flags = build_arguments()
    capabilities = {"core": "not-tested", "door": "not-tested",
                    "webui": "not-tested", "link": "not-tested", "zmodem": "not-tested"}
    try:
        run(flags + ["-j2", "all"])
        run([sys.executable, "tests/test_solaris_service.py"])
        run([sys.executable, "scripts/install-solaris.py", "--destdir", "build/solaris-stage", "--python", sys.executable])
        run(["/usr/sbin/svccfg", "validate", "build/solaris-stage/opt/liner-notes/share/smf/liner-notes.xml"])
        run(flags + ["check"])
        run([sys.executable, "tests/test_stage73_database.py", "./runtime"])
        capabilities["core"] = "passed"
        run(flags + ["test-doors"])
        capabilities["door"] = "passed"
        # Python 3.11/tomllib is an application dependency of WebUI and link tools.
        if sys.version_info >= (3, 11) and importlib.util.find_spec("aiohttp"):
            run(flags + ["test-web"])
            capabilities["webui"] = "passed"
        else:
            capabilities["webui"] = "unverified: requires Python >=3.11 and aiohttp"
        if sys.version_info >= (3, 11) and importlib.util.find_spec("cryptography"):
            run([sys.executable, "tests/test_link_bbs.py", "./runtime"])
            run([sys.executable, "tests/test_stage74_link_bbs.py", "./runtime"])
            capabilities["link"] = "passed"
        else:
            capabilities["link"] = "unverified: requires Python >=3.11 and cryptography"
        lrz, lsz = shutil.which("lrz"), shutil.which("lsz")
        if lrz and lsz:
            run(flags + ["test-zmodem", "LRZ=" + lrz, "LSZ=" + lsz])
            capabilities["zmodem"] = "passed"
        else:
            capabilities["zmodem"] = "unverified: lrz/lsz not installed"
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

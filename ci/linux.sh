#!/bin/bash
set -euo pipefail
cat /etc/os-release
dnf install -y clang gcc make openssl-devel sqlite-devel python3.11 python3.11-pip tar gzip procps-ng shadow-utils systemd telnet
useradd --create-home bbstest
mkdir /home/bbstest/source
tar -C /source --exclude=.git --exclude=build -cf - . | tar -C /home/bbstest/source -xf -
chown -R bbstest:bbstest /home/bbstest/source
runuser -u bbstest -- bash -c '
  set -euo pipefail
  cd /home/bbstest/source
  export CC=clang
  export LINER_TEST_START_TIMEOUT=180
  make -j2 CC=clang PYTHON=python3.11 SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3
  python3.11 tests/test_linux_install.py
'
cd /home/bbstest/source
python3.11 scripts/install-linux.py --python /usr/bin/python3.11
useradd --system --home-dir /var/lib/liner-notes --shell /sbin/nologin liner_notes
systemd-analyze verify /usr/local/lib/systemd/system/liner-notes.service
/usr/local/bin/liner-notes-admin --help
/usr/local/bin/liner-notes-check-resume --help
python3.11 tests/test_linux_service.py
runuser -u bbstest -- bash -c '
  set -euo pipefail
  cd /home/bbstest/source
  export CC=clang
  export LINER_TEST_START_TIMEOUT=180
  python3.11 -m venv .web-venv
  .web-venv/bin/pip install -r webui/requirements.txt -r link-requirements.txt
  make CC=clang PYTHON="$PWD/.web-venv/bin/python" SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3 test-stage73
  if ! python3.11 scripts/build-lrzsz.py /home/bbstest/lrzsz-check; then
    cat /home/bbstest/lrzsz-check/build.txt
    exit 1
  fi
  make CC=clang PYTHON=python3.11 SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3 test-zmodem LRZ=/home/bbstest/lrzsz-check/local/bin/lrz LSZ=/home/bbstest/lrzsz-check/local/bin/lsz
  make CC=clang PYTHON=python3.11 SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3 check test-stage67 test-stage68 test-stage69 test-stage70
  make CC=clang PYTHON="$PWD/.web-venv/bin/python" SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3 test-stage72
'

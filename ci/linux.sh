#!/bin/bash
set -euo pipefail
cat /etc/os-release
dnf install -y clang gcc make openssl-devel sqlite-devel python3.11 tar gzip procps-ng shadow-utils systemd telnet
useradd --create-home bbstest
mkdir /home/bbstest/source
tar -C /source --exclude=.git --exclude=build -cf - . | tar -C /home/bbstest/source -xf -
chown -R bbstest:bbstest /home/bbstest/source
runuser -u bbstest -- bash -c '
  set -euo pipefail
  cd /home/bbstest/source
  export CC=clang
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
  if ! python3.11 scripts/build-lrzsz.py /home/bbstest/lrzsz-check; then
    cat /home/bbstest/lrzsz-check/build.txt
    exit 1
  fi
  make CC=clang PYTHON=python3.11 SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3 test test-stage65 test-stage70 test-stage71
  make CC=clang PYTHON=python3.11 SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3 test-zmodem LRZ=/home/bbstest/lrzsz-check/local/bin/lrz LSZ=/home/bbstest/lrzsz-check/local/bin/lsz
'

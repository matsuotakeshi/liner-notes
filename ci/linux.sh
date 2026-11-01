#!/bin/bash
set -euo pipefail
cat /etc/os-release
dnf install -y clang gcc make openssl-devel sqlite-devel python3.11 tar gzip procps-ng shadow-utils
useradd --create-home bbstest
mkdir /home/bbstest/source
tar -C /source --exclude=.git --exclude=build -cf - . | tar -C /home/bbstest/source -xf -
chown -R bbstest:bbstest /home/bbstest/source
runuser -u bbstest -- bash -c '
  set -euo pipefail
  cd /home/bbstest/source
  export CC=clang
  make -j2 CC=clang PYTHON=python3.11 SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3
  make CC=clang PYTHON=python3.11 SQLITE_CFLAGS= SQLITE_LIBS=-lsqlite3 test test-stage65 test-stage70 test-stage71
'

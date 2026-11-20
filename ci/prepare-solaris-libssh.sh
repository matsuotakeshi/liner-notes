#!/bin/sh
# Explicit, non-root dependency preparation; never invoked by ordinary CI.
set -eu
export LC_ALL=C
umask 077
test "$(id -u)" -ne 0
test "$(uname -s)" = SunOS
test "$(isainfo -n)" = amd64
base="$HOME/liner-notes-deps/libssh-0.12.2"
if test -e "$base"; then echo 'STOP: dependency directory already exists'; exit 1; fi
mkdir -p "$base"
cd "$base"
curl --fail --location --proto '=https' --tlsv1.2 --retry 2 --connect-timeout 10 --max-time 120 -o libssh-0.12.2.tar.xz https://www.libssh.org/files/0.12/libssh-0.12.2.tar.xz
actual=$(digest -a sha256 libssh-0.12.2.tar.xz)
test "$actual" = 49560f677d96e3706a904ac2de1116e25f3680937d51e5c92198fcba4a1c1e9f
gtar xf libssh-0.12.2.tar.xz
cmake -S libssh-0.12.2 -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_C_FLAGS=-m64 -DCMAKE_INSTALL_PREFIX="$base/local" -DCMAKE_INSTALL_LIBDIR=lib -DOPENSSL_ROOT_DIR=/usr/openssl/3 -DWITH_SERVER=ON -DWITH_GSSAPI=OFF -DWITH_EXAMPLES=OFF -DUNIT_TESTING=OFF
cmake --build build -j2
cmake --install build
PKG_CONFIG_PATH="$base/local/lib/pkgconfig" pkg-config --modversion libssh
file "$base/local/lib/libssh.so"

#!/bin/sh
# Explicit administrative setup only, never called by package installation/CI.
set -eu
test "$(id -u)" = 0
test "$#" = 1
case "$1" in
  freebsd15) python=/usr/local/bin/python3.12 ;;
  rocky10|debian13) python=/usr/bin/python3 ;;
  *) exit 2 ;;
esac
test -x "$python"
"$python" -E -s -c 'import cryptography'
base=/usr/local/libexec/liner-notes-deploy
install -d -o root -m 755 "$base"
for source in deploy_policy deploy_transaction deploy_host deploy_restore; do
  install -o root -m 644 "scripts/$source.py" "$base/$source.py"
done
printf '#!/bin/sh\nexec %s -E -s %s/deploy_host.py\n' "$python" "$base" > /usr/local/libexec/liner-notes-deploy-host
chown root /usr/local/libexec/liner-notes-deploy-host
chmod 755 /usr/local/libexec/liner-notes-deploy-host
install -d -o root -m 700 /etc/liner-notes-deploy /var/lib/liner-notes-deploy
printf '%s\n' 'Helper installed only. No sudo rule, keys, profile, service change or deployment was performed.'

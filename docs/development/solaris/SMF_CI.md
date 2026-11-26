# Solaris SMF lifecycle acceptance — approval-gated preparation

This is a test-host preparation plan, not a production installer. It applies only
to `ci-test@10.0.0.52` (hostname `solaris`, Oracle Solaris 11.4 CBE). Root access is
not currently authorized. Do not execute the administrator section before explicit
approval of its reviewed source SHA, file hashes and scope.

The 2026-11-16 read-only check found uid 1000/staff, only Basic Solaris User/All
profiles, no SMF authorizations, and `sudo -n -l` requiring a password. No existing
Liner Notes SMF service was present. No host configuration was changed.

## Security boundary

- Existing exact-main workflow, pinned SSH keys and shared native host lease remain.
- PR tests use GitHub-hosted runners; no PR code is executed on the home VM.
- The sole elevated command is `/opt/liner-notes-ci/solaris_smf_control.py session`.
  It accepts no FMRI, path, shell program, PID or manifest arguments.
- The fixed FMRI is `svc:/application/liner-notes-ci:default`. Pre-existing service
  registration is refused before import, even without a default instance. A
  successful `svccfg list application/liner-notes-ci` must establish absence;
  query failures are never interpreted as absence. Other services are not modified.
- Root-owned helper/manifest/lock and their ancestors reject symlinks, other owners
  and group/world write permission. CI cannot change method credentials or exec.
- The method runs as `ci-test:staff`, with both privilege sets restricted to
  `basic`. It calls the real product start method from the reviewed native copy.
  Neither service method nor runtime runs as root.
- The helper reads bounded chunks, not blocking readline. A partial request cannot
  bypass the 30-second heartbeat or 600-second overall session limit.
- Replies use bounded nonblocking writes with a 10-second deadline. A caller
  leaving the reply pipe open without reading cannot indefinitely prevent cleanup.
- EOF, invalid input, expiry and ordinary interruption stop the owned service with
  `disable -s -T 30`, then delete only that registration. Deletion is refused if
  disabled state is not confirmed. The helper is outside the build process group;
  closing the private stdin pipe on CI death triggers cleanup.
- No `enable -r`, global kill, OS reboot, svc.startd restart, package update or
  mutation of another service exists in the interface. SIGKILL injection is done
  by ci-test only after checking its runtime UID and executable identity.
- A root-owned `/etc/liner-notes-smf-ci.required` marker prevents a missing helper
  from silently reverting configured CI to an unverified capability.

## Prepare a review bundle (unprivileged, after main integration)

In the exact reviewed source copy:

```sh
python3.13 ci/prepare_solaris_smf.py /private/new/smf-reviewed-bundle
cat /private/new/smf-reviewed-bundle/SHA256.json
```

The directory must not exist. This only writes five payload files and a hash list.
It does not import a service, run sudo or modify host paths. Compare each SHA-256
with the approved source-derived bundle before privileged copying. Do not trust a
mutable hash list from the same unverified input as the only integrity check.

## One-time administrator commands — not executed yet

The administrator supplies `SMF_BUNDLE` as the reviewed, hash-verified bundle path.
The fixed installation paths must be absent. Existing installations require a
separately reviewed update; these commands must not overwrite them.

```sh
test "$(id -u)" = 0 || exit 1
test "$(uname -n)" = solaris-ci || exit 1
test "$(uname -s)" = SunOS || exit 1
id ci-test || exit 1
test -d /etc/sudoers.d || exit 1
test ! -L /etc/sudoers.d || exit 1
# Confirm the existing /etc/sudoers already includes /etc/sudoers.d.
# Do not add a new global include or broader permissions as a fallback.
grep -E '^[#@]includedir[[:space:]]+/etc/sudoers.d[[:space:]]*$' /etc/sudoers || exit 1
test ! -e /opt/liner-notes-ci && test ! -L /opt/liner-notes-ci || exit 1
test ! -e /etc/sudoers.d/liner-notes-smf-ci || exit 1
test ! -e /etc/liner-notes-smf-ci.required || exit 1
SMF_EXISTING=$(/usr/sbin/svccfg list application/liner-notes-ci) || exit 1
test -z "$SMF_EXISTING" || exit 1
/usr/sbin/svccfg validate "$SMF_BUNDLE/liner-notes-ci.xml" || exit 1
/usr/sbin/visudo -cf "$SMF_BUNDLE/liner-notes-smf-ci.sudoers" || exit 1
umask 077
mkdir /opt/liner-notes-ci || exit 1
chmod 755 /opt/liner-notes-ci || exit 1
# Solaris install(1) options vary: use explicit cp/chown/chmod on new fixed files.
cp "$SMF_BUNDLE/solaris_smf_control.py" /opt/liner-notes-ci/solaris_smf_control.py || exit 1
cp "$SMF_BUNDLE/solaris_smf_method.py" /opt/liner-notes-ci/solaris_smf_method.py || exit 1
cp "$SMF_BUNDLE/liner-notes-ci.xml" /opt/liner-notes-ci/liner-notes-ci.xml || exit 1
cp "$SMF_BUNDLE/session.lock" /opt/liner-notes-ci/session.lock || exit 1
chown root:root /opt/liner-notes-ci /opt/liner-notes-ci/* || exit 1
chmod 755 /opt/liner-notes-ci/solaris_smf_control.py || exit 1
chmod 644 /opt/liner-notes-ci/solaris_smf_method.py /opt/liner-notes-ci/liner-notes-ci.xml || exit 1
chmod 600 /opt/liner-notes-ci/session.lock || exit 1
cp "$SMF_BUNDLE/liner-notes-smf-ci.sudoers" /etc/sudoers.d/liner-notes-smf-ci || exit 1
chown root:root /etc/sudoers.d/liner-notes-smf-ci || exit 1
chmod 440 /etc/sudoers.d/liner-notes-smf-ci || exit 1
/usr/sbin/visudo -c || exit 1
: > /etc/liner-notes-smf-ci.required
chown root:root /etc/liner-notes-smf-ci.required
chmod 644 /etc/liner-notes-smf-ci.required
```

The sudoers entry grants only the fixed helper plus the literal `session` argument.
It grants neither a root shell nor direct svcadm/svccfg/signal access. No account,
RBAC profile, password or SSH key is modified. Preparation itself does not register
or start a service. Ordinary main CI will create the disabled test registration,
then exercise only this service. Review directory ownership along the fixed paths;
the helper's own validation will fail closed if ancestors are not root-controlled.

If any preparation step fails, stop and use the rollback section. Do not continue
with a partial installation or grant broader access to make the test pass.

## Lifecycle assertions and evidence

The existing `Solaris Native CI` invokes the lifecycle test only after preparation.
Before preparation it records SMF lifecycle as unverified. A prepared but missing,
outdated or unusable helper fails the test, rather than being reported as passed.

The test checks disabled registration; persistent enable; loopback listener and
real login/article readback; restart preserving data; own-runtime crash recovery;
disabled state; invalid configuration reaching maintenance; corrected configuration
and clear; SMF log entries; and cleanup removing registration. CI's attempt to set
the unchanged start method must be denied. The installed fixed manifest and both
scripts must exactly match the current reviewed main sources.

Enabled state surviving service restart/crash is **not an OS boot test**. No host
reboot is performed. Full boot behavior remains unverified. A surviving stale
registration is refused on the next run and requires specific investigation; the
test never adopts an unexplained pre-existing service. A selection file left after
CI SIGKILL is privately archived only after the root helper owns a fresh disabled
registration and exclusive helper lease.

Results and the bounded synthetic SMF log are retained in the existing native
Actions log/artifact with source SHA. Test state resides only beneath the isolated
native source's `build/smf-lifecycle` and the private CI selection directory.

## Rollback — dedicated fixture only, after approval

Stop the relevant CI run normally and wait for its helper lease/cleanup. Verify no
process holds `/opt/liner-notes-ci/session.lock`. Do not remove an active lease.
If the dedicated service remains, first inspect its fixed method, ci-test/basic
credentials and contract to confirm it is this fixture. Then, and only then:

```sh
/usr/sbin/svcadm disable -s -T 30 svc:/application/liner-notes-ci:default
test "$(/usr/bin/svcs -H -o state svc:/application/liner-notes-ci:default)" = disabled || exit 1
/usr/sbin/svccfg delete svc:/application/liner-notes-ci
```

After no registration and no active helper remain, revoke and remove only the
fixed prepared files (preserve synthetic logs for diagnosis):

```sh
rm /etc/sudoers.d/liner-notes-smf-ci
/usr/sbin/visudo -c || exit 1
rm /etc/liner-notes-smf-ci.required
rm /opt/liner-notes-ci/solaris_smf_control.py
rm /opt/liner-notes-ci/solaris_smf_method.py
rm /opt/liner-notes-ci/liner-notes-ci.xml
rm /opt/liner-notes-ci/session.lock
rmdir /opt/liner-notes-ci
```

Do not delete any existing production FMRI, global SMF configuration or another
service's data. No OS restart is part of rollback.

Primary references: [Oracle SMF permissions](https://docs.oracle.com/cd/E37838_01/html/E60998/svcprivs.html),
[service start semantics](https://docs.oracle.com/cd/E37838_01/html/E60998/svcstart.html),
[service processes](https://docs.oracle.com/cd/E37838_01/html/E60998/svclist.html),
[repository list semantics](https://docs.oracle.com/cd/E88353_01/html/E72487/svccfg-8.html).

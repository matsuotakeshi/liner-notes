# Solaris CI temporary directory generation

## Failure evidence

- Main SHA: `589cb70e5dc3cd1b28e423f3a0a46b6e1b6875e9`.
- [Solaris Native CI run 44096709686](https://github.com/matsuotakeshi/liner-notes/actions/runs/44096709686), job `134085800989`.
- Artifact `14334981327` (`native-ci-solaris-results`): `solaris.log` and
  `results.json` report `Unexpected remote directory`, after 0.626 seconds,
  before source transfer or build. The artifact does not record the rejected path.
- Read-only inspection via the existing runner/CI accounts found the newest
  work directory `/export/home/ci-test/ci/native-solaris-XXm6u.Fb`, with no
  transferred source. Its dot-containing suffix fails the existing guard.
  Solaris mktemp can generate a dot as well as the previously observed underscore;
  the guard accepted only alphanumerics and underscores.

## Fix and trust boundary

Use the Python interpreter already selected and validated by the Solaris probe
to create `~/ci/native-solaris-*` with `tempfile.mkdtemp`. Creation is exclusive
and mode 0700, under umask 077. Existing directories are not reused or removed.
The transfer path guard and installed SMF method remain unchanged: generated
names must still pass them. No new root helper installation is needed.

FreeBSD/Rocky/Ubuntu directory generation, SSH host verification, exact-main
restriction, source/archive verification, host locks and test conditions are
unchanged. No unreviewed code is executed on the home VMs. This is a CI repair,
not a BBS feature or public manual change.

## Verification and resumption

`python tests/test_native_ci.py PolicyTests` passes 11 tests locally on Windows.
The new tests execute the generated Python program in a private temporary home,
check distinct directories and accepted names, preserve other OS commands and
reject interpreter/path injection and the observed dot-containing name.
The 0700 assertion runs on POSIX; Windows does not establish POSIX permissions.

Existing `Native CI Source Checks` runs the full native orchestration and SMF
fixture tests on GitHub-hosted Ubuntu for the PR, including the POSIX mode check.
After review and merge, the exact resulting main SHA must pass Solaris Native CI
through source transfer, build, tests and SMF lifecycle. Local tests are not a
substitute for that native result. Do not rerun the unchanged failing main SHA
as proof of the repair, dispatch a PR to the home runner, or reinstall SMF helpers.
No production service, VM setting or existing BBS/game data was changed.

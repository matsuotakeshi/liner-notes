# Door Broker implementation review, 2026-11-19

This is the implementer's review, not an independent security sign-off. PR #135
remains Draft. No production activation or private-host PR execution occurred.

| Finding | Change | Evidence to require |
| --- | --- | --- |
| Killing only the parent launcher could leave active workers/leases | Shared parent-liveness pipe; graceful parent waits for worker cleanup | Root hosted/jail fixture stops parent with an active detached Door child |
| flock alone disappears on worker crash, making uncertain state reusable | Persistent BUSY reservation, fsynced before launch, cleared only after cleanup | Fixture refuses a retained marker; no automated reset |
| Checking file size before acquiring flock permits a stale empty observation | Re-read size on the locked descriptor | Source inspection; reservation rejection fixture |
| Linux-only process enumeration/signalling cannot represent FreeBSD safely | FreeBSD kernel reaper subtree and no-new-privileges, no ps-derived PID signalling | Portable API-contract tests, main-only native own-descendant check and disposable jail integration |
| Service units could kill workers before cleanup or erase their state | Examples outside active unit paths; persistent StateDirectory, parent-only TERM, no forced KILL/restart | Hosted syntax/packaging checks; real systemd lifecycle still pending |
| Synthetic game success could be confused with real integration | Pin and inspect AREA-PC88 source; explicit protocol handoff | AREA-PC88 PR and dual-SHA integration evidence still required |

The original separation remains: only the trusted root supervisor may issue;
BBS supplies Core-authenticated identity; consumers are dedicated game-server
UIDs; every Door uses a unique concurrent slot UID; secrets are carried by FD,
not argv/environment values. Disabling the isolation service cannot activate the
legacy same-UID launch path. Ordinary Door configuration without opt-in remains
unchanged.

## Acceptance boundaries

- Portable tests exercise API rejection, malformed/failed native results,
  credential memory release, service-example invariants, ticket replay,
  generations, revocation and consumer scope.
- Windows can run those tests, but cannot establish Unix PTY/UID isolation.
- Hosted Linux executes the root integration inside its existing ephemeral
  container. It additionally checks NAWS, unauthorized mutations, replay,
  persistent game data, parent stop and descendants. Check the exact PR SHA.
- Native FreeBSD reaper/peer checks are part of the existing `make check`;
  cross-UID integration runs only inside the existing admitted package-upgrade
  VNET jail on trusted main. Existing workflow admission and helper hashes stay.
- Solaris adds peer-credential handling and its native check only. Full isolated
  launch continues to fail closed. No contract/privilege backend is inferred.

Still open: independent security review; Solaris process contracts and privilege
limits; SELinux domains/labels for the new services; installed systemd/rc/SMF
lifecycle; resource/fork exhaustion; forced worker death recovery; real
AREA-PC88 integration by its development workstream. In particular, file UID
separation is not a general filesystem/network sandbox. Administrative policy
must prevent slot groups from accessing private BBS/Broker/game-server data.

## Reproduction

```sh
python3 tests/test_door_broker.py -v
python3 tests/test_door_processes.py -v
# Existing hosted Linux CI calls the privileged fixture in its container:
python3 tests/test_door_broker_linux.py ./runtime
```

The last command must not run on a production host. On FreeBSD it requires the
admitted CI jail hostname and jail flag. The normal non-root native tests only
signal their own isolated reaper subtree. A platform skip is not a success for
that platform. Exact Actions results belong to the PR/check SHA, not an earlier
candidate. No existing successful result is reassigned to this revision.

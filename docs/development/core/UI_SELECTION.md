# UI selection candidate

Based on main `449fceca8a87d6f2f2cfdaa3523c4c2d1c8da813` (PR #132).
This implements the selection/settings portion of Issue #54, not MMM or WebUI.

## Configuration and operation

`--ui-policy FIXED|MEMBERS|ALL`, `--ui-default rtbbs`, and
`--ui-allow rtbbs` configure an immutable per-connection snapshot. Defaults are
FIXED/rtbbs/rtbbs. The runtime refuses unknown or duplicated profiles, an empty
allowlist or a default outside the allowlist. Only compiled adapters may appear.
No configuration reload affects existing sessions.

FreeBSD rc.conf uses `liner_notes_ui_policy`, `liner_notes_ui_default` and
`liner_notes_ui_allow`. Linux uses the same CLI flags in `LINER_NOTES_OPTIONS`.
Solaris uses the existing runtime argument configuration. No service is enabled
or restarted by this change.

RTBBS `UI` lists permitted profiles; `UI name` selects a profile and stores the
authenticated owner's preference. Guests under ALL select only for the current
connection. `UI RESET` selects the station default and removes the preference.
Successful persistence is acknowledged by `UI PREF SAVED name|DEFAULT`.
If storage/submission fails, the active selection can remain changed but is not
reported as saved. Display settings and their existing save-on-exit are separate.

## Core and persistence

Member Core checks identity, epoch, suspension and storage health before reading
or writing the authenticated actor's row. There is no caller-selected owner.
The new `ui_preferences(user PRIMARY KEY, profile TEXT)` table lives in the
existing feature settings DB (or authoritative SQLite DB). An absent row means
station default. Record streams, member IDs and the storage schema version do
not change. An older binary ignores this extra settings table; old backup tools
may not preserve it, so use the matching backup/restore tools.

Feature DB import, logical export/restore and conversion include the new table.
All writes use bound statements. Malformed values and SQL errors are failures,
not missing defaults. Compiled profile availability is a Session policy: a saved
name can remain valid while its adapter is disabled or temporarily uninstalled.

Auth results carry the saved preference as a typed field. Restoration happens
after Core authentication and adapter completion, and waits for a safe boundary
if necessary. The active policy wins; denied/unknown preferences fall back to the
configured default without deleting the stored value. An explicit choice made
during the current connection wins over login restoration. Logout clears that
choice's ownership. Existing Core ACLs, binary/connector and Door barriers apply.

## Checks

- Core tests: authenticated owner, suspended/stale actor, settings failures,
  validation, save/read/reset.
- Real SQLite tests: absent default, independent row, rollback and validation.
- Session tests with two small compiled test adapters: allowlist/default policy,
  restoration, unavailable profile retention, explicit guest choice, owner save.
- Real socket test: public RTBBS commands, policy denial, reset, restart and
  unknown saved profile fallback. Included in existing `make check`.
- JA/EN HELP and settings menus are updated. The canonical manual explicitly
  labels this as a development candidate; 0.74.1 gains no new capability.

Windows focused tests do not establish POSIX runtime compatibility. Hosted and
main-only native CI must validate their respective commits before acceptance.

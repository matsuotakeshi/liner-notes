# Board entry and information Core boundary

Baseline: main `ab6fbf054bf7f1d3c53e68cfc45973f5bcccfcca` (PR #72).
This LN-23/24 slice extracts BOARD destination admission and INFO retrieval.
No catalog/policy format, password hashing, ACL, command or public UI changes.

## Operations and trust boundary

`article_enter_board(actor, board, out)` rechecks store health and actor identity
before examining catalog visibility and the existing inherited password policy.
Guest access remains allowed where the current catalog permits it. The returned
ArticleBoardEntry owns its data:

- An authorized destination has id, title and the menu-node flag.
- A password challenge has only the destination id and password_board. This is
  the ID of the board whose password is required (possibly an ancestor), NOT
  its password revision. No title/menu metadata is returned in this case.
- Denial or storage failure clears the complete result.

The password policy retains its existing manager bypass and ancestor traversal.
Core does not collect/verify passwords, write grants, enforce input retry delays
or select a session's board. The adapter keeps BOARD PASSWORD and ERR try later,
including the requested destination separately from the ancestor password ID.
An unlocked result is not a reusable authorization capability: subsequent INFO,
read or write operations always check current rights and grants again.

`article_board_info(actor, board, out, information, capacity)` requires current
catalog_read authorization, including inherited password grants. It returns
owned id/title/description metadata and copies the long information text into
caller-owned storage. Text bounds are 80/320/8192 bytes, as in the existing
catalog/policy store, plus terminating NUL. Missing/oversized store text and
insufficient output capacity fail closed; all supplied output is cleared on
error. Empty information is valid and still requires room for its NUL.

The trusted session supplies member ID, credential epoch and grants; no caller
admin/visible flag is accepted. Pointers are borrowed for a call and outputs must
not overlap inputs. Both functions run on the existing single store worker and
clear the policy context after completion. They perform no article scan/body
read, SEEN update, audit write, editor operation or session mutation.

## Adapter and compatibility

board.c retains exact BOARD/INFO framing, the INFO default-current-board
selection, retry throttling response and the menu/password service actions.
It renders typed results and uses the existing common article error formatter.
This does not redesign BoardRequest/BoardResult, authentication, password input,
catalog administration or the WebUI. Those remain separate extraction work.

Reviewed docs/manual/boards.md and Japanese/English content HELP against these
operations. Their public descriptions are still correct; no new feature or
command is claimed. CHANGELOG records the refactor and only the three reviewed
source digests are updated in source-lock.json.

## Tests and limits of evidence

tests/test_article_core.c adds direct tests for guest/member entry, menu nodes,
private/deleted/out-of-range destinations, ancestor password challenges and
manager bypass, password revision changes, revoked identities, broken stores,
metadata non-disclosure, owned text, maximum lengths and short output buffers.
The existing 13 Core groups also run. Local UCRT GCC compilation uses
-std=c11 -Wall -Wextra -Werror; all 14 groups pass.

tests/test_stage60.py already covers real socket menu entry, password failures,
per-session grants and Japanese long information. It now also checks that INFO
is denied immediately after a password change and returns the complete unchanged
guide after re-entry. Local Python syntax validation is separate from executing
that runtime regression. Existing catalog/stage60/stage60-safety and other
integration tests remain in make check / hosted PR CI. Native OS evidence must
come from trusted-main CI after merge; no test VM or production changes here.

Canonical manual generation, links/search and Markdown/HTML/offline parity use
the existing tooling. This development contract is not added to the public book.

# Unread selection and member-owned marks

Baseline: main `ab031526b7b8d518293e3295ab6de59fe25b7f2c` (PR #61).
This LN-23/24 slice extracts UNREAD, NEW and SEEN/UNSEEN into the existing
article Core. NOTES, cross-board queries and UI profiles remain separate work.
No public command, transport, configuration or database format is added.

## API, authority and lifetime

`article_unread(actor, board, thread, start, out)` returns an owned
`ArticleUnreadPage`: total unread count, first unread ID, next page cursor and
up to eight rows containing copied ID/board/parent/title/root metadata.
`thread=0` selects the whole board; an article ID selects its containing thread,
including when that ID names a reply. The target must belong to this board.
Rows use ascending article IDs from the inclusive start cursor (0 means 1).
Total and first cover the complete scope regardless of the page cursor.

`article_mark_seen(actor, board, article, seen)` changes only the authenticated
actor's read state. There is no target-member override, even for administrators.
Zero means unread; nonzero values are normalized to read. It delegates durable
storage to the existing members module and reports write failure as
`ARTICLE_READ_STATE_STORAGE`; it does not report success on a failed append.

Each call rechecks member existence, session epoch, suspension, board read ACL
and password grants. Guests cannot use these member-specific operations. Write
permission is not required to mark a readable article. Effective visibility
excludes hidden/withdrawn articles and descendants from rows, counts and first
selection; both SEEN and UNSEEN reject an unavailable target. There is no manager
visibility bypass. Repository/visibility/read-state failures clear the entire
page, including results collected before the failure.

These functions run synchronously on the existing single board worker, like
the other article Core services. They are not thread-safe or reentrant. Actor
and grants are borrowed for the call; the policy context is cleared on return.
Inputs must not alias the output. No body buffer or index pointer escapes, and
the service does not allocate an article list or load article bodies. The
existing metadata scan remains linear in repository article count; this is not
completion of the large-data optimization or a new database index.

## Preserved adapter behavior

The legacy worker retains its early global guards and error precedence, builds
the trusted actor, and formats the same UNREAD/row/END NEXT or SEEN/UNSEEN text.
NEW selects the page's first unread ID and uses the existing structured article
read adapter. Selection/read/list operations never acknowledge articles: the
existing explicit SEEN and SEEN NEXT flow remains responsible for that action.
Stored IDs, read flags, article selection, read logging and session routing are
unchanged. The UI cannot provide another user's identity as an authority claim;
the internal actor is not a public wire/API authentication object.

## Verification and documentation

The direct Core target covers member isolation, explicit mark/unmark, thread
selection through replies, pagination/count/first consistency, empty/all-seen/
hidden scopes, no body access, write-ACL independence, current identity/read/
password checks, wrong-board/missing targets, health and read/write failures,
and clearing partially collected output. Existing mutation, read, moderation,
list/search and tree tests remain enabled in the same target.

Local UCRT GCC direct tests passed with `-std=c11 -Wall -Wextra -Werror`.
`tests/test_members.py` adds actual-protocol reply-scope selection and NEW without
implicit acknowledgement. Its existing coverage includes per-user marks and
restart persistence. Python syntax is checked locally; execution of the runtime
suite is delegated to hosted `make check`. Native OS validation runs only after
trusted-main integration and must cite that exact SHA. No Windows runtime
compatibility is claimed by the portable Core test.

Reviewed `docs/manual/article-search.md`, `boards.md` and both languages of
`help/*/unread.txt`: public behavior is unchanged, so their wording is retained.
Reviewed source-lock hashes track the extraction rather than a public spec
change. Prior PR/main CI success is not evidence for this new change.

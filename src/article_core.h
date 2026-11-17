#ifndef ARTICLE_CORE_H
#define ARTICLE_CORE_H
#include <stdint.h>
#include <stddef.h>

/* Internal, worker-thread-only API, not a wire protocol. The trusted session
 * supplies its authenticated member/epoch and board-password grants. The Core
 * rechecks identity, current ACLs and provenance for each operation. No caller
 * supplied owner/admin/visible flag is accepted. Pointers are borrowed for the
 * duration of a call; no body, credential or result pointer is retained. */
typedef struct {
    uint32_t user, epoch;
    const uint32_t *board_grants;
} ArticleActor;

typedef enum {
    ARTICLE_OK, ARTICLE_CONFIRM, ARTICLE_LOGIN_REQUIRED, ARTICLE_REVOKED,
    ARTICLE_MEMBER_STORAGE, ARTICLE_MODERATION_STORAGE, ARTICLE_POLICY_STORAGE,
    ARTICLE_EXTRAS_STORAGE, ARTICLE_NOT_FOUND, ARTICLE_OWNER_REQUIRED,
    ARTICLE_IMPORTED, ARTICLE_LINK_STORAGE, ARTICLE_HIDDEN,
    ARTICLE_POSTING_DENIED, ARTICLE_INVALID_TITLE, ARTICLE_ADMIN_REQUIRED,
    ARTICLE_STORAGE, ARTICLE_BUFFER_TOO_SMALL, ARTICLE_INVALID_READ,
    ARTICLE_WITHDRAWN_TARGET, ARTICLE_MODERATION_WRITE,
    ARTICLE_BOARD_DENIED, ARTICLE_INVALID_QUERY, ARTICLE_INVALID_CURSOR,
    ARTICLE_READ_STATE_STORAGE,
    ARTICLE_LEGACY_BOARDS, ARTICLE_LEGACY_MEMBERS, ARTICLE_BODY_LIMIT,
    ARTICLE_FULL, ARTICLE_INVALID_DRAFT, ARTICLE_LEGACY_BODY,
    ARTICLE_POST_UNAVAILABLE, ARTICLE_POST_UNCERTAIN, ARTICLE_DRAFT_CONFLICT
} ArticleOutcome;

/* Local editing admission and posting. Preparation grants no durable authority:
 * post repeats identity/ACL/parent/guest checks and validates current limits.
 * Borrowed strings must be NUL-terminated; caller keeps them alive for the call.
 * The authenticated name wins over guest_name. No remote origin/time override.
 * out_id is zero on failure; acknowledged IDs follow durable store publication.
 * No implicit SEEN, session selection or draft deletion. Worker-thread only. */
ArticleOutcome article_prepare_post(const ArticleActor *,uint32_t board,uint32_t parent);
ArticleOutcome article_post(const ArticleActor *,uint32_t board,uint32_t parent,
                            const char *guest_name,const char *title,const char *body,uint32_t *out_id);

/* Member-owned saved article copy, distinct from the live editor and posting.
 * Status reveals only the revision (0 = none). Resume copies metadata/body
 * only after current read/write ACL and parent visibility checks. Outputs are
 * caller-owned and cleared on error; body needs room for its terminating NUL.
 * Save/remove use the store's exact revision comparison, never force overwrite.
 * Removal/status remain available after board access is lost. Empty body can
 * be saved; posting quotas/format upgrades are checked by article_post later.
 * No target-member override, session/SEEN mutation or implicit copy removal.
 * Input/output buffers must not overlap. Calls run on the existing worker. */
typedef struct { uint32_t board,parent,revision; char title[81]; } ArticleDraftView;
ArticleOutcome article_draft_status(const ArticleActor *,uint32_t *revision);
ArticleOutcome article_draft_resume(const ArticleActor *,ArticleDraftView *,char *body,size_t capacity);
ArticleOutcome article_draft_save(const ArticleActor *,uint32_t board,uint32_t parent,
                                 const char *title,const char *body,uint32_t expected,uint32_t *revision);
ArticleOutcome article_draft_remove(const ArticleActor *,uint32_t expected,uint32_t *revision);

/* Board entry resolves current visibility and inherited password requirements.
 * password_board is the board requiring a password, NOT a password revision;
 * on a challenge only id/password_board are returned. Otherwise title/menu
 * describe the authorized destination. Core never changes session selection,
 * grants or retry throttles; a later request must reauthorize independently. */
typedef struct { uint32_t id,password_board; int menu; char title[81]; } ArticleBoardEntry;
ArticleOutcome article_enter_board(const ArticleActor *,uint32_t board,ArticleBoardEntry *out);
/* INFO requires current read access, including all password grants. Caller owns
 * metadata and long-information storage (capacity includes NUL). Errors clear
 * both outputs. No article scan, read mark, selection or editor side effects. */
typedef struct { uint32_t id; size_t information_size; char title[81],description[321]; } ArticleBoardInfo;
ArticleOutcome article_board_info(const ArticleActor *,uint32_t board,ArticleBoardInfo *out,
                                  char *information,size_t capacity);

ArticleOutcome article_withdraw(const ArticleActor *,uint32_t board,
                               uint32_t article,int confirmed);
ArticleOutcome article_retitle(const ArticleActor *,uint32_t board,
                              uint32_t article,const char *title);
/* Local moderation only: current board manager AND read access required.
 * Imported articles are allowed; withdrawn articles cannot be hidden/restored.
 * Restore clears this article's local flag, never an ancestor's or origin's
 * withdrawal. No extras/link event is emitted by either operation. */
ArticleOutcome article_hide(const ArticleActor *,uint32_t board,uint32_t article);
ArticleOutcome article_restore(const ArticleActor *,uint32_t board,uint32_t article);
/* A structured read result owns its metadata; body storage belongs to the
 * caller. Hidden/withdrawn results contain only IDs and state, never title,
 * author or body. On any error the result and supplied body buffer are cleared.
 * NULL actor means guest. Inspection always rechecks the actual board's ACL.
 * No selection, seen-state or access-log side effect: the legacy worker keeps
 * those responsibilities until their own service extraction. */
typedef enum { ARTICLE_EMPTY, ARTICLE_VISIBLE, ARTICLE_CONCEALED, ARTICLE_WITHDRAWN } ArticleViewState;
typedef struct {
    uint32_t id,board,parent;
    ArticleViewState state;
    int inspected_hidden;
    size_t body_size;
    char author[33],title[81];
} ArticleView;
ArticleOutcome article_read(const ArticleActor *,uint32_t article,int inspect,
                            ArticleView *out,char *body,size_t capacity);
/* Owned, bounded current-board pages. No body/hidden metadata is returned.
 * LIST returns up to 5 rows and the visible board total; SEARCH up to 8 rows
 * after at most 256 index candidates, with total=0 (no global count). next=0
 * means end; start=0 means first page. Reauthorize every call, not just page 1.
 * Errors clear the entire page. No SEEN/cursor/session state is mutated. */
typedef struct { uint32_t id,board,parent; char title[81]; } ArticleSummary;
/* Select a related visible article; no body/read mark/audit/selection mutation.
 * Hidden parent chains move upward; hidden children/next siblings move forward,
 * previous siblings backward. A hidden current article may still navigate out.
 * id=0 means no visible neighbour. Invalid references/cycles fail closed and
 * all errors clear the caller-owned result. Recheck identity/board ACL per call. */
typedef enum { ARTICLE_PARENT, ARTICLE_CHILD, ARTICLE_NEXT, ARTICLE_PREVIOUS } ArticleDirection;
ArticleOutcome article_navigate(const ArticleActor *,uint32_t board,uint32_t current,
                                ArticleDirection direction,ArticleSummary *out);
typedef struct {
    uint32_t total,next;
    unsigned count;
    ArticleSummary rows[8];
} ArticlePage;
ArticleOutcome article_list(const ArticleActor *,uint32_t board,uint32_t start,ArticlePage *out);
ArticleOutcome article_search(const ArticleActor *,uint32_t board,uint32_t start,
                              const char *query,ArticlePage *out);
/* NEWS metadata pages: inclusive timestamps; until=0 means no upper bound.
 * Unknown (zero) creation times are excluded. Scope root need not be readable
 * for ALL/SUBTREE, but every result's board must be. Scan at most 256 IDs,
 * return at most 8 rows, total=0. start=0 means 1; an empty continuation is
 * valid. No body reads, acknowledgement or check-time/session mutation. */
typedef enum { ARTICLE_HERE, ARTICLE_ALL, ARTICLE_SUBTREE } ArticleScope;
ArticleOutcome article_news(const ArticleActor *,uint32_t board,ArticleScope scope,
                            uint32_t start,uint64_t since,uint64_t until,ArticlePage *out);
/* Borrowed exact-author/literal-query filter. "*" means any author/no term.
 * Named authors must exist; query is 1..80 bytes without controls. Core checks
 * each result's read ACL and effective visibility before body matching.
 * FIND: up to 8 rows/256 index candidates, total=0, next=unscanned cursor.
 * SCAN: first match in the chosen direction, or article.id=0 for end. No 256
 * candidate cutoff for sequential reading. start=0 means 1; descending starts
 * above the repository end are clamped (UINT32_MAX selects newest).
 * Result next is an inclusive cursor, not a capability; reauthorize each call.
 * No body, read mark, audit or session selection is returned/mutated. */
typedef struct {
    uint32_t board;
    ArticleScope scope;
    const char *author,*query;
} ArticleFilter;
typedef struct { ArticleSummary article; uint32_t next; } ArticleScan;
ArticleOutcome article_find(const ArticleActor *,const ArticleFilter *,uint32_t start,ArticlePage *out);
ArticleOutcome article_scan(const ArticleActor *,const ArticleFilter *,uint32_t start,
                            int descending,ArticleScan *out);
/* Whole thread containing article, in existing depth-first order. start=0
 * begins at its root; other cursors must belong to this thread and board.
 * Up to 8 visible rows, next=next visible node or 0. Hidden nodes contribute
 * no rows. root remains the thread identifier even for an empty hidden tree.
 * Depth is structural; indentation belongs to UI. Errors clear all output. */
typedef struct { ArticleSummary summary; uint32_t depth; } ArticleTreeRow;
typedef struct {
    uint32_t root,next;
    unsigned count;
    ArticleTreeRow rows[8];
} ArticleTreePage;
ArticleOutcome article_tree(const ArticleActor *,uint32_t board,uint32_t article,
                            uint32_t start,ArticleTreePage *out);
/* Authenticated member's own read state. No target-member override; only
 * readable, effectively visible articles may be marked. Nonzero seen=read. */
ArticleOutcome article_mark_seen(const ArticleActor *,uint32_t board,uint32_t article,int seen);
/* thread=0 means whole board; otherwise use that article's containing thread.
 * total/first cover the whole scope; rows start at start (0=1), at most 8.
 * next is the next eligible ID or 0. Neither listing nor selecting first
 * acknowledges delivery. Every call rechecks identity/ACL; errors clear output. */
typedef struct { ArticleSummary summary; uint32_t root; } ArticleUnreadRow;
typedef struct {
    uint32_t total,first,next;
    unsigned count;
    ArticleUnreadRow rows[8];
} ArticleUnreadPage;
ArticleOutcome article_unread(const ArticleActor *,uint32_t board,uint32_t thread,
                              uint32_t start,ArticleUnreadPage *out);
/* Root threads, ascending IDs, inclusive start (0=1), up to 5 rows.
 * Replies/latest/unread include only effectively visible nodes. Latest means
 * greatest visible ID, not greatest timestamp; updated is that node's created.
 * Guests may list all threads; unread_only requires membership. No marking.
 * Total covers all qualifying roots; next is the next root or 0.
 * Output is owned, with no formatted dates/body/index pointers. Errors clear it. */
typedef struct {
    ArticleSummary summary;
    uint32_t replies,latest,unread;
    uint64_t updated;
} ArticleNoteRow;
typedef struct {
    uint32_t total,next;
    unsigned count;
    int unread_known;
    ArticleNoteRow rows[5];
} ArticleNotesPage;
ArticleOutcome article_notes(const ArticleActor *,uint32_t board,uint32_t start,
                             int unread_only,ArticleNotesPage *out);
/* Catalog-order board discovery. start=0 means board 1; children_only filters
 * direct children of start (legacy menu-node listing). At most eight owned
 * rows; next is an inclusive catalog cursor. Visible locked boards keep title
 * and write capability but have unread_known=0. Guests never read seen state.
 * NEXTBOARD requires membership, skips current and wraps to board 1. id=0
 * means no other readable board with unread articles. No session/read mutation.
 * Recheck identity/grants on every call; errors clear all output. */
typedef struct {
    uint32_t id,parent,depth,unread;
    int writable,unread_known;
    char title[81];
} ArticleBoardRow;
typedef struct { uint32_t next; unsigned count; ArticleBoardRow rows[8]; } ArticleBoardsPage;
ArticleOutcome article_boards(const ArticleActor *,uint32_t start,int children_only,ArticleBoardsPage *out);
ArticleOutcome article_next_board(const ArticleActor *,uint32_t current,ArticleBoardRow *out);

#endif

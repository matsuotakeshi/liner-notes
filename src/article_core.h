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
    ARTICLE_WITHDRAWN_TARGET, ARTICLE_MODERATION_WRITE
} ArticleOutcome;

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
#endif

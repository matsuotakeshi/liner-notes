#ifndef ARTICLE_CORE_H
#define ARTICLE_CORE_H
#include <stdint.h>

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
    ARTICLE_POSTING_DENIED, ARTICLE_INVALID_TITLE
} ArticleOutcome;

ArticleOutcome article_withdraw(const ArticleActor *,uint32_t board,
                               uint32_t article,int confirmed);
ArticleOutcome article_retitle(const ArticleActor *,uint32_t board,
                              uint32_t article,const char *title);
#endif

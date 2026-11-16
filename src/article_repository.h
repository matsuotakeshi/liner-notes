#ifndef ARTICLE_REPOSITORY_H
#define ARTICLE_REPOSITORY_H
#include <stdint.h>
#include <stddef.h>

/* Core/storage boundary, implemented by the single-owner board store.
 * Never called by UI/transport code. Copies metadata instead of exposing the
 * SQLite-backed index's transient pointers. The existing worker queue owns
 * serialization of lookup, authorization, journal sync and index publication. */
typedef struct { uint32_t board,parent,body_size,root,depth,child,next,prev; uint64_t created; char author[33],title[81]; } ArticleMetadata;
int article_repository_find(uint32_t article,ArticleMetadata *out);
/* -1 visibility/storage failure; otherwise 0 visible, 1 concealed. */
int article_repository_unavailable(uint32_t article);
/* -1 storage uncertainty, 0 local origin, 1 imported origin. */
int article_repository_imported(uint32_t article);
/* Called only after the title journal has been durably committed. */
void article_repository_title_changed(uint32_t article,const char *title);
/* Copy a complete NUL-terminated body, or return 0 without claiming success. */
int article_repository_body(uint32_t article,char *body,size_t capacity);
uint32_t article_repository_count(void);
/* Existing index selects candidates, never authorizes visibility. 0=end;
 * otherwise candidate >= start. Database errors retain fail-closed behavior. */
uint32_t article_repository_search_next(uint32_t start,const char *query);
/* Ordered candidate selection for SCAN; descending candidate <= start.
 * Like search_next, 0=end. Core validates bounds and strict cursor progress. */
uint32_t article_repository_search_ordered(uint32_t start,const char *query,int descending);
/* -1 storage failure; 0 no literal match; 1 match. Core checks ACL and
 * effective visibility before asking storage to inspect the body. */
int article_repository_body_contains(uint32_t article,const char *query);
/* Existing depth-first index traversal, including invisible nodes; 0=end.
 * Core must independently check board/root membership and visibility. */
uint32_t article_repository_tree_next(uint32_t article);
/* Store-owned posting configuration/counters, never supplied by a UI caller.
 * 0 means configuration/storage read failure. The worker serializes snapshots
 * with append; zero limits retain the existing unlimited policy. */
typedef struct {
    unsigned version;
    int members_required,guest_allowed,storage_failed;
    uint32_t total_limit,user_limit,user_posts;
    size_t body_limit;
} ArticlePostingPolicy;
int article_repository_posting_policy(uint32_t board,uint32_t user,ArticlePostingPolicy *out);
/* -1 failure, 0 board quota exhausted, 1 fits. No reservation or mutation. */
int article_repository_post_capacity(uint32_t board,size_t body_size);
/* Core-authorized LOCAL append only. Caller validates lengths/ACL/quotas;
 * journal sync precedes publication, ID and counter increment. No remote
 * identity/timestamp input is exposed here. Failure returns no acknowledged ID. */
int article_repository_append(uint32_t board,uint32_t parent,uint32_t user,
                              const char *author,const char *title,const char *body,uint32_t *id);
#endif

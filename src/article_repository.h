#ifndef ARTICLE_REPOSITORY_H
#define ARTICLE_REPOSITORY_H
#include <stdint.h>
#include <stddef.h>

/* Core/storage boundary, implemented by the single-owner board store.
 * Never called by UI/transport code. Copies metadata instead of exposing the
 * SQLite-backed index's transient pointers. The existing worker queue owns
 * serialization of lookup, authorization, journal sync and index publication. */
typedef struct { uint32_t board,parent,body_size; char author[33],title[81]; } ArticleMetadata;
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
/* -1 storage failure; 0 no literal match; 1 match. Core checks ACL and
 * effective visibility before asking storage to inspect the body. */
int article_repository_body_contains(uint32_t article,const char *query);
#endif

#ifndef ARTICLE_REPOSITORY_H
#define ARTICLE_REPOSITORY_H
#include <stdint.h>

/* Core/storage boundary, implemented by the single-owner board store.
 * Never called by UI/transport code. Copies metadata instead of exposing the
 * SQLite-backed index's transient pointers. The existing worker queue owns
 * serialization of lookup, authorization, journal sync and index publication. */
typedef struct { uint32_t board; char author[33]; } ArticleMetadata;
int article_repository_find(uint32_t article,ArticleMetadata *out);
int article_repository_unavailable(uint32_t article);
/* -1 storage uncertainty, 0 local origin, 1 imported origin. */
int article_repository_imported(uint32_t article);
/* Called only after the title journal has been durably committed. */
void article_repository_title_changed(uint32_t article,const char *title);
#endif

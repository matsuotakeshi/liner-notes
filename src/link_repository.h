#ifndef LINK_REPOSITORY_H
#define LINK_REPOSITORY_H
#include "link_core.h"
/* Internal worker-owned storage hooks. No request/response/UI types. */
LinkOutcome link_repository_health(void);
int link_repository_enabled(uint32_t board,uint32_t user);
int link_repository_version(void);
void link_repository_fail(void);
/* next: 1 row, 0 end, -1 storage failure. body copies only when requested. */
int link_repository_next(uint32_t board,uint32_t start,LinkItem *);
int link_repository_body(uint32_t id,char *,size_t,size_t *);
int link_repository_lookup(uint32_t board,const char *origin,const char *key,LinkReference *,int *done);
int link_repository_on_board(uint32_t id,uint32_t board);
int link_repository_removed(uint32_t id);
/* 1 immutable match, 0 conflict, -1 storage failure. */
int link_repository_matches(uint32_t id,const LinkEvent *,const char *alias,const char *hash);
LinkOutcome link_repository_post(const LinkActor *,const LinkEvent *,const char *alias,int validate_only,uint32_t *id);
int link_repository_intent(const LinkActor *,const LinkEvent *,uint32_t id,const char *alias,const char *hash);
int link_repository_recover(void);
/* SHA-256 content check is shared with recovery, not peer authentication. */
int link_content_hash(const char *,char out[65]);
#endif

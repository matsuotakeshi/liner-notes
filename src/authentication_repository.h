#ifndef AUTHENTICATION_REPOSITORY_H
#define AUTHENTICATION_REPOSITORY_H
#include <stdint.h>
/* Internal board-worker hooks, never a UI authorization API. The unread scan
 * preserves the existing index/visibility/grant context; no marking or writes. */
int authentication_repository_healthy(void);
void authentication_repository_unread(uint32_t,uint32_t *articles,uint32_t *boards);
#endif

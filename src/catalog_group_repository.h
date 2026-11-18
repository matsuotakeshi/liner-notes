#ifndef CATALOG_GROUP_REPOSITORY_H
#define CATALOG_GROUP_REPOSITORY_H
#include <stdint.h>
/* Internal persistence boundary; the board worker serializes all calls.
 * Core validates identity/ACL/group values before mutation. No UI entry point.
 * Existing delegated/group_moves tables and recovery records remain unchanged. */
int catalog_group_repository_ready(void);
int catalog_group_repository_delegate(uint32_t actor,uint32_t board,unsigned group);
/* Persist the intent before applying ACL/member changes; failure may occur
 * after durable changes. Recovery, not rollback, completes such an operation. */
int catalog_group_repository_move(uint32_t actor,uint32_t board,unsigned from,unsigned to);
/* Startup-only replay of already persisted intents, also invoked after save.
 * No live-session notifications: startup has no sessions to invalidate. */
int catalog_group_repository_recover(void);
#endif

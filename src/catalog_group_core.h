#ifndef CATALOG_GROUP_CORE_H
#define CATALOG_GROUP_CORE_H
#include "catalog_core.h"
/* Worker-owned services. Trusted session supplies identity and epoch; never
 * trust a caller's admin/delegation flag. No terminal or transport dependency. */
typedef enum {
    GROUP_CORE_OK,GROUP_CORE_ADMIN_REQUIRED,GROUP_CORE_REVOKED,
    GROUP_CORE_MEMBER_STORAGE,GROUP_CORE_MODERATION_STORAGE,
    GROUP_CORE_POLICY_STORAGE,GROUP_CORE_CATALOG_STORAGE,
    GROUP_CORE_SETTINGS_STORAGE,GROUP_CORE_INVALID
} CatalogGroupOutcome;
/* Global admin only. group 1..100 must occur in this board's own ACL;
 * Internal API zero clears all recorded delegations for this board; the
 * unchanged terminal number parser rejects zero before reaching this API. */
CatalogGroupOutcome catalog_group_delegate(const CatalogActor *,uint32_t board,unsigned group);
/* Global admin only. Replace the source in this board's ACL, migrate global
 * member memberships, then migrate its delegation row through durable replay.
 * Owns no returned pointers. Output clears on failure, input must not alias it.
 * After success notify vault for every slot 1..through, revoke other sessions,
 * and retain actor_epoch on the initiating session. Do not reuse this result. */
typedef struct { uint32_t through,actor_epoch; } CatalogGroupChange;
CatalogGroupOutcome catalog_group_move(const CatalogActor *,uint32_t board,unsigned from,unsigned to,CatalogGroupChange *out);
#endif

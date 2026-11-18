#ifndef CATALOG_CORE_H
#define CATALOG_CORE_H
#include "catalog.h"
/* Internal worker-only service. Authenticated identity is supplied by the
 * trusted session; permissions are rechecked, never accepted as input flags.
 * Caller owns outputs, no aliases/pointers are retained. Failure clears outputs.
 * No terminal, transport, session-selection or credential mutation. */
typedef struct { uint32_t user,epoch; } CatalogActor;
typedef enum {
    CATALOG_CORE_OK,CATALOG_CORE_LOGIN_REQUIRED,CATALOG_CORE_REVOKED,
    CATALOG_CORE_MEMBER_STORAGE,CATALOG_CORE_MODERATION_STORAGE,
    CATALOG_CORE_POLICY_STORAGE,CATALOG_CORE_STORAGE,CATALOG_CORE_DENIED,
    CATALOG_CORE_INVALID,CATALOG_CORE_CURSOR,
    CATALOG_CORE_NOT_EMPTY,CATALOG_CORE_INFO_LIMIT,CATALOG_CORE_BUFFER
} CatalogOutcome;
typedef struct { uint32_t board,user; } CatalogRole;
typedef struct { unsigned count;CatalogRole rows[CATALOG_MAX]; } CatalogRoles;
typedef struct { unsigned count;uint32_t next;CatalogChange rows[8]; } CatalogHistory;
/* Shared authorization for worker-owned board management services. */
CatalogOutcome catalog_core_admin(const CatalogActor *);
CatalogOutcome catalog_core_manage(const CatalogActor *,uint32_t board);
CatalogOutcome catalog_core_info(const CatalogActor *,uint32_t board,CatalogConfig *out);
/* Declarative six-field catalog specification, not a BBS command. Keep exact
 * spelling in the existing journal. Schema/ACL parser lives in the repository;
 * Core validates authority over both current and proposed parent scopes. */
CatalogOutcome catalog_core_update(const CatalogActor *,const char *specification,uint32_t *out_id);
/* Current assignments for every managed board, including inherited authority.
 * user=0 denotes no direct assignment. Ordinary members receive an empty list. */
CatalogOutcome catalog_core_roles(const CatalogActor *,CatalogRoles *out);
/* Same legacy descending cursor: 0 starts newest, otherwise cursor-1 is the
 * first candidate. Up to 8 authorized rows/256 scanned records. next=0 is end.
 * Current-board SIGOP/admin admits history; each record requires its own
 * current board management permission, regardless of the displayed board. */
CatalogOutcome catalog_core_history(const CatalogActor *,uint32_t current_board,uint32_t cursor,CatalogHistory *out);
#endif

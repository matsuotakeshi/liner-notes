#ifndef VAULT_QUERY_CORE_H
#define VAULT_QUERY_CORE_H
#include "vault.h"
/* Worker-owned, synchronous services. Resolve Principal through the trusted
 * IdentityProvider on EVERY request; clients must not supply capabilities.
 * Vault operations enforce current read/board/password/audit ACLs. No borrowed
 * result pointers, mutation, protocol strings or session/UI dependencies.
 * Inputs must not alias outputs. Failure clears the entire output. */
typedef struct {
    uint32_t cursor;
    const char *scope; /* NULL: all accessible; "": explicit root. */
    const char *pattern;
    int check_scope,news,date_valid,has_until;
    uint64_t since,until;
} VaultQuery;
typedef struct { size_t count;uint32_t next;VaultEntry rows[8];uint64_t downloads[8]; } VaultQueryFiles;
typedef struct { size_t count;uint64_t bytes;uint32_t ids[16]; } VaultQueryMatch;
typedef struct { char name[49],display[81]; } VaultQueryChild;
/* Keep the legacy whole-backend-page rule: seven already collected children
 * plus eight on the final page can produce fifteen, without losing its cursor. */
typedef struct { size_t count;uint32_t next;VaultQueryChild rows[15]; } VaultQueryChildren;
typedef struct { VaultEntry entry;char protocol[12]; } VaultQueryDetail;
typedef struct { size_t count;uint32_t next;VaultBoard rows[8]; } VaultQueryBoards;
typedef struct { size_t count;uint32_t next;VaultCategory rows[8]; } VaultQueryCategories;
typedef struct { size_t count;uint32_t next;VaultEvent rows[4]; } VaultQueryAudit;
VaultResult vault_query_files(Vault *,const Principal *,const VaultQuery *,VaultQueryFiles *);
VaultResult vault_query_match(Vault *,const Principal *,const char *,const char *,VaultQueryMatch *);
VaultResult vault_query_children(Vault *,const Principal *,const char *,uint32_t,VaultQueryChildren *);
VaultResult vault_query_detail(Vault *,const Principal *,uint32_t,VaultQueryDetail *);
VaultResult vault_query_boards(Vault *,const Principal *,uint32_t,VaultQueryBoards *);
VaultResult vault_query_categories(Vault *,const Principal *,uint32_t,VaultQueryCategories *);
VaultResult vault_query_audit(Vault *,const Principal *,uint32_t,VaultQueryAudit *);
#endif

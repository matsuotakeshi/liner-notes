#ifndef VAULT_MANAGEMENT_CORE_H
#define VAULT_MANAGEMENT_CORE_H
#include "vault.h"
/* Synchronous services owned by the existing vault worker. Supply a freshly
 * resolved trusted Principal on every call, never client capability fields.
 * Repository ACLs/live guards remain authoritative; no blanket admin gate.
 * Names/subjects/ACL expressions use the vault namespace, not UI user numbers.
 * Inputs are borrowed for this call and must not alias result snapshots. */
typedef enum {
    VAULT_FILE_DESCRIPTION,VAULT_FILE_RENAME,VAULT_FILE_OWNER,VAULT_FILE_ASCII,
    VAULT_FILE_CATEGORY,VAULT_FILE_HIDE,VAULT_FILE_RESTORE,VAULT_FILE_DELETE
} VaultFileChangeKind;
typedef struct { VaultFileChangeKind kind;const char *text;int ascii; } VaultFileChange;
typedef enum { VAULT_BOARD_NAME,VAULT_BOARD_GUIDE,VAULT_BOARD_SIGOP,VAULT_BOARD_OPTIONS } VaultBoardField;
/* A retention error is distinct: the preceding metadata call already succeeded.
 * This does not imply rollback and must not trigger an automatic mutation retry. */
typedef struct { VaultResult result;int retention_failed; } VaultBoardUpdate;
typedef struct { uint32_t board,revision; } VaultBoardGrant;
VaultResult vault_manage_file(Vault *,const Principal *,uint32_t,const VaultFileChange *);
VaultResult vault_manage_copy(Vault *,const Principal *,uint32_t,const char *,VaultEntry *);
VaultResult vault_manage_policy(Vault *,const Principal *,const char *,const char *,const char *);
/* Field values retain vault's configuration grammar, just like ACL expressions;
 * terminal command parsing and password/user-ID prompts do not belong here. */
VaultBoardUpdate vault_manage_board(Vault *,const Principal *,const char *,VaultBoardField,const char *);
VaultResult vault_manage_inspect(Vault *,const Principal *,const char *,int for_edit,VaultBoard *);
VaultResult vault_manage_password(Vault *,const Principal *,const char *,const char *);
VaultResult vault_manage_unlock(Vault *,const Principal *,const char *,const char *,VaultBoardGrant *);
#endif

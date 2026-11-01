#ifndef VAULT_BRIDGE_H
#define VAULT_BRIDGE_H
#include "principal.h"
#include "board.h"
#include <stddef.h>
enum { VAULT_CMD_LIST=1,VAULT_CMD_INFO,VAULT_CMD_HIDE,VAULT_CMD_SEARCH,VAULT_CMD_IN,VAULT_CMD_CATEGORY,VAULT_CMD_CATEGORIES,VAULT_CMD_AUDIT,VAULT_CMD_DESCRIPTION,VAULT_CMD_RESTORE,VAULT_CMD_NEWS,VAULT_CMD_BOARDS,VAULT_CMD_DELETE,VAULT_CMD_STATS,VAULT_CMD_HISTORY,VAULT_CMD_BOARDSET,VAULT_CMD_BOARDINFO,VAULT_CMD_OWNER,VAULT_CMD_ASCII,VAULT_CMD_TEXT,VAULT_CMD_BOARDNAME,VAULT_CMD_BOARDGUIDE,VAULT_CMD_BOARDSIGOP,VAULT_CMD_BOARDEDIT,VAULT_CMD_MATCH,VAULT_CMD_RENAME,VAULT_CMD_PASSWORD,VAULT_CMD_UNLOCK,VAULT_CMD_OPTIONS,VAULT_CMD_COPY,VAULT_CMD_CHILDREN };
int vault_bridge_open(const char *directory);
void vault_bridge_close(void);
/* Thin application adapter. The current board worker supplies its identity
 * provider; the vault library never reads an account or article store. */
void vault_bridge_execute(IdentityProvider identity,uint32_t user,uint32_t epoch,
    int operation,uint32_t id,const char *argument,char *out,size_t size);
void vault_bridge_transfer(IdentityProvider,uint32_t user,uint32_t epoch,const char *line,unsigned protocol,char *out,size_t size);
void vault_bridge_guard(int (*guard)(void *),void *context);
void vault_bridge_binary(IdentityProvider,const BoardRequest *,BoardResult *);
#endif

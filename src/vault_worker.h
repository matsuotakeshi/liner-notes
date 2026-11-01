#ifndef VAULT_WORKER_H
#define VAULT_WORKER_H
#include "board.h"
#include "principal.h"
int vault_worker_start(void);
void vault_worker_stop(void);
int vault_worker_submit(const BoardRequest *,const Principal *);
int vault_worker_poll(BoardResult *);
/* Runtime publishes connection lifetimes; board owner publishes revocation.
 * No member-store pointer or lock crosses into the I/O worker. */
void vault_worker_session(int slot,unsigned long session,int active);
void vault_worker_account(uint32_t user,uint32_t epoch);
void vault_worker_health(int healthy);
#endif

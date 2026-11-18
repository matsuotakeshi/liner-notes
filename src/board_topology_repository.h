#ifndef BOARD_TOPOLOGY_REPOSITORY_H
#define BOARD_TOPOLOGY_REPOSITORY_H
#include <stdint.h>
#include <stddef.h>
/* Single board worker owns these hooks. Metadata copies never alias db_at().
 * No UI calls, ACL decisions or transaction ownership cross this boundary. */
typedef struct { uint32_t board,parent,root,bytes;int removed; } BoardTopologyArticle;
typedef struct { uint32_t board,depth; } BoardDeletion;
uint32_t board_topology_repository_count(void);
int board_topology_repository_article(uint32_t id,BoardTopologyArticle *out);
int board_topology_repository_ready(void);
/* Mark policy/access storage uncertain on deletion/pruning failure, as before. */
int board_topology_repository_delete(uint32_t board,uint32_t actor);
/* Append the existing M/S journal event and replay into the existing index.
 * through is the worker's preflight snapshot; IDs are never reassigned. */
int board_topology_repository_move(uint32_t actor,uint32_t from,uint32_t to,
                                   uint32_t root,uint32_t through,int branch);
/* Atomically persist the complete plan before invoking existing restart-safe
 * recovery. Recovery/pruning can fail after commit: never claim rollback. */
int board_topology_repository_cascade(uint32_t actor,const BoardDeletion *,size_t count);
#endif

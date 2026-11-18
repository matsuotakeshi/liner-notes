#ifndef BOARD_TOPOLOGY_CORE_H
#define BOARD_TOPOLOGY_CORE_H
#include "catalog_core.h"
/* Internal, worker-serialized services. Caller supplies trusted session identity,
 * never admin flags. UI owns confirmations, selected board and wire messages.
 * Existing article IDs, signed provenance and durable formats are preserved. */
typedef enum {
    TOPOLOGY_OK,TOPOLOGY_DENIED,TOPOLOGY_REVOKED,TOPOLOGY_MEMBER_STORAGE,
    TOPOLOGY_MODERATION_STORAGE,TOPOLOGY_POLICY_STORAGE,TOPOLOGY_CATALOG_STORAGE,
    TOPOLOGY_EXTRAS_STORAGE,TOPOLOGY_SETTINGS_STORAGE,TOPOLOGY_INVALID,
    TOPOLOGY_NOT_EMPTY,TOPOLOGY_MOVE_TARGET,TOPOLOGY_FULL
} BoardTopologyOutcome;
BoardTopologyOutcome board_topology_delete(const CatalogActor *,uint32_t board,int cascade);
BoardTopologyOutcome board_topology_move(const CatalogActor *,uint32_t from,uint32_t root,
                                         uint32_t to,int branch);
/* Internal admission helper for posting/import/move. Not an authorized UI read.
 * 1 fits, 0 quota full, -1 uncertain metadata/storage. Removed articles do not
 * consume quota; hidden articles do. Zero limits mean unlimited. No mutation. */
int board_topology_capacity(uint32_t board,uint32_t extra_count,uint64_t extra_bytes);
#endif

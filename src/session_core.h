#ifndef SESSION_CORE_H
#define SESSION_CORE_H
#include "board.h"
#include <stdint.h>
typedef struct {
    unsigned long id;
    int slot;
    int active;
    int structured; /* opt-in structured content in worker completions */
    int board_enabled;
    int member_mode;
    uint64_t connected_at;
    char guest_handle[33];
    uint64_t now;
    uint64_t wake;
    uint64_t auth_after;
    int closing;
    int pending;
    uint32_t board_grants[1025];
    uint32_t vault_grants[1025];
    uint32_t current;
    uint32_t user;
    uint32_t board;
    uint32_t user_epoch;
    unsigned member_level;
    char username[33];
} SessionCore;
/* Single event-loop owner. Queue copies the request synchronously; no pointer is
 * retained. Domain workers still validate epoch, account state and ACL. */
typedef struct { void *context; int (*enqueue)(void *,const BoardRequest *); } SessionOperationPort;
typedef enum { SESSION_OPERATION_ACCEPTED,SESSION_OPERATION_INACTIVE,
    SESSION_OPERATION_BUSY,SESSION_OPERATION_THROTTLED,SESSION_OPERATION_UNAVAILABLE } SessionOperationStatus;
SessionOperationStatus session_core_submit(SessionCore *,BoardRequest *,SessionOperationPort);
/* Reject stale completions before mutating state. A valid result is borrowed. */
int session_core_complete(SessionCore *,const BoardResult *,uint64_t now);
void session_core_identity(SessionCore *,const BoardResult *,uint64_t now);
#endif

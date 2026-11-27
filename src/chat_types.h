#ifndef LINER_CHAT_TYPES_H
#define LINER_CHAT_TYPES_H
#include <stdint.h>
#include "app_limits.h"
#define CHAT_TEXT 512
#define CHAT_HISTORY 20
/* Event-loop-owned room; no sockets, BBS files or member database. Trusted
 * callers authenticate operations and provide an immutable identity snapshot. */
typedef struct { char kind; int replay; uint32_t user; unsigned long connection;
    char login[33],name[81],text[CHAT_TEXT+1]; } ChatEvent;
typedef struct {
    unsigned long connection;
    uint64_t generation;
    int accepts_telegram;
    char login[33],name[81],memo[81];
} ChatPresenceRow;
typedef struct { unsigned count; ChatPresenceRow rows[CONNECTION_MAX]; } ChatPresence;
typedef int (*ChatNotify)(void *,const ChatEvent *);
#endif

#ifndef LINER_CHAT_TYPES_H
#define LINER_CHAT_TYPES_H
#include <stdint.h>
#define CHAT_TEXT 512
#define CHAT_HISTORY 20
/* Event-loop-owned room; no sockets, BBS files or member database. Trusted
 * callers authenticate operations and provide an immutable identity snapshot. */
typedef struct { char kind; int replay; uint32_t user; unsigned long connection;
    char login[33],name[81],text[CHAT_TEXT+1]; } ChatEvent;
typedef int (*ChatNotify)(void *,const ChatEvent *);
#endif

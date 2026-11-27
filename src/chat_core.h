#ifndef LINER_CHAT_CORE_H
#define LINER_CHAT_CORE_H
#include "app_limits.h"
#include "chat_types.h"
#include "chat_preferences.h"
/* Event-loop-owned state. Zero-initialize before use; never reset live peers.
 * Callbacks are synchronous, borrow their event, and must not reenter this core.
 * Identity, privileges and preference snapshots come from trusted session/worker
 * adapters. A connection ID or user ID alone is not an authentication credential. */
typedef struct {
    void *context;
    int (*save)(void *,const ChatEvent *);
    int (*enabled)(void *);
} ChatPersistence;
typedef struct {
    uint32_t revision;int used,room,resident,said,deny;
    char muted[32][33];uint64_t last,called_at,generation;int admin,called;
    ChatEvent identity;ChatNotify notify;void *context;
} ChatPeer;
typedef struct {
    ChatPeer peers[CONNECTION_MAX];ChatEvent history[CHAT_HISTORY];
    unsigned history_count;uint64_t next_generation;ChatPersistence persistence;
} ChatCore;
/* Restore validated public log records before accepting connections; no callback
 * or write is made. Private/presence events cannot enter durable replay. */
int chat_core_restore_public(ChatCore *,const ChatEvent *);
int chat_core_text_valid(const char *s);
int chat_core_receive(ChatCore *core,unsigned long id,int enabled);
int chat_core_mute(ChatCore *core,unsigned long id,const char *login,int enabled);
void chat_core_settings(ChatCore *core,unsigned long id);
void chat_core_apply_preferences(ChatCore *core,uint32_t user,const ChatPreferences *settings);
int chat_core_join(ChatCore *core,unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context);
int chat_core_register(ChatCore *core,unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context,const ChatPreferences *settings);
int chat_core_join_config(ChatCore *core,unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context,const ChatPreferences *settings);
void chat_core_leave(ChatCore *core,unsigned long id);
void chat_core_disconnect(ChatCore *core,unsigned long id);
void chat_core_admin(ChatCore *core,unsigned long id,int enabled);
int chat_core_memo(ChatCore *core,unsigned long id,const char *text);
int chat_core_call(ChatCore *core,unsigned long id,const char *text,uint64_t now);
void chat_core_online(ChatCore *core,unsigned long id);
int chat_core_presence(ChatCore *,unsigned long id,uint32_t user,int room_only,ChatPresence *);
int chat_core_say(ChatCore *core,unsigned long id,const char *text,uint64_t now);
void chat_core_who(ChatCore *core,unsigned long id);
int chat_core_target(ChatCore *core,unsigned long from,unsigned long to);
int chat_core_private_scoped(ChatCore *,unsigned long from,unsigned long to,uint64_t generation,const char *,uint64_t now);
int chat_core_private(ChatCore *core,unsigned long from,unsigned long to,const char *text,uint64_t now);
#endif

#ifndef CHAT_H
#define CHAT_H
#include <stdint.h>
#include "chat_preferences.h"
#define CHAT_TEXT 512
#define CHAT_HISTORY 20
/* Event-loop-owned room; no sockets, BBS files or member database. Trusted
 * callers authenticate operations and provide an immutable identity snapshot. */
typedef struct { char kind; int replay; uint32_t user; unsigned long connection;
    char login[33],name[81],text[CHAT_TEXT+1]; } ChatEvent;
typedef int (*ChatNotify)(void *,const ChatEvent *);
int chat_log_open(const char *path);
void chat_log_close(void);
int chat_text_valid(const char *);
int chat_join(unsigned long,uint32_t,const char *,const char *,ChatNotify,void *);
int chat_join_config(unsigned long,uint32_t,const char *,const char *,ChatNotify,void *,const ChatPreferences *);
void chat_apply_preferences(uint32_t,const ChatPreferences *);
int chat_register(unsigned long,uint32_t,const char *,const char *,ChatNotify,void *,const ChatPreferences *);
void chat_disconnect(unsigned long);
void chat_online(unsigned long);
void chat_admin(unsigned long,int);
int chat_memo(unsigned long,const char *);
int chat_call(unsigned long,const char *,uint64_t);
void chat_leave(unsigned long);
int chat_say(unsigned long,const char *,uint64_t);
void chat_who(unsigned long);
int chat_target(unsigned long,unsigned long);
int chat_receive(unsigned long,int);
int chat_mute(unsigned long,const char *,int);
void chat_settings(unsigned long);
int chat_private(unsigned long,unsigned long,const char *,uint64_t);
#endif

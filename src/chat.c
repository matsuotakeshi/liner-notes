#define _POSIX_C_SOURCE 200809L
#include "chat.h"
#include "chat_core.h"
#include "durable.h"
#include <string.h>

static Durable public_log={-1,0,0};
static int save_public(void *,const ChatEvent *);
static int log_enabled(void *context) { (void)context;return public_log.fd>=0; }
static ChatCore room={.persistence={NULL,save_public,log_enabled}};
static int replay_public(void *ctx,const DurableRow *row) {
    (void)ctx;if(row->kind!='S' || !row->actor || row->actor>MEMBER_MAX)return 0;
    ChatEvent event={0};const char *a=strchr(row->text,'\t'),*b=a?strchr(a+1,'\t'):NULL;
    if(!a || !b || a-row->text>32 || b-a-1>80 || strlen(b+1)>CHAT_TEXT)return 0;
    memcpy(event.login,row->text,(size_t)(a-row->text));memcpy(event.name,a+1,(size_t)(b-a-1));strcpy(event.text,b+1);event.user=row->actor;event.kind='S';
    if(!chat_text_valid(event.login) || !chat_text_valid(event.name) || !chat_text_valid(event.text))return 0;
    return chat_core_restore_public(&room,&event);
}
int chat_log_open(const char *path) { return dj_open(&public_log,path,"LNCHAT01",replay_public,NULL); }
void chat_log_close(void) { dj_close(&public_log); }
static int save_public(void *context,const ChatEvent *e) {
    (void)context;
    if(public_log.fd<0)return 1;
    DurableRow row={0};row.kind='S';row.actor=e->user;snprintf(row.text,sizeof(row.text),"%s\t%s\t%s",e->login,e->name,e->text);return dj_append(&public_log,&row);
}

int chat_text_valid(const char *s) { return chat_core_text_valid(s); }
int chat_receive(unsigned long id,int enabled) { return chat_core_receive(&room,id,enabled); }
int chat_mute(unsigned long id,const char *login,int enabled) { return chat_core_mute(&room,id,login,enabled); }
void chat_settings(unsigned long id) { chat_core_settings(&room,id); }
void chat_apply_preferences(uint32_t user,const ChatPreferences *settings) { chat_core_apply_preferences(&room,user,settings); }
int chat_join(unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context) { return chat_core_join(&room,id,user,login,name,notify,context); }
int chat_register(unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context,const ChatPreferences *settings) { return chat_core_register(&room,id,user,login,name,notify,context,settings); }
int chat_join_config(unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context,const ChatPreferences *settings) { return chat_core_join_config(&room,id,user,login,name,notify,context,settings); }
void chat_leave(unsigned long id) { chat_core_leave(&room,id); }
void chat_disconnect(unsigned long id) { chat_core_disconnect(&room,id); }
void chat_admin(unsigned long id,int enabled) { chat_core_admin(&room,id,enabled); }
int chat_memo(unsigned long id,const char *text) { return chat_core_memo(&room,id,text); }
int chat_call(unsigned long id,const char *text,uint64_t now) { return chat_core_call(&room,id,text,now); }
void chat_online(unsigned long id) { chat_core_online(&room,id); }
int chat_say(unsigned long id,const char *text,uint64_t now) { return chat_core_say(&room,id,text,now); }
void chat_who(unsigned long id) { chat_core_who(&room,id); }
int chat_target(unsigned long from,unsigned long to) { return chat_core_target(&room,from,to); }
int chat_private(unsigned long from,unsigned long to,const char *text,uint64_t now) { return chat_core_private(&room,from,to,text,now); }

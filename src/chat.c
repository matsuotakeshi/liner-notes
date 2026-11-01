#define _POSIX_C_SOURCE 200809L
#include "app_limits.h"
#include "chat.h"
#include "durable.h"
#include <string.h>
typedef struct { uint32_t revision; int used,room,resident,said,deny; char muted[32][33]; uint64_t last,called_at; int admin,called; ChatEvent identity; ChatNotify notify; void *context; } Peer;
static Peer peers[CONNECTION_MAX];
static ChatEvent history[CHAT_HISTORY];
static unsigned count;

static Durable public_log={-1,0,0};
static int replay_public(void *ctx,const DurableRow *row) {
    (void)ctx;if(row->kind!='S' || !row->actor || row->actor>MEMBER_MAX)return 0;
    ChatEvent event={0};const char *a=strchr(row->text,'\t'),*b=a?strchr(a+1,'\t'):NULL;
    if(!a || !b || a-row->text>32 || b-a-1>80 || strlen(b+1)>CHAT_TEXT)return 0;
    memcpy(event.login,row->text,(size_t)(a-row->text));memcpy(event.name,a+1,(size_t)(b-a-1));strcpy(event.text,b+1);event.user=row->actor;event.kind='S';
    if(!chat_text_valid(event.login) || !chat_text_valid(event.name) || !chat_text_valid(event.text))return 0;
    if(count==CHAT_HISTORY) { memmove(history,history+1,sizeof(history[0])*(CHAT_HISTORY-1));count--; }history[count++]=event;return 1;
}
int chat_log_open(const char *path) { return dj_open(&public_log,path,"LNCHAT01",replay_public,NULL); }
void chat_log_close(void) { dj_close(&public_log); }
static int save_public(const ChatEvent *e) {
    if(public_log.fd<0)return 1;
    DurableRow row={0};row.kind='S';row.actor=e->user;snprintf(row.text,sizeof(row.text),"%s\t%s\t%s",e->login,e->name,e->text);return dj_append(&public_log,&row);
}

int chat_text_valid(const char *s) {
    size_t size=strlen(s),i=0;if(!size || size>CHAT_TEXT)return 0;
    while(i<size) {
        unsigned c=(unsigned char)s[i],n=1,cp=c;
        if(c>=128) {
            n=c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0;
            if(!n || i+n>size)return 0;
            cp=c&((1u<<(7-n))-1);
            for(unsigned j=1;j<n;j++) { unsigned b=(unsigned char)s[i+j];if((b&0xc0)!=0x80)return 0;cp=(cp<<6)|(b&63); }
            if((n==2 && cp<128)||(n==3 && cp<2048)||(n==4 && cp<65536)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff))return 0;
        }
        if(cp<32 || (cp>=127 && cp<=159))return 0;
        i+=n;
    }
    return 1;
}
static Peer *find(unsigned long id) { for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used && peers[i].identity.connection==id)return &peers[i];return NULL; }
static int muted(Peer *p,const char *login) {
    for(int i=0;i<32;i++)if(!strcmp(p->muted[i],login))return 1;
    return 0;
}
int chat_receive(unsigned long id,int enabled) {
    Peer *p=find(id);if(!p)return 0;p->deny=!enabled;return 1;
}
int chat_mute(unsigned long id,const char *login,int enabled) {
    Peer *p=find(id);if(!p || !*login || strlen(login)>32 || !strcmp(login,p->identity.login))return 0;
    for(int i=0;i<32;i++)if(!strcmp(p->muted[i],login)) {
        if(!enabled)p->muted[i][0]=0;
        return 1;
    }
    if(!enabled)return 1;
    int known=0;for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used && !strcmp(peers[i].identity.login,login))known=1;
    if(!known)return 0;
    for(int i=0;i<32;i++)if(!p->muted[i][0]) { strcpy(p->muted[i],login);return 1; }
    return 0;
}
void chat_settings(unsigned long id) {
    Peer *p=find(id);if(!p)return;
    ChatEvent e={0};e.kind='A';e.replay=1;strcpy(e.text,p->deny?"OFF":"ON");p->notify(p->context,&e);
    for(int i=0;i<32;i++)if(p->muted[i][0]) { e.kind='M';strcpy(e.login,p->muted[i]);p->notify(p->context,&e); }
    memset(&e,0,sizeof(e));e.kind='E';p->notify(p->context,&e);
}
static void broadcast(ChatEvent *e) {
    if(count==CHAT_HISTORY) { memmove(history,history+1,sizeof(history[0])*(CHAT_HISTORY-1));count--; }
    history[count++]=*e;
    for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used && (peers[i].room || e->kind=='L') && !(e->kind=='S' && muted(&peers[i],e->login)))peers[i].notify(peers[i].context,e);
}
void chat_apply_preferences(uint32_t user,const ChatPreferences *settings) {
    for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used && peers[i].identity.user==user && peers[i].revision<=settings->revision) {
        peers[i].revision=settings->revision;peers[i].deny=settings->deny;
        memcpy(peers[i].muted,settings->muted,sizeof(peers[i].muted));
    }
}
int chat_join(unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context) {
    ChatPreferences defaults={0};return chat_join_config(id,user,login,name,notify,context,&defaults);
}
int chat_register(unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context,const ChatPreferences *settings) {
    Peer *p=find(id);if(p) { if(p->identity.user!=user)return 0;chat_apply_preferences(user,settings);return 1; }
    if(!user || !notify || strlen(login)>32 || strlen(name)>80 || !chat_text_valid(login) || !chat_text_valid(name))return 0;
    for(int i=0;i<CONNECTION_MAX;i++)if(!peers[i].used) { p=&peers[i];break; }
    if(!p)return 0;
    memset(p,0,sizeof(*p));p->used=p->resident=1;p->notify=notify;p->context=context;
    p->identity.user=user;p->identity.connection=id;strcpy(p->identity.login,login);strcpy(p->identity.name,name);
    p->revision=settings->revision;p->deny=settings->deny;memcpy(p->muted,settings->muted,sizeof(p->muted));
    for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used && peers[i].identity.user==user && peers[i].revision>p->revision) {
        p->revision=peers[i].revision;p->deny=peers[i].deny;memcpy(p->muted,peers[i].muted,sizeof(p->muted));
    }
    return 1;
}
int chat_join_config(unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context,const ChatPreferences *settings) {
    Peer *p=find(id);int resident=p!=NULL;if(p && p->room)return 0;
    if(!chat_register(id,user,login,name,notify,context,settings))return 0;
    p=find(id);p->resident=resident;p->room=1;strcpy(p->identity.name,name);
    for(unsigned i=0;i<count;i++) { if(history[i].kind=='S' && muted(p,history[i].login))continue;ChatEvent event=history[i];event.replay=1;notify(context,&event); }
    ChatEvent event=p->identity;event.kind='J';broadcast(&event);return 1;
}
void chat_leave(unsigned long id) {
    Peer *p=find(id);if(!p || !p->room)return;
    ChatEvent event=p->identity;event.kind='L';p->room=0;if(!p->resident)memset(p,0,sizeof(*p));broadcast(&event);
    int any=0;for(int i=0;i<CONNECTION_MAX;i++)any|=peers[i].used && peers[i].room;
    if(!any) {
        unsigned kept=0;if(public_log.fd>=0)for(unsigned i=0;i<count;i++)if(history[i].kind=='S')history[kept++]=history[i];
        memset(history+kept,0,(CHAT_HISTORY-kept)*sizeof(history[0]));count=kept;
    }
}
void chat_disconnect(unsigned long id) {
    Peer *p=find(id);if(!p)return;int room=p->room;
    ChatEvent e=p->identity;e.kind='X';chat_leave(id);memset(p,0,sizeof(*p));
    if(!room)for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used)peers[i].notify(peers[i].context,&e);
}
void chat_admin(unsigned long id,int enabled) { Peer *p=find(id);if(p)p->admin=enabled; }
int chat_memo(unsigned long id,const char *text) {
    Peer *p=find(id);if(!p || strlen(text)>80 || (*text && !chat_text_valid(text)))return 0;
    strcpy(p->identity.text,text);return 1;
}
int chat_call(unsigned long id,const char *text,uint64_t now) {
    Peer *p=find(id);if(!p || !chat_text_valid(text))return -2;
    if(p->called && now-p->called_at<30000)return -1;
    p->called=1;p->called_at=now;ChatEvent e=p->identity;e.kind='C';strcpy(e.text,text);int count=0;
    for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used && peers[i].admin && peers[i].identity.connection!=id && peers[i].notify(peers[i].context,&e))count++;
    return count;
}
void chat_online(unsigned long id) {
    Peer *p=find(id);if(!p)return;
    for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used) { ChatEvent e=peers[i].identity;e.kind='W';e.replay=1;p->notify(p->context,&e); }
    ChatEvent e={0};e.kind='E';p->notify(p->context,&e);
}
int chat_say(unsigned long id,const char *text,uint64_t now) {
    Peer *p=find(id);if(!p || !p->room || !chat_text_valid(text))return 0;
    if(p->said && now-p->last<500)return -1;
    p->last=now;p->said=1;ChatEvent event=p->identity;event.kind='S';strcpy(event.text,text);if(!save_public(&event))return -4;broadcast(&event);return 1;
}
void chat_who(unsigned long id) {
    Peer *p=find(id);if(!p)return;
    for(int i=0;i<CONNECTION_MAX;i++)if(peers[i].used && peers[i].room) { ChatEvent event=peers[i].identity;event.kind='W';event.replay=1;p->notify(p->context,&event); }
    ChatEvent end={0};end.kind='E';p->notify(p->context,&end);
}
int chat_target(unsigned long from,unsigned long to) { return from!=to && find(from) && find(to); }
int chat_private(unsigned long from,unsigned long to,const char *text,uint64_t now) {
    Peer *sender=find(from),*receiver=find(to);
    if(!sender || !receiver || from==to)return -2;
    if(receiver->deny || muted(receiver,sender->identity.login))return -3;
    if(!chat_text_valid(text))return 0;
    if(sender->said && now-sender->last<500)return -1;
    ChatEvent event=sender->identity;event.kind='I';strcpy(event.text,text);
    /* Private events never pass through broadcast() or its history ring. */
    if(!receiver->notify(receiver->context,&event))return -2;
    sender->last=now;sender->said=1;
    event=receiver->identity;event.kind='O';strcpy(event.text,text);
    sender->notify(sender->context,&event);return 1;
}

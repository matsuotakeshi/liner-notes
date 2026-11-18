#include "chat_core.h"
#include <string.h>

int chat_core_text_valid(const char *s) {
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
static ChatPeer *find(ChatCore *core,unsigned long id) { for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used && core->peers[i].identity.connection==id)return &core->peers[i];return NULL; }
static int muted(ChatPeer *p,const char *login) {
    for(int i=0;i<32;i++)if(!strcmp(p->muted[i],login))return 1;
    return 0;
}
int chat_core_receive(ChatCore *core,unsigned long id,int enabled) {
    ChatPeer *p=find(core,id);if(!p)return 0;p->deny=!enabled;return 1;
}
int chat_core_mute(ChatCore *core,unsigned long id,const char *login,int enabled) {
    ChatPeer *p=find(core,id);if(!p || !*login || strlen(login)>32 || !strcmp(login,p->identity.login))return 0;
    for(int i=0;i<32;i++)if(!strcmp(p->muted[i],login)) {
        if(!enabled)p->muted[i][0]=0;
        return 1;
    }
    if(!enabled)return 1;
    int known=0;for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used && !strcmp(core->peers[i].identity.login,login))known=1;
    if(!known)return 0;
    for(int i=0;i<32;i++)if(!p->muted[i][0]) { strcpy(p->muted[i],login);return 1; }
    return 0;
}
void chat_core_settings(ChatCore *core,unsigned long id) {
    ChatPeer *p=find(core,id);if(!p)return;
    ChatEvent e={0};e.kind='A';e.replay=1;strcpy(e.text,p->deny?"OFF":"ON");p->notify(p->context,&e);
    for(int i=0;i<32;i++)if(p->muted[i][0]) { e.kind='M';strcpy(e.login,p->muted[i]);p->notify(p->context,&e); }
    memset(&e,0,sizeof(e));e.kind='E';p->notify(p->context,&e);
}
static void broadcast(ChatCore *core,ChatEvent *e) {
    if(core->history_count==CHAT_HISTORY) { memmove(core->history,core->history+1,sizeof(core->history[0])*(CHAT_HISTORY-1));core->history_count--; }
    core->history[core->history_count++]=*e;
    for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used && (core->peers[i].room || e->kind=='L') && !(e->kind=='S' && muted(&core->peers[i],e->login)))core->peers[i].notify(core->peers[i].context,e);
}
void chat_core_apply_preferences(ChatCore *core,uint32_t user,const ChatPreferences *settings) {
    for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used && core->peers[i].identity.user==user && core->peers[i].revision<=settings->revision) {
        core->peers[i].revision=settings->revision;core->peers[i].deny=settings->deny;
        memcpy(core->peers[i].muted,settings->muted,sizeof(core->peers[i].muted));
    }
}
int chat_core_join(ChatCore *core,unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context) {
    ChatPreferences defaults={0};return chat_core_join_config(core,id,user,login,name,notify,context,&defaults);
}
int chat_core_register(ChatCore *core,unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context,const ChatPreferences *settings) {
    ChatPeer *p=find(core,id);if(p) { if(p->identity.user!=user)return 0;chat_core_apply_preferences(core,user,settings);return 1; }
    if(!user || !notify || strlen(login)>32 || strlen(name)>80 || !chat_core_text_valid(login) || !chat_core_text_valid(name))return 0;
    for(int i=0;i<CONNECTION_MAX;i++)if(!core->peers[i].used) { p=&core->peers[i];break; }
    if(!p)return 0;
    memset(p,0,sizeof(*p));p->used=p->resident=1;p->notify=notify;p->context=context;
    p->identity.user=user;p->identity.connection=id;strcpy(p->identity.login,login);strcpy(p->identity.name,name);
    p->revision=settings->revision;p->deny=settings->deny;memcpy(p->muted,settings->muted,sizeof(p->muted));
    for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used && core->peers[i].identity.user==user && core->peers[i].revision>p->revision) {
        p->revision=core->peers[i].revision;p->deny=core->peers[i].deny;memcpy(p->muted,core->peers[i].muted,sizeof(p->muted));
    }
    return 1;
}
int chat_core_join_config(ChatCore *core,unsigned long id,uint32_t user,const char *login,const char *name,ChatNotify notify,void *context,const ChatPreferences *settings) {
    ChatPeer *p=find(core,id);int resident=p!=NULL;if(p && p->room)return 0;
    if(!chat_core_register(core,id,user,login,name,notify,context,settings))return 0;
    p=find(core,id);p->resident=resident;p->room=1;strcpy(p->identity.name,name);
    for(unsigned i=0;i<core->history_count;i++) { if(core->history[i].kind=='S' && muted(p,core->history[i].login))continue;ChatEvent event=core->history[i];event.replay=1;notify(context,&event); }
    ChatEvent event=p->identity;event.kind='J';broadcast(core,&event);return 1;
}
void chat_core_leave(ChatCore *core,unsigned long id) {
    ChatPeer *p=find(core,id);if(!p || !p->room)return;
    ChatEvent event=p->identity;event.kind='L';p->room=0;if(!p->resident)memset(p,0,sizeof(*p));broadcast(core,&event);
    int any=0;for(int i=0;i<CONNECTION_MAX;i++)any|=core->peers[i].used && core->peers[i].room;
    if(!any) {
        unsigned kept=0;if(core->persistence.enabled && core->persistence.enabled(core->persistence.context))for(unsigned i=0;i<core->history_count;i++)if(core->history[i].kind=='S')core->history[kept++]=core->history[i];
        memset(core->history+kept,0,(CHAT_HISTORY-kept)*sizeof(core->history[0]));core->history_count=kept;
    }
}
void chat_core_disconnect(ChatCore *core,unsigned long id) {
    ChatPeer *p=find(core,id);if(!p)return;int room=p->room;
    ChatEvent e=p->identity;e.kind='X';chat_core_leave(core,id);memset(p,0,sizeof(*p));
    if(!room)for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used)core->peers[i].notify(core->peers[i].context,&e);
}
void chat_core_admin(ChatCore *core,unsigned long id,int enabled) { ChatPeer *p=find(core,id);if(p)p->admin=enabled; }
int chat_core_memo(ChatCore *core,unsigned long id,const char *text) {
    ChatPeer *p=find(core,id);if(!p || strlen(text)>80 || (*text && !chat_core_text_valid(text)))return 0;
    strcpy(p->identity.text,text);return 1;
}
int chat_core_call(ChatCore *core,unsigned long id,const char *text,uint64_t now) {
    ChatPeer *p=find(core,id);if(!p || !chat_core_text_valid(text))return -2;
    if(p->called && now-p->called_at<30000)return -1;
    p->called=1;p->called_at=now;ChatEvent e=p->identity;e.kind='C';strcpy(e.text,text);int delivered=0;
    for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used && core->peers[i].admin && core->peers[i].identity.connection!=id && core->peers[i].notify(core->peers[i].context,&e))delivered++;
    return delivered;
}
void chat_core_online(ChatCore *core,unsigned long id) {
    ChatPeer *p=find(core,id);if(!p)return;
    for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used) { ChatEvent e=core->peers[i].identity;e.kind='W';e.replay=1;p->notify(p->context,&e); }
    ChatEvent e={0};e.kind='E';p->notify(p->context,&e);
}
int chat_core_say(ChatCore *core,unsigned long id,const char *text,uint64_t now) {
    ChatPeer *p=find(core,id);if(!p || !p->room || !chat_core_text_valid(text))return 0;
    if(p->said && now-p->last<500)return -1;
    p->last=now;p->said=1;ChatEvent event=p->identity;event.kind='S';strcpy(event.text,text);if(core->persistence.save && !core->persistence.save(core->persistence.context,&event))return -4;broadcast(core,&event);return 1;
}
void chat_core_who(ChatCore *core,unsigned long id) {
    ChatPeer *p=find(core,id);if(!p)return;
    for(int i=0;i<CONNECTION_MAX;i++)if(core->peers[i].used && core->peers[i].room) { ChatEvent event=core->peers[i].identity;event.kind='W';event.replay=1;p->notify(p->context,&event); }
    ChatEvent end={0};end.kind='E';p->notify(p->context,&end);
}
int chat_core_target(ChatCore *core,unsigned long from,unsigned long to) { return from!=to && find(core,from) && find(core,to); }
int chat_core_private(ChatCore *core,unsigned long from,unsigned long to,const char *text,uint64_t now) {
    ChatPeer *sender=find(core,from),*receiver=find(core,to);
    if(!sender || !receiver || from==to)return -2;
    if(receiver->deny || muted(receiver,sender->identity.login))return -3;
    if(!chat_core_text_valid(text))return 0;
    if(sender->said && now-sender->last<500)return -1;
    ChatEvent event=sender->identity;event.kind='I';strcpy(event.text,text);
    /* Private events never pass through broadcast() or its history ring. */
    if(!receiver->notify(receiver->context,&event))return -2;
    sender->last=now;sender->said=1;
    event=receiver->identity;event.kind='O';strcpy(event.text,text);
    sender->notify(sender->context,&event);return 1;
}

int chat_core_restore_public(ChatCore *core,const ChatEvent *event) {
    if(!event || event->kind!='S' || !event->user || event->user>MEMBER_MAX
        || !memchr(event->login,0,sizeof(event->login)) || !memchr(event->name,0,sizeof(event->name))
        || !memchr(event->text,0,sizeof(event->text)) || !chat_core_text_valid(event->login)
        || !chat_core_text_valid(event->name) || !chat_core_text_valid(event->text))return 0;
    if(core->history_count==CHAT_HISTORY) {
        memmove(core->history,core->history+1,sizeof(core->history[0])*(CHAT_HISTORY-1));core->history_count--;
    }
    ChatEvent copy=*event;copy.connection=0;copy.replay=0;
    core->history[core->history_count++]=copy;return 1;
}

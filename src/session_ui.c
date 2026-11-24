#include "session.h"
#include "session_io.h"
#include <string.h>
#include <stdlib.h>
#include "ui_profile.h"
#include <openssl/crypto.h>

SessionUiConfig session_ui_default(void) {
    SessionUiConfig config={.count=2,.allowed=1,.policy=SESSION_UI_FIXED};
    config.adapters[0]=session_ui_rtbbs();
    config.adapters[1]=session_ui_mmm();
    return config;
}

int session_ui_configure(SessionUiConfig *config,const char *policy,const char *name,const char *allow) {
    if(!config || !policy || !name || !allow || !*allow || strlen(allow)>255)return 0;
    SessionUiConfig next=*config;
    if(!strcmp(policy,"FIXED"))next.policy=SESSION_UI_FIXED;
    else if(!strcmp(policy,"MEMBERS"))next.policy=SESSION_UI_MEMBERS;
    else if(!strcmp(policy,"ALL"))next.policy=SESSION_UI_ALL;
    else return 0;
    unsigned i=0;for(;i<next.count;i++)if(!strcmp(name,next.adapters[i]->name))break;
    if(i==next.count)return 0;
    next.default_index=i;next.allowed=0;
    const char *p=allow;
    while(*p) {
        const char *end=strchr(p,',');size_t length=end?(size_t)(end-p):strlen(p);
        if(!length || length>31)return 0;
        char part[32];memcpy(part,p,length);part[length]=0;
        for(i=0;i<next.count;i++)if(!strcmp(part,next.adapters[i]->name))break;
        if(i==next.count || (next.allowed&(1u<<i)))return 0;
        next.allowed|=1u<<i;
        if(!end)break;
        p=end+1;if(!*p)return 0;
    }
    if(!session_ui_config_valid(&next))return 0;
    *config=next;return 1;
}

int session_ui_config_valid(const SessionUiConfig *config) {
    if(!config || !config->count || config->count>SESSION_UI_MAX ||
       config->default_index>=config->count || config->policy<SESSION_UI_FIXED ||
       config->policy>SESSION_UI_ALL || (config->allowed>>config->count) ||
       !(config->allowed&(1u<<config->default_index)))return 0;
    for(unsigned i=0;i<config->count;i++) {
        const SessionUiAdapter *a=config->adapters[i];
        if(!a || !a->state_size || !a->snapshot || !a->cleanup || !a->connector_line || !a->binary_byte ||
           !a->prepare_request || !a->preferences || !a->signature || !a->scan_pause || !a->scan_stop ||
           !a->board_refresh || !a->access_refresh || !a->chat_kick || !a->chat_event || !a->announce || !a->notify ||
           !a->mail_arrived || !a->mail_notify || !a->transfer_receipt || !a->transfer_finish || !a->notice || !a->name || !*a->name || strlen(a->name)>31 || !a->enter || !a->leave ||
           !a->can_leave || !a->line || !a->keep_position || !a->result || !a->emit ||
           !a->prompt || !a->menu || !a->help || !a->uppercase || !a->tick || !a->door_finish)return 0;
        for(const char *p=a->name;*p;p++)if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='-'))return 0;
        for(unsigned j=0;j<i;j++)if(!strcmp(a->name,config->adapters[j]->name))return 0;
    }
    return 1;
}

const char *session_ui_name(const Session *s) { return s->ui_binding.active?s->ui_binding.active->name:NULL; }

SessionUiSelection session_ui_preflight(const Session *s,const char *name) {
    if(!s->ui_binding.active || !name)return SESSION_UI_INVALID;
    if(s->connection.legacy_connector || (*s->connection.connector_token &&
        strcmp(name,s->ui_binding.active->name)))return SESSION_UI_DENIED;
    const SessionUiConfig *c=&s->ui_binding.config;
    unsigned i;
    for(i=0;i<c->count;i++)if(!strcmp(name,c->adapters[i]->name))break;
    if(i==c->count)return SESSION_UI_UNKNOWN;
    if(!(c->allowed&(1u<<i)) || (c->policy==SESSION_UI_FIXED && c->adapters[i]!=s->ui_binding.active) ||
       (c->policy==SESSION_UI_MEMBERS && !s->core.user))return SESSION_UI_DENIED;
    /* Common security/transport barriers apply even to a permissive UI adapter. */
    if(!session_ready(s) || session_secret(s) || s->connection.door || s->connection.zmodem_start ||
       s->connection.binary || s->connection.connector_literal || s->connection.connector_block.data ||
       s->connection.binary_reader.used || !s->ui_binding.active->can_leave(s))return SESSION_UI_BUSY;
    if(c->adapters[i]==s->ui_binding.active)return SESSION_UI_UNCHANGED;
    return SESSION_UI_SELECTED;
}
SessionUiSelection session_ui_select(Session *s,const char *name) {
    SessionUiSelection result=session_ui_preflight(s,name);if(result!=SESSION_UI_SELECTED)return result;
    const SessionUiConfig *c=&s->ui_binding.config;unsigned i=0;
    while(strcmp(name,c->adapters[i]->name))i++;
    SessionUiSnapshot view=session_ui_snapshot(s);
    if(!session_ui_allocate(s,i))return SESSION_UI_INVALID;
    s->ui_binding.active->leave(s);s->ui_binding.active=c->adapters[i];s->ui_binding.state=s->ui_binding.states[i];
    s->core.login_required=s->ui_binding.active->login_required;
    s->ui_binding.active->enter(s,0,view.menu,view.language);
    if(s->core.user)s->ui_binding.active->mail_notify(s,s->core.user,s->core.mail_notify_deny,s->core.mail_notify_revision);
    return SESSION_UI_SELECTED;
}

int session_ui_legacy_connector(Session *s) {
    const SessionUiAdapter *active=s->ui_binding.active;
    if(!active->login_required || s->core.user || session_secret(s) || !active->can_leave(s))
        return 0;
    const SessionUiConfig *c=&s->ui_binding.config;
    unsigned i=0;
    while(i<c->count && c->adapters[i]!=session_ui_rtbbs())i++;
    if(i==c->count || !session_ui_allocate(s,i))return 0;
    Language language=session_ui_snapshot(s).language;
    active->leave(s);
    s->connection.legacy_connector=1;
    s->ui_binding.active=c->adapters[i];
    s->ui_binding.state=s->ui_binding.states[i];
    /* The client protocol keeps the same required authentication as the MMM listener. */
    s->ui_binding.active->enter(s,0,0,language);
    session_impl_queue_text(s,"\r\nRAW\r\n");
    return 1;
}

static void restore_choice(Session *s) {
    if(!s->ui_binding.restore_pending || s->ui_binding.explicit_choice)return;
    const SessionUiConfig *c=&s->ui_binding.config;
    const char *fallback=c->adapters[c->default_index]->name;
    const char *name=c->policy!=SESSION_UI_FIXED && *s->ui_binding.preferred?s->ui_binding.preferred:fallback;
    SessionUiSelection result=session_ui_select(s,name);
    if(result!=SESSION_UI_SELECTED && result!=SESSION_UI_UNCHANGED && result!=SESSION_UI_BUSY)
        result=session_ui_select(s,fallback);
    if(result!=SESSION_UI_BUSY)s->ui_binding.restore_pending=0;
}
void session_result(Session *s,const BoardResult *completion,uint64_t now) {
    if(!session_core_matches(&s->core,completion))return;
    if(completion->revoked) { session_revoke(s);return; }
    int cancelled=s->ui_binding.active->keep_position(s,completion);
    SessionResultView view=session_core_receive(&s->core,completion,now,cancelled);
    if(view.status!=SESSION_RESULT_IGNORED && completion->auth)session_ui_discard_inactive(s);
    if(view.status==SESSION_RESULT_OK && completion->mail_notify_user)
        session_mail_notify(s,completion->mail_notify_user,completion->mail_notify_deny,completion->mail_notify_revision);
    if(view.status!=SESSION_RESULT_IGNORED)s->ui_binding.active->result(s,completion,view,now,cancelled);
    if(view.status==SESSION_RESULT_OK && (completion->auth>0 || completion->feedback.ui_profile_saved)) {
        memcpy(s->ui_binding.preferred,completion->ui_profile,sizeof(s->ui_binding.preferred));
        s->ui_binding.preferred[31]=0;
        if(completion->auth>0 && !s->ui_binding.explicit_choice) { s->ui_binding.restore_pending=1;restore_choice(s); }
    }
}

SessionUiSelection session_ui_choose(Session *s,const char *name) {
    SessionUiSelection result=session_ui_select(s,name);
    if(result==SESSION_UI_SELECTED || result==SESSION_UI_UNCHANGED) { s->ui_binding.explicit_choice=1;s->ui_binding.restore_pending=0; }
    return result;
}
int session_ui_save_choice(Session *s,const char *name) {
    if(!s->core.user || !name || (*name && !ui_profile_valid(name)))return 0;
    OperationRequest *q=calloc(1,sizeof(*q));if(!q)return 0;
    q->op='V';q->number=4;strcpy(q->title,name);
    SessionOperationStatus status=session_submit(s,q);free(q);
    return status==SESSION_OPERATION_ACCEPTED;
}

void session_impl_emit(Session *s,const char *text) {
    if(s->connection.binary) { session_impl_binary_frame(s,'T',text,strlen(text));return; }
    s->ui_binding.active->emit(s,text);
}
void session_impl_prompt(Session *s) { s->ui_binding.active->prompt(s); }
void session_impl_show_menu(Session *s,int explicit_request) { s->ui_binding.active->menu(s,explicit_request); }
void session_impl_show_help(Session *s,const char *topic) { s->ui_binding.active->help(s,topic); }
int session_uppercase(const Session *s,const char *line,size_t size) {
    if(session_secret(s) || s->connection.binary || s->connection.zmodem)return 0;
    return s->ui_binding.active->uppercase(s,line,size);
}
void session_door_finish(Session *s) {
    s->connection.door=0;
    if(s->core.active && !s->core.closing)s->ui_binding.active->door_finish(s);
}
void session_tick(Session *s,uint64_t now) {
    if(!s->core.connected_at)s->core.connected_at=now;
    s->core.now=now;
    /* Framing deadlines cannot be bypassed by a UI or an automatic read. */
    if(connector_block_expired(&s->connection.connector_block,now)) {
        connector_block_clear(&s->connection.connector_block);session_abort(s);return;
    }
    if(s->connection.binary && connector_reader_expired(&s->connection.binary_reader,now)) { session_abort(s);return; }
    if(s->core.active && !s->core.closing) { restore_choice(s);s->ui_binding.active->tick(s,now); }
}

SessionUiSnapshot session_ui_snapshot(const Session *s) {
    SessionUiSnapshot empty={0};
    return s->ui_binding.active && s->ui_binding.state?s->ui_binding.active->snapshot(s):empty;
}
int session_ui_allocate(Session *s,unsigned index) {
    if(index>=s->ui_binding.config.count)return 0;
    if(!s->ui_binding.states[index])s->ui_binding.states[index]=calloc(1,s->ui_binding.config.adapters[index]->state_size);
    if(!s->ui_binding.state)s->ui_binding.state=s->ui_binding.states[index];
    return s->ui_binding.states[index]!=NULL;
}
static void dispose_state(Session *s,unsigned index) {
    if(!s->ui_binding.states[index])return;
    const SessionUiAdapter *active=s->ui_binding.active;void *state=s->ui_binding.state;
    s->ui_binding.active=s->ui_binding.config.adapters[index];s->ui_binding.state=s->ui_binding.states[index];
    int current=s->ui_binding.state==state;
    s->ui_binding.active->cleanup(s,current?0:2,0);
    OPENSSL_cleanse(s->ui_binding.state,s->ui_binding.active->state_size);free(s->ui_binding.state);
    s->ui_binding.states[index]=NULL;s->ui_binding.active=active;s->ui_binding.state=current?NULL:state;
}
void session_ui_discard_inactive(Session *s) {
    for(unsigned i=0;i<s->ui_binding.config.count;i++)if(s->ui_binding.states[i]!=s->ui_binding.state)dispose_state(s,i);
}
void session_ui_dispose(Session *s) {
    for(unsigned i=0;i<s->ui_binding.config.count;i++)dispose_state(s,i);
    s->ui_binding.state=NULL;s->ui_binding.active=NULL;
}
int session_auto_read(const Session *s) { return session_ui_snapshot(s).automatic; }
int session_binary_byte(Session *s,unsigned char byte,uint64_t now) { return s->ui_binding.active->binary_byte(s,byte,now); }
int session_disconnect_preferences(const Session *s,BoardRequest *q) {
    if(!s->ui_binding.state) { if(q)memset(q,0,sizeof(*q));return 0; }
    return s->ui_binding.active->preferences(s,q);
}
uint64_t session_ui_signature(const Session *s) { return s->ui_binding.state?s->ui_binding.active->signature(s):0; }
void session_scan_pause(Session *s) { s->ui_binding.active->scan_pause(s); }
void session_scan_stop(Session *s) { s->ui_binding.active->scan_stop(s); }
void session_board_refresh(Session *s) { s->ui_binding.active->board_refresh(s); }
void session_access_refresh(Session *s) { s->ui_binding.active->access_refresh(s); }
void session_chat_kick(Session *s) { s->ui_binding.active->chat_kick(s); }
void session_announce(Session *s,const char *text) { s->ui_binding.active->announce(s,text); }
void session_notify(Session *s,MessageId id,uint32_t number,const char *body) { s->ui_binding.active->notify(s,id,number,body); }
void session_mail_arrived(Session *s,uint32_t user) { s->ui_binding.active->mail_arrived(s,user); }
void session_mail_notify(Session *s,uint32_t user,int deny,uint32_t revision) {
    if(!user || user!=s->core.user || revision<s->core.mail_notify_revision)return;
    s->core.mail_notify_deny=deny;s->core.mail_notify_revision=revision;
    s->ui_binding.active->mail_notify(s,user,deny,revision);
}
void session_transfer_receipt(Session *s,const char *text) { s->ui_binding.active->transfer_receipt(s,text); }
void session_zmodem_finish(Session *s,int ok,unsigned id,uint64_t size,int recovery) {
    s->connection.zmodem=0;s->connection.zmodem_start=0;
    s->ui_binding.active->transfer_finish(s,ok,id,size,recovery);
}
int session_notice(const Session *s,SessionNotice kind,unsigned number,uint64_t bytes,uint64_t total,char *out,size_t size) {
    if(!s->ui_binding.state || !out || !size)return 0;
    return s->ui_binding.active->notice(s,kind,number,bytes,total,out,size);
}

int session_chat_event(void *context,const ChatEvent *event) {
    Session *s=context;
    if(!s->core.active || s->core.closing || !s->core.user || s->connection.zmodem || s->connection.door || !s->ui_binding.state)return 0;
    return s->ui_binding.active->chat_event(s,event);
}

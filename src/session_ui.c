#include "session_internal.h"
#include <string.h>

SessionUiConfig session_ui_default(void) {
    SessionUiConfig config={.count=1,.allowed=1,.policy=SESSION_UI_FIXED};
    config.adapters[0]=session_ui_rtbbs();return config;
}

int session_ui_config_valid(const SessionUiConfig *config) {
    if(!config || !config->count || config->count>SESSION_UI_MAX ||
       config->default_index>=config->count || config->policy<SESSION_UI_FIXED ||
       config->policy>SESSION_UI_ALL || (config->allowed>>config->count) ||
       !(config->allowed&(1u<<config->default_index)))return 0;
    for(unsigned i=0;i<config->count;i++) {
        const SessionUiAdapter *a=config->adapters[i];
        if(!a || !a->name || !*a->name || strlen(a->name)>31 || !a->enter || !a->leave ||
           !a->can_leave || !a->line || !a->keep_position || !a->result || !a->emit ||
           !a->prompt || !a->menu || !a->help || !a->uppercase || !a->tick || !a->door_finish)return 0;
        for(const char *p=a->name;*p;p++)if(!((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='-'))return 0;
        for(unsigned j=0;j<i;j++)if(!strcmp(a->name,config->adapters[j]->name))return 0;
    }
    return 1;
}

const char *session_ui_name(const Session *s) { return s->ui_binding.active?s->ui_binding.active->name:NULL; }

SessionUiSelection session_ui_select(Session *s,const char *name) {
    if(!s->ui_binding.active || !name)return SESSION_UI_INVALID;
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
    int menu=s->presentation.ui.enabled;Language language=s->presentation.ui.language;
    s->ui_binding.active->leave(s);s->ui_binding.active=c->adapters[i];
    s->ui_binding.active->enter(s,0,menu,language);
    return SESSION_UI_SELECTED;
}

void session_result(Session *s,const BoardResult *completion,uint64_t now) {
    if(!session_core_matches(&s->core,completion))return;
    if(completion->revoked) { session_revoke(s);return; }
    int cancelled=s->ui_binding.active->keep_position(s,completion);
    SessionResultView view=session_core_receive(&s->core,completion,now,cancelled);
    if(view.status!=SESSION_RESULT_IGNORED)s->ui_binding.active->result(s,completion,view,now,cancelled);
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
    if(s->core.active && !s->core.closing)s->ui_binding.active->tick(s,now);
}

#include "message_flow_core.h"
#include <string.h>
void gather_flow_read_submitted(GatherFlow *g,int read_only,int accepted) {
    g->pending=accepted?(read_only?2:1):0;
}
void gather_flow_post_submitted(GatherFlow *g,int accepted) { g->pending=accepted?3:0; }
static GatherEffect finish_reading(GatherFlow *g) {
    if(g->mode==2) { g->mode=0;return GATHER_CLOSED; }
    g->lines=0;return GATHER_EDIT;
}
GatherEffect gather_flow_input(GatherFlow *g,GatherInput input,const char *text,int valid,char *body,size_t capacity) {
    if(input==GATHER_INPUT_CANCEL) { g->mode=0;g->cursor=0;g->lines=0;return GATHER_ABORTED; }
    if(g->cursor) {
        if(input==GATHER_INPUT_FINISH || input==GATHER_INPUT_QUIT) { g->cursor=0;return finish_reading(g); }
        return input==GATHER_INPUT_NEXT?GATHER_NEXT_PAGE:GATHER_NO_EFFECT;
    }
    if(input!=GATHER_INPUT_FINISH) {
        if(!valid)return GATHER_INVALID;
        size_t used=strlen(body),size=strlen(text);
        if(used>=capacity || capacity-used<3 || size>capacity-used-3)return GATHER_INVALID;
        memcpy(body+used,text,size);memcpy(body+used+size,"\r\n",3);g->lines++;
        if(g->lines<10)return GATHER_NO_EFFECT;
    }
    if(!g->lines) { g->mode=0;return GATHER_EMPTY; }
    return GATHER_POST;
}
GatherEffect gather_flow_complete(GatherFlow *g,GatherResponse response,uint32_t next) {
    if(!g->pending)return GATHER_NO_EFFECT;
    int mode=g->pending;g->pending=0;
    if(response==GATHER_RESPONSE_PAGE) {
        g->cursor=next;g->mode=mode;
        return next?GATHER_NO_EFFECT:finish_reading(g);
    }
    if(response==GATHER_RESPONSE_SAVED) { g->mode=0;g->lines=0;return GATHER_SAVED; }
    return GATHER_FAILED;
}
int sysop_mail_ready(const SysopMailFlow *m,uint64_t now) { return now>=m->after; }
int sysop_mail_title_valid(const char *title) {
    if(!title || !*title || strlen(title)>80)return 0;
    for(const unsigned char *p=(const unsigned char *)title;*p;p++)if(*p<32 || *p==127)return 0;
    return 1;
}
void sysop_mail_submitted(SysopMailFlow *m,int accepted) { m->pending=!!accepted; }
SysopMailEffect sysop_mail_complete(SysopMailFlow *m,int sent,uint64_t now) {
    if(!m->pending)return SYSOP_MAIL_NONE;
    m->pending=0;
    if(!sent)return SYSOP_MAIL_RETRY;
    m->after=now+30000;return SYSOP_MAIL_SENT;
}

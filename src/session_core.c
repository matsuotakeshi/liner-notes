#include "session_core.h"
#include <string.h>
#include <stdio.h>
SessionOperationStatus session_core_submit(SessionCore *s,BoardRequest *q,SessionOperationPort port) {
    if(!s->active || s->closing)return SESSION_OPERATION_INACTIVE;
    if(s->pending || (s->wake && s->now<s->wake))return SESSION_OPERATION_BUSY;
    if((q->op=='a' || q->op=='g') && s->now<s->auth_after)return SESSION_OPERATION_THROTTLED;
    q->structured=s->structured;q->session=s->id;q->slot=s->slot;q->user=s->user;q->user_epoch=s->user_epoch;
    memcpy(q->board_grants,s->board_grants,sizeof(q->board_grants));
    memcpy(q->vault_grants,s->vault_grants,sizeof(q->vault_grants));
    if(!s->user && q->op=='P')snprintf(q->username,sizeof(q->username),"%s",*s->guest_handle?s->guest_handle:"guest");
    if(q->op=='j' && s->now<s->auth_after)q->action='t';
    if(!port.enqueue || !port.enqueue(port.context,q))return SESSION_OPERATION_UNAVAILABLE;
    s->pending=1;if(q->op=='a' || q->op=='g')s->auth_after=s->now+1000;return SESSION_OPERATION_ACCEPTED;
}
int session_core_complete(SessionCore *s,const BoardResult *r,uint64_t now) {
    if(!s->active || s->closing || s->id!=r->session || s->slot!=r->slot)return 0;
    s->now=now;s->pending=0;return 1;
}
void session_core_identity(SessionCore *s,const BoardResult *r,uint64_t now) {
    if(!r->auth)return;
    s->user=r->auth>0?(uint32_t)r->auth:0;s->user_epoch=r->user_epoch;
    s->current=0;s->board=1;s->auth_after=now+1000;
    memset(s->board_grants,0,sizeof(s->board_grants));memset(s->vault_grants,0,sizeof(s->vault_grants));
    if(r->auth>0) { snprintf(s->username,sizeof(s->username),"%s",r->chat_login);s->member_level=(unsigned)r->member_level; }
}

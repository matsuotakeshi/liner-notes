#include "session_core.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct { unsigned calls;int accept;BoardRequest *copy; } Queue;
static int enqueue(void *context,const BoardRequest *q) { Queue *v=context;v->calls++;*v->copy=*q;return v->accept; }
int main(void) {
    BoardRequest *q=calloc(1,sizeof(*q)),*copy=calloc(1,sizeof(*copy));BoardResult *r=calloc(1,sizeof(*r));assert(q && copy && r);
    SessionCore s={.id=91,.slot=2,.active=1,.user=7,.user_epoch=11,.board=3,.now=1000};
    s.board_grants[3]=5;s.vault_grants[8]=2;Queue queue={0,1,copy};SessionOperationPort port={&queue,enqueue};
    q->op='R';q->session=888;q->slot=9;q->user=99;q->user_epoch=999;q->board=8;q->board_grants[4]=888;q->vault_grants[4]=888;
    assert(session_core_submit(&s,q,port)==SESSION_OPERATION_ACCEPTED && s.pending);
    assert(copy->session==91 && copy->slot==2 && copy->user==7 && copy->user_epoch==11 && copy->board==8);
    assert(copy->board_grants[3]==5 && !copy->board_grants[4] && copy->vault_grants[8]==2 && !copy->vault_grants[4]);
    assert(session_core_submit(&s,q,port)==SESSION_OPERATION_BUSY && queue.calls==1);
    r->session=92;r->slot=2;assert(!session_core_complete(&s,r,2000) && s.pending && s.now==1000);
    r->session=91;r->slot=3;assert(!session_core_complete(&s,r,2000));
    r->slot=2;assert(session_core_complete(&s,r,2000) && !s.pending);
    queue.accept=0;assert(session_core_submit(&s,q,port)==SESSION_OPERATION_UNAVAILABLE && !s.pending);
    s.closing=1;assert(session_core_submit(&s,q,port)==SESSION_OPERATION_INACTIVE);s.closing=0;
    s.user=0;s.auth_after=2100;q->op='a';assert(session_core_submit(&s,q,port)==SESSION_OPERATION_THROTTLED);
    s.now=2100;queue.accept=1;assert(session_core_submit(&s,q,port)==SESSION_OPERATION_ACCEPTED && s.auth_after==3100);
    assert(session_core_complete(&s,r,2101));assert(session_core_submit(&s,q,port)==SESSION_OPERATION_THROTTLED);
    r->auth=7;r->user_epoch=19;r->member_level=2;strcpy(r->chat_login,"alice");session_core_identity(&s,r,2200);
    assert(s.user==7 && s.user_epoch==19 && s.member_level==2 && !strcmp(s.username,"alice") && !s.board_grants[3] && !s.vault_grants[8]);
    r->auth=-1;session_core_identity(&s,r,2300);assert(!s.user);
    q->op='P';strcpy(s.guest_handle,"visitor");assert(session_core_submit(&s,q,port)==SESSION_OPERATION_ACCEPTED);assert(!strcmp(copy->username,"visitor"));
    free(q);free(copy);free(r);puts("PASS: UI-free operations, actor/grant isolation, throttle, stale completion and identity transitions");return 0;
}

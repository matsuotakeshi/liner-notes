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
    r->auth=7;r->user_epoch=19;r->member_level=2;strcpy(r->chat_login,"alice");assert(session_core_complete(&s,r,2200));
    assert(s.user==7 && s.user_epoch==19 && s.member_level==2 && !strcmp(s.username,"alice") && !s.board_grants[3] && !s.vault_grants[8]);
    r->auth=-1;assert(session_core_complete(&s,r,2300));assert(!s.user);
    q->op='P';strcpy(s.guest_handle,"visitor");assert(session_core_submit(&s,q,port)==SESSION_OPERATION_ACCEPTED);assert(!strcmp(copy->username,"visitor"));
    /* Lifecycle and results have the same behavior without a Ui or worker. */
    session_core_init(&s,100,4,1,1);assert(s.active && s.board==1 && !s.user && !s.username[0]);
    memset(r,0,sizeof(*r));r->session=100;r->slot=4;r->sigop=-1;
    r->auth=9;r->user_epoch=22;r->chat_admin=1;r->member_level=5;strcpy(r->chat_login,"owner");
    assert(session_core_complete(&s,r,4000));assert(s.user==9 && s.admin && s.member_level==5);
    r->auth=0;r->board=3;r->selected=42;r->sigop=1;
    assert(session_core_complete(&s,r,4001));assert(s.board==3 && s.current==42 && s.sigop);
    r->board=4;r->selected=99;r->slot=5;s.pending=1;
    assert(!session_core_complete(&s,r,4002) && s.pending && s.board==3 && s.current==42);
    SessionResultView stale=session_core_receive(&s,r,4002,0);assert(stale.status==SESSION_RESULT_IGNORED && !stale.content && !stale.page && !stale.effects && s.pending);
    r->slot=4;assert(session_core_complete_mode(&s,r,4002,1));assert(s.board==3 && s.current==42);
    r->board=r->selected=0;r->service_action='2';r->board_grant=1024;r->board_revision=7;
    r->feedback.file_unlocked=1;r->feedback.grant_board=1024;r->feedback.grant_revision=8;
    assert(session_core_complete(&s,r,4003));assert(s.board_grants[1024]==7 && s.vault_grants[1024]==8 && s.auth_after==4003);
    r->board_grant=1025;r->feedback.grant_board=1025;
    assert(session_core_complete(&s,r,4004));assert(s.board_grants[1024]==7 && s.vault_grants[1024]==8);
    r->board_grant=0;r->feedback.grant_board=0;assert(session_core_complete(&s,r,4005));assert(!s.board_grants[0] && !s.vault_grants[0]);
    r->service_action=0;r->feedback.file_unlocked=0;r->password_changed=1;r->account_user=10;r->account_epoch=23;
    assert(session_core_complete(&s,r,4006));assert(s.user_epoch==22);
    r->account_user=9;r->account_epoch=21;assert(session_core_complete(&s,r,4007));assert(s.user_epoch==22);
    r->account_epoch=23;assert(session_core_complete(&s,r,4008));assert(s.user_epoch==23 && s.auth_after==5008);
    /* GROUPMOVE reports the calling actor's epoch without a target account. */
    r->account_user=0;r->account_epoch=24;assert(session_core_complete(&s,r,4009));assert(s.user_epoch==24);
    r->password_changed=0;r->content.kind=1;r->content.id=42;strcpy(r->draft_body,"authorized body");
    SessionResultView view=session_core_result(r);assert(view.status==SESSION_RESULT_OK && view.content->id==42 && view.body==r->draft_body && view.effects==&r->feedback);
    r->page.kind=OPERATION_PAGE_ARTICLES;r->page.data.articles.count=1;view=session_core_result(r);assert(view.page && view.page->data.articles.count==1);
    r->feedback.failed=1;r->board=8;r->selected=88;r->service_action='2';r->board_grant=8;r->board_revision=1;
    assert(session_core_complete(&s,r,4010));assert(s.board==3 && s.current==42 && !s.board_grants[8]);
    view=session_core_result(r);assert(view.status==SESSION_RESULT_FAILED && !view.content && !view.body && !view.attachments && !view.effects && !view.page);
    s.pending=1;assert(!session_core_logout(&s) && s.user==9);s.pending=0;
    assert(session_core_logout(&s));assert(!s.user && !s.user_epoch && !s.username[0] && !s.member_level && !s.admin && !s.sigop && !s.board_grants[1024] && !s.vault_grants[1024] && s.board==1);
    r->feedback.failed=0;r->board=r->selected=0;r->service_action=0;r->auth=9;r->user_epoch=24;
    assert(session_core_complete(&s,r,4011));r->auth=-1;assert(session_core_complete(&s,r,4012));assert(!s.user && !s.username[0] && !s.member_level && !s.admin);
    r->auth=9;r->revoked=1;s.pending=1;s.wake=9999;
    assert(session_core_complete_mode(&s,r,4013,1));assert(!s.user && s.closing && !s.pending && !s.wake);
    view=session_core_result(r);assert(view.status==SESSION_RESULT_REVOKED && !view.content && !view.body && !view.effects && !view.page);
    assert(!session_core_complete(&s,r,4014));assert(session_core_submit(&s,q,port)==SESSION_OPERATION_INACTIVE);
    session_core_init(&s,101,4,1,1);assert(!session_core_complete(&s,r,4015));s.user=9;s.user_epoch=24;session_core_abort(&s);assert(!s.active && s.closing && s.user==9 && s.user_epoch==24);assert(session_core_submit(&s,q,port)==SESSION_OPERATION_INACTIVE);
    free(q);free(copy);free(r);puts("PASS: UI-free operations, actor/grant isolation, throttle, stale completion and identity transitions");return 0;
}

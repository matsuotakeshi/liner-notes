#include "session_core.h"
#include <string.h>
#include <stdio.h>
SessionOperationStatus session_core_submit(SessionCore *s,OperationRequest *q,SessionOperationPort port) {
    if(!s->active || s->closing)return SESSION_OPERATION_INACTIVE;
    if(s->pending || (s->wake && s->now<s->wake))return SESSION_OPERATION_BUSY;
    if(s->login_required && !s->user && q->op!='a' && q->op!='g' && q->op!=BOARD_SSH_KEY)
        return SESSION_OPERATION_LOGIN_REQUIRED;
    if((q->op=='a' || q->op=='g' || q->op==BOARD_SSH_KEY) && s->now<s->auth_after)return SESSION_OPERATION_THROTTLED;
    q->structured=s->structured;q->session=s->id;q->slot=s->slot;q->user=s->user;q->user_epoch=s->user_epoch;
    memcpy(q->board_grants,s->board_grants,sizeof(q->board_grants));
    memcpy(q->vault_grants,s->vault_grants,sizeof(q->vault_grants));
    if(!s->user && q->op=='P')snprintf(q->username,sizeof(q->username),"%s",*s->guest_handle?s->guest_handle:"guest");
    if(q->op=='j' && s->now<s->auth_after)q->action='t';
    if(!port.enqueue || !port.enqueue(port.context,q))return SESSION_OPERATION_UNAVAILABLE;
    s->pending=1;if(q->op=='a' || q->op=='g' || q->op==BOARD_SSH_KEY)s->auth_after=s->now+1000;return SESSION_OPERATION_ACCEPTED;
}
void session_core_init(SessionCore *s,unsigned long id,int slot,int boards,int members) {
    memset(s,0,sizeof(*s));s->active=1;s->id=id;s->slot=slot;s->board_enabled=boards;s->member_mode=members;s->board=1;
}
static void clear_identity(SessionCore *s) {
    s->mail_notify_revision=0;s->mail_notify_deny=0;
    s->user=s->user_epoch=s->current=0;s->board=1;s->username[0]=0;s->member_level=0;s->admin=s->sigop=0;
    memset(s->board_grants,0,sizeof(s->board_grants));session_core_clear_vault_grants(s);
}
void session_core_clear_vault_grants(SessionCore *s) { memset(s->vault_grants,0,sizeof(s->vault_grants)); }
int session_core_logout(SessionCore *s) {
    if(s->pending)return 0;
    clear_identity(s);return 1;
}
void session_core_revoke(SessionCore *s) { clear_identity(s);s->pending=0;s->wake=0;s->closing=1; }
/* Keep the actor until the transport saves disconnect preferences. Disposal
 * erases it; revocation is the separate security invalidation path. */
void session_core_abort(SessionCore *s) { s->active=0;s->closing=1; }
void session_core_close(SessionCore *s) { s->closing=1; }
void session_core_throttle(SessionCore *s,uint64_t now) { s->auth_after=now+1000; }
int session_core_password_epoch(SessionCore *s,unsigned long session,uint32_t user,uint32_t epoch) {
    if(!s->active || s->closing || s->id!=session || !user || s->user!=user || epoch<s->user_epoch)return 0;
    s->user_epoch=epoch;return 1;
}
int session_core_matches(const SessionCore *s,const OperationResult *r) {
    return s->active && !s->closing && s->id==r->session && s->slot==r->slot;
}
int session_core_complete_mode(SessionCore *s,const OperationResult *r,uint64_t now,int keep_position) {
    if(!session_core_matches(s,r))return 0;
    s->now=now;s->pending=0;
    if(r->revoked) { session_core_revoke(s);return 1; }
    if(r->auth) {
        clear_identity(s);session_core_throttle(s,now);
        if(r->auth>0) {
            s->user=(uint32_t)r->auth;s->user_epoch=r->user_epoch;s->member_level=(unsigned)r->member_level;
            snprintf(s->username,sizeof(s->username),"%s",r->chat_login);s->admin=!!r->chat_admin;
        }
    }
    if(r->sigop>=0)s->sigop=r->sigop;
    if(!r->feedback.failed) {
        if(r->password_changed && session_core_password_epoch(s,r->session,r->account_user?r->account_user:s->user,r->account_epoch))session_core_throttle(s,now);
        if(r->service_action=='2') {
            s->auth_after=now;
            if(r->board_grant && r->board_grant<sizeof(s->board_grants)/sizeof(s->board_grants[0]))s->board_grants[r->board_grant]=r->board_revision;
        }
        uint32_t board=r->feedback.grant_board;
        if(r->feedback.file_unlocked && board && board<sizeof(s->vault_grants)/sizeof(s->vault_grants[0]))s->vault_grants[board]=r->feedback.grant_revision;
        if(!keep_position) {
            if(r->board) { s->board=r->board;s->current=0; }
            if(r->selected)s->current=r->selected;
        }
    }
    return 1;
}
int session_core_complete(SessionCore *s,const OperationResult *r,uint64_t now) { return session_core_complete_mode(s,r,now,0); }
SessionResultView session_core_result(const OperationResult *r) {
    SessionResultView view={0};
    if(r->revoked) { view.status=SESSION_RESULT_REVOKED;return view; }
    if(r->feedback.failed) { view.status=SESSION_RESULT_FAILED;return view; }
    view.status=SESSION_RESULT_OK;view.effects=&r->feedback;
    if(r->page.kind!=OPERATION_PAGE_NONE)view.page=&r->page;
    if(r->content.kind) { view.content=&r->content;view.body=r->draft_body;view.attachments=&r->mail_attachments; }
    return view;
}
SessionResultView session_core_receive(SessionCore *s,const OperationResult *r,uint64_t now,int keep_position) {
    if(!session_core_complete_mode(s,r,now,keep_position)) {
        SessionResultView ignored={.status=SESSION_RESULT_IGNORED};return ignored;
    }
    return session_core_result(r);
}

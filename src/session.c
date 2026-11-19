#define _POSIX_C_SOURCE 200809L
#include "editor_core.h"
#include "batch_protocol.h"
#include "list_navigation_protocol.h"
#include "traversal_protocol.h"
#include "editor_commands.h"
#include "editor_view.h"
#include "classic_input.h"
#include "terminal_pager.h"
#include "connector_protocol.h"
#include "runtime_limits.h"
#include "app_limits.h"
#include "activity.h"
#include "vault.h"
#include "session_internal.h"
#include "members.h"
#include "catalog.h"
#include "store_limits.h"
#include "profile_store.h"
#include "menu.h"
#include "chat.h"
#include "date_filter.h"
#include "vault_bridge.h"
#include "transfer_line.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <openssl/crypto.h>

static NotificationAudience notification_audience(const Session *s);

void session_abort(Session *s) { s->active=0; }

void session_dimensions(Session *s,unsigned columns,unsigned rows) {
    TerminalDimensions size=terminal_dimensions(columns,rows);s->columns=size.columns;s->rows=size.rows;
}

void session_impl_selection_reset(Session *s) {
    s->file_selected=s->file_selecting=0;s->batch_screen=s->batch_copying=0;
    batch_reset(&s->batch);
    vault_navigation_reset(&s->vault);
    s->game_menu=0;s->vault_mode=0;
    memset(s->vault_grants,0,sizeof(s->vault_grants));
    profile_form_target(&s->profile_form,0);profile_walk_reset(&s->profile_walk);
    s->talk_once=s->talk_return_editor=s->status_pending=s->memo_input=0;
    s->talk_target=s->reply_target=0;s->talk_select=s->talk_lost=s->outside_talk=0;
    s->session_impl_classic_input=0;s->classic_id=0;s->classic_area=CM_MAIN;
}

void session_impl_submit(Session *s,BoardRequest *q) {
    q->session=s->id; q->slot=s->slot;
    if(q->op=='V')session_impl_preferences_request(s,q);
    q->user=s->user; q->user_epoch=s->user_epoch;if(!s->user && q->op=='P')snprintf(q->username,sizeof(q->username),"%s",*s->guest_handle?s->guest_handle:"guest");
    if(q->op=='j' && s->now<s->auth_after)q->action='t';
    if(q!=&s->scan)q->board=s->board;
    if(q->op=='Z' && q->until && s->check.active==1)q->board=s->check.board;
    memcpy(q->board_grants,s->board_grants,sizeof(q->board_grants));memcpy(q->vault_grants,s->vault_grants,sizeof(q->vault_grants));
    if(q->op=='v') s->ui.unread_root=q->number;
    if(board_submit(q)) s->pending=1;
    else session_impl_emit(s,"ERR board busy\r\n");
}

void session_init(Session *s,SessionIO io,unsigned long id,int slot,int boards,int members,int menu,Language language) {
    memset(s,0,sizeof(*s)); s->io=io;s->menu_mode=1;s->check.automatic=menu; s->active=1; s->id=id; s->slot=slot;
    s->binary_allowed=1; s->board_enabled=boards; s->member_mode=members; s->board=1; s->ui.language=language;
    char greeting[128]; snprintf(greeting,sizeof(greeting),"RTBBS-PROBE %lu\r\n",id); session_impl_queue_text(s,greeting);
    session_dimensions(s,80,24);
    if(menu) { s->ui.enabled=1; session_impl_queue_text(s,banner_text());session_impl_queue_text(s,login_text(0));session_impl_queue_text(s,login_text(1));session_impl_show_menu(s,1); session_impl_prompt(s); }
}

void session_dispose(Session *s) { connector_block_clear(&s->connector_block);s->active=0;chat_disconnect(s->id);OPENSSL_cleanse(s,sizeof(*s)); }

int session_ready(const Session *s) { return s->active && !s->wake && !s->pending && !s->closing && !s->zmodem; }

int session_secret(const Session *s) { return s->password_mode!=0; }

static NotificationAudience notification_audience(const Session *s) {
    unsigned blocked=(s->door?NOTICE_DOOR:0)|(s->binary?NOTICE_BINARY:0)|(s->zmodem?NOTICE_TRANSFER:0)|
        (s->password_mode?NOTICE_SECRET:0)|(s->pending?NOTICE_PENDING:0)|(s->page[0]?NOTICE_PAGE:0)|
        (s->editing?NOTICE_EDITOR:0)|(s->wake?NOTICE_TIMER:0)|
        ((s->ui.input || s->session_impl_classic_input || s->mail_input || s->mail.delete_id)?NOTICE_INPUT:0)|
        (s->chatting?NOTICE_CHAT:0)|(s->reader.active?NOTICE_SCAN:0);
    return (NotificationAudience){s->user,blocked,s->active,s->closing,s->ui.enabled,s->binary_menu};
}

void session_mail_notify(Session *s,uint32_t user,int deny,uint32_t revision) {
    notification_mail_preference(&s->notifications,s->user,user,deny,revision);
}

void session_mail_arrived(Session *s,uint32_t recipient) {
    NotificationAudience audience=notification_audience(s);
    notification_mail_arrived(&s->notifications,&audience,recipient);
}

void session_announce(Session *s,const char *text) {
    NotificationAudience audience=notification_audience(s);
    notification_announce(&s->notifications,&audience,text);
}

void session_tick(Session *s,uint64_t now) {
    if(!s->connected_at)s->connected_at=now;
    if(check_traversal_auto_ready(&s->check,s->page[0]!=0,s->pending,s->closing,1) && (!s->io.drained || s->io.drained(s->io.context))) { session_impl_extended_command(s,"CHECK DONE");return; }
    if(s->reader.automatic && s->active && !s->pending) {
        if(s->reader.active && s->scan.cursor)session_impl_submit(s,&s->scan);
        else { s->reader.automatic=0;session_impl_prompt(s); }
    }
    s->now=now;
    if(s->editing && s->editor_upload && now>=s->editor_deadline && !s->pending) { s->editor_upload=0;s->session_impl_editor_input=0;session_impl_queue_text(s,s->ui.language==LANGUAGE_JA?"受信終了。編集を続けられます。\r\n":"Text received. Continue editing.\r\n");session_impl_prompt(s); }
    NotificationAudience audience=notification_audience(s);
    const char *announcement=notification_announcement_peek(&s->notifications,&audience);
    if(announcement) { session_impl_queue_text(s,announcement);notification_announcement_clear(&s->notifications); }
    /* A failed announcement write may abort the session before mail delivery. */
    audience=notification_audience(s);
    uint32_t arrivals=notification_mail_take(&s->notifications,&audience);
    if(arrivals) {
        char text[256];snprintf(text,sizeof(text),ui_text(&s->ui,MSG_MAIL_ARRIVED),arrivals);
        s->chat_notice=1;session_impl_queue_text(s,text);s->chat_prompting=1;session_impl_prompt(s);s->chat_prompting=0;s->chat_notice=0;
    }
    if(connector_block_expired(&s->connector_block,now)) { connector_block_clear(&s->connector_block);session_abort(s);return; }
    if(s->binary && connector_reader_expired(&s->binary_reader,now)) { session_abort(s);return; }
    if(s->active && s->wake && now>=s->wake) { s->wake=0; session_impl_emit(s,"AWAKE\r\n"); session_impl_prompt(s); }
}

int session_line(Session *s,const char *text,size_t size,uint64_t now) {
    if(!session_ready(s)) return 0;
    if(size>ln_limit("INPUT",INPUT_LINE_MAX) || memchr(text,0,size)) { session_abort(s); return 0; }
    char line[INPUT_LINE_MAX+1]; memcpy(line,text,size); line[size]=0; s->now=now;
    if(session_impl_connector_line(s,line,size)) { OPENSSL_cleanse(line,sizeof(line));return 1; }
    session_impl_normalize_command(s,line,size);session_impl_command(s,line); OPENSSL_cleanse(line,sizeof(line)); return 1;
}

void session_revoke(Session *s) {
    session_impl_selection_reset(s);
    s->session_impl_classic_input=0;s->classic_id=0;s->classic_area=CM_MAIN;check_traversal_reset(&s->check);memset(s->vault_grants,0,sizeof(s->vault_grants));
    chat_disconnect(s->id);s->mail_mode=s->mail_input=0;mail_navigation_confirmed(&s->mail,0);
    if(s->chatting) { s->chatting=0;chat_leave(s->id); }
    if(s->door || s->binary || s->zmodem) { session_abort(s);return; }
    if(!s->active || s->closing) return;
    s->game_menu=0;s->vault_mode=0;notification_mail_discard(&s->notifications);s->user=0;s->ui.admin=0;s->ui.sigop=0;s->reset_target=0; s->user_epoch=0; s->current=0; s->board=1; s->password_mode=0;
    session_impl_page_clear(s);
    s->reader.active=0; OPENSSL_cleanse(&s->scan,sizeof(s->scan));
    s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0; s->pending=0; s->wake=0; s->ui.input=0;
    OPENSSL_cleanse(s->old_password,sizeof(s->old_password)); OPENSSL_cleanse(s->new_password,sizeof(s->new_password));
    session_impl_undo_clear(s); s->draft_revision=0;
    OPENSSL_cleanse(&s->draft,sizeof(s->draft));
    session_impl_emit(s,"ERR session revoked\r\n"); s->closing=1;
}

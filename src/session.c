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

void session_abort(Session *s) { session_core_abort(&s->core); }

void session_dimensions(Session *s,unsigned columns,unsigned rows) {
    TerminalDimensions size=terminal_dimensions(columns,rows);s->presentation.columns=size.columns;s->presentation.rows=size.rows;
}

void session_impl_selection_reset(Session *s) {
    s->files.file_selected=s->files.file_selecting=0;s->files.batch_screen=s->files.batch_copying=0;
    batch_reset(&s->files.batch);
    vault_navigation_reset(&s->files.vault);
    s->presentation.game_menu=0;s->files.vault_mode=0;
    session_core_clear_vault_grants(&s->core);
    profile_form_target(&s->reading.profile_form,0);profile_walk_reset(&s->reading.profile_walk);
    s->conversation.talk_once=s->conversation.talk_return_editor=s->conversation.status_pending=s->conversation.memo_input=0;
    s->conversation.talk_target=s->conversation.reply_target=0;s->conversation.talk_select=s->conversation.talk_lost=s->conversation.outside_talk=0;
    s->presentation.classic_input=0;s->presentation.classic_id=0;s->presentation.classic_area=CM_MAIN;
}

static int enqueue_operation(void *context,const BoardRequest *q) { (void)context;return board_submit(q); }
void session_impl_submit(Session *s,BoardRequest *q) {
    if(q->op=='V')session_impl_preferences_request(s,q);
    if(q!=&s->reading.scan)q->board=s->core.board;
    if(q->op=='Z' && q->until && s->reading.check.active==1)q->board=s->reading.check.board;
    if(q->op=='v')s->presentation.ui.unread_root=q->number;
    SessionOperationStatus result=session_core_submit(&s->core,q,(SessionOperationPort){NULL,enqueue_operation});
    if(result==SESSION_OPERATION_THROTTLED)session_impl_emit(s,"ERR try later\r\n");
    else if(result!=SESSION_OPERATION_ACCEPTED)session_impl_emit(s,"ERR board busy\r\n");
}

int session_init_ui(Session *s,SessionIO io,unsigned long id,int slot,int boards,int members,int menu,Language language,const SessionUiConfig *config) {
    SessionUiConfig defaults=session_ui_default();
    if(!config)config=&defaults;
    memset(s,0,sizeof(*s));
    if(!session_ui_config_valid(config) || !io.write)return 0;
    s->ui_binding.config=*config;s->ui_binding.active=config->adapters[config->default_index];
    s->connection.io=io;session_core_init(&s->core,id,slot,boards,members);
    s->connection.binary_allowed=1;session_dimensions(s,80,24);
    s->ui_binding.active->enter(s,1,menu,language);
    return s->core.active;
}

void session_init(Session *s,SessionIO io,unsigned long id,int slot,int boards,int members,int menu,Language language) {
    (void)session_init_ui(s,io,id,slot,boards,members,menu,language,NULL);
}

void session_dispose(Session *s) { connector_block_clear(&s->connection.connector_block);session_core_abort(&s->core);chat_disconnect(s->core.id);OPENSSL_cleanse(s,sizeof(*s)); }

int session_ready(const Session *s) { return s->core.active && !s->core.wake && !s->core.pending && !s->core.closing && !s->connection.zmodem; }

int session_secret(const Session *s) { return s->auth.password_mode!=0; }


int session_line(Session *s,const char *text,size_t size,uint64_t now) {
    if(!session_ready(s)) return 0;
    if(size>ln_limit("INPUT",INPUT_LINE_MAX) || memchr(text,0,size)) { session_abort(s); return 0; }
    char line[INPUT_LINE_MAX+1]; memcpy(line,text,size); line[size]=0; s->core.now=now;
    if(session_impl_connector_line(s,line,size)) { OPENSSL_cleanse(line,sizeof(line));return 1; }
    s->ui_binding.active->line(s,line,size); OPENSSL_cleanse(line,sizeof(line)); return 1;
}

void session_revoke(Session *s) {
    session_impl_selection_reset(s);
    s->presentation.classic_input=0;s->presentation.classic_id=0;s->presentation.classic_area=CM_MAIN;check_traversal_reset(&s->reading.check);session_core_clear_vault_grants(&s->core);
    chat_disconnect(s->core.id);s->mail_state.mail_mode=s->mail_state.mail_input=0;mail_navigation_confirmed(&s->mail_state.mail,0);
    if(s->conversation.chatting) { s->conversation.chatting=0;chat_leave(s->core.id); }
    if(s->connection.door || s->connection.binary || s->connection.zmodem) { session_core_revoke(&s->core);session_abort(s);return; }
    if(!s->core.active || s->core.closing) return;
    session_core_revoke(&s->core);
    s->presentation.game_menu=0;s->files.vault_mode=0;notification_mail_discard(&s->conversation.notifications);s->presentation.ui.admin=0;s->presentation.ui.sigop=0;s->auth.reset_target=0;  s->auth.password_mode=0;
    session_impl_page_clear(s);
    s->reading.reader.active=0; OPENSSL_cleanse(&s->reading.scan,sizeof(s->reading.scan));
    s->editor.editing=0;s->editor.editor_upload=s->editor.editor_confirm=s->editor.editor_upload_question=0; s->presentation.ui.input=0;
    OPENSSL_cleanse(s->auth.old_password,sizeof(s->auth.old_password)); OPENSSL_cleanse(s->auth.new_password,sizeof(s->auth.new_password));
    session_impl_undo_clear(s); s->editor.draft_revision=0;
    OPENSSL_cleanse(&s->editor.draft,sizeof(s->editor.draft));
    session_impl_emit(s,"ERR session revoked\r\n"); session_core_close(&s->core);
}

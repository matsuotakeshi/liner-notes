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

static ClassicInput classic_input_state(const Session *s);
static void connector_transfer(Session *s,const char *line);
static void connector_link(Session *s,char *line);

int session_impl_binary_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->binary_enter) {
        s->connection.binary_menu=s->presentation.ui.enabled;s->presentation.ui.enabled=0;session_impl_page_clear(s);s->connection.binary=1;connector_reader_enter(&s->connection.binary_reader,now);
        session_impl_queue_text(s,"BINARY READY binary-v1\r\n");return 1;
    }
    if(s->connection.binary) {
        if(completion->binary_size)session_impl_binary_frame(s,0,completion->text,completion->binary_size);
        else session_impl_binary_frame(s,'T',completion->text,strlen(completion->text));
        connector_reader_touch(&s->connection.binary_reader,now);return 1;
    }

    return 0;
}

static ClassicInput classic_input_state(const Session *s) {
    return (ClassicInput){.memo=s->conversation.memo_input,
        .selection=s->presentation.classic_input==CI_FILE_ACTION || s->presentation.classic_input==CI_BATCH,
        .application=s->auth.application.step,.classic=s->presentation.classic_input,.secret=session_secret(s),
        .input=s->presentation.ui.input,.mail_input=s->mail_state.mail_input,.paging=!!s->presentation.page[0],.binary=s->connection.binary,.transfer=s->connection.zmodem,
        .editing=s->editor.editing,.chatting=s->conversation.chatting,.gather=s->conversation.gather.mode,.talk=s->conversation.outside_talk,
        .line_editor=s->editor.editor_line,.editor_input=s->editor.editor_input,.mail_mode=s->mail_state.mail_mode};
}

int session_rtbbs_uppercase(const Session *s,const char *line,size_t size) {
    ClassicInput state=classic_input_state(s);return classic_input_uppercase(&state,line,size);
}

void session_impl_normalize_command(Session *s,char *line,size_t size) {
    ClassicInput state=classic_input_state(s);classic_input_normalize(&state,line,size);
}

static void connector_transfer(Session *s,const char *line) {
    BoardRequest q={0};
    if(!strncmp(line,"MFILE",5)) { q.op=BOARD_MAIL_TRANSFER;line++; }
    else q.op='$';
    snprintf(q.body,sizeof(q.body),"%s",line);session_impl_submit(s,&q);
}

static void connector_link(Session *s,char *line) {
    if(!s->core.user || s->editor.editing || session_secret(s) || s->conversation.chatting || s->conversation.gather.mode || !board_link_enabled(s->core.board,s->core.user)) { session_impl_queue_text(s,"ERR link denied\r\n");return; }
    BoardRequest q={0};LinkWireRequest request;
    _Static_assert(sizeof(q.link_origin)==sizeof(request.origin),"link origin capacity");
    _Static_assert(sizeof(q.link_article)==sizeof(request.article),"link article capacity");
    _Static_assert(sizeof(q.username)==sizeof(request.author),"link author capacity");
    _Static_assert(sizeof(q.title)==sizeof(request.title),"link title capacity");
    if(!link_wire_parse(line,&request,q.body,sizeof(q.body))) { session_impl_queue_text(s,"ERR link syntax\r\n");return; }
    q.op=BOARD_LINK;q.action=request.action;q.cursor=request.cursor;
    q.number=request.parent;q.since=request.timestamp;
    memcpy(q.link_origin,request.origin,sizeof(q.link_origin));
    memcpy(q.link_article,request.article,sizeof(q.link_article));
    memcpy(q.username,request.author,sizeof(q.username));
    memcpy(q.title,request.title,sizeof(q.title));session_impl_submit(s,&q);
}

int session_impl_connector_line(Session *s,char *line,size_t size) {
    ConnectorEnvelope envelope=connector_envelope(line,size,s->connection.connector_token);
    if(*s->connection.connector_token && link_wire_command(line)) { connector_link(s,line);return 1; }
    if(envelope.kind==CONNECTOR_LINKBLOCK) {
        const char *end;uint32_t n;connector_block_clear(&s->connection.connector_block);
        if(!s->core.user || s->editor.editing || session_secret(s) || s->conversation.chatting || s->conversation.gather.mode || !board_link_enabled(s->core.board,s->core.user) || !session_impl_number_arg(envelope.payload,&n,&end) || *end || !n || n>CONNECTOR_LINK_BLOCK_MAX) { session_impl_queue_text(s,"ERR link syntax\r\n");return 1; }
        if(connector_block_begin(&s->connection.connector_block,CONNECTOR_LINK_BLOCK,n,s->core.now)<1)session_abort(s);
        return 1;
    }
    if(envelope.kind==CONNECTOR_TRANSFER) {
        const char *cmd=envelope.payload;
        if(s->core.user && !s->auth.password_mode && !s->conversation.chatting && !s->conversation.gather.mode &&
           (!s->editor.editing || s->editor.draft.op=='m') && !strncmp(cmd,"MFILE",5) && transfer_line_command(cmd+1))connector_transfer(s,cmd);
        else session_impl_queue_text(s,"ERR vault syntax\r\n");
        return 1;
    }
    if(envelope.kind==CONNECTOR_BLOCK) {
        const char *end;uint32_t n;
        connector_block_clear(&s->connection.connector_block);
        if(!s->core.user || (s->editor.editing && s->editor.draft.op!='m') || s->auth.password_mode || s->conversation.chatting || s->conversation.gather.mode ||
           !session_impl_number_arg(envelope.payload,&n,&end) || *end || !n || n>CONNECTOR_FILE_BLOCK_MAX) { session_impl_queue_text(s,"ERR vault syntax\r\n");return 1; }
        if(connector_block_begin(&s->connection.connector_block,CONNECTOR_FILE_BLOCK,n,s->core.now)<1)session_abort(s);
        return 1;
    }
    if(envelope.kind==CONNECTOR_DATA) {
        size_t n=envelope.size;
        int received=connector_block_append(&s->connection.connector_block,envelope.payload,n);
        if(received<0) { session_impl_queue_text(s,"ERR vault syntax\r\n");return 1; }
        if(received>0) {
            ConnectorBlockKind kind;char *session_impl_command=connector_block_take(&s->connection.connector_block,&kind);
            if(kind==CONNECTOR_LINK_BLOCK && !strncmp(session_impl_command,"LINKAPPLY ",10))connector_link(s,session_impl_command);
            else if(kind==CONNECTOR_FILE_BLOCK && ((!s->editor.editing && !strncmp(session_impl_command,"FILEPUTBLOCK ",13)) || !strncmp(session_impl_command,"MFILEPUTBLOCK ",14)))connector_transfer(s,session_impl_command);
            else session_impl_queue_text(s,"ERR vault syntax\r\n");
            free(session_impl_command);
        }return 1;
    }
    if(envelope.kind==CONNECTOR_LITERAL && s->editor.editing && !s->auth.password_mode && !s->conversation.chatting) {
        s->connection.connector_literal=1;session_impl_queue_text(s,"CONNECTOR LITERAL READY\r\n");return 1;
    }
    if(!s->presentation.ui.enabled && !s->editor.editing && !s->conversation.chatting && !s->auth.password_mode &&
       envelope.kind==CONNECTOR_OPEN) {
        memcpy(s->connection.connector_token,envelope.payload,64);s->connection.connector_token[64]=0;
        session_impl_queue_text(s,"CONNECTOR READY 1\r\n");return 1;
    }
    if(envelope.kind==CONNECTOR_SYNC) {
        char out[2048];
        ConnectorState state={
            .connection=s->core.id,.user=s->core.user,.username=s->core.user?s->core.username:"",
            .board=s->core.board,.article=s->core.current,.admin=!!s->presentation.ui.admin,.sigop=!!s->presentation.ui.sigop,.level=s->core.member_level,
            .editing=s->editor.editing,.editor_kind=(unsigned char)s->editor.draft.op,.secret=session_secret(s),.password_mode=s->auth.password_mode,
            .mail=s->mail_state.mail_mode,.mail_current=s->mail_state.mail.current,.chat=s->conversation.chatting,.talk=s->conversation.outside_talk,
            .pager=!!s->presentation.page[0],.gather=s->conversation.gather.mode,
            .input=s->auth.application.step?s->auth.application.step:s->presentation.classic_input?s->presentation.classic_input:s->conversation.memo_input?s->conversation.memo_input:s->mail_state.mail_input?s->mail_state.mail_input:s->conversation.talk_select,
            .confirmation=s->mail_state.mail.delete_id,.binary=s->connection.binary,.door=s->connection.door,.members=s->core.member_mode,
            .feature_mail=board_feature_enabled("mail"),.feature_chat=board_feature_enabled("chat"),.feature_vault=board_feature_enabled("vault")
        };
        if(!connector_state_format(&state,envelope.payload,out,sizeof(out))) { session_abort(s);return 1; }
        session_impl_queue_text(s,out);return 1;
    }
    if(s->connection.connector_literal) {
        s->connection.connector_literal=0;
        if(!s->editor.editing || s->auth.password_mode || s->conversation.chatting)session_impl_queue_text(s,"ERR connector editor state\r\n");
        else if(!session_impl_safe_text(line))session_impl_queue_text(s,"ERR control character\r\n");
        else {
            if(session_impl_edit_apply(s,EDITOR_APPEND_LINE,0,0,line)!=EDITOR_OK)session_impl_queue_text(s,"ERR body limit\r\n");
        }
        return 1;
    }
    return 0;
}

int session_binary_byte(Session *s,unsigned char byte,uint64_t now) {
    if(!s->connection.binary || !session_ready(s))return 0;
    ConnectorFrame frame;int received=connector_reader_feed(&s->connection.binary_reader,byte,now,&frame);
    if(received<0) { session_abort(s);return -1; }if(!received)return 0;
    if(frame.kind=='Q') {
        session_impl_binary_frame(s,'E',"",0);s->connection.binary=0;s->presentation.ui.enabled=s->connection.binary_menu;
        session_impl_queue_text(s,"BINARY CLOSED\r\n");if(s->presentation.ui.enabled)session_impl_show_menu(s,1);session_impl_prompt(s);return 1;
    }
    BoardRequest q={0};
    if(frame.kind=='L') {
        memcpy(q.body,frame.data,frame.size);q.body[frame.size]=0;
        if(!transfer_line_command(q.body)) { session_abort(s);return -1; }q.op='$';
    } else {
        q.op='^';q.action=frame.kind;q.number=frame.id;q.cursor=frame.offset;q.scope=(int)frame.size;
        if(frame.size)memcpy(q.body,frame.data,frame.size);
    }
    session_impl_submit(s,&q);return 1;
}

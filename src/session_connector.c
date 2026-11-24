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
        s->connection.binary_menu=rtbbs(s)->presentation.ui.enabled;rtbbs(s)->presentation.ui.enabled=0;session_impl_page_clear(s);s->connection.binary=1;connector_reader_enter(&s->connection.binary_reader,now);
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
    return (ClassicInput){.memo=rtbbs_const(s)->conversation.memo_input,
        .selection=rtbbs_const(s)->presentation.classic_input==CI_FILE_ACTION || rtbbs_const(s)->presentation.classic_input==CI_BATCH,
        .application=rtbbs_const(s)->auth.application.step,.classic=rtbbs_const(s)->presentation.classic_input,.secret=session_secret(s),
        .input=rtbbs_const(s)->presentation.ui.input,.mail_input=rtbbs_const(s)->mail_state.mail_input,.paging=!!rtbbs_const(s)->presentation.page[0],.binary=s->connection.binary,.transfer=s->connection.zmodem,
        .editing=rtbbs_const(s)->editor.editing,.chatting=rtbbs_const(s)->conversation.chatting,.gather=rtbbs_const(s)->conversation.gather.mode,.talk=rtbbs_const(s)->conversation.outside_talk,
        .line_editor=rtbbs_const(s)->editor.editor_line,.editor_input=rtbbs_const(s)->editor.editor_input,.mail_mode=rtbbs_const(s)->mail_state.mail_mode};
}

int session_rtbbs_uppercase(const Session *s,const char *line,size_t size) {
    if(s->connection.connector_token[0] && size>=10 && !memcmp(line,"CONNECTOR ",10))return 0;
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
    if(!s->core.user || rtbbs(s)->editor.editing || session_secret(s) || rtbbs(s)->conversation.chatting || rtbbs(s)->conversation.gather.mode || !board_link_enabled(s->core.board,s->core.user)) { session_impl_queue_text(s,"ERR link denied\r\n");return; }
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
    if(s->connection.connector_block.data && s->connection.connector_block.kind==CONNECTOR_UPDATE_BLOCK &&
       envelope.kind!=CONNECTOR_DATA) {
        connector_block_clear(&s->connection.connector_block);
        session_abort(s);
        return 1;
    }
    if(*s->connection.connector_token && link_wire_command(line)) { connector_link(s,line);return 1; }
    if(envelope.kind==CONNECTOR_VIEW) { session_impl_web_view(s,envelope.payload);return 1; }
    if(envelope.kind==CONNECTOR_UPDATE) {
        const char *end;uint32_t n;
        if(s->connection.connector_block.data || rtbbs(s)->presentation.ui.enabled ||
           !session_impl_number_arg(envelope.payload,&n,&end) || *end ||
           connector_block_begin(&s->connection.connector_block,CONNECTOR_UPDATE_BLOCK,n,s->core.now)<1)
            session_abort(s);
        return 1;
    }
    if(envelope.kind==CONNECTOR_LINKBLOCK) {
        const char *end;uint32_t n;connector_block_clear(&s->connection.connector_block);
        if(!s->core.user || rtbbs(s)->editor.editing || session_secret(s) || rtbbs(s)->conversation.chatting || rtbbs(s)->conversation.gather.mode || !board_link_enabled(s->core.board,s->core.user) || !session_impl_number_arg(envelope.payload,&n,&end) || *end || !n || n>CONNECTOR_LINK_BLOCK_MAX) { session_impl_queue_text(s,"ERR link syntax\r\n");return 1; }
        if(connector_block_begin(&s->connection.connector_block,CONNECTOR_LINK_BLOCK,n,s->core.now)<1)session_abort(s);
        return 1;
    }
    if(envelope.kind==CONNECTOR_TRANSFER) {
        const char *cmd=envelope.payload;
        if(s->core.user && !rtbbs(s)->auth.password_mode && !rtbbs(s)->conversation.chatting && !rtbbs(s)->conversation.gather.mode &&
           (!rtbbs(s)->editor.editing || rtbbs(s)->editor.draft.op=='m') && !strncmp(cmd,"MFILE",5) && transfer_line_command(cmd+1))connector_transfer(s,cmd);
        else session_impl_queue_text(s,"ERR vault syntax\r\n");
        return 1;
    }
    if(envelope.kind==CONNECTOR_BLOCK) {
        const char *end;uint32_t n;
        connector_block_clear(&s->connection.connector_block);
        if(!s->core.user || (rtbbs(s)->editor.editing && rtbbs(s)->editor.draft.op!='m') || rtbbs(s)->auth.password_mode || rtbbs(s)->conversation.chatting || rtbbs(s)->conversation.gather.mode ||
           !session_impl_number_arg(envelope.payload,&n,&end) || *end || !n || n>CONNECTOR_FILE_BLOCK_MAX) { session_impl_queue_text(s,"ERR vault syntax\r\n");return 1; }
        if(connector_block_begin(&s->connection.connector_block,CONNECTOR_FILE_BLOCK,n,s->core.now)<1)session_abort(s);
        return 1;
    }
    if(envelope.kind==CONNECTOR_DATA) {
        size_t n=envelope.size;
        int received=connector_block_append(&s->connection.connector_block,envelope.payload,n);
        if(received<0) { session_impl_queue_text(s,"ERR vault syntax\r\n");return 1; }
        if(received>0) {
            size_t size=s->connection.connector_block.size;
            ConnectorBlockKind kind;char *session_impl_command=connector_block_take(&s->connection.connector_block,&kind);
            if(kind==CONNECTOR_LINK_BLOCK && !strncmp(session_impl_command,"LINKAPPLY ",10))connector_link(s,session_impl_command);
            else if(kind==CONNECTOR_FILE_BLOCK && ((!rtbbs(s)->editor.editing && !strncmp(session_impl_command,"FILEPUTBLOCK ",13)) || !strncmp(session_impl_command,"MFILEPUTBLOCK ",14)))connector_transfer(s,session_impl_command);
            else if(kind==CONNECTOR_UPDATE_BLOCK) {
                session_impl_web_update(s,session_impl_command);
                OPENSSL_cleanse(session_impl_command,size);
            } else session_impl_queue_text(s,"ERR vault syntax\r\n");
            free(session_impl_command);
        }return 1;
    }
    if(envelope.kind==CONNECTOR_LITERAL && rtbbs(s)->editor.editing && !rtbbs(s)->auth.password_mode && !rtbbs(s)->conversation.chatting) {
        s->connection.connector_literal=1;session_impl_queue_text(s,"CONNECTOR LITERAL READY\r\n");return 1;
    }
    if(!rtbbs(s)->presentation.ui.enabled && !rtbbs(s)->editor.editing && !rtbbs(s)->conversation.chatting && !rtbbs(s)->auth.password_mode &&
       envelope.kind==CONNECTOR_OPEN) {
        memcpy(s->connection.connector_token,envelope.payload,64);s->connection.connector_token[64]=0;
        session_impl_queue_text(s,"CONNECTOR READY 1\r\n");return 1;
    }
    if(envelope.kind==CONNECTOR_SYNC) {
        char out[2048];
        ConnectorState state={
            .connection=s->core.id,.user=s->core.user,.username=s->core.user?s->core.username:"",
            .input_limit=(unsigned)ln_limit("INPUT",INPUT_LINE_MAX),
            .board=s->core.board,.article=s->core.current,.admin=!!rtbbs(s)->presentation.ui.admin,.sigop=!!rtbbs(s)->presentation.ui.sigop,.level=s->core.member_level,
            .editing=rtbbs(s)->editor.editing,.editor_kind=(unsigned char)rtbbs(s)->editor.draft.op,.secret=session_secret(s),.password_mode=rtbbs(s)->auth.password_mode,
            .mail=rtbbs(s)->mail_state.mail_mode,.mail_current=rtbbs(s)->mail_state.mail.current,.chat=rtbbs(s)->conversation.chatting,.talk=rtbbs(s)->conversation.outside_talk,
            .pager=!!rtbbs(s)->presentation.page[0],.gather=rtbbs(s)->conversation.gather.mode,
            .input=rtbbs(s)->auth.application.step?rtbbs(s)->auth.application.step:rtbbs(s)->presentation.classic_input?rtbbs(s)->presentation.classic_input:rtbbs(s)->conversation.memo_input?rtbbs(s)->conversation.memo_input:rtbbs(s)->mail_state.mail_input?rtbbs(s)->mail_state.mail_input:rtbbs(s)->conversation.talk_select,
            .confirmation=rtbbs(s)->mail_state.mail.delete_id,.binary=s->connection.binary,.door=s->connection.door,.members=s->core.member_mode,
            .feature_mail=board_feature_enabled("mail"),.feature_chat=board_feature_enabled("chat"),.feature_vault=board_feature_enabled("vault")
        };
        if(!connector_state_format(&state,envelope.payload,out,sizeof(out))) { session_abort(s);return 1; }
        session_impl_queue_text(s,out);return 1;
    }
    if(s->connection.connector_literal) {
        s->connection.connector_literal=0;
        if(!rtbbs(s)->editor.editing || rtbbs(s)->auth.password_mode || rtbbs(s)->conversation.chatting)session_impl_queue_text(s,"ERR connector editor state\r\n");
        else if(!session_impl_safe_text(line))session_impl_queue_text(s,"ERR control character\r\n");
        else {
            if(session_impl_edit_apply(s,EDITOR_APPEND_LINE,0,0,line)!=EDITOR_OK)session_impl_queue_text(s,"ERR body limit\r\n");
        }
        return 1;
    }
    return 0;
}

int session_rtbbs_binary_byte(Session *s,unsigned char byte,uint64_t now) {
    if(!s->connection.binary || !session_ready(s))return 0;
    ConnectorFrame frame;int received=connector_reader_feed(&s->connection.binary_reader,byte,now,&frame);
    if(received<0) { session_abort(s);return -1; }if(!received)return 0;
    if(frame.kind=='Q') {
        session_impl_binary_frame(s,'E',"",0);s->connection.binary=0;rtbbs(s)->presentation.ui.enabled=s->connection.binary_menu;
        session_impl_queue_text(s,"BINARY CLOSED\r\n");if(rtbbs(s)->presentation.ui.enabled)session_impl_show_menu(s,1);session_impl_prompt(s);return 1;
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

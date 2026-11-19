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
    if(completion->binary_enter) { s->zmodem=1;session_impl_page_clear(s);s->reader.active=0;if(s->zmodem_protocol)session_impl_queue_text(s,s->ui.language==LANGUAGE_JA?"選択した方式で端末の転送を開始してください。\r\n":"Start terminal transfer using the selected protocol.\r\n");else session_impl_queue_text(s,ui_text(&s->ui,(s->zmodem_receive==6 || s->zmodem_receive==9 || s->zmodem_receive==11 || s->zmodem_receive==12)?MSG_ZMODEM_BATCH_UPLOAD_READY:(s->zmodem_receive==2 || s->zmodem_receive==10)?MSG_ZMODEM_BATCH_READY:MSG_ZMODEM_READY)); }
        else { session_impl_emit(s,completion->text);session_impl_prompt(s); }
        return 1;
    }
    if(completion->binary_enter) {
        s->binary_menu=s->ui.enabled;s->ui.enabled=0;session_impl_page_clear(s);s->binary=1;connector_reader_enter(&s->binary_reader,now);
        session_impl_queue_text(s,"BINARY READY binary-v1\r\n");return 1;
    }
    if(s->binary) {
        if(completion->binary_size)session_impl_binary_frame(s,0,completion->text,completion->binary_size);
        else session_impl_binary_frame(s,'T',completion->text,strlen(completion->text));
        connector_reader_touch(&s->binary_reader,now);return 1;
    }

    return 0;
}

static ClassicInput classic_input_state(const Session *s) {
    return (ClassicInput){.memo=s->memo_input,
        .selection=s->session_impl_classic_input==CI_FILE_ACTION || s->session_impl_classic_input==CI_BATCH,
        .application=s->application.step,.classic=s->session_impl_classic_input,.secret=session_secret(s),
        .input=s->ui.input,.mail_input=s->mail_input,.paging=!!s->page[0],.binary=s->binary,.transfer=s->zmodem,
        .editing=s->editing,.chatting=s->chatting,.gather=s->gather.mode,.talk=s->outside_talk,
        .line_editor=s->editor_line,.session_impl_editor_input=s->session_impl_editor_input,.mail_mode=s->mail_mode};
}

int session_uppercase(const Session *s,const char *line,size_t size) {
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
    if(!s->user || s->editing || session_secret(s) || s->chatting || s->gather.mode || !board_link_enabled(s->board,s->user)) { session_impl_queue_text(s,"ERR link denied\r\n");return; }
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
    ConnectorEnvelope envelope=connector_envelope(line,size,s->connector_token);
    if(*s->connector_token && link_wire_command(line)) { connector_link(s,line);return 1; }
    if(envelope.kind==CONNECTOR_LINKBLOCK) {
        const char *end;uint32_t n;connector_block_clear(&s->connector_block);
        if(!s->user || s->editing || session_secret(s) || s->chatting || s->gather.mode || !board_link_enabled(s->board,s->user) || !session_impl_number_arg(envelope.payload,&n,&end) || *end || !n || n>CONNECTOR_LINK_BLOCK_MAX) { session_impl_queue_text(s,"ERR link syntax\r\n");return 1; }
        if(connector_block_begin(&s->connector_block,CONNECTOR_LINK_BLOCK,n,s->now)<1)session_abort(s);
        return 1;
    }
    if(envelope.kind==CONNECTOR_TRANSFER) {
        const char *cmd=envelope.payload;
        if(s->user && !s->password_mode && !s->chatting && !s->gather.mode &&
           (!s->editing || s->draft.op=='m') && !strncmp(cmd,"MFILE",5) && transfer_line_command(cmd+1))connector_transfer(s,cmd);
        else session_impl_queue_text(s,"ERR vault syntax\r\n");
        return 1;
    }
    if(envelope.kind==CONNECTOR_BLOCK) {
        const char *end;uint32_t n;
        connector_block_clear(&s->connector_block);
        if(!s->user || (s->editing && s->draft.op!='m') || s->password_mode || s->chatting || s->gather.mode ||
           !session_impl_number_arg(envelope.payload,&n,&end) || *end || !n || n>CONNECTOR_FILE_BLOCK_MAX) { session_impl_queue_text(s,"ERR vault syntax\r\n");return 1; }
        if(connector_block_begin(&s->connector_block,CONNECTOR_FILE_BLOCK,n,s->now)<1)session_abort(s);
        return 1;
    }
    if(envelope.kind==CONNECTOR_DATA) {
        size_t n=envelope.size;
        int received=connector_block_append(&s->connector_block,envelope.payload,n);
        if(received<0) { session_impl_queue_text(s,"ERR vault syntax\r\n");return 1; }
        if(received>0) {
            ConnectorBlockKind kind;char *session_impl_command=connector_block_take(&s->connector_block,&kind);
            if(kind==CONNECTOR_LINK_BLOCK && !strncmp(session_impl_command,"LINKAPPLY ",10))connector_link(s,session_impl_command);
            else if(kind==CONNECTOR_FILE_BLOCK && ((!s->editing && !strncmp(session_impl_command,"FILEPUTBLOCK ",13)) || !strncmp(session_impl_command,"MFILEPUTBLOCK ",14)))connector_transfer(s,session_impl_command);
            else session_impl_queue_text(s,"ERR vault syntax\r\n");
            free(session_impl_command);
        }return 1;
    }
    if(envelope.kind==CONNECTOR_LITERAL && s->editing && !s->password_mode && !s->chatting) {
        s->connector_literal=1;session_impl_queue_text(s,"CONNECTOR LITERAL READY\r\n");return 1;
    }
    if(!s->ui.enabled && !s->editing && !s->chatting && !s->password_mode &&
       envelope.kind==CONNECTOR_OPEN) {
        memcpy(s->connector_token,envelope.payload,64);s->connector_token[64]=0;
        session_impl_queue_text(s,"CONNECTOR READY 1\r\n");return 1;
    }
    if(envelope.kind==CONNECTOR_SYNC) {
        char out[2048];
        ConnectorState state={
            .connection=s->id,.user=s->user,.username=s->user?s->username:"",
            .board=s->board,.article=s->current,.admin=!!s->ui.admin,.sigop=!!s->ui.sigop,.level=s->member_level,
            .editing=s->editing,.editor_kind=(unsigned char)s->draft.op,.secret=session_secret(s),.password_mode=s->password_mode,
            .mail=s->mail_mode,.mail_current=s->mail.current,.chat=s->chatting,.talk=s->outside_talk,
            .pager=!!s->page[0],.gather=s->gather.mode,
            .input=s->application.step?s->application.step:s->session_impl_classic_input?s->session_impl_classic_input:s->memo_input?s->memo_input:s->mail_input?s->mail_input:s->talk_select,
            .confirmation=s->mail.delete_id,.binary=s->binary,.door=s->door,.members=s->member_mode,
            .feature_mail=board_feature_enabled("mail"),.feature_chat=board_feature_enabled("chat"),.feature_vault=board_feature_enabled("vault")
        };
        if(!connector_state_format(&state,envelope.payload,out,sizeof(out))) { session_abort(s);return 1; }
        session_impl_queue_text(s,out);return 1;
    }
    if(s->connector_literal) {
        s->connector_literal=0;
        if(!s->editing || s->password_mode || s->chatting)session_impl_queue_text(s,"ERR connector editor state\r\n");
        else if(!session_impl_safe_text(line))session_impl_queue_text(s,"ERR control character\r\n");
        else {
            if(session_impl_edit_apply(s,EDITOR_APPEND_LINE,0,0,line)!=EDITOR_OK)session_impl_queue_text(s,"ERR body limit\r\n");
        }
        return 1;
    }
    return 0;
}

int session_binary_byte(Session *s,unsigned char byte,uint64_t now) {
    if(!s->binary || !session_ready(s))return 0;
    ConnectorFrame frame;int received=connector_reader_feed(&s->binary_reader,byte,now,&frame);
    if(received<0) { session_abort(s);return -1; }if(!received)return 0;
    if(frame.kind=='Q') {
        session_impl_binary_frame(s,'E',"",0);s->binary=0;s->ui.enabled=s->binary_menu;
        session_impl_queue_text(s,"BINARY CLOSED\r\n");if(s->ui.enabled)session_impl_show_menu(s,1);session_impl_prompt(s);return 1;
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

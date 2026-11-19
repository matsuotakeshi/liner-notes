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

static void mail_list_request(const Session *s,BoardRequest *q);

int session_impl_mail_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(s->mail_state.mail_pending) {
        s->mail_state.mail_pending=0;
        MailDraftEffect effect=mail_draft_effect(session_impl_mail_event(completion->mail_action),completion->mail_to,completion->mail_id,completion->mail_revision);
        if(effect==MAIL_DRAFT_ATTACH)s->editor.draft.mail_attachments=completion->mail_attachments;
        if(effect==MAIL_DRAFT_OPEN) {
            memset(&s->editor.draft,0,sizeof(s->editor.draft));s->editor.draft.op='m';s->editor.draft.action='S';s->editor.draft.number=completion->mail_to;s->editor.draft.cursor=completion->mail_parent;s->editor.draft.mail_attachments=completion->mail_attachments;
            strcpy(s->editor.draft.title,completion->draft_title);strcpy(s->editor.draft.body,completion->draft_body);s->mail_state.mail_revision=completion->mail_revision;s->editor.editing=1;s->editor.editor_input=0;s->editor.editor_at=1;session_impl_undo_clear(s);
        }
        if(effect==MAIL_DRAFT_SAVED) { OPENSSL_cleanse(&s->editor.draft,sizeof(s->editor.draft));s->editor.editing=0;s->editor.editor_upload=s->editor.editor_confirm=s->editor.editor_upload_question=0;s->mail_state.mail_revision=0;session_impl_undo_clear(s); }
        if(effect==MAIL_DRAFT_SENT || effect==MAIL_DRAFT_RETRY) {
            if(effect==MAIL_DRAFT_SENT) { OPENSSL_cleanse(&s->editor.draft,sizeof(s->editor.draft));s->editor.editing=0;s->editor.editor_upload=s->editor.editor_confirm=s->editor.editor_upload_question=0; }
            else { s->editor.editing=1;s->editor.editor_input=0;s->editor.editor_at=1; }
        }

        mail_navigation_page(&s->mail_state.mail,session_impl_mail_event(completion->mail_action),completion->mail_next);
        if(s->presentation.ui.enabled && s->presentation.pager_enabled && strchr("ILRNT",completion->mail_action) && completion->mail_action) {
            snprintf(s->presentation.page,sizeof(s->presentation.page),"%s",completion->text);s->presentation.page_offset=0;session_impl_page_next(s);
        } else if(!completion->mail_action)session_impl_emit(s,completion->text);
        else session_impl_queue_text(s,completion->text);
        session_impl_prompt(s);return 1;
    }

    return 0;
}

MailSessionEvent session_impl_mail_event(int action) {
    switch(action) {
    case 'L':return MAIL_EVENT_LIST;case 'I':return MAIL_EVENT_FILES;
    case 'B':return MAIL_EVENT_BEGIN;case 'Q':return MAIL_EVENT_REPLY;
    case 'O':return MAIL_EVENT_RESUME;case 'W':return MAIL_EVENT_SAVE;
    case 'S':return MAIL_EVENT_SEND;case 'A':return MAIL_EVENT_ATTACH;
    case 'R':case 'N':case 'T':case 'f':return MAIL_EVENT_READ;
    default:return MAIL_EVENT_OTHER;
    }
}

static void mail_list_request(const Session *s,BoardRequest *q) {
    MailListing plan=mail_navigation_listing(&s->mail_state.mail);
    q->action='L';q->cursor=plan.cursor;q->number=(uint32_t)plan.filter;
    strcpy(q->title,plan.query);
}

void session_impl_mail_submit(Session *s,BoardRequest *q) {
    q->op='m';q->scope=s->presentation.ui.enabled?(int)s->presentation.ui.language+1:0;session_impl_submit(s,q);s->mail_state.mail_pending=s->core.pending;
}

int session_impl_mail_command(Session *s,char *line) {
    int explicit_mail=!strcmp(line,"MAIL") || !strncmp(line,"MAIL ",5);
    if(!s->mail_state.mail_mode && !explicit_mail)return 0;
    if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
    if(explicit_mail) { s->mail_state.mail_mode=1;line=line[4]?line+5:"L"; }
    if(!s->mail_state.mail_input && (!strcmp(line,"G") || !strcmp(line,"GET") || !strncmp(line,"GET ",4))) { BoardRequest q={0};q.op='}';q.action='M';q.number=s->mail_state.mail.current;const char *tail;if(!strncmp(line,"GET ",4) && (!session_impl_number_arg(line+4,&q.number,&tail) || *tail)) { session_impl_emit(s,"ERR command\r\n");return 1; }session_impl_submit(s,&q);return 1; }
    if(!s->mail_state.mail_input && (!strcmp(line,"F") || !strcmp(line,"SCAN") || !strcmp(line,"SCAN ASC") || !strcmp(line,"SCAN DESC"))) { BoardRequest q={0};q.op='m';q.action='f';ReaderPosition start=mail_navigation_start(!strcmp(line,"SCAN DESC"));q.descending=start.descending;q.cursor=start.cursor;q.scope=s->presentation.ui.enabled?(int)s->presentation.ui.language+1:0;session_impl_submit(s,&q);if(s->core.pending) { s->reading.scan=q;s->reading.reader.active=1; }return 1; }
    if(s->mail_state.mail.delete_id) {
        uint32_t id=mail_navigation_confirmed(&s->mail_state.mail,!strcmp(line,"Y") || !strcmp(line,"y"));
        if(id) { BoardRequest q={0};q.action='D';q.number=id;session_impl_mail_submit(s,&q); }
        else session_impl_emit(s,"CANCELLED\r\n");
        return 1;
    }
    if(s->mail_state.mail_input) {
        if(!strcmp(line,"/CANCEL") || !strcmp(line,"0")) { s->mail_state.mail_input=0;memset(&s->editor.draft,0,sizeof(s->editor.draft));session_impl_emit(s,"CANCELLED\r\n");return 1; }
        if(s->mail_state.mail_input==1) {
            if(!*line || strlen(line)>32 || !session_impl_safe_text(line)) { session_impl_emit(s,"ERR command\r\n");return 1; }
            snprintf(s->editor.draft.username,sizeof(s->editor.draft.username),"%s",line);s->mail_state.mail_input=2;return 1;
        }
        if(!*line || strlen(line)>80 || !session_impl_safe_text(line)) { session_impl_emit(s,"ERR title 1..80 bytes, no controls\r\n");return 1; }
        snprintf(s->editor.draft.title,sizeof(s->editor.draft.title),"%s",line);s->mail_state.mail_input=0;s->editor.draft.action='B';session_impl_mail_submit(s,&s->editor.draft);return 1;
    }
    if(!strcmp(line,"0") || !strcmp(line,"EXIT") || !strcmp(line,"/LEAVE")) { s->mail_state.mail_mode=0;s->presentation.ui.view=UI_HOME;session_impl_queue_text(s,s->presentation.ui.enabled?ui_text(&s->presentation.ui,MSG_MAIL_CLOSED):"MAIL CLOSED\r\n");return 1; }
    if(!strcmp(line,"H") || !strcmp(line,"?") || !strcmp(line,"HELP")) { session_impl_show_help(s,"MAIL");return 1; }
    BoardRequest q={0};const char *tail=NULL;uint32_t id=0;
    if(!board_feature_enabled("mail")) { s->mail_state.mail_mode=0;session_impl_emit(s,"ERR feature disabled\r\n");return 1; }
    if(!strcmp(line,"NOTIFY") || !strcmp(line,"NOTIFY ON") || !strcmp(line,"NOTIFY OFF")) { q.action='U';q.number=!strcmp(line,"NOTIFY ON")?1:!strcmp(line,"NOTIFY OFF")?2:0; }
    else if(!strncmp(line,"NEWS ",5) || !strncmp(line,"NEWSFROM ",9)) {
        const char *date=line+5;uint64_t stamp;
        if(!strncmp(line,"NEWSFROM ",9)) { if(!session_impl_number_arg(line+9,&q.cursor,&tail) || *tail!=' ' || q.cursor>CONTENT_ID_MAX)goto invalid;date=tail+1; }
        if(!date_filter(date,&stamp))goto invalid;
        if(!mail_navigation_filter(&s->mail_state.mail,MAIL_LIST_NEWS,date,q.cursor))goto invalid;
        mail_list_request(s,&q);
    }
    else if(!strncmp(line,"SEARCH ",7) || !strncmp(line,"SEARCHFROM ",11)) {
        const char *query=line+7;
        if(!strncmp(line,"SEARCHFROM ",11)) { if(!session_impl_number_arg(line+11,&q.cursor,&tail) || *tail!=' ' || q.cursor>CONTENT_ID_MAX)goto invalid;query=tail+1; }
        if(!mail_navigation_filter(&s->mail_state.mail,MAIL_LIST_SEARCH,query,q.cursor))goto invalid;
        mail_list_request(s,&q);
    }
    else if(!strcmp(line,"FILES") || !strncmp(line,"FILES ",6)) { q.action='I';if(line[5] && (!session_impl_number_arg(line+6,&q.cursor,&tail) || *tail))goto invalid; }
    else if(!strncmp(line,"DROP ",5)) { q.action='X';if(!session_impl_number_arg(line+5,&q.number,&tail) || strcmp(tail," CONFIRM"))goto invalid; }
    else if(!strcmp(line,"DRAFT"))q.action='F';
    else if(!strcmp(line,"RESUME"))q.action='O';
    else if(!strncmp(line,"DRAFT DELETE ",13)) { q.action='C';if(!session_impl_audit_number(line+13,&q.mail_revision))goto invalid; }
    else if(!strcmp(line,"L") || !strcmp(line,"LIST") || !strcmp(line,"SENT") || !strcmp(line,"INBOX") || !strcmp(line,"UNREAD") || !strcmp(line,"+")) {
        if(strcmp(line,"+"))mail_navigation_filter(&s->mail_state.mail,!strcmp(line,"SENT")?MAIL_LIST_SENT:!strcmp(line,"UNREAD")?MAIL_LIST_UNREAD:!strcmp(line,"INBOX")?MAIL_LIST_INBOX:MAIL_LIST_ALL,NULL,0);
        mail_list_request(s,&q);
    } else if(!strcmp(line,"N") || !strcmp(line,"NEXT"))q.action='N';
    else if(!strcmp(line,"T") || !strcmp(line,"LATEST"))q.action='T';
    else if(!strcmp(line,"E") || !strcmp(line,"SEND")) { memset(&s->editor.draft,0,sizeof(s->editor.draft));s->mail_state.mail_input=1;return 1; }
    else if(!strncmp(line,"SEND ",5) || !strncmp(line,"E ",2)) {
        const char *arg=line+(*line=='E'?2:5),*space=strchr(arg,' ');size_t n=space?(size_t)(space-arg):strlen(arg);
        if(!n || n>32)goto invalid;
        memcpy(q.username,arg,n);q.username[n]=0;q.action='B';
        if(!space || !space[1]) { s->editor.draft=q;s->mail_state.mail_input=2;return 1; }
        if(strlen(space+1)>80 || !session_impl_safe_text(space+1))goto invalid;
        strcpy(q.title,space+1);
    } else {
        const char *arg=line;
        if(!strncmp(line,"READ ",5)) { q.action='R';arg=line+5; }
        else if(!strncmp(line,"REPLY ",6)) { q.action='Q';arg=line+6; }
        else if(!strncmp(line,"R ",2)) { q.action='Q';arg=line+2; }
        else if(!strncmp(line,"DELETE ",7)) { q.action='D';arg=line+7; }
        else if(!strncmp(line,"K ",2)) { q.action='D';arg=line+2; }
        else if(!strcmp(line,"R") || !strcmp(line,"K")) { q.action=*line=='R'?'Q':'D';id=s->mail_state.mail.current;tail=""; }
        else if(*line>='0' && *line<='9')q.action='R';
        else return 0;
        if(!id && !session_impl_number_arg(arg,&id,&tail))goto invalid;
        if(!id)goto invalid;
        q.number=id;
        if(q.action=='Q' && *tail==' ' && strlen(tail+1)<=80 && session_impl_safe_text(tail+1))strcpy(q.title,tail+1);
        else if(*tail)goto invalid;
        if(q.action=='D') { mail_navigation_confirm(&s->mail_state.mail,id);if(!s->presentation.ui.enabled)session_impl_queue_text(s,"MAIL DELETE CONFIRM Y/N\r\n");return 1; }
    }
    session_impl_mail_submit(s,&q);return 1;
invalid:session_impl_emit(s,"ERR command\r\n");return 1;
}

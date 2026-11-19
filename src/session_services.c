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

int session_impl_message_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(s->gather.pending) {
        GatherResponse response=completion->service_action=='G'?GATHER_RESPONSE_PAGE:
            completion->service_action=='g'?GATHER_RESPONSE_SAVED:GATHER_RESPONSE_OTHER;
        GatherEffect effect=gather_flow_complete(&s->gather,response,completion->service_next);
        if(response==GATHER_RESPONSE_PAGE)session_impl_queue_text(s,completion->text);
        if(effect==GATHER_EDIT || effect==GATHER_SAVED)memset(&s->draft,0,sizeof(s->draft));
        if(effect==GATHER_EDIT)session_impl_queue_text(s,ui_text(&s->ui,MSG_GATHER_EDIT));
        else if(effect==GATHER_SAVED)session_impl_queue_text(s,completion->text);
        else if(effect==GATHER_FAILED)session_impl_emit(s,completion->text);
        session_impl_prompt(s);return 1;
    }
    if(s->sysop_mail.pending) {
        SysopMailEffect effect=sysop_mail_complete(&s->sysop_mail,completion->service_action=='M',now);
        if(effect==SYSOP_MAIL_SENT) { s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0;memset(&s->draft,0,sizeof(s->draft));session_impl_queue_text(s,ui_text(&s->ui,MSG_SYSOP_MAIL_SENT)); }
        else { s->editing=1;session_impl_emit(s,completion->text); }
        session_impl_prompt(s);return 1;
    }

    return 0;
}

int session_impl_service_command(Session *s,char *line) {
    BoardRequest q={0};q.op='!';q.scope=s->ui.enabled?(int)s->ui.language+1:0;
    if(!strcmp(line,"GATHER") || !strcmp(line,"GATHER READ")) {
        q.action='G';session_impl_submit(s,&q);gather_flow_read_submitted(&s->gather,!strcmp(line,"GATHER READ"),s->pending);return 1;
    }
    if(!strncmp(line,"SYSOPMAIL ",10)) {
        const char *title=line+10;
        if(!sysop_mail_ready(&s->sysop_mail,s->now)) { session_impl_queue_text(s,ui_text(&s->ui,MSG_SYSOP_MAIL_RATE));return 1; }
        if(!board_feature_enabled("mail")) { session_impl_emit(s,"ERR feature disabled\r\n");return 1; }
        if(!sysop_mail_title_valid(title)) { session_impl_emit(s,"ERR title 1..80 bytes, no controls\r\n");return 1; }
        memset(&s->draft,0,sizeof(s->draft));s->draft.op='?';s->draft.action='M';strcpy(s->draft.title,title);s->editing=1;session_impl_undo_clear(s);session_impl_queue_text(s,ui_text(&s->ui,MSG_SYSOP_MAIL_EDIT));return 1;
    }
    if(!strncmp(line,"FILEBOARDINFO ",14)) {
        q.op='Y';q.action=VAULT_CMD_BOARDINFO;if(strlen(line+14)>48 || !session_impl_safe_text(line+14))goto invalid;strcpy(q.title,line+14);session_impl_submit(s,&q);return 1;
    }
    if(!strncmp(line,"FILEBOARDSET ",13) || !strcmp(line,"FILESTATS") || !strncmp(line,"FILESTATS ",10) || !strcmp(line,"FILEHISTORY") || !strncmp(line,"FILEHISTORY ",12)) {
        q.op='Y';
        if(!strncmp(line,"FILEBOARDSET ",13)) { q.action=VAULT_CMD_BOARDSET;if(strlen(line+13)>=256 || !session_impl_safe_text(line+13))goto invalid;strcpy(q.body,line+13); }
        else {
            int history=!strncmp(line,"FILEHISTORY",11);q.action=history?VAULT_CMD_HISTORY:VAULT_CMD_STATS;const char *arg=line+(history?11:9),*tail;
            if(*arg) { if(!strncmp(arg+1,"ALL",3) && (!arg[4] || arg[4]==' '))tail=arg+4;else if(!session_impl_number_arg(arg+1,&q.number,&tail))goto invalid;if(*tail && (*tail!=' ' || !session_impl_number_arg(tail+1,&q.cursor,&tail) || *tail))goto invalid; }
            snprintf(q.title,sizeof(q.title),"%u",q.cursor);
        }
        session_impl_submit(s,&q);
        if(q.action!=VAULT_CMD_BOARDSET && s->pending) { report_navigation_begin(&s->report,q.action==VAULT_CMD_HISTORY?REPORT_FILEHISTORY:REPORT_FILESTATS,q.number,!q.number); }
        return 1;
    }
    if(!strcmp(line,"SIGOPS"))q.action='v';
    else if(!strncmp(line,"SIGOP ",6)) {
        const char *tail;q.action='s';if(!session_impl_number_arg(line+6,&q.number,&tail) || *tail!=' ')goto invalid;
        if(!strcmp(tail+1,"NONE"))q.cursor=0;
        else if(!session_impl_number_arg(tail+1,&q.cursor,&tail) || *tail)goto invalid;
    }
    else if(!strncmp(line,"BOARDSET ",9)) {
        if(strlen(line+9)>=1024 || !session_impl_safe_text(line+9))goto invalid;
        q.action='b';strcpy(q.body,line+9);
    } else if(!strncmp(line,"BOARDINFO ",10)) { const char *tail;q.action='i';if(!session_impl_number_arg(line+10,&q.number,&tail) || *tail)goto invalid; }
    else if(!strcmp(line,"BOARDLOG") || !strncmp(line,"BOARDLOG ",9)) { const char *tail;q.action='j';if(line[8] && (!session_impl_number_arg(line+9,&q.cursor,&tail) || *tail))goto invalid; }
    else if(!strcmp(line,"USERTABLE") || !strncmp(line,"USERTABLE ",10) || !strcmp(line,"USERLOG") || !strncmp(line,"USERLOG ",8) || !strcmp(line,"USERSTATS") || !strncmp(line,"USERSTATS ",10)) {
        int stats=!strncmp(line,"USERSTATS",9),table=!strncmp(line,"USERTABLE",9);q.action=stats?'V':table?'u':'U';const char *arg=line+(stats || table?9:7),*tail;
        if(*arg) {
            arg++;
            if(!strncmp(arg,"ALL",3) && (!arg[3] || arg[3]==' ')) { q.notes_unread=1;tail=arg+3; }
            else if(!session_impl_number_arg(arg,&q.number,&tail))goto invalid;
            if(*tail && (stats || *tail!=' ' || !session_impl_number_arg(tail+1,&q.cursor,&tail) || *tail))goto invalid;
        }
    } else if(!strcmp(line,"CONNECTIONS"))q.action='L';
    else if(!strncmp(line,"ANNOUNCE ",9)) { q.action='A';if(!chat_text_valid(line+9))goto invalid;strcpy(q.body,line+9); }
    else if(!strncmp(line,"DISCONNECT ",11)) { const char *tail;q.action='K';if(!session_impl_number_arg(line+11,&q.number,&tail) || strcmp(tail," CONFIRM"))goto invalid; }
    else if(!strcmp(line,"SHUTDOWN CANCEL"))q.action='C';
    else if(!strcmp(line,"SHUTDOWN NOW CONFIRM"))q.action='S';
    else if(!strncmp(line,"SHUTDOWN ",9)) {
        const char *tail;q.action='S';if(!session_impl_number_arg(line+9,&q.number,&tail) || !q.number || q.number>86400 || *tail!=' ' || !chat_text_valid(tail+1))goto invalid;strcpy(q.body,tail+1);
    } else return 0;
    session_impl_submit(s,&q);
    if(s->pending && (q.action=='j' || q.action=='U' || q.action=='u')) {
        report_navigation_begin(&s->report,q.action=='j'?REPORT_BOARDLOG:q.action=='u'?REPORT_USERTABLE:REPORT_USERLOG,
                                q.number?q.number:s->user,q.notes_unread);
    }
    return 1;
invalid:session_impl_emit(s,"ERR command\r\n");return 1;
}

void session_impl_gather_input(Session *s,const char *line) {
    GatherInput input=!strcmp(line,"/CANCEL")?GATHER_INPUT_CANCEL:
        !strcmp(line,"//")?GATHER_INPUT_FINISH:!strcmp(line,"Q")?GATHER_INPUT_QUIT:
        (!*line || !strcmp(line,"+"))?GATHER_INPUT_NEXT:GATHER_INPUT_TEXT;
    GatherEffect effect=gather_flow_input(&s->gather,input,line,gather_line_valid(line),s->draft.body,sizeof(s->draft.body));
    switch(effect) {
    case GATHER_ABORTED:memset(&s->draft,0,sizeof(s->draft));session_impl_emit(s,"CANCELLED\r\n");break;
    case GATHER_EMPTY:session_impl_emit(s,"CANCELLED\r\n");break;
    case GATHER_EDIT:memset(&s->draft,0,sizeof(s->draft));session_impl_queue_text(s,ui_text(&s->ui,MSG_GATHER_EDIT));break;
    case GATHER_INVALID:session_impl_queue_text(s,ui_text(&s->ui,MSG_GATHER_INVALID));break;
    case GATHER_NEXT_PAGE: {
        BoardRequest q={0};q.op='!';q.action='G';q.cursor=s->gather.cursor;session_impl_submit(s,&q);
        gather_flow_read_submitted(&s->gather,s->gather.mode==2,s->pending);break;
    }
    case GATHER_POST:
        s->draft.op='!';s->draft.action='g';s->draft.scope=s->ui.enabled?(int)s->ui.language+1:0;
        session_impl_submit(s,&s->draft);gather_flow_post_submitted(&s->gather,s->pending);break;
    default:break;
    }
}

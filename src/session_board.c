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

int session_impl_board_options(Session *s,char *line) {
    if(!strncmp(line,"BOARDOPTIONS",12) || !strncmp(line,"BOARDPASS ",10) || !strncmp(line,"BOARDACCESS",11) || !strncmp(line,"INFOEDIT",8) || !strncmp(line,"BOARDDELETE ",12) || !strncmp(line,"MOVE ",5) || !strcmp(line,"GET") || !strncmp(line,"GET ",4)) {
        BoardRequest q={0};q.op='(';const char *tail=NULL,*arg=NULL;
        if(!strncmp(line,"BOARDOPTIONS",12) && (!line[12] || line[12]==' ')) { q.action='O';arg=line+12; }
        else if(!strncmp(line,"BOARDACCESS",11) && (!line[11] || line[11]==' ')) { q.action='A';arg=line+11; }
        else if(!strncmp(line,"INFOEDIT",8) && (!line[8] || line[8]==' ')) { q.action='I';arg=line+8; }
        else if(!strncmp(line,"BOARDPASS ",10)) { q.action='P';arg=line+9; }
        else if(!strncmp(line,"BOARDDELETE ",12)) { q.action='D';arg=line+11; }
        else if(!strncmp(line,"MOVE ",5)) { q.action='M';arg=line+4; }
        else if(!strncmp(line,"GET",3) && (!line[3] || line[3]==' ')) { q.action='G';arg=line+3; }
        if(!arg)goto board_syntax;
        q.number=q.action=='G'?s->core.current:s->core.board;
        if(*arg) { if(!session_impl_number_arg(arg+1,&q.number,&tail))goto board_syntax; }else tail=arg;
        if(q.action=='O' && *tail==' ') { if(strlen(tail+1)>120)goto board_syntax;strcpy(q.body,tail+1);for(char *p=q.body;*p && *p!=' ';p++)if(*p>='a' && *p<='z')*p-=32;tail=""; }
        else if(q.action=='A' && *tail==' ') { if(!session_impl_number_arg(tail+1,&q.cursor,&tail))goto board_syntax; }
        else if(q.action=='P' && !strcmp(tail," NONE CONFIRM")) { q.action='C';tail=""; }
        else if(q.action=='D') { if(strcmp(tail," CONFIRM"))goto board_syntax;
            tail=""; }
        else if(q.action=='M') {
            q.cursor=q.number;q.number=s->core.board;
            if(*tail!=' ' || !session_impl_number_arg(tail+1,&q.audit_article,&tail) || strcmp(tail," CONFIRM"))goto board_syntax;
            tail="";
        }
        if(*tail)goto board_syntax;
        if(q.action=='P' && s->core.now<s->core.auth_after) { session_impl_emit(s,"ERR try later\r\n");return 1; }
        session_impl_submit(s,&q);
        if(q.action=='A' && s->core.pending) { report_navigation_begin(&s->reading.report,REPORT_BOARDACCESS,q.number,0); }
        return 1;
board_syntax:session_impl_emit(s,"ERR command\r\n");return 1;
    }

    return 0;
}

int session_impl_board_structure_command(Session *s,char *line) {
    if(!strncmp(line,"MOVEBRANCH ",11)) {
        BoardRequest q={0};const char *p;q.op='(';q.action='B';q.number=s->core.board;
        if(!session_impl_number_arg(line+11,&q.cursor,&p) || *p++!=' ' || !session_impl_number_arg(p,&q.audit_article,&p) || strcmp(p," CONFIRM")) { session_impl_emit(s,"ERR command\r\n");return 1; }
        session_impl_submit(s,&q);return 1;
    }
    if(!strncmp(line,"BOARDDELETEALL ",15) && strstr(line," CASCADE CONFIRM")) {
        BoardRequest q={0};const char *p;q.op='}';q.action='D';
        if(!session_impl_number_arg(line+15,&q.number,&p) || strcmp(p," CASCADE CONFIRM")) { session_impl_emit(s,"ERR command\r\n");return 1; }
        session_impl_submit(s,&q);return 1;
    }
    if(!strncmp(line,"AUTHOR ",7) || !strncmp(line,"BOARDGUEST ",11)) {
        BoardRequest q={0};q.op='}';q.action=line[0]=='A'?'o':'p';const char *tail;
        if(!session_impl_number_arg(line+(q.action=='o'?7:11),&q.number,&tail) || *tail++!=' ') { session_impl_emit(s,"ERR command\r\n");return 1; }
        if(q.action=='o') { const char *end;if(!session_impl_number_arg(tail,&q.cursor,&end) || strcmp(end," CONFIRM")) { session_impl_emit(s,"ERR command\r\n");return 1; } }
        else { if(strcmp(tail,"ON") && strcmp(tail,"OFF")) { session_impl_emit(s,"ERR command\r\n");return 1; }q.cursor=!strcmp(tail,"ON"); }
        session_impl_submit(s,&q);return 1;
    }

    return 0;
}

int session_impl_board_unlock_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='1') { s->auth.board_enter=completion->service_number;s->auth.board_lock=completion->board_grant;s->auth.password_mode='b'; }
    if(completion->service_action=='2') {
        s->core.auth_after=now; /* A verified ancestor may immediately lead to the next password prompt. */
        if(completion->board_grant>0 && completion->board_grant<=CATALOG_MAX)s->core.board_grants[completion->board_grant]=completion->board_revision;
        BoardRequest q={0};q.op='j';q.number=completion->service_number;session_impl_submit(s,&q);return 1;
    }

    return 0;
}

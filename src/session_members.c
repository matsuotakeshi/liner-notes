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

int session_impl_member_rights_command(Session *s,char *line) {
    if(!strncmp(line,"REJECT ",7) || !strncmp(line,"DELETEAPPLICATION ",18) || !strncmp(line,"DELETEUSER ",11) || !strncmp(line,"RETIRE",6)) {
        BoardRequest q={0};q.op=BOARD_MEMBER_LIFECYCLE;
        if(!strcmp(line,"RETIRE CONFIRM"))q.action=3;
        else { const char *tail;const char *arg=strchr(line,' ');q.action=!strncmp(line,"REJECT ",7)?1:!strncmp(line,"DELETEAPPLICATION ",18)?2:4;
            if(!arg || !strncmp(line,"RETIRE",6) || !session_impl_number_arg(arg+1,&q.number,&tail) || q.number>MEMBER_MAX || strcmp(tail," CONFIRM")) { session_impl_emit(s,"ERR command\r\n");return 1; }
        }
        session_impl_submit(s,&q);return 1;
    }
    if(!strcmp(line,"RIGHTS") || !strncmp(line,"RIGHTS ",7) || !strncmp(line,"LEVEL ",6) || !strncmp(line,"GROUP ",6) || !strncmp(line,"UNGROUP ",8) || !strncmp(line,"APPROVE ",8) || !strcmp(line,"APPLICATIONS") || !strncmp(line,"APPLICATIONS ",13)) {
        BoardRequest q={0};q.op='%';const char *tail="";
        if(!strncmp(line,"APPLICATIONS",12)) { q.action='P';if(line[12] && (!session_impl_number_arg(line+13,&q.cursor,&tail) || *tail || q.cursor>MEMBER_MAX))goto rights_error; }
        else {
            const char *arg=strchr(line,' ');
            if(arg && (!session_impl_number_arg(arg+1,&q.number,&tail) || q.number>MEMBER_MAX))goto rights_error;
            if(!strncmp(line,"LEVEL ",6))q.action='L';
            else if(!strncmp(line,"GROUP ",6))q.action='G';
            else if(!strncmp(line,"UNGROUP ",8))q.action='D';
            else if(!strncmp(line,"APPROVE ",8))q.action='A';
            if(q.action && q.action!='A') {
                if(*tail!=' ' || (!session_impl_audit_number(tail+1,&q.cursor) && !(q.action=='L' && !strcmp(tail+1,"0"))) || q.cursor>(q.action=='L'?5u:100u) || (q.action!='L' && !q.cursor))goto rights_error;
            } else if(*tail)goto rights_error;
        }
        session_impl_submit(s,&q);return 1;
    rights_error:session_impl_emit(s,"ERR rights syntax\r\n");return 1;
    }

    return 0;
}

int session_impl_member_directory_command(Session *s,char *line) {
    if(!strcmp(line,"MEMBERS") || !strncmp(line,"MEMBERS ",8) || !strncmp(line,"MEMBER ",7) ||
       !strncmp(line,"SUSPEND ",8) || !strncmp(line,"RESUME ",7)) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        BoardRequest q={0}; q.op=!strncmp(line,"MEMBERS",7)?'l':!strncmp(line,"MEMBER ",7)?'t':line[0]=='S'?'x':'y';
        q.number=q.op=='l'?1:0;
        const char *arg=strchr(line,' ');
        if(arg) { const char *tail; if(!session_impl_number_arg(arg+1,&q.number,&tail) || *tail || q.number>MEMBER_MAX) { session_impl_emit(s,"ERR member number\r\n"); return 1; } }
        session_impl_submit(s,&q); return 1;
    }
    if(!strcmp(line,"ADMIN") || !strcmp(line,"HIDE") || !strncmp(line,"HIDE ",5) ||
       !strcmp(line,"RESTORE") || !strncmp(line,"RESTORE ",8) || !strcmp(line,"INSPECT") || !strncmp(line,"INSPECT ",8)) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        BoardRequest q={0}; q.op=!strcmp(line,"ADMIN")?'z':line[0]=='H'?'h':line[0]=='R'?'r':'i';
        q.number=s->core.current;
        const char *arg=strchr(line,' ');
        if(arg) { const char *tail; if(!session_impl_number_arg(arg+1,&q.number,&tail)||*tail) { session_impl_emit(s,"ERR article number\r\n"); return 1; } }
        session_impl_submit(s,&q); return 1;
    }

    return 0;
}

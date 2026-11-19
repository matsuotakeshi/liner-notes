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

int session_impl_vault_command(Session *s,char *line) {
    if(!strcmp(line,"FILES") || !strncmp(line,"FILES ",6) || !strncmp(line,"FILE ",5) ||
       !strncmp(line,"FILEDESC ",9) || !strncmp(line,"FILERESTORE ",12) || !strncmp(line,"FILENEWS ",9) || !strncmp(line,"FILENEWSFROM ",13) ||
       !strncmp(line,"FILEDELETE ",11) || !strncmp(line,"FILEHIDE ",9) || !strncmp(line,"FILESEARCH ",11) ||
       !strncmp(line,"FILESEARCHFROM ",15) || !strncmp(line,"FILESIN ",8) || !strncmp(line,"FILESINFROM ",12) ||
       !strncmp(line,"FILECATEGORY ",13) || !strcmp(line,"FILECATEGORIES") || !strncmp(line,"FILECATEGORIES ",15) ||
       !strcmp(line,"FILEBOARDS") || !strncmp(line,"FILEBOARDS ",11) || !strcmp(line,"FILEAUDIT") || !strncmp(line,"FILEAUDIT ",10)) {
        if(!s->board_enabled) { session_impl_emit(s,"ERR no board\r\n");return 1; }
        BoardRequest q={0};q.op='Y';const char *tail=NULL,*argument=NULL;
        if(!strncmp(line,"FILEDELETE ",11)) {
            q.action=VAULT_CMD_DELETE;if(!session_impl_number_arg(line+11,&q.number,&tail) || strcmp(tail," CONFIRM"))goto vault_syntax;
        } else if(!strncmp(line,"FILEDESC ",9)) {
            q.action=VAULT_CMD_DESCRIPTION;
            if(!session_impl_number_arg(line+9,&q.number,&tail) || (*tail && *tail!=' '))goto vault_syntax;
            const char *text=*tail?tail+1:"";
            if(strlen(text)>240 || (*text && !session_impl_safe_text(text)))goto vault_syntax;
            strcpy(q.body,text);
        } else if(!strncmp(line,"FILERESTORE ",12)) {
            q.action=VAULT_CMD_RESTORE;
            if(!session_impl_number_arg(line+12,&q.number,&tail) || strcmp(tail," CONFIRM"))goto vault_syntax;
        } else if(!strncmp(line,"FILENEWS",8)) {
            q.action=VAULT_CMD_NEWS;
            if(!strncmp(line+8,"FROM ",5)) { if(!session_impl_number_arg(line+13,&q.number,&tail) || *tail!=' ')goto vault_syntax;argument=tail+1; }
            else argument=line+9;
        } else if(!strncmp(line,"FILESEARCH",10) || !strncmp(line,"FILESIN",7)) {
            int search=!strncmp(line,"FILESEARCH",10),length=search?10:7;q.action=search?VAULT_CMD_SEARCH:VAULT_CMD_IN;
            if(!strncmp(line+length,"FROM ",5)) {
                if(!session_impl_number_arg(line+length+5,&q.number,&tail) || *tail!=' ')goto vault_syntax;
                argument=tail+1;
            } else argument=line+length+1;
        } else if(!strncmp(line,"FILECATEGORY ",13)) {
            q.action=VAULT_CMD_CATEGORY;
            if(!session_impl_number_arg(line+13,&q.number,&tail) || *tail!=' ')goto vault_syntax;
            argument=tail+1;
        } else if(!strncmp(line,"FILEBOARDS",10)) {
            q.action=VAULT_CMD_BOARDS;if(line[10] && (!session_impl_number_arg(line+11,&q.number,&tail) || *tail))goto vault_syntax;
        } else if(!strncmp(line,"FILECATEGORIES",14) || !strncmp(line,"FILEAUDIT",9) || !strncmp(line,"FILES",5)) {
            int length=!strncmp(line,"FILECATEGORIES",14)?14:!strncmp(line,"FILEAUDIT",9)?9:5;
            q.action=length==14?VAULT_CMD_CATEGORIES:length==9?VAULT_CMD_AUDIT:VAULT_CMD_LIST;
            if(line[length] && (!session_impl_number_arg(line+length+1,&q.number,&tail) || *tail))goto vault_syntax;
        } else if(!strncmp(line,"FILEHIDE ",9)) {
            q.action=VAULT_CMD_HIDE;
            if(!session_impl_number_arg(line+9,&q.number,&tail) || strcmp(tail," CONFIRM"))goto vault_syntax;
        } else {
            q.action=VAULT_CMD_INFO;
            if(!session_impl_number_arg(line+5,&q.number,&tail) || *tail)goto vault_syntax;
        }
        if(q.number>(q.action==VAULT_CMD_AUDIT?VAULT_ID_MAX:VAULT_ID_MAX))goto vault_syntax;
        if(argument) {
            if(!*argument || strlen(argument)>((q.action==VAULT_CMD_SEARCH || q.action==VAULT_CMD_NEWS)?129u:48u) || !session_impl_safe_text(argument))goto vault_syntax;
            if(q.action==VAULT_CMD_SEARCH || q.action==VAULT_CMD_NEWS)strcpy(q.body,argument);else strcpy(q.title,argument);
        }
        session_impl_submit(s,&q);return 1;
    vault_syntax:
        session_impl_emit(s,"ERR vault syntax\r\n");return 1;
    }

    return 0;
}

int session_impl_vault_children_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(!strncmp(completion->text,"FILECHILDREN ",13)) {
        vault_children_response(&s->vault,completion->text);
        session_impl_queue_text(s,completion->text);session_impl_prompt(s);return 1;
    }

    return 0;
}

int session_impl_batch_copy_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(s->batch_copying) {
        session_impl_queue_text(s,completion->text);
        if(strncmp(completion->text,"FILE COPIED ",12)) { s->batch_copying=0;session_impl_prompt(s);return 1; }
        uint32_t next=batch_copy_complete(&s->batch);
        if(next) { BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_COPY;q.number=next;strcpy(q.body,s->batch.copy_path);session_impl_submit(s,&q); }
        else { s->batch_copying=0;session_impl_prompt(s); }return 1;
    }

    return 0;
}

int session_impl_vault_edit_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(!strncmp(completion->text,"BATCHMATCH",10)) {
        BatchIds selected=batch_response_ids(completion->text+10);batch_replace(&s->batch,&selected);
        char ids[192],text[384];if(!batch_format_ids(&selected,ids,sizeof(ids))) { session_abort(s);return 1; }
        snprintf(text,sizeof(text),s->ui.enabled?ui_text(&s->ui,MSG_BATCH_LIST):"BATCH %u %s\r\n",s->batch.count,*ids?ids:"-");session_impl_queue_text(s,text);session_impl_prompt(s);return 1;
    }
    if(!strncmp(completion->text,"FILEBOARD EDIT ",15)) { const char *body=strstr(completion->text,"\r\n");if(body) { memset(&s->draft,0,sizeof(s->draft));s->draft.op='Y';s->draft.action=VAULT_CMD_BOARDGUIDE;size_t n=(size_t)(body-(completion->text+15));if(n<=48) { memcpy(s->draft.title,completion->text+15,n);snprintf(s->draft.body,sizeof(s->draft.body),"%s",body+2);s->editing=1;s->session_impl_editor_input=0;s->editor_at=1;session_impl_undo_clear(s);session_impl_queue_text(s,s->ui.language==LANGUAGE_JA?"ファイルボード案内文を編集します。/SAVE で保存。\r\n":"Edit file-board guide. /SAVE to save.\r\n");session_impl_prompt(s);return 1; } } }
    if(s->editing && s->draft.op=='Y' && !strcmp(completion->text,"FILEBOARD UPDATED\r\n")) { s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0;memset(&s->draft,0,sizeof(s->draft)); }

    return 0;
}

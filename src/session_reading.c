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

int session_impl_search_command(Session *s,char *line) {
    if(!strcmp(line,"SEARCH") || !strncmp(line,"SEARCH ",7) || !strcmp(line,"SEARCHFROM") || !strncmp(line,"SEARCHFROM ",11)) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        BoardRequest q={0}; q.op='f'; const char *query="";
        if(!strncmp(line,"SEARCHFROM",10)) {
            const char *tail;
            if(!line[10] || !session_impl_number_arg(line+11,&q.cursor,&tail) || *tail!=' ') { session_impl_emit(s,"ERR search cursor\r\n"); return 1; }
            query=tail+1;
        } else if(line[6]) query=line+7;
        if(!*query || strlen(query)>80 || !session_impl_safe_text(query)) { session_impl_emit(s,"ERR search query\r\n"); return 1; }
        strcpy(q.title,query); session_impl_submit(s,&q); return 1;
    }

    return 0;
}

int session_impl_audit_request(Session *s,char *line) {
    if(!strcmp(line,"AUDIT") || !strncmp(line,"AUDIT ",6)) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        BoardRequest q={0}; q.op='A';
        if(line[5] && !session_impl_audit_arguments(line+6,&q)) { session_impl_emit(s,"ERR audit cursor\r\n"); return 1; }
        session_impl_submit(s,&q); return 1;
    }

    return 0;
}

int session_impl_article_command(Session *s,char *line) {
    if(!strcmp(line,"NEXTBOARD")) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n"); return 1; }
        BoardRequest q={0}; q.op='J'; session_impl_submit(s,&q);
    } else if(!strcmp(line,"BOARDS") || !strncmp(line,"BOARDS ",7) || !strncmp(line,"BOARD ",6)) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        BoardRequest q={0}; q.op=!strncmp(line,"BOARDS",6)?'b':'j'; q.number=1;
        const char *arg=q.op=='b'?(line[6]?line+7:NULL):line+6;
        if(arg) { const char *tail; if(!session_impl_number_arg(arg,&q.number,&tail)||*tail) { session_impl_emit(s,"ERR board number\r\n"); return 1; } }
        session_impl_submit(s,&q);
    } else if(!strcmp(line,"SEEN") || !strncmp(line,"SEEN ",5) || !strcmp(line,"UNSEEN") || !strncmp(line,"UNSEEN ",7) ||
              !strcmp(line,"UNREAD") || !strncmp(line,"UNREAD ",7) || !strcmp(line,"NEW") || !strncmp(line,"NEW ",4)) {
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n"); return 1; }
        BoardRequest q={0}; const char *arg=NULL;
        if(!strncmp(line,"UNREAD",6)) { q.op='v'; if(line[6]) arg=line+7; }
        else if(!strncmp(line,"NEW",3)) { q.op='n'; if(line[3]) arg=line+4; }
        else if(!strncmp(line,"UNSEEN",6)) { q.op='u'; q.number=s->core.current; if(line[6]) arg=line+7; }
        else { q.op='s'; q.number=s->core.current; if(line[4]) arg=line+5; }
        if(arg) {
            const char *tail;
            if(q.op=='v' && arg[0]=='0' && (!arg[1] || arg[1]==' ')) { q.number=0; tail=arg+1; }
            else if(!session_impl_number_arg(arg,&q.number,&tail)) { session_impl_emit(s,"ERR article number\r\n"); return 1; }
            if(*tail && (q.op!='v' || *tail!=' ' || !session_impl_number_arg(tail+1,&q.cursor,&tail) || *tail)) { session_impl_emit(s,"ERR article number\r\n"); return 1; }
        }
        session_impl_submit(s,&q);
    } else if(!strcmp(line,"HELP") || !strncmp(line,"HELP ",5)) { session_impl_show_help(s,line[4]?line+5:""); }
    else if(!strcmp(line,"PARENT") || !strcmp(line,"CHILD") || !strcmp(line,"NEXT") || !strcmp(line,"PREV") ||
            !strcmp(line,"NOTES") || !strncmp(line,"NOTES ",6) || !strcmp(line,"TREE") || !strncmp(line,"TREE ",5)) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        BoardRequest q={0}; q.number=s->core.current;
        if(!strncmp(line,"NOTES",5)) {
            q.op='O'; q.number=1;
            const char *arg=line[5]?line+6:NULL;
            if(arg && (!strcmp(arg,"UNREAD") || !strncmp(arg,"UNREAD ",7))) {
                if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n"); return 1; }
                q.notes_unread=1; arg=arg[6]?arg+7:NULL;
            }
            if(arg) { const char *tail; if(!session_impl_number_arg(arg,&q.number,&tail) || *tail) { session_impl_emit(s,"ERR article number\r\n"); return 1; } }
        } else if(!strncmp(line,"TREE",4)) {
            q.op='T';
            if(line[4]) {
                const char *tail;
                if(!session_impl_number_arg(line+5,&q.number,&tail)) { session_impl_emit(s,"ERR tree syntax\r\n"); return 1; }
                if(*tail && (*tail!=' ' || !session_impl_number_arg(tail+1,&q.cursor,&tail) || *tail)) { session_impl_emit(s,"ERR tree syntax\r\n"); return 1; }
            }
        } else q.op=!strcmp(line,"PARENT")?'U':!strcmp(line,"CHILD")?'C':!strcmp(line,"NEXT")?'N':'B';
        session_impl_submit(s,&q);
    }
    else if(!strncmp(line,"POST ",5) || !strncmp(line,"REPLY ",6) || !strncmp(line,"RESPOND ",8)) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        memset(&s->editor.draft,0,sizeof(s->editor.draft)); s->editor.draft_revision=0; session_impl_undo_clear(s);
        const char *title=line+5;
        if(!strncmp(line,"RESPOND ",8)) {
            if(!s->core.current) { session_impl_emit(s,"ERR no current article\r\n"); return 1; }
            s->editor.draft.number=s->core.current; title=line+8;
        }
        if(!strncmp(line,"REPLY ",6)) {
            if(!session_impl_number_arg(line+6,&s->editor.draft.number,&title) || *title!=' ') { session_impl_emit(s,"ERR reply syntax\r\n"); return 1; }
            title++;
        }
        if(!*title || strlen(title)>BOARD_TITLE || !session_impl_safe_text(title)) { session_impl_emit(s,"ERR title 1..80 bytes, no controls\r\n"); return 1; }
        strcpy(s->editor.draft.title,title); s->editor.draft.op='e'; session_impl_submit(s,&s->editor.draft);
    } else if(!strcmp(line,"LIST") || !strncmp(line,"LIST ",5) || !strncmp(line,"READ ",5)) {
        if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
        BoardRequest q={0}; q.op=line[0]=='L'?'L':'R'; q.number=1;
        if(strcmp(line,"LIST")) {
            const char *tail;
            if(!session_impl_number_arg(line+5,&q.number,&tail) || *tail) { session_impl_emit(s,"ERR article number\r\n"); return 1; }
        }
        session_impl_submit(s,&q);
    }
    else return 0;
    return 1;
}

int session_impl_traversal_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='n') { check_traversal_begin(&s->reading.check,completion->service_number,completion->since,completion->until);session_impl_check_page(s);return 1; }
    if(s->reading.check.committing) { check_traversal_committed(&s->reading.check,completion->feedback.checkpoint_saved);session_impl_emit(s,completion->text);session_impl_prompt(s);return 1; }
    if(s->reading.check.pending) { check_traversal_page(&s->reading.check,completion->feedback.page_found,completion->feedback.page_next);session_impl_emit(s,completion->text);session_impl_queue_text(s,s->reading.check.automatic?"CHECK NEXT / CHECK CANCEL (AUTO)\r\n":s->presentation.ui.language==LANGUAGE_JA?"CHECK NEXT: 続き / CHECK DONE: 確認日時を保存 / CHECK CANCEL: 中断\r\n":"CHECK NEXT / CHECK DONE / CHECK CANCEL\r\n");session_impl_prompt(s);return 1; }

    return 0;
}

int session_impl_profile_walk_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(s->reading.profile_walk.pending) {
        ListContinuation page={completion->feedback.page_found,completion->feedback.page_next};
        profile_walk_complete(&s->reading.profile_walk,page.next);
        session_impl_queue_text(s,completion->text);
        if(page.next) { s->presentation.classic_input=CI_PROF_NEXT;session_impl_queue_text(s,s->presentation.ui.language==LANGUAGE_JA?"次の会員:Enter / 0:終了 > ":"Next:Enter / 0:stop > "); }
        else session_impl_prompt(s);
        return 1;
    }

    return 0;
}

int session_impl_report_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(s->reading.report.pending) {
        ListContinuation page={completion->feedback.page_found,completion->feedback.page_next};
        report_navigation_complete(&s->reading.report,page.found,page.next);
        session_impl_emit(s,completion->text);session_impl_prompt(s);return 1;
    }

    return 0;
}

int session_impl_article_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->board) { s->core.board=completion->board; s->core.current=0; if(completion->direct) s->presentation.ui.board_title[0]=0; }
    if(completion->draft_loaded) {
        memset(&s->editor.draft,0,sizeof(s->editor.draft)); session_impl_undo_clear(s);
        s->editor.draft.number=completion->draft_parent; strcpy(s->editor.draft.title,completion->draft_title); strcpy(s->editor.draft.body,completion->draft_body);
        s->presentation.classic_area=CM_MAIN;s->presentation.ui.next=0; s->presentation.ui.view=UI_HOME; s->presentation.ui.board_title[0]=0;
    }
    if(completion->draft_revision) s->editor.draft_revision=completion->draft_revision;
    if(completion->editing) { s->editor.editing=1;s->editor.editor_input=0;s->editor.editor_at=1; }
    if(s->files.file_selecting) { if(completion->feedback.file_detail) { s->files.file_selected=s->files.file_selecting;s->presentation.classic_input=CI_FILE_ACTION; }s->files.file_selecting=0; }
    if(completion->selected) { s->core.current=completion->selected;if(!s->mail_state.mail_mode && !s->files.vault_mode)s->presentation.classic_area=CM_BOARD; }
    reader_complete(&s->reading.reader,completion->sequence,completion->selected || completion->mail_id,completion->feedback.failed);
    if(completion->sequence)s->reading.scan.cursor=completion->sequence_next;
    session_impl_emit(s,completion->text);
    if(s->files.vault_mode && completion->feedback.file_board_info && completion->feedback.file_menu) { BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_CHILDREN;strcpy(q.body,vault_navigation_path(&s->files.vault));session_impl_submit(s,&q);return 1; }
    if(s->presentation.ui.enabled && s->core.active) {
        if(completion->auth>0) {
            char notice[512];
            snprintf(notice,sizeof(notice),ui_text(&s->presentation.ui,MSG_LOGIN_UNREAD),
                     completion->unread_articles_notice,completion->unread_boards_notice);
            session_impl_queue_text(s,notice);
            if(board_feature_enabled("mail")) { snprintf(notice,sizeof(notice),ui_text(&s->presentation.ui,MSG_MAIL_NOTICE),completion->mail_notice);session_impl_queue_text(s,notice); }
        }
        if(completion->auth>0 && completion->mail_draft_notice) {
            char notice[512];
            snprintf(notice,sizeof(notice),ui_text(&s->presentation.ui,MSG_MAIL_DRAFT_NOTICE),completion->mail_draft_notice);
            session_impl_queue_text(s,notice);
        }
        if(completion->auth>0 && completion->saved_draft_notice) {
            char notice[512];
            snprintf(notice,sizeof(notice),ui_text(&s->presentation.ui,MSG_DRAFT_NOTICE),completion->saved_draft_notice);
            session_impl_queue_text(s,notice);
        }
        int next=s->presentation.ui.next_after_seen; s->presentation.ui.next_after_seen=0;
        if(next && completion->feedback.seen) { char cmd[]="NEW"; session_impl_command_core(s,cmd); }
        else if(completion->board && !completion->draft_loaded && !completion->direct) { if(completion->service_action=='7') { BoardRequest q={0};q.op='b';q.action='C';q.number=completion->board;session_impl_submit(s,&q); }else { char cmd[]="NOTES"; session_impl_command_core(s,cmd); } }
        session_impl_prompt(s);
    }

    return 0;
}

void session_impl_check_page(Session *s) {
    BoardRequest q={0};q.since=s->reading.check.since;q.until=s->reading.check.until;q.cursor=s->reading.check.cursor;
    if(s->reading.check.active==1) { q.op='Z';q.action=11;q.scope=s->reading.check.scope;strcpy(q.title,"CHECK"); }
    else if(s->reading.check.active==2) { q.op='m';q.action='L';q.number=5;strcpy(q.title,"CHECK"); }
    else { q.op='Y';q.action=VAULT_CMD_NEWS;q.number=s->reading.check.cursor;snprintf(q.body,sizeof(q.body),"%s%s@%llu:%llu",*s->reading.check.key?s->reading.check.key:"",*s->reading.check.key?"|":"",(unsigned long long)q.since,(unsigned long long)q.until); }
    session_impl_submit(s,&q);s->reading.check.pending=s->core.pending;
}

void session_impl_profile_continue(Session *s) {
    ProfileWalkQuery plan=profile_walk_next(&s->reading.profile_walk);
    BoardRequest q={0};q.op='Z';q.action=10;q.notes_unread=1;
    q.scope=plan.field;q.number=plan.group;q.cursor=plan.cursor;strcpy(q.title,plan.query);
    session_impl_submit(s,&q);
}

int session_auto_read(const Session *s) { return s->reading.reader.automatic; }

void session_scan_pause(Session *s) { s->reading.reader.active=s->reading.reader.automatic=0; }

void session_board_refresh(Session *s) {
    s->presentation.page[0]=0;s->presentation.page_offset=0;s->reading.reader.active=0;
    s->presentation.ui.next=0;s->presentation.ui.view=UI_HOME;s->presentation.ui.board_title[0]=0;
    report_navigation_clear(&s->reading.report);
}

void session_access_refresh(Session *s) {
    if(report_navigation_access_refresh(&s->reading.report)) { s->presentation.page[0]=0;s->presentation.page_offset=0; }
}

void session_scan_stop(Session *s) {
    reader_stop(&s->reading.reader,s->core.pending);session_impl_page_clear(s);
    if(!s->core.pending) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_SCAN_STOP));session_impl_prompt(s); }
}

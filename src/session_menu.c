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

int session_impl_door_command(Session *s,char *line) {
    if(!strcmp(line,"GAME") || !strcmp(line,"GAMES") || !strncmp(line,"DOOR ",5)) {
        BoardRequest q={0};q.op='}';q.action='J';const char *end;
        if(line[0]=='D' && (!session_impl_number_arg(line+5,&q.number,&end) || *end || !s->connection.binary_allowed)) { session_impl_emit(s,"ERR command\r\n");return 1; }
        session_impl_submit(s,&q);return 1;
    }

    return 0;
}

void session_impl_command(Session *s,char *line) {
    if(s->conversation.chatting) { session_impl_chat_input(s,line);return; }
    char mapped[INPUT_LINE_MAX+128]; const char *action=NULL;
    if(s->presentation.page[0]) {
        if(s->reading.reader.active && (!strcmp(line,"F") || !strcmp(line,"f"))) { session_impl_queue_text(s,s->presentation.page+s->presentation.page_offset);session_impl_page_clear(s);s->reading.reader.automatic=1;return; }
        if(!*line) session_impl_page_next(s);
        else if(!strcmp(line,"Q") || !strcmp(line,"q") || !strcmp(line,"/CANCEL")) { int help=s->presentation.help_page; s->reading.reader.active=0;if(!help)s->reading.check.active=0; session_impl_page_clear(s); if(help) session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_HELP_CLOSED)); else session_impl_emit(s,s->editor.editing?"PREVIEW CLOSED\r\n":"CANCELLED\r\n"); }
        else if(!strcmp(line,"PAGER OFF") || !strcmp(line,"OFF")) { s->presentation.pager_enabled=0; session_impl_queue_text(s,s->presentation.page+s->presentation.page_offset); session_impl_page_clear(s); session_impl_emit(s,"PAGER OFF\r\n"); }
        else session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_PAGER_MORE));
        session_impl_prompt(s); return;
    }
    if(s->conversation.gather.mode) { session_impl_gather_input(s,line);session_impl_prompt(s);return; }
    if(s->reading.reader.active) {
        if(!strcmp(line,"P") || !strcmp(line,"L")) {
            s->reading.reader.automatic=0;
            ReaderMove move=*line=='P'?READER_PREVIOUS:READER_REPEAT;
            ReaderPosition next=s->reading.scan.op=='m'?mail_navigation_move(&s->mail_state.mail,move,s->reading.scan.descending,1):reader_move(move,s->core.current,s->reading.scan.descending,1);
            s->reading.scan.cursor=next.cursor;s->reading.scan.descending=next.descending;
            if(s->reading.scan.cursor)session_impl_submit(s,&s->reading.scan);else session_impl_emit(s,"SCAN END\r\n");
            return;
        }
        if(!strcmp(line,"F")) { s->reading.reader.automatic=1;return; }
        if(s->reading.reader.automatic) { s->reading.reader.automatic=0;session_impl_page_clear(s);if(!*line || !strcmp(line,"STOP")) { s->reading.reader.active=0;session_impl_prompt(s);return; } }
        if(*line=='+' || *line=='-') {
            uint32_t distance=1;const char *tail;
            if(line[1] && (!session_impl_number_arg(line+1,&distance,&tail) || *tail || !distance)) { session_impl_emit(s,"ERR relative distance\r\n");session_impl_prompt(s);return; }
            ReaderMove move=*line=='-'?READER_BACKWARD:READER_FORWARD;
            ReaderPosition next=s->reading.scan.op=='m'?mail_navigation_move(&s->mail_state.mail,move,s->reading.scan.descending,distance):reader_move(move,s->core.current,s->reading.scan.descending,distance);
            s->reading.scan.cursor=next.cursor;s->reading.scan.descending=next.descending;
            if(s->reading.scan.cursor)session_impl_submit(s,&s->reading.scan);else { s->reading.reader.active=0;session_impl_emit(s,s->reading.scan.op=='m'?"MAIL SCAN END\r\n":"SCAN END\r\n");session_impl_prompt(s); }return;
        }
        if(!*line || !strcmp(line,"CONTINUE")) { if(s->reading.scan.cursor)session_impl_submit(s,&s->reading.scan);else { s->reading.reader.active=0;session_impl_emit(s,s->reading.scan.op=='m'?"MAIL SCAN END\r\n":"SCAN END\r\n");session_impl_prompt(s); }return; }
        s->reading.reader.active=0;
        if(!strcmp(line,"0") || !strcmp(line,"Q") || !strcmp(line,"q") || !strcmp(line,"STOP") || !strcmp(line,"/CANCEL")) {
            session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_SCAN_STOP)); session_impl_prompt(s); return;
        }
    }
    if(s->auth.password_mode || s->editor.editing || s->auth.application.step) { session_impl_command_core(s,line); session_impl_prompt(s); return; }
    if(!s->presentation.ui.input && !s->presentation.classic_input && !s->mail_state.mail_input && !s->mail_state.mail.delete_id && (!strcmp(line,"LANG") || !strncmp(line,"LANG ",5))) {
        if(line[4] && !language_parse(line+5,&s->presentation.ui.language)) session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_ERR_LANGUAGE));
        else {
            snprintf(mapped,sizeof(mapped),ui_text(&s->presentation.ui,MSG_LANGUAGE),language_code(s->presentation.ui.language));
            session_impl_queue_text(s,mapped);
        }
        session_impl_prompt(s); return;
    }
    if(!s->presentation.ui.input && !s->presentation.classic_input && !s->mail_state.mail_input && !s->mail_state.mail.delete_id && s->presentation.ui.enabled && !*s->connection.connector_token &&
       (!strcmp(line,"GAME") || !strcmp(line,"GAMES"))) {
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n");session_impl_prompt(s);return; }
        s->presentation.game_menu=1;s->files.vault_mode=s->mail_state.mail_mode=s->conversation.gather.mode=0;s->presentation.classic_area=CM_MAIN;
        session_impl_command_core(s,"GAMES");session_impl_prompt(s);return;
    }
    if(s->presentation.game_menu) {
        if(!strcmp(line,"0") || !strcmp(line,"EXIT") || !strcmp(line,"MENU")) { s->presentation.game_menu=0;s->presentation.ui.view=UI_HOME;s->presentation.classic_area=CM_MAIN;session_impl_show_menu(s,1);session_impl_prompt(s);return; }
        if(!*line || !strcmp(line,"L") || !strcmp(line,"?") || !strcmp(line,"H")) { session_impl_command_core(s,"GAMES");session_impl_prompt(s);return; }
        uint32_t id;const char *tail;
        if(session_impl_number_arg(line,&id,&tail) && !*tail && id) { snprintf(mapped,sizeof(mapped),"DOOR %u",id);session_impl_command_core(s,mapped);session_impl_prompt(s);return; }
        if(!strncmp(line,"DOOR ",5) || !strncmp(line,"LANG ",5) || !strcmp(line,"PING") || !strcmp(line,"QUIT") || !strcmp(line,"LOGOUT")) { session_impl_command_core(s,line);session_impl_prompt(s);return; }
        session_impl_queue_text(s,s->presentation.ui.language==LANGUAGE_JA?"ゲーム番号、L（一覧）、0（主画面）を入力してください。\r\n":"Enter a game number, L (list), or 0 (main).\r\n");session_impl_prompt(s);return;
    }
    if(session_impl_classic_commands(s,line))return;
    if(!strcmp(line,"+") && s->reading.report.kind!=REPORT_NONE) {
        if(!s->reading.report.next) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_NO_PAGE));session_impl_prompt(s);return; }
        if(report_navigation_command(&s->reading.report,mapped,sizeof(mapped)))session_impl_command_core(s,mapped);
        session_impl_prompt(s);return;
    }
    report_navigation_clear(&s->reading.report);
    if(!s->presentation.ui.input && !strcmp(line,"VAULT")) {
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n");session_impl_prompt(s);return; }
        if(!board_feature_enabled("vault")) { session_impl_emit(s,"ERR feature disabled\r\n");session_impl_prompt(s);return; }
        s->files.vault_mode=1;s->mail_state.mail_mode=0;session_impl_command_core(s,"FILES");session_impl_prompt(s);return;
    }
    if(s->files.vault_mode) {
        if(!strcmp(line,"CD .."))line="0";
        if(!strcmp(line,"MAIL") || !strncmp(line,"MAIL ",5))s->files.vault_mode=0;
        char parent[49];
        if(!strcmp(line,"0") && vault_navigation_parent(&s->files.vault,parent)) { snprintf(mapped,sizeof(mapped),"FILEBOARD %s",parent);session_impl_command_core(s,mapped);session_impl_prompt(s);return; }
        if(!strcmp(line,"0") || !strcmp(line,"EXIT")) { s->files.vault_mode=0;s->presentation.ui.view=UI_HOME;session_impl_show_menu(s,1);session_impl_prompt(s);return; }
        if(!strcmp(line,"H") || !strcmp(line,"?")) { session_impl_show_help(s,"FILES");session_impl_prompt(s);return; }
        if(!strcmp(line,"L")) { s->files.vault.child_view=0;snprintf(mapped,sizeof(mapped),"FILESIN %s",vault_navigation_path(&s->files.vault)); }
        else if(!strncmp(line,"UIN ",4))snprintf(mapped,sizeof(mapped),"ZUPLOADIN %s",line+4);
        else if(!strcmp(line,"BOARDS") || !strcmp(line,"DIR"))snprintf(mapped,sizeof(mapped),"FILECHILDREN %s",vault_navigation_path(&s->files.vault));
        else if(!strcmp(line,"U") || !strncmp(line,"U ",2))snprintf(mapped,sizeof(mapped),"UPLOAD %s|%s",vault_navigation_path(&s->files.vault),line+2);
        else if(!strncmp(line,"D ",2))snprintf(mapped,sizeof(mapped),"DOWNLOAD %s",line+2);
        else if(!strncmp(line,"N ",2))snprintf(mapped,sizeof(mapped),"FILENEWS %s|%s",vault_navigation_path(&s->files.vault),line+2);
        else if(!strncmp(line,"S ",2))snprintf(mapped,sizeof(mapped),"FILESEARCH %s|%s",vault_navigation_path(&s->files.vault),line+2);
        else if(!strcmp(line,"C"))snprintf(mapped,sizeof(mapped),"FILECATEGORIES");
        else if(!strncmp(line,"I ",2))snprintf(mapped,sizeof(mapped),"FILEBOARD %s",line+2);
        else if(!strncmp(line,"CD ",3))snprintf(mapped,sizeof(mapped),"FILEBOARD %s%s%s",*s->files.vault.path && strcmp(s->files.vault.path,"-")?s->files.vault.path:"",*s->files.vault.path && strcmp(s->files.vault.path,"-")?"/":"",line+3);
        else if(!strcmp(line,"+")) {
            if(s->files.vault.child_view) { if(!s->files.vault.next) { session_impl_emit(s,"END CHILDREN\r\n");session_impl_prompt(s);return; }BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_CHILDREN;q.number=s->files.vault.next;strcpy(q.body,vault_navigation_path(&s->files.vault));session_impl_submit(s,&q);return; }
            if(!s->presentation.ui.next) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_NO_PAGE));session_impl_prompt(s);return; }
            if(s->presentation.ui.view==UI_FILES && s->presentation.ui.file_mode)snprintf(mapped,sizeof(mapped),s->presentation.ui.file_mode==3?"FILENEWSFROM %u %s":s->presentation.ui.file_mode==1?"FILESEARCHFROM %u %s":"FILESINFROM %u %s",s->presentation.ui.next,s->presentation.ui.file_query);
            else snprintf(mapped,sizeof(mapped),s->presentation.ui.view==UI_FILEBOARDS?"FILEBOARDS %u":s->presentation.ui.view==UI_FILECATEGORIES?"FILECATEGORIES %u":"FILES %u",s->presentation.ui.next);
        }
        else if(*line>='0' && *line<='9') { const char *end;uint32_t selected;if(!session_impl_number_arg(line,&selected,&end) || *end) { session_impl_emit(s,"ERR file number\r\n");return; }if(s->files.vault.child_view) { const char *path=vault_children_select(&s->files.vault,selected);if(!path) { session_impl_emit(s,"ERR board number\r\n");session_impl_prompt(s);return; }snprintf(mapped,sizeof(mapped),"FILEBOARD %s",path); }else { s->files.file_selecting=selected;snprintf(mapped,sizeof(mapped),"FILE %u",selected); } }
        else snprintf(mapped,sizeof(mapped),"%s",line);
        session_impl_command_core(s,mapped);session_impl_prompt(s);return;
    }
    if(s->mail_state.mail_mode || !strcmp(line,"MAIL") || !strncmp(line,"MAIL ",5)) { session_impl_command_core(s,line);session_impl_prompt(s);return; }
    if(!strcmp(line,"MENU")) {
        s->presentation.classic_area=CM_MAIN;
        Language language=s->presentation.ui.language;
        int admin=s->presentation.ui.admin,sigop=s->presentation.ui.sigop;memset(&s->presentation.ui,0,sizeof(s->presentation.ui));s->presentation.ui.admin=admin;s->presentation.ui.sigop=sigop; s->presentation.ui.language=language; s->presentation.ui.enabled=1;
        session_impl_show_menu(s,1); session_impl_prompt(s); return;
    }
    if(!s->presentation.ui.enabled) { if(!strcmp(line,"RAW")) session_impl_queue_text(s,"RAW\r\n"); else session_impl_command_core(s,line); return; }
    if(s->presentation.ui.input) {
        int input=s->presentation.ui.input; s->presentation.ui.input=0;
        if(!strcmp(line,"/CANCEL")) session_impl_emit(s,"CANCELLED\r\n");
        else {
            snprintf(mapped,sizeof(mapped),"%s %s",input==UI_LOGIN?"LOGIN":input==UI_REGISTER?"REGISTER":input==UI_POST?"POST":input==UI_SEARCH_INPUT?"SEARCH":"RESPOND",line);
            session_impl_command_core(s,mapped);
        }
        session_impl_prompt(s); return;
    }
    if(!strcmp(line,"RAW")) { Language language=s->presentation.ui.language; int admin=s->presentation.ui.admin,sigop=s->presentation.ui.sigop;memset(&s->presentation.ui,0,sizeof(s->presentation.ui));s->presentation.ui.admin=admin;s->presentation.ui.sigop=sigop; s->presentation.ui.language=language; session_impl_queue_text(s,"\r\nRAW\r\n"); return; }
    if(!strcmp(line,"?")) {
        s->presentation.ui.view=UI_HOME; s->presentation.ui.next=0; session_impl_show_menu(s,1); session_impl_prompt(s); return;
    }
    if(strlen(line)==1) {
        char key=line[0]; if(key>='a' && key<='z') key-=32;
        switch(key) {
        case 'E': action="FILES"; break;
        case 'H': session_impl_show_help(s,""); session_impl_prompt(s); return;
        case 'V':
            if(s->presentation.ui.view==UI_AUDIT) { session_impl_audit_command(&s->presentation.ui,0,mapped,sizeof(mapped)); action=mapped; }
            else action="AUDIT";
            break;
        case 'K': action="NOTES UNREAD"; break;
        case 'J': action="NEXTBOARD"; break;
        case 'B': action="BOARDS"; break; case 'N': action="NOTES"; break;
        case 'U': action="UNREAD"; break; case 'F': action="NEW"; break;
        case 'P': action="PARENT"; break; case 'C': action="CHILD"; break;
        case '>': action="NEXT"; break; case '<': action="PREV"; break;
        case 'T': action="TREE"; break; case 'S': action="SEEN"; break;
        case 'A': action="SEEN"; s->presentation.ui.next_after_seen=1; break;
        case 'I': action="ACCOUNT"; break; case 'O': action="LOGOUT"; break;
        case 'Q': action="QUIT"; break;
        case 'X': s->presentation.ui.input=UI_SEARCH_INPUT; session_impl_prompt(s); return;
        case 'L': case 'G':
            if(key=='G' && s->core.user && s->presentation.ui.view==UI_ARTICLE && s->core.current) { snprintf(mapped,sizeof(mapped),"GET %u",s->core.current);action=mapped;break; }
            if(s->core.user) session_impl_emit(s,"ERR logout first\r\n");
            else if(!s->core.member_mode) session_impl_emit(s,"ERR members disabled\r\n");
            else if(key=='G' && members_auto_ids()) { session_impl_command_core(s,"REGISTER auto");return; }
            else s->presentation.ui.input=key=='L'?UI_LOGIN:UI_REGISTER;
            session_impl_prompt(s); return;
        case 'W': case 'R':
            if(key=='R' && !s->core.current) session_impl_emit(s,"ERR no current article\r\n");
            else if(s->core.member_mode && !s->core.user) { BoardRequest q={0};q.op='e';q.action='?';q.number=key=='R'?s->core.current:0;session_impl_submit(s,&q); }
            else s->presentation.ui.input=key=='W'?UI_POST:UI_REPLY;
            session_impl_prompt(s); return;
        case '+':
            if(!s->presentation.ui.next) { session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_NO_PAGE)); session_impl_prompt(s); return; }
            if(s->presentation.ui.view==UI_AUDIT) session_impl_audit_command(&s->presentation.ui,s->presentation.ui.next,mapped,sizeof(mapped));
            else if(s->presentation.ui.view==UI_FILES) {
                if(s->presentation.ui.file_mode)snprintf(mapped,sizeof(mapped),"%s %u %s",s->presentation.ui.file_mode==3?"FILENEWSFROM":s->presentation.ui.file_mode==1?"FILESEARCHFROM":"FILESINFROM",s->presentation.ui.next,s->presentation.ui.file_query);
                else snprintf(mapped,sizeof(mapped),"FILES %u",s->presentation.ui.next);
            }
            else if(s->presentation.ui.view==UI_FILEAUDIT || s->presentation.ui.view==UI_FILEBOARDS || s->presentation.ui.view==UI_FILECATEGORIES)snprintf(mapped,sizeof(mapped),"%s %u",s->presentation.ui.view==UI_FILEAUDIT?"FILEAUDIT":s->presentation.ui.view==UI_FILEBOARDS?"FILEBOARDS":"FILECATEGORIES",s->presentation.ui.next);
            else if(s->presentation.ui.view==UI_SEARCH) snprintf(mapped,sizeof(mapped),"SEARCHFROM %u %s",s->presentation.ui.next,s->presentation.ui.search);
            else if(s->presentation.ui.view==UI_FIND || s->presentation.ui.view==UI_NEWS) snprintf(mapped,sizeof(mapped),"%s %u %s",s->presentation.ui.view==UI_NEWS?"NEWSFROM":"FINDFROM",s->presentation.ui.next,s->presentation.ui.find_query);
            else if(s->presentation.ui.view==UI_PROFILES) snprintf(mapped,sizeof(mapped),"PROFILESFROM %u %s",s->presentation.ui.next,s->presentation.ui.profile_query);
            else if(s->presentation.ui.view==UI_TREE) snprintf(mapped,sizeof(mapped),"TREE %u %u",s->presentation.ui.tree,s->presentation.ui.next);
            else if(s->presentation.ui.view==UI_UNREAD) snprintf(mapped,sizeof(mapped),"UNREAD %u %u",s->presentation.ui.unread_root,s->presentation.ui.next);
            else snprintf(mapped,sizeof(mapped),"%s %u",s->presentation.ui.view==UI_BOARDS?"BOARDS":s->presentation.ui.view==UI_NOTES?(s->presentation.ui.notes_unread?"NOTES UNREAD":"NOTES"):s->presentation.ui.view==UI_MEMBERS?"MEMBERS":s->presentation.ui.view==UI_AUDIT?"AUDIT":"LIST",s->presentation.ui.next);
            action=mapped; break;
        }
    }
    uint32_t id; const char *tail;
    if(!action && s->presentation.ui.view==UI_AUDIT && *line>='0' && *line<='9') {
        if(!session_impl_audit_number(line,&id)) { session_impl_emit(s,"ERR audit cursor\r\n"); session_impl_prompt(s); return; }
        session_impl_audit_command(&s->presentation.ui,id,mapped,sizeof(mapped)); action=mapped;
    }
    if(!action && (s->presentation.ui.view==UI_FILEAUDIT || s->presentation.ui.view==UI_FILEBOARDS || s->presentation.ui.view==UI_FILECATEGORIES) && session_impl_number_arg(line,&id,&tail) && !*tail) {
        session_impl_queue_text(s,ui_text(&s->presentation.ui,s->presentation.ui.view==UI_FILEAUDIT?MSG_VAUDIT_HINT:s->presentation.ui.view==UI_FILEBOARDS?MSG_VBOARDS_HINT:MSG_VCATEGORIES_HINT));session_impl_prompt(s);return;
    }
    if(!action && session_impl_number_arg(line,&id,&tail) && !*tail) {
        snprintf(mapped,sizeof(mapped),"%s %u",s->presentation.ui.view==UI_BOARDS?"BOARD":s->presentation.ui.view==UI_MEMBERS?"MEMBER":s->presentation.ui.view==UI_PROFILES?"PROFILEID":s->presentation.ui.view==UI_FILES?"FILE":(s->presentation.ui.view==UI_FIND || s->presentation.ui.view==UI_NEWS)?"READGLOBAL":"READ",id); action=mapped;
    }
    if(action && action!=mapped) snprintf(mapped,sizeof(mapped),"%s",action);
    session_impl_command_core(s,action?mapped:line);
    if(!s->core.pending) s->presentation.ui.next_after_seen=0;
    session_impl_prompt(s);
}

void session_rtbbs_door_finish(Session *s) {
    session_impl_queue_text(s,s->presentation.ui.language==LANGUAGE_JA?"\r\nゲーム終了。BBSへ戻ります。\r\n":"\r\nGame ended. Back to BBS.\r\n");
    if(s->presentation.game_menu && s->core.user && !s->core.closing) { session_impl_command_core(s,"GAMES");session_impl_prompt(s);return; }
    if(s->presentation.ui.enabled)session_impl_show_menu(s,0);
    session_impl_prompt(s);
}

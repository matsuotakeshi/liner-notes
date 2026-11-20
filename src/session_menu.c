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
    if((!strcmp(line,"UI") || !strncmp(line,"UI ",3)) && !session_secret(s) && !rtbbs(s)->editor.editing) {
        int en=rtbbs(s)->presentation.ui.language==LANGUAGE_EN;
        const SessionUiConfig *config=&s->ui_binding.config;
        if(!strcmp(line,"UI")) {
            char text[512];size_t used=(size_t)snprintf(text,sizeof(text),"UI %s / %s / DEFAULT %s\r\n",session_ui_name(s),config->policy==SESSION_UI_FIXED?"FIXED":config->policy==SESSION_UI_MEMBERS?"MEMBERS":"ALL",config->adapters[config->default_index]->name);
            for(unsigned i=0;i<config->count && used<sizeof(text);i++)if((config->allowed&(1u<<i)) && (config->policy!=SESSION_UI_FIXED || i==config->default_index))used+=(size_t)snprintf(text+used,sizeof(text)-used,"  UI %s\r\n",config->adapters[i]->name);
            session_impl_queue_text(s,text);session_impl_prompt(s);return;
        }
        char name[32];const char *input=line+3;int reset=!strcmp(input,"RESET");
        if(reset)input=config->adapters[config->default_index]->name;
        size_t n=strlen(input);if(!n || n>=sizeof(name)) { session_impl_emit(s,"ERR UI\r\n");session_impl_prompt(s);return; }
        for(size_t i=0;i<=n;i++)name[i]=input[i]>='A' && input[i]<='Z'?(char)(input[i]+32):input[i];
        SessionUiSelection result=session_ui_choose(s,name);
        if(result==SESSION_UI_SELECTED || result==SESSION_UI_UNCHANGED) {
            if(s->core.user) {
                if(!session_ui_save_choice(s,reset?"":name))session_impl_emit(s,en?"UI selected; preference was not saved.\r\n":"UIを選択しましたが、設定を保存できませんでした。\r\n");
            } else session_impl_emit(s,en?"UI selected for this connection.\r\n":"この接続中のUIを選択しました。\r\n");
        } else session_impl_emit(s,result==SESSION_UI_BUSY?(en?"Finish the current operation before changing UI.\r\n":"操作を終了してからUIを切り替えてください。\r\n"):(en?"UI selection is not permitted or unavailable.\r\n":"選択できないUI、または選択が許可されていません。\r\n"));
        session_impl_prompt(s);return;
    }
    if(rtbbs(s)->conversation.chatting) { session_impl_chat_input(s,line);return; }
    char mapped[INPUT_LINE_MAX+128]; const char *action=NULL;
    if(rtbbs(s)->presentation.page[0]) {
        if(rtbbs(s)->reading.reader.active && (!strcmp(line,"F") || !strcmp(line,"f"))) { session_impl_queue_text(s,rtbbs(s)->presentation.page+rtbbs(s)->presentation.page_offset);session_impl_page_clear(s);rtbbs(s)->reading.reader.automatic=1;return; }
        if(!*line) session_impl_page_next(s);
        else if(!strcmp(line,"Q") || !strcmp(line,"q") || !strcmp(line,"/CANCEL")) { int help=rtbbs(s)->presentation.help_page; rtbbs(s)->reading.reader.active=0;if(!help)rtbbs(s)->reading.check.active=0; session_impl_page_clear(s); if(help) session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_HELP_CLOSED)); else session_impl_emit(s,rtbbs(s)->editor.editing?"PREVIEW CLOSED\r\n":"CANCELLED\r\n"); }
        else if(!strcmp(line,"PAGER OFF") || !strcmp(line,"OFF")) { rtbbs(s)->presentation.pager_enabled=0; session_impl_queue_text(s,rtbbs(s)->presentation.page+rtbbs(s)->presentation.page_offset); session_impl_page_clear(s); session_impl_emit(s,"PAGER OFF\r\n"); }
        else session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_PAGER_MORE));
        session_impl_prompt(s); return;
    }
    if(rtbbs(s)->conversation.gather.mode) { session_impl_gather_input(s,line);session_impl_prompt(s);return; }
    if(rtbbs(s)->reading.reader.active) {
        if(!strcmp(line,"P") || !strcmp(line,"L")) {
            rtbbs(s)->reading.reader.automatic=0;
            ReaderMove move=*line=='P'?READER_PREVIOUS:READER_REPEAT;
            ReaderPosition next=rtbbs(s)->reading.scan.op=='m'?mail_navigation_move(&rtbbs(s)->mail_state.mail,move,rtbbs(s)->reading.scan.descending,1):reader_move(move,s->core.current,rtbbs(s)->reading.scan.descending,1);
            rtbbs(s)->reading.scan.cursor=next.cursor;rtbbs(s)->reading.scan.descending=next.descending;
            if(rtbbs(s)->reading.scan.cursor)session_impl_submit(s,&rtbbs(s)->reading.scan);else session_impl_emit(s,"SCAN END\r\n");
            return;
        }
        if(!strcmp(line,"F")) { rtbbs(s)->reading.reader.automatic=1;return; }
        if(rtbbs(s)->reading.reader.automatic) { rtbbs(s)->reading.reader.automatic=0;session_impl_page_clear(s);if(!*line || !strcmp(line,"STOP")) { rtbbs(s)->reading.reader.active=0;session_impl_prompt(s);return; } }
        if(*line=='+' || *line=='-') {
            uint32_t distance=1;const char *tail;
            if(line[1] && (!session_impl_number_arg(line+1,&distance,&tail) || *tail || !distance)) { session_impl_emit(s,"ERR relative distance\r\n");session_impl_prompt(s);return; }
            ReaderMove move=*line=='-'?READER_BACKWARD:READER_FORWARD;
            ReaderPosition next=rtbbs(s)->reading.scan.op=='m'?mail_navigation_move(&rtbbs(s)->mail_state.mail,move,rtbbs(s)->reading.scan.descending,distance):reader_move(move,s->core.current,rtbbs(s)->reading.scan.descending,distance);
            rtbbs(s)->reading.scan.cursor=next.cursor;rtbbs(s)->reading.scan.descending=next.descending;
            if(rtbbs(s)->reading.scan.cursor)session_impl_submit(s,&rtbbs(s)->reading.scan);else { rtbbs(s)->reading.reader.active=0;session_impl_emit(s,rtbbs(s)->reading.scan.op=='m'?"MAIL SCAN END\r\n":"SCAN END\r\n");session_impl_prompt(s); }return;
        }
        if(!*line || !strcmp(line,"CONTINUE")) { if(rtbbs(s)->reading.scan.cursor)session_impl_submit(s,&rtbbs(s)->reading.scan);else { rtbbs(s)->reading.reader.active=0;session_impl_emit(s,rtbbs(s)->reading.scan.op=='m'?"MAIL SCAN END\r\n":"SCAN END\r\n");session_impl_prompt(s); }return; }
        rtbbs(s)->reading.reader.active=0;
        if(!strcmp(line,"0") || !strcmp(line,"Q") || !strcmp(line,"q") || !strcmp(line,"STOP") || !strcmp(line,"/CANCEL")) {
            session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_SCAN_STOP)); session_impl_prompt(s); return;
        }
    }
    if(rtbbs(s)->auth.password_mode || rtbbs(s)->editor.editing || rtbbs(s)->auth.application.step) { session_rtbbs_command(s,line); session_impl_prompt(s); return; }
    if(!rtbbs(s)->presentation.ui.input && !rtbbs(s)->presentation.classic_input && !rtbbs(s)->mail_state.mail_input && !rtbbs(s)->mail_state.mail.delete_id && (!strcmp(line,"LANG") || !strncmp(line,"LANG ",5))) {
        if(line[4] && !language_parse(line+5,&rtbbs(s)->presentation.ui.language)) session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_ERR_LANGUAGE));
        else {
            snprintf(mapped,sizeof(mapped),ui_text(&rtbbs(s)->presentation.ui,MSG_LANGUAGE),language_code(rtbbs(s)->presentation.ui.language));
            session_impl_queue_text(s,mapped);
        }
        session_impl_prompt(s); return;
    }
    if(!rtbbs(s)->presentation.ui.input && !rtbbs(s)->presentation.classic_input && !rtbbs(s)->mail_state.mail_input && !rtbbs(s)->mail_state.mail.delete_id && rtbbs(s)->presentation.ui.enabled && !*s->connection.connector_token &&
       (!strcmp(line,"GAME") || !strcmp(line,"GAMES"))) {
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n");session_impl_prompt(s);return; }
        rtbbs(s)->presentation.game_menu=1;rtbbs(s)->files.vault_mode=rtbbs(s)->mail_state.mail_mode=rtbbs(s)->conversation.gather.mode=0;rtbbs(s)->presentation.classic_area=CM_MAIN;
        session_rtbbs_command(s,"GAMES");session_impl_prompt(s);return;
    }
    if(rtbbs(s)->presentation.game_menu) {
        if(!strcmp(line,"0") || !strcmp(line,"EXIT") || !strcmp(line,"MENU")) { rtbbs(s)->presentation.game_menu=0;rtbbs(s)->presentation.ui.view=UI_HOME;rtbbs(s)->presentation.classic_area=CM_MAIN;session_impl_show_menu(s,1);session_impl_prompt(s);return; }
        if(!*line || !strcmp(line,"L") || !strcmp(line,"?") || !strcmp(line,"H")) { session_rtbbs_command(s,"GAMES");session_impl_prompt(s);return; }
        uint32_t id;const char *tail;
        if(session_impl_number_arg(line,&id,&tail) && !*tail && id) { snprintf(mapped,sizeof(mapped),"DOOR %u",id);session_rtbbs_command(s,mapped);session_impl_prompt(s);return; }
        if(!strncmp(line,"DOOR ",5) || !strncmp(line,"LANG ",5) || !strcmp(line,"PING") || !strcmp(line,"QUIT") || !strcmp(line,"LOGOUT")) { session_rtbbs_command(s,line);session_impl_prompt(s);return; }
        session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"ゲーム番号、L（一覧）、0（主画面）を入力してください。\r\n":"Enter a game number, L (list), or 0 (main).\r\n");session_impl_prompt(s);return;
    }
    if(session_impl_classic_commands(s,line))return;
    if(!strcmp(line,"+") && rtbbs(s)->reading.report.kind!=REPORT_NONE) {
        if(!rtbbs(s)->reading.report.next) { session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_NO_PAGE));session_impl_prompt(s);return; }
        if(report_navigation_command(&rtbbs(s)->reading.report,mapped,sizeof(mapped)))session_rtbbs_command(s,mapped);
        session_impl_prompt(s);return;
    }
    report_navigation_clear(&rtbbs(s)->reading.report);
    if(!rtbbs(s)->presentation.ui.input && !strcmp(line,"VAULT")) {
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n");session_impl_prompt(s);return; }
        if(!board_feature_enabled("vault")) { session_impl_emit(s,"ERR feature disabled\r\n");session_impl_prompt(s);return; }
        rtbbs(s)->files.vault_mode=1;rtbbs(s)->mail_state.mail_mode=0;session_rtbbs_command(s,"FILES");session_impl_prompt(s);return;
    }
    if(rtbbs(s)->files.vault_mode) {
        if(!strcmp(line,"CD .."))line="0";
        if(!strcmp(line,"MAIL") || !strncmp(line,"MAIL ",5))rtbbs(s)->files.vault_mode=0;
        char parent[49];
        if(!strcmp(line,"0") && vault_navigation_parent(&rtbbs(s)->files.vault,parent)) { snprintf(mapped,sizeof(mapped),"FILEBOARD %s",parent);session_rtbbs_command(s,mapped);session_impl_prompt(s);return; }
        if(!strcmp(line,"0") || !strcmp(line,"EXIT")) { rtbbs(s)->files.vault_mode=0;rtbbs(s)->presentation.ui.view=UI_HOME;session_impl_show_menu(s,1);session_impl_prompt(s);return; }
        if(!strcmp(line,"H") || !strcmp(line,"?")) { session_impl_show_help(s,"FILES");session_impl_prompt(s);return; }
        if(!strcmp(line,"L")) { rtbbs(s)->files.vault.child_view=0;snprintf(mapped,sizeof(mapped),"FILESIN %s",vault_navigation_path(&rtbbs(s)->files.vault)); }
        else if(!strncmp(line,"UIN ",4))snprintf(mapped,sizeof(mapped),"ZUPLOADIN %s",line+4);
        else if(!strcmp(line,"BOARDS") || !strcmp(line,"DIR"))snprintf(mapped,sizeof(mapped),"FILECHILDREN %s",vault_navigation_path(&rtbbs(s)->files.vault));
        else if(!strcmp(line,"U") || !strncmp(line,"U ",2))snprintf(mapped,sizeof(mapped),"UPLOAD %s|%s",vault_navigation_path(&rtbbs(s)->files.vault),line+2);
        else if(!strncmp(line,"D ",2))snprintf(mapped,sizeof(mapped),"DOWNLOAD %s",line+2);
        else if(!strncmp(line,"N ",2))snprintf(mapped,sizeof(mapped),"FILENEWS %s|%s",vault_navigation_path(&rtbbs(s)->files.vault),line+2);
        else if(!strncmp(line,"S ",2))snprintf(mapped,sizeof(mapped),"FILESEARCH %s|%s",vault_navigation_path(&rtbbs(s)->files.vault),line+2);
        else if(!strcmp(line,"C"))snprintf(mapped,sizeof(mapped),"FILECATEGORIES");
        else if(!strncmp(line,"I ",2))snprintf(mapped,sizeof(mapped),"FILEBOARD %s",line+2);
        else if(!strncmp(line,"CD ",3))snprintf(mapped,sizeof(mapped),"FILEBOARD %s%s%s",*rtbbs(s)->files.vault.path && strcmp(rtbbs(s)->files.vault.path,"-")?rtbbs(s)->files.vault.path:"",*rtbbs(s)->files.vault.path && strcmp(rtbbs(s)->files.vault.path,"-")?"/":"",line+3);
        else if(!strcmp(line,"+")) {
            if(rtbbs(s)->files.vault.child_view) { if(!rtbbs(s)->files.vault.next) { session_impl_emit(s,"END CHILDREN\r\n");session_impl_prompt(s);return; }BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_CHILDREN;q.number=rtbbs(s)->files.vault.next;strcpy(q.body,vault_navigation_path(&rtbbs(s)->files.vault));session_impl_submit(s,&q);return; }
            if(!rtbbs(s)->presentation.ui.next) { session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_NO_PAGE));session_impl_prompt(s);return; }
            if(rtbbs(s)->presentation.ui.view==UI_FILES && rtbbs(s)->presentation.ui.file_mode)snprintf(mapped,sizeof(mapped),rtbbs(s)->presentation.ui.file_mode==3?"FILENEWSFROM %u %s":rtbbs(s)->presentation.ui.file_mode==1?"FILESEARCHFROM %u %s":"FILESINFROM %u %s",rtbbs(s)->presentation.ui.next,rtbbs(s)->presentation.ui.file_query);
            else snprintf(mapped,sizeof(mapped),rtbbs(s)->presentation.ui.view==UI_FILEBOARDS?"FILEBOARDS %u":rtbbs(s)->presentation.ui.view==UI_FILECATEGORIES?"FILECATEGORIES %u":"FILES %u",rtbbs(s)->presentation.ui.next);
        }
        else if(*line>='0' && *line<='9') { const char *end;uint32_t selected;if(!session_impl_number_arg(line,&selected,&end) || *end) { session_impl_emit(s,"ERR file number\r\n");return; }if(rtbbs(s)->files.vault.child_view) { const char *path=vault_children_select(&rtbbs(s)->files.vault,selected);if(!path) { session_impl_emit(s,"ERR board number\r\n");session_impl_prompt(s);return; }snprintf(mapped,sizeof(mapped),"FILEBOARD %s",path); }else { rtbbs(s)->files.file_selecting=selected;snprintf(mapped,sizeof(mapped),"FILE %u",selected); } }
        else snprintf(mapped,sizeof(mapped),"%s",line);
        session_rtbbs_command(s,mapped);session_impl_prompt(s);return;
    }
    if(rtbbs(s)->mail_state.mail_mode || !strcmp(line,"MAIL") || !strncmp(line,"MAIL ",5)) { session_rtbbs_command(s,line);session_impl_prompt(s);return; }
    if(!strcmp(line,"MENU")) {
        rtbbs(s)->presentation.classic_area=CM_MAIN;
        Language language=rtbbs(s)->presentation.ui.language;
        int admin=rtbbs(s)->presentation.ui.admin,sigop=rtbbs(s)->presentation.ui.sigop;memset(&rtbbs(s)->presentation.ui,0,sizeof(rtbbs(s)->presentation.ui));rtbbs(s)->presentation.ui.admin=admin;rtbbs(s)->presentation.ui.sigop=sigop; rtbbs(s)->presentation.ui.language=language; rtbbs(s)->presentation.ui.enabled=1;
        session_impl_show_menu(s,1); session_impl_prompt(s); return;
    }
    if(!rtbbs(s)->presentation.ui.enabled) { if(!strcmp(line,"RAW")) session_impl_queue_text(s,"RAW\r\n"); else session_rtbbs_command(s,line); return; }
    if(rtbbs(s)->presentation.ui.input) {
        int input=rtbbs(s)->presentation.ui.input; rtbbs(s)->presentation.ui.input=0;
        if(!strcmp(line,"/CANCEL")) session_impl_emit(s,"CANCELLED\r\n");
        else {
            snprintf(mapped,sizeof(mapped),"%s %s",input==UI_LOGIN?"LOGIN":input==UI_REGISTER?"REGISTER":input==UI_POST?"POST":input==UI_SEARCH_INPUT?"SEARCH":"RESPOND",line);
            session_rtbbs_command(s,mapped);
        }
        session_impl_prompt(s); return;
    }
    if(!strcmp(line,"RAW")) { Language language=rtbbs(s)->presentation.ui.language; int admin=rtbbs(s)->presentation.ui.admin,sigop=rtbbs(s)->presentation.ui.sigop;memset(&rtbbs(s)->presentation.ui,0,sizeof(rtbbs(s)->presentation.ui));rtbbs(s)->presentation.ui.admin=admin;rtbbs(s)->presentation.ui.sigop=sigop; rtbbs(s)->presentation.ui.language=language; session_impl_queue_text(s,"\r\nRAW\r\n"); return; }
    if(!strcmp(line,"?")) {
        rtbbs(s)->presentation.ui.view=UI_HOME; rtbbs(s)->presentation.ui.next=0; session_impl_show_menu(s,1); session_impl_prompt(s); return;
    }
    if(strlen(line)==1) {
        char key=line[0]; if(key>='a' && key<='z') key-=32;
        switch(key) {
        case 'E': action="FILES"; break;
        case 'H': session_impl_show_help(s,""); session_impl_prompt(s); return;
        case 'V':
            if(rtbbs(s)->presentation.ui.view==UI_AUDIT) { session_impl_audit_command(&rtbbs(s)->presentation.ui,0,mapped,sizeof(mapped)); action=mapped; }
            else action="AUDIT";
            break;
        case 'K': action="NOTES UNREAD"; break;
        case 'J': action="NEXTBOARD"; break;
        case 'B': action="BOARDS"; break; case 'N': action="NOTES"; break;
        case 'U': action="UNREAD"; break; case 'F': action="NEW"; break;
        case 'P': action="PARENT"; break; case 'C': action="CHILD"; break;
        case '>': action="NEXT"; break; case '<': action="PREV"; break;
        case 'T': action="TREE"; break; case 'S': action="SEEN"; break;
        case 'A': action="SEEN"; rtbbs(s)->presentation.ui.next_after_seen=1; break;
        case 'I': action="ACCOUNT"; break; case 'O': action="LOGOUT"; break;
        case 'Q': action="QUIT"; break;
        case 'X': rtbbs(s)->presentation.ui.input=UI_SEARCH_INPUT; session_impl_prompt(s); return;
        case 'L': case 'G':
            if(key=='G' && s->core.user && rtbbs(s)->presentation.ui.view==UI_ARTICLE && s->core.current) { snprintf(mapped,sizeof(mapped),"GET %u",s->core.current);action=mapped;break; }
            if(s->core.user) session_impl_emit(s,"ERR logout first\r\n");
            else if(!s->core.member_mode) session_impl_emit(s,"ERR members disabled\r\n");
            else if(key=='G' && members_auto_ids()) { session_rtbbs_command(s,"REGISTER auto");return; }
            else rtbbs(s)->presentation.ui.input=key=='L'?UI_LOGIN:UI_REGISTER;
            session_impl_prompt(s); return;
        case 'W': case 'R':
            if(key=='R' && !s->core.current) session_impl_emit(s,"ERR no current article\r\n");
            else if(s->core.member_mode && !s->core.user) { BoardRequest q={0};q.op='e';q.action='?';q.number=key=='R'?s->core.current:0;session_impl_submit(s,&q); }
            else rtbbs(s)->presentation.ui.input=key=='W'?UI_POST:UI_REPLY;
            session_impl_prompt(s); return;
        case '+':
            if(!rtbbs(s)->presentation.ui.next) { session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_NO_PAGE)); session_impl_prompt(s); return; }
            if(rtbbs(s)->presentation.ui.view==UI_AUDIT) session_impl_audit_command(&rtbbs(s)->presentation.ui,rtbbs(s)->presentation.ui.next,mapped,sizeof(mapped));
            else if(rtbbs(s)->presentation.ui.view==UI_FILES) {
                if(rtbbs(s)->presentation.ui.file_mode)snprintf(mapped,sizeof(mapped),"%s %u %s",rtbbs(s)->presentation.ui.file_mode==3?"FILENEWSFROM":rtbbs(s)->presentation.ui.file_mode==1?"FILESEARCHFROM":"FILESINFROM",rtbbs(s)->presentation.ui.next,rtbbs(s)->presentation.ui.file_query);
                else snprintf(mapped,sizeof(mapped),"FILES %u",rtbbs(s)->presentation.ui.next);
            }
            else if(rtbbs(s)->presentation.ui.view==UI_FILEAUDIT || rtbbs(s)->presentation.ui.view==UI_FILEBOARDS || rtbbs(s)->presentation.ui.view==UI_FILECATEGORIES)snprintf(mapped,sizeof(mapped),"%s %u",rtbbs(s)->presentation.ui.view==UI_FILEAUDIT?"FILEAUDIT":rtbbs(s)->presentation.ui.view==UI_FILEBOARDS?"FILEBOARDS":"FILECATEGORIES",rtbbs(s)->presentation.ui.next);
            else if(rtbbs(s)->presentation.ui.view==UI_SEARCH) snprintf(mapped,sizeof(mapped),"SEARCHFROM %u %s",rtbbs(s)->presentation.ui.next,rtbbs(s)->presentation.ui.search);
            else if(rtbbs(s)->presentation.ui.view==UI_FIND || rtbbs(s)->presentation.ui.view==UI_NEWS) snprintf(mapped,sizeof(mapped),"%s %u %s",rtbbs(s)->presentation.ui.view==UI_NEWS?"NEWSFROM":"FINDFROM",rtbbs(s)->presentation.ui.next,rtbbs(s)->presentation.ui.find_query);
            else if(rtbbs(s)->presentation.ui.view==UI_PROFILES) snprintf(mapped,sizeof(mapped),"PROFILESFROM %u %s",rtbbs(s)->presentation.ui.next,rtbbs(s)->presentation.ui.profile_query);
            else if(rtbbs(s)->presentation.ui.view==UI_TREE) snprintf(mapped,sizeof(mapped),"TREE %u %u",rtbbs(s)->presentation.ui.tree,rtbbs(s)->presentation.ui.next);
            else if(rtbbs(s)->presentation.ui.view==UI_UNREAD) snprintf(mapped,sizeof(mapped),"UNREAD %u %u",rtbbs(s)->presentation.ui.unread_root,rtbbs(s)->presentation.ui.next);
            else snprintf(mapped,sizeof(mapped),"%s %u",rtbbs(s)->presentation.ui.view==UI_BOARDS?"BOARDS":rtbbs(s)->presentation.ui.view==UI_NOTES?(rtbbs(s)->presentation.ui.notes_unread?"NOTES UNREAD":"NOTES"):rtbbs(s)->presentation.ui.view==UI_MEMBERS?"MEMBERS":rtbbs(s)->presentation.ui.view==UI_AUDIT?"AUDIT":"LIST",rtbbs(s)->presentation.ui.next);
            action=mapped; break;
        }
    }
    uint32_t id; const char *tail;
    if(!action && rtbbs(s)->presentation.ui.view==UI_AUDIT && *line>='0' && *line<='9') {
        if(!session_impl_audit_number(line,&id)) { session_impl_emit(s,"ERR audit cursor\r\n"); session_impl_prompt(s); return; }
        session_impl_audit_command(&rtbbs(s)->presentation.ui,id,mapped,sizeof(mapped)); action=mapped;
    }
    if(!action && (rtbbs(s)->presentation.ui.view==UI_FILEAUDIT || rtbbs(s)->presentation.ui.view==UI_FILEBOARDS || rtbbs(s)->presentation.ui.view==UI_FILECATEGORIES) && session_impl_number_arg(line,&id,&tail) && !*tail) {
        session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,rtbbs(s)->presentation.ui.view==UI_FILEAUDIT?MSG_VAUDIT_HINT:rtbbs(s)->presentation.ui.view==UI_FILEBOARDS?MSG_VBOARDS_HINT:MSG_VCATEGORIES_HINT));session_impl_prompt(s);return;
    }
    if(!action && session_impl_number_arg(line,&id,&tail) && !*tail) {
        snprintf(mapped,sizeof(mapped),"%s %u",rtbbs(s)->presentation.ui.view==UI_BOARDS?"BOARD":rtbbs(s)->presentation.ui.view==UI_MEMBERS?"MEMBER":rtbbs(s)->presentation.ui.view==UI_PROFILES?"PROFILEID":rtbbs(s)->presentation.ui.view==UI_FILES?"FILE":(rtbbs(s)->presentation.ui.view==UI_FIND || rtbbs(s)->presentation.ui.view==UI_NEWS)?"READGLOBAL":"READ",id); action=mapped;
    }
    if(action && action!=mapped) snprintf(mapped,sizeof(mapped),"%s",action);
    session_rtbbs_command(s,action?mapped:line);
    if(!s->core.pending) rtbbs(s)->presentation.ui.next_after_seen=0;
    session_impl_prompt(s);
}

void session_rtbbs_door_finish(Session *s) {
    session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"\r\nゲーム終了。BBSへ戻ります。\r\n":"\r\nGame ended. Back to BBS.\r\n");
    if(rtbbs(s)->presentation.game_menu && s->core.user && !s->core.closing) { session_rtbbs_command(s,"GAMES");session_impl_prompt(s);return; }
    if(rtbbs(s)->presentation.ui.enabled)session_impl_show_menu(s,0);
    session_impl_prompt(s);
}

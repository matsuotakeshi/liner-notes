#define _POSIX_C_SOURCE 200809L
#include "date_filter.h"
#include "ui.h"
#include "content_limits.h"
#include "menu.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

typedef struct { char *p; size_t size,used; int ok; } Text;
const char *ui_text(const Ui *u,MessageId id) { return message_text(u->language,id); }
static void add(Text *t,const char *format,...) {
    if(!t->ok) return;
    va_list ap; va_start(ap,format); int n=vsnprintf(t->p+t->used,t->size-t->used,format,ap); va_end(ap);
    if(n<0 || (size_t)n>=t->size-t->used) { t->ok=0; return; }
    t->used+=(size_t)n;
}
const char *ui_menu(const Ui *u) {
    const char *external=menu_text(u->language);
    static char text[16384];
    const char *source=external?external:ui_text(u,MSG_MENU);size_t used=0;int admin_block=0,legacy=0,placed=0;
    for(const char *p=source;*p;) {
        const char *end=strchr(p,'\n');size_t n=end?(size_t)(end+1-p):strlen(p);
        if(!strncmp(p,"[ADMIN_MENU]",12)) { if(!placed) { int n=snprintf(text+used,sizeof(text)-used,"%s%s",u->admin?ui_text(u,MSG_MENU_ADMIN):"",u->sigop?ui_text(u,MSG_MENU_SIGOP):"");if(n>0 && (size_t)n<sizeof(text)-used)used+=(size_t)n;placed=1; } }
        else if(!strncmp(p,"[ADMIN]\r\n",9) || !strncmp(p,"[ADMIN]\n",8))admin_block=1;
        else if(!strncmp(p,"[/ADMIN]",8))admin_block=0;
        else {
            if(!strncmp(p," [管理者用]",strlen(" [管理者用]")) || !strncmp(p," [Administrators]",17))legacy=1;
            else if(legacy && !strncmp(p," [",2))legacy=0;
            if(!legacy && (!admin_block || u->admin) && used+n<sizeof(text)) { memcpy(text+used,p,n);used+=n; }
        }
        p+=n;
    }
    if(!placed)snprintf(text+used,sizeof(text)-used,"%s%s",u->admin?ui_text(u,MSG_MENU_ADMIN):"",u->sigop?ui_text(u,MSG_MENU_SIGOP):"");
    else text[used]=0;
    return text;
}
void ui_prompt(const Ui *u,uint32_t board,uint32_t article,const char *name,int editing,int password,char *out,size_t size) {
    if(password) snprintf(out,size,"%s",ui_text(u,MSG_PROMPT_PASSWORD));
    else if(u->input==UI_SEARCH_INPUT) snprintf(out,size,"%s",ui_text(u,MSG_SEARCH_PROMPT));
    else if(editing) snprintf(out,size,"%s",ui_text(u,MSG_PROMPT_BODY));
    else if(u->input==UI_LOGIN || u->input==UI_REGISTER) snprintf(out,size,"%s",ui_text(u,MSG_PROMPT_NAME));
    else if(u->input==UI_POST || u->input==UI_REPLY) snprintf(out,size,ui_text(u,MSG_PROMPT_TITLE),u->input==UI_REPLY?ui_text(u,MSG_REPLY):ui_text(u,MSG_NOTE));
    else snprintf(out,size,ui_text(u,MSG_PROMPT),name,board,u->board_title[0]?" ":"",u->board_title,article);
}
static const char *error_text(const Ui *u,const char *p) {
    static const struct { const char *wire; MessageId message; } errors[]={
        {"ERR this protocol transfers one file at a time",MSG_ERR_PROTOCOL_SINGLE},
        {"ERR body limit",MSG_ERR_BODY_QUOTA},
        {"ERR CHECK ARTICLES [HERE|SUBTREE|ALL]|MAIL|FILES [path]|NEXT|DONE|CANCEL",MSG_ERR_CHECK_SYNTAX},
        {"ERR application storage; account %s reserved",MSG_ERR_APPLICATION_STORAGE},
        {"ERR settings storage",MSG_ERR_SETTINGS_STORAGE},
        {"ERR profile permission or value",MSG_ERR_PROFILE_VALUE},
        {"ERR file copy",MSG_ERR_COMMAND},
        {"ERR file number",MSG_ERR_COMMAND},
        {"ERR memo",MSG_ERR_COMMAND},
        {"ERR profile administrator required",MSG_ERR_ADMIN},
        {"ERR profile search permission",MSG_ERR_ADMIN},
        {"ERR profile field",MSG_ERR_PROFILE_FIELD},
        {"ERR command or transfer unavailable",MSG_ERR_TRANSFER_UNAVAILABLE},
        {"ERR finish CHECK NEXT first",MSG_ERR_CHECK_UNFINISHED},
        {"ERR relative distance",MSG_ERR_RELATIVE_DISTANCE},
        {"ERR application values",MSG_ERR_APPLICATION_VALUES},
        {"ERR application value",MSG_ERR_APPLICATION_VALUE},
        {"ERR CHECK ARTICLES|MAIL|FILES|NEXT|DONE|CANCEL",MSG_ERR_CHECK_SYNTAX},
        {"ERR catalog storage",MSG_ERR_CATALOG},
        {"ERR catalog syntax",MSG_ERR_CATALOG_SYNTAX},
        {"ERR userlog storage",MSG_ERR_USERLOG},
        {"ERR userlog disabled",MSG_ERR_FEATURE_DISABLED},
        {"ERR download ticket",MSG_ERR_DOWNLOAD_TICKET},
        {"ERR no board",MSG_ERR_BOARD},
        {"ERR activity storage",MSG_ERR_ACTIVITY},
        {"ERR no connection",MSG_ERR_CONNECTION},
        {"ERR game unavailable",MSG_ERR_GAME},
        {"ERR game busy",MSG_ERR_GAME_BUSY},
        {"ERR SYSOP unavailable",MSG_ERR_SYSOP},
        {"ERR mail storage or limit",MSG_ERR_SYSOP_MAIL},
        {"ERR legacy board capacity; compact to RTBOARD4 first",MSG_ERR_LEGACY_CAPACITY},
        {"ERR registration closed",MSG_ERR_REGISTRATION_CLOSED},
        {"ERR feature disabled",MSG_ERR_FEATURE_DISABLED},
        {"ERR board not empty",MSG_ERR_BOARD_NOT_EMPTY},
        {"ERR move target",MSG_ERR_MOVE_TARGET},
        {"ERR board policy storage",MSG_ERR_BOARD_POLICY},
        {"ERR board access storage",MSG_ERR_BOARD_ACCESS},
        {"ERR board info limit",MSG_ERR_BOARD_INFO_LIMIT},
        {"ERR rights syntax",MSG_ERR_RIGHTS},
        {"ERR article owner",MSG_ERR_OWNER},
        {"ERR article withdrawn",MSG_ERR_WITHDRAWN},
        {"ERR imported article; use HIDE for local moderation",MSG_ERR_IMPORTED},
        {"ERR extras storage",MSG_ERR_EXTRAS},
        {"ERR find syntax",MSG_ERR_FIND},
        {"ERR extended syntax",MSG_ERR_EXTENDED},
        {"ERR profile unavailable",MSG_ERR_PROFILE},
        {"ERR profile text",MSG_ERR_PROFILE_TEXT},
        {"ERR password change",MSG_ERR_PASSWORD_CHANGE},
        {"ERR password mismatch",MSG_ERR_PASSWORD_MISMATCH},
        {"ERR password unchanged",MSG_ERR_PASSWORD_UNCHANGED},
        {"ERR member storage unavailable",MSG_ERR_MEMBER_STORAGE},
        {"ERR member state",MSG_ERR_MEMBER_STATE},
        {"ERR article storage",MSG_ERR_STORAGE},
        {"ERR draft conflict",MSG_ERR_DRAFT_CONFLICT},
        {"ERR draft revision",MSG_ERR_DRAFT_REVISION},
        {"ERR no undo",MSG_ERR_NO_UNDO},
        {"ERR connector editor state",MSG_ERR_CONNECTOR_EDITOR_STATE},
        {"ERR link denied",MSG_ERR_LINK_DENIED},
        {"ERR link posting denied",MSG_ERR_LINK_DENIED},
        {"ERR link syntax",MSG_ERR_LINK_SYNTAX},
        {"ERR link identity",MSG_ERR_LINK_SYNTAX},
        {"ERR link revision",MSG_ERR_LINK_SYNTAX},
        {"ERR link action",MSG_ERR_LINK_SYNTAX},
        {"ERR link title",MSG_ERR_LINK_SYNTAX},
        {"ERR link missing predecessor",MSG_ERR_LINK_PREDECESSOR},
        {"ERR link article unavailable",MSG_ERR_LINK_PREDECESSOR},
        {"ERR link parent unavailable",MSG_ERR_LINK_PREDECESSOR},
        {"ERR link storage; restart inspection required",MSG_ERR_LINK_STORAGE},
        {"ERR draft line",MSG_ERR_DRAFT_LINE},
        {"ERR preferences",MSG_ERR_PREFS},
        {"ERR pager size",MSG_ERR_PAGER_SIZE},
        {"ERR search query",MSG_ERR_SEARCH_QUERY},
        {"ERR search cursor",MSG_ERR_SEARCH_CURSOR},
        {"ERR login required",MSG_ERR_LOGIN},
        {"ERR board unavailable",MSG_ERR_BOARD},
        {"ERR posting denied",MSG_ERR_POST},
        {"ERR no article",MSG_ERR_ARTICLE},
        {"ERR no parent",MSG_ERR_PARENT},
        {"ERR no current article",MSG_ERR_CURRENT},
        {"ERR no neighbour",MSG_ERR_NEIGHBOUR},
        {"ERR authentication",MSG_ERR_AUTH},
        {"ERR logout first",MSG_ERR_LOGOUT},
        {"ERR try later",MSG_ERR_LATER},
        {"ERR username",MSG_ERR_NAME},
        {"ERR password 8..128 bytes",MSG_ERR_PASSWORD},
        {"ERR title 1..80 bytes, no controls",MSG_ERR_TITLE},
        {"ERR empty body",MSG_ERR_EMPTY},
        {"ERR body limit 262144 bytes; cancel or finish",MSG_ERR_BODY},
        {"ERR control character",MSG_ERR_CONTROL},
        {"ERR read-state storage",MSG_ERR_SEEN},
        {"ERR storage uncertain; restart inspection required",MSG_ERR_UNCERTAIN},
        {"ERR storage unavailable",MSG_ERR_STORAGE},
        {"ERR board busy",MSG_ERR_BUSY},
        {"ERR board full",MSG_ERR_FULL},
        {"ERR members disabled",MSG_ERR_MEMBERS},
        {"ERR no board",MSG_ERR_DISABLED},
        {"ERR tree cursor",MSG_ERR_CURSOR},
        {"ERR article number",MSG_ERR_NUMBER},
        {"ERR board number",MSG_ERR_BOARD_NUMBER},
        {"ERR tree syntax",MSG_ERR_TREE},
        {"ERR command",MSG_ERR_COMMAND},
        {"ERR draft",MSG_ERR_DRAFT},
        {"ERR member posting needs RTBOARD3",MSG_ERR_V3},
        {"ERR multiple boards need RTBOARD4",MSG_ERR_V4},
        {"ERR reply syntax",MSG_ERR_REPLY},
        {"ERR WAIT 1..5000",MSG_ERR_WAIT},
        {"ERR admin required",MSG_ERR_ADMIN},
        {"ERR article hidden",MSG_ERR_HIDDEN},
        {"ERR moderation storage uncertain",MSG_ERR_MOD_UNCERTAIN},
        {"ERR moderation storage unavailable",MSG_ERR_MOD_STORAGE},
        {"ERR session revoked",MSG_ERR_REVOKED},
        {"ERR no member",MSG_ERR_MEMBER},
        {"ERR member number",MSG_ERR_MEMBER_NUMBER},
        {"ERR member not found",MSG_ERR_CHAT_MEMBER},
        {"ERR protected member",MSG_ERR_PROTECTED},
        {"ERR audit cursor",MSG_ERR_AUDIT_CURSOR},
        {"ERR audit filter",MSG_ERR_AUDIT_FILTER},
        {"ERR vault disabled",MSG_ERR_VAULT_DISABLED}, {"ERR vault denied",MSG_ERR_VAULT_DENIED},
        {"ERR no file",MSG_ERR_NO_FILE}, {"ERR vault busy",MSG_ERR_VAULT_BUSY},
        {"ERR vault syntax",MSG_ERR_VAULT_SYNTAX}, {"ERR vault storage",MSG_ERR_VAULT_STORAGE},
        {"ERR binary negotiation",MSG_ERR_BINARY_NEGOTIATION}, {"ERR vault client",MSG_ERR_VAULT_CLIENT}, {"ERR vault limit",MSG_ERR_VAULT_LIMIT}
    };
    for(size_t i=0;i<sizeof(errors)/sizeof(errors[0]);i++) if(!strcmp(p,errors[i].wire)) return ui_text(u,errors[i].message);
    if(!strncmp(p,"ERR application storage; account ",strlen("ERR application storage; account ")))return ui_text(u,MSG_ERR_APPLICATION_STORAGE);
    return NULL;
}
static void report_stamp(unsigned long long value,char out[40]) {
    time_t at=(time_t)value;struct tm tm;
#ifdef _WIN32
    int ok=value && !localtime_s(&tm,&at);
#else
    int ok=value && localtime_r(&at,&tm)!=NULL;
#endif
    if(!ok || !strftime(out,40,date_pattern(),&tm))strcpy(out,"-");
}
static int report_format(Ui *u,const char *wire,Text *t) {
    unsigned id,connection,user,file,bytes;unsigned long long value,elapsed,at;char stamp[40],subject[80],protocol[12],kind;int used=0;
    if(!strcmp(wire,"MENU MODE SAVED\r\n") || !strcmp(wire,"MENU MODE CHANGED\r\n")) { add(t,"%s",u->language==LANGUAGE_JA?"メニューの案内量を変更しました。\r\n":"Menu guidance changed.\r\n");return 1; }
    if(!strcmp(wire,"BOARD GUEST SAVED\r\n")) { add(t,"%s",u->language==LANGUAGE_JA?"ゲスト投稿設定を保存しました。\r\n":"Guest posting setting saved.\r\n");return 1; }
    if(!strcmp(wire,"BOARD TREE DELETED\r\n")) { add(t,"%s",u->language==LANGUAGE_JA?"ボードと配下を削除し、記事を取り消しました。\r\n":"Board tree deleted; articles withdrawn.\r\n");return 1; }
    if(sscanf(wire,"AUTHOR CHANGED %u %u",&id,&user)==2) { add(t,u->language==LANGUAGE_JA?"記事 %u の投稿者を会員 %u に変更しました。\r\n":"Article %u author changed to member %u.\r\n",id,user);return 1; }
    if(!strncmp(wire,"GUEST HANDLE ",13)) { add(t,u->language==LANGUAGE_JA?"接続中ハンドル: %s":"Connection handle: %s",wire+13);return 1; }
    if(!strncmp(wire,"GAMES\r\n",7)) {
        add(t,"%s",u->language==LANGUAGE_JA?"外部ゲーム（ゲームメニューでは番号、直接起動は DOOR 番号）\r\n":"Native games (number in game menu; DOOR id for direct launch)\r\n");
        if(!strncmp(wire+7,"END GAMES",9))add(t,"%s",u->language==LANGUAGE_JA?"現在利用できるゲームはありません。\r\n":"No games are available for this account.\r\n");
        const char *p=wire+7;while(!strncmp(p,"GAME ",5)) { unsigned game,seconds;int n=0;if(sscanf(p,"GAME %u %u %n",&game,&seconds,&n)!=2)break;const char *end=strstr(p+n,"\r\n");if(!end)break;add(t,u->language==LANGUAGE_JA?"%u: %.*s（制限 %u 秒）\r\n":"%u: %.*s (limit %u seconds)\r\n",game,(int)(end-p-n),p+n,seconds);p=end+2; }
        return 1;
    }
    if(!strcmp(wire,"DOCUMENT EDIT\r\n")) { add(t,"%s",u->language==LANGUAGE_JA?"文章を編集します。/SAVE で保存、/CANCEL で中止。\r\n":"Edit document. /SAVE to save; /CANCEL to cancel.\r\n");return 1; }
    if(!strcmp(wire,"BUFFER IMPORTED\r\n")) { add(t,"%s",u->language==LANGUAGE_JA?"共通編集バッファを取り込みました。\r\n":"Shared editing buffer imported.\r\n");return 1; }
    if(!strcmp(wire,"GROUP MOVED\r\n")) { add(t,"%s",u->language==LANGUAGE_JA?"グループ所属を移しました。ボードの権限設定は別途変更してください。\r\n":"Group memberships moved. Update board access rules separately.\r\n");return 1; }
    if(sscanf(wire,"FILE OWNER CHANGED %u",&id)==1) { add(t,u->language==LANGUAGE_JA?"ファイル %u の登録者を変更しました。\r\n":"Changed owner of file %u.\r\n",id);return 1; }
    if(sscanf(wire,"FILE ATTRIBUTE CHANGED %u",&id)==1) { add(t,u->language==LANGUAGE_JA?"ファイル %u のテキスト属性を変更しました。\r\n":"Changed text attribute of file %u.\r\n",id);return 1; }
    if(sscanf(wire,"FILETEXT %u %u",&id,&bytes)==2) {
        const char *body=strstr(wire,"\r\n"),*end=NULL,*next=wire;while((next=strstr(next,"\r\nEND TEXT NEXT "))!=NULL) { end=next;next+=2; }
        if(body && end && end>=body+2) { add(t,u->language==LANGUAGE_JA?"ファイル %u（位置 %u バイト）\r\n":"File %u (byte offset %u)\r\n",id,bytes);add(t,"%.*s\r\n",(int)(end-(body+2)),body+2);if(sscanf(end+2,"END TEXT NEXT %u",&id)==1 && id)add(t,ui_text(u,MSG_REPORT_NEXT),id);return 1; }
    }
    if(!strcmp(wire,"BOARD OPTIONS SAVED\r\n")) { add(t,"%s",ui_text(u,MSG_BOARD_OPTIONS_SAVED));return 1; }
    if(!strcmp(wire,"BOARD PASSWORD SAVED\r\n")) { add(t,"%s",ui_text(u,MSG_BOARD_PASSWORD_SAVED));return 1; }
    if(!strcmp(wire,"BOARD PASSWORD\r\n")) { add(t,"%s",ui_text(u,MSG_BOARD_PASSWORD_PROMPT));return 1; }
    if(!strcmp(wire,"BOARD INFO EDIT\r\n")) { add(t,"%s",ui_text(u,MSG_BOARD_INFO_EDIT));return 1; }
    if(!strcmp(wire,"BOARD INFO SAVED\r\n")) { add(t,"%s",ui_text(u,MSG_BOARD_INFO_SAVED));return 1; }
    if(!strcmp(wire,"ARTICLE IMPORTED TO BUFFER\r\n")) { add(t,"%s",ui_text(u,MSG_ARTICLE_IMPORTED));return 1; }
    if(!strcmp(wire,"BOARD DELETED\r\n")) { add(t,"%s",ui_text(u,MSG_BOARD_DELETED));return 1; }

    if(!strncmp(wire,"BOARDOPTIONS ",13)) { add(t,ui_text(u,MSG_BOARD_OPTIONS),wire+13);return 1; }
    if(sscanf(wire,"THREAD MOVED %u %u",&id,&user)==2) { add(t,ui_text(u,MSG_THREAD_MOVED),id,user);return 1; }
    if(sscanf(wire,"POST SAVED LOG FAILED %u",&id)==1) { add(t,ui_text(u,MSG_POST_LOG_FAILED),id);return 1; }
    if(sscanf(wire,"BOARDACCESS %u",&id)==1) {
        add(t,ui_text(u,MSG_BOARD_ACCESS),id);const char *p=strstr(wire,"\r\n");if(p)p+=2;
        while(p && *p) { unsigned row,article,actor;char action;unsigned long long when;
            if(sscanf(p,"ACCESSROW %u %u %u %c %llu",&row,&article,&actor,&action,&when)==5) { report_stamp(when,stamp);add(t,ui_text(u,MSG_BOARD_ACCESS_ROW),row,stamp,actor,action=='P'?ui_text(u,MSG_ACCESS_POST):ui_text(u,MSG_ACCESS_READ),article); }
            if(sscanf(p,"END NEXT %u",&id)==1 && id)add(t,ui_text(u,MSG_REPORT_NEXT),id);
            p=strstr(p,"\r\n");if(p)p+=2;
        }return 1;
    }
    if(sscanf(wire,"PASSWORD RESET %u",&id)==1) { add(t,ui_text(u,MSG_PASSWORD_RESET),id);return 1; }
    if(sscanf(wire,"SIGOP SET %u %u",&id,&user)==2) { add(t,ui_text(u,MSG_SIGOP_SET),id,user);return 1; }
    if(!strncmp(wire,"SIGOPS\r\n",8)) {
        add(t,"%s",ui_text(u,MSG_SIGOPS_HEADER));const char *p=wire+8;
        while(*p) { if(sscanf(p,"SIGOP %u %u",&id,&user)==2)add(t,ui_text(u,MSG_SIGOP_ROW),id,user);const char *end=strstr(p,"\r\n");if(!end)break;p=end+2; }
        return 1;
    }
    if(sscanf(wire,"BOARD UPDATED %u",&id)==1) { add(t,ui_text(u,MSG_BOARD_UPDATED),id);return 1; }
    if(!strncmp(wire,"FILEBOARD UPDATED",17)) { add(t,"%s",ui_text(u,MSG_FILEBOARD_UPDATED));return 1; }
    if(!strncmp(wire,"FILEBOARDSETTING ",17)) { add(t,ui_text(u,MSG_FILEBOARD_SETTING),wire+17);return 1; }
    if(!strncmp(wire,"BOARDSETTING ",13)) { add(t,ui_text(u,MSG_BOARD_SETTING),wire+13);return 1; }
    if(sscanf(wire,"USERSTATS %u LOGINS %llu SECONDS %llu LAST %llu",&user,&value,&elapsed,&at)==4) {
        report_stamp(at,stamp);add(t,ui_text(u,MSG_USER_STATS),user,value,elapsed,stamp);return 1;
    }
    if(!strncmp(wire,"USERTABLE ",10)) {
        add(t,u->language==LANGUAGE_JA?"接続  会員  ログイン / 終了 (%s)  秒数  回数  接続方式\r\n":"Connection / member / login / logout (%s) / seconds / count / transport\r\n",date_zone());
        const char *p=strstr(wire,"\r\n");if(p)p+=2;
        while(p && *p) { unsigned long long login,logout,seconds,count;char transport[12],start[40],end[40];
            if(sscanf(p,"SESSIONROW %u %u %llu %llu %llu %llu %11s",&connection,&user,&login,&logout,&seconds,&count,transport)==7) { report_stamp(login,start);report_stamp(logout,end);add(t,"%u / %u / %s / %s / %llu / %llu / %s\r\n",connection,user,start,logout?end:u->language==LANGUAGE_JA?"接続中":"online",seconds,count,transport); }
            if(sscanf(p,"END NEXT %u",&id)==1 && id)add(t,ui_text(u,MSG_REPORT_NEXT),id);
            p=strstr(p,"\r\n");if(p)p+=2;
        }return 1;
    }
    MessageId header;
    if(!strncmp(wire,"USERLOG\r\n",9))header=MSG_USERLOG_HEADER;
    else if(!strncmp(wire,"FILEHISTORY\r\n",13))header=MSG_DOWNLOAD_HISTORY_HEADER;
    else if(!strncmp(wire,"FILESTATS\r\n",11))header=MSG_DOWNLOAD_STATS_HEADER;
    else if(!strncmp(wire,"BOARDLOG\r\n",10))header=MSG_BOARDLOG_HEADER;
    else return 0;
    add(t,"%s",ui_text(u,header));const char *p=strstr(wire,"\r\n");
    while(p && *(p+=2)) {
        char row[1200];size_t n=strcspn(p,"\r\n");if(n>=sizeof(row))return 0;memcpy(row,p,n);row[n]=0;
        if(sscanf(row,"USERROW %u %u %u %c %llu %llu",&id,&connection,&user,&kind,&at,&elapsed)==6) {
            MessageId label=kind=='C'?MSG_LOG_CONNECT:kind=='L'?MSG_LOG_LOGIN:kind=='O'?MSG_LOG_LOGOUT:kind=='X'?MSG_LOG_CRASH:MSG_LOG_END;
            report_stamp(at,stamp);add(t,ui_text(u,MSG_USERLOG_ROW),id,connection,user,ui_text(u,label),stamp,elapsed);
        } else if(sscanf(row,"DOWNLOADCOUNT %u %llu",&file,&value)==2)add(t,ui_text(u,MSG_DOWNLOAD_COUNT),file,value);
        else if(sscanf(row,"DOWNLOADROW %u %u %79s %llu %u %11s",&id,&file,subject,&at,&bytes,protocol)==6) { report_stamp(at,stamp);add(t,ui_text(u,MSG_DOWNLOAD_ROW),id,file,subject,stamp,bytes);add(t,"  %s\r\n",protocol); }
        else if(sscanf(row,"BOARDCHANGE %u %u %llu %n",&id,&user,&at,&used)==3 && used) { report_stamp(at,stamp);add(t,ui_text(u,MSG_BOARDLOG_ROW),id,user,stamp,row+used); }
        else if(sscanf(row,"END NEXT %u",&id)==1) { if(id)add(t,ui_text(u,MSG_REPORT_NEXT),id);else add(t,"%s",ui_text(u,MSG_REPORT_END)); }
        p=strstr(p,"\r\n");
    }
    return 1;
}
int ui_format(Ui *u,const char *wire,char *out,size_t size) {
    Text t={out,size,0,1}; if(!size) return 0; out[0]=0;
    char first[1024]; size_t n=strcspn(wire,"\r\n"); if(n>=sizeof(first)) n=sizeof(first)-1;
    memcpy(first,wire,n); first[n]=0;
    if(!strncmp(first,"ERR ",4)) {
        const char *localized=error_text(u,first);
        add(&t,"\r\n%s\r\n",localized?localized:first); return t.ok;
    }
    if(!strcmp(first,"CHECKPOINT SAVED")) { add(&t,"%s\r\n",ui_text(u,MSG_CHECKPOINT_SAVED));return t.ok; }
    if(!strcmp(first,"MAIL IMPORTED TO BUFFER")) { add(&t,"%s\r\n",ui_text(u,MSG_MAIL_BUFFER_IMPORTED));return t.ok; }
    if(!strcmp(first,"SIGOP GROUP SAVED")) { add(&t,"%s\r\n",ui_text(u,MSG_SIGOP_GROUP_SAVED));return t.ok; }
    if(!strcmp(first,"BOARD GROUP MOVED")) { add(&t,"%s\r\n",ui_text(u,MSG_BOARD_GROUP_MOVED));return t.ok; }
    if(report_format(u,wire,&t))return t.ok;
    unsigned service_id;
    if(sscanf(first,"FILE DELETED %u",&service_id)==1) { add(&t,ui_text(u,MSG_FILE_DELETED),service_id);return t.ok; }
    if(sscanf(first,"DISCONNECTED %u",&service_id)==1) { add(&t,ui_text(u,MSG_DISCONNECTED),service_id);return t.ok; }
    if(sscanf(first,"SHUTDOWN SCHEDULED %u",&service_id)==1) { add(&t,ui_text(u,MSG_STOP_SCHEDULED),service_id);return t.ok; }
    if(!strcmp(first,"ANNOUNCE SENT")) { add(&t,"%s",ui_text(u,MSG_ANNOUNCE_SENT));return t.ok; }
    if(!strcmp(first,"SHUTDOWN CANCELLED")) { add(&t,"%s\r\n",ui_text(u,MSG_SHUTDOWN_CANCELLED));return t.ok; }
    if(!strcmp(first,"CONNECTIONS")) {
        add(&t,"%s",ui_text(u,MSG_CONNECTIONS_HEADER));const char *row=strstr(wire,"\r\n");
        while(row && *(row+=2)) { unsigned long id;char name[33],mode[20],protocol[16],peer[64];unsigned long long age,idle,rx,tx;size_t queued;unsigned cols,lines;
            if(sscanf(row,"CONNECTION %lu %32s %19s %15s %63s %llu %llu %llu %llu %zu %u %u",&id,name,mode,protocol,peer,&age,&idle,&rx,&tx,&queued,&cols,&lines)==12) {
                add(&t,"%lu  %s  %s / %s  %s\r\n",id,name,protocol,mode,peer);
                add(&t,u->language==LANGUAGE_JA?"  接続 %llu秒 / 無入力 %llu秒 / 受信 %lluB / 送信 %lluB / 送信待ち %zuB / %ux%u\r\n":"  Connected %llus / idle %llus / RX %lluB / TX %lluB / queued %zuB / %ux%u\r\n",age,idle,rx,tx,queued,cols,lines);
            }row=strstr(row,"\r\n"); }
        add(&t,"%s",ui_text(u,MSG_CONNECTIONS_END));return t.ok;
    }
    unsigned removed_member,removed_kind;
    if(sscanf(first,"MEMBER REMOVED %u KIND %u",&removed_member,&removed_kind)==2) { add(&t,u->language==LANGUAGE_JA?"会員 #%u: %s。ID・ログイン名は再利用しません。\r\n":"Member #%u: %s. ID and login name remain reserved.\r\n",removed_member,u->language==LANGUAGE_JA?(removed_kind==1?"申請を却下しました":removed_kind==2?"申請を削除しました":removed_kind==3?"退会しました":"削除しました"):(removed_kind==1?"application rejected":removed_kind==2?"application deleted":removed_kind==3?"membership retired":"deleted"));return t.ok; }
    if(!strcmp(first,"APPLICATION SAVED")) { add(&t,"%s",ui_text(u,MSG_APPLICATION_SAVED));return t.ok; }
    unsigned rights_id,level;char groups[401],rights_status[16];
    if(sscanf(first,"RIGHTS %u LEVEL %u GROUPS %400s STATUS %15s",&rights_id,&level,groups,rights_status)==4) {
        const char *state=!strcmp(rights_status,"pending")?ui_text(u,MSG_RIGHTS_PENDING):!strcmp(rights_status,"suspended")?ui_text(u,MSG_RIGHTS_SUSPENDED):ui_text(u,MSG_RIGHTS_ACTIVE);
        add(&t,ui_text(u,MSG_RIGHTS),rights_id,level,groups,state);return t.ok;
    }
    if(!strcmp(first,"APPLICATIONS")) {
        add(&t,"%s",ui_text(u,MSG_APPLICATIONS));const char *row=strstr(wire,"\r\n");
        while(row && *(row+=2)) {
            unsigned id;char name[33];
            if(sscanf(row,"APPLICATION %u %32s",&id,name)==2)add(&t,ui_text(u,MSG_APPLICATION_ROW),id,name);
            else if(sscanf(row,"END NEXT %u",&id)==1) { if(id)add(&t,ui_text(u,MSG_APPLICATION_NEXT),id);break; }
            row=strstr(row,"\r\n");
        }
        return t.ok;
    }
    if(!strcmp(first,"NEXTBOARD NONE")) { add(&t,"%s",ui_text(u,MSG_NEXTBOARD_NONE)); return t.ok; }
    unsigned ext_id=0,ext_parent=0; int ext_used=0;
    if(sscanf(first,"WITHDRAW CONFIRM %u",&ext_id)==1) { add(&t,ui_text(u,MSG_WITHDRAW_CONFIRM),ext_id,ext_id); return t.ok; }
    if(sscanf(first,"WITHDRAW SAVED %u",&ext_id)==1) { add(&t,ui_text(u,MSG_WITHDRAW_SAVED),ext_id); return t.ok; }
    if(sscanf(first,"RETITLED %u",&ext_id)==1) { add(&t,ui_text(u,MSG_RETITLED),ext_id); return t.ok; }
    if(!strcmp(first,"SCAN END")) { add(&t,"%s",ui_text(u,MSG_SCAN_END)); return t.ok; }
    if(!strcmp(first,"PROFILE SAVED")) { add(&t,"%s",ui_text(u,MSG_PROFILE_SAVED)); return t.ok; }
    if(sscanf(first,"INFO %u %n",&ext_id,&ext_used)==1 && ext_used) {
        const char *desc=strstr(wire,"\r\nDESCRIPTION "); desc=desc?desc+14:"";
        add(&t,ui_text(u,MSG_INFO),ext_id,first+ext_used,(int)strcspn(desc,"\r\n"),desc);
        const char *body=strstr(desc,"\r\n");if(body) { body+=2;size_t len=strlen(body);if(len>=10 && !strcmp(body+len-10,"END INFO\r\n"))add(&t,"%.*s",(int)(len-10),body); }return t.ok;
    }
    ext_used=0;
    if(sscanf(first,"PROFILE %u %n",&ext_id,&ext_used)==1 && ext_used) {
        const char *handle=strstr(wire,"\r\nHANDLE "),*bio=strstr(wire,"\r\nBIO ");
        handle=handle?handle+9:""; bio=bio?bio+6:"";
        add(&t,ui_text(u,MSG_PROFILE),ext_id,first+ext_used,(int)strcspn(handle,"\r\n"),handle,(int)strcspn(bio,"\r\n"),bio);
        static const char *keys[]={"LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT"};
        static const MessageId labels[]={MSG_PROFILE_LOCATION,MSG_PROFILE_OCCUPATION,MSG_PROFILE_INTERESTS,MSG_PROFILE_WEBSITE,MSG_PROFILE_CONTACT};
        for(unsigned i=0;i<5;i++) { char key[32];snprintf(key,sizeof(key),"\r\n%s ",keys[i]);const char *value=strstr(wire,key);if(value) { value+=strlen(key);add(&t,"%s: %.*s\r\n",ui_text(u,labels[i]),(int)strcspn(value,"\r\n"),value); } }
        static const char *more[]={"MACHINE","BIRTHDAY","REALNAME","KANA","ADDRESS","PHONE"};
        static const char *ja[]={"使用機種","誕生日","氏名","カナ氏名","住所","電話番号"};
        for(unsigned i=0;i<6;i++) { char key[32];snprintf(key,sizeof(key),"\r\n%s ",more[i]);const char *v=strstr(wire,key);if(v) { v+=strlen(key);add(&t,"%s: %.*s\r\n",u->language==LANGUAGE_JA?ja[i]:more[i],(int)strcspn(v,"\r\n"),v); } }
        const char *longbio=strstr(wire,"\r\nPROFILEBODY ");unsigned len=0;int off=0;
        if(longbio && sscanf(longbio+2,"PROFILEBODY %u\r\n%n",&len,&off)==1 && off && len<=strlen(longbio+2+off))add(&t,"%.*s",(int)len,longbio+2+off);
        return t.ok;
    }
    if(sscanf(first,"WITHDRAWN %u PARENT %u",&ext_id,&ext_parent)==2) {
        u->view=UI_ARTICLE; u->next=0; add(&t,ui_text(u,MSG_WITHDRAWN),ext_id,ext_parent); return t.ok;
    }
    unsigned revision=0;
    if(sscanf(first,"DRAFT STORED %u",&revision)==1) { add(&t,ui_text(u,MSG_DRAFT_STORED),revision); return t.ok; }
    if(!strcmp(first,"DRAFT NONE")) { add(&t,"%s",ui_text(u,MSG_DRAFT_NONE)); return t.ok; }
    if(!strcmp(first,"DRAFT RESUMED")) { add(&t,"%s",ui_text(u,MSG_DRAFT_RESUMED)); return t.ok; }
    if(!strcmp(first,"DRAFT REMOVED")) { add(&t,"%s",ui_text(u,MSG_DRAFT_REMOVED)); return t.ok; }
    unsigned draft_count=0;
    if(sscanf(first,"DRAFT LINES %u",&draft_count)==1) {
        add(&t,ui_text(u,MSG_DRAFT_LINES),draft_count);
        const char *p=strstr(wire,"\r\n"); if(!p) return 0; p+=2;
        while(*p) {
            size_t len=strcspn(p,"\r\n");
            if(len==9 && !strncmp(p,"END DRAFT",9)) break;
            if(len>=6 && !strncmp(p,"TITLE ",6)) add(&t,ui_text(u,MSG_DRAFT_TITLE), (int)(len-6),p+6);
            else if(len>=5 && !strncmp(p,"LINE ",5)) add(&t,"%.*s\r\n",(int)(len-5),p+5);
            p+=len; if(*p=='\r') p++; if(*p=='\n') p++;
        }
        add(&t,"%s",ui_text(u,MSG_DRAFT_HINT)); return t.ok;
    }
    unsigned id=0,parent=0; char author[33],status[16];
    if(sscanf(first,"MEMBER %u %32s %15s",&id,author,status)==3) {
        add(&t,ui_text(u,MSG_MEMBER_ROW),id,author,ui_text(u,!strcmp(status,"pending")?MSG_RIGHTS_PENDING:!strcmp(status,"suspended")?MSG_MEMBER_SUSPENDED:MSG_MEMBER_ACTIVE)); return t.ok;
    }
    if(sscanf(first,"HIDDEN %u PARENT %u",&id,&parent)==2) {
        u->view=UI_ARTICLE; u->next=0;
        add(&t,ui_text(u,MSG_HIDDEN_ARTICLE),id,parent); return t.ok;
    }
    int preview=sscanf(first,"PREVIEW PARENT %u AUTHOR %32s",&parent,author)==2;
    if(preview || sscanf(first,"ARTICLE %u PARENT %u AUTHOR %32s",&id,&parent,author)==3) {
        const char *title=strstr(wire,"\r\nTITLE "),*body=strstr(wire,"\r\nBODY ");
        if(!title || !body) return 0;
        title+=8; size_t title_len=strcspn(title,"\r\n"),body_len=0;
        if(sscanf(body,"\r\nBODY %zu",&body_len)!=1) return 0;
        body=strstr(body+2,"\r\n"); if(!body || body_len>CONTENT_BODY) return 0; body+=2;
        if(strlen(body)<body_len) return 0;
        if(preview) {
            add(&t,ui_text(u,MSG_PREVIEW),parent,author,(int)title_len,title,(int)body_len,body); return t.ok;
        }
        u->view=UI_ARTICLE; u->next=0;
        if(strstr(first," HIDDEN 1")) add(&t,"%s",ui_text(u,MSG_INSPECT_HIDDEN));
        add(&t,ui_text(u,MSG_ARTICLE),
               id,parent,author,(int)title_len,title,(int)body_len,body);
        return t.ok;
    }
    char pref_action[16],pref_lang[3],pref_pager[4];
    if(!strcmp(first,"PREFS NONE")) { add(&t,"%s",ui_text(u,MSG_PREFS_NONE)); return t.ok; }
    if(sscanf(first,"PREFS %15s %2s %3s",pref_action,pref_lang,pref_pager)==3) {
        MessageId label=!strcmp(pref_action,"SAVED")?MSG_PREFS_SAVED:!strcmp(pref_action,"LOADED")?MSG_PREFS_LOADED:MSG_PREFS_STORED;
        add(&t,ui_text(u,label),pref_lang,pref_pager); return t.ok;
    }
    unsigned file_id,file_size;char file_hash[65];
    if(sscanf(first,"FILE EDITED %u",&file_id)==1) { add(&t,ui_text(u,MSG_FILE_EDITED),file_id);return t.ok; }
    if(sscanf(first,"FILE RESTORED %u",&file_id)==1) { add(&t,ui_text(u,MSG_FILE_RESTORED),file_id);return t.ok; }
    if(sscanf(first,"FILE CATEGORY %u",&file_id)==1) { add(&t,ui_text(u,MSG_FILE_CATEGORY_SET),file_id);return t.ok; }
    if(sscanf(first,"FILE HIDDEN %u",&file_id)==1) { add(&t,ui_text(u,MSG_FILE_HIDDEN),file_id);return t.ok; }
    if(sscanf(first,"FILE %u %u %64s",&file_id,&file_size,file_hash)==3) {
        const char *name=strstr(wire,"\r\nNAME "),*description=strstr(wire,"\r\nDESCRIPTION ");
        if(!name || !description)return 0;
        name+=7;description+=14;
        if(u->view!=UI_FILES) { u->view=UI_FILES;u->next=0;u->file_mode=0; }
        add(&t,ui_text(u,MSG_FILE_INFO),file_id,file_size,(int)strcspn(name,"\r\n"),name,(int)strcspn(description,"\r\n"),description,file_hash);
        const char *category=strstr(wire,"\r\nCATEGORY ");
        if(category) { category+=11;add(&t,ui_text(u,MSG_FILE_CATEGORY),(int)strcspn(category,"\r\n"),category); }
        const char *protocol=strstr(wire,"\r\nPROTOCOL ");if(protocol) { protocol+=11;add(&t,u->language==LANGUAGE_JA?"登録時の方式: %.*s\r\n":"Registration protocol: %.*s\r\n",(int)strcspn(protocol,"\r\n"),protocol); }
        return t.ok;
    }
    int list=0;
    if(!strcmp(first,"FILES")) { u->view=UI_FILES;u->file_mode=0;list=1;add(&t,"%s",ui_text(u,MSG_FILES)); }
    else if(!strncmp(first,"FILESEARCH ",11) || !strncmp(first,"FILESIN ",8) || !strncmp(first,"FILENEWS ",9)) {
        u->view=UI_FILES;u->file_mode=!strncmp(first,"FILENEWS ",9)?3:!strncmp(first,"FILESEARCH ",11)?1:2;
        snprintf(u->file_query,sizeof(u->file_query),"%.129s",first+(u->file_mode==3?9:u->file_mode==1?11:8));list=1;
        add(&t,ui_text(u,u->file_mode==3?MSG_FILE_NEWS:u->file_mode==1?MSG_FILE_SEARCH:MSG_FILE_IN),u->file_query);
    }
    else if(!strcmp(first,"FILEAUDIT")) { u->view=UI_FILEAUDIT;list=1;add(&t,"%s",ui_text(u,MSG_VAUDIT)); }
    else if(!strcmp(first,"FILEBOARDS")) { u->view=UI_FILEBOARDS;list=1;add(&t,"%s",ui_text(u,MSG_VBOARDS)); }
    else if(!strcmp(first,"FILECATEGORIES")) { u->view=UI_FILECATEGORIES;list=1;add(&t,"%s",ui_text(u,MSG_VCATEGORIES)); }
    else if(!strncmp(first,"NEWS ",5)) { u->view=UI_NEWS; snprintf(u->find_query,sizeof(u->find_query),"%.159s",first+5); list=1; add(&t,ui_text(u,MSG_NEWS),u->find_query); }
    else if(!strncmp(first,"FIND ",5)) { u->view=UI_FIND; snprintf(u->find_query,sizeof(u->find_query),"%.159s",first+5); list=1; add(&t,ui_text(u,MSG_FIND),u->find_query); }
    else if(!strncmp(first,"PROFILES ",9)) { u->view=UI_PROFILES; snprintf(u->profile_query,sizeof(u->profile_query),"%.80s",first+9); list=1; add(&t,ui_text(u,MSG_PROFILES),u->profile_query); }
    else if(!strncmp(first,"SEARCH ",7)) { u->view=UI_SEARCH; snprintf(u->search,sizeof(u->search),"%.80s",first+7); list=1; add(&t,ui_text(u,MSG_SEARCH),u->search); }
    else if(sscanf(first,"AUDIT %u",&id)==1) {
        u->view=UI_AUDIT; list=1; u->audit_article=0; u->audit_actor=0;
        sscanf(first,"AUDIT %u ARTICLE %u ACTORID %u",&id,&u->audit_article,&u->audit_actor);
        add(&t,"%s",ui_text(u,MSG_AUDIT));
        if(u->audit_article) add(&t,ui_text(u,MSG_AUDIT_FILTER_ARTICLE),u->audit_article);
        if(u->audit_actor) add(&t,ui_text(u,MSG_AUDIT_FILTER_ACTOR),u->audit_actor);
    }
    else if(!strcmp(first,"MEMBERS")) { u->view=UI_MEMBERS; list=1; add(&t,"%s",ui_text(u,MSG_MEMBERS)); }
    else if(!strcmp(first,"BOARDS")) { u->view=UI_BOARDS; list=1; add(&t,ui_text(u,MSG_BOARDS)); }
    else if(!strncmp(first,"NOTES UNREAD ",13)) { u->view=UI_NOTES; u->notes_unread=1; list=1; add(&t,ui_text(u,MSG_NOTES_UNREAD),first+13); }
    else if(!strncmp(first,"NOTES ",6)) { u->view=UI_NOTES; u->notes_unread=0; list=1; add(&t,ui_text(u,MSG_NOTES),first+6); }
    else if(sscanf(first,"TREE %u",&id)==1) { u->view=UI_TREE; u->tree=id; list=1; add(&t,ui_text(u,MSG_TREE),id); }
    else if(!strncmp(first,"UNREAD ",7)) { u->view=UI_UNREAD; list=1; add(&t,ui_text(u,MSG_UNREAD),first+7); }
    else if(!strncmp(first,"LIST ",5)) { u->view=UI_LIST; list=1; add(&t,ui_text(u,MSG_LIST),first+5); }
    if(list) {
        u->next=0; int rows=0; const char *p=strstr(wire,"\r\n"); if(!p) return 0; p+=2;
        while(*p) {
            size_t len=strcspn(p,"\r\n"); char row[2048]; if(len>=sizeof(row)) return 0;
            memcpy(row,p,len); row[len]=0;
            if(sscanf(row,"END NEXT %u",&id)==1) { u->next=id; break; }
            rows++; int used=0; unsigned depth=0, count=0, latest=0; char field[40];
            unsigned actor=0,target=0,board=0; char date[40],name[33],action[16],unread[16];
            if((u->view==UI_FIND || u->view==UI_NEWS) && sscanf(row,"%u %u %u %n",&id,&board,&parent,&used)==3 && used) {
                add(&t,ui_text(u,MSG_FIND_ROW),id,board,parent,row+used);
            } else if(u->view==UI_FILES && sscanf(row,"FILEITEM %u %u %n",&id,&count,&used)==2 && used) {
                add(&t,ui_text(u,MSG_FILE_ROW),id,count,row+used);
            } else if(u->view==UI_FILES && !strncmp(row,"FILEDETAIL ",11)) {
                add(&t,"  %s\r\n",row+11);
            } else if(u->view==UI_FILEBOARDS && sscanf(row,"VBOARD %u %n",&count,&used)==1 && used) {
                add(&t,ui_text(u,MSG_VBOARD_ROW),row+used,ui_text(u,count?MSG_WRITABLE:MSG_READ_ONLY));
            } else if(u->view==UI_FILECATEGORIES && sscanf(row,"VCATEGORY %u %n",&count,&used)==1 && used) {
                add(&t,ui_text(u,MSG_VCATEGORY_ROW),count,row+used);
            } else if(u->view==UI_FILEAUDIT) {
                char actor_id[80],before[241],after[241],kind;
                if(sscanf(row,"VEVENT %u %u %39s %c\t%79[^\t]\t%240[^\t]\t%240[^\r\n]",&id,&target,date,&kind,actor_id,before,after)!=7)return 0;
                MessageId label=kind=='F'?MSG_VDELETE:kind=='B'?MSG_VPOLICY:kind=='E'?MSG_VEDIT:kind=='S'?MSG_VRESTORE:kind=='A'?MSG_VIMPORT:kind=='C'?MSG_VCATEGORY:kind=='R'?MSG_VRESERVE:kind=='P'?MSG_VPUBLISH:kind=='X'?MSG_VCANCEL:MSG_VHIDE;
                add(&t,ui_text(u,MSG_VAUDIT_ROW),id,target,!strcmp(date,"unknown")?ui_text(u,MSG_UNKNOWN_DATE):date,(kind=='Q'?"TRANSFER":ui_text(u,label)),!strcmp(actor_id,"unknown")?ui_text(u,MSG_VUNKNOWN):actor_id);
                if(kind=='Q')add(&t,"  Protocol: %s -> %s\r\n",before,after);
                if(kind=='C')add(&t,ui_text(u,MSG_VCATEGORY_CHANGE),before,after);
            } else if(u->view==UI_PROFILES && sscanf(row,"%u %32s %n",&id,name,&used)==2 && used) {
                add(&t,ui_text(u,MSG_PROFILE_ROW),id,name,row+used);
            } else if(sscanf(row,"WITHDRAWN %u PARENT %u DEPTH %u",&id,&parent,&depth)==3) {
                add(&t,ui_text(u,MSG_WITHDRAWN),id,parent);
            } else if(u->view==UI_AUDIT && sscanf(row,"EVENT %u %39s %u %32s %15s %u %u",&id,date,&actor,name,action,&target,&board)==7) {
                MessageId label=!strcmp(action,"CHAT_KICK")?MSG_AUDIT_CHAT_KICK:!strcmp(action,"WITHDRAW")?MSG_AUDIT_WITHDRAW:!strcmp(action,"RETITLE")?MSG_AUDIT_RETITLE:!strcmp(action,"AUTHOR")?MSG_AUDIT_AUTHOR:!strcmp(action,"HIDE")?MSG_AUDIT_HIDE:!strcmp(action,"RESTORE")?MSG_AUDIT_RESTORE:!strcmp(action,"SUSPEND")?MSG_AUDIT_SUSPEND:MSG_AUDIT_RESUME;
                const char *stamp=!strcmp(date,"unknown")?ui_text(u,MSG_UNKNOWN_DATE):date;
                if(board) add(&t,ui_text(u,MSG_AUDIT_ARTICLE_ROW),id,stamp,actor,name,ui_text(u,label),target,board);
                else add(&t,ui_text(u,MSG_AUDIT_MEMBER_ROW),id,stamp,actor,name,ui_text(u,label),target);
            } else if(u->view==UI_MEMBERS && sscanf(row,"MEMBER %u %32s %15s",&id,author,status)==3) {
                add(&t,ui_text(u,MSG_MEMBER_ROW),id,author,ui_text(u,!strcmp(status,"pending")?MSG_RIGHTS_PENDING:!strcmp(status,"suspended")?MSG_MEMBER_SUSPENDED:MSG_MEMBER_ACTIVE));
            } else if(sscanf(row,"HIDDEN %u PARENT %u DEPTH %u",&id,&parent,&depth)==3) {
                add(&t,ui_text(u,MSG_HIDDEN_ROW),(int)(u->view==UI_TREE?(depth>12?24:depth*2):0),"",id,parent);
            } else if(u->view==UI_BOARDS && sscanf(row," #%u parent=%u depth=%u write=%39s unread=%15s %n",&id,&parent,&depth,field,unread,&used)==5 && used) {
                add(&t,ui_text(u,MSG_BOARD_UNREAD_ROW),(int)(depth>12?24:depth*2),"",id,row+used,!strcmp(field,"yes")?ui_text(u,MSG_WRITABLE):ui_text(u,MSG_READ_ONLY),unread);
            } else if(u->view==UI_NOTES && sscanf(row,"#%u replies=%u latest=%u updated=%39s unread=%15s %n",&id,&count,&latest,field,unread,&used)==5 && used) {
                add(&t,ui_text(u,MSG_NOTE_ROW),id,row+used,count,latest,!strcmp(field,"unknown")?ui_text(u,MSG_UNKNOWN_DATE):field,unread);
            } else if(u->view==UI_TREE && sscanf(row," #%u parent=%u depth=%u %n",&id,&parent,&depth,&used)==3 && used) {
                add(&t,"%*s%u  %s%s",(int)(depth>12?24:depth*2),"",id,row+used,depth>12?"":"\r\n");
                if(depth>12) add(&t,ui_text(u,MSG_DEPTH),depth);
            } else if(u->view==UI_UNREAD && sscanf(row,"#%u root=%u %n",&id,&parent,&used)==2 && used) {
                add(&t,ui_text(u,MSG_UNREAD_ROW),id,row+used,parent);
            } else if((u->view==UI_LIST || u->view==UI_SEARCH) && sscanf(row,"%u %u %n",&id,&parent,&used)==2 && used) {
                add(&t,ui_text(u,MSG_LIST_ROW),id,row+used,parent);
            } else add(&t,"%s\r\n",row);
            p+=len; if(*p=='\r') p++; if(*p=='\n') p++;
        }
        if(!rows) add(&t,"%s",ui_text(u,u->view==UI_AUDIT?MSG_AUDIT_EMPTY:u->view==UI_SEARCH?MSG_SEARCH_EMPTY:MSG_EMPTY));
        if(u->view==UI_FILEAUDIT || u->view==UI_FILEBOARDS || u->view==UI_FILECATEGORIES) {
            add(&t,"%s",ui_text(u,u->view==UI_FILEAUDIT?MSG_VAUDIT_HINT:u->view==UI_FILEBOARDS?MSG_VBOARDS_HINT:MSG_VCATEGORIES_HINT));
            if(u->next)add(&t,"%s\r\n",ui_text(u,MSG_NEXT_PAGE));
            return t.ok;
        }
        add(&t,ui_text(u,MSG_LIST_HINT),u->view==UI_BOARDS?ui_text(u,MSG_SELECT_BOARD):(u->view==UI_MEMBERS || u->view==UI_PROFILES)?ui_text(u,MSG_MEMBER_SELECT):u->view==UI_AUDIT?ui_text(u,MSG_AUDIT_SELECT):u->view==UI_FILES?ui_text(u,MSG_FILE_SELECT):ui_text(u,MSG_READ_ARTICLE),u->next?ui_text(u,MSG_NEXT_PAGE):"");
        if(u->view==UI_AUDIT) add(&t,"%s",ui_text(u,MSG_AUDIT_NAV));
        return t.ok;
    }
    int used=0;
    if(sscanf(first,"BOARD %u %n",&id,&used)==1 && used) {
        snprintf(u->board_title,sizeof(u->board_title),"%.80s",first+used); u->next=0;
        add(&t,ui_text(u,MSG_BOARD_SELECTED),id,u->board_title);
    } else if(!strncmp(first,"LOGGED IN ",10)) { u->board_title[0]=0; u->next=0; u->view=UI_HOME; add(&t,ui_text(u,MSG_LOGGED_IN),first+10); }
    else if(!strcmp(first,"LOGGED OUT")) { u->board_title[0]=0; u->next=0; u->view=UI_HOME; add(&t,ui_text(u,MSG_LOGGED_OUT)); }
    else if(!strcmp(first,"CURRENT PASSWORD")) add(&t,"%s",ui_text(u,MSG_CURRENT_PASSWORD));
    else if(!strcmp(first,"NEW PASSWORD")) add(&t,"%s",ui_text(u,MSG_NEW_PASSWORD));
    else if(!strcmp(first,"CONFIRM PASSWORD")) add(&t,"%s",ui_text(u,MSG_CONFIRM_PASSWORD));
    else if(!strcmp(first,"PASSWORD CHANGED")) add(&t,"%s",ui_text(u,MSG_PASSWORD_CHANGED));
    else if(!strcmp(first,"PAGER ON")) add(&t,"%s",ui_text(u,MSG_PAGER_ON));
    else if(!strcmp(first,"PAGER OFF")) add(&t,"%s",ui_text(u,MSG_PAGER_OFF));
    else if(!strcmp(first,"PAGER SIZE SET")) add(&t,"%s",ui_text(u,MSG_PAGER_SIZE));
    else if(!strcmp(first,"PASSWORD")) add(&t,ui_text(u,MSG_PASSWORD));
    else if(!strcmp(first,"EDIT end with . or /CANCEL")) add(&t,ui_text(u,MSG_EDIT));
    else if(sscanf(first,"SAVED %u",&id)==1) { u->next=0; u->view=UI_ARTICLE; add(&t,ui_text(u,MSG_SAVED),id); }
    else if(sscanf(first,"SEEN %u",&id)==1) add(&t,ui_text(u,MSG_SEEN),id);
    else if(sscanf(first,"UNSEEN %u",&id)==1) add(&t,ui_text(u,MSG_UNSEEN),id);
    else if(!strcmp(first,"NO UNREAD")) add(&t,ui_text(u,MSG_NO_UNREAD));
    else if(!strcmp(first,"DRAFT LINE INSERTED")) add(&t,"%s",ui_text(u,MSG_DRAFT_INSERTED));
    else if(!strcmp(first,"DRAFT UNDONE")) add(&t,"%s",ui_text(u,MSG_DRAFT_UNDONE));
    else if(!strcmp(first,"DRAFT TITLE CHANGED")) add(&t,"%s",ui_text(u,MSG_DRAFT_TITLE_CHANGED));
    else if(!strcmp(first,"DRAFT LINE REPLACED")) add(&t,"%s",ui_text(u,MSG_DRAFT_REPLACED));
    else if(!strcmp(first,"DRAFT LINE DELETED")) add(&t,"%s",ui_text(u,MSG_DRAFT_DELETED));
    else if(!strcmp(first,"PREVIEW CLOSED")) add(&t,"%s",ui_text(u,MSG_PREVIEW_CLOSED));
    else if(!strcmp(first,"CANCELLED")) add(&t,ui_text(u,MSG_CANCELLED));
    else if(!strcmp(first,"BYE")) add(&t,ui_text(u,MSG_BYE));
    else if(sscanf(first,"ACCOUNT %u %32s",&id,author)==2) add(&t,ui_text(u,MSG_ACCOUNT),id,author);
    else if(sscanf(first,"HIDDEN SAVED %u",&id)==1) add(&t,ui_text(u,MSG_HIDE_SAVED),id);
    else if(sscanf(first,"SUSPENDED %u",&id)==1) add(&t,ui_text(u,MSG_SUSPENDED),id);
    else if(sscanf(first,"RESUMED %u",&id)==1) add(&t,ui_text(u,MSG_RESUMED),id);
    else if(sscanf(first,"RESTORED %u",&id)==1) add(&t,ui_text(u,MSG_RESTORED),id);
    else if(!strcmp(first,"ROLE admin")) add(&t,"%s",ui_text(u,MSG_ROLE_ADMIN));
    else if(!strcmp(first,"ROLE member")) add(&t,"%s",ui_text(u,MSG_ROLE_MEMBER));
    else if(!strcmp(first,"ROLE guest")) add(&t,"%s",ui_text(u,MSG_ROLE_GUEST));
    else if(!strcmp(first,"WAITING")) add(&t,"%s",ui_text(u,MSG_WAITING));
    else if(!strcmp(first,"AWAKE")) add(&t,"%s",ui_text(u,MSG_AWAKE));
    else add(&t,"%s",wire);
    return t.ok;
}

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

static void classic_run(Session *s,const char *text);
static void classic_ask(Session *s,int input,MessageId message);
static void classic_read(Session *s,int descending,uint32_t distance,int repeat);
static void profile_fields(Session *s,int search);

static void classic_run(Session *s,const char *text) {
    char line[INPUT_LINE_MAX+128];snprintf(line,sizeof(line),"%s",text);session_impl_command(s,line);
}

static void classic_ask(Session *s,int input,MessageId message) {
    rtbbs(s)->presentation.classic_input=input;session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,message));
}

static void classic_read(Session *s,int descending,uint32_t distance,int repeat) {
    uint32_t current=rtbbs(s)->mail_state.mail_mode?rtbbs(s)->mail_state.mail.current:s->core.current;
    if(!current) { session_impl_emit(s,"ERR no current article\r\n");session_impl_prompt(s);return; }
    BoardRequest q={0};q.op=rtbbs(s)->mail_state.mail_mode?'m':'Z';q.action=rtbbs(s)->mail_state.mail_mode?'f':4;q.descending=descending;
    if(rtbbs(s)->mail_state.mail_mode)q.scope=(int)rtbbs(s)->presentation.ui.language+1;
    strcpy(q.username,"*");strcpy(q.title,"*");
    ReaderMove move=repeat?READER_REPEAT:descending?READER_BACKWARD:READER_FORWARD;
    ReaderPosition next=rtbbs(s)->mail_state.mail_mode?mail_navigation_move(&rtbbs(s)->mail_state.mail,move,descending,distance):reader_move(move,current,descending,distance);
    q.cursor=next.cursor;
    if(!q.cursor) { session_impl_emit(s,"SCAN END\r\n");session_impl_prompt(s);return; }
    rtbbs(s)->presentation.classic_direction=descending;session_impl_submit(s,&q);if(s->core.pending) { rtbbs(s)->reading.scan=q;rtbbs(s)->reading.reader.active=1; }
}

static void profile_fields(Session *s,int search) {
    char text[96];for(unsigned i=0;i<profile_form_count(search);i++) { snprintf(text,sizeof(text),"%u: %s\r\n",i+1,profile_form_field(i));session_impl_queue_text(s,text); }
    rtbbs(s)->presentation.classic_input=search?CI_SEARCH_FIELD:CI_PROF_FIELD;session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"項目番号 (0:戻る) > ":"Field (0:back) > ");
}

int session_impl_classic_input(Session *s,const char *line) {
    int mode=rtbbs(s)->presentation.classic_input;if(!mode)return 0;
    char text[INPUT_LINE_MAX+128];rtbbs(s)->presentation.classic_input=0;
    if(!strcmp(line,"/CANCEL")) { rtbbs(s)->files.batch_screen=0;rtbbs(s)->files.file_selected=0;rtbbs(s)->reading.profile_walk.active=0;session_impl_emit(s,"CANCELLED\r\n");session_impl_prompt(s);return 1; }
    if(mode==CI_PROF_TARGET) {
        const char *end;uint32_t target=0;
        if(*line && (!session_impl_number_arg(line,&target,&end) || *end)) { rtbbs(s)->presentation.classic_input=CI_PROF_TARGET;session_impl_queue_text(s,"Member ID (Enter:self) > ");return 1; }
        profile_form_target(&rtbbs(s)->reading.profile_form,target);profile_fields(s,0);return 1;
    }
    if(mode==CI_PROF_NEXT) {
        if(!strcmp(line,"0") || !strcmp(line,"N")) { rtbbs(s)->reading.profile_walk.active=0;session_impl_prompt(s);return 1; }
        session_impl_profile_continue(s);return 1;
    }
    if(mode==CI_BATCH_COPY) {
        snprintf(text,sizeof(text),"BATCH COPY %s",*line?line:(vault_navigation_path(&rtbbs(s)->files.vault)));classic_run(s,text);return 1;
    }
    if(mode==CI_FILE_ACTION) {
        if(!strcmp(line,"0")) { rtbbs(s)->files.file_selected=0;session_impl_prompt(s);return 1; }
        if(!strcmp(line,"E")) { rtbbs(s)->presentation.classic_input=CI_FILE_DESC;session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"説明 > ":"Description > ");return 1; }
        if(!strcmp(line,"K")) { rtbbs(s)->presentation.classic_input=CI_FILE_DELETE;session_impl_queue_text(s,"DELETE? Y/N > ");return 1; }
        if(!strcmp(line,"B"))snprintf(text,sizeof(text),"BATCH ADD %u",rtbbs(s)->files.file_selected);
        else if(!strcmp(line,"T"))snprintf(text,sizeof(text),"FILETYPE %u",rtbbs(s)->files.file_selected);
        else { const char *keys="SCXYGZM";const char *key=strchr(keys,*line);if(!*line || line[1] || !key) { rtbbs(s)->presentation.classic_input=CI_FILE_ACTION;session_impl_prompt(s);return 1; }
            const unsigned modes[]={6,1,7,2,3,0,4};s->connection.transfer_protocol=modes[key-keys];snprintf(text,sizeof(text),"DOWNLOAD %u",rtbbs(s)->files.file_selected); }
        classic_run(s,text);return 1;
    }
    if(mode==CI_FILE_DESC) { snprintf(text,sizeof(text),"FILEDESC %u %s",rtbbs(s)->files.file_selected,line);classic_run(s,text);return 1; }
    if(mode==CI_FILE_DELETE) { if(!strcmp(line,"Y") || !strcmp(line,"y")) { snprintf(text,sizeof(text),"FILEDELETE %u CONFIRM",rtbbs(s)->files.file_selected);classic_run(s,text); }else session_impl_prompt(s);return 1; }
    if(mode==CI_PROF_FIELD || mode==CI_SEARCH_FIELD) {
        if(!strcmp(line,"0")) { session_impl_prompt(s);return 1; }uint32_t field;const char *end;
        if(!session_impl_number_arg(line,&field,&end) || *end || !profile_form_select(&rtbbs(s)->reading.profile_form,mode==CI_SEARCH_FIELD,field)) { profile_fields(s,mode==CI_SEARCH_FIELD);return 1; }
        rtbbs(s)->presentation.classic_input=mode==CI_SEARCH_FIELD?CI_SEARCH_VALUE:CI_PROF_VALUE;
        session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"内容 > ":"Value > ");return 1;
    }
    if(mode==CI_PROF_VALUE || mode==CI_SEARCH_VALUE) {
        ProfileFormRequest request=profile_form_request(&rtbbs(s)->reading.profile_form,mode==CI_SEARCH_VALUE);
        if(request.kind==PROFILE_FORM_SEARCH)rtbbs(s)->reading.profile_walk.active=1;
        if(request.kind==PROFILE_FORM_MEMBER)snprintf(text,sizeof(text),"MEMBERPROFILE %u %s %s",request.target,request.field,line);
        else snprintf(text,sizeof(text),"%s %s %s",request.kind==PROFILE_FORM_SEARCH?"PROFILEFIND":"PROFILESET",request.field,line);
        classic_run(s,text);return 1;
    }
    if(mode==CI_BATCH_CLEAR) { if(!strcmp(line,"Y") || !strcmp(line,"y"))classic_run(s,"BATCH CLEAR");else session_impl_prompt(s);return 1; }
    if(mode==CI_PROTOCOL || mode==CI_SEND_PROTOCOL) {
        const char *names[]={"ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};
        char protocol[32];snprintf(protocol,sizeof(protocol),"%s",line);for(char *p=protocol;*p;p++)if(*p>='a' && *p<='z')*p-=32;
        if(*line) { unsigned selected=8;for(unsigned i=0;i<8;i++)if(!strcmp(protocol,names[i]) || (line[0]=='1'+(int)i && !line[1]))selected=i;
            if(selected==8 || (s->connection.upload_batch && (selected==1 || selected>=5))) { classic_ask(s,mode,MSG_TRANSFER_PROTOCOL);return 1; }s->connection.transfer_protocol=selected; }
        if(s->connection.upload_batch && (s->connection.transfer_protocol==1 || s->connection.transfer_protocol>=5)) { classic_ask(s,mode,MSG_TRANSFER_PROTOCOL);return 1; }
        if(mode==CI_SEND_PROTOCOL) { classic_run(s,"BATCH SEND");return 1; }
        classic_ask(s,CI_UPLOAD,MSG_CLASSIC_UPLOAD);return 1;
    }
    if(mode==CI_FILENAME) {
        if(!*line || strlen(line)>100 || strpbrk(line,"/\\:|") || !session_impl_safe_text(line) || !strcmp(line,".") || !strcmp(line,"..")) { classic_ask(s,CI_FILENAME,MSG_TRANSFER_FILENAME);return 1; }
        strcpy(s->connection.upload_filename,line);classic_ask(s,CI_FILESIZE,MSG_TRANSFER_SIZE);return 1;
    }
    if(mode==CI_FILESIZE) {
        uint32_t bytes;const char *tail;if((!strcmp(line,"0")?(bytes=0,tail=line+1,0):!session_impl_number_arg(line,&bytes,&tail)) || *tail || bytes>ln_limit("FILE",1073741824u)) { classic_ask(s,CI_FILESIZE,MSG_TRANSFER_SIZE);return 1; }
        snprintf(text,sizeof(text),"UPLOAD %s|%s|%u|%s",vault_navigation_path(&rtbbs(s)->files.vault),s->connection.upload_filename,bytes,s->connection.upload_description);classic_run(s,text);return 1;
    }
    if(mode==CI_BATCH) {
        if(!strcmp(line,"1"))classic_ask(s,CI_BATCH_ADD,MSG_TRANSFER_ID);
        else if(!strcmp(line,"2"))classic_ask(s,CI_BATCH_REMOVE,MSG_TRANSFER_ID);
        else if(!strcmp(line,"3"))classic_ask(s,CI_BATCH_MATCH,MSG_TRANSFER_MATCH);
        else if(!strcmp(line,"4") || !strcmp(line,"L"))classic_run(s,"BATCH LIST");
        else if(!strcmp(line,"5") || !strcmp(line,"C")) { rtbbs(s)->presentation.classic_input=CI_BATCH_CLEAR;session_impl_queue_text(s,"CLEAR? Y/N > "); }
        else if(!strcmp(line,"6") || !strcmp(line,"D")) { s->connection.upload_batch=1;classic_ask(s,CI_SEND_PROTOCOL,MSG_TRANSFER_PROTOCOL); }
        else if(!strcmp(line,"M")) { rtbbs(s)->presentation.classic_input=CI_BATCH_COPY;session_impl_queue_text(s,"Destination board (Enter:current) > "); }
        else if(!strcmp(line,"0")) { rtbbs(s)->files.batch_screen=0;session_impl_prompt(s); }
        else classic_ask(s,CI_BATCH,MSG_TRANSFER_BATCH);
        return 1;
    }
    if(mode==CI_BATCH_ADD || mode==CI_BATCH_REMOVE || mode==CI_BATCH_MATCH) {
        snprintf(text,sizeof(text),"BATCH %s %s",mode==CI_BATCH_ADD?"ADD":mode==CI_BATCH_REMOVE?"REMOVE":"MATCH",line);classic_run(s,text);return 1;
    }
    if(mode==CI_EXIT) {
        if(!strcmp(line,"Y") || !strcmp(line,"y"))classic_run(s,"QUIT");
        else { session_impl_emit(s,"CANCELLED\r\n");session_impl_prompt(s); }
        return 1;
    }
    if(mode==CI_HEADER) {
        if(!strcmp(line,"1"))classic_ask(s,CI_OWNER,MSG_CLASSIC_OWNER);
        else if(!strcmp(line,"2"))classic_ask(s,CI_TITLE,MSG_CLASSIC_TITLE);
        else if(strcmp(line,"0"))classic_ask(s,CI_HEADER,MSG_CLASSIC_HEADER);
        else session_impl_prompt(s);
        return 1;
    }
    if(mode==CI_WITHDRAW) {
        if(!strcmp(line,"Y") || !strcmp(line,"y"))snprintf(text,sizeof(text),"WITHDRAW %u CONFIRM",rtbbs(s)->presentation.classic_id);
        else { session_impl_emit(s,"CANCELLED\r\n");session_impl_prompt(s);return 1; }
    } else if(mode==CI_DATE) {
        if(!*line) { if(rtbbs(s)->files.vault_mode)snprintf(text,sizeof(text),"CHECK FILES %s",vault_navigation_path(&rtbbs(s)->files.vault));else snprintf(text,sizeof(text),"CHECK %s",rtbbs(s)->mail_state.mail_mode?"MAIL":"ARTICLES HERE"); }
        else if(rtbbs(s)->files.vault_mode)snprintf(text,sizeof(text),"FILENEWS %s|%s",vault_navigation_path(&rtbbs(s)->files.vault),line);
        else snprintf(text,sizeof(text),"%s %s",rtbbs(s)->mail_state.mail_mode?"MAIL NEWS":rtbbs(s)->files.vault_mode?"FILENEWS":"NEWS HERE",line);
    } else if(mode==CI_FILEINFO)snprintf(text,sizeof(text),"FILEBOARDINFO %s",*line?line:"-");
    else if(mode==CI_DOWNLOAD)snprintf(text,sizeof(text),"DOWNLOAD %s",line);
    else if(mode==CI_UPLOAD) {
        if(strlen(line)>240 || !session_impl_safe_text(line) || strchr(line,'|')) { classic_ask(s,CI_UPLOAD,MSG_CLASSIC_UPLOAD);return 1; }
        strcpy(s->connection.upload_description,line);
        if(s->connection.transfer_protocol==1 || s->connection.transfer_protocol>=5) { classic_ask(s,CI_FILENAME,MSG_TRANSFER_FILENAME);return 1; }
        snprintf(text,sizeof(text),"%s %s|%s",s->connection.upload_batch?"UPLOADBATCH":"UPLOAD",vault_navigation_path(&rtbbs(s)->files.vault),line);
    }
    else if(mode==CI_AUTHOR)snprintf(text,sizeof(text),"FIND HERE %s *",line);
    else if(mode==CI_HANDLE)snprintf(text,sizeof(text),"HANDLE %s",line);
    else if(mode==CI_PROFILE)snprintf(text,sizeof(text),"PROFILE %s",line);
    else if(mode==CI_PROFILE_SEARCH)snprintf(text,sizeof(text),"PROFILES %s",line);
    else if(mode==CI_MEMO)snprintf(text,sizeof(text),"MEMO%s%s",*line?" ":"",line);
    else if(mode==CI_TITLE)snprintf(text,sizeof(text),"RETITLE %u %s",rtbbs(s)->presentation.classic_id,line);
    else if(mode==CI_OWNER)snprintf(text,sizeof(text),"AUTHOR %u %s CONFIRM",rtbbs(s)->presentation.classic_id,line);
    else if(mode==CI_EDITOR || mode==CI_MENUMODE) {
        char value[32];if(strlen(line)>=sizeof(value)) { session_impl_emit(s,"ERR command\r\n");session_impl_prompt(s);return 1; }
        strcpy(value,line);for(char *p=value;*p;p++)if(*p>='a' && *p<='z')*p-=32;
        snprintf(text,sizeof(text),"%s %s",mode==CI_EDITOR?"EDITOR":"MENUMODE",value);
    } else if(mode==CI_ROWS) {
        uint32_t rows;const char *tail;
        if(!session_impl_number_arg(line,&rows,&tail) || *tail || (rows && (rows<5 || rows>97))) { session_impl_emit(s,"ERR rows 0 or 5..97\r\n");session_impl_prompt(s);return 1; }
        if(rows) { session_dimensions(s,s->connection.columns,rows+3);rtbbs(s)->presentation.pager_enabled=1; }
        else rtbbs(s)->presentation.pager_enabled=0;
        session_impl_emit(s,rows?"PAGER ON\r\n":"PAGER OFF\r\n");session_impl_prompt(s);return 1;
    } else { session_impl_emit(s,"ERR command\r\n");session_impl_prompt(s);return 1; }
    classic_run(s,text);return 1;
}

int session_impl_classic_commands(Session *s,const char *line) {
    if(!rtbbs(s)->presentation.ui.enabled || rtbbs(s)->presentation.ui.input)return 0;
    if(session_impl_classic_input(s,line))return 1;
    if(!strcmp(line,"AUDIT REFRESH") && !rtbbs(s)->mail_state.mail_input && !rtbbs(s)->mail_state.mail.delete_id) {
        char mapped[256];
        if(rtbbs(s)->presentation.ui.view==UI_AUDIT)session_impl_audit_command(&rtbbs(s)->presentation.ui,0,mapped,sizeof(mapped));
        else strcpy(mapped,"AUDIT");
        session_rtbbs_command(s,mapped);session_impl_prompt(s);return 1;
    }
    if(!rtbbs(s)->mail_state.mail_mode && !rtbbs(s)->files.vault_mode && !rtbbs(s)->mail_state.mail_input && !rtbbs(s)->mail_state.mail.delete_id) {
        if(!strcmp(line,"REGISTER") || !strcmp(line,"LOGIN")) {
            if(s->core.user)session_impl_emit(s,"ERR logout first\r\n");
            else if(!s->core.member_mode)session_impl_emit(s,"ERR members disabled\r\n");
            else if(*line=='R' && members_auto_ids()) { classic_run(s,"REGISTER auto");return 1; }
            else rtbbs(s)->presentation.ui.input=*line=='R'?UI_REGISTER:UI_LOGIN;
            session_impl_prompt(s);return 1;
        }
        if(!strcmp(line,"POST") || !strcmp(line,"REPLY")) {
            rtbbs(s)->presentation.classic_area=CM_BOARD;
            if(*line=='R' && !s->core.current)session_impl_emit(s,"ERR no current article\r\n");
            else if(s->core.member_mode && !s->core.user) { BoardRequest q={0};q.op='e';q.action='?';q.number=*line=='R'?s->core.current:0;session_impl_submit(s,&q); }
            else rtbbs(s)->presentation.ui.input=*line=='R'?UI_REPLY:UI_POST;
            session_impl_prompt(s);return 1;
        }
        if(!strcmp(line,"SEARCH")) { rtbbs(s)->presentation.ui.input=UI_SEARCH_INPUT;session_impl_prompt(s);return 1; }
        if(!strcmp(line,"SEEN NEXT")) { rtbbs(s)->presentation.ui.next_after_seen=1;classic_run(s,"SEEN");return 1; }
    }
    /* These commands explicitly switch screens, independent of the last report. */
    if(!strcmp(line,"MENU") && !rtbbs(s)->mail_state.mail_input && !rtbbs(s)->mail_state.mail.delete_id) {
        rtbbs(s)->mail_state.mail_mode=rtbbs(s)->files.vault_mode=0;rtbbs(s)->files.file_selected=0;rtbbs(s)->files.batch_screen=0;rtbbs(s)->presentation.classic_area=CM_MAIN;return 0;
    }
    if(!strcmp(line,"BOARDS") || !strncmp(line,"BOARD ",6) || !strcmp(line,"NOTES") ||
       !strncmp(line,"NOTES ",6) || !strncmp(line,"READ ",5) || !strncmp(line,"SCAN ",5) ||
       !strncmp(line,"POST ",5) || !strncmp(line,"REPLY ",6))rtbbs(s)->presentation.classic_area=CM_BOARD;
    if((!strcmp(line,"O") || !strcmp(line,"Q")) && !rtbbs(s)->mail_state.mail_input && !rtbbs(s)->mail_state.mail.delete_id) { classic_ask(s,CI_EXIT,MSG_EXIT_CONFIRM);return 1; }
    if(rtbbs(s)->mail_state.mail_mode) {
        if(rtbbs(s)->mail_state.mail.reading && !rtbbs(s)->mail_state.mail_input && !rtbbs(s)->mail_state.mail.delete_id && (!*line || !strcmp(line,"P") || !strcmp(line,"L") || !strcmp(line,"F") || *line=='+' || *line=='-')) {
            uint32_t distance=1;const char *tail;
            if((*line=='+' || *line=='-') && line[1] && (!session_impl_number_arg(line+1,&distance,&tail) || *tail || !distance)) { session_impl_emit(s,"ERR relative distance\r\n");session_impl_prompt(s);return 1; }
            classic_read(s,*line=='P' || *line=='-'?1:*line=='+'?0:rtbbs(s)->presentation.classic_direction,distance,*line=='L');
            if(*line=='F' && s->core.pending)rtbbs(s)->reading.reader.automatic=1;
            return 1;
        }
        if(rtbbs(s)->mail_state.mail_input || rtbbs(s)->mail_state.mail.delete_id)return 0;
        if(!strcmp(line,"N")) { classic_ask(s,CI_DATE,MSG_CLASSIC_DATE);return 1; }
        if(!strcmp(line,"T")) { classic_run(s,"MAIL SCAN DESC");return 1; }
        if(!strcmp(line,"0") || !strcmp(line,"EXIT"))rtbbs(s)->presentation.classic_area=CM_MAIN;
        return 0;
    }
    if(rtbbs(s)->files.vault_mode) {
        if(!strcmp(line,"N")) { classic_ask(s,CI_DATE,MSG_CLASSIC_DATE);return 1; }
        if(!strcmp(line,"B")) { rtbbs(s)->files.batch_screen=1;rtbbs(s)->files.file_selected=0;classic_ask(s,CI_BATCH,MSG_TRANSFER_BATCH);return 1; }
        if(!strcmp(line,"I")) { char info[80];snprintf(info,sizeof(info),"FILEBOARDINFO %s",vault_navigation_path(&rtbbs(s)->files.vault));classic_run(s,info);return 1; }
        if(!strcmp(line,"D")) { classic_ask(s,CI_DOWNLOAD,MSG_CLASSIC_DOWNLOAD);return 1; }
        if(!strcmp(line,"U") || !strcmp(line,"UB")) { s->connection.upload_batch=!strcmp(line,"UB");classic_ask(s,CI_PROTOCOL,MSG_TRANSFER_PROTOCOL);return 1; }
        if(!strcmp(line,"0") || !strcmp(line,"EXIT"))rtbbs(s)->presentation.classic_area=CM_MAIN;
        return 0;
    }
    if(!strcmp(line,"?")) { session_impl_show_menu(s,1);session_impl_prompt(s);return 1; }
    if(rtbbs(s)->presentation.classic_area==CM_SETTINGS) {
        if(!strcmp(line,"H")) { classic_ask(s,CI_HANDLE,MSG_CLASSIC_HANDLE);return 1; }
        if(!strcmp(line,"1")) { classic_ask(s,CI_EDITOR,MSG_CLASSIC_EDITOR);return 1; }
        if(!strcmp(line,"3")) { classic_ask(s,CI_MENUMODE,MSG_CLASSIC_MENUMODE);return 1; }
        if(!strcmp(line,"4")) { classic_ask(s,CI_ROWS,MSG_CLASSIC_ROWS);return 1; }
        if(!strcmp(line,"2")) { session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_CLASSIC_NO_ESC));session_impl_prompt(s);return 1; }
    }
    if(!strcmp(line,"0")) {
        if(rtbbs(s)->presentation.classic_area==CM_BOARD && rtbbs(s)->presentation.ui.view==UI_ARTICLE)classic_run(s,"NOTES");
        else classic_run(s,"MENU");
        return 1;
    }
    if(!strcmp(line,"H")) { session_impl_show_help(s,rtbbs(s)->presentation.classic_area==CM_BOARD?"READ":rtbbs(s)->presentation.classic_area==CM_SETTINGS?"SETTINGS":rtbbs(s)->presentation.classic_area==CM_PROFILE?"PROFILE":rtbbs(s)->presentation.classic_area==CM_ADMIN?"ADMIN":"");session_impl_prompt(s);return 1; }
    if(!strcmp(line,"O") || !strcmp(line,"Q")) { classic_run(s,"QUIT");return 1; }
    if(!strcmp(line,"$")) {
        if(!rtbbs(s)->presentation.ui.admin && !(rtbbs(s)->presentation.classic_area==CM_BOARD && rtbbs(s)->presentation.ui.sigop)) { session_impl_emit(s,"ERR admin required\r\n");session_impl_prompt(s);return 1; }
        rtbbs(s)->presentation.classic_area=CM_ADMIN;session_impl_show_menu(s,1);session_impl_prompt(s);return 1;
    }
    if(rtbbs(s)->presentation.classic_area==CM_ADMIN && strlen(line)==1 && *line>='1' && *line<='5') {
        if(rtbbs(s)->presentation.ui.admin)session_impl_show_help(s,(const char *[]){"ADMINMEMBERS","ADMINBOARDS","ADMINFILES","ADMINCONNECTIONS","ADMINSTORAGE"}[*line-'1']);
        else if(rtbbs(s)->presentation.ui.sigop)session_impl_show_help(s,"SIGOP");else session_impl_emit(s,"ERR admin required\r\n");
        session_impl_prompt(s);return 1;
    }
    if(rtbbs(s)->presentation.classic_area==CM_BOARD && rtbbs(s)->presentation.ui.view==UI_ARTICLE) {
        if(!*line || !strcmp(line,"P") || !strcmp(line,"L") || !strcmp(line,"F") || *line=='+' || *line=='-') {
            uint32_t distance=1;const char *tail;
            if((*line=='+' || *line=='-') && line[1] && (!session_impl_number_arg(line+1,&distance,&tail) || *tail || !distance)) { session_impl_emit(s,"ERR relative distance\r\n");session_impl_prompt(s);return 1; }
            int direction=*line=='P' || *line=='-'?1:*line=='+'?0:rtbbs(s)->presentation.classic_direction;
            classic_read(s,direction,distance,*line=='L');if(*line=='F' && s->core.pending)rtbbs(s)->reading.reader.automatic=1;return 1;
        }
        if(!strcmp(line,"S")) { BoardRequest q={0};q.op='Z';q.action=17;q.number=s->core.current;session_impl_submit(s,&q);return 1; }
        if(!strcmp(line,"D")) { classic_run(s,"PARENT");return 1; }
        if(!strcmp(line,"A")) { classic_run(s,"CHILD");return 1; }
        if(!strcmp(line,"G")) { classic_run(s,"GET");return 1; }
        if(!strcmp(line,"K")) { rtbbs(s)->presentation.classic_id=s->core.current;classic_ask(s,CI_WITHDRAW,MSG_CLASSIC_DELETE);return 1; }
        if(!strcmp(line,"E")) {
            if(!rtbbs(s)->presentation.ui.admin && !rtbbs(s)->presentation.ui.sigop) { session_impl_emit(s,"ERR admin required\r\n");session_impl_prompt(s);return 1; }
            rtbbs(s)->presentation.classic_id=s->core.current;classic_ask(s,CI_HEADER,MSG_CLASSIC_HEADER);return 1;
        }
    }
    if(strlen(line)!=1 || *line=='+' || *line=='>' || *line=='<' || (*line>='0' && *line<='9'))return 0;
    const char *action=NULL;char key=*line;
    if(rtbbs(s)->presentation.classic_area==CM_MAIN) {
        switch(key) {
        case 'A':action="REGISTER";break;
        case 'B': action="BOARDS";break;case 'C':action="CHAT";break;case 'D':action="CALL";break;
        case 'E':action="BUFFER";break;case 'F':action="VAULT";break;case 'G':action="GATHER";break;
        case 'I':rtbbs(s)->presentation.classic_area=CM_SETTINGS;session_impl_show_menu(s,1);session_impl_prompt(s);return 1;
        case 'L':action="USERTABLE";break;case 'M':action="MAIL";break;
        case 'P':rtbbs(s)->presentation.classic_area=CM_PROFILE;session_impl_show_menu(s,1);session_impl_prompt(s);return 1;
        case 'T':action="TALK";break;case 'U':action="PROFILES";break;case 'W':action="ONLINE";break;
        case '\\':classic_ask(s,CI_MEMO,MSG_CLASSIC_MEMO);return 1;
        case '#':action="ACCOUNT";break;
        case 'S':session_impl_queue_text(s,*site_text(0)?site_text(0):ui_text(&rtbbs(s)->presentation.ui,MSG_SITE_EMPTY));session_impl_prompt(s);return 1;
        case 'V':session_impl_queue_text(s,LINER_VERSION_TEXT "\r\n");session_impl_prompt(s);return 1;
        case 'X':session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_CLASSIC_NO_GATEWAY));session_impl_prompt(s);return 1;
        }
    } else if(rtbbs(s)->presentation.classic_area==CM_BOARD) {
        switch(key) {
        case 'B':action="BOARDS";break;case 'I':action="INFO";break;case 'L':action="NOTES";break;
        case 'T':action="SCAN DESC HERE * *";break;
        case 'N':classic_ask(s,CI_DATE,MSG_CLASSIC_DATE);return 1;
        case 'F':classic_ask(s,CI_AUTHOR,MSG_CLASSIC_AUTHOR);return 1;
        case 'E':action="POST";break;
        case 'R':if(!s->core.current) { session_impl_emit(s,"ERR no current article\r\n");session_impl_prompt(s);return 1; }rtbbs(s)->presentation.ui.input=UI_REPLY;session_impl_prompt(s);return 1;
        case 'K':rtbbs(s)->presentation.classic_id=s->core.current;classic_ask(s,CI_WITHDRAW,MSG_CLASSIC_DELETE);return 1;
        }
    } else if(rtbbs(s)->presentation.classic_area==CM_SETTINGS) {
        switch(key) {
        case 'D': {
            char settings[512];snprintf(settings,sizeof(settings),"SETTINGS\r\nLANG %s / EDITOR %s / MENUMODE %s\r\nPAGER %s / SIZE %u x %u / TIMEZONE %s\r\nPROTOCOL %s / LINE NUMBERS %s\r\n",language_code(rtbbs(s)->presentation.ui.language),rtbbs(s)->editor.editor_line?"LINE":"BETA",rtbbs(s)->presentation.menu_mode==0?"BEGINNER":rtbbs(s)->presentation.menu_mode==2?"EXPERT":"STANDARD",rtbbs(s)->presentation.pager_enabled?"ON":"OFF",s->connection.columns,s->connection.rows,date_zone(),(const char *[]){"ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"}[s->connection.transfer_protocol],rtbbs(s)->editor.editor_numbers?"ON":"OFF");
            session_impl_queue_text(s,settings);session_impl_show_menu(s,1);session_impl_prompt(s);return 1;
        }
        case 'P':action="PASSWD";break;
        case 'H':classic_ask(s,CI_HANDLE,MSG_CLASSIC_HANDLE);return 1;
        }
    } else if(rtbbs(s)->presentation.classic_area==CM_PROFILE) {
        switch(key) {
        case 'L':action="PROFILES";break;case 'P':classic_ask(s,CI_PROFILE,MSG_CLASSIC_PROFILE);return 1;
        case 'S':profile_fields(s,1);return 1;
        case 'W':action="BIOEDIT";break;case 'E':profile_form_target(&rtbbs(s)->reading.profile_form,0);if(rtbbs(s)->presentation.ui.admin) { rtbbs(s)->presentation.classic_input=CI_PROF_TARGET;session_impl_queue_text(s,"Member ID (Enter:self) > "); }else profile_fields(s,0);return 1;
        }
    }
    if(action)classic_run(s,action);
    else { session_impl_emit(s,"ERR command\r\n");session_impl_prompt(s); }
    return 1;
}

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

int session_impl_extended_command(Session *s,char *line) {
    const char *mail_transfers[]={"MAILUPLOAD","MAILUPLOADBATCH","MAILDOWNLOAD","MAILDOWNLOADALL"};
    for(unsigned i=0;i<4;i++) {
        size_t n=strlen(mail_transfers[i]);if(strncmp(line,mail_transfers[i],n) || (line[n] && line[n]!=' '))continue;
        if((i==1 || i==3) && (s->connection.transfer_protocol==1 || s->connection.transfer_protocol>=5)) { session_impl_emit(s,"ERR this protocol transfers one file at a time\r\n");return 1; }
        char mapped[INPUT_LINE_MAX+2];snprintf(mapped,sizeof(mapped),"Z%s",line);session_rtbbs_command(s,mapped);
        if(s->core.pending) { s->connection.zmodem_protocol=s->connection.transfer_protocol;rtbbs(s)->mail_state.mail_batch_protocol=s->connection.transfer_protocol; }
        return 1;
    }
    if(!strcmp(line,"FILECHILDREN") || !strncmp(line,"FILECHILDREN ",13)) {
        BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_CHILDREN;const char *path=line[12]?line+13:(vault_navigation_path(&rtbbs(s)->files.vault));
        if(!*path || strlen(path)>48 || !session_impl_safe_text(path)) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }
        strcpy(q.body,path);session_impl_submit(s,&q);return 1;
    }
    if(!strcmp(line,"FILEBOARD") || !strncmp(line,"FILEBOARD ",10)) {
        const char *path=line[9]?line+10:"-";
        if(!*path || strlen(path)>48 || !session_impl_safe_text(path)) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }
        BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_BOARDINFO;strcpy(q.title,path);vault_navigation_begin(&rtbbs(s)->files.vault,path);session_impl_submit(s,&q);if(!s->core.pending)rtbbs(s)->files.vault.selecting=0;return 1;
    }
    if(!strcmp(line,"CHECKMODE AUTO") || !strcmp(line,"CHECKMODE MANUAL")) { rtbbs(s)->reading.check.automatic=!strcmp(line,"CHECKMODE AUTO");session_impl_emit(s,rtbbs(s)->reading.check.automatic?"CHECKMODE AUTO\r\n":"CHECKMODE MANUAL\r\n");return 1; }
    if(!strncmp(line,"CHECK ",6)) {
        const char *kind=line+6;BoardRequest q={0};q.op='}';
        if(!strcmp(kind,"NEXT")) { if(!rtbbs(s)->reading.check.active || rtbbs(s)->reading.check.finished)session_impl_emit(s,"CHECK END; CHECK DONE to save checkpoint\r\n");else session_impl_check_page(s);return 1; }
        if(!strcmp(kind,"CANCEL")) { rtbbs(s)->reading.check.active=0;session_impl_emit(s,"CHECK CANCELLED\r\n");return 1; }
        if(!strcmp(kind,"DONE")) { CheckCommit commit;if(!check_traversal_commit(&rtbbs(s)->reading.check,&commit)) { session_impl_emit(s,"ERR finish CHECK NEXT first\r\n");return 1; }q.action='n';q.number=commit.kind;q.until=commit.until;strcpy(q.title,commit.key);session_impl_submit(s,&q);rtbbs(s)->reading.check.committing=s->core.pending;return 1; }
        q.action='N';q.number=check_traversal_select(&rtbbs(s)->reading.check,kind,s->core.board);
        strcpy(q.title,rtbbs(s)->reading.check.key);
        if(q.number==3)session_impl_emit(s,"ERR CHECK ARTICLES [HERE|SUBTREE|ALL]|MAIL|FILES [path]|NEXT|DONE|CANCEL\r\n");else session_impl_submit(s,&q);return 1;
    }
    if(!strncmp(line,"SIGOPGROUP ",11) || !strncmp(line,"BOARDGROUP ",11)) {
        BoardRequest q={0};q.op='}';q.action=*line=='S'?'G':'g';const char *tail;
        if(!session_impl_number_arg(line+11,&q.number,&tail) || *tail!=' ' || !session_impl_number_arg(tail+1,&q.cursor,&tail)) { session_impl_emit(s,"ERR command\r\n");return 1; }
        if(q.action=='g') { if(*tail!=' ' || !session_impl_number_arg(tail+1,&q.audit_actor,&tail) || strcmp(tail," CONFIRM")) { session_impl_emit(s,"ERR command\r\n");return 1; } }
        else if(*tail) { session_impl_emit(s,"ERR command\r\n");return 1; }
        session_impl_submit(s,&q);return 1;
    }
    if(!strncmp(line,"APPLICATION ",12) || !strncmp(line,"APPLICATIONSET ",15)) {
        BoardRequest q={0};q.op='}';const char *tail;int set=!strncmp(line,"APPLICATIONSET ",15);
        if(!session_impl_number_arg(line+(set?15:12),&q.number,&tail)) { session_impl_emit(s,"ERR command\r\n");return 1; }
        q.action='A';if(set) { if(*tail!=' ') { session_impl_emit(s,"ERR command\r\n");return 1; }const char *field=++tail,*space=strchr(field,' ');size_t n=space?(size_t)(space-field):strlen(field);if(!n || n>32 || (space && strlen(space+1)>320)) { session_impl_emit(s,"ERR command\r\n");return 1; }memcpy(q.title,field,n);if(space)strcpy(q.body,space+1);int f=profile_field(q.title);if(f>=0) { q.op=')';q.action='A';q.cursor=q.number;q.number=(uint32_t)f; }else q.action='a'; }
        else if(*tail) { session_impl_emit(s,"ERR command\r\n");return 1; }
        session_impl_submit(s,&q);return 1;
    }
    if(!strncmp(line,"ZMAILDOWNLOADALL ",17)) { rtbbs(s)->mail_state.mail_batch_protocol=0;BoardRequest q={0};q.op='}';q.action='B';const char *tail;if(!s->connection.zmodem_available || !s->connection.binary_allowed || !session_impl_number_arg(line+17,&q.number,&tail) || *tail) { session_impl_emit(s,"ERR command or transfer unavailable\r\n");return 1; }session_impl_submit(s,&q);return 1; }

    if(!strncmp(line,"FILEBOARDPASS ",14) || !strncmp(line,"FILEUNLOCK ",11)) {
        int set=!strncmp(line,"FILEBOARDPASS ",14);const char *p=line+(set?14:11);size_t n=strcspn(p," ");
        if(!s->core.user || !n || n>48 || (p[n] && strcmp(p+n," NONE CONFIRM"))) { session_impl_emit(s,"ERR command\r\n");return 1; }
        memcpy(rtbbs(s)->auth.filepass_path,p,n);rtbbs(s)->auth.filepass_path[n]=0;
        if(p[n]) { if(!set) { session_impl_emit(s,"ERR command\r\n");return 1; }BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_PASSWORD;snprintf(q.body,sizeof(q.body),"%s|",rtbbs(s)->auth.filepass_path);session_impl_submit(s,&q); }
        else { rtbbs(s)->auth.filepass_setting=set;rtbbs(s)->auth.password_mode=set?'v':'f';session_impl_emit(s,set?"NEW PASSWORD\r\n":"PASSWORD\r\n"); }
        return 1;
    }
    if(!strncmp(line,"FILERENAME ",11) || !strncmp(line,"FILEBOARDOPTIONS ",17)) {
        BoardRequest q={0};q.op='Y';const char *tail;
        if(line[4]=='R') { q.action=VAULT_CMD_RENAME;if(!session_impl_number_arg(line+11,&q.number,&tail) || *tail!=' ' || strlen(tail+1)>100) { session_impl_emit(s,"ERR command\r\n");return 1; }strcpy(q.body,tail+1); }
        else { q.action=VAULT_CMD_OPTIONS;if(strlen(line+17)>160 || !strchr(line+17,'|')) { session_impl_emit(s,"ERR command\r\n");return 1; }strcpy(q.body,line+17); }
        session_impl_submit(s,&q);return 1;
    }
    if(!strncmp(line,"MEMBERPROFILE ",14)) {
        BoardRequest q={0};const char *tail;q.op=')';q.action='A';
        if(!session_impl_number_arg(line+14,&q.cursor,&tail) || *tail!=' ') { session_impl_emit(s,"ERR command\r\n");return 1; }
        const char *space=strchr(++tail,' ');char key[32];size_t n=space?(size_t)(space-tail):strlen(tail);
        if(!n || n>=sizeof(key) || (space && strlen(space+1)>BOARD_BODY)) { session_impl_emit(s,"ERR command\r\n");return 1; }
        memcpy(key,tail,n);key[n]=0;for(char *p=key;*p;p++)if(*p>='a' && *p<='z')*p-=32;
        int f=profile_field(key);if(f<0) { session_impl_emit(s,"ERR profile field\r\n");return 1; }q.number=(uint32_t)f;if(space)strcpy(q.body,space+1);session_impl_submit(s,&q);return 1;
    }
    if(!strcmp(line,"PROTOCOL") || !strncmp(line,"PROTOCOL ",9)) {
        const char *names[]={"ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};
        if(line[8]) { unsigned i;for(i=0;i<8;i++)if(!strcmp(line+9,names[i]))break;if(i==8) { session_impl_emit(s,"ERR command\r\n");return 1; }s->connection.transfer_protocol=i; }
        char text[160];snprintf(text,sizeof(text),"PROTOCOL %s\r\n",names[s->connection.transfer_protocol]);session_impl_emit(s,text);return 1;
    }
    if(!strncmp(line,"DOWNLOAD ",9) || !strncmp(line,"UPLOAD ",7) || !strncmp(line,"UPLOADBATCH ",12)) {
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        if(!board_feature_enabled("vault")) { session_impl_emit(s,"ERR feature disabled\r\n");return 1; }
        if(!s->connection.zmodem_available || !s->connection.binary_allowed) { session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_ZMODEM_UNAVAILABLE));return 1; }
        BoardRequest q={0};q.op='@';const char *arg=strchr(line,' ')+1,*tail;int receive=*line=='U',batch=!strncmp(line,"UPLOADBATCH ",12);
        if(batch && (s->connection.transfer_protocol==1 || s->connection.transfer_protocol>=5)) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }
        if(!receive) { if(!session_impl_number_arg(arg,&q.number,&tail) || *tail || q.number>VAULT_ID_MAX) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }s->connection.zmodem_text[0]=0; }
        else { if(strlen(arg)>512 || !session_impl_safe_text(arg)) { session_impl_emit(s,"ERR vault syntax\r\n");return 1; }strcpy(s->connection.zmodem_text,arg); }
        s->connection.zmodem_id=q.number;s->connection.zmodem_receive=batch?6:receive?3:0;s->connection.zmodem_protocol=s->connection.transfer_protocol;s->connection.zmodem_start=1;session_impl_submit(s,&q);if(!s->core.pending)s->connection.zmodem_start=0;return 1;
    }

    if(!strncmp(line,"FILECOPY ",9)) { BoardRequest q={0};const char *end;q.op='Y';q.action=VAULT_CMD_COPY;if(!session_impl_number_arg(line+9,&q.number,&end) || *end!=' ' || strlen(end+1)>48 || !session_impl_safe_text(end+1)) { session_impl_emit(s,"ERR file copy\r\n");return 1; }strcpy(q.body,end+1);session_impl_submit(s,&q);return 1; }
    if(!strncmp(line,"FILEOWNER ",10) || !strncmp(line,"FILEASCII ",10) || !strncmp(line,"FILETYPE ",9) || !strncmp(line,"FILEBOARDNAME ",14) || !strncmp(line,"FILEBOARDSIGOP ",15) || !strncmp(line,"FILEINFOEDIT ",13)) {
        BoardRequest q={0};q.op='Y';const char *arg=strchr(line,' ')+1,*tail;
    if(!strncmp(line,"FILEOWNER ",10)) { q.action=VAULT_CMD_OWNER;if(!session_impl_number_arg(arg,&q.number,&tail) || *tail!=' ' || !session_impl_number_arg(tail+1,&q.cursor,&tail) || *tail)goto newvault_bad; }
        else if(!strncmp(line,"FILEASCII ",10)) { q.action=VAULT_CMD_ASCII;if(!session_impl_number_arg(arg,&q.number,&tail) || *tail!=' ' || strlen(tail+1)>3)goto newvault_bad;strcpy(q.body,tail+1);for(char *p=q.body;*p;p++)if(*p>='a' && *p<='z')*p-=32; }
        else if(!strncmp(line,"FILETYPE ",9)) { q.action=VAULT_CMD_TEXT;if(!session_impl_number_arg(arg,&q.number,&tail))goto newvault_bad;if(*tail && (*tail!=' ' || !session_impl_number_arg(tail+1,&q.cursor,&tail) || *tail))goto newvault_bad;snprintf(q.body,sizeof(q.body),"%u",q.cursor); }
        else if(!strncmp(line,"FILEINFOEDIT ",13)) { q.action=VAULT_CMD_BOARDEDIT;if(!*arg || strlen(arg)>48 || !session_impl_safe_text(arg))goto newvault_bad;strcpy(q.body,arg); }
        else { const char *bar=strchr(arg,'|');if(!bar || bar-arg>48)goto newvault_bad;
            if(!strncmp(line,"FILEBOARDSIGOP ",15)) { q.action=VAULT_CMD_BOARDSIGOP;if(strcmp(bar+1,"NONE") && (!session_impl_number_arg(bar+1,&q.cursor,&tail) || *tail))goto newvault_bad;memcpy(q.title,arg,(size_t)(bar-arg)); }
            else { q.action=VAULT_CMD_BOARDNAME;if(strlen(arg)>129 || !session_impl_safe_text(arg))goto newvault_bad;strcpy(q.body,arg); }
        }
        session_impl_submit(s,&q);if(q.action==VAULT_CMD_TEXT && s->core.pending) { report_navigation_begin(&rtbbs(s)->reading.report,REPORT_FILETEXT,q.number,0); }return 1;
newvault_bad:session_impl_emit(s,"ERR vault syntax\r\n");return 1;
    }

    if(!strcmp(line,"BIOEDIT") || !strcmp(line,"BUFFER") || !strcmp(line,"BUFFER CLEAR") || !strncmp(line,"PROFILESET ",11) || !strncmp(line,"PROFILEVIS ",11) || !strncmp(line,"PROFILEFIND ",12) || !strncmp(line,"GROUPMOVE ",10)) {
        BoardRequest q={0};q.op=')';const char *arg=NULL,*tail=NULL;
        if(!strcmp(line,"BIOEDIT") || !strcmp(line,"BUFFER")) { q.action='I';q.number=!strcmp(line,"BIOEDIT")?1:2; }
        else if(!strcmp(line,"BUFFER CLEAR"))q.action='C';
        else if(!strncmp(line,"GROUPMOVE ",10)) { q.action='G';if(!session_impl_number_arg(line+10,&q.number,&tail) || *tail!=' ' || !session_impl_number_arg(tail+1,&q.cursor,&tail) || strcmp(tail," CONFIRM"))goto bad_profile; }
        else {
            arg=line+(!strncmp(line,"PROFILEFIND ",12)?12:11);const char *space=strchr(arg,' ');char key[32];size_t n=space?(size_t)(space-arg):strlen(arg);if(!n || n>=sizeof(key))goto bad_profile;memcpy(key,arg,n);key[n]=0;for(char *p=key;*p;p++)if(*p>='a' && *p<='z')*p-=32;int field=profile_field(key);const char *value=space?space+1:"";
            if(!strncmp(line,"PROFILEFIND ",12)) { q.op='Z';q.action=10;if(!strcmp(key,"GROUP")) { q.scope=100;if(!session_impl_number_arg(value,&q.number,&tail) || *tail || q.number>100)goto bad_profile;strcpy(q.title,"*"); }
                else { if(field<0 || !*value || strlen(value)>80)goto bad_profile;q.scope=field+1;strcpy(q.title,value); } }
            else if(field<0)goto bad_profile;
            else if(!strncmp(line,"PROFILEVIS ",11)) { q.action='V';q.number=(uint32_t)field;if(!strcmp(value,"PRIVATE"))q.cursor=0;else if(!strcmp(value,"MEMBERS"))q.cursor=1;else if(!strcmp(value,"MONTHDAY"))q.cursor=2;else goto bad_profile; }
            else { q.action='F';q.number=(uint32_t)field;strcpy(q.body,value); }
        }
        if(q.op=='Z') { rtbbs(s)->reading.profile_filter=q.scope;rtbbs(s)->reading.profile_group=q.number;if(rtbbs(s)->reading.profile_walk.active) { q.notes_unread=1;profile_walk_begin(&rtbbs(s)->reading.profile_walk,q.scope,q.number,q.title); } }
        session_impl_submit(s,&q);return 1;
bad_profile:session_impl_emit(s,"ERR profile text\r\n");return 1;
    }
    static const char *fields[]={"LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT"};
    for(unsigned i=0;i<5;i++) { size_t n=strlen(fields[i]);if(!strncmp(line,fields[i],n) && (!line[n] || line[n]==' ')) {
        const char *value=line[n]?line+n+1:"";if(strlen(value)>320 || !session_impl_safe_text(value)) { session_impl_emit(s,"ERR profile text\r\n");return 1; }
        BoardRequest q={0};q.op='Z';q.action=12+(int)i;strcpy(q.body,value);session_impl_submit(s,&q);return 1;
    } }

    BoardRequest q={0}; q.op='Z'; const char *arg=NULL,*tail=NULL;
    if(!strncmp(line,"NEWS ",5) || !strncmp(line,"NEWSFROM ",9)) {
        q.action=11;q.cursor=1;
        if(!strncmp(line,"NEWSFROM ",9)) { if(!session_impl_number_arg(line+9,&q.cursor,&tail) || *tail!=' ')goto syntax;arg=tail+1; }
        else arg=line+5;
        if(!strncmp(arg,"ALL ",4)) { q.scope=1;arg+=4; }
        else if(!strncmp(arg,"HERE ",5))arg+=5;
        uint64_t stamp;if(q.cursor>CONTENT_ID_MAX+1 || !date_filter(arg,&stamp))goto syntax;
        strcpy(q.title,arg);
    } else if(!strncmp(line,"WITHDRAW ",9)) {
        q.action=1;
        if(!session_impl_number_arg(line+9,&q.number,&tail) || (*tail && strcmp(tail," CONFIRM"))) goto syntax;
        q.cursor=*tail?1:0;
    } else if(!strncmp(line,"RETITLE ",8)) {
        q.action=2;
        if(!session_impl_number_arg(line+8,&q.number,&tail) || *tail!=' ' || !tail[1] || strlen(tail+1)>80 || !session_impl_safe_text(tail+1)) goto syntax;
        strcpy(q.title,tail+1);
    } else if(!strncmp(line,"FIND ",5) || !strncmp(line,"FINDFROM ",9) || !strncmp(line,"SCAN ",5)) {
        q.action=line[0]=='S'?4:3; q.cursor=1;
        if(!strncmp(line,"FINDFROM ",9)) { if(!session_impl_number_arg(line+9,&q.cursor,&tail) || *tail!=' ') goto syntax; arg=tail+1; }
        else arg=line+5;
        if(q.action==4 && (!strncmp(arg,"DESC ",5) || !strncmp(arg,"ASC ",4))) { q.descending=*arg=='D';arg+=q.descending?5:4;if(q.descending)q.cursor=UINT32_MAX; }
        if(!session_impl_find_arguments(arg,&q)) goto syntax;
    } else if(!strncmp(line,"READGLOBAL ",11)) {
        q.action=5; if(!session_impl_number_arg(line+11,&q.number,&tail) || *tail) goto syntax;
    } else if(!strcmp(line,"INFO") || !strncmp(line,"INFO ",5)) {
        q.action=6; if(line[4] && (!session_impl_number_arg(line+5,&q.number,&tail) || *tail)) goto syntax;
    } else if(!strcmp(line,"PROFILE") || !strncmp(line,"PROFILE ",8)) {
        q.action=7;
        if(line[7]) { if(!line[8] || strlen(line+8)>32 || strchr(line+8,' ')) goto syntax; strcpy(q.username,line+8); }
    } else if(!strncmp(line,"PROFILEID ",10)) {
        q.action=7; if(!session_impl_number_arg(line+10,&q.number,&tail) || *tail || q.number>MEMBER_MAX) goto syntax;
    } else if(s->core.user && (!strcmp(line,"HANDLE") || !strncmp(line,"HANDLE ",7))) {
        q.action=8; if(line[6]) { if(strlen(line+7)>80 || !session_impl_safe_text(line+7)) goto syntax; strcpy(q.title,line+7); }
    } else if(!strcmp(line,"BIO") || !strncmp(line,"BIO ",4)) {
        q.action=9; if(line[3]) { if(strlen(line+4)>320 || !session_impl_safe_text(line+4)) goto syntax; strcpy(q.body,line+4); }
    } else if(!strcmp(line,"PROFILES") || !strncmp(line,"PROFILES ",9) || !strncmp(line,"PROFILESFROM ",13)) {
        q.action=10;q.scope=rtbbs(s)->reading.profile_filter;q.number=rtbbs(s)->reading.profile_group;if(strncmp(line,"PROFILESFROM ",13)) { q.scope=0;q.number=0;rtbbs(s)->reading.profile_filter=0;rtbbs(s)->reading.profile_group=0; } q.cursor=1; arg="*";
        if(!strncmp(line,"PROFILESFROM ",13)) { if(!session_impl_number_arg(line+13,&q.cursor,&tail) || *tail!=' ' || q.cursor>MEMBER_MAX) goto syntax; arg=tail+1; }
        else if(line[8]) arg=line+9;
        if(!*arg || strlen(arg)>80 || !session_impl_safe_text(arg)) goto syntax;
        strcpy(q.title,arg);
    } else return 0;
    if(!s->core.board_enabled) { session_impl_emit(s,"ERR no board\r\n"); return 1; }
    session_impl_submit(s,&q);
    if(q.action==4 && s->core.pending) { rtbbs(s)->reading.scan=q; rtbbs(s)->reading.reader.active=1; }
    return 1;
syntax:
    session_impl_emit(s,"ERR extended syntax\r\n"); return 1;
}

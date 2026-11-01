#define _POSIX_C_SOURCE 200809L
#include "runtime_limits.h"
#include "app_limits.h"
#include "activity.h"
#include "vault.h"
#include "session.h"
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
void session_abort(Session *s) { s->active=0; }
static void binary_frame(Session *s,unsigned char kind,const void *data,size_t size) {
    unsigned char frame[BOARD_REPLY+6];size_t length=size+(kind?1:0);
    if(length>BOARD_REPLY || !length) { session_abort(s);return; }
    frame[0]='~';for(int i=0;i<4;i++)frame[1+i]=(unsigned char)(length>>(24-i*8));
    if(kind)frame[5]=kind;
    memcpy(frame+5+(kind?1:0),data,size);
    if(!s->io.write(s->io.context,(char *)frame,length+5))session_abort(s);
}
static void queue_text(Session *s,const char *text) {
    if(s->active && !s->io.write(s->io.context,text,strlen(text))) session_abort(s);
}
void session_dimensions(Session *s,unsigned columns,unsigned rows) {
    s->columns=columns>=20 && columns<=240?columns:80;
    s->rows=rows>=5 && rows<=100?rows:24;
}
static void page_clear(Session *s) { OPENSSL_cleanse(s->page,sizeof(s->page)); s->page_offset=0; s->help_page=0; }
/* Count common UTF-8 terminal cells without relying on the process locale. */
static unsigned cell_width(const unsigned char *p,size_t *bytes) {
    unsigned cp=p[0]; *bytes=1;
    unsigned n=cp>=0xf0 && cp<=0xf4?4:cp>=0xe0 && cp<=0xef?3:cp>=0xc2 && cp<=0xdf?2:1;
    if(n>1) {
        cp&=(1u<<(7-n))-1;
        for(unsigned i=1;i<n;i++) { if(!p[i] || (p[i]&0xc0)!=0x80) return 1; cp=(cp<<6)|(p[i]&63); }
        *bytes=n;
    }
    if((cp>=0x300 && cp<=0x36f) || (cp>=0xfe00 && cp<=0xfe0f) || cp==0x200d) return 0;
    return (cp>=0x1100 && (cp<=0x115f || cp==0x2329 || cp==0x232a ||
            (cp>=0x2e80 && cp<=0xa4cf) || (cp>=0xac00 && cp<=0xd7a3) ||
            (cp>=0xf900 && cp<=0xfaff) || (cp>=0xfe10 && cp<=0xfe6f) ||
            (cp>=0xff01 && cp<=0xff60) || (cp>=0xffe0 && cp<=0xffe6) || cp>=0x1f300))?2:1;
}
static void page_next(Session *s) {
    char out[8192]; size_t used=0; unsigned lines=0,col=0;
    unsigned limit=s->rows-3,width=s->columns-1;
    while(s->page[s->page_offset] && lines<limit) {
        const unsigned char *p=(const unsigned char *)s->page+s->page_offset;
        if(*p=='\r' || *p=='\n') {
            size_t n=(*p=='\r' && p[1]=='\n')?2:1;
            if(used+2>=sizeof(out)) break;
            out[used++]='\r'; out[used++]='\n'; s->page_offset+=n; lines++; col=0; continue;
        }
        size_t n; unsigned cells=cell_width(p,&n);
        if(col+cells>width) {
            if(used+2>=sizeof(out)) break;
            out[used++]='\r'; out[used++]='\n'; lines++; col=0; continue;
        }
        if(used+n>=sizeof(out)) break;
        memcpy(out+used,p,n); used+=n; s->page_offset+=n; col+=cells;
    }
    out[used]=0; queue_text(s,out);
    if(s->page[s->page_offset]) queue_text(s,ui_text(&s->ui,MSG_PAGER_MORE));
    else page_clear(s);
}
/* Only complete, authorized article/list responses enter the display pager.
 * UI cursor/view state belongs to the logical result, not a screen fragment. */
static int paged_response(const char *text) {
    static const char *prefixes[]={"ARTICLE ","PREVIEW ","DRAFT LINES ","NOTES ","TREE ","UNREAD ","LIST ","SEARCH ","AUDIT ","FIND ","NEWS ","INFO ","PROFILE ","PROFILES ","FILES", "FILE ","FILEAUDIT","FILECATEGORIES","FILEBOARDS","FILENEWS","FILEHISTORY","FILETEXT","USERTABLE","USERLOG","BOARDLOG","BOARDACCESS"};
    for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++)
        if(!strncmp(text,prefixes[i],strlen(prefixes[i]))) return 1;
    return !strcmp(text,"BOARDS\r\n") || !strncmp(text,"BOARDS\r\n",8) ||
           !strcmp(text,"MEMBERS\r\n") || !strncmp(text,"MEMBERS\r\n",9);
}
static void emit(Session *s,const char *text) {
    if(s->binary) { binary_frame(s,'T',text,strlen(text));return; }
    char formatted[CONTENT_DISPLAY];
    if(s->ui.enabled) {
        if(!ui_format(&s->ui,text,formatted,sizeof(formatted))) { session_abort(s); return; }
        if(s->pager_enabled && !s->auto_read && paged_response(text)) {
            snprintf(s->page,sizeof(s->page),"%s",formatted); s->page_offset=0; page_next(s);
        } else queue_text(s,formatted);
    } else queue_text(s,text);
}
static unsigned editor_count(const Session *s);
static void command_core(Session *,char *);
enum { CI_NONE, CI_DATE, CI_AUTHOR, CI_HANDLE, CI_PROFILE, CI_PROFILE_SEARCH,
       CI_MEMO, CI_WITHDRAW, CI_HEADER, CI_TITLE, CI_OWNER, CI_EDITOR, CI_MENUMODE, CI_ROWS,
       CI_FILEINFO, CI_DOWNLOAD, CI_UPLOAD, CI_EXIT, CI_PROTOCOL, CI_FILENAME, CI_FILESIZE, CI_BATCH, CI_BATCH_ADD, CI_BATCH_REMOVE, CI_BATCH_MATCH,CI_PROF_FIELD,CI_PROF_VALUE,CI_SEARCH_FIELD,CI_SEARCH_VALUE,CI_BATCH_CLEAR,CI_SEND_PROTOCOL,CI_FILE_ACTION,CI_FILE_DESC,CI_FILE_DELETE,CI_PROF_TARGET,CI_PROF_NEXT,CI_BATCH_COPY };
enum { CM_MAIN, CM_BOARD, CM_SETTINGS, CM_PROFILE, CM_ADMIN };
/* Do not carry per-account selection or private search state across login. */
static void selection_reset(Session *s) {
    s->file_selected=s->file_selecting=0;s->batch_screen=s->batch_copying=0;
    s->batch_count=s->batch_copy_at=0;memset(s->batch_done,0,sizeof(s->batch_done));
    memset(s->batch_copied,0,sizeof(s->batch_copied));s->batch_copy_path[0]=0;
    s->vault_child_view=s->vault_child_count=s->vault_child_next=0;
    s->vault_mode=s->vault_selecting=0;s->vault_path[0]=s->vault_select[0]=0;
    memset(s->vault_grants,0,sizeof(s->vault_grants));
    s->profile_walk=s->profile_walk_pending=s->profile_edit_target=0;
    OPENSSL_cleanse(&s->profile_search,sizeof(s->profile_search));
    s->talk_once=s->talk_return_editor=s->status_pending=s->memo_input=0;
    s->talk_target=s->reply_target=0;s->talk_select=s->talk_lost=s->outside_talk=0;
    s->classic_input=0;s->classic_id=0;s->classic_area=CM_MAIN;
}
static void show_menu(Session *s,int explicit_request) {
    if(s->classic_area) {
        unsigned part=s->classic_area==CM_BOARD?(s->ui.view==UI_ARTICLE?1:0):s->classic_area==CM_SETTINGS?2:s->classic_area==CM_PROFILE?3:s->ui.admin?4:5;
        static const MessageId fallback[]={MSG_CLASSIC_BOARD_MENU,MSG_CLASSIC_READER_MENU,MSG_CLASSIC_SETTINGS_MENU,MSG_CLASSIC_PROFILE_MENU,MSG_CLASSIC_ADMIN_MENU,MSG_CLASSIC_SIGOP_MENU};
        const char *text=menu_section_text(s->ui.language,part);
        queue_text(s,text?text:ui_text(&s->ui,fallback[part]));return;
    }
    if(explicit_request || !s->menu_mode || (s->menu_mode==1 && !s->menu_shown))queue_text(s,ui_menu(&s->ui));
    s->menu_shown=1;
}
static void prompt(Session *s) {
    if(s->auto_read || s->door || s->memo_input || s->application_step || (s->editing && s->editor_input==4))return;
    if(!s->zmodem && !s->chatting && !s->binary && s->ui.enabled && s->active && !s->pending && !s->closing && !s->wake && !s->page[0]) {
        if(s->gather_mode) { queue_text(s,ui_text(&s->ui,s->gather_cursor?MSG_GATHER_MORE:MSG_GATHER_PROMPT));return; }
        if(s->vault_mode && !s->editing && !s->password_mode && !s->ui.input && (!s->classic_input || s->classic_input==CI_FILE_ACTION || s->classic_input==CI_BATCH)) {
            if(s->batch_screen) { s->classic_input=CI_BATCH;queue_text(s,ui_text(&s->ui,MSG_TRANSFER_BATCH));return; }
            if(s->file_selected) { s->classic_input=CI_FILE_ACTION;queue_text(s,s->ui.language==LANGUAGE_JA?"T:表示 S/C/X/Y/G/Z/M:転送 B:選択 E:説明 K:削除 0:戻る > ":"T:view S/C/X/Y/G/Z/M:transfer B:select E:description K:delete 0:back > ");return; }
        }
        if(s->classic_input || (s->editing && (s->editor_confirm || s->editor_upload_question)))return;
        if(s->editing && s->editor_line && !s->editor_input) { queue_text(s,ui_text(&s->ui,MSG_EDITOR_PROMPT));return; }
        if(s->editing && s->editor_numbers) { char number[40];snprintf(number,sizeof(number),"%u ",s->editor_line?s->editor_at:editor_count(s)+1);queue_text(s,number); }
        if(s->vault_mode && !s->editing && !s->password_mode && !s->ui.input) { queue_text(s,ui_text(&s->ui,MSG_VAULT_PROMPT));return; }
        if(s->mail_input) { queue_text(s,ui_text(&s->ui,s->mail_input==1?MSG_MAIL_TO_PROMPT:MSG_MAIL_TITLE_PROMPT));return; }
    if(s->mail_delete) { queue_text(s,ui_text(&s->ui,MSG_MAIL_CONFIRM));return; }
        if(s->mail_mode && !s->editing && !s->password_mode) { queue_text(s,ui_text(&s->ui,MSG_MAIL_PROMPT));return; }
        if(s->scanning && !s->auto_read) queue_text(s,ui_text(&s->ui,MSG_SCAN_HINT));
        if(!s->editing && !s->password_mode && !s->ui.input && !s->scanning && s->ui.view==UI_HOME)show_menu(s,0);
        char text[512];size_t prefix=0;
        if(!s->editing && !s->password_mode && !s->ui.input)
            prefix=(size_t)snprintf(text,sizeof(text),"%s",s->classic_area==CM_BOARD?(s->ui.view==UI_ARTICLE?"READ ":"BOARD "):s->classic_area==CM_SETTINGS?"INSTALL ":s->classic_area==CM_PROFILE?"PROFILE ":"MAIN ");
        /* One write: asynchronous notices redraw pending terminal input once. */
        ui_prompt(&s->ui,s->board,s->current,s->user?s->username:*s->guest_handle?s->guest_handle:ui_text(&s->ui,MSG_GUEST),s->editing,s->password_mode,text+prefix,sizeof(text)-prefix);
        queue_text(s,text);
    }
}
/* Help is local display only: preserve board, selection and list cursors. */
static void show_help(Session *s,const char *topic) {
    static const char *names[]={"", "READ", "UNREAD", "POST", "DRAFT", "SETTINGS", "ACCOUNT", "ADMIN", "CONTENT", "PROFILE", "FILES", "CHAT", "TALK", "MAIL", "GATHER", "SIGOP", "EDITOR", "ADMINMEMBERS", "ADMINBOARDS", "ADMINFILES", "ADMINCONNECTIONS", "ADMINSTORAGE"};
    static const MessageId ids[]={MSG_HELP_INDEX,MSG_HELP_READ,MSG_HELP_UNREAD,MSG_HELP_POST,
                                  MSG_HELP_DRAFT,MSG_HELP_SETTINGS,MSG_HELP_ACCOUNT,MSG_HELP_ADMIN,MSG_HELP_CONTENT,MSG_HELP_PROFILE,MSG_HELP_FILES,MSG_HELP_CHAT,MSG_HELP_CHAT,MSG_HELP_MAIL,MSG_HELP_GATHER,MSG_HELP_SIGOP,MSG_EDITOR_HELP,MSG_HELP_ADMINMEMBERS,MSG_HELP_ADMINBOARDS,MSG_HELP_ADMINFILES,MSG_HELP_ADMINCONNECTIONS,MSG_HELP_ADMINSTORAGE};
    char key[32]; size_t n=strlen(topic);
    if(n>=sizeof(key)) { queue_text(s,ui_text(&s->ui,MSG_HELP_UNKNOWN)); if(!s->ui.enabled) queue_text(s,"END HELP\r\n"); return; }
    for(size_t i=0;i<=n;i++) key[i]=topic[i]>='a' && topic[i]<='z'?topic[i]-32:topic[i];
    MessageId id=MSG_HELP_UNKNOWN;
    for(size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++) if(!strcmp(key,names[i])) { id=ids[i]; break; }
    if((!strncmp(key,"ADMIN",5) && !s->ui.admin) || (id==MSG_HELP_SIGOP && !s->ui.admin && !s->ui.sigop)) { emit(s,"ERR admin required\r\n");if(!s->ui.enabled)queue_text(s,"END HELP\r\n");return; }
    const char *external=help_text(s->ui.language,key);const char *text=external?external:ui_text(&s->ui,id);
    char filtered[CONTENT_DISPLAY];
    if(id==MSG_HELP_INDEX && !s->ui.admin) {
        snprintf(filtered,sizeof(filtered),"%s",text);char *entry=strstr(filtered,"HELP ADMIN");
        if(entry) { char *end=strstr(entry,"\r\n");if(end)memmove(entry,end+2,strlen(end+2)+1);else *entry=0; }text=filtered;
    }
    if(id==MSG_HELP_INDEX && (s->ui.admin || s->ui.sigop)) {
        if(text!=filtered)snprintf(filtered,sizeof(filtered),"%s",text);
        size_t have=strlen(filtered);snprintf(filtered+have,sizeof(filtered)-have,"%s",ui_text(&s->ui,MSG_HELP_SIGOP_LINK));text=filtered;
    }
    if(s->ui.enabled && s->pager_enabled) {
        snprintf(s->page,sizeof(s->page),"%s",text); s->page_offset=0; s->help_page=1; page_next(s);
    } else {
        queue_text(s,text);
        if(!s->ui.enabled) queue_text(s,"END HELP\r\n");
    }
}
static int number_arg(const char *p,uint32_t *number,const char **rest) {
    if(*p<'0' || *p>'9') return 0;
    uint32_t n=0;
    while(*p>='0' && *p<='9') { unsigned digit=(unsigned)(*p++-'0');if(n>(CONTENT_ID_MAX+1-digit)/10)return 0;n=n*10+digit; }
    if(!n) return 0;
    *number=n; *rest=p; return 1;
}
static int audit_number(const char *p,uint32_t *out) {
    if(!*p) return 0;
    uint32_t n=0;
    for(;*p;p++) {
        if(*p<'0' || *p>'9') return 0;
        unsigned digit=(unsigned)(*p-'0');
        if(n>(UINT32_MAX-digit)/10) return 0;
        n=n*10+digit;
    }
    if(!n) return 0;
    *out=n; return 1;
}
/* AUDIT [sequence] [ARTICLE id] [ACTOR login | ACTORID id]. */
static int audit_arguments(const char *args,BoardRequest *q) {
    if(!strcmp(args,"ALL")) return 1;
    char copy[256]; if(!*args || strlen(args)>=sizeof(copy)) return 0;
    strcpy(copy,args); char *tokens[8],*p=copy; unsigned count=0;
    while(*p) {
        if(count==8 || *p==' ') return 0;
        tokens[count++]=p;
        while(*p && *p!=' ') p++;
        if(*p) { *p++=0; if(!*p) return 0; }
    }
    if(!count)return 0;
    unsigned i=0; int actor_set=0;
    if(strcmp(tokens[0],"ARTICLE") && strcmp(tokens[0],"ACTOR") && strcmp(tokens[0],"ACTORID")) {
        if(!audit_number(tokens[0],&q->number)) return 0;
        i++;
    }
    while(i<count) {
        const char *key=tokens[i++]; if(i==count) return 0;
        const char *value=tokens[i++];
        if(!strcmp(key,"ARTICLE")) {
            if(q->audit_article || !audit_number(value,&q->audit_article) || q->audit_article>CONTENT_ID_MAX) return 0;
        } else if(!strcmp(key,"ACTORID")) {
            if(actor_set++ || !audit_number(value,&q->audit_actor) || q->audit_actor>MEMBER_MAX) return 0;
        } else if(!strcmp(key,"ACTOR")) {
            if(actor_set++ || strlen(value)>32) return 0;
            for(const unsigned char *v=(const unsigned char *)value;*v;v++) if(*v<33 || *v>126) return 0;
            strcpy(q->title,value);
        } else return 0;
    }
    return 1;
}
static void audit_command(const Ui *ui,uint32_t cursor,char *out,size_t size) {
    size_t n=(size_t)snprintf(out,size,"AUDIT");
    if(cursor) n+=(size_t)snprintf(out+n,size-n," %u",cursor);
    if(ui->audit_article) n+=(size_t)snprintf(out+n,size-n," ARTICLE %u",ui->audit_article);
    if(ui->audit_actor) snprintf(out+n,size-n," ACTORID %u",ui->audit_actor);
}
static int safe_text(const char *p) {
    for(;*p;p++) if((unsigned char)*p<32 || (unsigned char)*p==127) return 0;
    return 1;
}
static void submit(Session *s,BoardRequest *q) {
    q->session=s->id; q->slot=s->slot;
    if(q->op=='V') { q->ui_aux=(unsigned)s->editor_numbers | (s->transfer_protocol<<1);q->scope=s->editor_line;q->audit_article=s->columns;q->audit_actor=s->rows; }
    q->user=s->user; q->user_epoch=s->user_epoch;if(!s->user && q->op=='P')snprintf(q->username,sizeof(q->username),"%s",*s->guest_handle?s->guest_handle:"guest");
    if(q->op=='j' && s->now<s->auth_after)q->action='t';
    if(q!=&s->scan)q->board=s->board;
    if(q->op=='Z' && q->until && s->checking==1)q->board=s->check_board;
    memcpy(q->board_grants,s->board_grants,sizeof(q->board_grants));memcpy(q->vault_grants,s->vault_grants,sizeof(q->vault_grants));
    if(q->op=='v') s->ui.unread_root=q->number;
    if(board_submit(q)) s->pending=1;
    else emit(s,"ERR board busy\r\n");
}
static void check_page(Session *s) {
    BoardRequest q={0};q.since=s->check_since;q.until=s->check_until;q.cursor=s->check_cursor;
    if(s->checking==1) { q.op='Z';q.action=11;q.scope=s->check_scope;strcpy(q.title,"CHECK"); }
    else if(s->checking==2) { q.op='m';q.action='L';q.number=5;strcpy(q.title,"CHECK"); }
    else { q.op='Y';q.action=VAULT_CMD_NEWS;q.number=s->check_cursor;snprintf(q.body,sizeof(q.body),"%s%s@%llu:%llu",*s->check_key?s->check_key:"",*s->check_key?"|":"",(unsigned long long)q.since,(unsigned long long)q.until); }
    submit(s,&q);s->check_pending=s->pending;
}
static int find_arguments(const char *arg,BoardRequest *q) {
    const char *p=strchr(arg,' '); if(!p) return 0;
    size_t n=(size_t)(p-arg);
    if(n==3 && !strncmp(arg,"ALL",3)) q->scope=1;
    else if(n==4 && !strncmp(arg,"HERE",4)) q->scope=0;
    else if(n==7 && !strncmp(arg,"SUBTREE",7)) q->scope=2;
    else return 0;
    arg=p+1; p=strchr(arg,' '); if(!p) return 0; n=(size_t)(p-arg);
    if(!n || n>32 || !p[1] || strlen(p+1)>80 || !safe_text(p+1)) return 0;
    memcpy(q->username,arg,n); q->username[n]=0; strcpy(q->title,p+1); return 1;
}
static int extended_command(Session *s,const char *line) {
    const char *mail_transfers[]={"MAILUPLOAD","MAILUPLOADBATCH","MAILDOWNLOAD","MAILDOWNLOADALL"};
    for(unsigned i=0;i<4;i++) {
        size_t n=strlen(mail_transfers[i]);if(strncmp(line,mail_transfers[i],n) || (line[n] && line[n]!=' '))continue;
        if((i==1 || i==3) && (s->transfer_protocol==1 || s->transfer_protocol>=5)) { emit(s,"ERR this protocol transfers one file at a time\r\n");return 1; }
        char mapped[INPUT_LINE_MAX+2];snprintf(mapped,sizeof(mapped),"Z%s",line);command_core(s,mapped);
        if(s->pending) { s->zmodem_protocol=s->transfer_protocol;s->mail_batch_protocol=s->transfer_protocol; }
        return 1;
    }
    if(!strcmp(line,"FILECHILDREN") || !strncmp(line,"FILECHILDREN ",13)) {
        BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_CHILDREN;const char *path=line[12]?line+13:(*s->vault_path?s->vault_path:"-");
        if(!*path || strlen(path)>48 || !safe_text(path)) { emit(s,"ERR vault syntax\r\n");return 1; }
        strcpy(q.body,path);submit(s,&q);return 1;
    }
    if(!strcmp(line,"FILEBOARD") || !strncmp(line,"FILEBOARD ",10)) {
        const char *path=line[9]?line+10:"-";
        if(!*path || strlen(path)>48 || !safe_text(path)) { emit(s,"ERR vault syntax\r\n");return 1; }
        BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_BOARDINFO;strcpy(q.title,path);strcpy(s->vault_select,path);s->vault_selecting=1;submit(s,&q);if(!s->pending)s->vault_selecting=0;return 1;
    }
    if(!strcmp(line,"CHECKMODE AUTO") || !strcmp(line,"CHECKMODE MANUAL")) { s->check_auto=!strcmp(line,"CHECKMODE AUTO");emit(s,s->check_auto?"CHECKMODE AUTO\r\n":"CHECKMODE MANUAL\r\n");return 1; }
    if(!strncmp(line,"CHECK ",6)) {
        const char *kind=line+6;BoardRequest q={0};q.op='}';
        if(!strcmp(kind,"NEXT")) { if(!s->checking || s->check_finished)emit(s,"CHECK END; CHECK DONE to save checkpoint\r\n");else check_page(s);return 1; }
        if(!strcmp(kind,"CANCEL")) { s->checking=0;emit(s,"CHECK CANCELLED\r\n");return 1; }
        if(!strcmp(kind,"DONE")) { if(!s->checking || !s->check_finished) { emit(s,"ERR finish CHECK NEXT first\r\n");return 1; }q.action='n';q.number=(uint32_t)s->checking-1;q.until=s->check_until;strcpy(q.title,s->check_key);submit(s,&q);s->check_committing=s->pending;return 1; }
        q.action='N';s->check_key[0]=0;s->check_scope=1;s->check_board=s->board;
        q.number=!strcmp(kind,"ARTICLES") || !strcmp(kind,"ARTICLES ALL")?0:!strcmp(kind,"MAIL")?1:!strcmp(kind,"FILES")?2:3;
        if(!strcmp(kind,"ARTICLES HERE") || !strcmp(kind,"ARTICLES SUBTREE")) { q.number=0;s->check_scope=!strcmp(kind,"ARTICLES HERE")?0:2;snprintf(s->check_key,sizeof(s->check_key),"%u:%u",s->board,(unsigned)s->check_scope); }
        else if(!strncmp(kind,"FILES ",6) && strlen(kind+6)<=48 && safe_text(kind+6) && !strchr(kind+6,'|')) { q.number=2;strcpy(s->check_key,kind+6); }
        strcpy(q.title,s->check_key);
        if(q.number==3)emit(s,"ERR CHECK ARTICLES [HERE|SUBTREE|ALL]|MAIL|FILES [path]|NEXT|DONE|CANCEL\r\n");else submit(s,&q);return 1;
    }
    if(!strncmp(line,"SIGOPGROUP ",11) || !strncmp(line,"BOARDGROUP ",11)) {
        BoardRequest q={0};q.op='}';q.action=*line=='S'?'G':'g';const char *tail;
        if(!number_arg(line+11,&q.number,&tail) || *tail!=' ' || !number_arg(tail+1,&q.cursor,&tail)) { emit(s,"ERR command\r\n");return 1; }
        if(q.action=='g') { if(*tail!=' ' || !number_arg(tail+1,&q.audit_actor,&tail) || strcmp(tail," CONFIRM")) { emit(s,"ERR command\r\n");return 1; } }
        else if(*tail) { emit(s,"ERR command\r\n");return 1; }
        submit(s,&q);return 1;
    }
    if(!strncmp(line,"APPLICATION ",12) || !strncmp(line,"APPLICATIONSET ",15)) {
        BoardRequest q={0};q.op='}';const char *tail;int set=!strncmp(line,"APPLICATIONSET ",15);
        if(!number_arg(line+(set?15:12),&q.number,&tail)) { emit(s,"ERR command\r\n");return 1; }
        q.action='A';if(set) { if(*tail!=' ') { emit(s,"ERR command\r\n");return 1; }const char *field=++tail,*space=strchr(field,' ');size_t n=space?(size_t)(space-field):strlen(field);if(!n || n>32 || (space && strlen(space+1)>320)) { emit(s,"ERR command\r\n");return 1; }memcpy(q.title,field,n);if(space)strcpy(q.body,space+1);int f=profile_field(q.title);if(f>=0) { q.op=')';q.action='A';q.cursor=q.number;q.number=(uint32_t)f; }else q.action='a'; }
        else if(*tail) { emit(s,"ERR command\r\n");return 1; }
        submit(s,&q);return 1;
    }
    if(!strncmp(line,"ZMAILDOWNLOADALL ",17)) { s->mail_batch_protocol=0;BoardRequest q={0};q.op='}';q.action='B';const char *tail;if(!s->zmodem_available || !s->binary_allowed || !number_arg(line+17,&q.number,&tail) || *tail) { emit(s,"ERR command or transfer unavailable\r\n");return 1; }submit(s,&q);return 1; }

    if(!strncmp(line,"FILEBOARDPASS ",14) || !strncmp(line,"FILEUNLOCK ",11)) {
        int set=!strncmp(line,"FILEBOARDPASS ",14);const char *p=line+(set?14:11);size_t n=strcspn(p," ");
        if(!s->user || !n || n>48 || (p[n] && strcmp(p+n," NONE CONFIRM"))) { emit(s,"ERR command\r\n");return 1; }
        memcpy(s->filepass_path,p,n);s->filepass_path[n]=0;
        if(p[n]) { if(!set) { emit(s,"ERR command\r\n");return 1; }BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_PASSWORD;snprintf(q.body,sizeof(q.body),"%s|",s->filepass_path);submit(s,&q); }
        else { s->filepass_setting=set;s->password_mode=set?'v':'f';emit(s,set?"NEW PASSWORD\r\n":"PASSWORD\r\n"); }
        return 1;
    }
    if(!strncmp(line,"FILERENAME ",11) || !strncmp(line,"FILEBOARDOPTIONS ",17)) {
        BoardRequest q={0};q.op='Y';const char *tail;
        if(line[4]=='R') { q.action=VAULT_CMD_RENAME;if(!number_arg(line+11,&q.number,&tail) || *tail!=' ' || strlen(tail+1)>100) { emit(s,"ERR command\r\n");return 1; }strcpy(q.body,tail+1); }
        else { q.action=VAULT_CMD_OPTIONS;if(strlen(line+17)>160 || !strchr(line+17,'|')) { emit(s,"ERR command\r\n");return 1; }strcpy(q.body,line+17); }
        submit(s,&q);return 1;
    }
    if(!strncmp(line,"MEMBERPROFILE ",14)) {
        BoardRequest q={0};const char *tail;q.op=')';q.action='A';
        if(!number_arg(line+14,&q.cursor,&tail) || *tail!=' ') { emit(s,"ERR command\r\n");return 1; }
        const char *space=strchr(++tail,' ');char key[32];size_t n=space?(size_t)(space-tail):strlen(tail);
        if(!n || n>=sizeof(key) || (space && strlen(space+1)>BOARD_BODY)) { emit(s,"ERR command\r\n");return 1; }
        memcpy(key,tail,n);key[n]=0;for(char *p=key;*p;p++)if(*p>='a' && *p<='z')*p-=32;
        int f=profile_field(key);if(f<0) { emit(s,"ERR profile field\r\n");return 1; }q.number=(uint32_t)f;if(space)strcpy(q.body,space+1);submit(s,&q);return 1;
    }
    if(!strcmp(line,"PROTOCOL") || !strncmp(line,"PROTOCOL ",9)) {
        const char *names[]={"ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};
        if(line[8]) { unsigned i;for(i=0;i<8;i++)if(!strcmp(line+9,names[i]))break;if(i==8) { emit(s,"ERR command\r\n");return 1; }s->transfer_protocol=i; }
        char text[160];snprintf(text,sizeof(text),"PROTOCOL %s\r\n",names[s->transfer_protocol]);emit(s,text);return 1;
    }
    if(!strncmp(line,"DOWNLOAD ",9) || !strncmp(line,"UPLOAD ",7) || !strncmp(line,"UPLOADBATCH ",12)) {
        if(!s->user) { emit(s,"ERR login required\r\n");return 1; }
        if(!board_feature_enabled("vault")) { emit(s,"ERR feature disabled\r\n");return 1; }
        if(!s->zmodem_available || !s->binary_allowed) { queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return 1; }
        BoardRequest q={0};q.op='@';const char *arg=strchr(line,' ')+1,*tail;int receive=*line=='U',batch=!strncmp(line,"UPLOADBATCH ",12);
        if(batch && (s->transfer_protocol==1 || s->transfer_protocol>=5)) { emit(s,"ERR vault syntax\r\n");return 1; }
        if(!receive) { if(!number_arg(arg,&q.number,&tail) || *tail || q.number>VAULT_ID_MAX) { emit(s,"ERR vault syntax\r\n");return 1; }s->zmodem_text[0]=0; }
        else { if(strlen(arg)>512 || !safe_text(arg)) { emit(s,"ERR vault syntax\r\n");return 1; }strcpy(s->zmodem_text,arg); }
        s->zmodem_id=q.number;s->zmodem_receive=batch?6:receive?3:0;s->zmodem_protocol=s->transfer_protocol;s->zmodem_start=1;submit(s,&q);if(!s->pending)s->zmodem_start=0;return 1;
    }

    if(!strncmp(line,"FILECOPY ",9)) { BoardRequest q={0};const char *end;q.op='Y';q.action=VAULT_CMD_COPY;if(!number_arg(line+9,&q.number,&end) || *end!=' ' || strlen(end+1)>48 || !safe_text(end+1)) { emit(s,"ERR file copy\r\n");return 1; }strcpy(q.body,end+1);submit(s,&q);return 1; }
    if(!strncmp(line,"FILEOWNER ",10) || !strncmp(line,"FILEASCII ",10) || !strncmp(line,"FILETYPE ",9) || !strncmp(line,"FILEBOARDNAME ",14) || !strncmp(line,"FILEBOARDSIGOP ",15) || !strncmp(line,"FILEINFOEDIT ",13)) {
        BoardRequest q={0};q.op='Y';const char *arg=strchr(line,' ')+1,*tail;
    if(!strncmp(line,"FILEOWNER ",10)) { q.action=VAULT_CMD_OWNER;if(!number_arg(arg,&q.number,&tail) || *tail!=' ' || !number_arg(tail+1,&q.cursor,&tail) || *tail)goto newvault_bad; }
        else if(!strncmp(line,"FILEASCII ",10)) { q.action=VAULT_CMD_ASCII;if(!number_arg(arg,&q.number,&tail) || *tail!=' ' || strlen(tail+1)>3)goto newvault_bad;strcpy(q.body,tail+1);for(char *p=q.body;*p;p++)if(*p>='a' && *p<='z')*p-=32; }
        else if(!strncmp(line,"FILETYPE ",9)) { q.action=VAULT_CMD_TEXT;if(!number_arg(arg,&q.number,&tail))goto newvault_bad;if(*tail && (*tail!=' ' || !number_arg(tail+1,&q.cursor,&tail) || *tail))goto newvault_bad;snprintf(q.body,sizeof(q.body),"%u",q.cursor); }
        else if(!strncmp(line,"FILEINFOEDIT ",13)) { q.action=VAULT_CMD_BOARDEDIT;if(!*arg || strlen(arg)>48 || !safe_text(arg))goto newvault_bad;strcpy(q.body,arg); }
        else { const char *bar=strchr(arg,'|');if(!bar || bar-arg>48)goto newvault_bad;
            if(!strncmp(line,"FILEBOARDSIGOP ",15)) { q.action=VAULT_CMD_BOARDSIGOP;if(strcmp(bar+1,"NONE") && (!number_arg(bar+1,&q.cursor,&tail) || *tail))goto newvault_bad;memcpy(q.title,arg,(size_t)(bar-arg)); }
            else { q.action=VAULT_CMD_BOARDNAME;if(strlen(arg)>129 || !safe_text(arg))goto newvault_bad;strcpy(q.body,arg); }
        }
        submit(s,&q);if(q.action==VAULT_CMD_TEXT && s->pending) { snprintf(s->report,sizeof(s->report),"FILETYPE %u",q.number);s->report_pending=1; }return 1;
newvault_bad:emit(s,"ERR vault syntax\r\n");return 1;
    }

    if(!strcmp(line,"BIOEDIT") || !strcmp(line,"BUFFER") || !strcmp(line,"BUFFER CLEAR") || !strncmp(line,"PROFILESET ",11) || !strncmp(line,"PROFILEVIS ",11) || !strncmp(line,"PROFILEFIND ",12) || !strncmp(line,"GROUPMOVE ",10)) {
        BoardRequest q={0};q.op=')';const char *arg=NULL,*tail=NULL;
        if(!strcmp(line,"BIOEDIT") || !strcmp(line,"BUFFER")) { q.action='I';q.number=!strcmp(line,"BIOEDIT")?1:2; }
        else if(!strcmp(line,"BUFFER CLEAR"))q.action='C';
        else if(!strncmp(line,"GROUPMOVE ",10)) { q.action='G';if(!number_arg(line+10,&q.number,&tail) || *tail!=' ' || !number_arg(tail+1,&q.cursor,&tail) || strcmp(tail," CONFIRM"))goto bad_profile; }
        else {
            arg=line+(!strncmp(line,"PROFILEFIND ",12)?12:11);const char *space=strchr(arg,' ');char key[32];size_t n=space?(size_t)(space-arg):strlen(arg);if(!n || n>=sizeof(key))goto bad_profile;memcpy(key,arg,n);key[n]=0;for(char *p=key;*p;p++)if(*p>='a' && *p<='z')*p-=32;int field=profile_field(key);const char *value=space?space+1:"";
            if(!strncmp(line,"PROFILEFIND ",12)) { q.op='Z';q.action=10;if(!strcmp(key,"GROUP")) { q.scope=100;if(!number_arg(value,&q.number,&tail) || *tail || q.number>100)goto bad_profile;strcpy(q.title,"*"); }
                else { if(field<0 || !*value || strlen(value)>80)goto bad_profile;q.scope=field+1;strcpy(q.title,value); } }
            else if(field<0)goto bad_profile;
            else if(!strncmp(line,"PROFILEVIS ",11)) { q.action='V';q.number=(uint32_t)field;if(!strcmp(value,"PRIVATE"))q.cursor=0;else if(!strcmp(value,"MEMBERS"))q.cursor=1;else if(!strcmp(value,"MONTHDAY"))q.cursor=2;else goto bad_profile; }
            else { q.action='F';q.number=(uint32_t)field;strcpy(q.body,value); }
        }
        if(q.op=='Z') { s->profile_filter=q.scope;s->profile_group=q.number;if(s->profile_walk) { q.notes_unread=1;s->profile_search=q;s->profile_walk_pending=1; } }
        submit(s,&q);return 1;
bad_profile:emit(s,"ERR profile text\r\n");return 1;
    }
    static const char *fields[]={"LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT"};
    for(unsigned i=0;i<5;i++) { size_t n=strlen(fields[i]);if(!strncmp(line,fields[i],n) && (!line[n] || line[n]==' ')) {
        const char *value=line[n]?line+n+1:"";if(strlen(value)>320 || !safe_text(value)) { emit(s,"ERR profile text\r\n");return 1; }
        BoardRequest q={0};q.op='Z';q.action=12+(int)i;strcpy(q.body,value);submit(s,&q);return 1;
    } }

    BoardRequest q={0}; q.op='Z'; const char *arg=NULL,*tail=NULL;
    if(!strncmp(line,"NEWS ",5) || !strncmp(line,"NEWSFROM ",9)) {
        q.action=11;q.cursor=1;
        if(!strncmp(line,"NEWSFROM ",9)) { if(!number_arg(line+9,&q.cursor,&tail) || *tail!=' ')goto syntax;arg=tail+1; }
        else arg=line+5;
        if(!strncmp(arg,"ALL ",4)) { q.scope=1;arg+=4; }
        else if(!strncmp(arg,"HERE ",5))arg+=5;
        uint64_t stamp;if(q.cursor>CONTENT_ID_MAX+1 || !date_filter(arg,&stamp))goto syntax;
        strcpy(q.title,arg);
    } else if(!strncmp(line,"WITHDRAW ",9)) {
        q.action=1;
        if(!number_arg(line+9,&q.number,&tail) || (*tail && strcmp(tail," CONFIRM"))) goto syntax;
        q.cursor=*tail?1:0;
    } else if(!strncmp(line,"RETITLE ",8)) {
        q.action=2;
        if(!number_arg(line+8,&q.number,&tail) || *tail!=' ' || !tail[1] || strlen(tail+1)>80 || !safe_text(tail+1)) goto syntax;
        strcpy(q.title,tail+1);
    } else if(!strncmp(line,"FIND ",5) || !strncmp(line,"FINDFROM ",9) || !strncmp(line,"SCAN ",5)) {
        q.action=line[0]=='S'?4:3; q.cursor=1;
        if(!strncmp(line,"FINDFROM ",9)) { if(!number_arg(line+9,&q.cursor,&tail) || *tail!=' ') goto syntax; arg=tail+1; }
        else arg=line+5;
        if(q.action==4 && (!strncmp(arg,"DESC ",5) || !strncmp(arg,"ASC ",4))) { q.descending=*arg=='D';arg+=q.descending?5:4;if(q.descending)q.cursor=UINT32_MAX; }
        if(!find_arguments(arg,&q)) goto syntax;
    } else if(!strncmp(line,"READGLOBAL ",11)) {
        q.action=5; if(!number_arg(line+11,&q.number,&tail) || *tail) goto syntax;
    } else if(!strcmp(line,"INFO") || !strncmp(line,"INFO ",5)) {
        q.action=6; if(line[4] && (!number_arg(line+5,&q.number,&tail) || *tail)) goto syntax;
    } else if(!strcmp(line,"PROFILE") || !strncmp(line,"PROFILE ",8)) {
        q.action=7;
        if(line[7]) { if(!line[8] || strlen(line+8)>32 || strchr(line+8,' ')) goto syntax; strcpy(q.username,line+8); }
    } else if(!strncmp(line,"PROFILEID ",10)) {
        q.action=7; if(!number_arg(line+10,&q.number,&tail) || *tail || q.number>MEMBER_MAX) goto syntax;
    } else if(s->user && (!strcmp(line,"HANDLE") || !strncmp(line,"HANDLE ",7))) {
        q.action=8; if(line[6]) { if(strlen(line+7)>80 || !safe_text(line+7)) goto syntax; strcpy(q.title,line+7); }
    } else if(!strcmp(line,"BIO") || !strncmp(line,"BIO ",4)) {
        q.action=9; if(line[3]) { if(strlen(line+4)>320 || !safe_text(line+4)) goto syntax; strcpy(q.body,line+4); }
    } else if(!strcmp(line,"PROFILES") || !strncmp(line,"PROFILES ",9) || !strncmp(line,"PROFILESFROM ",13)) {
        q.action=10;q.scope=s->profile_filter;q.number=s->profile_group;if(strncmp(line,"PROFILESFROM ",13)) { q.scope=0;q.number=0;s->profile_filter=0;s->profile_group=0; } q.cursor=1; arg="*";
        if(!strncmp(line,"PROFILESFROM ",13)) { if(!number_arg(line+13,&q.cursor,&tail) || *tail!=' ' || q.cursor>MEMBER_MAX) goto syntax; arg=tail+1; }
        else if(line[8]) arg=line+9;
        if(!*arg || strlen(arg)>80 || !safe_text(arg)) goto syntax;
        strcpy(q.title,arg);
    } else return 0;
    if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return 1; }
    submit(s,&q);
    if(q.action==4 && s->pending) { s->scan=q; s->scanning=1; }
    return 1;
syntax:
    emit(s,"ERR extended syntax\r\n"); return 1;
}
static void undo_clear(Session *s) {
    s->undo_available=0; OPENSSL_cleanse(s->undo_title,sizeof(s->undo_title)); OPENSSL_cleanse(s->undo_body,sizeof(s->undo_body));
}
static void undo_save(Session *s) {
    memcpy(s->undo_title,s->draft.title,sizeof(s->undo_title)); memcpy(s->undo_body,s->draft.body,sizeof(s->undo_body)); s->undo_available=1;
}
static int draft_word(const char *line,const char *word) {
    size_t n=strlen(word); return !strncmp(line,word,n) && (!line[n] || line[n]==' ');
}
static int edit_draft(Session *s,const char *line) {
    char *body=s->draft.body;
    if(!strcmp(line,"/UNDO")) {
        if(!s->undo_available) emit(s,"ERR no undo\r\n");
        else {
            memcpy(s->draft.title,s->undo_title,sizeof(s->undo_title)); memcpy(body,s->undo_body,sizeof(s->undo_body));
            undo_clear(s); emit(s,"DRAFT UNDONE\r\n");
        }
        return 1;
    }
    if(!strcmp(line,"/LINES")) {
        char out[CONTENT_DISPLAY]; unsigned count=0;
        for(const char *p=body;*p;p=strstr(p,"\r\n")+2) count++;
        size_t used=(size_t)snprintf(out,sizeof(out),"DRAFT LINES %u\r\nTITLE %s\r\n",count,s->draft.title);
        unsigned number=1;
        for(const char *p=body;*p;number++) {
            const char *end=strstr(p,"\r\n");
            used+=(size_t)snprintf(out+used,sizeof(out)-used,"LINE %u %.*s\r\n",number,(int)(end-p),p); p=end+2;
        }
        snprintf(out+used,sizeof(out)-used,"END DRAFT\r\n"); emit(s,out); return 1;
    }
    if(draft_word(line,"/TITLE")) {
        const char *title=line[6]?line+7:"";
        if(!*title || strlen(title)>BOARD_TITLE || !safe_text(title)) emit(s,"ERR title 1..80 bytes, no controls\r\n");
        else { if(strcmp(s->draft.title,title)) undo_save(s); strcpy(s->draft.title,title); emit(s,"DRAFT TITLE CHANGED\r\n"); }
        return 1;
    }
    int replace=draft_word(line,"/REPLACE"),remove=draft_word(line,"/DELETE"),insert=draft_word(line,"/INSERT");
    if(!replace && !remove && !insert) return 0;
    size_t prefix=replace?8:7; uint32_t number; const char *tail;
    if(!line[prefix] || !number_arg(line+prefix+1,&number,&tail) || (remove && *tail) || (!remove && *tail && *tail!=' ')) {
        emit(s,"ERR draft line\r\n"); return 1;
    }
    char *start=body;
    uint32_t at=1;
    for(;at<number && *start;at++) start=strstr(start,"\r\n")+2;
    if(at!=number || (!insert && !*start)) { emit(s,"ERR draft line\r\n"); return 1; }
    char *end=insert?start:strstr(start,"\r\n")+2;
    const char *text=!remove && *tail?tail+1:"";
    if(!safe_text(text)) { emit(s,"ERR control character\r\n"); return 1; }
    size_t n=strlen(text),old=(size_t)(end-start),length=strlen(body),add=remove?0:n+2;
    if(length-old+add>BOARD_BODY) { emit(s,"ERR body limit 262144 bytes; cancel or finish\r\n"); return 1; }
    if(replace && old==add && !memcmp(start,text,n)) { emit(s,"DRAFT LINE REPLACED\r\n"); return 1; }
    undo_save(s);
    memmove(start+add,end,strlen(end)+1);
    if(!remove) { memcpy(start,text,n); memcpy(start+n,"\r\n",2); }
    memset(body+length-old+add+1,0,BOARD_BODY-(length-old+add));
    emit(s,insert?"DRAFT LINE INSERTED\r\n":replace?"DRAFT LINE REPLACED\r\n":"DRAFT LINE DELETED\r\n"); return 1;
}

static unsigned editor_count(const Session *s) {
    unsigned n=0;for(const char *p=s->draft.body;*p;p=strstr(p,"\r\n")+2)n++;return n;
}
/* RTBBS line commands share the existing draft and undo snapshot. */
static int classic_editor(Session *s,const char *line,char *translated,size_t size) {
    if(s->editor_upload_question) {
        if(!*line || !strcmp(line,"Y") || !strcmp(line,"y")) { undo_save(s);s->draft.body[0]=0; }
        else if(strcmp(line,"N") && strcmp(line,"n")) { queue_text(s,"Y / N > ");return 1; }
        s->editor_upload_question=0;s->editor_at=editor_count(s)+1;s->editor_input=1;s->editor_upload=1;s->editor_deadline=s->now+3000;
        queue_text(s,s->ui.language==LANGUAGE_JA?"テキスト受信中。3秒間の無入力で終了します。\r\n":"Receiving text; 3 seconds idle ends input.\r\n");return 1;
    }
    if(s->editor_confirm) { char c=*line;if(c>='a' && c<='z')c-=32;if(c && !line[1] && (c=='W' || c=='E' || c=='Q' || c=='0')) { s->editor_confirm=0;if(c=='E')return 1;snprintf(translated,size,"%s",c=='W'?".":"/CANCEL");return 0; }queue_text(s,"W / E / 0 > ");return 1; }
    if(!strcmp(line,"/E") || !strcmp(line,"/EDITOR LINE")) {
        s->editor_line=1;s->editor_input=0;s->editor_at=1;queue_text(s,ui_text(&s->ui,MSG_EDITOR_LINE));return 1;
    }
    if(!strcmp(line,"/EDITOR BETA")) { s->editor_line=0;s->editor_input=0;queue_text(s,ui_text(&s->ui,MSG_EDITOR_BETA));return 1; }
    if(!strcmp(line,"/?") || (s->editor_line && !s->editor_input && !strcmp(line,"?"))) { queue_text(s,help_text(s->ui.language,"EDITOR")?help_text(s->ui.language,"EDITOR"):ui_text(&s->ui,MSG_EDITOR_HELP));return 1; }
    if(!strcmp(line,"/M") || (s->editor_line && !s->editor_input && !strcmp(line,"M"))) { s->editor_numbers=!s->editor_numbers;queue_text(s,ui_text(&s->ui,MSG_EDITOR_NUMBERS));queue_text(s,s->editor_numbers?"ON\r\n":"OFF\r\n");return 1; }
    if(!strcmp(line,"/N")) { undo_save(s);memset(s->draft.body,0,sizeof(s->draft.body));s->editor_at=1;return 1; }
    if(!s->editor_line) { if(!strcmp(line,"//")) {
        if(s->ui.enabled) { s->editor_confirm=1;queue_text(s,ui_text(&s->ui,MSG_CLASSIC_WRITE));return 1; }
        snprintf(translated,size,".");
    }return 0; }
    if(s->editor_input==4) { s->editor_input=0;if(!*line)return 1;char search[INPUT_LINE_MAX+128];snprintf(search,sizeof(search),"S%u-%u %s",s->search_first,s->search_last,line);return classic_editor(s,search,translated,size); }
    if(s->editor_input) {
        if(s->editor_upload)s->editor_deadline=s->now+3000;
        if(!strcmp(line,"//")) { s->editor_input=0;s->editor_upload=0;return 1; }
        if(*line=='/' && (draft_word(line,"/SAVE") || draft_word(line,"/CANCEL") || draft_word(line,"/UNDO") || draft_word(line,"/PREVIEW") || draft_word(line,"/ATTACH") || draft_word(line,"/DETACH") || draft_word(line,"/TITLE") || draft_word(line,"/LINES") || draft_word(line,"/REPLACE") || draft_word(line,"/INSERT") || draft_word(line,"/DELETE") || draft_word(line,"/HELP")))return 0;
        unsigned count=editor_count(s);if(s->editor_at>count+1)s->editor_at=count+1;const char *op=s->editor_input==2 && s->editor_at<=count?"/REPLACE":"/INSERT";
        char command[INPUT_LINE_MAX+128];snprintf(command,sizeof(command),"%s %u %s",op,s->editor_at,line);
        /* Check capacity before advancing the cursor. */
        const char *at=s->draft.body;for(unsigned n=1;n<s->editor_at && *at;n++)at=strstr(at,"\r\n")+2;
        size_t old=!strcmp(op,"/REPLACE")?(size_t)(strstr(at,"\r\n")+2-at):0;
        if(safe_text(line) && strlen(s->draft.body)-old+strlen(line)+2<=BOARD_BODY) {
            edit_draft(s,command);s->editor_at++;if(s->editor_input==2)s->editor_input=0;
        } else emit(s,"ERR body limit 262144 bytes; cancel or finish\r\n");
        return 1;
    }
    if(*line=='/' || !strcmp(line,"."))return 0;
    if(!strcmp(line,"0")) { s->editor_confirm=1;queue_text(s,s->ui.language==LANGUAGE_JA?"W: 書き込み / E: 編集へ戻る / 0: 取り消し > ":"W: write / E: edit / 0: cancel > ");return 1; }
    unsigned count=editor_count(s);if(!s->editor_at || s->editor_at>count+1)s->editor_at=1;
    if(*line>='0' && *line<='9') {
        const char *end;uint32_t at;if(number_arg(line,&at,&end) && !*end && at<=count)s->editor_at=at;else emit(s,"ERR draft line\r\n");return 1;
    }
    char key=*line;if(key>='a' && key<='z')key-=32;
    if(key=='N' && !line[1]) { undo_save(s);memset(s->draft.body,0,sizeof(s->draft.body));s->editor_at=1;s->editor_input=1;return 1; }
    if(key=='U' && !line[1] && count) { s->editor_upload_question=1;queue_text(s,s->ui.language==LANGUAGE_JA?"バッファをクリアしますか？ [Y]/N（Nは追記） > ":"Clear buffer? [Y]/N (N appends) > ");return 1; }
    if((key=='A' || key=='U') && !line[1]) { s->editor_at=count+1;s->editor_input=1;if(key=='U') { s->editor_upload=1;s->editor_deadline=s->now+3000;queue_text(s,s->ui.language==LANGUAGE_JA?"テキスト受信中。3秒間の無入力で編集へ戻ります。\r\n":"Receiving text; 3 seconds idle returns to editor.\r\n"); }return 1; }
    if(key=='S') {
        uint32_t first=1,last=count;const char *query=line+1;
        if(*query && *query!=' ') {
            if(!number_arg(query,&first,&query))goto invalid;last=first;
            if(*query=='-') { query++;last=count;if(*query && *query!=' ' && !number_arg(query,&last,&query))goto invalid; }
            if(*query && *query!=' ')goto invalid;
        }
        if(!first || first>last || last>count)goto invalid;
        while(*query==' ')query++;
        if(!*query) { s->search_first=first;s->search_last=last;s->editor_input=4;queue_text(s,ui_text(&s->ui,MSG_EDITOR_SEARCH));return 1; }
        unsigned n=1,matches=0;size_t length=strlen(query);
        for(const char *p=s->draft.body;*p;n++) {
            const char *end=strstr(p,"\r\n");if(!end)break;
            const char *hit=strstr(p,query);
            if(n>=first && n<=last && hit && hit+length<=end) {
                size_t capacity=(size_t)(end-p)*12+128;char *out=malloc(capacity);if(!out) { emit(s,"ERR board busy\r\n");return 1; }size_t used=0;const char *at=p;
                if(s->editor_numbers)used+=(size_t)snprintf(out+used,capacity-used,"%u ",n);
                while(hit && hit+length<=end) {
                    used+=(size_t)snprintf(out+used,capacity-used,"%.*s%s%.*s%s",(int)(hit-at),at,s->ui.enabled?"\033[33m":"",(int)length,hit,s->ui.enabled?"\033[0m":"");
                    matches++;at=hit+length;hit=strstr(at,query);
                }
                snprintf(out+used,capacity-used,"%.*s\r\n",(int)(end-at),at);queue_text(s,out);free(out);
            }
            p=end+2;
        }
        char total[80];snprintf(total,sizeof(total),s->ui.language==LANGUAGE_JA?"一致: %u 件\r\n":"Matches: %u\r\n",matches);queue_text(s,total);return 1;
    }
    if(!strchr("LEIDQ",key) || !key) { queue_text(s,help_text(s->ui.language,"EDITOR")?help_text(s->ui.language,"EDITOR"):ui_text(&s->ui,MSG_EDITOR_HELP));return 1; }
    uint32_t first=(key=='L'?1:s->editor_at),last=(key=='L'?count:first);const char *tail=line+1;
    if(*tail) {
        if(*tail!='-') { if(!number_arg(tail,&first,&tail))goto invalid;last=first; }
        if(*tail=='-') { tail++;last=count;if(*tail && !number_arg(tail,&last,&tail))goto invalid; }
        if(*tail)goto invalid;
    }
    if(key=='L' && !count) { edit_draft(s,"/LINES");return 1; }
    if(!first || first>last || last>count+(key=='I'?1u:0u))goto invalid;
    if(key=='E' || key=='I') {
        s->editor_at=first;s->editor_input=key=='E'?2:3;
        if(key=='E' && s->ui.enabled) {
            const char *p=s->draft.body;for(uint32_t i=1;i<first;i++)p=strstr(p,"\r\n")+2;
            const char *end=strstr(p,"\r\n");char old[INPUT_LINE_MAX+80];
            snprintf(old,sizeof(old),"%u: %.*s\r\n",first,(int)(end-p),p);queue_text(s,old);
        }
        return 1;
    }
    char *begin=s->draft.body;for(uint32_t i=1;i<first;i++)begin=strstr(begin,"\r\n")+2;
    char *end=begin;for(uint32_t i=first;i<=last;i++)end=strstr(end,"\r\n")+2;
    if(key=='L') { char out[CONTENT_DISPLAY];size_t used=0;uint32_t n=first;
        for(char *p=begin;p<end;n++) { char *next=strstr(p,"\r\n");if(s->editor_numbers)used+=(size_t)snprintf(out+used,sizeof(out)-used,"%u ",n);used+=(size_t)snprintf(out+used,sizeof(out)-used,"%.*s\r\n",(int)(next-p),p);p=next+2; }queue_text(s,out);return 1; }
    if(key=='D') { undo_save(s);memmove(begin,end,strlen(end)+1);s->editor_at=first;return 1; }
    if(key=='Q') {
        if(strlen(s->draft.body)+2*(last-first+1)>BOARD_BODY)goto invalid;
        char copy[BOARD_BODY+1];size_t used=(size_t)(begin-s->draft.body);memcpy(copy,s->draft.body,used);
        for(char *p=begin;p<end;) { char *next=strstr(p,"\r\n")+2;copy[used++]='>';copy[used++]=' ';memcpy(copy+used,p,(size_t)(next-p));used+=(size_t)(next-p);p=next; }
        strcpy(copy+used,end);undo_save(s);strcpy(s->draft.body,copy);return 1;
    }
invalid:emit(s,"ERR draft line\r\n");return 1;
}
static void chat_prompt(Session *s) {
    if(!s->ui.enabled || !s->active)return;
    char text[128];
    if(s->talk_select)snprintf(text,sizeof(text),"%s",ui_text(&s->ui,MSG_TALK_SELECT));
    else if(s->talk_lost)snprintf(text,sizeof(text),"%s",ui_text(&s->ui,MSG_TALK_BLOCKED));
    else if(s->talk_target)snprintf(text,sizeof(text),ui_text(&s->ui,MSG_TALK_PROMPT),s->talk_target);
    else snprintf(text,sizeof(text),"%s",ui_text(&s->ui,MSG_CHAT_PROMPT));
    s->chat_prompting=1;queue_text(s,text);s->chat_prompting=0;
}
static int chat_event(void *context,const ChatEvent *e) {
    Session *s=context;if(!s->active || s->closing || !s->user || s->zmodem || s->door)return 0;
    if(e->kind=='X' && e->connection!=s->talk_target)return 1;
    if((e->kind=='I' || e->kind=='C') && !s->chatting && (s->password_mode || s->editing || s->binary || s->zmodem || s->pending || s->page[0] || s->ui.input || s->classic_input || s->mail_input || s->mail_delete || s->wake))return 0;
    if((e->kind=='J' || e->kind=='S') && (!s->chatting || s->outside_talk))return 1;
    if((e->kind=='L' || e->kind=='X') && (!s->chatting || s->outside_talk) && e->connection!=s->talk_target)return 1;
    char text[1400];
    if(e->kind=='I')s->reply_target=e->connection;
    if((e->kind=='L' || e->kind=='X') && e->connection==s->talk_target)s->talk_lost=1;
    if(s->ui.enabled) {
        MessageId id=e->kind=='J'?MSG_CHAT_JOIN:(e->kind=='L' || e->kind=='X')?MSG_CHAT_LEAVE:e->kind=='W'?MSG_CHAT_WHO:MSG_CHAT_SAY;
        if(e->kind=='A')snprintf(text,sizeof(text),ui_text(&s->ui,MSG_TALK_RECEIVE),e->text);
        else if(e->kind=='M')snprintf(text,sizeof(text),ui_text(&s->ui,MSG_TALK_MUTED),e->login);
        else if(e->kind=='E')snprintf(text,sizeof(text),"\r\n%s",ui_text(&s->ui,MSG_CHAT_END));
        else if(e->kind=='I' || e->kind=='O')snprintf(text,sizeof(text),ui_text(&s->ui,e->kind=='I'?MSG_TALK_IN:MSG_TALK_OUT),e->connection,e->name,e->login,e->text);
        else if(e->kind=='C')snprintf(text,sizeof(text),ui_text(&s->ui,MSG_SYSOP_CALL),e->connection,e->name,e->login,e->text);
        else if(e->kind=='W')snprintf(text,sizeof(text),ui_text(&s->ui,MSG_ONLINE_MEMO),e->connection,e->name,e->login,e->text);
        else if(e->kind=='S')snprintf(text,sizeof(text),ui_text(&s->ui,id),e->name,e->login,e->text);
        else snprintf(text,sizeof(text),ui_text(&s->ui,id),e->name,e->login);
    } else {
        if(e->kind=='A')snprintf(text,sizeof(text),"CHAT RECEIVE %s\r\n",e->text);
        else if(e->kind=='M')snprintf(text,sizeof(text),"CHAT MUTED %s\r\n",e->login);
        else if(e->kind=='E')strcpy(text,"CHAT END\r\n");
        else snprintf(text,sizeof(text),"CHAT %s %lu %u %s\t%s\t%s\r\n",e->kind=='C'?"SYSOP CALL":e->kind=='I'?"PRIVATE FROM":e->kind=='O'?"PRIVATE TO":e->kind=='J'?"JOIN":(e->kind=='L' || e->kind=='X')?"LEAVE":e->kind=='W'?"WHO":"SAY",e->connection,e->user,e->login,e->name,e->text);
    }
    s->chat_notice=1;queue_text(s,text);
    if((e->kind=='L' || e->kind=='X') && e->connection==s->talk_target)queue_text(s,s->ui.enabled?ui_text(&s->ui,MSG_TALK_GONE):"CHAT TALK GONE\r\n");
    if(!e->replay && !(e->kind=='O' && s->talk_once)) { if(s->chatting)chat_prompt(s);else { s->chat_prompting=1;prompt(s);s->chat_prompting=0; } }
    s->chat_notice=0;return s->active;
}
static void chat_error(Session *s,MessageId id,const char *raw) {
    queue_text(s,s->ui.enabled?ui_text(&s->ui,id):raw);chat_prompt(s);
}
static int talk_number(const char *text,unsigned long *id) {
    unsigned long value=0;if(!*text)return 0;
    for(;*text;text++) { if(*text<'0'||*text>'9')return 0;unsigned digit=(unsigned)(*text-'0');if(value>(ULONG_MAX-digit)/10)return 0;value=value*10+digit; }
    if(!value)return 0;
    *id=value;return 1;
}
static void talk_start(Session *s,unsigned long target) {
    BoardRequest q={0};q.op='&';q.action='T';q.chat_target=target;
    /* A failed selection must never turn subsequent private text into public text. */
    s->talk_select=0;s->talk_lost=1;submit(s,&q);s->chat_pending=s->pending;
    if(!s->pending)chat_prompt(s);
}
static void session_status(Session *s) {
    char out[320],stamp[80];time_t now=time(NULL);struct tm *local=localtime(&now);if(local)strftime(stamp,sizeof(stamp),date_pattern(),local);else strcpy(stamp,"unknown");
    snprintf(out,sizeof(out),"STATUS\r\nTIME %s\r\nCONNECTION %lu / SECONDS %llu / ONLINE %u\r\nMEMBER %u / LEVEL %u / BOARD %u\r\n",stamp,s->id,(unsigned long long)(s->now>=s->connected_at?(s->now-s->connected_at)/1000:0),s->io.online?s->io.online(s->io.context):0,s->user,s->member_level,s->board);queue_text(s,out);
    if(!s->user) { if(s->chatting)chat_prompt(s);else prompt(s);return; }
    BoardRequest q={0};q.op='!';q.action='V';submit(s,&q);s->status_pending=s->pending;
}
static void chat_input(Session *s,char *line) {
    if(s->memo_input) { s->memo_input=0;if(!strcmp(line,"/CANCEL")) { emit(s,"CANCELLED\r\n");chat_prompt(s);return; }BoardRequest q={0};q.op='&';q.action='M';if(strlen(line)>80 || (*line && !safe_text(line))) { chat_error(s,MSG_CHAT_INVALID,"CHAT ERROR text\r\n");return; }strcpy(q.body,line);submit(s,&q);s->chat_pending=s->pending;return; }
    if(!strcmp(line,"\\") || !strcmp(line,"``")) { s->memo_input=1;queue_text(s,s->ui.language==LANGUAGE_JA?"接続者メモ (80バイト以内): ":"Connection memo (up to 80 bytes): ");return; }
    if(!strcmp(line,"#")) { session_status(s);return; }
    if(!strcmp(line,"D") || !strcmp(line,"d") || !strcmp(line,"#")) { BoardRequest q={0};q.op='&';q.action=*line=='#'?'O':'C';if(q.action=='C')strcpy(q.body,"SYSOP");
        submit(s,&q);s->chat_pending=s->pending;return; }

    if(!strncmp(line,"/KICK ",6)) {
        BoardRequest q={0};q.op='&';q.action='K';snprintf(q.body,sizeof(q.body),"%s",line+6);
        submit(s,&q);s->chat_pending=s->pending;if(!s->pending)chat_prompt(s);return;
    }
    if(!strcmp(line,"/TALK ON") || !strcmp(line,"/TALK OFF") || !strcmp(line,"/MUTES") ||
       !strncmp(line,"/MUTE ",6) || !strncmp(line,"/UNMUTE ",8)) {
        BoardRequest q={0};q.op='&';q.action='P';snprintf(q.body,sizeof(q.body),"%s",line);
        submit(s,&q);s->chat_pending=s->pending;if(!s->pending)chat_prompt(s);return;
    }
    if(s->outside_talk && ((!*line && s->talk_select) || !strcmp(line,"//") || !strcmp(line,"/END") || !strcmp(line,"/LEAVE") || !strcmp(line,"/QUIT"))) {
        s->outside_talk=0;s->chatting=0;s->talk_target=0;s->talk_select=s->talk_lost=0;s->talk_once=s->talk_return_editor=0;
        queue_text(s,s->ui.enabled?ui_text(&s->ui,MSG_TALK_OUTSIDE_END):"TALK CLOSED\r\n");
        if(!strcmp(line,"/QUIT"))command_core(s,"QUIT");else prompt(s);return;
    }
    if((!strcmp(line,"//") || !strcmp(line,"/END")) && (s->talk_target || s->talk_select || s->talk_lost)) {
        s->talk_target=0;s->talk_select=0;s->talk_lost=0;
        queue_text(s,s->ui.enabled?ui_text(&s->ui,MSG_TALK_END):"CHAT TALK CLOSED\r\n");chat_prompt(s);return;
    }
    if(!strcmp(line,"T") || !strcmp(line,"t") || !strcmp(line,"/TALK")) {
        s->talk_once=!s->outside_talk && *line!='/';s->talk_select=1;chat_prompt(s);return;
    }
    if(!strcmp(line,"/REPLY")) { talk_start(s,s->reply_target);return; }
    if(!strncmp(line,"T ",2) || !strncmp(line,"t ",2) || !strncmp(line,"/TALK ",6)) {
        unsigned long target;
        if(!talk_number(line+(*line=='/'?6:2),&target)) { s->talk_lost=1;chat_error(s,MSG_TALK_INVALID,"CHAT ERROR target\r\n");return; }
        s->talk_once=!s->outside_talk && *line!='/';talk_start(s,target);return;
    }
    if(!strcmp(line,"//") || !strcmp(line,"/LEAVE") || !strcmp(line,"/QUIT")) {
        int quit=!strcmp(line,"/QUIT");s->talk_target=s->reply_target=0;s->talk_select=s->talk_lost=0;s->chatting=0;chat_leave(s->id);
        queue_text(s,s->ui.enabled?ui_text(&s->ui,MSG_CHAT_EXIT):"CHAT CLOSED\r\n");
        if(quit) { command_core(s,"QUIT");return; }
        if(s->ui.enabled)show_menu(s,1);
        prompt(s);return;
    }
    if(!strcmp(line,"?") || !strcmp(line,"/HELP")) { queue_text(s,help_text(s->ui.language,"CHAT")?help_text(s->ui.language,"CHAT"):ui_text(&s->ui,MSG_HELP_CHAT));if(!s->ui.enabled)queue_text(s,"END HELP\r\n");chat_prompt(s);return; }
    if(!strncmp(line,"/LANG ",6)) {
        Language language;if(language_parse(line+6,&language))s->ui.language=language;
        else { chat_error(s,MSG_CHAT_COMMAND,"CHAT ERROR command\r\n");return; }
        chat_prompt(s);return;
    }
    if(!*line) { chat_prompt(s);return; }
    BoardRequest q={0};q.op='&';q.action='S';
    if(!strcmp(line,"W") || !strcmp(line,"w") || !strcmp(line,"/WHO"))q.action='O';
    else {
        if(s->talk_select) {
            unsigned long target;if(!talk_number(line,&target)) { chat_error(s,MSG_TALK_INVALID,"CHAT ERROR target\r\n");return; }
            talk_start(s,target);return;
        }
        if(s->talk_lost || (s->outside_talk && !s->talk_target)) { chat_error(s,MSG_TALK_GONE,"CHAT ERROR target\r\n");return; }
        const char *text=!strncmp(line,"/SAY ",5)?line+5:line;
        if(text==line && *line=='/') { chat_error(s,MSG_CHAT_COMMAND,"CHAT ERROR command\r\n");return; }
        if(!chat_text_valid(text)) { chat_error(s,MSG_CHAT_INVALID,"CHAT ERROR text\r\n");return; }
        strcpy(q.body,text);
        if(s->talk_target) { q.action='D';q.chat_target=s->talk_target; }
    }
    submit(s,&q);s->chat_pending=s->pending;if(!s->pending)chat_prompt(s);
}
static void mail_submit(Session *s,BoardRequest *q) {
    q->op='m';q->scope=s->ui.enabled?(int)s->ui.language+1:0;submit(s,q);s->mail_pending=s->pending;
}
static int mail_command(Session *s,const char *line) {
    int explicit_mail=!strcmp(line,"MAIL") || !strncmp(line,"MAIL ",5);
    if(!s->mail_mode && !explicit_mail)return 0;
    if(!s->user) { emit(s,"ERR login required\r\n");return 1; }
    if(explicit_mail) { s->mail_mode=1;line=line[4]?line+5:"L"; }
    if(!s->mail_input && (!strcmp(line,"G") || !strcmp(line,"GET") || !strncmp(line,"GET ",4))) { BoardRequest q={0};q.op='}';q.action='M';q.number=s->mail_current;const char *tail;if(!strncmp(line,"GET ",4) && (!number_arg(line+4,&q.number,&tail) || *tail)) { emit(s,"ERR command\r\n");return 1; }submit(s,&q);return 1; }
    if(!s->mail_input && (!strcmp(line,"F") || !strcmp(line,"SCAN") || !strcmp(line,"SCAN ASC") || !strcmp(line,"SCAN DESC"))) { BoardRequest q={0};q.op='m';q.action='f';q.descending=!strcmp(line,"SCAN DESC");q.cursor=q.descending?UINT32_MAX:1;q.scope=s->ui.enabled?(int)s->ui.language+1:0;submit(s,&q);if(s->pending) { s->scan=q;s->scanning=1; }return 1; }
    if(s->mail_delete) {
        uint32_t id=s->mail_delete;s->mail_delete=0;
        if(!strcmp(line,"Y") || !strcmp(line,"y")) { BoardRequest q={0};q.action='D';q.number=id;mail_submit(s,&q); }
        else emit(s,"CANCELLED\r\n");
        return 1;
    }
    if(s->mail_input) {
        if(!strcmp(line,"/CANCEL") || !strcmp(line,"0")) { s->mail_input=0;memset(&s->draft,0,sizeof(s->draft));emit(s,"CANCELLED\r\n");return 1; }
        if(s->mail_input==1) {
            if(!*line || strlen(line)>32 || !safe_text(line)) { emit(s,"ERR command\r\n");return 1; }
            snprintf(s->draft.username,sizeof(s->draft.username),"%s",line);s->mail_input=2;return 1;
        }
        if(!*line || strlen(line)>80 || !safe_text(line)) { emit(s,"ERR title 1..80 bytes, no controls\r\n");return 1; }
        snprintf(s->draft.title,sizeof(s->draft.title),"%s",line);s->mail_input=0;s->draft.action='B';mail_submit(s,&s->draft);return 1;
    }
    if(!strcmp(line,"0") || !strcmp(line,"EXIT") || !strcmp(line,"/LEAVE")) { s->mail_mode=0;s->ui.view=UI_HOME;queue_text(s,s->ui.enabled?ui_text(&s->ui,MSG_MAIL_CLOSED):"MAIL CLOSED\r\n");return 1; }
    if(!strcmp(line,"H") || !strcmp(line,"?") || !strcmp(line,"HELP")) { show_help(s,"MAIL");return 1; }
    BoardRequest q={0};const char *tail=NULL;uint32_t id=0;
    if(!board_feature_enabled("mail")) { s->mail_mode=0;emit(s,"ERR feature disabled\r\n");return 1; }
    if(!strcmp(line,"NOTIFY") || !strcmp(line,"NOTIFY ON") || !strcmp(line,"NOTIFY OFF")) { q.action='U';q.number=!strcmp(line,"NOTIFY ON")?1:!strcmp(line,"NOTIFY OFF")?2:0; }
    else if(!strncmp(line,"NEWS ",5) || !strncmp(line,"NEWSFROM ",9)) {
        const char *date=line+5;uint64_t stamp;
        if(!strncmp(line,"NEWSFROM ",9)) { if(!number_arg(line+9,&q.cursor,&tail) || *tail!=' ' || q.cursor>CONTENT_ID_MAX)goto invalid;date=tail+1; }
        if(!date_filter(date,&stamp))goto invalid;
        strcpy(s->mail_query,date);s->mail_filter=5;s->mail_cursor=q.cursor;q.action='L';q.number=5;strcpy(q.title,date);
    }
    else if(!strncmp(line,"SEARCH ",7) || !strncmp(line,"SEARCHFROM ",11)) {
        const char *query=line+7;
        if(!strncmp(line,"SEARCHFROM ",11)) { if(!number_arg(line+11,&q.cursor,&tail) || *tail!=' ' || q.cursor>CONTENT_ID_MAX)goto invalid;query=tail+1; }
        if(!*query || strlen(query)>80 || !safe_text(query))goto invalid;
        strcpy(s->mail_query,query);s->mail_filter=4;s->mail_cursor=q.cursor;
        q.action='L';q.number=4;strcpy(q.title,query);
    }
    else if(!strcmp(line,"FILES") || !strncmp(line,"FILES ",6)) { q.action='I';if(line[5] && (!number_arg(line+6,&q.cursor,&tail) || *tail))goto invalid; }
    else if(!strncmp(line,"DROP ",5)) { q.action='X';if(!number_arg(line+5,&q.number,&tail) || strcmp(tail," CONFIRM"))goto invalid; }
    else if(!strcmp(line,"DRAFT"))q.action='F';
    else if(!strcmp(line,"RESUME"))q.action='O';
    else if(!strncmp(line,"DRAFT DELETE ",13)) { q.action='C';if(!audit_number(line+13,&q.mail_revision))goto invalid; }
    else if(!strcmp(line,"L") || !strcmp(line,"LIST") || !strcmp(line,"SENT") || !strcmp(line,"INBOX") || !strcmp(line,"UNREAD") || !strcmp(line,"+")) {
        q.action='L';if(strcmp(line,"+")) { s->mail_cursor=0;s->mail_filter=!strcmp(line,"SENT")?1:!strcmp(line,"UNREAD")?2:!strcmp(line,"INBOX")?3:0; }
        q.cursor=s->mail_cursor;q.number=(uint32_t)s->mail_filter;if(s->mail_filter==4 || s->mail_filter==5)strcpy(q.title,s->mail_query);
    } else if(!strcmp(line,"N") || !strcmp(line,"NEXT"))q.action='N';
    else if(!strcmp(line,"T") || !strcmp(line,"LATEST"))q.action='T';
    else if(!strcmp(line,"E") || !strcmp(line,"SEND")) { memset(&s->draft,0,sizeof(s->draft));s->mail_input=1;return 1; }
    else if(!strncmp(line,"SEND ",5) || !strncmp(line,"E ",2)) {
        const char *arg=line+(*line=='E'?2:5),*space=strchr(arg,' ');size_t n=space?(size_t)(space-arg):strlen(arg);
        if(!n || n>32)goto invalid;
        memcpy(q.username,arg,n);q.username[n]=0;q.action='B';
        if(!space || !space[1]) { s->draft=q;s->mail_input=2;return 1; }
        if(strlen(space+1)>80 || !safe_text(space+1))goto invalid;
        strcpy(q.title,space+1);
    } else {
        const char *arg=line;
        if(!strncmp(line,"READ ",5)) { q.action='R';arg=line+5; }
        else if(!strncmp(line,"REPLY ",6)) { q.action='Q';arg=line+6; }
        else if(!strncmp(line,"R ",2)) { q.action='Q';arg=line+2; }
        else if(!strncmp(line,"DELETE ",7)) { q.action='D';arg=line+7; }
        else if(!strncmp(line,"K ",2)) { q.action='D';arg=line+2; }
        else if(!strcmp(line,"R") || !strcmp(line,"K")) { q.action=*line=='R'?'Q':'D';id=s->mail_current;tail=""; }
        else if(*line>='0' && *line<='9')q.action='R';
        else return 0;
        if(!id && !number_arg(arg,&id,&tail))goto invalid;
        if(!id)goto invalid;
        q.number=id;
        if(q.action=='Q' && *tail==' ' && strlen(tail+1)<=80 && safe_text(tail+1))strcpy(q.title,tail+1);
        else if(*tail)goto invalid;
        if(q.action=='D') { s->mail_delete=id;if(!s->ui.enabled)queue_text(s,"MAIL DELETE CONFIRM Y/N\r\n");return 1; }
    }
    mail_submit(s,&q);return 1;
invalid:emit(s,"ERR command\r\n");return 1;
}

static int service_command(Session *s,const char *line) {
    BoardRequest q={0};q.op='!';q.scope=s->ui.enabled?(int)s->ui.language+1:0;
    if(!strcmp(line,"GATHER") || !strcmp(line,"GATHER READ")) {
        q.action='G';submit(s,&q);s->gather_pending=s->pending?(!strcmp(line,"GATHER READ")?2:1):0;return 1;
    }
    if(!strncmp(line,"SYSOPMAIL ",10)) {
        const char *title=line+10;
        if(s->now<s->sysop_mail_after) { queue_text(s,ui_text(&s->ui,MSG_SYSOP_MAIL_RATE));return 1; }
        if(!board_feature_enabled("mail")) { emit(s,"ERR feature disabled\r\n");return 1; }
        if(!*title || strlen(title)>80 || !safe_text(title)) { emit(s,"ERR title 1..80 bytes, no controls\r\n");return 1; }
        memset(&s->draft,0,sizeof(s->draft));s->draft.op='?';s->draft.action='M';strcpy(s->draft.title,title);s->editing=1;undo_clear(s);queue_text(s,ui_text(&s->ui,MSG_SYSOP_MAIL_EDIT));return 1;
    }
    if(!strncmp(line,"FILEBOARDINFO ",14)) {
        q.op='Y';q.action=VAULT_CMD_BOARDINFO;if(strlen(line+14)>48 || !safe_text(line+14))goto invalid;strcpy(q.title,line+14);submit(s,&q);return 1;
    }
    if(!strncmp(line,"FILEBOARDSET ",13) || !strcmp(line,"FILESTATS") || !strncmp(line,"FILESTATS ",10) || !strcmp(line,"FILEHISTORY") || !strncmp(line,"FILEHISTORY ",12)) {
        q.op='Y';
        if(!strncmp(line,"FILEBOARDSET ",13)) { q.action=VAULT_CMD_BOARDSET;if(strlen(line+13)>=256 || !safe_text(line+13))goto invalid;strcpy(q.body,line+13); }
        else {
            int history=!strncmp(line,"FILEHISTORY",11);q.action=history?VAULT_CMD_HISTORY:VAULT_CMD_STATS;const char *arg=line+(history?11:9),*tail;
            if(*arg) { if(!strncmp(arg+1,"ALL",3) && (!arg[4] || arg[4]==' '))tail=arg+4;else if(!number_arg(arg+1,&q.number,&tail))goto invalid;if(*tail && (*tail!=' ' || !number_arg(tail+1,&q.cursor,&tail) || *tail))goto invalid; }
            snprintf(q.title,sizeof(q.title),"%u",q.cursor);
        }
        submit(s,&q);
        if(q.action!=VAULT_CMD_BOARDSET && s->pending) { snprintf(s->report,sizeof(s->report),q.number?"%s %u":"%s ALL",q.action==VAULT_CMD_HISTORY?"FILEHISTORY":"FILESTATS",q.number);s->report_pending=1; }
        return 1;
    }
    if(!strcmp(line,"SIGOPS"))q.action='v';
    else if(!strncmp(line,"SIGOP ",6)) {
        const char *tail;q.action='s';if(!number_arg(line+6,&q.number,&tail) || *tail!=' ')goto invalid;
        if(!strcmp(tail+1,"NONE"))q.cursor=0;
        else if(!number_arg(tail+1,&q.cursor,&tail) || *tail)goto invalid;
    }
    else if(!strncmp(line,"BOARDSET ",9)) {
        if(strlen(line+9)>=1024 || !safe_text(line+9))goto invalid;
        q.action='b';strcpy(q.body,line+9);
    } else if(!strncmp(line,"BOARDINFO ",10)) { const char *tail;q.action='i';if(!number_arg(line+10,&q.number,&tail) || *tail)goto invalid; }
    else if(!strcmp(line,"BOARDLOG") || !strncmp(line,"BOARDLOG ",9)) { const char *tail;q.action='j';if(line[8] && (!number_arg(line+9,&q.cursor,&tail) || *tail))goto invalid; }
    else if(!strcmp(line,"USERTABLE") || !strncmp(line,"USERTABLE ",10) || !strcmp(line,"USERLOG") || !strncmp(line,"USERLOG ",8) || !strcmp(line,"USERSTATS") || !strncmp(line,"USERSTATS ",10)) {
        int stats=!strncmp(line,"USERSTATS",9),table=!strncmp(line,"USERTABLE",9);q.action=stats?'V':table?'u':'U';const char *arg=line+(stats || table?9:7),*tail;
        if(*arg) {
            arg++;
            if(!strncmp(arg,"ALL",3) && (!arg[3] || arg[3]==' ')) { q.notes_unread=1;tail=arg+3; }
            else if(!number_arg(arg,&q.number,&tail))goto invalid;
            if(*tail && (stats || *tail!=' ' || !number_arg(tail+1,&q.cursor,&tail) || *tail))goto invalid;
        }
    } else if(!strcmp(line,"CONNECTIONS"))q.action='L';
    else if(!strncmp(line,"ANNOUNCE ",9)) { q.action='A';if(!chat_text_valid(line+9))goto invalid;strcpy(q.body,line+9); }
    else if(!strncmp(line,"DISCONNECT ",11)) { const char *tail;q.action='K';if(!number_arg(line+11,&q.number,&tail) || strcmp(tail," CONFIRM"))goto invalid; }
    else if(!strcmp(line,"SHUTDOWN CANCEL"))q.action='C';
    else if(!strcmp(line,"SHUTDOWN NOW CONFIRM"))q.action='S';
    else if(!strncmp(line,"SHUTDOWN ",9)) {
        const char *tail;q.action='S';if(!number_arg(line+9,&q.number,&tail) || !q.number || q.number>86400 || *tail!=' ' || !chat_text_valid(tail+1))goto invalid;strcpy(q.body,tail+1);
    } else return 0;
    submit(s,&q);
    if(s->pending && (q.action=='j' || q.action=='U' || q.action=='u')) {
        if(q.action=='j')strcpy(s->report,"BOARDLOG");
        else if(q.notes_unread)snprintf(s->report,sizeof(s->report),"%s ALL",q.action=='u'?"USERTABLE":"USERLOG");
        else snprintf(s->report,sizeof(s->report),"%s %u",q.action=='u'?"USERTABLE":"USERLOG",q.number?q.number:s->user);
        s->report_pending=1;
    }
    return 1;
invalid:emit(s,"ERR command\r\n");return 1;
}
static void gather_input(Session *s,const char *line) {
    if(!strcmp(line,"/CANCEL")) { s->gather_mode=0;s->gather_cursor=0;s->gather_lines=0;memset(&s->draft,0,sizeof(s->draft));emit(s,"CANCELLED\r\n");return; }
    if(s->gather_cursor) {
        if(!strcmp(line,"//") || !strcmp(line,"Q")) {
            s->gather_cursor=0;
            if(s->gather_mode==2)s->gather_mode=0;
            else { s->gather_lines=0;memset(&s->draft,0,sizeof(s->draft));queue_text(s,ui_text(&s->ui,MSG_GATHER_EDIT)); }
            return;
        }
        if(!*line || !strcmp(line,"+")) { BoardRequest q={0};q.op='!';q.action='G';q.cursor=s->gather_cursor;submit(s,&q);s->gather_pending=s->pending?(s->gather_mode==2?2:1):0; }
        return;
    }
    if(strcmp(line,"//")) {
        if(!gather_line_valid(line)) { queue_text(s,ui_text(&s->ui,MSG_GATHER_INVALID));return; }
        size_t n=strlen(s->draft.body);snprintf(s->draft.body+n,sizeof(s->draft.body)-n,"%s\r\n",line);s->gather_lines++;
        if(s->gather_lines<10)return;
    }
    if(!s->gather_lines) { s->gather_mode=0;emit(s,"CANCELLED\r\n");return; }
    s->draft.op='!';s->draft.action='g';s->draft.scope=s->ui.enabled?(int)s->ui.language+1:0;submit(s,&s->draft);s->gather_pending=s->pending?3:0;
}

static const char *application_fields[]={"REALNAME","KANA","ADDRESS","PHONE","BIRTHDAY","MACHINE","HANDLE","LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT","FROMBBS","SYSOPNOTE"};
static void application_prompt(Session *s) { char text[180];snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"%s（省略はEnter、中止は/CANCEL）: ":"%s (Enter to skip, /CANCEL to abort): ",application_fields[s->application_step-1]);queue_text(s,text); }
static void command_core(Session *s,char *line) {
    if(s->application_step) {
        if(!strcmp(line,"/CANCEL") || !strcmp(line,"/cancel")) { s->application_step=0;OPENSSL_cleanse(s->application_body,sizeof(s->application_body));emit(s,"CANCELLED\r\n");return; }
        if(s->application_step==15) {
            if(!strcmp(line,"Y") || !strcmp(line,"y")) { s->application_step=0;s->password_mode='g';emit(s,"PASSWORD\r\n"); }
            else if(!strcmp(line,"E") || !strcmp(line,"e")) { s->application_step=1;s->application_body[0]=0;application_prompt(s); }
            else queue_text(s,"Y / E / /CANCEL > ");return;
        }
        const char *key=application_fields[s->application_step-1];int field=profile_field(key);
        if(strlen(line)>320 || !safe_text(line) || (field>=0 && !profile_text_valid((unsigned)field,line))) { emit(s,"ERR application value\r\n");application_prompt(s);return; }
        size_t n=strlen(s->application_body);snprintf(s->application_body+n,sizeof(s->application_body)-n,"%s|%s\n",key,line);
        if(++s->application_step>14) { queue_text(s,s->application_body);queue_text(s,s->ui.language==LANGUAGE_JA?"この内容で申請しますか？ Y:確定 E:入力し直す /CANCEL:中止 > ":"Submit these details? Y:confirm E:edit /CANCEL:abort > "); }else application_prompt(s);return;
    }
    if(members_auto_ids() && !strcmp(line,"REGISTER"))line="REGISTER auto";
    if(s->editing && s->memo_input) { s->memo_input=0;if(!strcmp(line,"/CANCEL"))return;BoardRequest q={0};q.op='&';q.action='M';if(strlen(line)>80 || !safe_text(line)) { emit(s,"ERR memo\r\n");return; }strcpy(q.body,line);submit(s,&q);s->chat_pending=s->pending;return; }
    if(s->editing && s->editor_line && !s->editor_input && !strcmp(line,"\\")) { s->memo_input=1;queue_text(s,s->ui.language==LANGUAGE_JA?"接続者メモ > ":"Connection memo > ");return; }
    char editor_aux[INPUT_LINE_MAX+2];
    if(s->editing && s->editor_line && !s->editor_input && !s->editor_confirm && (!strcmp(line,"W") || !strcmp(line,"T") || !strncmp(line,"T ",2) || !strcmp(line,"#"))) { snprintf(editor_aux,sizeof(editor_aux),"/%s",line);line=editor_aux; }

    char out[128];
    if(s->editing && (!strcmp(line,"/BUFFER") || !strcmp(line,"/TOBUFFER"))) { BoardRequest q={0};q.op=')';q.action=!strcmp(line,"/BUFFER")?'E':'C';q.scope=1;if(q.action=='C')strcpy(q.body,s->draft.body);submit(s,&q);return; }
    if(s->password_mode=='f' || s->password_mode=='v' || s->password_mode=='w') {
        int mode=s->password_mode;s->password_mode=0;s->auth_after=s->now+1000;
        if(!strcmp(line,"/CANCEL"))emit(s,"CANCELLED\r\n");
        else if(strlen(line)<8 || strlen(line)>128)emit(s,"ERR password 8..128 bytes\r\n");
        else if(mode=='v') { strcpy(s->new_password,line);s->password_mode='w';emit(s,"CONFIRM PASSWORD\r\n"); }
        else if(mode=='w' && strcmp(line,s->new_password))emit(s,"ERR password mismatch\r\n");
        else { BoardRequest q={0};q.op='Y';q.action=mode=='f'?VAULT_CMD_UNLOCK:VAULT_CMD_PASSWORD;snprintf(q.body,sizeof(q.body),"%s|%s",s->filepass_path,line);submit(s,&q);OPENSSL_cleanse(&q,sizeof(q)); }
        if(mode!='v')OPENSSL_cleanse(s->new_password,sizeof(s->new_password));
        OPENSSL_cleanse(line,strlen(line));return;
    }
    if(s->password_mode=='b') {
        s->password_mode=0;s->auth_after=s->now+1000;
        if(!strcmp(line,"/CANCEL"))emit(s,"CANCELLED\r\n");
        else if(strlen(line)<8 || strlen(line)>128)emit(s,"ERR password 8..128 bytes\r\n");
        else { BoardRequest q={0};q.op='(';q.action='U';q.number=s->board_enter;q.cursor=s->board_lock;strcpy(q.password,line);submit(s,&q);OPENSSL_cleanse(&q,sizeof(q)); }
        OPENSSL_cleanse(line,strlen(line));return;
    }
    if(s->password_mode=='o' || s->password_mode=='p' || s->password_mode=='c') {
        int mode=s->password_mode;
        if(!strcmp(line,"/CANCEL")) { emit(s,"CANCELLED\r\n"); }
        else if(strlen(line)<8 || strlen(line)>128) { emit(s,"ERR password 8..128 bytes\r\n"); }
        else if(mode=='o') { strcpy(s->old_password,line); s->password_mode='p'; emit(s,"NEW PASSWORD\r\n"); return; }
        else if(mode=='p' && strcmp(line,s->old_password)) { strcpy(s->new_password,line); s->password_mode='c'; emit(s,"CONFIRM PASSWORD\r\n"); return; }
        else if(mode=='c' && !strcmp(line,s->new_password)) {
            BoardRequest q={0}; q.op=s->boardpass_target?'(':s->reset_target?'~':'k';q.number=s->boardpass_target?s->boardpass_target:s->reset_target;q.action=s->boardpass_target?'S':1; strcpy(q.password,s->old_password); strcpy(q.new_password,s->new_password);
            submit(s,&q); OPENSSL_cleanse(&q,sizeof(q));
        } else emit(s,mode=='c'?"ERR password mismatch\r\n":"ERR password unchanged\r\n");
        s->password_mode=0;s->reset_target=0;s->boardpass_target=0; s->auth_after=s->now+1000;
        OPENSSL_cleanse(s->old_password,sizeof(s->old_password)); OPENSSL_cleanse(s->new_password,sizeof(s->new_password));
        return;
    }
    if(s->password_mode) {
        if(s->ui.enabled && !strcmp(line,"/CANCEL")) { s->password_mode=0; emit(s,"CANCELLED\r\n"); return; }
        BoardRequest q={0}; q.op=(char)s->password_mode; strcpy(q.username,s->username);if(q.op=='g') { strcpy(q.body,s->application_body);OPENSSL_cleanse(s->application_body,sizeof(s->application_body)); }
        s->password_mode=0; s->auth_after=s->now+1000;
        if(strlen(line)<8 || strlen(line)>128) emit(s,"ERR password 8..128 bytes\r\n");
        else { strcpy(q.password,line); submit(s,&q); }
        OPENSSL_cleanse(q.password,sizeof(q.password)); OPENSSL_cleanse(line,strlen(line)); return;
    }
    if(s->editing && (!strcmp(line,"/#") || !strcmp(line,"/STATUS"))) { session_status(s);return; }
    if(s->editing && (!strcmp(line,"/ONLINE") || !strcmp(line,"/W") || !strcmp(line,"/STATUS") || !strcmp(line,"/#") || !strcmp(line,"/TALK") || !strncmp(line,"/TALK ",6) || !strncmp(line,"/T ",3) || !strcmp(line,"/T") || !strncmp(line,"/MEMO ",6) || !strcmp(line,"/CALL"))) {
        BoardRequest q={0};q.op='&';q.action=(!strncmp(line,"/T",2))?'V':!strncmp(line,"/MEMO ",6)?'M':!strcmp(line,"/CALL")?'C':'O';
        if(q.action=='V') { s->talk_once=strncmp(line,"/TALK",5)!=0;s->talk_return_editor=s->talk_once; }
        if(q.action=='V' && !board_feature_enabled("chat")) { emit(s,"ERR feature disabled\r\n");return; }
        const char *arg=strchr(line,' ');if(q.action=='V' && arg && !talk_number(arg+1,&q.chat_target)) { emit(s,"ERR command\r\n");return; }
        if(q.action=='M') { if(strlen(arg+1)>80 || !safe_text(arg+1)) { emit(s,"ERR command\r\n");return; }strcpy(q.body,arg+1); }
        if(q.action=='C')strcpy(q.body,"SYSOP");
        submit(s,&q);s->chat_pending=s->pending;return;
    }
    if(s->editing) {
        char translated[INPUT_LINE_MAX+128]="";if(classic_editor(s,line,translated,sizeof(translated)))return;
        if(*translated)line=translated;
        if(!strcmp(line,"/HELP")) { show_help(s,s->draft.op=='m'?"MAIL":"POST"); return; }
        if(!strcmp(line,"/SAVE") || ((s->draft.op=='Y' || s->draft.op==')' || s->draft.op=='(') && !strcmp(line,"."))) {
            if(s->draft.op=='Y') { if(strlen(s->draft.body)>8192) { emit(s,"ERR profile text\r\n");return; }BoardRequest q=s->draft;snprintf(q.body,sizeof(q.body),"%.48s|%.8192s",s->draft.title,s->draft.body);submit(s,&q);return; }
            if(s->draft.op=='(' || s->draft.op==')') { submit(s,&s->draft);return; }
            if(s->draft.op=='?') { queue_text(s,ui_text(&s->ui,MSG_SYSOP_MAIL_NO_DRAFT));return; }
            if(s->draft.op=='m') { s->draft.action='W';s->draft.mail_revision=s->mail_revision;mail_submit(s,&s->draft);return; }
            if(!s->user) { emit(s,"ERR login required\r\n"); return; }
            s->draft.op='w'; s->draft.cursor=s->draft_revision; submit(s,&s->draft); return;
        }
        if(s->draft.op=='m' && (!strncmp(line,"/ATTACH ",8) || !strcmp(line,"/DETACH") || !strncmp(line,"/DETACH ",8))) {
            uint32_t id=0;const char *tail;
            if(strcmp(line,"/DETACH") && (!number_arg(line+8,&id,&tail) || *tail || id>VAULT_ID_MAX)) { emit(s,"ERR vault syntax\r\n");return; }
            BoardRequest q={0};q.action='A';q.number=id;q.cursor=!strncmp(line,"/DETACH ",8);q.mail_attachments=s->draft.mail_attachments;mail_submit(s,&q);return;
        }
        if(edit_draft(s,line)) return;
        if(!strcmp(line,"/CANCEL")) { s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0; memset(&s->draft,0,sizeof(s->draft)); s->draft_revision=0; undo_clear(s); emit(s,"CANCELLED\r\n"); }
        else if(!strcmp(line,"/PREVIEW")) {
            if(s->draft.op=='m')for(unsigned i=0;i<s->draft.mail_attachments.count;i++) { char text[256];snprintf(text,sizeof(text),s->ui.enabled?ui_text(&s->ui,MSG_MAIL_ATTACHMENT_SET):"MAIL ATTACHMENT %u\r\n",s->draft.mail_attachments.ids[i]);queue_text(s,text); }
            char preview[BOARD_REPLY];
            snprintf(preview,sizeof(preview),"PREVIEW PARENT %u AUTHOR %s\r\nTITLE %s\r\nBODY %zu\r\n%sEND\r\n",
                     s->draft.number,s->user?s->username:"guest",s->draft.title,strlen(s->draft.body),s->draft.body);
            emit(s,preview);
        }
        else if(!strcmp(line,".")) {
            if(!s->draft.body[0] && s->draft.op!='(' && s->draft.op!=')') { emit(s,"ERR empty body\r\n"); return; }
            if(s->draft.op=='(' || s->draft.op==')')submit(s,&s->draft);
            else if(s->draft.op=='m') { s->draft.action='S';s->draft.mail_revision=s->mail_revision;mail_submit(s,&s->draft); }
            else if(s->draft.op=='?') { submit(s,&s->draft);s->service_pending=s->pending?'M':0; }
            else { s->draft.op='P'; submit(s,&s->draft); }
            if(s->pending) { s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0; undo_clear(s); }
        } else {
            if(!strncmp(line,"..",2) || !strcmp(line,"//PREVIEW") || !strcmp(line,"//LINES") || !strcmp(line,"//UNDO") || !strcmp(line,"//SAVE") || !strcmp(line,"//HELP") ||
               (line[0]=='/' && (draft_word(line+1,"/TITLE") || draft_word(line+1,"/REPLACE") || draft_word(line+1,"/DELETE") || draft_word(line+1,"/INSERT")))) line++;
            size_t have=strlen(s->draft.body), n=strlen(line);
            if(!safe_text(line)) emit(s,"ERR control character\r\n");
            else if(have+n+2>BOARD_BODY) emit(s,"ERR body limit 262144 bytes; cancel or finish\r\n");
            else { undo_save(s); memcpy(s->draft.body+have,line,n); memcpy(s->draft.body+have+n,"\r\n",3); }
        }
        return;
    }
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
        q.number=q.action=='G'?s->current:s->board;
        if(*arg) { if(!number_arg(arg+1,&q.number,&tail))goto board_syntax; }else tail=arg;
        if(q.action=='O' && *tail==' ') { if(strlen(tail+1)>120)goto board_syntax;strcpy(q.body,tail+1);for(char *p=q.body;*p && *p!=' ';p++)if(*p>='a' && *p<='z')*p-=32;tail=""; }
        else if(q.action=='A' && *tail==' ') { if(!number_arg(tail+1,&q.cursor,&tail))goto board_syntax; }
        else if(q.action=='P' && !strcmp(tail," NONE CONFIRM")) { q.action='C';tail=""; }
        else if(q.action=='D') { if(strcmp(tail," CONFIRM"))goto board_syntax;
            tail=""; }
        else if(q.action=='M') {
            q.cursor=q.number;q.number=s->board;
            if(*tail!=' ' || !number_arg(tail+1,&q.audit_article,&tail) || strcmp(tail," CONFIRM"))goto board_syntax;
            tail="";
        }
        if(*tail)goto board_syntax;
        if(q.action=='P' && s->now<s->auth_after) { emit(s,"ERR try later\r\n");return; }
        submit(s,&q);
        if(q.action=='A' && s->pending) { snprintf(s->report,sizeof(s->report),"BOARDACCESS %u",q.number);s->report_pending=1; }
        return;
board_syntax:emit(s,"ERR command\r\n");return;
    }
    if(!strncmp(line,"RESETPASS ",10)) {
        if(!s->ui.admin) { emit(s,"ERR admin required\r\n");return; }
        BoardRequest q={0};const char *tail;q.op='~';
        if(!number_arg(line+10,&q.number,&tail) || *tail) { emit(s,"ERR command\r\n");return; }
        if(s->now<s->auth_after) { emit(s,"ERR try later\r\n");return; }
        submit(s,&q);return;
    }
    if(!strcmp(line,"EDITOR LINE") || !strcmp(line,"EDITOR BETA")) { s->editor_line=!strcmp(line,"EDITOR LINE");s->editor_input=0;s->editor_at=1;queue_text(s,ui_text(&s->ui,s->editor_line?MSG_EDITOR_LINE:MSG_EDITOR_BETA));return; }
    if(service_command(s,line))return;
    if(mail_command(s,line))return;
    if(!strcmp(line,"TALK") || !strncmp(line,"TALK ",5) || !strcmp(line,"/REPLY") || !strcmp(line,"ONLINE")) {
        if(!board_feature_enabled("chat")) { emit(s,"ERR feature disabled\r\n");return; }
        if(!s->user) { emit(s,"ERR login required\r\n");return; }
        BoardRequest q={0};q.op='&';q.action=!strcmp(line,"ONLINE")?'O':'V';
        if(!strcmp(line,"/REPLY")) { if(!s->reply_target) { chat_error(s,MSG_TALK_INVALID,"CHAT ERROR target\r\n");return; }q.chat_target=s->reply_target; }
        else if(!strncmp(line,"TALK ",5) && !talk_number(line+5,&q.chat_target)) { emit(s,"ERR command\r\n");return; }
        submit(s,&q);s->chat_pending=s->pending;return;
    }
    if(!strcmp(line,"MEMO") || !strncmp(line,"MEMO ",5) || !strcmp(line,"CALL") || !strncmp(line,"CALL ",5)) {
        BoardRequest q={0};q.op='&';q.action=*line=='M'?'M':'C';
        const char *text=line[4]?line+5:q.action=='C'?"SYSOP":"";
        if(strlen(text)>(q.action=='M'?80u:512u) || (*text && !safe_text(text))) { emit(s,"ERR command\r\n");return; }
        strcpy(q.body,text);submit(s,&q);s->chat_pending=s->pending;return;
    }
    if(!strcmp(line,"CHAT")) {
        if(!board_feature_enabled("chat")) { emit(s,"ERR feature disabled\r\n");return; }
        if(!s->user || !s->member_mode) { emit(s,"ERR login required\r\n");return; }
        BoardRequest q={0};q.op='&';q.action='J';submit(s,&q);s->chat_pending=s->pending;return;
    }
    if(extended_command(s,line)) return;
    if(!strcmp(line,"DRAFT") || !strcmp(line,"RESUME") || !strncmp(line,"DRAFT DELETE ",13)) {
        if(!s->user) { emit(s,"ERR login required\r\n"); return; }
        BoardRequest q={0}; q.op=!strcmp(line,"DRAFT")?'d':!strcmp(line,"RESUME")?'o':'X';
        if(q.op=='X' && !audit_number(line+13,&q.cursor)) { emit(s,"ERR draft revision\r\n"); return; }
        submit(s,&q); return;
    }
    if(!strncmp(line,"REJECT ",7) || !strncmp(line,"DELETEAPPLICATION ",18) || !strncmp(line,"DELETEUSER ",11) || !strncmp(line,"RETIRE",6)) {
        BoardRequest q={0};q.op=BOARD_MEMBER_LIFECYCLE;
        if(!strcmp(line,"RETIRE CONFIRM"))q.action=3;
        else { const char *tail;const char *arg=strchr(line,' ');q.action=!strncmp(line,"REJECT ",7)?1:!strncmp(line,"DELETEAPPLICATION ",18)?2:4;
            if(!arg || !strncmp(line,"RETIRE",6) || !number_arg(arg+1,&q.number,&tail) || q.number>MEMBER_MAX || strcmp(tail," CONFIRM")) { emit(s,"ERR command\r\n");return; }
        }
        submit(s,&q);return;
    }
    if(!strcmp(line,"RIGHTS") || !strncmp(line,"RIGHTS ",7) || !strncmp(line,"LEVEL ",6) || !strncmp(line,"GROUP ",6) || !strncmp(line,"UNGROUP ",8) || !strncmp(line,"APPROVE ",8) || !strcmp(line,"APPLICATIONS") || !strncmp(line,"APPLICATIONS ",13)) {
        BoardRequest q={0};q.op='%';const char *tail="";
        if(!strncmp(line,"APPLICATIONS",12)) { q.action='P';if(line[12] && (!number_arg(line+13,&q.cursor,&tail) || *tail || q.cursor>MEMBER_MAX))goto rights_error; }
        else {
            const char *arg=strchr(line,' ');
            if(arg && (!number_arg(arg+1,&q.number,&tail) || q.number>MEMBER_MAX))goto rights_error;
            if(!strncmp(line,"LEVEL ",6))q.action='L';
            else if(!strncmp(line,"GROUP ",6))q.action='G';
            else if(!strncmp(line,"UNGROUP ",8))q.action='D';
            else if(!strncmp(line,"APPROVE ",8))q.action='A';
            if(q.action && q.action!='A') {
                if(*tail!=' ' || (!audit_number(tail+1,&q.cursor) && !(q.action=='L' && !strcmp(tail+1,"0"))) || q.cursor>(q.action=='L'?5u:100u) || (q.action!='L' && !q.cursor))goto rights_error;
            } else if(*tail)goto rights_error;
        }
        submit(s,&q);return;
    rights_error:emit(s,"ERR rights syntax\r\n");return;
    }
    if(!strcmp(line,"PREFS") || !strcmp(line,"PREFS SAVE") || !strcmp(line,"PREFS LOAD")) {
        if(!s->user) { emit(s,"ERR login required\r\n"); return; }
        BoardRequest q={0}; q.op='V'; q.number=!strcmp(line,"PREFS SAVE")?1:!strcmp(line,"PREFS LOAD")?2:0;
        q.cursor=(uint32_t)s->ui.language; q.title[0]=s->pager_enabled?'1':'0'; submit(s,&q); return;
    }
    if(!strcmp(line,"PAGER") || !strcmp(line,"PAGER ON") || !strcmp(line,"PAGER OFF")) {
        if(!strcmp(line,"PAGER ON")) s->pager_enabled=1;
        if(!strcmp(line,"PAGER OFF")) s->pager_enabled=0;
        emit(s,s->pager_enabled?"PAGER ON\r\n":"PAGER OFF\r\n"); return;
    }
    if(!strncmp(line,"PAGER SIZE ",11)) {
        uint32_t cols,rows; const char *tail;
        if(!number_arg(line+11,&cols,&tail) || *tail!=' ' || !number_arg(tail+1,&rows,&tail) || *tail || cols<20 || cols>240 || rows<5 || rows>100) {
            emit(s,"ERR pager size\r\n"); return;
        }
        session_dimensions(s,cols,rows); emit(s,"PAGER SIZE SET\r\n"); return;
    }
    if(!strcmp(line,"SEARCH") || !strncmp(line,"SEARCH ",7) || !strcmp(line,"SEARCHFROM") || !strncmp(line,"SEARCHFROM ",11)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        BoardRequest q={0}; q.op='f'; const char *query="";
        if(!strncmp(line,"SEARCHFROM",10)) {
            const char *tail;
            if(!line[10] || !number_arg(line+11,&q.cursor,&tail) || *tail!=' ') { emit(s,"ERR search cursor\r\n"); return; }
            query=tail+1;
        } else if(line[6]) query=line+7;
        if(!*query || strlen(query)>80 || !safe_text(query)) { emit(s,"ERR search query\r\n"); return; }
        strcpy(q.title,query); submit(s,&q); return;
    }
    if(!strcmp(line,"PASSWD")) {
        if(!s->user) { emit(s,"ERR login required\r\n"); return; }
        if(s->now<s->auth_after) { emit(s,"ERR try later\r\n"); return; }
        s->password_mode='o'; emit(s,"CURRENT PASSWORD\r\n"); return;
    }
    if(!strcmp(line,"BATCH") || !strncmp(line,"BATCH ",6)) {
        if(!s->user) { emit(s,"ERR login required\r\n");return; }
        if(!board_feature_enabled("vault")) { emit(s,"ERR feature disabled\r\n");return; }
        if(!strncmp(line,"BATCH MATCH ",12)) { if(!line[12] || strlen(line+12)>80 || !safe_text(line+12))goto batch_error;BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_MATCH;snprintf(q.body,sizeof(q.body),"%s|%s",*s->vault_path?s->vault_path:"-",line+12);submit(s,&q);return; }
        if(!strncmp(line,"BATCH COPY ",11)) {
            const char *path=line+11;if(!*path || strlen(path)>48 || !safe_text(path) || !s->batch_count)goto batch_error;
            if(strcmp(path,s->batch_copy_path))memset(s->batch_copied,0,sizeof(s->batch_copied));strcpy(s->batch_copy_path,path);
            s->batch_copy_at=0;while(s->batch_copy_at<s->batch_count && s->batch_copied[s->batch_copy_at])s->batch_copy_at++;
            if(s->batch_copy_at==s->batch_count) { emit(s,"BATCH COPY COMPLETE\r\n");return; }
            BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_COPY;q.number=s->batch[s->batch_copy_at];strcpy(q.body,path);submit(s,&q);s->batch_copying=s->pending;return;
        }
        if(!strcmp(line,"BATCH CLEAR")) { s->batch_count=0;memset(s->batch_copied,0,sizeof(s->batch_copied)); }
        else if(!strncmp(line,"BATCH ADD ",10) || !strncmp(line,"BATCH REMOVE ",13)) {
            int add=!strncmp(line,"BATCH ADD ",10);const char *tail;uint32_t id;
            if(!number_arg(line+(add?10:13),&id,&tail) || *tail || id>VAULT_ID_MAX)goto batch_error;
            unsigned i=0;while(i<s->batch_count && s->batch[i]!=id)i++;
            if(add && i==s->batch_count) { if(s->batch_count>=ln_limit("BATCH_FILES",16))goto batch_error;s->batch_done[s->batch_count]=0;s->batch_copied[s->batch_count]=0;s->batch[s->batch_count++]=id; }
            if(!add && i<s->batch_count) { memmove(s->batch_copied+i,s->batch_copied+i+1,s->batch_count-i-1);memmove(s->batch_done+i,s->batch_done+i+1,s->batch_count-i-1);memmove(s->batch+i,s->batch+i+1,(s->batch_count-i-1)*sizeof(s->batch[0]));s->batch_count--; }
        } else if(!strcmp(line,"BATCH SEND")) {
            if(!s->batch_count)goto batch_error;
            if(!s->zmodem_available || !s->binary_allowed) { queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return; }
            size_t used=0;unsigned pending=0;
            for(unsigned i=0;i<s->batch_count;i++)if(!s->batch_done[i]) { used+=(size_t)snprintf(s->zmodem_text+used,sizeof(s->zmodem_text)-used,"%s%u",pending?",":"",s->batch[i]);pending++; }
            if(!pending) { emit(s,"BATCH COMPLETE\r\n");return; }
            s->zmodem_protocol=s->transfer_protocol;if(s->zmodem_protocol==1 || s->zmodem_protocol>=5)goto batch_error;
            s->zmodem_receive=2;s->zmodem_id=pending;s->zmodem_start=1;
            BoardRequest q={0};q.op='@';submit(s,&q);if(!s->pending)s->zmodem_start=0;return;
        } else if(strcmp(line,"BATCH") && strcmp(line,"BATCH LIST"))goto batch_error;
        char text[384],ids[192]="";size_t used=0;
        for(unsigned i=0;i<s->batch_count;i++)used+=(size_t)snprintf(ids+used,sizeof(ids)-used,"%s%u",i?",":"",s->batch[i]);
        snprintf(text,sizeof(text),s->ui.enabled?ui_text(&s->ui,MSG_BATCH_LIST):"BATCH %u %s\r\n",s->batch_count,*ids?ids:"-");queue_text(s,text);
        for(unsigned i=0;i<s->batch_count;i++) { snprintf(text,sizeof(text),"BATCH FILE %u %s\r\n",s->batch[i],s->batch_done[i]?"DONE":"PENDING");queue_text(s,text);if(s->batch_copied[i]) { snprintf(text,sizeof(text),"BATCH COPY %u DONE\r\n",s->batch[i]);queue_text(s,text); } }return;
    batch_error:queue_text(s,ui_text(&s->ui,MSG_BATCH_ERROR));return;
    }
    if(!strcmp(line,"ZMAILRESUME") || !strncmp(line,"ZMAILRESUME ",12) || !strcmp(line,"ZMAILUPLOAD") || !strncmp(line,"ZMAILUPLOAD ",12) || !strncmp(line,"ZMAILDOWNLOAD ",14)) {
        if(!s->user) { emit(s,"ERR login required\r\n");return; }
        if(!s->zmodem_available || !s->binary_allowed) { queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return; }
        const char *arg="",*tail;uint32_t id=0;int resume=!strncmp(line,"ZMAILRESUME",11);int receive=resume || !strncmp(line,"ZMAILUPLOAD",11);
        if(receive)arg=line[11]?line+12:"";
        else { if(!number_arg(line+14,&id,&tail) || id>VAULT_ID_MAX || (*tail && *tail!=' ')) { emit(s,"ERR vault syntax\r\n");return; }arg=*tail?tail+1:""; }
        if(strlen(arg)>512 || (*arg && !safe_text(arg))) { emit(s,"ERR vault syntax\r\n");return; }
        s->zmodem_protocol=0;s->zmodem_receive=resume?8:receive?4:5;s->zmodem_id=id;strcpy(s->zmodem_text,arg);s->zmodem_start=1;
        BoardRequest q={0};q.op='@';q.action=1;submit(s,&q);if(!s->pending)s->zmodem_start=0;return;
    }
    if(!strncmp(line,"ZUPLOADBATCHRESUME ",19) || !strcmp(line,"ZUPLOADBATCHRESUME") || !strncmp(line,"ZUPLOADBATCH ",13) || !strcmp(line,"ZUPLOADBATCH") || !strncmp(line,"ZRESUME ",8) || !strcmp(line,"ZRESUME")) {
        if(!s->user) { emit(s,"ERR login required\r\n");return; }
        if(!s->zmodem_available || !s->binary_allowed) { queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return; }
        int batch_resume=!strncmp(line,"ZUPLOADBATCHRESUME",18);int resume=!strncmp(line,"ZRESUME",7);size_t prefix=batch_resume?18:resume?7:12;const char *arg=line[prefix]?line+prefix+1:((resume || batch_resume) && *s->vault_path?s->vault_path:"-");
        size_t n=strcspn(arg,"|");if(!n || n>48 || strlen(arg)>289 || !safe_text(arg)) { emit(s,"ERR vault syntax\r\n");return; }
        s->zmodem_protocol=0;s->zmodem_receive=batch_resume?9:resume?7:6;s->zmodem_id=0;strcpy(s->zmodem_text,arg);s->zmodem_start=1;
        BoardRequest q={0};q.op='@';submit(s,&q);if(!s->pending)s->zmodem_start=0;return;
    }
    if(!strncmp(line,"ZUPLOADIN ",10) || !strcmp(line,"ZUPLOAD") || !strncmp(line,"ZUPLOAD ",8) || !strncmp(line,"ZDOWNLOAD ",10)) {
        if(!s->user) { emit(s,"ERR login required\r\n");return; }
        if(!s->zmodem_available || !s->binary_allowed) { queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return; }
        const char *arg="";uint32_t id=0;int receive=!strncmp(line,"ZUPLOAD",7);
        if(!strncmp(line,"ZUPLOADIN ",10)) { receive=3;arg=line+10;size_t n=strcspn(arg,"|");if(!n || n>48 || (arg[n] && strlen(arg+n+1)>240)) { emit(s,"ERR vault syntax\r\n");return; } }
        else if(receive)arg=line[7]?line+8:"";
        else { const char *tail;if(!number_arg(line+10,&id,&tail) || id>VAULT_ID_MAX || (*tail && *tail!=' ')) { emit(s,"ERR vault syntax\r\n");return; }arg=*tail?tail+1:""; }
        if(strlen(arg)>(receive==3?289u:80u) || (*arg && !safe_text(arg))) { emit(s,"ERR vault syntax\r\n");return; }
        s->zmodem_protocol=0;s->zmodem_receive=receive;s->zmodem_id=id;strcpy(s->zmodem_text,arg);s->zmodem_start=1;
        BoardRequest q={0};q.op='@';submit(s,&q);if(!s->pending)s->zmodem_start=0;return;
    }
    if(!strcmp(line,"TRANSFER BINARY")) {
        if(!s->binary_allowed) { emit(s,"ERR binary negotiation\r\n");return; }
        if(!s->board_enabled || !s->user) { emit(s,"ERR login required\r\n");return; }
        BoardRequest q={0};q.op='@';submit(s,&q);return;
    }
    if(!strncmp(line,"MFILE",5)) {
        if(s->ui.enabled) { emit(s,"ERR vault client\r\n");return; }
        BoardRequest q={0};q.op=BOARD_MAIL_TRANSFER;snprintf(q.body,sizeof(q.body),"%s",line+1);submit(s,&q);return;
    }
    if(transfer_line_command(line)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n");return; }
        if(s->ui.enabled) { emit(s,"ERR vault client\r\n");return; }
        BoardRequest q={0};q.op='$';snprintf(q.body,sizeof(q.body),"%s",line);submit(s,&q);return;
    }
    if(!strcmp(line,"FILES") || !strncmp(line,"FILES ",6) || !strncmp(line,"FILE ",5) ||
       !strncmp(line,"FILEDESC ",9) || !strncmp(line,"FILERESTORE ",12) || !strncmp(line,"FILENEWS ",9) || !strncmp(line,"FILENEWSFROM ",13) ||
       !strncmp(line,"FILEDELETE ",11) || !strncmp(line,"FILEHIDE ",9) || !strncmp(line,"FILESEARCH ",11) ||
       !strncmp(line,"FILESEARCHFROM ",15) || !strncmp(line,"FILESIN ",8) || !strncmp(line,"FILESINFROM ",12) ||
       !strncmp(line,"FILECATEGORY ",13) || !strcmp(line,"FILECATEGORIES") || !strncmp(line,"FILECATEGORIES ",15) ||
       !strcmp(line,"FILEBOARDS") || !strncmp(line,"FILEBOARDS ",11) || !strcmp(line,"FILEAUDIT") || !strncmp(line,"FILEAUDIT ",10)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n");return; }
        BoardRequest q={0};q.op='Y';const char *tail=NULL,*argument=NULL;
        if(!strncmp(line,"FILEDELETE ",11)) {
            q.action=VAULT_CMD_DELETE;if(!number_arg(line+11,&q.number,&tail) || strcmp(tail," CONFIRM"))goto vault_syntax;
        } else if(!strncmp(line,"FILEDESC ",9)) {
            q.action=VAULT_CMD_DESCRIPTION;
            if(!number_arg(line+9,&q.number,&tail) || (*tail && *tail!=' '))goto vault_syntax;
            const char *text=*tail?tail+1:"";
            if(strlen(text)>240 || (*text && !safe_text(text)))goto vault_syntax;
            strcpy(q.body,text);
        } else if(!strncmp(line,"FILERESTORE ",12)) {
            q.action=VAULT_CMD_RESTORE;
            if(!number_arg(line+12,&q.number,&tail) || strcmp(tail," CONFIRM"))goto vault_syntax;
        } else if(!strncmp(line,"FILENEWS",8)) {
            q.action=VAULT_CMD_NEWS;
            if(!strncmp(line+8,"FROM ",5)) { if(!number_arg(line+13,&q.number,&tail) || *tail!=' ')goto vault_syntax;argument=tail+1; }
            else argument=line+9;
        } else if(!strncmp(line,"FILESEARCH",10) || !strncmp(line,"FILESIN",7)) {
            int search=!strncmp(line,"FILESEARCH",10),length=search?10:7;q.action=search?VAULT_CMD_SEARCH:VAULT_CMD_IN;
            if(!strncmp(line+length,"FROM ",5)) {
                if(!number_arg(line+length+5,&q.number,&tail) || *tail!=' ')goto vault_syntax;
                argument=tail+1;
            } else argument=line+length+1;
        } else if(!strncmp(line,"FILECATEGORY ",13)) {
            q.action=VAULT_CMD_CATEGORY;
            if(!number_arg(line+13,&q.number,&tail) || *tail!=' ')goto vault_syntax;
            argument=tail+1;
        } else if(!strncmp(line,"FILEBOARDS",10)) {
            q.action=VAULT_CMD_BOARDS;if(line[10] && (!number_arg(line+11,&q.number,&tail) || *tail))goto vault_syntax;
        } else if(!strncmp(line,"FILECATEGORIES",14) || !strncmp(line,"FILEAUDIT",9) || !strncmp(line,"FILES",5)) {
            int length=!strncmp(line,"FILECATEGORIES",14)?14:!strncmp(line,"FILEAUDIT",9)?9:5;
            q.action=length==14?VAULT_CMD_CATEGORIES:length==9?VAULT_CMD_AUDIT:VAULT_CMD_LIST;
            if(line[length] && (!number_arg(line+length+1,&q.number,&tail) || *tail))goto vault_syntax;
        } else if(!strncmp(line,"FILEHIDE ",9)) {
            q.action=VAULT_CMD_HIDE;
            if(!number_arg(line+9,&q.number,&tail) || strcmp(tail," CONFIRM"))goto vault_syntax;
        } else {
            q.action=VAULT_CMD_INFO;
            if(!number_arg(line+5,&q.number,&tail) || *tail)goto vault_syntax;
        }
        if(q.number>(q.action==VAULT_CMD_AUDIT?VAULT_ID_MAX:VAULT_ID_MAX))goto vault_syntax;
        if(argument) {
            if(!*argument || strlen(argument)>((q.action==VAULT_CMD_SEARCH || q.action==VAULT_CMD_NEWS)?129u:48u) || !safe_text(argument))goto vault_syntax;
            if(q.action==VAULT_CMD_SEARCH || q.action==VAULT_CMD_NEWS)strcpy(q.body,argument);else strcpy(q.title,argument);
        }
        submit(s,&q);return;
    vault_syntax:
        emit(s,"ERR vault syntax\r\n");return;
    }
    if(!strcmp(line,"AUDIT") || !strncmp(line,"AUDIT ",6)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        BoardRequest q={0}; q.op='A';
        if(line[5] && !audit_arguments(line+6,&q)) { emit(s,"ERR audit cursor\r\n"); return; }
        submit(s,&q); return;
    }
    if(!strcmp(line,"MEMBERS") || !strncmp(line,"MEMBERS ",8) || !strncmp(line,"MEMBER ",7) ||
       !strncmp(line,"SUSPEND ",8) || !strncmp(line,"RESUME ",7)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        BoardRequest q={0}; q.op=!strncmp(line,"MEMBERS",7)?'l':!strncmp(line,"MEMBER ",7)?'t':line[0]=='S'?'x':'y';
        q.number=q.op=='l'?1:0;
        const char *arg=strchr(line,' ');
        if(arg) { const char *tail; if(!number_arg(arg+1,&q.number,&tail) || *tail || q.number>MEMBER_MAX) { emit(s,"ERR member number\r\n"); return; } }
        submit(s,&q); return;
    }
    if(!strcmp(line,"ADMIN") || !strcmp(line,"HIDE") || !strncmp(line,"HIDE ",5) ||
       !strcmp(line,"RESTORE") || !strncmp(line,"RESTORE ",8) || !strcmp(line,"INSPECT") || !strncmp(line,"INSPECT ",8)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        BoardRequest q={0}; q.op=!strcmp(line,"ADMIN")?'z':line[0]=='H'?'h':line[0]=='R'?'r':'i';
        q.number=s->current;
        const char *arg=strchr(line,' ');
        if(arg) { const char *tail; if(!number_arg(arg+1,&q.number,&tail)||*tail) { emit(s,"ERR article number\r\n"); return; } }
        submit(s,&q); return;
    }
    if(!strcmp(line,"GAMES") || !strncmp(line,"DOOR ",5)) {
        BoardRequest q={0};q.op='}';q.action='J';const char *end;
        if(line[0]=='D' && (!number_arg(line+5,&q.number,&end) || *end || !s->binary_allowed)) { emit(s,"ERR command\r\n");return; }
        submit(s,&q);return;
    }
    if(!strncmp(line,"MENUMODE ",9)) {
        const char *mode=line+9;int n=!strcmp(mode,"BEGINNER")?0:!strcmp(mode,"STANDARD")?1:!strcmp(mode,"EXPERT")?2:-1;
        if(n<0) { emit(s,"ERR command\r\n");return; }s->menu_mode=n;s->menu_shown=0;
        if(s->user) { BoardRequest q={0};q.op='}';q.action='v';q.number=(uint32_t)n;submit(s,&q); }else emit(s,"MENU MODE CHANGED\r\n");return;
    }
    if(!s->user && (!strcmp(line,"HANDLE") || !strncmp(line,"HANDLE ",7))) {
        if(line[6]) { if(!safe_text(line+7) || !* (line+7) || strlen(line+7)>25) { emit(s,"ERR profile text\r\n");return; }snprintf(s->guest_handle,sizeof(s->guest_handle),"guest:%s",line+7); }
        snprintf(out,sizeof(out),"GUEST HANDLE %s\r\n",*s->guest_handle?s->guest_handle:"guest");emit(s,out);return;
    }
    if(!strncmp(line,"MOVEBRANCH ",11)) {
        BoardRequest q={0};const char *p;q.op='(';q.action='B';q.number=s->board;
        if(!number_arg(line+11,&q.cursor,&p) || *p++!=' ' || !number_arg(p,&q.audit_article,&p) || strcmp(p," CONFIRM")) { emit(s,"ERR command\r\n");return; }
        submit(s,&q);return;
    }
    if(!strncmp(line,"BOARDDELETEALL ",15) && strstr(line," CASCADE CONFIRM")) {
        BoardRequest q={0};const char *p;q.op='}';q.action='D';
        if(!number_arg(line+15,&q.number,&p) || strcmp(p," CASCADE CONFIRM")) { emit(s,"ERR command\r\n");return; }
        submit(s,&q);return;
    }
    if(!strncmp(line,"AUTHOR ",7) || !strncmp(line,"BOARDGUEST ",11)) {
        BoardRequest q={0};q.op='}';q.action=line[0]=='A'?'o':'p';const char *tail;
        if(!number_arg(line+(q.action=='o'?7:11),&q.number,&tail) || *tail++!=' ') { emit(s,"ERR command\r\n");return; }
        if(q.action=='o') { const char *end;if(!number_arg(tail,&q.cursor,&end) || strcmp(end," CONFIRM")) { emit(s,"ERR command\r\n");return; } }
        else { if(strcmp(tail,"ON") && strcmp(tail,"OFF")) { emit(s,"ERR command\r\n");return; }q.cursor=!strcmp(tail,"ON"); }
        submit(s,&q);return;
    }
    if(!strcmp(line,"ZMAILUPLOADBATCH") || !strncmp(line,"ZMAILUPLOADBATCH ",17) || !strcmp(line,"ZMAILRESUMEBATCH") || !strncmp(line,"ZMAILRESUMEBATCH ",17)) {
        if(!s->user) { emit(s,"ERR login required\r\n");return; }
        if(!s->zmodem_available || !s->binary_allowed) { queue_text(s,ui_text(&s->ui,MSG_ZMODEM_UNAVAILABLE));return; }
        const char *description=line[16]?line+17:"";
        if(strlen(description)>80 || (*description && !safe_text(description))) { emit(s,"ERR command\r\n");return; }
        s->zmodem_protocol=0;s->zmodem_receive=line[5]=='R'?12:11;s->zmodem_id=0;snprintf(s->zmodem_text,sizeof(s->zmodem_text),"%s",description);s->zmodem_start=1;
        BoardRequest q={0};q.op='@';q.action=1;submit(s,&q);if(!s->pending)s->zmodem_start=0;return;
    }
    if(!strncmp(line,"LOGIN ",6) || !strncmp(line,"REGISTER ",9)) {
        if(!s->member_mode) { emit(s,"ERR members disabled\r\n"); return; }
        if(s->user) { emit(s,"ERR logout first\r\n"); return; }
        if(s->now<s->auth_after) { emit(s,"ERR try later\r\n"); return; }
        const char *name=line+(line[0]=='L'?6:9); size_t n=strlen(name);
        if(!n || n>32) { emit(s,"ERR username\r\n"); return; }
        for(size_t i=0;i<n;i++) if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='0'&&name[i]<='9')||name[i]=='_'||name[i]=='-')) { emit(s,"ERR username\r\n"); return; }
        strcpy(s->username,name);
        if(line[0]=='R' && members_detailed_form()) { s->application_body[0]=0;s->application_step=1;application_prompt(s);return; }
        s->password_mode=line[0]=='L'?'a':'g'; emit(s,"PASSWORD\r\n");
    } else if(!strcmp(line,"ACCOUNT")) {
        if(!s->user) emit(s,"ERR login required\r\n");
        else { snprintf(out,sizeof(out),"ACCOUNT %u %s\r\n",s->user,s->username); emit(s,out); }
    } else if(!strcmp(line,"LOGOUT")) {
        if(s->user && s->exit_after_save!=3) { s->exit_after_save=1;command_core(s,"PREFS SAVE");if(!s->pending)s->exit_after_save=0;return; }
        s->exit_after_save=0;selection_reset(s);
        s->classic_input=0;s->classic_id=0;s->classic_area=CM_MAIN;s->checking=s->check_pending=s->check_finished=0;s->check_since=s->check_until=0;s->check_cursor=0;memset(s->vault_grants,0,sizeof(s->vault_grants));
        chat_disconnect(s->id);s->mail_mode=0;OPENSSL_cleanse(s->mail_query,sizeof(s->mail_query));s->mail_filter=0;s->mail_cursor=0;
        memset(s->board_grants,0,sizeof(s->board_grants));s->batch_count=0;s->vault_mode=0;s->mail_arrivals=0;s->user=0;s->ui.admin=0;s->ui.sigop=0;s->reset_target=0; s->user_epoch=0; s->current=0; s->board=1; memset(&s->draft,0,sizeof(s->draft)); s->draft_revision=0; undo_clear(s); emit(s,"LOGGED OUT\r\n");
    } else if(!strcmp(line,"NEXTBOARD")) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        if(!s->user) { emit(s,"ERR login required\r\n"); return; }
        BoardRequest q={0}; q.op='J'; submit(s,&q);
    } else if(!strcmp(line,"BOARDS") || !strncmp(line,"BOARDS ",7) || !strncmp(line,"BOARD ",6)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        BoardRequest q={0}; q.op=!strncmp(line,"BOARDS",6)?'b':'j'; q.number=1;
        const char *arg=q.op=='b'?(line[6]?line+7:NULL):line+6;
        if(arg) { const char *tail; if(!number_arg(arg,&q.number,&tail)||*tail) { emit(s,"ERR board number\r\n"); return; } }
        submit(s,&q);
    } else if(!strcmp(line,"SEEN") || !strncmp(line,"SEEN ",5) || !strcmp(line,"UNSEEN") || !strncmp(line,"UNSEEN ",7) ||
              !strcmp(line,"UNREAD") || !strncmp(line,"UNREAD ",7) || !strcmp(line,"NEW") || !strncmp(line,"NEW ",4)) {
        if(!s->user) { emit(s,"ERR login required\r\n"); return; }
        BoardRequest q={0}; const char *arg=NULL;
        if(!strncmp(line,"UNREAD",6)) { q.op='v'; if(line[6]) arg=line+7; }
        else if(!strncmp(line,"NEW",3)) { q.op='n'; if(line[3]) arg=line+4; }
        else if(!strncmp(line,"UNSEEN",6)) { q.op='u'; q.number=s->current; if(line[6]) arg=line+7; }
        else { q.op='s'; q.number=s->current; if(line[4]) arg=line+5; }
        if(arg) {
            const char *tail;
            if(q.op=='v' && arg[0]=='0' && (!arg[1] || arg[1]==' ')) { q.number=0; tail=arg+1; }
            else if(!number_arg(arg,&q.number,&tail)) { emit(s,"ERR article number\r\n"); return; }
            if(*tail && (q.op!='v' || *tail!=' ' || !number_arg(tail+1,&q.cursor,&tail) || *tail)) { emit(s,"ERR article number\r\n"); return; }
        }
        submit(s,&q);
    } else if(!strcmp(line,"HELP") || !strncmp(line,"HELP ",5)) { show_help(s,line[4]?line+5:""); }
    else if(!strcmp(line,"PARENT") || !strcmp(line,"CHILD") || !strcmp(line,"NEXT") || !strcmp(line,"PREV") ||
            !strcmp(line,"NOTES") || !strncmp(line,"NOTES ",6) || !strcmp(line,"TREE") || !strncmp(line,"TREE ",5)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        BoardRequest q={0}; q.number=s->current;
        if(!strncmp(line,"NOTES",5)) {
            q.op='O'; q.number=1;
            const char *arg=line[5]?line+6:NULL;
            if(arg && (!strcmp(arg,"UNREAD") || !strncmp(arg,"UNREAD ",7))) {
                if(!s->user) { emit(s,"ERR login required\r\n"); return; }
                q.notes_unread=1; arg=arg[6]?arg+7:NULL;
            }
            if(arg) { const char *tail; if(!number_arg(arg,&q.number,&tail) || *tail) { emit(s,"ERR article number\r\n"); return; } }
        } else if(!strncmp(line,"TREE",4)) {
            q.op='T';
            if(line[4]) {
                const char *tail;
                if(!number_arg(line+5,&q.number,&tail)) { emit(s,"ERR tree syntax\r\n"); return; }
                if(*tail && (*tail!=' ' || !number_arg(tail+1,&q.cursor,&tail) || *tail)) { emit(s,"ERR tree syntax\r\n"); return; }
            }
        } else q.op=!strcmp(line,"PARENT")?'U':!strcmp(line,"CHILD")?'C':!strcmp(line,"NEXT")?'N':'B';
        submit(s,&q);
    }
    else if(!strncmp(line,"POST ",5) || !strncmp(line,"REPLY ",6) || !strncmp(line,"RESPOND ",8)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        memset(&s->draft,0,sizeof(s->draft)); s->draft_revision=0; undo_clear(s);
        const char *title=line+5;
        if(!strncmp(line,"RESPOND ",8)) {
            if(!s->current) { emit(s,"ERR no current article\r\n"); return; }
            s->draft.number=s->current; title=line+8;
        }
        if(!strncmp(line,"REPLY ",6)) {
            if(!number_arg(line+6,&s->draft.number,&title) || *title!=' ') { emit(s,"ERR reply syntax\r\n"); return; }
            title++;
        }
        if(!*title || strlen(title)>BOARD_TITLE || !safe_text(title)) { emit(s,"ERR title 1..80 bytes, no controls\r\n"); return; }
        strcpy(s->draft.title,title); s->draft.op='e'; submit(s,&s->draft);
    } else if(!strcmp(line,"LIST") || !strncmp(line,"LIST ",5) || !strncmp(line,"READ ",5)) {
        if(!s->board_enabled) { emit(s,"ERR no board\r\n"); return; }
        BoardRequest q={0}; q.op=line[0]=='L'?'L':'R'; q.number=1;
        if(strcmp(line,"LIST")) {
            const char *tail;
            if(!number_arg(line+5,&q.number,&tail) || *tail) { emit(s,"ERR article number\r\n"); return; }
        }
        submit(s,&q);
    } else if(!strcmp(line,"PING")) emit(s,"PONG\r\n");
    else if(!strcmp(line,"WHO")) {
        unsigned n=s->io.online?s->io.online(s->io.context):1;
        snprintf(out,sizeof(out),"WHO %u\r\n",n); emit(s,out);
    } else if(!strncmp(line,"ECHO ",5)) { emit(s,line+5); emit(s,"\r\n"); }
    else if(!strncmp(line,"WAIT ",5)) {
        char *end; long n=strtol(line+5,&end,10);
        if(end==line+5 || *end || n<1 || n>5000) emit(s,"ERR WAIT 1..5000\r\n");
        else { s->wake=s->now+(uint64_t)n; emit(s,"WAITING\r\n"); }
    } else if(!strcmp(line,"QUIT")) {
        if(s->user && s->exit_after_save!=3) { s->exit_after_save=2;command_core(s,"PREFS SAVE");if(!s->pending)s->exit_after_save=0;return; }
        s->exit_after_save=0;if(s->ui.enabled)queue_text(s,site_text(1));emit(s,"BYE\r\n");s->closing=1;
    }
    else emit(s,"ERR command\r\n");
}
#include "classic_commands.inc"

static void command(Session *s,char *line) {
    if(s->chatting) { chat_input(s,line);return; }
    char mapped[INPUT_LINE_MAX+128]; const char *action=NULL;
    if(s->page[0]) {
        if(s->scanning && (!strcmp(line,"F") || !strcmp(line,"f"))) { queue_text(s,s->page+s->page_offset);page_clear(s);s->auto_read=1;return; }
        if(!*line) page_next(s);
        else if(!strcmp(line,"Q") || !strcmp(line,"q") || !strcmp(line,"/CANCEL")) { int help=s->help_page; s->scanning=0;if(!help)s->checking=0; page_clear(s); if(help) queue_text(s,ui_text(&s->ui,MSG_HELP_CLOSED)); else emit(s,s->editing?"PREVIEW CLOSED\r\n":"CANCELLED\r\n"); }
        else if(!strcmp(line,"PAGER OFF") || !strcmp(line,"OFF")) { s->pager_enabled=0; queue_text(s,s->page+s->page_offset); page_clear(s); emit(s,"PAGER OFF\r\n"); }
        else queue_text(s,ui_text(&s->ui,MSG_PAGER_MORE));
        prompt(s); return;
    }
    if(s->gather_mode) { gather_input(s,line);prompt(s);return; }
    if(s->scanning) {
        if(!strcmp(line,"P") || !strcmp(line,"L")) {
            s->auto_read=0;
            uint32_t id=s->scan.op=='m'?s->mail_current:s->current;
            s->scan.cursor=id;
            if(*line=='P') { s->scan.descending=!s->scan.descending;s->scan.cursor=s->scan.descending?(id>1?id-1:0):(id<CONTENT_ID_MAX?id+1:0); }
            if(s->scan.cursor)submit(s,&s->scan);else emit(s,"SCAN END\r\n");
            return;
        }
        if(!strcmp(line,"F")) { s->auto_read=1;return; }
        if(s->auto_read) { s->auto_read=0;page_clear(s);if(!*line || !strcmp(line,"STOP")) { s->scanning=0;prompt(s);return; } }
        if(*line=='+' || *line=='-') {
            uint32_t distance=1;const char *tail;
            if(line[1] && (!number_arg(line+1,&distance,&tail) || *tail || !distance)) { emit(s,"ERR relative distance\r\n");prompt(s);return; }
            s->scan.descending=*line=='-';uint32_t id=s->scan.op=='m'?s->mail_current:s->current;
            s->scan.cursor=s->scan.descending?(id>distance?id-distance:0):distance<=CONTENT_ID_MAX-id?id+distance:0;
            if(s->scan.cursor)submit(s,&s->scan);else { s->scanning=0;emit(s,s->scan.op=='m'?"MAIL SCAN END\r\n":"SCAN END\r\n");prompt(s); }return;
        }
        if(!*line || !strcmp(line,"CONTINUE")) { if(s->scan.cursor)submit(s,&s->scan);else { s->scanning=0;emit(s,s->scan.op=='m'?"MAIL SCAN END\r\n":"SCAN END\r\n");prompt(s); }return; }
        s->scanning=0;
        if(!strcmp(line,"0") || !strcmp(line,"Q") || !strcmp(line,"q") || !strcmp(line,"STOP") || !strcmp(line,"/CANCEL")) {
            queue_text(s,ui_text(&s->ui,MSG_SCAN_STOP)); prompt(s); return;
        }
    }
    if(s->password_mode || s->editing || s->application_step) { command_core(s,line); prompt(s); return; }
    if(!s->ui.input && !s->classic_input && !s->mail_input && !s->mail_delete && (!strcmp(line,"LANG") || !strncmp(line,"LANG ",5))) {
        if(line[4] && !language_parse(line+5,&s->ui.language)) queue_text(s,ui_text(&s->ui,MSG_ERR_LANGUAGE));
        else {
            snprintf(mapped,sizeof(mapped),ui_text(&s->ui,MSG_LANGUAGE),language_code(s->ui.language));
            queue_text(s,mapped);
        }
        prompt(s); return;
    }
    if(classic_commands(s,line))return;
    if(!strcmp(line,"+") && *s->report) {
        if(!s->report_next) { queue_text(s,ui_text(&s->ui,MSG_NO_PAGE));prompt(s);return; }
        snprintf(mapped,sizeof(mapped),"%s %u",s->report,s->report_next);command_core(s,mapped);prompt(s);return;
    }
    s->report[0]=0;s->report_next=0;
    if(!s->ui.input && !strcmp(line,"VAULT")) {
        if(!s->user) { emit(s,"ERR login required\r\n");prompt(s);return; }
        if(!board_feature_enabled("vault")) { emit(s,"ERR feature disabled\r\n");prompt(s);return; }
        s->vault_mode=1;s->mail_mode=0;command_core(s,"FILES");prompt(s);return;
    }
    if(s->vault_mode) {
        if(!strcmp(line,"CD .."))line="0";
        if(!strcmp(line,"MAIL") || !strncmp(line,"MAIL ",5))s->vault_mode=0;
        if(!strcmp(line,"0") && *s->vault_path && strcmp(s->vault_path,"-")) { char parent[49];strcpy(parent,s->vault_path);char *slash=strrchr(parent,'/');if(slash)*slash=0;else parent[0]=0;snprintf(mapped,sizeof(mapped),"FILEBOARD %s",*parent?parent:"-");command_core(s,mapped);prompt(s);return; }
        if(!strcmp(line,"0") || !strcmp(line,"EXIT")) { s->vault_mode=0;s->ui.view=UI_HOME;show_menu(s,1);prompt(s);return; }
        if(!strcmp(line,"H") || !strcmp(line,"?")) { show_help(s,"FILES");prompt(s);return; }
        if(!strcmp(line,"L")) { s->vault_child_view=0;snprintf(mapped,sizeof(mapped),"FILESIN %s",*s->vault_path?s->vault_path:"-"); }
        else if(!strncmp(line,"UIN ",4))snprintf(mapped,sizeof(mapped),"ZUPLOADIN %s",line+4);
        else if(!strcmp(line,"BOARDS") || !strcmp(line,"DIR"))snprintf(mapped,sizeof(mapped),"FILECHILDREN %s",*s->vault_path?s->vault_path:"-");
        else if(!strcmp(line,"U") || !strncmp(line,"U ",2))snprintf(mapped,sizeof(mapped),"UPLOAD %s|%s",*s->vault_path?s->vault_path:"-",line+2);
        else if(!strncmp(line,"D ",2))snprintf(mapped,sizeof(mapped),"DOWNLOAD %s",line+2);
        else if(!strncmp(line,"N ",2))snprintf(mapped,sizeof(mapped),"FILENEWS %s|%s",*s->vault_path?s->vault_path:"-",line+2);
        else if(!strncmp(line,"S ",2))snprintf(mapped,sizeof(mapped),"FILESEARCH %s|%s",*s->vault_path?s->vault_path:"-",line+2);
        else if(!strcmp(line,"C"))snprintf(mapped,sizeof(mapped),"FILECATEGORIES");
        else if(!strncmp(line,"I ",2))snprintf(mapped,sizeof(mapped),"FILEBOARD %s",line+2);
        else if(!strncmp(line,"CD ",3))snprintf(mapped,sizeof(mapped),"FILEBOARD %s%s%s",*s->vault_path && strcmp(s->vault_path,"-")?s->vault_path:"",*s->vault_path && strcmp(s->vault_path,"-")?"/":"",line+3);
        else if(!strcmp(line,"+")) {
            if(s->vault_child_view) { if(!s->vault_child_next) { emit(s,"END CHILDREN\r\n");prompt(s);return; }BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_CHILDREN;q.number=s->vault_child_next;strcpy(q.body,*s->vault_path?s->vault_path:"-");submit(s,&q);return; }
            if(!s->ui.next) { queue_text(s,ui_text(&s->ui,MSG_NO_PAGE));prompt(s);return; }
            if(s->ui.view==UI_FILES && s->ui.file_mode)snprintf(mapped,sizeof(mapped),s->ui.file_mode==3?"FILENEWSFROM %u %s":s->ui.file_mode==1?"FILESEARCHFROM %u %s":"FILESINFROM %u %s",s->ui.next,s->ui.file_query);
            else snprintf(mapped,sizeof(mapped),s->ui.view==UI_FILEBOARDS?"FILEBOARDS %u":s->ui.view==UI_FILECATEGORIES?"FILECATEGORIES %u":"FILES %u",s->ui.next);
        }
        else if(*line>='0' && *line<='9') { const char *end;uint32_t selected;if(!number_arg(line,&selected,&end) || *end) { emit(s,"ERR file number\r\n");return; }if(s->vault_child_view) { if(selected>s->vault_child_count) { emit(s,"ERR board number\r\n");prompt(s);return; }snprintf(mapped,sizeof(mapped),"FILEBOARD %s",s->vault_children[selected-1]); }else { s->file_selecting=selected;snprintf(mapped,sizeof(mapped),"FILE %u",selected); } }
        else snprintf(mapped,sizeof(mapped),"%s",line);
        command_core(s,mapped);prompt(s);return;
    }
    if(s->mail_mode || !strcmp(line,"MAIL") || !strncmp(line,"MAIL ",5)) { command_core(s,line);prompt(s);return; }
    if(!strcmp(line,"MENU")) {
        s->classic_area=CM_MAIN;
        Language language=s->ui.language;
        int admin=s->ui.admin,sigop=s->ui.sigop;memset(&s->ui,0,sizeof(s->ui));s->ui.admin=admin;s->ui.sigop=sigop; s->ui.language=language; s->ui.enabled=1;
        show_menu(s,1); prompt(s); return;
    }
    if(!s->ui.enabled) { if(!strcmp(line,"RAW")) queue_text(s,"RAW\r\n"); else command_core(s,line); return; }
    if(s->ui.input) {
        int input=s->ui.input; s->ui.input=0;
        if(!strcmp(line,"/CANCEL")) emit(s,"CANCELLED\r\n");
        else {
            snprintf(mapped,sizeof(mapped),"%s %s",input==UI_LOGIN?"LOGIN":input==UI_REGISTER?"REGISTER":input==UI_POST?"POST":input==UI_SEARCH_INPUT?"SEARCH":"RESPOND",line);
            command_core(s,mapped);
        }
        prompt(s); return;
    }
    if(!strcmp(line,"RAW")) { Language language=s->ui.language; int admin=s->ui.admin,sigop=s->ui.sigop;memset(&s->ui,0,sizeof(s->ui));s->ui.admin=admin;s->ui.sigop=sigop; s->ui.language=language; queue_text(s,"\r\nRAW\r\n"); return; }
    if(!strcmp(line,"?")) {
        s->ui.view=UI_HOME; s->ui.next=0; show_menu(s,1); prompt(s); return;
    }
    if(strlen(line)==1) {
        char key=line[0]; if(key>='a' && key<='z') key-=32;
        switch(key) {
        case 'E': action="FILES"; break;
        case 'H': show_help(s,""); prompt(s); return;
        case 'V':
            if(s->ui.view==UI_AUDIT) { audit_command(&s->ui,0,mapped,sizeof(mapped)); action=mapped; }
            else action="AUDIT";
            break;
        case 'K': action="NOTES UNREAD"; break;
        case 'J': action="NEXTBOARD"; break;
        case 'B': action="BOARDS"; break; case 'N': action="NOTES"; break;
        case 'U': action="UNREAD"; break; case 'F': action="NEW"; break;
        case 'P': action="PARENT"; break; case 'C': action="CHILD"; break;
        case '>': action="NEXT"; break; case '<': action="PREV"; break;
        case 'T': action="TREE"; break; case 'S': action="SEEN"; break;
        case 'A': action="SEEN"; s->ui.next_after_seen=1; break;
        case 'I': action="ACCOUNT"; break; case 'O': action="LOGOUT"; break;
        case 'Q': action="QUIT"; break;
        case 'X': s->ui.input=UI_SEARCH_INPUT; prompt(s); return;
        case 'L': case 'G':
            if(key=='G' && s->user && s->ui.view==UI_ARTICLE && s->current) { snprintf(mapped,sizeof(mapped),"GET %u",s->current);action=mapped;break; }
            if(s->user) emit(s,"ERR logout first\r\n");
            else if(!s->member_mode) emit(s,"ERR members disabled\r\n");
            else if(key=='G' && members_auto_ids()) { command_core(s,"REGISTER auto");return; }
            else s->ui.input=key=='L'?UI_LOGIN:UI_REGISTER;
            prompt(s); return;
        case 'W': case 'R':
            if(key=='R' && !s->current) emit(s,"ERR no current article\r\n");
            else if(s->member_mode && !s->user) { BoardRequest q={0};q.op='e';q.action='?';q.number=key=='R'?s->current:0;submit(s,&q); }
            else s->ui.input=key=='W'?UI_POST:UI_REPLY;
            prompt(s); return;
        case '+':
            if(!s->ui.next) { queue_text(s,ui_text(&s->ui,MSG_NO_PAGE)); prompt(s); return; }
            if(s->ui.view==UI_AUDIT) audit_command(&s->ui,s->ui.next,mapped,sizeof(mapped));
            else if(s->ui.view==UI_FILES) {
                if(s->ui.file_mode)snprintf(mapped,sizeof(mapped),"%s %u %s",s->ui.file_mode==3?"FILENEWSFROM":s->ui.file_mode==1?"FILESEARCHFROM":"FILESINFROM",s->ui.next,s->ui.file_query);
                else snprintf(mapped,sizeof(mapped),"FILES %u",s->ui.next);
            }
            else if(s->ui.view==UI_FILEAUDIT || s->ui.view==UI_FILEBOARDS || s->ui.view==UI_FILECATEGORIES)snprintf(mapped,sizeof(mapped),"%s %u",s->ui.view==UI_FILEAUDIT?"FILEAUDIT":s->ui.view==UI_FILEBOARDS?"FILEBOARDS":"FILECATEGORIES",s->ui.next);
            else if(s->ui.view==UI_SEARCH) snprintf(mapped,sizeof(mapped),"SEARCHFROM %u %s",s->ui.next,s->ui.search);
            else if(s->ui.view==UI_FIND || s->ui.view==UI_NEWS) snprintf(mapped,sizeof(mapped),"%s %u %s",s->ui.view==UI_NEWS?"NEWSFROM":"FINDFROM",s->ui.next,s->ui.find_query);
            else if(s->ui.view==UI_PROFILES) snprintf(mapped,sizeof(mapped),"PROFILESFROM %u %s",s->ui.next,s->ui.profile_query);
            else if(s->ui.view==UI_TREE) snprintf(mapped,sizeof(mapped),"TREE %u %u",s->ui.tree,s->ui.next);
            else if(s->ui.view==UI_UNREAD) snprintf(mapped,sizeof(mapped),"UNREAD %u %u",s->ui.unread_root,s->ui.next);
            else snprintf(mapped,sizeof(mapped),"%s %u",s->ui.view==UI_BOARDS?"BOARDS":s->ui.view==UI_NOTES?(s->ui.notes_unread?"NOTES UNREAD":"NOTES"):s->ui.view==UI_MEMBERS?"MEMBERS":s->ui.view==UI_AUDIT?"AUDIT":"LIST",s->ui.next);
            action=mapped; break;
        }
    }
    uint32_t id; const char *tail;
    if(!action && s->ui.view==UI_AUDIT && *line>='0' && *line<='9') {
        if(!audit_number(line,&id)) { emit(s,"ERR audit cursor\r\n"); prompt(s); return; }
        audit_command(&s->ui,id,mapped,sizeof(mapped)); action=mapped;
    }
    if(!action && (s->ui.view==UI_FILEAUDIT || s->ui.view==UI_FILEBOARDS || s->ui.view==UI_FILECATEGORIES) && number_arg(line,&id,&tail) && !*tail) {
        queue_text(s,ui_text(&s->ui,s->ui.view==UI_FILEAUDIT?MSG_VAUDIT_HINT:s->ui.view==UI_FILEBOARDS?MSG_VBOARDS_HINT:MSG_VCATEGORIES_HINT));prompt(s);return;
    }
    if(!action && number_arg(line,&id,&tail) && !*tail) {
        snprintf(mapped,sizeof(mapped),"%s %u",s->ui.view==UI_BOARDS?"BOARD":s->ui.view==UI_MEMBERS?"MEMBER":s->ui.view==UI_PROFILES?"PROFILEID":s->ui.view==UI_FILES?"FILE":(s->ui.view==UI_FIND || s->ui.view==UI_NEWS)?"READGLOBAL":"READ",id); action=mapped;
    }
    if(action && action!=mapped) snprintf(mapped,sizeof(mapped),"%s",action);
    command_core(s,action?mapped:line);
    if(!s->pending) s->ui.next_after_seen=0;
    prompt(s);
}

void session_init(Session *s,SessionIO io,unsigned long id,int slot,int boards,int members,int menu,Language language) {
    memset(s,0,sizeof(*s)); s->io=io;s->menu_mode=1;s->check_auto=menu; s->active=1; s->id=id; s->slot=slot;
    s->binary_allowed=1; s->board_enabled=boards; s->member_mode=members; s->board=1; s->ui.language=language;
    char greeting[128]; snprintf(greeting,sizeof(greeting),"RTBBS-PROBE %lu\r\n",id); queue_text(s,greeting);
    session_dimensions(s,80,24);
    if(menu) { s->ui.enabled=1; queue_text(s,banner_text());queue_text(s,login_text(0));queue_text(s,login_text(1));show_menu(s,1); prompt(s); }
}
void session_scan_stop(Session *s) {
    s->auto_read=0;s->scanning=0;s->scan_cancel=s->pending;page_clear(s);
    if(!s->pending) { queue_text(s,ui_text(&s->ui,MSG_SCAN_STOP));prompt(s); }
}
void session_door_finish(Session *s) {
    s->door=0;queue_text(s,s->ui.language==LANGUAGE_JA?"\r\nゲーム終了。BBSへ戻ります。\r\n":"\r\nGame ended. Back to BBS.\r\n");
    if(s->ui.enabled)show_menu(s,0);prompt(s);
}
uint64_t session_ui_signature(const Session *s) { return (uint64_t)s->ui.language | ((uint64_t)s->pager_enabled<<2) | ((uint64_t)s->editor_line<<3) | ((uint64_t)s->columns<<8) | ((uint64_t)s->rows<<24) | ((uint64_t)s->editor_numbers<<32) | ((uint64_t)s->transfer_protocol<<33); }
void session_dispose(Session *s) { s->active=0;chat_disconnect(s->id);OPENSSL_cleanse(s,sizeof(*s)); }
int session_ready(const Session *s) { return s->active && !s->wake && !s->pending && !s->closing && !s->zmodem; }
int session_secret(const Session *s) { return s->password_mode!=0; }
void session_mail_notify(Session *s,uint32_t user,int deny,uint32_t revision) {
    if(!user || s->user!=user || revision<s->mail_notify_revision)return;
    s->mail_notify_revision=revision;s->mail_notify_deny=deny;if(deny)s->mail_arrivals=0;
}
void session_mail_arrived(Session *s,uint32_t recipient) {
    if(!s->mail_notify_deny && s->active && !s->closing && recipient && s->user==recipient && (s->ui.enabled || (s->binary && s->binary_menu)) && s->mail_arrivals<UINT32_MAX)s->mail_arrivals++;
}
void session_announce(Session *s,const char *text) {
    if(!s->active || s->closing || !s->ui.enabled)return;
    size_t n=strlen(s->notices),size=strlen(text);
    if(n+size+3<sizeof(s->notices)) { memcpy(s->notices+n,text,size);strcpy(s->notices+n+size,"\r\n"); }
}
void session_tick(Session *s,uint64_t now) {
    if(!s->connected_at)s->connected_at=now;
    if(s->checking && s->check_auto && s->check_finished && !s->page[0] && !s->pending && !s->closing && (!s->io.drained || s->io.drained(s->io.context))) { extended_command(s,"CHECK DONE");return; }
    if(s->auto_read && s->active && !s->pending) {
        if(s->scanning && s->scan.cursor)submit(s,&s->scan);
        else { s->auto_read=0;prompt(s); }
    }
    s->now=now;
    if(s->editing && s->editor_upload && now>=s->editor_deadline && !s->pending) { s->editor_upload=0;s->editor_input=0;queue_text(s,s->ui.language==LANGUAGE_JA?"受信終了。編集を続けられます。\r\n":"Text received. Continue editing.\r\n");prompt(s); }
    if(*s->notices && !s->door && !s->binary && !s->zmodem && !s->password_mode && !s->pending && !s->page[0]) { queue_text(s,s->notices);s->notices[0]=0; }
    if(!s->door && !s->mail_notify_deny && s->mail_arrivals && s->user && s->ui.enabled && s->active && !s->closing && !s->editing && !s->pending && !s->password_mode && !s->binary && !s->zmodem && !s->wake && !s->page[0] && !s->ui.input && !s->classic_input && !s->mail_input && !s->mail_delete && !s->chatting && !s->scanning) {
        char text[256];snprintf(text,sizeof(text),ui_text(&s->ui,MSG_MAIL_ARRIVED),s->mail_arrivals);s->mail_arrivals=0;
        s->chat_notice=1;queue_text(s,text);s->chat_prompting=1;prompt(s);s->chat_prompting=0;s->chat_notice=0;
    }
    if(s->binary && now>=s->binary_deadline) { session_abort(s);return; }
    if(s->active && s->wake && now>=s->wake) { s->wake=0; emit(s,"AWAKE\r\n"); prompt(s); }
}
static int word_is(const char *p,size_t n,const char *word) {
    if(strlen(word)!=n)return 0;
    for(size_t i=0;i<n;i++) { unsigned c=(unsigned char)p[i];if(c>='a' && c<='z')c-=32;if(c!=(unsigned char)word[i])return 0; }return 1;
}
int session_uppercase(const Session *s,const char *line,size_t size) {
    if(s->memo_input)return 0;
    if(s->classic_input==CI_FILE_ACTION || s->classic_input==CI_BATCH)return 1;
    if(s->application_step || s->classic_input)return 0;
    if(session_secret(s) || s->ui.input || s->mail_input || s->page[0] || s->binary || s->zmodem)return 0;
    if(s->editing || s->chatting || s->gather_mode || s->outside_talk) {
        if(!size || line[0]!='/')return !s->chatting && s->editing && s->editor_line && !s->editor_input && !memchr(line,' ',size);
        if(size>1 && line[1]=='/')return 0;
        const char *space=memchr(line,' ',size);
        if(!space)return 1;
        size_t first=(size_t)(space-line);
        return (word_is(line,first,"/EDITOR") || word_is(line,first,"/LANG") || word_is(line,first,"/NOTIFY")) && !memchr(space+1,' ',size-first-1);
    }
    const char *space=memchr(line,' ',size);if(!space)return 1;
    size_t n=(size_t)(space-line),offset=n+1;
    const char *words[]={"MENUMODE","BOARDGUEST","LANG","HELP","PAGER","PREFS","PROTOCOL","EDITOR","MAIL","BATCH","CHAT","TALK","NOTES","USERLOG","USERTABLE","USERSTATS","FILESTATS","FILEHISTORY","SHUTDOWN","AUDIT","FIND","SCAN","NEWS","NOTIFY","DRAFT","BUFFER","CHECK"};
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);i++)if(word_is(line,n,words[i]))return !memchr(line+offset,' ',size-offset);
    return 0;
}
static char *upper_word(char *p) { for(;*p && *p!=' ';p++)if(*p>='a' && *p<='z')*p-=32;return *p?p+1:p; }
static void normalize_command(Session *s,char *line,size_t n) {
    /* Hidden input remains case-sensitive; only the exact cancellation token folds. */
    if(word_is(line,n,"/CANCEL")) { memcpy(line,"/CANCEL",7);return; }
    if(s->memo_input)return;
    if(s->classic_input==CI_FILE_ACTION || s->classic_input==CI_BATCH) { for(char *p=line;*p;p++)if(*p>='a' && *p<='z')*p-=32;return; }
    if(s->application_step || s->classic_input)return;
    if(session_secret(s))return;
    if(!strncmp(line,"//",2))return;
    for(size_t i=0;i<n;i++)if(session_uppercase(s,line,i) && line[i]>='a' && line[i]<='z')line[i]=(char)(line[i]-32);
    if(s->editing || s->chatting || s->gather_mode || s->outside_talk || s->ui.input || s->mail_input)return;
    if(!strncmp(line,"MAIL SCAN ",10))upper_word(line+10);
    if(!strncmp(line,"FIND ",5) || !strncmp(line,"SCAN ",5)) { char *scope=upper_word(line+5);if(!strncmp(line+5,"ASC ",4) || !strncmp(line+5,"DESC ",5))upper_word(scope); }
    if(!strncmp(line,"FINDFROM ",9)) { char *p=strchr(line+9,' ');if(p)upper_word(p+1); }
    if(!strncmp(line,"MEMBERPROFILE ",14) || !strncmp(line,"APPLICATIONSET ",15)) { char *p=strchr(line+(*line=='M'?14:15),' ');if(p)upper_word(p+1); }
    if(!strncmp(line,"FILEBOARDOPTIONS ",17)) { char *p=strchr(line+17,'|');if(p)upper_word(p+1); }
    if(!strncmp(line,"FILEBOARDPASS ",14)) { char *p=strchr(line+14,' ');if(p)upper_word(p+1); }
    if(!strncmp(line,"MAIL DRAFT ",11) && strlen(line)>=18 && word_is(line+11,6,"DELETE"))memcpy(line+11,"DELETE",6);
    if(!strncmp(line,"MAIL NOTIFY ",12) || !strncmp(line,"MAIL DROP ",10) || (s->mail_mode && !strncmp(line,"DROP ",5))) {
        char *last=strrchr(line,' ');if(last && (word_is(last+1,strlen(last+1),"ON") || word_is(last+1,strlen(last+1),"OFF") || word_is(last+1,strlen(last+1),"CONFIRM")))for(char *p=last+1;*p;p++)if(*p>='a' && *p<='z')*p-=32;
    }
    if(!strncmp(line,"BOARDPASS ",10)) { char *p=strchr(line+10,' ');if(p && strlen(p)>=5 && word_is(p+1,4,"NONE"))memcpy(p+1,"NONE",4); }
    if(!strncmp(line,"PROFILEVIS ",11)) { char *last=strrchr(line,' ');if(last)for(char *p=last+1;*p;p++)if(*p>='a' && *p<='z')*p-=32; }
    /* Confirmation tokens are syntax only on operations that accept them. */
    if(!strncmp(line,"BOARDGUEST ",11)) { char *p=strchr(line+11,' ');if(p)upper_word(p+1); }
    if(!strncmp(line,"BOARDDELETEALL ",15)) { char *p=strchr(line+15,' ');if(p)upper_word(p+1); }
    const char *confirm[]={"AUTHOR ","MOVEBRANCH ","BOARDDELETEALL ","FILEDELETE ","FILEHIDE ","FILERESTORE ","WITHDRAW ","DISCONNECT ","BOARDDELETE ","MOVE ","BOARDPASS ","GROUPMOVE ","BOARDGROUP ","FILEBOARDPASS "};
    for(unsigned i=0;i<sizeof(confirm)/sizeof(confirm[0]);i++)if(!strncmp(line,confirm[i],strlen(confirm[i]))) {
        char *last=strrchr(line,' ');if(last && word_is(last+1,strlen(last+1),"CONFIRM"))memcpy(last+1,"CONFIRM",7);
    }
}
int session_line(Session *s,const char *text,size_t size,uint64_t now) {
    if(!session_ready(s)) return 0;
    if(size>ln_limit("INPUT",INPUT_LINE_MAX) || memchr(text,0,size)) { session_abort(s); return 0; }
    char line[INPUT_LINE_MAX+1]; memcpy(line,text,size); line[size]=0; s->now=now;
    normalize_command(s,line,size);command(s,line); OPENSSL_cleanse(line,sizeof(line)); return 1;
}
void session_result(Session *s,const BoardResult *completion,uint64_t now) {
    if(!s->active || s->closing || s->id!=completion->session || s->slot!=completion->slot) return;
    s->now=now; s->pending=0;
    if(s->scan_cancel && completion->sequence) { s->scan_cancel=0;s->scanning=0;s->auto_read=0;queue_text(s,ui_text(&s->ui,MSG_SCAN_STOP));prompt(s);return; }
    s->scan_cancel=0;
    if(s->exit_after_save) {
        int action=s->exit_after_save;s->exit_after_save=0;
        if(!strncmp(completion->text,"PREFS SAVED",11)) { s->exit_after_save=3;command_core(s,action==1?"LOGOUT":"QUIT");prompt(s);return; }
        emit(s,completion->text);prompt(s);return;
    }
    if(completion->sigop>=0)s->ui.sigop=completion->sigop;
    if(completion->service_action=='E') { s->ui.input=completion->service_number==2?UI_REPLY:UI_POST;prompt(s);return; }
    if(completion->mail_notify_user)session_mail_notify(s,completion->mail_notify_user,completion->mail_notify_deny,completion->mail_notify_revision);
    if(completion->chat_user)chat_apply_preferences(completion->chat_user,&completion->chat_preferences);
    if(completion->revoked) { session_revoke(s); return; }
    if(s->vault_selecting) {
        s->vault_selecting=0;
        if(!strncmp(completion->text,"FILEBOARDSETTING ",17)) { strcpy(s->vault_path,s->vault_select);s->vault_child_view=0;s->vault_child_count=0;s->file_selected=0;s->vault_mode=1;s->mail_mode=0; }
    }
    if(completion->mail_id) { s->mail_current=completion->mail_id;s->mail_reading=completion->mail_action!='S'; }
    if(completion->mail_action=='L' || completion->mail_action=='I')s->mail_reading=0;
    if(completion->service_action=='n') { s->checking=(int)completion->service_number+1;s->check_since=completion->since;s->check_until=completion->until;s->check_cursor=s->checking==1?1:0;s->check_finished=0;check_page(s);return; }
    if(s->check_committing) { s->check_committing=0;if(!strncmp(completion->text,"CHECKPOINT SAVED",16))s->checking=0;else s->check_auto=0;emit(s,completion->text);prompt(s);return; }
    if(s->check_pending) { s->check_pending=0;const char *end=strstr(completion->text,"END NEXT ");unsigned next=0;if(end)sscanf(end,"END NEXT %u",&next);s->check_cursor=next;s->check_finished=end && !next;emit(s,completion->text);queue_text(s,s->check_auto?"CHECK NEXT / CHECK CANCEL (AUTO)\r\n":s->ui.language==LANGUAGE_JA?"CHECK NEXT: 続き / CHECK DONE: 確認日時を保存 / CHECK CANCEL: 中断\r\n":"CHECK NEXT / CHECK DONE / CHECK CANCEL\r\n");prompt(s);return; }
    if(!strncmp(completion->text,"MAILBATCH",9)) { const char *p=completion->text+9;size_t n=0;unsigned count=0;while(*p==' ' && count<16) { const char *tail;uint32_t id;if(!number_arg(++p,&id,&tail))break;n+=(size_t)snprintf(s->zmodem_text+n,sizeof(s->zmodem_text)-n,"%s%u",count++?",":"",id);p=tail; }if(count) { s->zmodem_receive=10;s->zmodem_protocol=s->mail_batch_protocol;s->zmodem_id=0;s->zmodem_start=1;BoardRequest q={0};q.op='@';q.action=1;submit(s,&q); }return; }
    if(!strncmp(completion->text,"FILECHILDREN ",13)) {
        s->vault_child_count=0;s->vault_child_view=1;s->vault_child_next=0;
        const char *row=strstr(completion->text,"\r\n");
        while(row && *row) { row+=2;unsigned n=0;char path[49];
            if(sscanf(row,"CHILD %u %48[^|]",&n,path)==2 && n && n<=16) { strcpy(s->vault_children[n-1],path);s->vault_child_count=n; }
            if(sscanf(row,"END CHILDREN NEXT %u",&s->vault_child_next)==1)break;row=strstr(row,"\r\n");
        }
        queue_text(s,completion->text);prompt(s);return;
    }
    if(s->status_pending) { s->status_pending=0;emit(s,completion->text);if(s->chatting)chat_prompt(s);else prompt(s);return; }
    if(s->profile_walk_pending) {
        s->profile_walk_pending=0;unsigned next=0;const char *end=strstr(completion->text,"END NEXT ");if(end)sscanf(end,"END NEXT %u",&next);
        queue_text(s,completion->text);
        if(next) { s->profile_search.cursor=next;s->classic_input=CI_PROF_NEXT;queue_text(s,s->ui.language==LANGUAGE_JA?"次の会員:Enter / 0:終了 > ":"Next:Enter / 0:stop > "); }
        else { s->profile_walk=0;prompt(s); }return;
    }
    if(s->batch_copying) {
        queue_text(s,completion->text);
        if(!strncmp(completion->text,"FILE COPIED ",12))s->batch_copied[s->batch_copy_at]=1;
        else { s->batch_copying=0;prompt(s);return; }
        while(++s->batch_copy_at<s->batch_count && s->batch_copied[s->batch_copy_at]);
        if(s->batch_copy_at<s->batch_count) { BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_COPY;q.number=s->batch[s->batch_copy_at];strcpy(q.body,s->batch_copy_path);submit(s,&q); }
        else { s->batch_copying=0;prompt(s); }return;
    }
    if(s->report_pending) {
        s->report_pending=0;s->report_next=0;const char *end=strstr(completion->text,"END NEXT ");
        if(end)sscanf(end,"END NEXT %u",&s->report_next);else { end=strstr(completion->text,"\r\nEND TEXT NEXT ");if(end)sscanf(end+2,"END TEXT NEXT %u",&s->report_next);else s->report[0]=0; }
        emit(s,completion->text);prompt(s);return;
    }
    if(!strncmp(completion->text,"FILEBOARD UNLOCKED ",19)) { unsigned id,revision;if(sscanf(completion->text,"FILEBOARD UNLOCKED %u %u",&id,&revision)==2 && id<=1024)s->vault_grants[id]=revision; }
    if(completion->auth) { s->classic_input=0;s->classic_id=0;s->classic_area=CM_MAIN;s->checking=s->check_pending=s->check_finished=0;s->check_since=s->check_until=0;s->check_cursor=0;memset(s->vault_grants,0,sizeof(s->vault_grants)); }
    if(!strncmp(completion->text,"BATCHMATCH",10)) { const char *p=completion->text+10;s->batch_count=0;memset(s->batch_done,0,sizeof(s->batch_done));char ids[192]="",text[384];size_t used=0;
        while(*p==' ' && s->batch_count<16) { const char *tail;uint32_t id;if(!number_arg(++p,&id,&tail))break;s->batch_done[s->batch_count]=0;s->batch_copied[s->batch_count]=0;s->batch[s->batch_count++]=id;p=tail;used+=(size_t)snprintf(ids+used,sizeof(ids)-used,"%s%u",used?",":"",id); }
        snprintf(text,sizeof(text),s->ui.enabled?ui_text(&s->ui,MSG_BATCH_LIST):"BATCH %u %s\r\n",s->batch_count,*ids?ids:"-");queue_text(s,text);prompt(s);return;
    }
    if(!strncmp(completion->text,"FILEBOARD EDIT ",15)) { const char *body=strstr(completion->text,"\r\n");if(body) { memset(&s->draft,0,sizeof(s->draft));s->draft.op='Y';s->draft.action=VAULT_CMD_BOARDGUIDE;size_t n=(size_t)(body-(completion->text+15));if(n<=48) { memcpy(s->draft.title,completion->text+15,n);snprintf(s->draft.body,sizeof(s->draft.body),"%s",body+2);s->editing=1;s->editor_input=0;s->editor_at=1;undo_clear(s);queue_text(s,s->ui.language==LANGUAGE_JA?"ファイルボード案内文を編集します。/SAVE で保存。\r\n":"Edit file-board guide. /SAVE to save.\r\n");prompt(s);return; } } }
    if(s->editing && s->draft.op=='Y' && !strcmp(completion->text,"FILEBOARD UPDATED\r\n")) { s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0;memset(&s->draft,0,sizeof(s->draft)); }
    if(s->gather_pending) {
        int mode=s->gather_pending;s->gather_pending=0;
        if(completion->service_action=='G') {
            queue_text(s,completion->text);s->gather_cursor=completion->service_next;s->gather_mode=mode;
            if(!s->gather_cursor) { if(mode==2)s->gather_mode=0;else { s->gather_lines=0;memset(&s->draft,0,sizeof(s->draft));queue_text(s,ui_text(&s->ui,MSG_GATHER_EDIT)); } }
        } else if(completion->service_action=='g') { s->gather_mode=0;s->gather_lines=0;memset(&s->draft,0,sizeof(s->draft));queue_text(s,completion->text); }
        else emit(s,completion->text);
        prompt(s);return;
    }
    if(s->service_pending=='M') {
        s->service_pending=0;
        if(completion->service_action=='M') { s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0;s->sysop_mail_after=now+30000;memset(&s->draft,0,sizeof(s->draft));queue_text(s,ui_text(&s->ui,MSG_SYSOP_MAIL_SENT)); }
        else { s->editing=1;emit(s,completion->text); }
        prompt(s);return;
    }
    if(s->mail_pending) {
        s->mail_pending=0;
        if(completion->mail_action=='A' && completion->mail_to)s->draft.mail_attachments=completion->mail_attachments;
        if(completion->mail_action=='B' || completion->mail_action=='Q' || completion->mail_action=='O') {
            if(completion->mail_to) {
                memset(&s->draft,0,sizeof(s->draft));s->draft.op='m';s->draft.action='S';s->draft.number=completion->mail_to;s->draft.cursor=completion->mail_parent;s->draft.mail_attachments=completion->mail_attachments;
                strcpy(s->draft.title,completion->draft_title);strcpy(s->draft.body,completion->draft_body);s->mail_revision=completion->mail_revision;s->editing=1;s->editor_input=0;s->editor_at=1;undo_clear(s);
            }
        }
        if(completion->mail_action=='W' && completion->mail_revision) { OPENSSL_cleanse(&s->draft,sizeof(s->draft));s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0;s->mail_revision=0;undo_clear(s); }
        if(completion->mail_action=='S') {
            if(completion->mail_id) { OPENSSL_cleanse(&s->draft,sizeof(s->draft));s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0; }
            else { s->editing=1;s->editor_input=0;s->editor_at=1; }
        }
        if(s->vault_selecting) {
        s->vault_selecting=0;
        if(!strncmp(completion->text,"FILEBOARDSETTING ",17)) { strcpy(s->vault_path,s->vault_select);s->vault_child_view=0;s->vault_child_count=0;s->file_selected=0;s->vault_mode=1;s->mail_mode=0; }
    }
    if(completion->mail_id) { s->mail_current=completion->mail_id;s->mail_reading=completion->mail_action!='S'; }
    if(completion->mail_action=='L' || completion->mail_action=='I')s->mail_reading=0;
        if(completion->mail_action=='L' || completion->mail_action=='I')s->mail_cursor=completion->mail_next;
        if(s->ui.enabled && s->pager_enabled && strchr("ILRNT",completion->mail_action) && completion->mail_action) {
            snprintf(s->page,sizeof(s->page),"%s",completion->text);s->page_offset=0;page_next(s);
        } else if(!completion->mail_action)emit(s,completion->text);
        else queue_text(s,completion->text);
        prompt(s);return;
    }
    if(s->chat_pending) {
        s->chat_pending=0;
        if(!completion->chat_action) {
            if(s->chatting) { s->chatting=0;chat_leave(s->id); }
            emit(s,completion->text);prompt(s);return;
        }
        if(completion->chat_action=='M') {
            int ok=chat_memo(s->id,completion->text);
            queue_text(s,ok?(s->ui.enabled?ui_text(&s->ui,MSG_MEMO_SAVED):"MEMO SAVED\r\n"):"ERR command\r\n");prompt(s);
        } else if(completion->chat_action=='C') {
            int count=chat_call(s->id,completion->text,now);char text[256];
            if(count<0)queue_text(s,s->ui.enabled?ui_text(&s->ui,MSG_SYSOP_RATE):"CALL ERROR rate\r\n");
            else { snprintf(text,sizeof(text),s->ui.enabled?ui_text(&s->ui,MSG_SYSOP_RESULT):"CALL DELIVERED %u\r\n",(unsigned)count);queue_text(s,text); }
            prompt(s);
        } else if(completion->chat_action=='V') {
            s->chatting=1;s->outside_talk=1;s->talk_lost=0;s->talk_target=0;s->talk_select=1;
            page_clear(s);s->scanning=0;
            if(completion->chat_target)talk_start(s,completion->chat_target);else chat_prompt(s);
        } else if(completion->chat_action=='O') { chat_online(s->id);
        } else if(completion->chat_action=='J') {
            s->outside_talk=0;
            s->talk_target=s->reply_target=0;s->talk_select=s->talk_lost=0;
            s->chatting=1;page_clear(s);s->scanning=0;
            queue_text(s,s->ui.enabled?ui_text(&s->ui,MSG_CHAT_ENTER):"CHAT OPEN\r\n");
            if(!chat_join_config(s->id,s->user,completion->chat_login,completion->chat_name,chat_event,s,&completion->chat_preferences)) {
                s->chatting=0;chat_error(s,MSG_CHAT_FULL,"CHAT ERROR full\r\n");prompt(s);
            }
        } else if(s->chatting && completion->chat_action=='K') {
            char text[256];snprintf(text,sizeof(text),s->ui.enabled?ui_text(&s->ui,MSG_CHAT_KICK_DONE):"CHAT KICK %s %u\r\n",completion->text,completion->chat_kick_count);
            queue_text(s,text);chat_prompt(s);
        } else if(s->chatting && completion->chat_action=='!') {
            emit(s,completion->text);chat_prompt(s);
        } else if(s->chatting && completion->chat_action=='P') {
            chat_settings(s->id);
        } else if(s->chatting && completion->chat_action=='E') {
            chat_error(s,MSG_TALK_MUTE_ERROR,"CHAT ERROR mute\r\n");
        } else if(s->chatting && completion->chat_action=='T') {
            if(chat_target(s->id,completion->chat_target)) {
                s->talk_target=completion->chat_target;s->talk_lost=0;
                char text[256];snprintf(text,sizeof(text),s->ui.enabled?ui_text(&s->ui,MSG_TALK_OPEN):"CHAT TALK OPEN %lu\r\n",s->talk_target);queue_text(s,text);chat_prompt(s);
            } else { s->talk_lost=1;chat_error(s,MSG_TALK_INVALID,"CHAT ERROR target\r\n"); }
        } else if(s->chatting && completion->chat_action=='D') {
            int ok=chat_private(s->id,completion->chat_target,completion->text,now);
            if(ok>0 && s->talk_once) { s->talk_once=0;s->talk_target=0;s->talk_select=s->talk_lost=0;if(s->talk_return_editor) { s->talk_return_editor=0;s->outside_talk=0;s->chatting=0; }if(s->chatting)chat_prompt(s);else prompt(s); }
            if(ok==-3)chat_error(s,MSG_TALK_REFUSED,"CHAT ERROR refused\r\n");
            else if(ok==-2) { s->talk_lost=1;chat_error(s,MSG_TALK_GONE,"CHAT ERROR target\r\n"); }
            else if(ok<=0)chat_error(s,ok<0?MSG_CHAT_RATE:MSG_CHAT_INVALID,ok<0?"CHAT ERROR rate\r\n":"CHAT ERROR text\r\n");
        } else if(s->chatting && completion->chat_action=='W')chat_who(s->id);
        else if(s->chatting && completion->chat_action=='S') {
            int ok=chat_say(s->id,completion->text,now);
            if(ok==-4)chat_error(s,MSG_CHAT_STORAGE,"CHAT ERROR storage\r\n");
            else if(ok<=0)chat_error(s,ok<0?MSG_CHAT_RATE:MSG_CHAT_INVALID,ok<0?"CHAT ERROR rate\r\n":"CHAT ERROR text\r\n");
        }
        return;
    }
    if(s->zmodem_start) {
        s->zmodem_start=0;
        if(completion->binary_enter) { s->zmodem=1;page_clear(s);s->scanning=0;if(s->zmodem_protocol)queue_text(s,s->ui.language==LANGUAGE_JA?"選択した方式で端末の転送を開始してください。\r\n":"Start terminal transfer using the selected protocol.\r\n");else queue_text(s,ui_text(&s->ui,(s->zmodem_receive==6 || s->zmodem_receive==9 || s->zmodem_receive==11 || s->zmodem_receive==12)?MSG_ZMODEM_BATCH_UPLOAD_READY:(s->zmodem_receive==2 || s->zmodem_receive==10)?MSG_ZMODEM_BATCH_READY:MSG_ZMODEM_READY)); }
        else { emit(s,completion->text);prompt(s); }
        return;
    }
    if(completion->binary_enter) {
        s->binary_menu=s->ui.enabled;s->ui.enabled=0;page_clear(s);s->binary=1;s->binary_used=0;s->binary_deadline=now+60000;
        queue_text(s,"BINARY READY binary-v1\r\n");return;
    }
    if(s->binary) {
        if(completion->binary_size)binary_frame(s,0,completion->text,completion->binary_size);
        else binary_frame(s,'T',completion->text,strlen(completion->text));
        s->binary_deadline=now+60000;return;
    }
    if(completion->preferences) { s->menu_mode=completion->menu_mode;s->menu_shown=0;s->ui.language=(Language)completion->language; s->pager_enabled=completion->pager;s->editor_line=completion->editor_preference;s->editor_numbers=completion->ui_aux&1;s->transfer_protocol=completion->ui_aux>>1;if(completion->columns && completion->rows)session_dimensions(s,completion->columns,completion->rows); }
    if(completion->auth) { selection_reset(s);memset(s->board_grants,0,sizeof(s->board_grants));OPENSSL_cleanse(s->mail_query,sizeof(s->mail_query));s->mail_filter=0;s->mail_cursor=0;s->mail_notify_deny=0;s->mail_notify_revision=0;s->mail_arrivals=0;chat_disconnect(s->id);s->user=completion->auth>0?(uint32_t)completion->auth:0; s->user_epoch=completion->user_epoch; s->current=0; s->board=1; s->auth_after=now+1000; }
    if(completion->auth>0)snprintf(s->username,sizeof(s->username),"%s",completion->chat_login);
    if(completion->auth>0)session_mail_notify(s,completion->mail_notify_user,completion->mail_notify_deny,completion->mail_notify_revision);
    if(completion->auth>0)s->member_level=(unsigned)completion->member_level;
    if(completion->auth>0) { s->menu_mode=completion->menu_mode;s->menu_shown=0; }
    if(completion->auth>0 && s->ui.enabled) { queue_text(s,login_text(1));time_t greet_now=time(NULL);struct tm *greet_tm=localtime(&greet_now);if(greet_tm)queue_text(s,hourly_greeting((unsigned)greet_tm->tm_hour));if(completion->birthday)queue_text(s,*login_text(2)?login_text(2):s->ui.language==LANGUAGE_JA?"お誕生日おめでとうございます！\r\n":"Happy birthday!\r\n"); }
    if(completion->auth>0 || completion->preferences)s->saved_ui=session_ui_signature(s);
    if(completion->auth>0 && board_feature_enabled("chat"))chat_register(s->id,s->user,completion->chat_login,completion->chat_name,chat_event,s,&completion->chat_preferences);
    if(completion->service_action=='1') { s->board_enter=completion->service_number;s->board_lock=completion->board_grant;s->password_mode='b'; }
    if(completion->service_action=='2') {
        s->auth_after=now; /* A verified ancestor may immediately lead to the next password prompt. */
        if(completion->board_grant>0 && completion->board_grant<=CATALOG_MAX)s->board_grants[completion->board_grant]=completion->board_revision;
        BoardRequest q={0};q.op='j';q.number=completion->service_number;submit(s,&q);return;
    }
    if(completion->service_action=='8') { memset(&s->draft,0,sizeof(s->draft));undo_clear(s);s->draft.op=')';s->draft.action=completion->service_number==1?'B':'C';strcpy(s->draft.body,completion->draft_body);s->editing=1;s->editor_input=0;s->editor_at=1; }
    if(completion->service_action=='9') { size_t have=strlen(s->draft.body),add=strlen(completion->draft_body);if(!s->editing || have+add>BOARD_BODY)emit(s,"ERR body limit 262144 bytes; cancel or finish\r\n");else { undo_save(s);memcpy(s->draft.body+have,completion->draft_body,add+1);emit(s,"BUFFER IMPORTED\r\n"); }prompt(s);return; }
    if(completion->service_action=='0' && completion->service_number) { s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0;memset(&s->draft,0,sizeof(s->draft));undo_clear(s); }
    if(completion->service_action=='3') { s->boardpass_target=completion->service_number;s->password_mode='p'; }
    if(completion->service_action=='4' || completion->service_action=='6') {
        memset(&s->draft,0,sizeof(s->draft));s->draft_revision=0;undo_clear(s);
        s->draft.op=completion->service_action=='4'?'(':'P';s->draft.action='W';s->draft.number=completion->service_action=='4'?completion->service_number:0;
        strcpy(s->draft.body,completion->draft_body);strcpy(s->draft.title,completion->draft_title);s->editing=1;s->editor_input=0;s->editor_at=1;
    }
    if(completion->service_action=='5') { s->editing=!completion->service_number;if(!s->editing) { memset(&s->draft,0,sizeof(s->draft));undo_clear(s); } }
    if(completion->service_action=='p') { s->reset_target=completion->service_number;s->password_mode='p'; }
    if(completion->auth)s->ui.admin=completion->auth>0 && completion->chat_admin;
    if(completion->auth>0)chat_admin(s->id,completion->chat_admin);
    if(completion->auth>0 && s->ui.enabled && s->ui.admin)queue_text(s,ui_text(&s->ui,MSG_MENU_ADMIN));
    if(completion->password_changed) { s->user_epoch=completion->account_epoch; s->auth_after=now+1000; }
    if(completion->board) { s->board=completion->board; s->current=0; if(completion->direct) s->ui.board_title[0]=0; }
    if(completion->draft_loaded) {
        memset(&s->draft,0,sizeof(s->draft)); undo_clear(s);
        s->draft.number=completion->draft_parent; strcpy(s->draft.title,completion->draft_title); strcpy(s->draft.body,completion->draft_body);
        s->classic_area=CM_MAIN;s->ui.next=0; s->ui.view=UI_HOME; s->ui.board_title[0]=0;
    }
    if(completion->draft_revision) s->draft_revision=completion->draft_revision;
    if(completion->editing) { s->editing=1;s->editor_input=0;s->editor_at=1; }
    if(s->file_selecting) { if(!strncmp(completion->text,"FILE ",5)) { s->file_selected=s->file_selecting;s->classic_input=CI_FILE_ACTION; }s->file_selecting=0; }
    if(completion->selected) { s->current=completion->selected;if(!s->mail_mode && !s->vault_mode)s->classic_area=CM_BOARD; }
    if(completion->sequence) { s->scanning=completion->selected || completion->mail_id; s->scan.cursor=completion->sequence_next; }
    else if(s->scanning && !strncmp(completion->text,"ERR ",4)) s->scanning=0;
    emit(s,completion->text);
    if(s->vault_mode && !strncmp(completion->text,"FILEBOARDSETTING ",17) && strstr(completion->text,"OPTIONS MENU ")) { BoardRequest q={0};q.op='Y';q.action=VAULT_CMD_CHILDREN;strcpy(q.body,*s->vault_path?s->vault_path:"-");submit(s,&q);return; }
    if(s->ui.enabled && s->active) {
        if(completion->auth>0) {
            char notice[512];
            snprintf(notice,sizeof(notice),ui_text(&s->ui,MSG_LOGIN_UNREAD),
                     completion->unread_articles_notice,completion->unread_boards_notice);
            queue_text(s,notice);
            if(board_feature_enabled("mail")) { snprintf(notice,sizeof(notice),ui_text(&s->ui,MSG_MAIL_NOTICE),completion->mail_notice);queue_text(s,notice); }
        }
        if(completion->auth>0 && completion->mail_draft_notice) {
            char notice[512];
            snprintf(notice,sizeof(notice),ui_text(&s->ui,MSG_MAIL_DRAFT_NOTICE),completion->mail_draft_notice);
            queue_text(s,notice);
        }
        if(completion->auth>0 && completion->saved_draft_notice) {
            char notice[512];
            snprintf(notice,sizeof(notice),ui_text(&s->ui,MSG_DRAFT_NOTICE),completion->saved_draft_notice);
            queue_text(s,notice);
        }
        int next=s->ui.next_after_seen; s->ui.next_after_seen=0;
        if(next && !strncmp(completion->text,"SEEN ",5)) { char cmd[]="NEW"; command_core(s,cmd); }
        else if(completion->board && !completion->draft_loaded && !completion->direct) { if(completion->service_action=='7') { BoardRequest q={0};q.op='b';q.action='C';q.number=completion->board;submit(s,&q); }else { char cmd[]="NOTES"; command_core(s,cmd); } }
        prompt(s);
    }
}

void session_chat_kick(Session *s) {
    chat_disconnect(s->id);
    if(!s->active || s->closing)return;
    if(s->chatting) { s->chatting=0;chat_leave(s->id); }
    s->chat_pending=0;s->pending=0;s->talk_target=s->reply_target=0;
    queue_text(s,s->ui.enabled?ui_text(&s->ui,MSG_CHAT_KICKED):"CHAT KICKED\r\n");s->closing=1;
}
void session_revoke(Session *s) {
    selection_reset(s);
    s->classic_input=0;s->classic_id=0;s->classic_area=CM_MAIN;s->checking=s->check_pending=s->check_finished=0;s->check_since=s->check_until=0;s->check_cursor=0;memset(s->vault_grants,0,sizeof(s->vault_grants));
    chat_disconnect(s->id);s->mail_mode=s->mail_input=0;s->mail_delete=0;
    if(s->chatting) { s->chatting=0;chat_leave(s->id); }
    if(s->door || s->binary || s->zmodem) { session_abort(s);return; }
    if(!s->active || s->closing) return;
    s->vault_mode=0;s->mail_arrivals=0;s->user=0;s->ui.admin=0;s->ui.sigop=0;s->reset_target=0; s->user_epoch=0; s->current=0; s->board=1; s->password_mode=0;
    page_clear(s);
    s->scanning=0; OPENSSL_cleanse(&s->scan,sizeof(s->scan));
    s->editing=0;s->editor_upload=s->editor_confirm=s->editor_upload_question=0; s->pending=0; s->wake=0; s->ui.input=0;
    OPENSSL_cleanse(s->old_password,sizeof(s->old_password)); OPENSSL_cleanse(s->new_password,sizeof(s->new_password));
    undo_clear(s); s->draft_revision=0;
    OPENSSL_cleanse(&s->draft,sizeof(s->draft));
    emit(s,"ERR session revoked\r\n"); s->closing=1;
}

static uint32_t get_big(const unsigned char *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
int session_binary_byte(Session *s,unsigned char byte,uint64_t now) {
    if(!s->binary || !session_ready(s))return 0;
    if(!s->binary_used) { if(byte!='~') { session_abort(s);return -1; }s->binary_deadline=now+15000; }
    if(s->binary_used>=sizeof(s->binary_buffer)) { session_abort(s);return -1; }
    s->binary_buffer[s->binary_used++]=byte;
    if(s->binary_used==5) {
        s->binary_length=get_big(s->binary_buffer+1);
        if(!s->binary_length || s->binary_length>1024) { session_abort(s);return -1; }
    }
    if(s->binary_used<5 || s->binary_used<5+s->binary_length)return 0;
    unsigned char *p=s->binary_buffer+5;size_t n=s->binary_length;s->binary_used=0;s->binary_deadline=now+60000;
    if(*p=='Q' && n==1) {
        binary_frame(s,'E',"",0);s->binary=0;s->ui.enabled=s->binary_menu;
        queue_text(s,"BINARY CLOSED\r\n");if(s->ui.enabled)show_menu(s,1);prompt(s);return 1;
    }
    BoardRequest q={0};
    if(*p=='L' && n>1 && !memchr(p+1,0,n-1) && !memchr(p+1,'\r',n-1) && !memchr(p+1,'\n',n-1)) {
        memcpy(q.body,p+1,n-1);q.body[n-1]=0;
        if(!transfer_line_command(q.body)) { session_abort(s);return -1; }q.op='$';
    } else if((*p=='R' && n==9) || (*p=='W' && n>9 && n<=9+768)) {
        q.op='^';q.action=*p;q.number=get_big(p+1);q.cursor=get_big(p+5);q.scope=(int)n-9;memcpy(q.body,p+9,n-9);
    } else { session_abort(s);return -1; }
    submit(s,&q);return 1;
}

void session_zmodem_finish(Session *s,int success,unsigned id,uint64_t size,int recovery) {
    s->zmodem=0;s->zmodem_start=0;
    char text[256];
    if(!success && recovery>=2 && recovery<=5) {
        static const char *ja[]={"再開できる中断データがありません。登録先ボードを確認してください。", "再開時の説明文または単一／一括の指定が一致しません。説明文を省略すると保存済みの説明を使います。", "受信プログラムの再開検証記録がないか一致しません。管理者は liner-notes-check-resume を実行してください。", "転送がタイムアウトしました。受信中はデータの増加がない場合に停止します。"};
        static const char *en[]={"No interrupted transfer in this board. Check the original destination.", "Resume description or single/batch mode differs. Omit the description to reuse the saved value.", "Resume receiver verification is missing or stale. Ask the administrator to run liner-notes-check-resume.", "Transfer timed out. Reception stops when file data makes no progress."};
        snprintf(text,sizeof(text),"%s\r\n",(s->ui.language==LANGUAGE_JA?ja:en)[recovery-2]);
    }
    else if(s->zmodem_protocol && !recovery) {
        const char *names[]={"ZMODEM","XMODEM","YMODEM","YMODEM-G","M-LINK","TEXT","XMODEM-SUM","XMODEM-1K"};
        if(success)snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"%s 転送完了: %u (%llu バイト)\r\n":"%s transfer completed: %u (%llu bytes)\r\n",names[s->zmodem_protocol],id,(unsigned long long)size);
        else snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"%s 転送は失敗または中断しました。\r\n":"%s transfer failed or cancelled.\r\n",names[s->zmodem_protocol]);
    }
    else if(success && (s->zmodem_receive==11 || s->zmodem_receive==12))snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"メール添付用に %u ファイル（%llu バイト）を受信しました。MAILFILES で番号を確認できます。\r\n":"Private mail batch ready: %u files (%llu bytes). Use MAILFILES for IDs.\r\n",id,(unsigned long long)size);
    else if(success)snprintf(text,sizeof(text),ui_text(&s->ui,(s->zmodem_receive==6 || s->zmodem_receive==9)?MSG_ZMODEM_BATCH_UPLOADED:(s->zmodem_receive==4 || s->zmodem_receive==8)?MSG_MAIL_UPLOAD_DONE:(s->zmodem_receive==2 || s->zmodem_receive==10)?MSG_ZMODEM_BATCH_SENT:(s->zmodem_receive && s->zmodem_receive!=5)?MSG_ZMODEM_RECEIVED:MSG_ZMODEM_SENT),id,(unsigned long long)size);
    else snprintf(text,sizeof(text),"%s",ui_text(&s->ui,recovery?MSG_ZMODEM_RECOVERY:MSG_ZMODEM_FAILED));
    queue_text(s,text);prompt(s);
}

#define _POSIX_C_SOURCE 200809L
#include "session_mmm_internal.h"
#include "mmm_input.h"
#include "vault_bridge.h"
#include "profile_store.h"
#include "chat.h"
#include "batch_protocol.h"
#include "runtime_limits.h"
#include "date_filter.h"
#include <stdio.h>
#include <string.h>

/* Submenu verbs are case-insensitive while login names, paths and text keep
 * their original bytes. */
static char *verb(char *text) {
    while(*text && *text!=' ' && *text!='\t') {
        if(*text>='A' && *text<='Z')*text=(char)(*text+'a'-'A');
        text++;
    }
    if(*text)*text++=0;
    while(*text==' ' || *text=='\t')text++;
    return text;
}

int mmm_ui_text_valid(const char *text,size_t limit) {
    if(strlen(text)>limit)return 0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)
        if(*p<32 || *p==127)return 0;
    return 1;
}

void mmm_bad_service_command(Session *s) {
    mmm_say(s,"Invalid command or argument. Use ?.\r\n","操作または引数が不正です。? で案内を表示できます。\r\n");
}

void mmm_simple_operation(Session *s,char op,int action,uint32_t number,MmmJob job) {
    OperationRequest *q=mmm_request(s,op);
    if(q) { q->action=action;q->number=number;mmm_submit(s,q,job); }
}

void mmm_mail_list(Session *s,MailListMode mode,uint32_t cursor) {
    OperationRequest *q=mmm_request(s,'m');
    if(q) {
        q->action='L';q->number=(uint32_t)mode;q->cursor=cursor;
        if(mode==MAIL_LIST_SEARCH || mode==MAIL_LIST_NEWS)strcpy(q->title,mmm_view(s)->mail_filter);
        if(mmm_submit(s,q,JOB_MAIL))mmm_state(s)->mail_mode=mode;
    }
}

static int transfer_protocol(Session *s,const char *name) {
    static const char *const names[]={"zmodem","xmodem","ymodem","ymodem-g","mlink","text","xmodem-sum","xmodem-1k"};
    if(*name) {
        unsigned index=0;
        while(index<sizeof(names)/sizeof(*names) && strcmp(names[index],name))index++;
        if(index==sizeof(names)/sizeof(*names))return 0;
        s->connection.transfer_protocol=index;
    }
    char text[48];
    snprintf(text,sizeof(text),"Protocol: %s\r\n",names[s->connection.transfer_protocol]);
    session_impl_queue_text(s,text);return 1;
}

static void transfer(Session *s,int mode,uint32_t id,const char *text,int mail) {
    unsigned protocol=s->connection.transfer_protocol;
    if((mode==7 || mode==12) && protocol) {
        mmm_say(s,"Resume requires ZMODEM.\r\n","再開にはZMODEMを指定してください。\r\n");return;
    }
    if((mode==2 || mode==6 || mode==10 || mode==11) && (protocol==1 || protocol>=5)) {
        mmm_say(s,"This protocol transfers one file at a time.\r\n","この方式は一度に1ファイルを転送します。\r\n");return;
    }
    if(!s->connection.zmodem_available || !s->connection.binary_allowed) {
        session_impl_queue_text(s,message_text(mmm_view(s)->language,MSG_ZMODEM_UNAVAILABLE));return;
    }
    OperationRequest *q=mmm_request(s,'@');
    if(!q)return;
    q->action=mail;
    if(!mmm_submit(s,q,JOB_TRANSFER))return;
    s->connection.zmodem_protocol=protocol;s->connection.zmodem_receive=mode;
    s->connection.zmodem_id=id;
    snprintf(s->connection.zmodem_text,sizeof(s->connection.zmodem_text),"%s",text);
    s->connection.zmodem_start=1;
}

static int mail_command(Session *s,const char *name,char *arg) {
    MmmUi *m=mmm_state(s);
    uint32_t id;
    if(!*name || !strcmp(name,"inbox"))mmm_mail_list(s,MAIL_LIST_INBOX,0);
    else if(!strcmp(name,"sent"))mmm_mail_list(s,MAIL_LIST_SENT,0);
    else if(!strcmp(name,"unread"))mmm_mail_list(s,MAIL_LIST_UNREAD,0);
    else if(!strcmp(name,"more") && m->service_next)mmm_mail_list(s,m->mail_mode,m->service_next);
    else if(!strcmp(name,"find") && *arg && mmm_ui_text_valid(arg,80)) {
        strcpy(m->mail_filter,arg);mmm_mail_list(s,MAIL_LIST_SEARCH,0);
    } else if(!strcmp(name,"news")) {
        uint64_t since;
        if(!date_filter(arg,&since))return 0;
        strcpy(m->mail_filter,arg);mmm_mail_list(s,MAIL_LIST_NEWS,0);
    } else if(!strcmp(name,"notify")) {
        unsigned mode=0;
        if(!strcmp(arg,"on"))mode=1;
        else if(!strcmp(arg,"off"))mode=2;
        else if(*arg)return 0;
        mmm_simple_operation(s,'m','U',mode,JOB_MAIL);
    } else if(!strcmp(name,"files")) {
        id=0;
        if(*arg && !mmm_number(arg,&id)) { mmm_bad_service_command(s);return 1; }
        OperationRequest *q=mmm_request(s,'m');
        if(q) { q->action='I';q->cursor=id;mmm_submit(s,q,JOB_MAIL); }
    } else if(!strcmp(name,"resume"))mmm_simple_operation(s,'m','O',0,JOB_MAIL);
    else if(!strcmp(name,"next"))mmm_simple_operation(s,'m','N',0,JOB_MAIL);
    else if(!strcmp(name,"send") && *arg && mmm_ui_text_valid(arg,32)) {
        OperationRequest *q=mmm_request(s,'m');
        if(q) { q->action='B';strcpy(q->username,arg);mmm_submit(s,q,JOB_MAIL); }
    } else if(!strcmp(name,"reply") && mmm_number(arg,&id))mmm_simple_operation(s,'m','Q',id,JOB_MAIL);
    else if((!strcmp(name,"read") && mmm_number(arg,&id)) || (!*arg && mmm_number(name,&id)))
        mmm_simple_operation(s,'m','R',id,JOB_MAIL);
    else if(!strcmp(name,"save") && mmm_number(arg,&id))mmm_simple_operation(s,'m','R',id,JOB_PFILE_MAIL);
    else if(!strcmp(name,"get") && mmm_number(arg,&id))transfer(s,5,id,"",1);
    else if(!strcmp(name,"getall") && mmm_number(arg,&id))mmm_simple_operation(s,'}','B',id,JOB_MAIL_BATCH);
    else if(!strcmp(name,"upload") && mmm_ui_text_valid(arg,240))transfer(s,4,0,arg,1);
    else if((!strcmp(name,"uploadbatch") || !strcmp(name,"resumebatch")) && mmm_ui_text_valid(arg,80))
        transfer(s,name[0]=='r'?12:11,0,arg,1);
    else if(!strcmp(name,"delete") || !strcmp(name,"killmail")) {
        char *confirm=verb(arg);
        if(!mmm_number(arg,&id) || strcmp(confirm,"confirm"))mmm_bad_service_command(s);
        else mmm_simple_operation(s,'m',name[0]=='k'?'K':'D',id,JOB_MAIL);
    } else return 0;
    return 1;
}

static void file_page(Session *s,uint32_t cursor) {
    const MmmUi *m=mmm_view(s);
    OperationRequest *q=mmm_request(s,'Y');
    if(!q)return;
    const char *path=*m->file_path?m->file_path:"-";
    q->action=m->file_action;q->number=cursor;
    if(q->action==VAULT_CMD_SEARCH)snprintf(q->body,sizeof(q->body),"%s|%s",path,m->file_filter);
    else if(q->action==VAULT_CMD_IN || q->action==VAULT_CMD_CHILDREN)strcpy(q->title,path);
    else if(q->action==VAULT_CMD_TEXT) {
        q->number=m->file_text_id;snprintf(q->title,sizeof(q->title),"%u",cursor);
    }
    mmm_submit(s,q,JOB_FILES);
}

void mmm_file_enter(Session *s,const char *path) {
    MmmUi *m=mmm_state(s);
    const char *name=!strcmp(path,"-")?"":path;
    if(!mmm_ui_text_valid(name,48) || strchr(name,'|')) { mmm_bad_service_command(s);return; }
    OperationRequest *q=mmm_request(s,'Y');
    if(q) {
        q->action=VAULT_CMD_CHILDREN;
        strcpy(q->title,*name?name:"-");
        if(mmm_submit(s,q,JOB_FILES)) {
            strcpy(m->requested_path,name);m->file_action=VAULT_CMD_CHILDREN;
        }
    }
}

static int filer_command(Session *s,const char *name,char *arg) {
    MmmUi *m=mmm_state(s);
    uint32_t id;
    if(!strcmp(name,"cd"))mmm_file_enter(s,arg);
    else if(!strcmp(name,"list") || !*name) {
        m->file_action=VAULT_CMD_IN;file_page(s,0);
    } else if(!strcmp(name,"dirs"))mmm_file_enter(s,m->file_path);
    else if(!strcmp(name,"more") && m->service_next)file_page(s,m->service_next);
    else if(!strcmp(name,"find") && *arg && mmm_ui_text_valid(arg,80) && !strchr(arg,'|')) {
        strcpy(m->file_filter,arg);m->file_action=VAULT_CMD_SEARCH;file_page(s,0);
    } else if(!strcmp(name,"text") && mmm_number(arg,&id)) {
        m->file_action=VAULT_CMD_TEXT;m->file_text_id=id;file_page(s,0);
    } else if((!strcmp(name,"info") || !strcmp(name,"add")) && mmm_number(arg,&id))
        mmm_simple_operation(s,'Y',VAULT_CMD_INFO,id,
                         !strcmp(name,"add")?JOB_FILE_ADD:JOB_FILES);
    else if(!strcmp(name,"get") && mmm_number(arg,&id))transfer(s,0,id,"",0);
    else if(!strcmp(name,"unlock") && mmm_ui_text_valid(arg,48) && !strchr(arg,'|')) {
        strcpy(m->requested_path,*arg?arg:m->file_path);m->input=INPUT_FILE_PASSWORD;
    } else if((!strcmp(name,"put") || !strcmp(name,"resume") || !strcmp(name,"putbatch")) &&
              mmm_ui_text_valid(arg,240) && (!strchr(arg,'|') ||
              (!strcmp(name,"put") && (s->connection.transfer_protocol==1 || s->connection.transfer_protocol>=5)))) {
        char text[291];
        const char *path=*m->file_path?m->file_path:"-";
        int mode=!strcmp(name,"resume")?7:!strcmp(name,"putbatch")?6:3;
        snprintf(text,sizeof(text),"%s%s%s",path,*arg?"|":"",arg);
        transfer(s,mode,0,text,0);
    } else if(!strcmp(name,"batch")) {
        char text[180];
        BatchIds ids=batch_ids(&m->files,1);
        if(ids.count && batch_format_ids(&ids,text,sizeof(text)))transfer(s,2,0,text,0);
        else mmm_say(s,"No pending files.\r\n","未取得の選択ファイルはありません。\r\n");
    } else if(!strcmp(name,"clear"))batch_reset(&m->files);
    else return 0;
    return 1;
}

void mmm_profile(Session *s,const char *arg) {
    if(!mmm_ui_text_valid(arg,32)) { mmm_bad_service_command(s);return; }
    OperationRequest *q=mmm_request(s,'Z');
    if(q) { q->action=7;strcpy(q->username,arg);mmm_submit(s,q,JOB_PROFILE); }
}

static void profile_search(Session *s,uint32_t cursor) {
    const MmmUi *m=mmm_view(s);
    OperationRequest *q=mmm_request(s,'Z');
    if(q) {
        q->action=10;q->scope=m->profile_scope;q->number=m->profile_group;q->cursor=cursor;
        strcpy(q->title,m->profile_filter);mmm_submit(s,q,JOB_PROFILE);
    }
}

static int term_command(Session *s,char *name,char *arg) {
    MmmUi *m=mmm_state(s);
    if(!strcmp(name,"lang")) {
        if(!strcmp(arg,"ja"))m->language=LANGUAGE_JA;
        else if(!strcmp(arg,"en"))m->language=LANGUAGE_EN;
        else return 0;
    } else if(!strcmp(name,"size")) {
        uint32_t cols,rows;
        char *height=verb(arg);
        if(!mmm_number(arg,&cols) || !mmm_number(height,&rows) || cols<20 || cols>240 || rows<5 || rows>100)
            return 0;
        session_dimensions(s,cols,rows);
    } else if(!strcmp(name,"load"))mmm_simple_operation(s,'V',0,2,JOB_PREF);
    else if(!strcmp(name,"save")) {
        OperationRequest *q=mmm_request(s,'V');
        if(q) {
            q->number=1;q->cursor=(uint32_t)m->pager;q->scope=m->editor;q->ui_aux=m->ui_aux;
            q->title[0]=m->language==LANGUAGE_EN?'1':'0';
            q->audit_article=s->connection.columns;q->audit_actor=s->connection.rows;
            mmm_submit(s,q,JOB_PREF);
        }
    } else if(!strcmp(name,"profile"))mmm_profile(s,arg);
    else if(!strcmp(name,"more") && !*arg && m->profile_next)profile_search(s,m->profile_next);
    else if(!strcmp(name,"find") && *arg && mmm_ui_text_valid(arg,80)) {
        strcpy(m->profile_filter,arg);m->profile_scope=0;profile_search(s,0);
    } else if(!strcmp(name,"ingroup")) {
        uint32_t group;
        if(!mmm_number(arg,&group))return 0;
        m->profile_scope=100;m->profile_group=group;m->profile_filter[0]=0;profile_search(s,0);
    } else if(!strcmp(name,"set") || !strcmp(name,"privacy") || !strcmp(name,"findfield")) {
        char *value=verb(arg);
        for(char *p=arg;*p;p++)if(*p>='a' && *p<='z')*p=(char)(*p-'a'+'A');
        int field=profile_field(arg);
        if(field<0 || !mmm_ui_text_valid(value,320))return 0;
        if(!strcmp(name,"findfield")) {
            if(!*value || strlen(value)>80)return 0;
            m->profile_scope=(unsigned)field+1;strcpy(m->profile_filter,value);profile_search(s,0);return 1;
        }
        int visibility=-1;
        if(!strcmp(name,"privacy")) {
            if(!strcmp(value,"private"))visibility=0;
            else if(!strcmp(value,"members"))visibility=1;
            else if(!strcmp(value,"monthday"))visibility=2;
            else return 0;
        }
        OperationRequest *q=mmm_request(s,')');
        if(q) {
            q->number=(uint32_t)field;
            if(visibility>=0) { q->action='V';q->cursor=(uint32_t)visibility; }
            else strcpy(q->body,value);
            mmm_submit(s,q,JOB_PROFILE);
        }
    } else return 0;
    return 1;
}

void mmm_chat_request(Session *s,int action,unsigned long target,const char *text) {
    OperationRequest *q=mmm_request(s,'&');
    if(q) { q->action=action;q->chat_target=target;strcpy(q->body,text);mmm_submit(s,q,JOB_CHAT); }
}

static void chat_line(Session *s,char *text) {
    if(!strcmp(text,"/leave") || !strcmp(text,".") || !strcmp(text,"/")) {
        mmm_state(s)->chatting=0;mmm_state(s)->screen=MAIN_SCREEN;chat_leave(s->core.id);mmm_menu(s,1);return;
    }
    if(!strcmp(text,"?")) { mmm_menu(s,1);return; }
    if(!strcmp(text,"/who")) { mmm_chat_request(s,'O',0,"");return; }
    if(!strncmp(text,"/wire ",6)) {
        uint32_t id;
        char *body=verb(text+6);
        if(mmm_number(text+6,&id) && chat_text_valid(body))mmm_chat_request(s,'D',id,body);
        else mmm_bad_service_command(s);
    } else if(chat_text_valid(text) && (*text!='/' || !strncmp(text,"/say ",5)))
        mmm_chat_request(s,'S',0,*text=='/'?text+5:text);
    else mmm_bad_service_command(s);
}

void mmm_games(Session *s,uint32_t id) {
    mmm_state(s)->screen=GAMES_SCREEN;
    mmm_simple_operation(s,'}','J',id,JOB_GAME);
}

static void news_page(Session *s,uint32_t cursor,uint32_t board,int skip,MmmJob job) {
    const MmmUi *m=mmm_view(s);
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(q) {
        q->action='S';q->number=board;q->scope=skip;q->cursor=cursor;q->since=m->new_since;q->until=m->new_until;
        q->notes_unread=m->stream_is_board && !m->stream_all && job==JOB_NEW_PAGE;
        mmm_submit(s,q,job);
    }
}

void mmm_next_scan_board(Session *s) {
    MmmUi *m=mmm_state(s);
    m->scan_at++;m->scan_had_match=m->scan_index=0;
}

void mmm_new_page(Session *s,uint32_t cursor) {
    const MmmUi *m=mmm_view(s);
    if(m->stream_is_board) { news_page(s,cursor,m->open_board,0,JOB_NEW_PAGE);return; }
    if(m->scan_custom && m->scan_at>=m->scan_count) {
        ArticlePage empty={0};
        mmm_render_news(s,&empty);
        return;
    }
    if(m->scan_custom && !m->continuous && !m->stream_paused &&
       (m->scan_index || m->scan_modes[m->scan_at]==MMM_SCAN_INDEX)) {
        OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
        if(q) {
            q->board=m->scan_boards[m->scan_at];q->cursor=cursor;q->scope=1;
            mmm_submit(s,q,JOB_NEW_PAGE);
        }
    } else news_page(s,cursor,m->scan_custom?m->scan_boards[m->scan_at]:0,m->scan_custom,JOB_NEW_PAGE);
}

void mmm_open_page(Session *s,uint32_t cursor) {
    news_page(s,cursor,mmm_view(s)->open_board,0,JOB_OPEN_PAGE);
}

void mmm_off(Session *s,int skip) {
    const MmmUi *m=mmm_view(s);
    if(skip || !s->core.user || !m->sequence_read || !m->login_at) { session_core_close(&s->core);return; }
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(q) { q->action='K';q->until=m->login_at;mmm_submit(s,q,JOB_OFF); }
}

void mmm_manage_menu(Session *s) {
    if(!s->core.admin && !s->core.sigop) { mmm_bad_service_command(s);return; }
    mmm_say(s,"info [BOARD] / audit [CURSOR] / hide NOTE / restore NOTE / inspect NOTE / group USER GROUP / ungroup USER GROUP\r\n",
              "info ボード情報 / audit 操作履歴 / hide 非表示 / restore 解除 / inspect 確認 / group・ungroup 会員 グループ\r\n");
    if(s->core.admin)mmm_say(s,
        "members [CURSOR] / member USER / applications [CURSOR] / approve USER / suspend USER / reinstate USER / boardset FIELDS\r\n",
        "members 会員一覧 / member 会員詳細 / applications 申請 / approve 承認 / suspend 停止 / reinstate 解除 / boardset ボード設定\r\n");
}

static int manage_command(Session *s,const char *name,char *arg) {
    char op=0;
    int action=0;
    uint32_t id=0,cursor=0;
    const char *body=NULL;
    if(!s->core.admin && !s->core.sigop)return 0;
    if(!strcmp(name,"info") || !strcmp(name,"audit")) {
        if(*arg && !mmm_number(arg,&id))return 0;
        if(name[0]=='i') { op='!';action='i';if(!id)id=s->core.board; }
        else { op='A';id=id?id:1; }
    } else if(!strcmp(name,"hide") || !strcmp(name,"restore") || !strcmp(name,"inspect")) {
        if(!mmm_number(arg,&id))return 0;
        op=name[0];
    } else if(!strcmp(name,"group") || !strcmp(name,"ungroup")) {
        char *group=verb(arg);
        if(!mmm_number(arg,&id) || !mmm_number(group,&cursor))return 0;
        op='%';action=name[0]=='g'?'G':'D';
    } else if(!s->core.admin)return 0;
    else if(!strcmp(name,"members") || !strcmp(name,"member") || !strcmp(name,"applications")) {
        if(*arg && !mmm_number(arg,&id))return 0;
        if(!strcmp(name,"applications")) { op='%';action='P';cursor=id; }
        else if(!strcmp(name,"members")) { op='l';if(!id)id=1; }
        else { if(!id)return 0;op='t'; }
    } else if(!strcmp(name,"approve") || !strcmp(name,"suspend") || !strcmp(name,"reinstate")) {
        if(!mmm_number(arg,&id))return 0;
        op=name[0]=='a'?'%':name[0]=='s'?'x':'y';
        if(op=='%')action='A';
    } else if(!strcmp(name,"boardset") && *arg && mmm_ui_text_valid(arg,1023)) {
        op='!';action='b';body=arg;
    } else return 0;

    OperationRequest *q=mmm_request(s,op);
    if(q) {
        q->action=action;q->number=id;q->cursor=cursor;
        if(body)strcpy(q->body,body);
        mmm_submit(s,q,JOB_MANAGE);
    }
    return 1;
}

int mmm_service_line(Session *s,char *text) {
    MmmUi *m=mmm_state(s);
    if(m->screen==CHAT_SCREEN) { chat_line(s,text);return 1; }
    if(m->screen<MAIL_SCREEN)return 0;
    if(!strcmp(text,".") || !strcmp(text,"/")) {
        m->continuous=m->stream_paused=m->stream_is_board=0;
        m->screen=MAIN_SCREEN;mmm_menu(s,1);return 1;
    }
    if(!strcmp(text,"?")) { mmm_menu(s,1);return 1; }
    char *arg=verb(text);
    uint32_t id;
    int handled=0;
    if((m->screen==MAIL_SCREEN || m->screen==FILER_SCREEN) && !strcmp(text,"protocol")) {
        if(!transfer_protocol(s,arg))mmm_bad_service_command(s);
        return 1;
    }
    switch(m->screen) {
    case MAIL_SCREEN: handled=mail_command(s,text,arg);break;
    case FILER_SCREEN: handled=filer_command(s,text,arg);break;
    case TERM_SCREEN: handled=term_command(s,text,arg);break;
    case MANAGE_SCREEN: handled=manage_command(s,text,arg);break;
    case GAMES_SCREEN:
        if(!*text || !strcmp(text,"list")) { mmm_games(s,0);handled=1; }
        else if((!*arg && mmm_number(text,&id)) || (!strcmp(text,"play") && mmm_number(arg,&id))) {
            mmm_games(s,id);handled=1;
        }
        break;
    case NEW_SCREEN:
        if(!strcmp(text,"c") && !*arg && m->stream_paused) {
            m->stream_paused=0;m->continuous=1;handled=1;
        } else if(!strcmp(text,"more") && m->service_next) { mmm_new_page(s,m->service_next);handled=1; }
        else if(!*arg && mmm_number(text,&id) && id<=m->row_count) {
            mmm_simple_operation(s,'Z',5,m->rows[id-1],JOB_NEW_READ);handled=1;
        }
        break;
    default: break;
    }
    if(!handled)mmm_bad_service_command(s);
    return 1;
}

void mmm_mail_result(Session *s,const OperationResult *r,SessionResultView projection) {
    MmmUi *m=mmm_state(s);
    const OperationPage *page=projection.page;
    if(r->mail_action=='B' || r->mail_action=='Q' || r->mail_action=='O') {
        int resume=r->mail_action=='O';
        if(!r->mail_to) { mmm_say(s,"No saved draft.\r\n","保存済みの下書きはありません。\r\n");return; }
        mmm_clear_composition(m);
        m->mail_to=r->mail_to;m->mail_parent=r->mail_parent;
        if(resume) {
            m->mail_revision=r->mail_revision;m->attachments=r->mail_attachments;
            strcpy(m->title,r->draft_title);strcpy(m->body,r->draft_body);
            m->input=INPUT_EDITOR;
        } else {
            m->input=INPUT_TITLE;
            if(*r->draft_title) {
                char text[128];snprintf(text,sizeof(text),"Suggested title: %s\r\n",r->draft_title);
                session_impl_queue_text(s,text);
            }
        }
    } else if(page && page->kind==OPERATION_PAGE_MAIL) {
        const MailPage *mail=&page->data.mail;
        m->service_next=mail->next;
        if(!mail->count)mmm_say(s,"No messages.\r\n","メールはありません。\r\n");
        for(unsigned i=0;i<mail->count;i++) {
            const MailSummary *item=&mail->rows[i];
            char text[192];
            snprintf(text,sizeof(text),"#%u %s %s -> %s  %s\r\n",item->id,item->read?" ":"*",item->from,item->to,item->title);
            session_impl_queue_text(s,text);
        }
    } else if(projection.content && projection.content->kind==2) {
        const OperationContent *item=projection.content;
        char text[192];snprintf(text,sizeof(text),"Mail #%u  %s -> %s\r\n%s\r\n",
                                item->id,item->author,item->recipient_name,item->title);
        session_impl_queue_text(s,text);session_impl_queue_text(s,projection.body);
        for(unsigned i=0;i<r->mail_attachments.count;i++) {
            snprintf(text,sizeof(text),"Attachment #%u\r\n",r->mail_attachments.ids[i]);session_impl_queue_text(s,text);
        }
    } else if(r->mail_action=='S') {
        mmm_clear_composition(m);m->input=INPUT_NONE;
        mmm_say(s,"Mail sent.\r\n","メールを送信しました。\r\n");
    } else if(r->mail_action=='W') {
        m->mail_revision=r->mail_revision;
        mmm_say(s,"Mail draft saved.\r\n","メールの下書きを保存しました。\r\n");
    } else if(r->mail_action=='A') {
        m->attachments=r->mail_attachments;
        mmm_say(s,"Attachment selection updated.\r\n","添付の選択を更新しました。\r\n");
    } else if(r->mail_action=='U') {
        if(r->mail_notify_deny)mmm_say(s,"Mail notifications: off.\r\n","メール通知: OFF\r\n");
        else mmm_say(s,"Mail notifications: on.\r\n","メール通知: ON\r\n");
    } else if(r->mail_action=='K')mmm_say(s,"Unread sent mail retracted.\r\n","送信した未読メールを取り消しました。\r\n");
    else if(r->mail_action=='D')mmm_say(s,"Mail deleted.\r\n","メールを削除しました。\r\n");
    else if(page && page->kind==OPERATION_PAGE_MAIL_FILES) {
        const MailFilePage *files=&page->data.mail_files;
        for(unsigned i=0;i<files->count;i++) {
            const MailFileInfo *file=&files->rows[i];
            char text[176];snprintf(text,sizeof(text),"#%u %u  %s\r\n",file->id,file->size,file->name);
            session_impl_queue_text(s,text);
        }
        if(files->next) {
            char text[48];snprintf(text,sizeof(text),"files %u\r\n",files->next);session_impl_queue_text(s,text);
        }
    } else mmm_say(s,"No unread messages.\r\n","未読メールはありません。\r\n");
}

void mmm_mail_batch_result(Session *s,const OperationFeedback *feedback) {
    if(feedback->mail_batch && feedback->count && feedback->count<=BATCH_CAPACITY) {
        BatchIds files={.count=feedback->count};
        char text[180];
        memcpy(files.ids,feedback->ids,files.count*sizeof(files.ids[0]));
        if(batch_format_ids(&files,text,sizeof(text))) { transfer(s,10,0,text,1);return; }
    }
    mmm_say(s,"No attachments available.\r\n","取得できる添付はありません。\r\n");
}

void mmm_file_result(Session *s,const OperationResult *r,const OperationPage *page,int add) {
    MmmUi *m=mmm_state(s);
    if(!page) {
        if(r->feedback.page_found)m->service_next=r->feedback.page_next;
        session_impl_queue_text(s,r->text);return;
    }
    if(page->kind==OPERATION_PAGE_FILES) {
        const VaultQueryFiles *files=&page->data.files;
        m->service_next=files->next;
        if(!files->count)mmm_say(s,"No matching files.\r\n","該当するファイルはありません。\r\n");
        for(size_t i=0;i<files->count;i++) {
            const VaultEntry *file=&files->rows[i];
            char text[200];snprintf(text,sizeof(text),"#%u %u  %s\r\n",file->id,file->size,file->name);
            session_impl_queue_text(s,text);
        }
    } else if(page->kind==OPERATION_PAGE_FILE_CHILDREN) {
        const VaultQueryChildren *children=&page->data.file_children;
        strcpy(m->file_path,m->requested_path);
        m->service_next=children->next;
        char text[160];snprintf(text,sizeof(text),"[%s]\r\n",*m->file_path?m->file_path:"/");
        session_impl_queue_text(s,text);
        for(size_t i=0;i<children->count;i++) {
            snprintf(text,sizeof(text),"%s  %s\r\n",children->rows[i].name,children->rows[i].display);
            session_impl_queue_text(s,text);
        }
    } else if(page->kind==OPERATION_PAGE_FILE_DETAIL) {
        const VaultEntry *file=&page->data.file_detail.entry;
        if(add) {
            if(batch_add(&m->files,file->id,ln_limit("BATCH_FILES",BATCH_CAPACITY)))
                mmm_say(s,"File selected.\r\n","ファイルを選択しました。\r\n");
            else mmm_say(s,"File selection is full.\r\n","ファイル選択の上限です。\r\n");
        } else {
            char text[540];snprintf(text,sizeof(text),"#%u %u  %s (%s)\r\n%s\r\n",
                                     file->id,file->size,file->name,file->publisher,file->description);
            session_impl_queue_text(s,text);
        }
    }
}

int mmm_chat_event(Session *s,const ChatEvent *e) {
    const MmmUi *m=mmm_view(s);
    if((e->kind=='I' || e->kind=='C') && (m->input || m->application.step || s->core.pending))return 0;
    if(strchr("LOJS",e->kind) && !m->chatting)return 1;
    char text[CHAT_TEXT+160];
    switch(e->kind) {
    case 'I': snprintf(text,sizeof(text),"[wire %lu %s] %s\r\n",e->connection,e->name,e->text);break;
    case 'O': snprintf(text,sizeof(text),"[to %lu %s] %s\r\n",e->connection,e->name,e->text);break;
    case 'C': snprintf(text,sizeof(text),"[SYSOP %lu %s] %s\r\n",e->connection,e->name,e->text);break;
    case 'S': snprintf(text,sizeof(text),"%s: %s\r\n",e->name,e->text);break;
    case 'W': snprintf(text,sizeof(text),"%lu  %s  %s\r\n",e->connection,e->name,e->text);break;
    case 'J': case 'L': snprintf(text,sizeof(text),"%s %s\r\n",e->name,e->kind=='J'?mmm_local(s,"joined","入室"):mmm_local(s,"left","退室"));break;
    default: return 1;
    }
    session_impl_queue_text(s,text);
    if(!e->replay && e->connection!=s->core.id && (e->kind=='I' || e->kind=='S' || e->kind=='C'))mmm_prompt(s);
    return s->core.active;
}

void mmm_chat_result(Session *s,const OperationResult *r,uint64_t now) {
    MmmUi *m=mmm_state(s);
    int ok=0;
    switch(r->chat_action) {
    case 'J':
        m->chatting=1;m->screen=CHAT_SCREEN;
        ok=chat_join_config(s->core.id,s->core.user,r->chat_login,r->chat_name,session_chat_event,s,&r->chat_preferences);
        if(ok)mmm_menu(s,1);else { m->chatting=0;m->screen=MAIN_SCREEN; }
        break;
    case 'O': chat_online(s->core.id);ok=1;break;
    case 'S': ok=chat_say(s->core.id,r->text,now);break;
    case 'D': ok=chat_private(s->core.id,r->chat_target,r->text,now);break;
    }
    if(ok<=0)mmm_say(s,"Chat operation unavailable.\r\n","チャット操作を実行できません。\r\n");
}

void mmm_load_preferences(Session *s,const OperationResult *r) {
    MmmUi *m=mmm_state(s);
    if(!r->preferences)return;
    m->language=(Language)r->language;m->pager=r->pager;m->editor=r->editor_preference;m->ui_aux=r->ui_aux;
    if(r->columns && r->rows)session_dimensions(s,r->columns,r->rows);
}

void mmm_settings_summary(Session *s) {
    char text[96];snprintf(text,sizeof(text),"LANG %s  SIZE %u %u\r\n",mmm_view(s)->language==LANGUAGE_JA?"ja":"en",
                           s->connection.columns,s->connection.rows);
    session_impl_queue_text(s,text);
}

void mmm_render_profile(Session *s,const OperationPage *page) {
    if(page && page->kind==OPERATION_PAGE_PROFILES) {
        const MemberProfilePage *profiles=&page->data.profiles;
        mmm_state(s)->profile_next=profiles->next;
        if(!profiles->count)mmm_say(s,"No matching profiles.\r\n","該当する会員はいません。\r\n");
        for(unsigned i=0;i<profiles->count;i++) {
            const MemberProfileRow *row=&profiles->rows[i];
            char text[160];snprintf(text,sizeof(text),"#%u %s  %s\r\n",row->user,row->name,row->handle);
            session_impl_queue_text(s,text);
        }
        return;
    }
    if(!page || page->kind!=OPERATION_PAGE_PROFILE) {
        mmm_say(s,"Profile saved.\r\n","プロフィールを保存しました。\r\n");return;
    }
    const MemberProfile *p=&page->data.profile;
    char text[96];snprintf(text,sizeof(text),"Profile #%u %s\r\n",p->user,p->name);
    session_impl_queue_text(s,text);
    for(unsigned i=0;i<PROFILE_FIELDS;i++) {
        const char *value=member_profile_value(p,i);
        if(*value) {
            session_impl_queue_text(s,profile_key(i));session_impl_queue_text(s," ");
            session_impl_queue_text(s,value);session_impl_queue_text(s,"\r\n");
        }
    }
}

void mmm_door_result(Session *s,const OperationResult *r,const OperationPage *page) {
    if(page && page->kind==OPERATION_PAGE_DOORS) {
        const DoorListing *listing=&page->data.doors;
        if(!listing->count)mmm_say(s,"No games available.\r\n","起動できるゲームはありません。\r\n");
        for(unsigned i=0;i<listing->count;i++) {
            const DoorSummary *game=&listing->games[i];
            char text[220];snprintf(text,sizeof(text),"%u  %s (%us)\r\n",game->id,game->title,game->seconds);
            session_impl_queue_text(s,text);
        }
        mmm_menu(s,1);
    } else if(s->connection.door)session_impl_queue_text(s,r->text);
    else switch(r->feedback.door_error) {
    case DOOR_REQUEST_UNKNOWN: mmm_say(s,"Unknown game number.\r\n","ゲーム番号が見つかりません。\r\n");break;
    case DOOR_REQUEST_DENIED: mmm_say(s,"Game access denied.\r\n","ゲームの利用権限がありません。\r\n");break;
    case DOOR_REQUEST_FULL: mmm_say(s,"Game is full.\r\n","ゲームの同時起動枠に空きがありません。\r\n");break;
    default: mmm_say(s,"Could not start the game.\r\n","ゲームを起動できませんでした。\r\n");break;
    }
}

void mmm_render_news(Session *s,const ArticlePage *page) {
    MmmUi *m=mmm_state(s);
    m->row_count=page->count;m->service_next=page->next;
    if(m->scan_custom && !page->next && m->scan_at<m->scan_count) {
        mmm_next_scan_board(s);
        if(m->scan_at<m->scan_count)m->service_next=1; /* First ID in the next board. */
    }
    for(unsigned i=0;i<page->count;i++) {
        const ArticleSummary *row=&page->rows[i];
        m->rows[i]=row->id;
        char text[130];snprintf(text,sizeof(text),"%u  #%u [%u] %s\r\n",i+1,row->id,row->board,row->title);
        session_impl_queue_text(s,text);
    }
    if(!page->count)mmm_say(s,"No matching notes on this page.\r\n","このページに該当するノートはありません。\r\n");
    mmm_menu(s,1);
}

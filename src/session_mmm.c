#include "session_mmm_internal.h"
#include "mmm_input.h"
#include "editor_core.h"
#include "editor_view.h"
#include "form_flow_core.h"
#include "members.h"
#include "profile_store.h"
#include "vault_bridge.h"
#include "chat.h"
#include "batch_protocol.h"
#include "date_filter.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

MmmUi *mmm_state(Session *s) { return s->ui_binding.state; }
const MmmUi *mmm_view(const Session *s) { return s->ui_binding.state; }
const char *mmm_local(const Session *s,const char *en,const char *ja) {
    return mmm_view(s)->language==LANGUAGE_JA?ja:en;
}
void mmm_say(Session *s,const char *en,const char *ja) {
    session_impl_queue_text(s,mmm_local(s,en,ja));
}

void mmm_prompt(Session *s) {
    const MmmUi *m=mmm_view(s);
    if(s->core.pending || s->core.closing || !s->core.active || s->connection.door ||
       s->connection.zmodem || s->connection.zmodem_start)return;
    if(m->application.step && m->input!=INPUT_PASSWORD) {
        const char *field=application_form_field(&m->application);
        if(!field) { mmm_say(s,"Apply? Y / E / /CANCEL > ","申請しますか Y / E / /CANCEL > ");return; }
        char text[96];
        snprintf(text,sizeof(text),"%s > ",field);
        session_impl_queue_text(s,text);
        return;
    }
    static const char *const inputs[]={
        NULL,"ID> ","Password> ","Board password> ","Title> ","Text> ",
        "Editor>> ","Base note #> ","Response number> ","Title search> ","File board password> "
    };
    if(m->input==INPUT_RESPONSE) {
        char text[32];
        snprintf(text,sizeof(text),"Response number> %u",m->first_digit);
        session_impl_queue_text(s,text);
        return;
    }
    if(m->input!=INPUT_NONE) { session_impl_queue_text(s,inputs[m->input]);return; }
    if(!s->core.user) { session_impl_queue_text(s,"LOGIN>> ");return; }
    static const char *const screens[]={"MAIN>> ","NOTES>> ","NOTES>> ","INDEX> ","OPEN> ",
        "MAIL>> ","FILER>> ","CHAT>> ","TERM>> ","GAMES>> ","NEW>> "};
    session_impl_queue_text(s,screens[m->screen]);
}

void mmm_menu(Session *s,int requested) {
    (void)requested;
    if(!s->core.user) { mmm_say(s,"Login required. login ID / register ID / ?\r\n",
                              "ログインが必要です。login ID / register ID / ?\r\n");return; }
    switch(mmm_view(s)->screen) {
    case MAIN_SCREEN:
        mmm_say(s,"1 Mail   2 Notes   3 Filer   4 Chat   5 Settings   6 Games   7 Exit\r\nnew: recent notes   ? Help\r\n",
              "1 メール   2 ノート   3 保管庫   4 チャット   5 設定   6 ゲーム   7 終了\r\nnew: 新着巡回   ? ヘルプ\r\n");break;
    case NOTES_SCREEN: mmm_say(s,"Number: select   L: more   .: up   /: main\r\n",
                             "番号で選択   L: 続き   .: 上へ   /: メイン\r\n");break;
    case BOARD_SCREEN: mmm_say(s,"1 Open   2 Post   3 Sub-boards   .: back\r\n",
                             "1 読む   2 投稿   3 子ボード   .: 戻る\r\n");break;
    case INDEX_SCREEN: mmm_say(s,"Number: open   Space/BS: page   f/F: find/continue   w: post   q: close\r\n",
                             "番号で開く   空白/BS: ページ   f/F: 検索/続行   w: 投稿   q: 閉じる\r\n");break;
    case OPEN_SCREEN: mmm_say(s,"Space/BS: response  +/-: base note  =/*: first/last  l/L: unread  f/F: find  w: reply  i: index  q: close\r\n",
                            "空白/BS: レス  +/-: ベース  =/*: 先頭/末尾  l/L: 未読  f/F: 検索  w: 返信  i: 索引  q: 閉じる\r\n");break;
    case MAIL_SCREEN:
        mmm_say(s,"inbox / sent / unread / more / read ID / send USER / reply ID / delete ID confirm\r\nfiles / resume / get ID / getall MAIL / upload / uploadbatch / resumebatch   .: back\r\n",
              "inbox 受信 / sent 送信済み / unread 未読 / more 続き / read 番号 / send 宛先 / reply 番号 / delete 番号 confirm\r\nfiles 添付一覧 / resume 下書き再開 / get 番号 / getall メール番号 / upload / uploadbatch / resumebatch   .: 戻る\r\n");break;
    case FILER_SCREEN:
        mmm_say(s,"list / more / cd PATH / find PATTERN / info ID / text ID / unlock / get ID\r\nput [description] / resume / add ID / batch / clear   .: back\r\n",
              "list 一覧 / more 続き / cd パス / find 検索語 / info 番号 / text 番号 / unlock / get 番号\r\nput [説明] / resume / add 番号 / batch / clear   .: 戻る\r\n");break;
    case CHAT_SCREEN:
        mmm_say(s,"Type a line to chat. /who lists users, /wire ID TEXT sends privately, /leave returns.\r\n",
              "1行で発言。/who 接続者、/wire 番号 本文 で個別送信、/leave で戻ります。\r\n");break;
    case TERM_SCREEN:
        mmm_say(s,"lang ja|en / size COLUMNS ROWS / save / load / profile [USER] / set FIELD VALUE   .: back\r\n",
              "lang ja|en 言語 / size 列 行 / save 保存 / load 読込 / profile [会員] / set 項目 値   .: 戻る\r\n");break;
    case GAMES_SCREEN: mmm_say(s,"Select a game number. Enter refreshes, . returns.\r\n",
                             "番号で起動。Enterで再表示、. で戻る。\r\n");break;
    case NEW_SCREEN: mmm_say(s,"Number: read   more: next page   .: back\r\n",
                           "番号で読む   more: 次のページ   .: 戻る\r\n");break;
    }
}

static void help(Session *s,const char *topic) {
    (void)topic;
    mmm_menu(s,1);
    if(s->core.user && (mmm_view(s)->screen==MAIN_SCREEN || mmm_view(s)->screen==NOTES_SCREEN))
        mmm_say(s,"notes / go / open / post / mail / filer / chat / term / profile / games / new [-n] / setsequencer / logout / off [-n] / ui\r\n",
              "notes / go / open / post / mail / filer / chat / term / profile / games / new [-n] / setsequencer / logout / off [-n] / ui\r\n");
}

OperationRequest *mmm_request(Session *s,char op) {
    OperationRequest *q=calloc(1,sizeof(*q));
    if(!q) { mmm_say(s,"Request unavailable.\r\n","要求を処理できません。\r\n");return NULL; }
    q->op=op;
    q->board=s->core.board;
    return q;
}

int mmm_submit(Session *s,OperationRequest *q,MmmJob job) {
    if(!q)return 0;
    SessionOperationStatus result=session_submit(s,q);
    OPENSSL_cleanse(q,sizeof(*q));
    free(q);
    if(result==SESSION_OPERATION_ACCEPTED) { mmm_state(s)->job=job;return 1; }
    if(result==SESSION_OPERATION_THROTTLED)
        mmm_say(s,"Try again shortly.\r\n","少し待って再試行してください。\r\n");
    else mmm_say(s,"Request unavailable.\r\n","要求を処理できません。\r\n");
    return 0;
}

static void boards(Session *s,uint32_t cursor) {
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->action='B';q->number=mmm_view(s)->parent;q->cursor=cursor;
    if(mmm_submit(s,q,JOB_BOARDS))mmm_state(s)->screen=NOTES_SCREEN;
}

static void index_page(Session *s,uint32_t cursor) {
    OperationRequest *q=mmm_request(s,'O');
    if(!q)return;
    q->number=cursor;
    if(mmm_submit(s,q,JOB_INDEX))mmm_state(s)->page_start=cursor;
}

static void board(Session *s,uint32_t id) {
    OperationRequest *q=mmm_request(s,'j');
    if(!q)return;
    q->number=id;
    if(mmm_submit(s,q,JOB_BOARD))mmm_state(s)->board_target=id;
}

static void enter_board(Session *s,const char *name,MmmBoardAction action) {
    MmmUi *m=mmm_state(s);
    m->action=action;
    uint32_t id;
    if(!*name) { board(s,s->core.board);return; }
    if(mmm_number(*name=='#'?name+1:name,&id)) { board(s,id);return; }
    if(strlen(name)>80) { mmm_say(s,"Invalid board name.\r\n","ボード名が不正です。\r\n");return; }
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->action='N';
    strcpy(q->title,name);
    mmm_submit(s,q,JOB_NAMED);
}

static void read_article(Session *s,uint32_t id) {
    OperationRequest *q=mmm_request(s,'R');
    if(!q)return;
    q->number=id;
    mmm_submit(s,q,JOB_READ);
}

static void continue_browse(Session *s,uint32_t cursor) {
    const MmmUi *m=mmm_view(s);
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->number=m->browse_root;q->cursor=cursor;q->descending=m->descending;q->notes_unread=m->unread;
    if(m->search_responses || m->search_next)strcpy(q->title,m->search);
    mmm_submit(s,q,JOB_BROWSE);
}

static void browse(Session *s,MmmBrowse purpose,uint32_t root,uint32_t start,int down,int unread) {
    MmmUi *m=mmm_state(s);
    m->browse=purpose;m->browse_root=root;m->descending=down;m->unread=unread;
    m->number_seen=0;
    continue_browse(s,start);
}

static void read_search(Session *s,int response,uint32_t start) {
    MmmUi *m=mmm_state(s);
    m->search_responses=response;
    m->search_next=start?start:UINT32_MAX;
    browse(s,BROWSE_READ,response?m->root:0,start,1,0);
}

static void prepare_post(Session *s,uint32_t parent) {
    OperationRequest *q=mmm_request(s,'e');
    if(!q)return;
    q->number=parent;
    if(mmm_submit(s,q,JOB_PREPARE))mmm_state(s)->post_parent=parent;
}

static EditorDocument document(MmmUi *m) {
    return (EditorDocument){m->title,m->body,m->undo_title,m->undo_body,&m->undo};
}

static void discard_draft(MmmUi *m) {
    OPENSSL_cleanse(m->body,sizeof(m->body));OPENSSL_cleanse(m->title,sizeof(m->title));
    EditorDocument d=document(m);
    editor_discard_undo(&d);
}

void mmm_clear_composition(MmmUi *m) {
    discard_draft(m);
    m->mail_to=m->mail_parent=m->mail_revision=0;
    memset(&m->attachments,0,sizeof(m->attachments));
}

static void editorial(Session *s,char *text) {
    MmmUi *m=mmm_state(s);
    EditorDocument d=document(m);
    if(m->input==INPUT_TITLE) {
        if(editor_apply(&d,EDITOR_SET_TITLE,0,0,text)==EDITOR_OK) {
            m->input=INPUT_BODY;m->insert_at=1;m->numbered=0;
        } else mmm_say(s,"Enter a title up to 80 bytes.\r\n","題名は80バイト以内です。\r\n");
        return;
    }
    if(m->input==INPUT_BODY) {
        if(!strcmp(text,"."))m->input=INPUT_EDITOR;
        else if(editor_apply(&d,EDITOR_INSERT,m->insert_at,0,text)==EDITOR_OK)m->insert_at++;
        else mmm_say(s,"Line or body limit.\r\n","入力行または本文の上限です。\r\n");
        if(m->input==INPUT_BODY && m->numbered) {
            char number[24];snprintf(number,sizeof(number),"%u: ",m->insert_at);
            session_impl_queue_text(s,number);
        }
        return;
    }
    if(!strcmp(text,"w")) {
        if(!*m->body) { mmm_say(s,"Enter a body before saving.\r\n","本文を入力してください。\r\n");return; }
        OperationRequest *q=mmm_request(s,m->mail_to?'m':'P');
        if(q) {
            q->number=m->mail_to?m->mail_to:m->post_parent;
            if(m->mail_to) {
                q->action='S';q->cursor=m->mail_parent;q->mail_revision=m->mail_revision;
                q->mail_attachments=m->attachments;
            }
            strcpy(q->title,m->title);strcpy(q->body,m->body);mmm_submit(s,q,m->mail_to?JOB_MAIL:JOB_POST);
        }
        return;
    }
    if(!strcmp(text,"q")) { mmm_clear_composition(m);m->input=INPUT_NONE;return; }
    if(m->mail_to && (!strcmp(text,"s") || !strncmp(text,"attach ",7) || !strncmp(text,"detach ",7))) {
        uint32_t id=0;
        if(text[1] && !mmm_number(text+7,&id))goto syntax;
        OperationRequest *q=mmm_request(s,'m');
        if(q) {
            q->mail_attachments=m->attachments;
            if(id) { q->action='A';q->number=id;q->cursor=text[0]=='d'; }
            else {
                q->action='W';q->number=m->mail_to;q->cursor=m->mail_parent;q->mail_revision=m->mail_revision;
                strcpy(q->title,m->title);strcpy(q->body,m->body);
            }
            mmm_submit(s,q,JOB_MAIL);
        }
        return;
    }
    EditorInfo info;
    if(editor_inspect(&d,&info)!=EDITOR_OK)return;
    if(!strcmp(text,"a") || !strcmp(text,"u")) {
        m->numbered=text[0]=='a';m->insert_at=info.lines+1;m->input=INPUT_BODY;return;
    }
    if(text[0]=='i') {
        const char *number=text+1;
        while(*number==' ' || *number=='\t')number++;
        if(!*number)m->insert_at=info.lines+1;
        if((!*number || mmm_number(number,&m->insert_at)) && m->insert_at<=info.lines+1) {
            m->numbered=0;m->input=INPUT_BODY;return;
        }
    } else if(*text && strchr("lpdr",text[0])) {
        uint32_t first,last;
        int except;
        const char *tail;
        if(mmm_range(text+1,info.lines,&first,&last,&except,&tail) && (text[0]=='r' || !*tail)) {
            if(text[0]=='d' || text[0]=='r') {
                char *needle=(char *)tail,*replacement=NULL;
                if(text[0]=='r') {
                    replacement=strchr(needle,' ');
                    if(replacement && *needle) *replacement++=0;
                    else goto syntax;
                }
                if(editor_transform(&d,first,last,except,text[0]=='r'?needle:NULL,replacement)!=EDITOR_OK)
                    mmm_say(s,"Edit could not be applied.\r\n","編集を適用できませんでした。\r\n");
                return;
            }
            if(except)goto syntax;
            char *out=malloc(CONTENT_DISPLAY);
            if(out && editor_view_lines(&d,first,last,text[0]=='l',out,CONTENT_DISPLAY)==EDITOR_VIEW_OK)
                session_impl_queue_text(s,out);
            if(out) { OPENSSL_cleanse(out,CONTENT_DISPLAY);free(out); }
            return;
        }
    }
syntax:
    mmm_say(s,"l/p [range], a/u, i[number], d[range], r[range] OLD NEW, w, q\r\n",
          "l/p [範囲]、a/u、i[行]、d[範囲]、r[範囲] 旧 新、w 保存、q 破棄\r\n");
}

static void send_password(Session *s,const char *text) {
    MmmUi *m=mmm_state(s);
    char op=m->input==INPUT_BOARD_PASSWORD?'(':m->auth_op;
    m->input=INPUT_NONE;
    if(!strcmp(text,"/CANCEL")) { application_form_clear(&m->application);return; }
    size_t n=strlen(text);
    if(n<8 || n>128) { mmm_say(s,"Password must be 8–128 bytes.\r\n","パスワードは8〜128バイトです。\r\n");return; }
    OperationRequest *q=mmm_request(s,op);
    if(!q)return;
    strcpy(q->password,text);
    if(op=='(') { q->action='U';q->number=m->board_target;q->cursor=m->lock; }
    else {
        strcpy(q->username,m->login);
        if(op=='g')strcpy(q->body,m->application.body);
        application_form_clear(&m->application);
    }
    mmm_submit(s,q,op=='('?JOB_UNLOCK:JOB_AUTH);
}

static void auth_start(Session *s,const char *name,char op) {
    MmmUi *m=mmm_state(s);
    if(!s->core.member_mode) { mmm_say(s,"The member service is disabled.\r\n","会員機能が無効です。\r\n");return; }
    if(!*name || strlen(name)>32 || strpbrk(name," \t")) {
        mmm_say(s,"Enter a valid account ID.\r\n","会員IDを入力してください。\r\n");return;
    }
    strcpy(m->login,name);
    m->auth_op=op;
    if(op=='g' && members_detailed_form())application_form_begin(&m->application);
    else m->input=INPUT_PASSWORD;
}

static void application_value(Session *s,const char *line) {
    MmmUi *m=mmm_state(s);
    const char *key=application_form_field(&m->application);
    int id=key?profile_field(key):-1;
    ApplicationInput kind=!strcmp(line,"/CANCEL")?APPLICATION_CANCEL:
                         (!strcmp(line,"y") || !strcmp(line,"Y"))?APPLICATION_CONFIRM:
                         (!strcmp(line,"e") || !strcmp(line,"E"))?APPLICATION_RESTART:APPLICATION_VALUE;
    ApplicationEffect result=application_form_input(&m->application,kind,line,
                                                     id<0 || profile_text_valid((unsigned)id,line));
    if(result==APPLICATION_PASSWORD)m->input=INPUT_PASSWORD;
    if(result==APPLICATION_REVIEW)session_impl_queue_text(s,m->application.body);
    if(result==APPLICATION_INVALID)mmm_say(s,"Invalid value.\r\n","入力内容が不正です。\r\n");
}

static void return_to_parent(Session *s) {
    MmmUi *m=mmm_state(s);
    m->row_count=0;m->next=0;
    if(m->screen==BOARD_SCREEN)boards(s,0);
    else if(m->depth) { m->parent=m->ancestors[--m->depth];boards(s,0); }
    else m->screen=MAIN_SCREEN;
}

static void subboards(Session *s,uint32_t parent) {
    MmmUi *m=mmm_state(s);
    if(parent!=m->parent && m->depth<sizeof(m->ancestors)/sizeof(m->ancestors[0])) {
        m->ancestors[m->depth++]=m->parent;
        m->parent=parent;
    }
    boards(s,0);
}

static void choose_profile(Session *s,char *arg) {
    const SessionUiConfig *c=&s->ui_binding.config;
    if(!*arg) {
        static const char *const policies[]={"FIXED","MEMBERS","ALL"};
        char summary[96];
        snprintf(summary,sizeof(summary),"UI mmm / %s / DEFAULT %s\r\n",
                 policies[c->policy],c->adapters[c->default_index]->name);
        session_impl_queue_text(s,summary);
        for(unsigned i=0;i<c->count;i++) {
            const char *name=c->adapters[i]->name;
            SessionUiSelection status=session_ui_preflight(s,name);
            if(status==SESSION_UI_SELECTED || status==SESSION_UI_UNCHANGED) {
                session_impl_queue_text(s,name);session_impl_queue_text(s,"\r\n");
            }
        }
        return;
    }
    for(char *p=arg;*p;p++)if(*p>='A' && *p<='Z')*p=(char)(*p+'a'-'A');
    int reset=!strcmp(arg,"reset");
    const char *name=reset?c->adapters[c->default_index]->name:arg;
    SessionUiSelection result=session_ui_choose(s,name);
    if(result==SESSION_UI_SELECTED || result==SESSION_UI_UNCHANGED) {
        if(s->core.user && !session_ui_save_choice(s,reset?"":name)) {
            session_impl_queue_text(s,message_text(session_ui_snapshot(s).language,MSG_ERR_SETTINGS_STORAGE));
            session_impl_queue_text(s,"\r\n");
        }
    } else mmm_say(s,"UI selection unavailable here.\r\n","ここではUIを切り替えられません。\r\n");
}

static void open_command(Session *s,char *arg) {
    MmmBoardAction action=OPEN_INDEX;
    while(*arg=='-') {
        char *end=strchr(arg,' ');
        if(end)*end++=0;
        if(!strcmp(arg,"-i")) {}
        else if(arg[1]=='b' && mmm_number(arg+2,&mmm_state(s)->target))action=OPEN_BASE;
        else { help(s,"open");return; }
        arg=end?end:arg+strlen(arg);
        while(*arg==' ')arg++;
    }
    enter_board(s,arg,action);
}

static void number_menu(Session *s,uint32_t n) {
    MmmUi *m=mmm_state(s);
    if(!s->core.user)return;
    if(m->screen==MAIN_SCREEN && n==1) { m->screen=MAIL_SCREEN;m->service_next=0;mmm_mail_list(s,MAIL_LIST_INBOX,0); }
    else if(m->screen==MAIN_SCREEN && n==2) { m->parent=0;m->depth=0;boards(s,0); }
    else if(m->screen==MAIN_SCREEN && n==3) { m->screen=FILER_SCREEN;m->service_next=0;mmm_file_enter(s,m->file_path); }
    else if(m->screen==MAIN_SCREEN && n==4)mmm_chat_request(s,'J',0,"");
    else if(m->screen==MAIN_SCREEN && n==5) {
        m->screen=TERM_SCREEN;mmm_menu(s,1);mmm_simple_operation(s,'V',0,2,JOB_PREF);
    }
    else if(m->screen==MAIN_SCREEN && n==6)mmm_games(s,0);
    else if(m->screen==MAIN_SCREEN && n==7)mmm_off(s,0);
    else if(m->screen==NOTES_SCREEN && n<=m->row_count) { m->action=OPEN_BOARD_MENU;board(s,m->rows[n-1]); }
    else if(m->screen==BOARD_SCREEN && n==1)index_page(s,0);
    else if(m->screen==BOARD_SCREEN && n==2)prepare_post(s,0);
    else if(m->screen==BOARD_SCREEN && n==3)subboards(s,s->core.board);
    else mmm_say(s,"Choose a listed number.\r\n","一覧にある番号を入力してください。\r\n");
}

static void command(Session *s,char *text) {
    MmmUi *m=mmm_state(s);
    if(s->core.user && mmm_service_line(s,text))return;
    MmmInput input=mmm_parse(text,s->core.user!=0);
    if(input.usage && input.command>MMM_AMBIGUOUS) {
        const char *name,*usage;
        mmm_command_help(input.command,&name,&usage);
        session_impl_queue_text(s,usage);session_impl_queue_text(s,"\r\n");return;
    }
    if(s->core.user && (m->screen==NOTES_SCREEN) && (!strcmp(text,"L") || !strcmp(text,"l"))) {
        if(m->next)boards(s,m->next);
        else mmm_say(s,"End of boards.\r\n","ボード一覧の末尾です。\r\n");
        return;
    }
    switch(input.command) {
    case MMM_MENU: if(m->screen==NOTES_SCREEN)boards(s,0);else mmm_menu(s,1);break;
    case MMM_HELP: help(s,"");break;
    case MMM_HOME:
        m->screen=MAIN_SCREEN;m->parent=0;m->depth=0;mmm_menu(s,1);break;
    case MMM_UP: return_to_parent(s);break;
    case MMM_NUMBER: number_menu(s,input.number);break;
    case MMM_LOGIN: case MMM_REGISTER:
        if(s->core.user) { mmm_say(s,"Log out first.\r\n","先にログアウトしてください。\r\n");break; }
        if(input.command==MMM_LOGIN && !*input.argument)m->input=INPUT_LOGIN;
        else auth_start(s,input.argument,input.command==MMM_REGISTER?'g':'a');
        break;
    case MMM_OFF:
        if(!*input.argument || !strcmp(input.argument,"-n"))mmm_off(s,*input.argument!=0);
        else mmm_bad_service_command(s);
        break;
    case MMM_LOGOUT:
        if(session_logout(s)) {
            chat_disconnect(s->core.id);m->screen=MAIN_SCREEN;m->parent=0;m->depth=0;
            m->sequence_read=0;m->login_at=0;m->service_next=0;
            m->pager=m->editor=0;m->ui_aux=0;
            m->file_path[0]=m->requested_path[0]=m->file_filter[0]=0;batch_reset(&m->files);
            mmm_menu(s,1);
        }
        break;
    case MMM_NOTES: m->parent=0;m->depth=0;boards(s,0);break;
    case MMM_GO: if(*input.argument)enter_board(s,input.argument,OPEN_BOARD_MENU);else help(s,"go");break;
    case MMM_OPEN: open_command(s,input.argument);break;
    case MMM_POST: enter_board(s,input.argument,OPEN_POST);break;
    case MMM_UI: choose_profile(s,input.argument);break;
    case MMM_MAIL: number_menu(s,1);break;
    case MMM_FILER:
        m->screen=FILER_SCREEN;m->service_next=0;mmm_file_enter(s,*input.argument?input.argument:m->file_path);break;
    case MMM_CHAT: mmm_chat_request(s,'J',0,"");break;
    case MMM_TERM: number_menu(s,5);break;
    case MMM_PROFILE: mmm_profile(s,input.argument);break;
    case MMM_GAMES: mmm_games(s,0);break;
    case MMM_NEW:
        if(!*input.argument || !strcmp(input.argument,"-n")) {
            m->new_all=*input.argument!=0;mmm_simple_operation(s,BOARD_NOTE_VIEW,'C',0,JOB_NEW_BEGIN);
        } else mmm_bad_service_command(s);
        break;
    case MMM_SEQUENCER: {
        if(!*input.argument) { mmm_simple_operation(s,BOARD_NOTE_VIEW,'C',0,JOB_SEQUENCE);break; }
        uint64_t stamp=0;
        if(strcmp(input.argument,"reset") && !date_filter(input.argument,&stamp)) { mmm_bad_service_command(s);break; }
        OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
        if(q) { q->action='T';q->until=stamp;mmm_submit(s,q,JOB_SEQUENCE); }
        break;
    }
    case MMM_AMBIGUOUS: mmm_say(s,"Ambiguous command.\r\n","コマンドの省略が曖昧です。\r\n");break;
    default:
        if(!s->core.user)mmm_menu(s,1);
        else mmm_say(s,"Unknown command. Use ?.\r\n","不明なコマンドです。? で案内を表示できます。\r\n");
    }
}

static void line(Session *s,char *text,size_t size) {
    (void)size;
    MmmUi *m=mmm_state(s);
    switch(m->input) {
    case INPUT_PASSWORD: case INPUT_BOARD_PASSWORD: send_password(s,text);break;
    case INPUT_FILE_PASSWORD: {
        m->input=INPUT_NONE;
        if(!strcmp(text,"/CANCEL"))break;
        if(!*text || !mmm_ui_text_valid(text,128)) { mmm_bad_service_command(s);break; }
        OperationRequest *q=mmm_request(s,'Y');
        if(q) {
            q->action=VAULT_CMD_UNLOCK;
            snprintf(q->body,sizeof(q->body),"%s|%s",*m->requested_path?m->requested_path:"-",text);
            mmm_submit(s,q,JOB_FILE_UNLOCK);
        }
        break;
    }
    case INPUT_LOGIN: m->input=INPUT_NONE;auth_start(s,text,'a');break;
    case INPUT_TITLE: case INPUT_BODY: case INPUT_EDITOR: editorial(s,text);break;
    case INPUT_NOTE: case INPUT_RESPONSE: {
        MmmPrompt input=m->input;m->input=INPUT_NONE;
        char joined[16];
        if(input==INPUT_RESPONSE) {
            if(strlen(text)>9) { mmm_say(s,"Invalid number.\r\n","番号が不正です。\r\n");break; }
            snprintf(joined,sizeof(joined),"%u%s",m->first_digit,text);
            text=joined;
        }
        if(!mmm_number(text,&m->target)) { mmm_say(s,"Invalid number.\r\n","番号が不正です。\r\n");break; }
        m->search_next=0;m->search_responses=0;
        browse(s,input==INPUT_NOTE?BROWSE_EXACT:BROWSE_RESPONSE,input==INPUT_NOTE?0:m->root,
               input==INPUT_NOTE?m->target:0,0,0);
        break;
    }
    case INPUT_FIND:
        m->input=INPUT_NONE;
        if(*text && strlen(text)<=80) {
            strcpy(m->search,text);
            if(m->screen==INDEX_SCREEN || m->article>1)
                read_search(s,m->search_responses,m->screen==INDEX_SCREEN?0:m->article-1);
            else mmm_say(s,"No earlier notes.\r\n","これより前にノートはありません。\r\n");
        }
        break;
    default:
        if(m->application.step)application_value(s,text);
        else command(s,text);
        break;
    }
    if(s->ui_binding.state==m)mmm_prompt(s);
}

static int keys(const Session *s) {
    const MmmUi *m=mmm_view(s);
    return s->core.user && m->input==INPUT_NONE &&
           (m->screen==INDEX_SCREEN || m->screen==OPEN_SCREEN);
}

static void key(Session *s,unsigned char ch) {
    MmmUi *m=mmm_state(s);
    int index=m->screen==INDEX_SCREEN;
    if(ch=='f' || (!index && ch=='\'')) {
        m->search_responses=ch=='\'';m->input=INPUT_FIND;
    } else if(ch=='F' || (!index && ch=='"')) {
        if(m->search_next && m->search_responses==(ch=='"'))
            read_search(s,m->search_responses,m->search_next);
        else mmm_say(s,"No further title matches.\r\n","これ以上一致する題名はありません。\r\n");
    } else {
        m->search_next=0;m->search_responses=0;
        if(ch=='q' || ch==4) { m->screen=BOARD_SCREEN;mmm_menu(s,1); }
        else if(ch=='?' || ch=='&')help(s,"");
        else if(ch=='w')prepare_post(s,index?0:m->root);
        else if(ch=='b')m->input=INPUT_NOTE;
        else if(ch=='i' && !index)index_page(s,m->page_start);
        else if(ch=='l' || ch=='L' || ch=='\t')
            browse(s,BROWSE_READ,index || ch=='L'?0:m->root,index?0:m->article+1,0,1);
        else if(ch=='=' || ch==12 || ch=='v' || ch=='X') {
            if(index)index_page(s,ch=='='?0:m->page_start);
            else read_article(s,ch=='='?m->root:m->article);
        } else if(index) {
            if(ch>='1' && ch<='8' && (unsigned)(ch-'0')<=m->row_count)
                read_article(s,m->rows[ch-'1']);
            else if((ch==' ' || ch==24 || ch=='\r') && m->next)index_page(s,m->next);
            else if(ch==8 || ch==127 || ch==5 || ch=='*')
                browse(s,BROWSE_INDEX,0,ch=='*'?0:(m->page_start>1?m->page_start-1:1),1,0);
        } else if(ch=='+' || ch=='j' || ch=='-') {
            if(ch!='-' || m->root>1)browse(s,BROWSE_READ,0,ch=='-'?m->root-1:m->root+1,ch=='-',0);
        } else if(ch=='*' || ch=='t')
            browse(s,ch=='t'?BROWSE_TITLES:BROWSE_READ,m->root,ch=='t'?m->article+1:0,ch=='*',0);
        else if(ch==' ' || ch=='>' || ch==24 || ch=='\r')
            browse(s,BROWSE_READ,m->root,m->article+1,0,0);
        else if(ch==8 || ch==127 || ch=='<' || ch==5) {
            if(m->article!=m->root)browse(s,BROWSE_READ,m->root,m->article-1,1,0);
            else if(m->root>1)browse(s,BROWSE_READ,0,m->root-1,1,0);
        } else if(ch>='1' && ch<='9') {
            m->first_digit=(unsigned)(ch-'0');m->input=INPUT_RESPONSE;
            session_impl_queue_text(s,"\r\n");
        }
    }
    mmm_prompt(s);
}

static void render_boards(Session *s,const ArticleBoardsPage *page) {
    MmmUi *m=mmm_state(s);
    m->row_count=page->count;m->next=page->next;
    if(!page->count)mmm_say(s,"No boards here.\r\n","ここに表示できるボードはありません。\r\n");
    for(unsigned i=0;i<page->count;i++) {
        const ArticleBoardRow *row=&page->rows[i];
        m->rows[i]=row->id;
        char text[128];snprintf(text,sizeof(text),"%u  #%u  %s\r\n",i+1,row->id,row->title);
        session_impl_queue_text(s,text);
    }
    mmm_menu(s,1);
}

static void render_index(Session *s,const ArticleNotesPage *page) {
    MmmUi *m=mmm_state(s);
    m->screen=INDEX_SCREEN;m->row_count=page->count;m->next=page->next;
    mmm_say(s,"Base notes\r\n","ベースノート\r\n");
    for(unsigned i=0;i<page->count;i++) {
        const ArticleNoteRow *row=&page->rows[i];m->rows[i]=row->summary.id;
        char text[200];snprintf(text,sizeof(text),"%u  #%u  %s  (%u)  %s\r\n",
                                i+1,row->summary.id,row->author,row->replies,row->summary.title);
        session_impl_queue_text(s,text);
    }
}

static void browse_result(Session *s,const ArticlePage *page) {
    MmmUi *m=mmm_state(s);
    if(m->browse==BROWSE_RESPONSE) {
        uint32_t before=m->number_seen;
        m->number_seen+=page->count;
        if(m->target>before && m->target<=m->number_seen) {
            read_article(s,page->rows[m->target-before-1].id);
            return;
        }
        if(page->next) { continue_browse(s,page->next);return; }
    } else if(!page->count && page->next) { continue_browse(s,page->next);return; }
    else if(page->count) {
        uint32_t id=page->rows[0].id;
        if(m->browse==BROWSE_INDEX) {
            unsigned end=page->count<5?page->count:5;
            index_page(s,page->rows[end-1].id);return;
        }
        if(m->browse==BROWSE_TITLES) {
            for(unsigned i=0;i<page->count;i++) {
                char text[114];
                snprintf(text,sizeof(text),"#%u  %s\r\n",page->rows[i].id,page->rows[i].title);
                session_impl_queue_text(s,text);
            }
            return;
        }
        if(m->browse!=BROWSE_EXACT || id==m->target) {
            if(m->search_next)m->search_next=id>1?id-1:0;
            read_article(s,id);return;
        }
    }
    if(m->search_next)m->search_next=0;
    else if(m->browse==BROWSE_READ && m->browse_root) {
        if(m->descending) { read_article(s,m->root);return; }
        if(m->unread) { browse(s,BROWSE_READ,0,m->root+1,0,1);return; }
        browse(s,BROWSE_READ,0,m->root+1,0,0);return;
    }
    mmm_say(s,"No matching note.\r\n","該当するノートがありません。\r\n");
}

static void render_article(Session *s,const OperationContent *c,const char *body) {
    char title[240];
    snprintf(title,sizeof(title),"%s #%u / #%u  %s\r\n%s\r\n",
             c->parent?"Response":"Base note",c->id,c->root,c->author,c->title);
    session_impl_queue_text(s,title);
    session_impl_queue_text(s,body);
    OperationRequest *q=mmm_request(s,'s');
    if(q) { q->board=c->board;q->number=c->id;mmm_submit(s,q,JOB_SEEN); }
}

static void result(Session *s,const OperationResult *r,SessionResultView projection,uint64_t now,int cancelled) {
    (void)cancelled;
    MmmUi *m=mmm_state(s);
    MmmJob job=m->job;m->job=JOB_NONE;
    if(job==JOB_CHAT && m->chat_kicked) { m->chat_kicked=0;mmm_prompt(s);return; }
    if(r->auth>0 && projection.status==SESSION_RESULT_OK) {
        mmm_load_preferences(s,r);
        time_t started=time(NULL);m->login_at=started>0?(uint64_t)started:0;
        if(board_feature_enabled("chat")) {
            chat_register(s->core.id,s->core.user,r->chat_login,r->chat_name,session_chat_event,s,&r->chat_preferences);
            chat_admin(s->core.id,r->chat_admin);
        }
        char message[88];snprintf(message,sizeof(message),"Logged in: %s\r\n",s->core.username);
        session_impl_queue_text(s,message);
        m->screen=MAIN_SCREEN;mmm_menu(s,1);mmm_prompt(s);return;
    }
    if(projection.status!=SESSION_RESULT_OK) {
        if(job==JOB_TRANSFER)s->connection.zmodem_start=0;
        if(job==JOB_OFF)
            mmm_say(s,"Could not save the sequencer. Retry off, or use off -n.\r\n",
                  "巡回時刻を保存できません。off で再試行するか、off -n で終了してください。\r\n");
        switch(job) {
        case JOB_AUTH: case JOB_UNLOCK: mmm_say(s,"Authentication or registration denied.\r\n","認証または登録が拒否されました。\r\n");break;
        case JOB_OFF: break;
        default: mmm_say(s,"The operation is unavailable.\r\n","この操作は利用できません。\r\n");break;
        }
        mmm_prompt(s);return;
    }
    if(r->feedback.ui_profile_saved)
        mmm_say(s,"UI preference saved.\r\n","UIの希望設定を保存しました。\r\n");
    const OperationPage *page=projection.page;
    switch(job) {
    case JOB_AUTH: mmm_say(s,"Registration request received.\r\n","登録申請を受け付けました。\r\n");break;
    case JOB_BOARDS: if(page && page->kind==OPERATION_PAGE_BOARDS)render_boards(s,&page->data.boards);break;
    case JOB_NAMED: if(r->service_number)board(s,r->service_number);break;
    case JOB_UNLOCK: board(s,m->board_target);break;
    case JOB_BOARD:
        if(r->service_action=='1') { m->lock=r->board_grant;m->input=INPUT_BOARD_PASSWORD; }
        else if(r->service_action=='7')subboards(s,r->board);
        else {
            snprintf(m->board_title,sizeof(m->board_title),"%.80s",r->service_text);
            m->screen=BOARD_SCREEN;m->article=m->root=m->search_next=0;m->search_responses=0;m->search[0]=0;
            if(m->action==OPEN_POST)prepare_post(s,0);
            else if(m->action==OPEN_INDEX)index_page(s,0);
            else if(m->action==OPEN_BASE)browse(s,BROWSE_EXACT,0,m->target,0,0);
            else {
                session_impl_queue_text(s,m->board_title);session_impl_queue_text(s,"\r\n");
                mmm_menu(s,1);
            }
        }
        break;
    case JOB_INDEX: if(page && page->kind==OPERATION_PAGE_NOTES)render_index(s,&page->data.notes);break;
    case JOB_BROWSE: if(page && page->kind==OPERATION_PAGE_ARTICLES)browse_result(s,&page->data.articles);break;
    case JOB_READ:
        if(projection.content && projection.content->state==ARTICLE_VISIBLE) {
            const OperationContent *c=projection.content;
            m->root=c->root;m->article=c->id;m->screen=OPEN_SCREEN;
            char title[240];snprintf(title,sizeof(title),"%s #%u / #%u  %s\r\n%s\r\n",
                c->parent?"Response":"Base note",c->id,c->root,c->author,c->title);
            session_impl_queue_text(s,title);
            session_impl_queue_text(s,projection.body);
            OperationRequest *q=mmm_request(s,'s');
            if(q) { q->number=c->id;mmm_submit(s,q,JOB_SEEN); }
        } else mmm_say(s,"Note is unavailable.\r\n","記事を表示できません。\r\n");
        break;
    case JOB_PREPARE: mmm_clear_composition(m);m->input=INPUT_TITLE;break;
    case JOB_POST: {
        char text[48];snprintf(text,sizeof(text),"Saved #%u\r\n",s->core.current);
        session_impl_queue_text(s,text);discard_draft(m);m->input=INPUT_NONE;
        if(m->screen==INDEX_SCREEN)index_page(s,m->page_start);
        break;
    }
    case JOB_MAIL: mmm_mail_result(s,r,projection);break;
    case JOB_MAIL_BATCH: mmm_mail_batch_result(s,&r->feedback);break;
    case JOB_FILES: case JOB_FILE_ADD: mmm_file_result(s,r,page,job==JOB_FILE_ADD);break;
    case JOB_FILE_UNLOCK: mmm_file_enter(s,m->requested_path);break;
    case JOB_CHAT: mmm_chat_result(s,r,now);break;
    case JOB_PREF: mmm_load_preferences(s,r);mmm_settings_summary(s);break;
    case JOB_PROFILE: mmm_render_profile(s,page);break;
    case JOB_GAME: mmm_door_result(s,r,page);break;
    case JOB_TRANSFER:
        s->connection.zmodem_start=0;
        if(r->binary_enter) {
            s->connection.zmodem=1;
            int mode=s->connection.zmodem_receive;
            MessageId ready=mode==2 || mode==10?MSG_ZMODEM_BATCH_READY:
                mode==6 || mode==11 || mode==12?MSG_ZMODEM_BATCH_UPLOAD_READY:MSG_ZMODEM_READY;
            session_impl_queue_text(s,message_text(m->language,ready));
        }
        break;
    case JOB_SEQUENCE: {
        if(r->feedback.checkpoint_saved)m->sequence_read=0;
        char stamp[64];snprintf(stamp,sizeof(stamp),"Sequencer: %llu\r\n",(unsigned long long)r->since);
        session_impl_queue_text(s,stamp);break;
    }
    case JOB_NEW_BEGIN:
        m->new_since=m->new_all?0:r->since;m->new_until=r->until;
        m->screen=NEW_SCREEN;m->row_count=0;m->service_next=0;mmm_new_page(s,0);break;
    case JOB_NEW_PAGE: if(page && page->kind==OPERATION_PAGE_ARTICLES)mmm_render_news(s,&page->data.articles);break;
    case JOB_NEW_READ:
        if(projection.content && projection.content->state==ARTICLE_VISIBLE) {
            if(!m->new_all)m->sequence_read=1;
            render_article(s,projection.content,projection.body);
        } else mmm_say(s,"Note is unavailable.\r\n","記事を表示できません。\r\n");
        break;
    case JOB_OFF: if(r->feedback.checkpoint_saved)session_core_close(&s->core);break;
    default: break;
    }
    mmm_prompt(s);
}

static SessionUiSnapshot snapshot(const Session *s) {
    const MmmUi *m=mmm_view(s);
    return (SessionUiSnapshot){.language=m->language,.menu=1,
        .secret=m->input==INPUT_PASSWORD || m->input==INPUT_BOARD_PASSWORD || m->input==INPUT_FILE_PASSWORD,
        .editing=m->input>=INPUT_TITLE && m->input<=INPUT_EDITOR,
        .chatting=m->chatting,.chat_pending=m->job==JOB_CHAT};
}

static void enter(Session *s,int initial,int interactive,Language language) {
    (void)interactive;
    MmmUi *m=mmm_state(s);
    m->language=language;m->screen=MAIN_SCREEN;
    s->core.structured=1;
    if(s->core.user && !m->login_at) {
        time_t started=time(NULL);m->login_at=started>0?(uint64_t)started:0;
    }
    if(initial) {
        char greeting[64];snprintf(greeting,sizeof(greeting),"RTBBS-PROBE %lu\r\n",s->core.id);
        session_impl_queue_text(s,greeting);
    }
    mmm_menu(s,1);mmm_prompt(s);
}

static int can_leave(const Session *s) {
    const MmmUi *m=mmm_view(s);
    return m->screen==MAIN_SCREEN && m->input==INPUT_NONE && !m->application.step;
}

static void cleanup(Session *s,int reason,int notify_revocation) {
    Language language=mmm_view(s)->language;
    if(reason!=2)chat_disconnect(s->core.id);
    OPENSSL_cleanse(mmm_state(s),sizeof(MmmUi));
    mmm_state(s)->language=language;
    if(reason==1 && notify_revocation)mmm_say(s,"Session revoked.\r\n","セッションが取り消されました。\r\n");
}

static void noop(Session *s) { (void)s; }
static void refresh_board_list(Session *s) {
    MmmUi *m=mmm_state(s);
    if(m->screen==NOTES_SCREEN || m->screen==INDEX_SCREEN)m->row_count=m->next=0;
    if(m->screen==NEW_SCREEN)m->row_count=0;
}
static int unsupported_line(Session *s,char *line,size_t size) { (void)s;(void)line;(void)size;return 0; }
static int unsupported_binary(Session *s,unsigned char ch,uint64_t now) { (void)s;(void)ch;(void)now;return -1; }
static void prepare(Session *s,OperationRequest *q) { q->board=s->core.board; }
static int preferences(const Session *s,OperationRequest *q) { (void)s;(void)q;return 0; }
static uint64_t signature(const Session *s) { (void)s;return 0; }
static void chat_kick(Session *s) {
    MmmUi *m=mmm_state(s);
    chat_leave(s->core.id);m->chatting=0;m->screen=MAIN_SCREEN;
    if(m->job==JOB_CHAT)m->chat_kicked=1;
    mmm_say(s,"You were removed from chat.\r\n","チャットから退出しました。\r\n");mmm_prompt(s);
}
static void ignored_mail(Session *s,uint32_t id,int deny,uint32_t rev) { (void)s;(void)id;(void)deny;(void)rev; }
static void mail_arrived(Session *s,uint32_t id) { (void)s;(void)id; }
static void transfer_finish(Session *s,int ok,unsigned id,uint64_t size,int recovery) {
    char text[152];
    if(ok)snprintf(text,sizeof(text),mmm_local(s,"Transfer complete: %u (%llu bytes).\r\n",
                                          "転送完了: %u (%llu バイト)。\r\n"),id,(unsigned long long)size);
    else if(recovery>0)snprintf(text,sizeof(text),"%s",message_text(mmm_view(s)->language,MSG_ZMODEM_RECOVERY));
    else snprintf(text,sizeof(text),"%s",message_text(mmm_view(s)->language,MSG_ZMODEM_FAILED));
    session_impl_queue_text(s,text);mmm_prompt(s);
}
static void transfer_receipt(Session *s,const char *text) {
    uint32_t id;
    if(s->connection.zmodem_receive==2 && batch_download_receipt(text,&id))batch_downloaded(&mmm_state(s)->files,id);
    session_impl_queue_text(s,text);
}
void door_finish(Session *s) { if(mmm_view(s)->screen==GAMES_SCREEN)mmm_games(s,0);mmm_prompt(s); }
static void tick(Session *s,uint64_t now) { (void)s;(void)now; }
static int keep_position(Session *s,const OperationResult *r) { (void)s;(void)r;return 0; }
static int uppercase(const Session *s,const char *line,size_t size) { (void)s;(void)line;(void)size;return 0; }

static void notify(Session *s,MessageId id,uint32_t number,const char *body) {
    char text[1024];const char *format=message_text(mmm_view(s)->language,id);
    if(id==MSG_SHUTDOWN_NOTICE)snprintf(text,sizeof(text),format,number,body?body:"");
    else if(id==MSG_ADMIN_NOTICE)snprintf(text,sizeof(text),format,body?body:"");
    else snprintf(text,sizeof(text),"%s",format);
    if(!session_secret(s))session_impl_queue_text(s,text);
}

static int notice(const Session *s,SessionNotice kind,unsigned number,uint64_t bytes,uint64_t total,
                  char *out,size_t capacity) {
    const char *text=NULL;
    switch(kind) {
    case SESSION_NOTICE_DOOR:text=mmm_local(s,"Game started. Ctrl-] returns to the game menu.\r\n",
                                          "ゲームを起動しました。Ctrl-] でゲーム一覧に戻ります。\r\n");break;
    case SESSION_NOTICE_PROGRESS: {
        int n=snprintf(out,capacity,mmm_local(s,"\r\nRegistering: %u%% (%llu / %llu bytes)\r\n",
                                         "\r\n登録中: %u%% (%llu / %llu バイト)\r\n"),
                       number,(unsigned long long)bytes,(unsigned long long)total);
        return n>=0 && (size_t)n<capacity;
    }
    case SESSION_NOTICE_IDLE_WARNING: text=message_text(mmm_view(s)->language,MSG_IDLE_WARNING);break;
    case SESSION_NOTICE_IDLE_CLOSED: text=message_text(mmm_view(s)->language,MSG_IDLE_CLOSED);break;
    case SESSION_NOTICE_TIME_WARNING: text=mmm_local(s,"Session time is nearly over.\r\n","接続時間の上限が近づいています。\r\n");break;
    case SESSION_NOTICE_TIME_GRACE: text=mmm_local(s,"Session limit reached.\r\n","接続時間の上限です。\r\n");break;
    default: return 0;
    }
    int n=snprintf(out,capacity,"%s",text);
    return n>=0 && (size_t)n<capacity;
}

const SessionUiAdapter *session_ui_mmm(void) {
    static const SessionUiAdapter adapter={
        .name="mmm",.state_size=sizeof(MmmUi),.login_required=1,.keys=keys,.key=key,
        .snapshot=snapshot,.cleanup=cleanup,.connector_line=unsupported_line,.binary_byte=unsupported_binary,
        .prepare_request=prepare,.preferences=preferences,.signature=signature,.scan_pause=noop,
        .scan_stop=noop,.board_refresh=refresh_board_list,.access_refresh=refresh_board_list,.chat_kick=chat_kick,.chat_event=mmm_chat_event,
        .announce=session_impl_queue_text,.notify=notify,.mail_arrived=mail_arrived,.mail_notify=ignored_mail,
        .transfer_receipt=transfer_receipt,.transfer_finish=transfer_finish,.notice=notice,
        .enter=enter,.leave=noop,.can_leave=can_leave,.line=line,.keep_position=keep_position,.result=result,
        .emit=session_impl_queue_text,.prompt=mmm_prompt,.menu=mmm_menu,.help=help,.uppercase=uppercase,.tick=tick,.door_finish=door_finish,
    };
    return &adapter;
}

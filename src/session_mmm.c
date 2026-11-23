#include "session_io.h"
#include "mmm_input.h"
#include "editor_core.h"
#include "editor_view.h"
#include "form_flow_core.h"
#include "members.h"
#include "profile_store.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { MAIN_SCREEN, NOTES_SCREEN, BOARD_SCREEN, INDEX_SCREEN, OPEN_SCREEN } MmmScreen;
typedef enum {
    INPUT_NONE, INPUT_LOGIN, INPUT_PASSWORD, INPUT_BOARD_PASSWORD, INPUT_TITLE,
    INPUT_BODY, INPUT_EDITOR, INPUT_NOTE, INPUT_RESPONSE, INPUT_FIND
} MmmPrompt;
typedef enum {
    JOB_NONE, JOB_AUTH, JOB_BOARDS, JOB_NAMED, JOB_BOARD, JOB_UNLOCK, JOB_INDEX,
    JOB_BROWSE, JOB_READ, JOB_SEEN, JOB_PREPARE, JOB_POST
} MmmJob;
typedef enum { OPEN_BOARD_MENU, OPEN_INDEX, OPEN_BASE, OPEN_POST } MmmBoardAction;
typedef enum { BROWSE_READ, BROWSE_INDEX, BROWSE_EXACT, BROWSE_RESPONSE, BROWSE_TITLES } MmmBrowse;

typedef struct {
    Language language;
    MmmScreen screen;
    MmmPrompt input;
    MmmJob job;
    MmmBoardAction action;
    MmmBrowse browse;
    ApplicationForm application;
    char auth_op, login[33];
    uint32_t parent, ancestors[1024];
    unsigned depth;
    uint32_t board_target, lock, target, next, page_start, rows[8];
    unsigned row_count;
    uint32_t root, article, reply_number, browse_root, number_seen, search_next;
    int descending, unread, search_responses;
    char search[81], board_title[81];
    uint32_t post_parent, insert_at;
    int numbered, undo;
    unsigned first_digit;
    char title[81], undo_title[81], body[CONTENT_BODY+1], undo_body[CONTENT_BODY+1];
} MmmUi;

static MmmUi *state(Session *s) { return s->ui_binding.state; }
static const MmmUi *view(const Session *s) { return s->ui_binding.state; }
static const char *local(const Session *s,const char *en,const char *ja) {
    return view(s)->language==LANGUAGE_JA?ja:en;
}
static void say(Session *s,const char *en,const char *ja) {
    session_impl_queue_text(s,local(s,en,ja));
}

static void prompt(Session *s) {
    const MmmUi *m=view(s);
    if(s->core.pending || s->core.closing || !s->core.active)return;
    if(m->application.step && m->input!=INPUT_PASSWORD) {
        const char *field=application_form_field(&m->application);
        if(!field) { say(s,"Apply? Y / E / /CANCEL > ","申請しますか Y / E / /CANCEL > ");return; }
        char text[96];
        snprintf(text,sizeof(text),"%s > ",field);
        session_impl_queue_text(s,text);
        return;
    }
    static const char *const inputs[]={
        NULL,"ID> ","Password> ","Board password> ","Title> ","Text> ",
        "Editor>> ","Base note #> ","Response number> ","Title search> "
    };
    if(m->input==INPUT_RESPONSE) {
        char text[32];
        snprintf(text,sizeof(text),"Response number> %u",m->first_digit);
        session_impl_queue_text(s,text);
        return;
    }
    if(m->input!=INPUT_NONE) { session_impl_queue_text(s,inputs[m->input]);return; }
    if(!s->core.user) { session_impl_queue_text(s,"LOGIN>> ");return; }
    static const char *const screens[]={"MAIN>> ","NOTES>> ","NOTES>> ","INDEX> ","OPEN> "};
    session_impl_queue_text(s,screens[m->screen]);
}

static void menu(Session *s,int requested) {
    (void)requested;
    if(!s->core.user) { say(s,"Login required. login ID / register ID / ?\r\n",
                              "ログインが必要です。login ID / register ID / ?\r\n");return; }
    switch(view(s)->screen) {
    case MAIN_SCREEN: say(s,"2 Notes   7 Exit   ? Help\r\n","2 ノート   7 終了   ? ヘルプ\r\n");break;
    case NOTES_SCREEN: say(s,"Number: select   L: more   .: up   /: main\r\n",
                             "番号で選択   L: 続き   .: 上へ   /: メイン\r\n");break;
    case BOARD_SCREEN: say(s,"1 Open   2 Post   3 Sub-boards   .: back\r\n",
                             "1 読む   2 投稿   3 子ボード   .: 戻る\r\n");break;
    case INDEX_SCREEN: say(s,"Number: open   Space/BS: page   f/F: find/continue   w: post   q: close\r\n",
                             "番号で開く   空白/BS: ページ   f/F: 検索/続行   w: 投稿   q: 閉じる\r\n");break;
    case OPEN_SCREEN: say(s,"Space/BS: response  +/-: base note  =/*: first/last  l/L: unread  f/F: find  w: reply  i: index  q: close\r\n",
                            "空白/BS: レス  +/-: ベース  =/*: 先頭/末尾  l/L: 未読  f/F: 検索  w: 返信  i: 索引  q: 閉じる\r\n");break;
    }
}

static void help(Session *s,const char *topic) {
    (void)topic;
    menu(s,1);
    if(s->core.user && (view(s)->screen==MAIN_SCREEN || view(s)->screen==NOTES_SCREEN))
        say(s,"notes, go BOARD, open [-i] [-bNUMBER] [BOARD], post [BOARD], logout, off, ui\r\n",
              "notes、go ボード、open [-i] [-b番号] [ボード]、post [ボード]、logout、off、ui\r\n");
}

static OperationRequest *request(Session *s,char op) {
    OperationRequest *q=calloc(1,sizeof(*q));
    if(!q) { say(s,"Request unavailable.\r\n","要求を処理できません。\r\n");return NULL; }
    q->op=op;
    q->board=s->core.board;
    return q;
}

static int submit(Session *s,OperationRequest *q,MmmJob job) {
    if(!q)return 0;
    SessionOperationStatus result=session_submit(s,q);
    OPENSSL_cleanse(q,sizeof(*q));
    free(q);
    if(result==SESSION_OPERATION_ACCEPTED) { state(s)->job=job;return 1; }
    if(result==SESSION_OPERATION_THROTTLED)
        say(s,"Try again shortly.\r\n","少し待って再試行してください。\r\n");
    else say(s,"Request unavailable.\r\n","要求を処理できません。\r\n");
    return 0;
}

static void boards(Session *s,uint32_t cursor) {
    OperationRequest *q=request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->action='B';q->number=view(s)->parent;q->cursor=cursor;
    if(submit(s,q,JOB_BOARDS))state(s)->screen=NOTES_SCREEN;
}

static void index_page(Session *s,uint32_t cursor) {
    OperationRequest *q=request(s,'O');
    if(!q)return;
    q->number=cursor;
    if(submit(s,q,JOB_INDEX))state(s)->page_start=cursor;
}

static void board(Session *s,uint32_t id) {
    OperationRequest *q=request(s,'j');
    if(!q)return;
    q->number=id;
    if(submit(s,q,JOB_BOARD))state(s)->board_target=id;
}

static void enter_board(Session *s,const char *name,MmmBoardAction action) {
    MmmUi *m=state(s);
    m->action=action;
    uint32_t id;
    if(!*name) { board(s,s->core.board);return; }
    if(mmm_number(*name=='#'?name+1:name,&id)) { board(s,id);return; }
    if(strlen(name)>80) { say(s,"Invalid board name.\r\n","ボード名が不正です。\r\n");return; }
    OperationRequest *q=request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->action='N';
    strcpy(q->title,name);
    submit(s,q,JOB_NAMED);
}

static void read_article(Session *s,uint32_t id) {
    OperationRequest *q=request(s,'R');
    if(!q)return;
    q->number=id;
    submit(s,q,JOB_READ);
}

static void continue_browse(Session *s,uint32_t cursor) {
    const MmmUi *m=view(s);
    OperationRequest *q=request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->number=m->browse_root;q->cursor=cursor;q->descending=m->descending;q->notes_unread=m->unread;
    if(m->search_responses || m->search_next)strcpy(q->title,m->search);
    submit(s,q,JOB_BROWSE);
}

static void browse(Session *s,MmmBrowse purpose,uint32_t root,uint32_t start,int down,int unread) {
    MmmUi *m=state(s);
    m->browse=purpose;m->browse_root=root;m->descending=down;m->unread=unread;
    m->number_seen=0;
    continue_browse(s,start);
}

static void read_search(Session *s,int response,uint32_t start) {
    MmmUi *m=state(s);
    m->search_responses=response;
    m->search_next=start?start:UINT32_MAX;
    browse(s,BROWSE_READ,response?m->root:0,start,1,0);
}

static void prepare_post(Session *s,uint32_t parent) {
    OperationRequest *q=request(s,'e');
    if(!q)return;
    q->number=parent;
    if(submit(s,q,JOB_PREPARE))state(s)->post_parent=parent;
}

static EditorDocument document(MmmUi *m) {
    return (EditorDocument){m->title,m->body,m->undo_title,m->undo_body,&m->undo};
}

static void discard_draft(MmmUi *m) {
    OPENSSL_cleanse(m->body,sizeof(m->body));OPENSSL_cleanse(m->title,sizeof(m->title));
    EditorDocument d=document(m);
    editor_discard_undo(&d);
}

static void editorial(Session *s,char *text) {
    MmmUi *m=state(s);
    EditorDocument d=document(m);
    if(m->input==INPUT_TITLE) {
        if(editor_apply(&d,EDITOR_SET_TITLE,0,0,text)==EDITOR_OK) {
            m->input=INPUT_BODY;m->insert_at=1;m->numbered=0;
        } else say(s,"Enter a title up to 80 bytes.\r\n","題名は80バイト以内です。\r\n");
        return;
    }
    if(m->input==INPUT_BODY) {
        if(!strcmp(text,"."))m->input=INPUT_EDITOR;
        else if(editor_apply(&d,EDITOR_INSERT,m->insert_at,0,text)==EDITOR_OK)m->insert_at++;
        else say(s,"Line or body limit.\r\n","入力行または本文の上限です。\r\n");
        if(m->input==INPUT_BODY && m->numbered) {
            char number[24];snprintf(number,sizeof(number),"%u: ",m->insert_at);
            session_impl_queue_text(s,number);
        }
        return;
    }
    if(!strcmp(text,"w")) {
        if(!*m->body) { say(s,"Enter a body before saving.\r\n","本文を入力してください。\r\n");return; }
        OperationRequest *q=request(s,'P');
        if(q) { q->number=m->post_parent;strcpy(q->title,m->title);strcpy(q->body,m->body);submit(s,q,JOB_POST); }
        return;
    }
    if(!strcmp(text,"q")) { discard_draft(m);m->input=INPUT_NONE;return; }
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
                    say(s,"Edit could not be applied.\r\n","編集を適用できませんでした。\r\n");
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
    say(s,"l/p [range], a/u, i[number], d[range], r[range] OLD NEW, w, q\r\n",
          "l/p [範囲]、a/u、i[行]、d[範囲]、r[範囲] 旧 新、w 保存、q 破棄\r\n");
}

static void send_password(Session *s,const char *text) {
    MmmUi *m=state(s);
    char op=m->input==INPUT_BOARD_PASSWORD?'(':m->auth_op;
    m->input=INPUT_NONE;
    if(!strcmp(text,"/CANCEL")) { application_form_clear(&m->application);return; }
    size_t n=strlen(text);
    if(n<8 || n>128) { say(s,"Password must be 8–128 bytes.\r\n","パスワードは8〜128バイトです。\r\n");return; }
    OperationRequest *q=request(s,op);
    if(!q)return;
    strcpy(q->password,text);
    if(op=='(') { q->action='U';q->number=m->board_target;q->cursor=m->lock; }
    else {
        strcpy(q->username,m->login);
        if(op=='g')strcpy(q->body,m->application.body);
        application_form_clear(&m->application);
    }
    submit(s,q,op=='('?JOB_UNLOCK:JOB_AUTH);
}

static void auth_start(Session *s,const char *name,char op) {
    MmmUi *m=state(s);
    if(!s->core.member_mode) { say(s,"The member service is disabled.\r\n","会員機能が無効です。\r\n");return; }
    if(!*name || strlen(name)>32 || strpbrk(name," \t")) {
        say(s,"Enter a valid account ID.\r\n","会員IDを入力してください。\r\n");return;
    }
    strcpy(m->login,name);
    m->auth_op=op;
    if(op=='g' && members_detailed_form())application_form_begin(&m->application);
    else m->input=INPUT_PASSWORD;
}

static void application_value(Session *s,const char *line) {
    MmmUi *m=state(s);
    const char *key=application_form_field(&m->application);
    int id=key?profile_field(key):-1;
    ApplicationInput kind=!strcmp(line,"/CANCEL")?APPLICATION_CANCEL:
                         (!strcmp(line,"y") || !strcmp(line,"Y"))?APPLICATION_CONFIRM:
                         (!strcmp(line,"e") || !strcmp(line,"E"))?APPLICATION_RESTART:APPLICATION_VALUE;
    ApplicationEffect result=application_form_input(&m->application,kind,line,
                                                     id<0 || profile_text_valid((unsigned)id,line));
    if(result==APPLICATION_PASSWORD)m->input=INPUT_PASSWORD;
    if(result==APPLICATION_REVIEW)session_impl_queue_text(s,m->application.body);
    if(result==APPLICATION_INVALID)say(s,"Invalid value.\r\n","入力内容が不正です。\r\n");
}

static void return_to_parent(Session *s) {
    MmmUi *m=state(s);
    m->row_count=0;m->next=0;
    if(m->screen==BOARD_SCREEN)boards(s,0);
    else if(m->depth) { m->parent=m->ancestors[--m->depth];boards(s,0); }
    else m->screen=MAIN_SCREEN;
}

static void subboards(Session *s,uint32_t parent) {
    MmmUi *m=state(s);
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
    } else say(s,"UI selection unavailable here.\r\n","ここではUIを切り替えられません。\r\n");
}

static void open_command(Session *s,char *arg) {
    MmmBoardAction action=OPEN_INDEX;
    while(*arg=='-') {
        char *end=strchr(arg,' ');
        if(end)*end++=0;
        if(!strcmp(arg,"-i")) {}
        else if(arg[1]=='b' && mmm_number(arg+2,&state(s)->target))action=OPEN_BASE;
        else { help(s,"open");return; }
        arg=end?end:arg+strlen(arg);
        while(*arg==' ')arg++;
    }
    enter_board(s,arg,action);
}

static void number_menu(Session *s,uint32_t n) {
    MmmUi *m=state(s);
    if(!s->core.user)return;
    if(m->screen==MAIN_SCREEN && n==2) { m->parent=0;m->depth=0;boards(s,0); }
    else if(m->screen==MAIN_SCREEN && n==7)session_core_close(&s->core);
    else if(m->screen==NOTES_SCREEN && n<=m->row_count) { m->action=OPEN_BOARD_MENU;board(s,m->rows[n-1]); }
    else if(m->screen==BOARD_SCREEN && n==1)index_page(s,0);
    else if(m->screen==BOARD_SCREEN && n==2)prepare_post(s,0);
    else if(m->screen==BOARD_SCREEN && n==3)subboards(s,s->core.board);
    else say(s,"Choose a listed number.\r\n","一覧にある番号を入力してください。\r\n");
}

static void command(Session *s,char *text) {
    MmmUi *m=state(s);
    MmmInput input=mmm_parse(text,s->core.user!=0);
    if(input.usage && input.command>MMM_AMBIGUOUS) {
        const char *name,*usage;
        mmm_command_help(input.command,&name,&usage);
        session_impl_queue_text(s,usage);session_impl_queue_text(s,"\r\n");return;
    }
    if(s->core.user && (m->screen==NOTES_SCREEN) && (!strcmp(text,"L") || !strcmp(text,"l"))) {
        if(m->next)boards(s,m->next);
        else say(s,"End of boards.\r\n","ボード一覧の末尾です。\r\n");
        return;
    }
    switch(input.command) {
    case MMM_MENU: if(m->screen==NOTES_SCREEN)boards(s,0);else menu(s,1);break;
    case MMM_HELP: help(s,"");break;
    case MMM_HOME:
        m->screen=MAIN_SCREEN;m->parent=0;m->depth=0;menu(s,1);break;
    case MMM_UP: return_to_parent(s);break;
    case MMM_NUMBER: number_menu(s,input.number);break;
    case MMM_LOGIN: case MMM_REGISTER:
        if(s->core.user) { say(s,"Log out first.\r\n","先にログアウトしてください。\r\n");break; }
        if(input.command==MMM_LOGIN && !*input.argument)m->input=INPUT_LOGIN;
        else auth_start(s,input.argument,input.command==MMM_REGISTER?'g':'a');
        break;
    case MMM_OFF: session_core_close(&s->core);break;
    case MMM_LOGOUT:
        if(session_logout(s)) { m->screen=MAIN_SCREEN;m->parent=0;m->depth=0;menu(s,1); }
        break;
    case MMM_NOTES: m->parent=0;m->depth=0;boards(s,0);break;
    case MMM_GO: if(*input.argument)enter_board(s,input.argument,OPEN_BOARD_MENU);else help(s,"go");break;
    case MMM_OPEN: open_command(s,input.argument);break;
    case MMM_POST: enter_board(s,input.argument,OPEN_POST);break;
    case MMM_UI: choose_profile(s,input.argument);break;
    case MMM_AMBIGUOUS: say(s,"Ambiguous command.\r\n","コマンドの省略が曖昧です。\r\n");break;
    default:
        if(!s->core.user)menu(s,1);
        else say(s,"Unknown command. Use ?.\r\n","不明なコマンドです。? で案内を表示できます。\r\n");
    }
}

static void line(Session *s,char *text,size_t size) {
    (void)size;
    MmmUi *m=state(s);
    switch(m->input) {
    case INPUT_PASSWORD: case INPUT_BOARD_PASSWORD: send_password(s,text);break;
    case INPUT_LOGIN: m->input=INPUT_NONE;auth_start(s,text,'a');break;
    case INPUT_TITLE: case INPUT_BODY: case INPUT_EDITOR: editorial(s,text);break;
    case INPUT_NOTE: case INPUT_RESPONSE: {
        MmmPrompt input=m->input;m->input=INPUT_NONE;
        char joined[16];
        if(input==INPUT_RESPONSE) {
            if(strlen(text)>9) { say(s,"Invalid number.\r\n","番号が不正です。\r\n");break; }
            snprintf(joined,sizeof(joined),"%u%s",m->first_digit,text);
            text=joined;
        }
        if(!mmm_number(text,&m->target)) { say(s,"Invalid number.\r\n","番号が不正です。\r\n");break; }
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
            else say(s,"No earlier notes.\r\n","これより前にノートはありません。\r\n");
        }
        break;
    default:
        if(m->application.step)application_value(s,text);
        else command(s,text);
        break;
    }
    if(s->ui_binding.state==m)prompt(s);
}

static int keys(const Session *s) {
    const MmmUi *m=view(s);
    return s->core.user && m->input==INPUT_NONE &&
           (m->screen==INDEX_SCREEN || m->screen==OPEN_SCREEN);
}

static void key(Session *s,unsigned char ch) {
    MmmUi *m=state(s);
    int index=m->screen==INDEX_SCREEN;
    if(ch=='f' || (!index && ch=='\'')) {
        m->search_responses=ch=='\'';m->input=INPUT_FIND;
    } else if(ch=='F' || (!index && ch=='"')) {
        if(m->search_next && m->search_responses==(ch=='"'))
            read_search(s,m->search_responses,m->search_next);
        else say(s,"No further title matches.\r\n","これ以上一致する題名はありません。\r\n");
    } else {
        m->search_next=0;m->search_responses=0;
        if(ch=='q' || ch==4) { m->screen=BOARD_SCREEN;menu(s,1); }
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
    prompt(s);
}

static void render_boards(Session *s,const ArticleBoardsPage *page) {
    MmmUi *m=state(s);
    m->row_count=page->count;m->next=page->next;
    if(!page->count)say(s,"No boards here.\r\n","ここに表示できるボードはありません。\r\n");
    for(unsigned i=0;i<page->count;i++) {
        const ArticleBoardRow *row=&page->rows[i];
        m->rows[i]=row->id;
        char text[128];snprintf(text,sizeof(text),"%u  #%u  %s\r\n",i+1,row->id,row->title);
        session_impl_queue_text(s,text);
    }
    menu(s,1);
}

static void render_index(Session *s,const ArticleNotesPage *page) {
    MmmUi *m=state(s);
    m->screen=INDEX_SCREEN;m->row_count=page->count;m->next=page->next;
    say(s,"Base notes\r\n","ベースノート\r\n");
    for(unsigned i=0;i<page->count;i++) {
        const ArticleNoteRow *row=&page->rows[i];m->rows[i]=row->summary.id;
        char text[200];snprintf(text,sizeof(text),"%u  #%u  %s  (%u)  %s\r\n",
                                i+1,row->summary.id,row->author,row->replies,row->summary.title);
        session_impl_queue_text(s,text);
    }
}

static void browse_result(Session *s,const ArticlePage *page) {
    MmmUi *m=state(s);
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
    say(s,"No matching note.\r\n","該当するノートがありません。\r\n");
}

static void result(Session *s,const OperationResult *r,SessionResultView projection,uint64_t now,int cancelled) {
    (void)now;(void)cancelled;
    MmmUi *m=state(s);
    MmmJob job=m->job;m->job=JOB_NONE;
    if(r->auth>0 && projection.status==SESSION_RESULT_OK) {
        if(r->preferences)m->language=(Language)r->language;
        char message[88];snprintf(message,sizeof(message),"Logged in: %s\r\n",s->core.username);
        session_impl_queue_text(s,message);
        m->screen=MAIN_SCREEN;menu(s,1);prompt(s);return;
    }
    if(projection.status!=SESSION_RESULT_OK) {
        switch(job) {
        case JOB_AUTH: case JOB_UNLOCK: say(s,"Authentication or registration denied.\r\n","認証または登録が拒否されました。\r\n");break;
        default: say(s,"The operation is unavailable.\r\n","この操作は利用できません。\r\n");break;
        }
        prompt(s);return;
    }
    if(r->feedback.ui_profile_saved)
        say(s,"UI preference saved.\r\n","UIの希望設定を保存しました。\r\n");
    const OperationPage *page=projection.page;
    switch(job) {
    case JOB_AUTH: say(s,"Registration request received.\r\n","登録申請を受け付けました。\r\n");break;
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
                menu(s,1);
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
            OperationRequest *q=request(s,'s');
            if(q) { q->number=c->id;submit(s,q,JOB_SEEN); }
        } else say(s,"Note is unavailable.\r\n","記事を表示できません。\r\n");
        break;
    case JOB_PREPARE: discard_draft(m);m->input=INPUT_TITLE;break;
    case JOB_POST: {
        char text[48];snprintf(text,sizeof(text),"Saved #%u\r\n",s->core.current);
        session_impl_queue_text(s,text);discard_draft(m);m->input=INPUT_NONE;
        if(m->screen==INDEX_SCREEN)index_page(s,m->page_start);
        break;
    }
    default: break;
    }
    prompt(s);
}

static SessionUiSnapshot snapshot(const Session *s) {
    const MmmUi *m=view(s);
    return (SessionUiSnapshot){.language=m->language,.menu=1,
        .secret=m->input==INPUT_PASSWORD || m->input==INPUT_BOARD_PASSWORD,
        .editing=m->input>=INPUT_TITLE && m->input<=INPUT_EDITOR};
}

static void enter(Session *s,int initial,int interactive,Language language) {
    (void)interactive;
    MmmUi *m=state(s);
    m->language=language;m->screen=MAIN_SCREEN;
    s->core.structured=1;
    if(initial) {
        char greeting[64];snprintf(greeting,sizeof(greeting),"RTBBS-PROBE %lu\r\n",s->core.id);
        session_impl_queue_text(s,greeting);
    }
    menu(s,1);prompt(s);
}

static int can_leave(const Session *s) {
    const MmmUi *m=view(s);
    return m->screen==MAIN_SCREEN && m->input==INPUT_NONE && !m->application.step;
}

static void cleanup(Session *s,int reason,int notify_revocation) {
    Language language=view(s)->language;
    OPENSSL_cleanse(state(s),sizeof(MmmUi));
    state(s)->language=language;
    if(reason==1 && notify_revocation)say(s,"Session revoked.\r\n","セッションが取り消されました。\r\n");
}

static void noop(Session *s) { (void)s; }
static void refresh_board_list(Session *s) {
    MmmUi *m=state(s);
    if(m->screen==NOTES_SCREEN || m->screen==INDEX_SCREEN)m->row_count=m->next=0;
}
static int unsupported_line(Session *s,char *line,size_t size) { (void)s;(void)line;(void)size;return 0; }
static int unsupported_binary(Session *s,unsigned char ch,uint64_t now) { (void)s;(void)ch;(void)now;return -1; }
static void prepare(Session *s,OperationRequest *q) { q->board=s->core.board; }
static int preferences(const Session *s,OperationRequest *q) { (void)s;(void)q;return 0; }
static uint64_t signature(const Session *s) { (void)s;return 0; }
static int ignored_chat(Session *s,const ChatEvent *event) { (void)s;(void)event;return 1; }
static void ignored_mail(Session *s,uint32_t id,int deny,uint32_t rev) { (void)s;(void)id;(void)deny;(void)rev; }
static void mail_arrived(Session *s,uint32_t id) { (void)s;(void)id; }
static void transfer_finish(Session *s,int ok,unsigned id,uint64_t size,int recovery) {
    (void)s;(void)ok;(void)id;(void)size;(void)recovery;
}
static void tick(Session *s,uint64_t now) { (void)s;(void)now; }
static int keep_position(Session *s,const OperationResult *r) { (void)s;(void)r;return 0; }
static int uppercase(const Session *s,const char *line,size_t size) { (void)s;(void)line;(void)size;return 0; }

static void notify(Session *s,MessageId id,uint32_t number,const char *body) {
    char text[1024];const char *format=message_text(view(s)->language,id);
    if(id==MSG_SHUTDOWN_NOTICE)snprintf(text,sizeof(text),format,number,body?body:"");
    else if(id==MSG_ADMIN_NOTICE)snprintf(text,sizeof(text),format,body?body:"");
    else snprintf(text,sizeof(text),"%s",format);
    if(!session_secret(s))session_impl_queue_text(s,text);
}

static int notice(const Session *s,SessionNotice kind,unsigned number,uint64_t bytes,uint64_t total,
                  char *out,size_t capacity) {
    (void)number;(void)bytes;(void)total;
    const char *text=NULL;
    switch(kind) {
    case SESSION_NOTICE_IDLE_WARNING: text=message_text(view(s)->language,MSG_IDLE_WARNING);break;
    case SESSION_NOTICE_IDLE_CLOSED: text=message_text(view(s)->language,MSG_IDLE_CLOSED);break;
    case SESSION_NOTICE_TIME_WARNING: text=local(s,"Session time is nearly over.\r\n","接続時間の上限が近づいています。\r\n");break;
    case SESSION_NOTICE_TIME_GRACE: text=local(s,"Session limit reached.\r\n","接続時間の上限です。\r\n");break;
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
        .scan_stop=noop,.board_refresh=refresh_board_list,.access_refresh=refresh_board_list,.chat_kick=noop,.chat_event=ignored_chat,
        .announce=session_impl_queue_text,.notify=notify,.mail_arrived=mail_arrived,.mail_notify=ignored_mail,
        .transfer_receipt=session_impl_queue_text,.transfer_finish=transfer_finish,.notice=notice,
        .enter=enter,.leave=noop,.can_leave=can_leave,.line=line,.keep_position=keep_position,.result=result,
        .emit=session_impl_queue_text,.prompt=prompt,.menu=menu,.help=help,.uppercase=uppercase,.tick=tick,.door_finish=prompt,
    };
    return &adapter;
}

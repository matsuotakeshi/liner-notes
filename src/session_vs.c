#define _POSIX_C_SOURCE 200809L
#include "session_vs.h"
#include "vs_core.h"
#include "net_cock_core.h"
#include "mmm_input.h"
#include "date_filter.h"
#include "editor_core.h"
#include "editor_view.h"
#include "store_limits.h"
#include "vault_bridge.h"
#include "chat.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VsUi *vs(Session *s) { return s->ui_binding.state; }
static const VsUi *vs_view(const Session *s) { return s->ui_binding.state; }
static int key(VsCommand c,const char *text) { return !strcmp(c.key,text); }
static void bad(Session *s) { mmm_bad_service_command(s); }

static int setting(Session *s,VsCoreAction action,uint32_t id,int scope,uint32_t cursor,uint64_t stamp) {
    OperationRequest *q=mmm_request(s,BOARD_VS);
    if(!q)return 0;
    q->action=action;q->number=id;q->scope=scope;q->cursor=cursor;q->until=stamp;
    return mmm_submit(s,q,JOB_VS_SETTING);
}

void vs_enter(Session *s) {
    VsUi *u=vs(s);
    u->restore_mode=1;u->input=VS_IDLE;
}

int vs_ready(const Session *s) {
    const VsUi *u=vs_view(s);
    return u->input==VS_IDLE && !u->scan && !u->posting && u->direct.kind==VS_DIRECT_INVALID;
}

void vs_menu(Session *s,int requested) {
    const VsUi *u=vs_view(s);
    const MmmUi *m=&u->common;
    if(!s->core.user) { mmm_say(s,"VS style | LOGIN ID / REGISTER ID\r\n","VS風 | LOGIN 会員ID / REGISTER 入会\r\n");return; }
    if(u->quiet && !requested)return;
    switch(u->input) {
    case VS_DIRECT_MENU: session_impl_queue_text(s,"W post / R reply / P profile / M mail / J unavailable / Q\r\n");return;
    case VS_MODE_MENU: session_impl_queue_text(s,"B board preferences / P profile / T ON|OFF telegrams / PROTOCOL / Q\r\n");return;
    case VS_MODE_NEWS: session_impl_queue_text(s,"N news order / Q back\r\n");return;
    case VS_POINTER_MENU: session_impl_queue_text(s,"T first / B newest / L last login / D date / # status / Q\r\n");return;
    case VS_FINDER: session_impl_queue_text(s,"S filename / Q return\r\n");return;
    case VS_BATCH: session_impl_queue_text(s,"L list / A ID / D ID / C clear / X download / Q\r\n");return;
    default:break;
    }
    switch(m->screen) {
    case MAIN_SCREEN:
        session_ui_menu_hint(s);
        mmm_extensions_hint(s);
        mmm_say(s,"N news / NA continuous / NF files / P pointer / B boards / F files / M mail / D direct / C chat / U settings / H help / O off\r\n",
                  "N ニュース / NA 一気読み / NF 新着ファイル / P ポインタ / B 掲示板 / F ファイル / M メール / D ダイレクト / C チャット / U 設定 / H 案内 / O 終了\r\n");break;
    case NOTES_SCREEN: session_impl_queue_text(s,"#ID/name / . up / + list / * descendants / A ID ON|OFF / Q\r\n");break;
    case BOARD_SCREEN: session_impl_queue_text(s,"R [ID] / L [ID|DATE|BY|TITLE|BASE] / W / N / NA / P / # / A ON|OFF / . / Q\r\n");break;
    case OPEN_SCREEN: session_impl_queue_text(s,"Enter / B / +/- / ID / S related / R reply / P profile / K delete / J unavailable / Q\r\n");break;
    case NEW_SCREEN: session_impl_queue_text(s,"Enter read / A continuous / S skip board / T base / B previous / Q stop\r\n");break;
    case MAIL_SCREEN: session_impl_queue_text(s,"R [ID] / W USER / L / K / B attachments / Q\r\n");break;
    case FILER_SCREEN: session_impl_queue_text(s,"F finder / B batch / L / R ID / XD YD GD ZD MD / XU YU GU ZU MU / Q\r\n");break;
    case CHAT_SCREEN: session_impl_queue_text(s,"/T telegram / /W online / /M memo / /S call / // exit\r\n");break;
    case MANAGE_SCREEN: mmm_manage_menu(s);break;
    case GAMES_SCREEN: session_impl_queue_text(s,"Number: start / Q: exit\r\n");break;
    case TALK_SCREEN: mmm_conference_menu(s);break;
    default:break;
    }
}

void vs_help(Session *s,const char *topic) {
    (void)topic;
    vs_menu(s,1);
    mmm_say(s,"Jump replies (J/DJ) are unavailable. Extensions: TOPIC / TALK / DRAFT / GAMES / MANAGE / UI.\r\n",
              "ジャンプレス (J/DJ) は未対応です。拡張: TOPIC / TALK / DRAFT / GAMES / MANAGE / UI。\r\n");
}

int vs_prompt(Session *s) {
    const VsUi *u=vs_view(s);
    const MmmUi *m=&u->common;
    if(m->input==INPUT_EDITOR) {
        session_impl_queue_text(s,u->input==VS_EDITOR_MENU?"Submit (Y/A/L/N)> ":"Edit> ");return 1;
    }
    if(m->input!=INPUT_NONE || m->screen==TALK_SCREEN)return 0;
    if(u->restore_mode || u->automatic)return 1;
    if(!s->core.user) { session_impl_queue_text(s,"VS ID> ");return 1; }
    static const char *const input[]={
        [VS_DIRECT_MENU]="Direct> ",[VS_POST_BOARD]="Board ID> ",[VS_POST_PARENT]="Reply ID> ",
        [VS_READ_START]="Read ID (Enter latest)> ",[VS_MODE_MENU]="User> ",[VS_MODE_NEWS]="Preferences> ",
        [VS_POINTER_MENU]="Pointer> ",[VS_POINTER_DATE]="Date> ",[VS_WITHDRAW_CONFIRM]="Withdraw (y/N)> ",
        [VS_FINDER]="Finder> ",[VS_BATCH]="Batch> "
    };
    if(u->input!=VS_IDLE && u->input<=VS_BATCH && input[u->input]) {
        session_impl_queue_text(s,input[u->input]);return 1;
    }
    const char *prompt="VS> ";
    switch(m->screen) {
    case NOTES_SCREEN:prompt="Tree> ";break;
    case BOARD_SCREEN:prompt="Message> ";break;
    case OPEN_SCREEN:prompt=u->read_related?"Related> ":"Read> ";break;
    case NEW_SCREEN:prompt=u->await_start?"News board> ":"News> ";break;
    case MAIL_SCREEN:prompt="Mail> ";break;
    case FILER_SCREEN:prompt="Files> ";break;
    case CHAT_SCREEN:prompt="Chat> ";break;
    case GAMES_SCREEN:prompt="Games> ";break;
    case MANAGE_SCREEN:prompt="Manage> ";break;
    default:break;
    }
    session_impl_queue_text(s,prompt);return 1;
}

static void stop_scan(VsUi *u) {
    u->scan=u->automatic=u->await_start=u->next_display=0;
}

static void home(Session *s) {
    VsUi *u=vs(s);stop_scan(u);
    u->common.screen=MAIN_SCREEN;u->input=VS_IDLE;u->posting=0;u->direct=(VsDirect){0};
}

static int select_board(Session *s,const char *target) {
    uint32_t id;
    if(mmm_number(*target=='#'?target+1:target,&id)) {
        char board[16];snprintf(board,sizeof(board),"#%u",id);
        mmm_ui_enter_board(s,board,OPEN_BOARD_MENU);
        return s->core.pending!=0;
    }
    if(!*target || !mmm_ui_text_valid(target,80)) { bad(s);return 0; }
    OperationRequest *q=mmm_request(s,BOARD_NET_COCK);
    if(!q)return 0;
    q->action=NET_LOOKUP;strcpy(q->title,target);
    return mmm_submit(s,q,JOB_VS_LOOKUP);
}

static void open_post_board(Session *s) {
    VsUi *u=vs(s);
    u->posting=1;u->input=VS_IDLE;
    if(!select_board(s,u->direct.board)) {
        u->posting=0;u->direct=(VsDirect){0};
    }
}

static void read_body(Session *s,uint32_t id) {
    OperationRequest *q=mmm_request(s,'Z');
    if(q) { q->action=5;q->number=id;q->scope=1;mmm_submit(s,q,JOB_VS_BODY); }
}

static void page(Session *s,VsPageMode mode,uint32_t root,uint32_t cursor,int backwards,int filter) {
    VsUi *u=vs(s);
    OperationRequest *q=mmm_request(s,BOARD_VS);
    if(!q)return;
    q->action=VS_PAGE;q->scope=mode==VS_GROUP;q->number=root;q->cursor=cursor;q->descending=backwards;
    if(filter) {
        q->since=u->since;
        q->until=u->scan && u->until?u->until-1:u->until;
    }
    if(mmm_submit(s,q,JOB_VS_PAGE)) { u->page=mode;u->page_backwards=backwards; }
}

static void start_targets(Session *s,uint32_t scope,int automatic,int list_only) {
    VsUi *u=vs(s);
    if(setting(s,VS_TARGETS,scope,list_only,0,0)) {
        u->scope=scope;u->scan=!list_only;u->automatic=automatic;u->manual_list=list_only;
        u->board_at=u->board_count=u->targets_next=0;
    }
}

static void next_board(Session *s) {
    VsUi *u=vs(s);
    if(u->board_at<u->board_count) {
        char id[16];snprintf(id,sizeof(id),"#%u",u->boards[u->board_at++]);select_board(s,id);
    } else if(u->targets_next)setting(s,VS_TARGETS,u->scope,0,u->targets_next,0);
    else {
        home(s);mmm_say(s,"News complete.\r\n","ニュース巡回を終了しました。\r\n");
    }
}

static void news_next(Session *s) {
    VsUi *u=vs(s);
    if(u->related) {
        if(u->group_root)page(s,VS_SELECT,u->group_root,u->continuation,0,1);
        else page(s,VS_GROUP,0,u->root_cursor,0,0);
    } else page(s,VS_SELECT,0,u->continuation,0,1);
}

void vs_board(Session *s) {
    VsUi *u=vs(s);
    u->current=u->root=u->selector_root=u->group_root=u->root_cursor=u->continuation=0;
    u->read_related=u->descending=0;u->input=VS_IDLE;
    u->news_current=u->news_previous=0;
    if(u->posting) {
        u->common.screen=BOARD_SCREEN;
        mmm_ui_prepare_post(s,u->direct.parent);
        if(!s->core.pending) { u->posting=0;u->direct=(VsDirect){0}; }
    } else if(u->scan) {
        u->common.screen=NEW_SCREEN;
        setting(s,VS_WINDOW,s->core.board,0,0,0);
    } else {
        u->common.screen=BOARD_SCREEN;
        session_impl_queue_text(s,u->common.board_title);session_impl_queue_text(s,"\r\n");
        vs_menu(s,0);
    }
}

static void exhausted(Session *s) {
    VsUi *u=vs(s);
    if(!u->scan) { mmm_say(s,"No more visible messages.\r\n","表示できる記事はありません。\r\n");return; }
    if(u->related && u->group_root) {
        u->root_cursor=u->group_root+1;u->group_root=u->continuation=0;news_next(s);return;
    }
    if(!setting(s,VS_COMMIT,s->core.board,0,0,u->until))home(s);
}

static void page_result(Session *s,const ArticlePage *p) {
    VsUi *u=vs(s);
    if(u->page==VS_LIST) {
        for(unsigned i=0;i<p->count;i++) {
            char text[182];const ArticleSummary *row=&p->rows[i];
            snprintf(text,sizeof(text),"#%u %s  %s\r\n",row->id,row->author,row->title);
            session_impl_queue_text(s,text);
        }
        if(!p->count && !p->next)mmm_say(s,"No matching entries.\r\n","該当する記事はありません。\r\n");
        u->common.next=u->filtered_list?0:p->next;
        return;
    }
    if(!p->count) {
        if(p->next) {
            if(u->page==VS_GROUP)u->root_cursor=p->next;
            else if(u->scan)u->continuation=p->next;
            page(s,u->page,u->scan?u->group_root:u->selector_root,p->next,u->page_backwards,u->scan && u->page!=VS_GROUP);
        } else exhausted(s);
        return;
    }
    if(u->page==VS_GROUP) {
        u->group_root=p->rows[0].id;u->continuation=0;news_next(s);
    } else {
        uint32_t id=p->rows[0].id;
        if(u->scan) {
            u->continuation=id+1;
            u->news_previous=u->news_current;u->news_current=id;
        }
        read_body(s,id);
    }
}

static void jump(Session *s) {
    VsUi *u=vs(s);
    u->direct=(VsDirect){0};u->posting=0;
    mmm_say(s,"Jump replies are unavailable.\r\n","ジャンプレスは未対応です。\r\n");
}

static void start_post(Session *s,uint32_t parent) {
    VsUi *u=vs(s);
    u->posting=1;u->input=VS_IDLE;
    mmm_ui_prepare_post(s,parent);
    if(!s->core.pending)u->posting=0;
}

static void edit_list(Session *s,int numbered) {
    MmmUi *m=&vs(s)->common;
    EditorDocument d={m->title,m->body,m->undo_title,m->undo_body,&m->undo};
    EditorInfo info;
    if(editor_inspect(&d,&info)!=EDITOR_OK || !info.lines)return;
    char *out=malloc(CONTENT_DISPLAY);
    if(out) {
        if(editor_view_lines(&d,1,info.lines,numbered,out,CONTENT_DISPLAY)==EDITOR_VIEW_OK)
            session_impl_queue_text(s,out);
        OPENSSL_cleanse(out,CONTENT_DISPLAY);free(out);
    }
}

int vs_editor(Session *s,char *line) {
    VsUi *u=vs(s);MmmUi *m=&u->common;
    EditorDocument d={m->title,m->body,m->undo_title,m->undo_body,&m->undo};
    if(m->input==INPUT_TITLE)return 0;
    if(m->input==INPUT_EDITOR && u->input==VS_IDLE && !strcmp(line,"q")) {
        u->posting=0;return 0;
    }
    if(u->input==VS_EDITOR_REPLACE) {
        if(editor_apply(&d,EDITOR_REPLACE,(uint32_t)u->edit_line,0,line)!=EDITOR_OK)bad(s);
        u->input=VS_IDLE;return 1;
    }
    if(u->input==VS_EDITOR_MENU) {
        VsCommand c=vs_command(line);
        if(key(c,"L"))edit_list(s,0);
        else if(key(c,"A")) { u->input=VS_IDLE;mmm_ui_editor(s,"u"); }
        else if(key(c,"Y") || !*line) { u->input=VS_IDLE;mmm_ui_editor(s,"w"); }
        else if(key(c,"N") || key(c,"Q")) { u->input=VS_IDLE;u->posting=0;mmm_ui_editor(s,"q"); }
        else bad(s);
        return 1;
    }
    if(*line!='/')return 0;
    char *parameter=strchr(line,':');
    if(parameter)*parameter++=0;
    VsCommand c=vs_command(line);
    if(!parameter)parameter=c.argument;
    MmmPrompt previous=m->input;
    m->input=INPUT_EDITOR;
    if(key(c,"//"))u->input=VS_EDITOR_MENU;
    else if(key(c,"/ES") || key(c,"/POST"))mmm_ui_editor(s,"w");
    else if(key(c,"/ABT") || key(c,"/CAN")) { u->posting=0;mmm_ui_editor(s,"q"); }
    else if(key(c,"/A"))mmm_ui_editor(s,"u");
    else if(key(c,"/TITLE")) {
        if(editor_apply(&d,EDITOR_SET_TITLE,0,0,parameter)!=EDITOR_OK)bad(s);
    } else if(key(c,"/CLR") || key(c,"/NEW")) {
        if(editor_apply(&d,EDITOR_CLEAR,0,0,NULL)==EDITOR_OK)m->insert_at=1;
        else bad(s);
    } else if(key(c,"/L") || key(c,"/LN")) {
        edit_list(s,key(c,"/LN"));m->input=previous;
    } else if(key(c,"/I") || key(c,"/O") || key(c,"/D")) {
        EditorInfo bounds;uint32_t row=m->insert_at?m->insert_at:1;
        if(editor_inspect(&d,&bounds)!=EDITOR_OK || (*parameter && !mmm_number(parameter,&row)))bad(s);
        else if(key(c,"/I") && row<=bounds.lines+1) { m->insert_at=row;m->input=INPUT_BODY; }
        else if(key(c,"/O") && row<=bounds.lines) { u->edit_line=(int)row;u->input=VS_EDITOR_REPLACE; }
        else if(key(c,"/D") && editor_apply(&d,EDITOR_DELETE,row,row,NULL)==EDITOR_OK) { m->insert_at=row; }
        else bad(s);
    } else {
        mmm_say(s,"Editor: /POST /CAN /CLR /L /LN /I:line /O:line /D:line /TITLE:title //\r\n",
                  "編集: /POST /CAN /CLR /L /LN /I:行 /O:行 /D:行 /TITLE:題名 //\r\n");
        m->input=previous;
    }
    return 1;
}

static int parse_date(char *text,uint64_t *stamp) {
    char day[11];
    if(strlen(text)!=10)return 0;
    for(unsigned i=0;i<10;i++)day[i]=text[i]=='/'?'-':text[i];
    day[10]=0;return date_filter(day,stamp);
}

static void pointer(Session *s,VsCommand c) {
    VsUi *u=vs(s);
    uint64_t stamp=0;
    if(u->input==VS_POINTER_DATE) {
        if(!parse_date(c.key,&stamp) || *c.argument) { bad(s);return; }
    } else if(key(c,"T"))stamp=0;
    else if(key(c,"B")) { time_t now=time(NULL);if(now<0)return;stamp=(uint64_t)now; }
    else if(key(c,"L"))stamp=s->core.previous_login;
    else if(key(c,"D")) {
        if(!*c.argument) { u->input=VS_POINTER_DATE;return; }
        if(!parse_date(c.argument,&stamp)) { bad(s);return; }
    } else if(key(c,"#")) { setting(s,VS_WINDOW,u->position_board,0,0,0);return; }
    else if(key(c,"Q")) { u->input=VS_IDLE;return; }
    else { bad(s);return; }
    u->input=VS_POINTER_MENU;
    setting(s,VS_POINTER,u->position_board,u->common.screen==NOTES_SCREEN,0,stamp);
}

static void read_command(Session *s,VsCommand c) {
    VsUi *u=vs(s);
    uint32_t id;
    if(key(c,"Q")) {
        if(u->scan)home(s);
        else if(u->read_related) { u->read_related=0;u->selector_root=0; }
        else u->common.screen=BOARD_SCREEN;
        return;
    }
    if(key(c,"J") || key(c,"DJ")) { jump(s);return; }
    if(u->scan && (u->await_start || key(c,"A") || !*c.key || key(c,"S"))) {
        if(key(c,"S")) { u->await_start=0;u->next_display=0;next_board(s);return; }
        if(key(c,"A"))u->automatic=1;
        else if(*c.key) { bad(s);return; }
        u->await_start=0;news_next(s);return;
    }
    if(key(c,"R") && u->current) { start_post(s,u->current);return; }
    if(key(c,"P") && *u->author) { mmm_profile(s,u->author);return; }
    if(key(c,"K") && u->current) { u->input=VS_WITHDRAW_CONFIRM;return; }
    if(u->scan) {
        if(key(c,"T") && u->related && u->root)read_body(s,u->root);
        else if(key(c,"B") && u->news_previous)read_body(s,u->news_previous);
        else bad(s);
        return;
    }
    if(key(c,"S") && u->root) {
        u->read_related=1;u->selector_root=u->root;
        page(s,VS_SELECT,u->selector_root,0,0,0);return;
    }
    if(key(c,"T") && u->read_related) { page(s,VS_SELECT,u->selector_root,0,0,0);return; }
    if(key(c,"+") || key(c,"-")) { u->descending=key(c,"-");return; }
    if(!*c.key || key(c,"B")) {
        int backwards=key(c,"B")?!u->descending:u->descending;
        if(backwards && u->current==1) { mmm_say(s,"Start of selection.\r\n","選択範囲の先頭です。\r\n");return; }
        uint32_t cursor=u->current?(backwards?u->current-1:u->current+1):0;
        page(s,VS_SELECT,u->selector_root,cursor,backwards,0);return;
    }
    if(!*c.argument && mmm_number(c.key,&id) && !u->read_related) { read_body(s,id);return; }
    bad(s);
}

static void titles(Session *s,char *arg) {
    VsUi *u=vs(s);
    VsCommand filter=vs_command(arg);
    uint32_t id;
    u->filtered_list=1;
    if(!*arg || mmm_number(arg,&id)) {
        u->filtered_list=0;
        page(s,VS_LIST,0,*arg?id:0,!*arg,0);return;
    }
    if(key(filter,"BASE") && !*filter.argument) {
        OperationRequest *q=mmm_request(s,BOARD_VS);
        if(q) { q->action=VS_PAGE;q->scope=1;if(mmm_submit(s,q,JOB_VS_PAGE))u->page=VS_LIST; }
        return;
    }
    if(key(filter,"DATE")) {
        uint64_t stamp;
        time_t now=time(NULL);
        if(now<0 || !parse_date(filter.argument,&stamp)) { bad(s);return; }
        u->since=stamp;u->until=(uint64_t)now;page(s,VS_LIST,0,0,0,1);return;
    }
    if((key(filter,"BY") || key(filter,"TITLE")) && *filter.argument &&
       mmm_ui_text_valid(filter.argument,key(filter,"BY")?32:80)) {
        OperationRequest *q=mmm_request(s,'Z');
        if(q) {
            q->action=3;q->scope=ARTICLE_HERE;
            strcpy(q->username,key(filter,"BY")?filter.argument:"*");
            strcpy(q->title,key(filter,"TITLE")?filter.argument:"*");
            if(mmm_submit(s,q,JOB_VS_PAGE))u->page=VS_LIST;
        }
    } else bad(s);
}

static void peers(Session *s) {
    VsUi *u=vs(s);ChatPresence list;
    u->peer_count=0;
    if(!chat_presence(s->core.id,s->core.user,0,&list))return;
    for(unsigned i=0;i<list.count;i++) {
        const ChatPresenceRow *p=&list.rows[i];
        if(!*p->login || p->connection==s->core.id)continue;
        u->peers[u->peer_count++]=*p;
        char text[200];snprintf(text,sizeof(text),"%lu  %s  %s%s\r\n",p->connection,p->login,p->name,p->accepts_telegram?"":" (closed)");
        session_impl_queue_text(s,text);
    }
}

static void telegram(Session *s,char *arg) {
    if(!*arg) { peers(s);return; }
    VsCommand target=vs_command(arg);
    uint32_t id;
    VsUi *u=vs(s);
    if(mmm_number(target.key,&id) && chat_text_valid(target.argument))for(unsigned i=0;i<u->peer_count;i++) {
        const ChatPresenceRow *p=&u->peers[i];
        if(p->connection!=id || !p->accepts_telegram)continue;
        OperationRequest *q=mmm_request(s,'&');
        if(q) {
            q->action='D';q->chat_target=p->connection;q->chat_generation=p->generation;
            strcpy(q->body,target.argument);mmm_submit(s,q,JOB_CHAT);
        }
        return;
    }
    mmm_say(s,"Run /W before choosing a current connection.\r\n","/W で現在の回線番号を確認してください。\r\n");
}

static void chat_line(Session *s,char *line) {
    if(*line!='/') {
        if(chat_text_valid(line))mmm_chat_request(s,'S',0,line);else bad(s);
        return;
    }
    VsCommand c=vs_command(line);
    if(key(c,"//")) { chat_leave(s->core.id);vs(s)->common.chatting=0;home(s); }
    else if(key(c,"/W"))peers(s);
    else if(key(c,"/T"))telegram(s,c.argument);
    else if(key(c,"/M") && chat_text_valid(c.argument)) {
        if(!chat_memo(s->core.id,c.argument))bad(s);
    }
    else if(key(c,"/S") && chat_text_valid(c.argument))mmm_chat_request(s,'C',0,c.argument);
    else if(key(c,"/?"))vs_help(s,"");
    else bad(s);
}

static void file_transfer(Session *s,VsCommand c) {
    static const char *const names[]={"xmodem","ymodem","ymodem-g","zmodem","mlink"};
    const char *codes="XYGZM",*found=strchr(codes,c.key[0]);
    if(!*c.key || !found || c.key[2] || (c.key[1]!='U' && c.key[1]!='D')) { bad(s);return; }
    if(c.key[1]=='D' && !*c.argument && vs(s)->vault_id) {
        char id[16];snprintf(id,sizeof(id),"%u",vs(s)->vault_id);
        mmm_ui_transfer_protocol(s,names[found-codes]);mmm_ui_filer_command(s,"get",id);return;
    }
    if(c.key[1]=='D') {
        uint32_t id;
        if(!mmm_number(c.argument,&id)) { bad(s);return; }
    }
    mmm_ui_transfer_protocol(s,names[found-codes]);
    if(!mmm_ui_filer_command(s,c.key[1]=='U'?"put":"get",c.argument))bad(s);
}

static void files(Session *s,VsCommand c) {
    VsUi *u=vs(s);MmmUi *m=&u->common;
    if(key(c,"Q")) { home(s);return; }
    if(key(c,"F")) { u->input=VS_FINDER;vs_menu(s,1);return; }
    if(key(c,"B")) { u->input=VS_BATCH;vs_menu(s,1);return; }
    if(key(c,"L")) { mmm_ui_filer_command(s,"list",c.argument);return; }
    if(key(c,"R")) { if(!mmm_ui_filer_command(s,"info",c.argument))bad(s);return; }
    if(key(c,"+") || key(c,"DIR")) { mmm_file_enter(s,m->file_path);return; }
    if(key(c,".")) {
        char parent[49];strcpy(parent,m->file_path);
        char *slash=strrchr(parent,'/');
        if(slash)*slash=0;else parent[0]=0;
        mmm_file_enter(s,parent);return;
    }
    if(key(c,"CD")) { mmm_file_enter(s,c.argument);return; }
    if(key(c,"MORE")) { if(!mmm_ui_filer_command(s,"more",c.argument))bad(s);return; }
    if(key(c,"ZRESUME")) { if(!mmm_ui_filer_command(s,"resume",c.argument))bad(s);return; }
    file_transfer(s,c);
}

static void batch(Session *s,VsCommand c) {
    VsUi *u=vs(s);BatchSelection *b=&u->common.files;
    uint32_t id;
    if(key(c,"Q"))u->input=VS_IDLE;
    else if(key(c,"A") && mmm_number(c.argument,&id))mmm_ui_filer_command(s,"add",c.argument);
    else if(key(c,"D") && mmm_number(c.argument,&id))batch_remove(b,id);
    else if(key(c,"C"))batch_reset(b);
    else if(key(c,"X"))mmm_ui_filer_command(s,"batch",c.argument);
    else if(key(c,"L") || !*c.key) {
        if(!b->count)mmm_say(s,"Batch list is empty.\r\n","バッチ一覧は空です。\r\n");
        for(unsigned i=0;i<b->count;i++) {
            char text[64];snprintf(text,sizeof(text),"#%u %s\r\n",b->ids[i],b->done[i]?"done":"pending");
            session_impl_queue_text(s,text);
        }
    } else bad(s);
}

static void mailbox(Session *s,VsCommand c) {
    VsUi *u=vs(s);uint32_t id;
    if(key(c,"Q")) { home(s);return; }
    if(key(c,"K") && u->current) { u->input=VS_WITHDRAW_CONFIRM;return; }
    const char *action=key(c,"R")?(*c.argument?"read":"next"):key(c,"W")?"send":
        key(c,"L")?"inbox":key(c,"B")?"files":key(c,"GET")?"get":key(c,"UPLOAD")?"upload":
        key(c,"SENT")?"sent":key(c,"MORE")?"more":NULL;
    if(!action && mmm_number(c.key,&id) && !*c.argument) {
        mmm_simple_operation(s,'m','R',id,JOB_MAIL);return;
    }
    if(!action || !mmm_ui_mail_command(s,action,c.argument))bad(s);
}

static void topic(Session *s,ConferenceAction action,uint32_t id,const char *text) {
    if(text && !mmm_ui_text_valid(text,CONFERENCE_TEXT)) { bad(s);return; }
    OperationRequest *q=mmm_request(s,BOARD_CONFERENCE);
    if(q) {
        q->conference.action=action;q->conference.cursor=id;
        if(text)strcpy(q->conference.text,text);
        mmm_submit(s,q,JOB_VS_TOPIC);
    }
}

static int extensions(Session *s,VsCommand c) {
    VsUi *u=vs(s);MmmUi *m=&u->common;uint32_t id;
    if(key(c,"UI"))mmm_ui_choose_profile(s,c.argument);
    else if(key(c,"GAMES") && (!*c.argument || mmm_number(c.argument,&id)))mmm_games(s,*c.argument?id:0);
    else if(key(c,"TALK"))mmm_conference_enter(s,c.argument);
    else if(key(c,"TOPIC")) {
        VsCommand sub=vs_command(c.argument);
        if(key(sub,"-W") && *sub.argument)topic(s,TOPIC_POST,0,sub.argument);
        else if(key(sub,"-D") && mmm_number(sub.argument,&id))topic(s,TOPIC_DELETE,id,NULL);
        else if(key(sub,"ALL"))topic(s,TOPIC_HISTORY,0,NULL);
        else if(key(sub,"MORE") && u->topic_next)topic(s,u->topic_action,u->topic_next,NULL);
        else if(!*sub.key)topic(s,TOPIC_UNREAD,0,NULL);
        else bad(s);
    } else if(key(c,"DRAFT"))mmm_ui_note_draft_command(s,c.argument);
    else if(key(c,"MANAGE")) { m->screen=MANAGE_SCREEN;mmm_manage_menu(s); }
    else if(key(c,"LOGOUT") && m->screen==MAIN_SCREEN) {
        char cmd[]="logout";Language language=m->language;
        mmm_ui_shared_command(s,cmd);
        if(!s->core.user) { OPENSSL_cleanse(u,sizeof(*u));u->common.language=language; }
    } else return 0;
    return 1;
}

static void set_mask(Session *s,char *argument,uint32_t board) {
    VsCommand c=vs_command(argument);
    if(!board && mmm_number(c.key,&board))c=vs_command(c.argument);
    if(board && (key(c,"ON") || key(c,"OFF")))setting(s,VS_MASK_SET,board,key(c,"OFF"),0,0);
    else bad(s);
}

static void pointer_open(Session *s,uint32_t board,char *arg) {
    VsUi *u=vs(s);
    u->position_board=board;u->input=VS_POINTER_MENU;
    if(*arg)pointer(s,vs_command(arg));
    else vs_menu(s,1);
}

static int pending_input(Session *s,VsCommand c,char *line) {
    VsUi *u=vs(s);MmmUi *m=&u->common;uint32_t id;
    switch(u->input) {
    case VS_IDLE:return 0;
    case VS_DIRECT_MENU:
        if(key(c,"Q"))u->input=VS_IDLE;
        else if(key(c,"J") || key(c,"DJ"))jump(s);
        else if(key(c,"W") || key(c,"R")) {
            u->direct=(VsDirect){.kind=key(c,"R")?VS_DIRECT_REPLY:VS_DIRECT_BASE};
            u->input=VS_POST_BOARD;
        } else if(key(c,"M") && *c.argument) {
            u->input=VS_IDLE;m->screen=MAIL_SCREEN;
            if(!mmm_ui_mail_command(s,"send",c.argument))bad(s);
        } else if(key(c,"P") && *c.argument) {
            if(!mmm_ui_term_command(s,"set",c.argument))bad(s);
            else u->input=VS_IDLE;
        } else bad(s);
        return 1;
    case VS_POST_BOARD:
        if(key(c,"Q")) { u->input=VS_DIRECT_MENU;u->direct=(VsDirect){0}; }
        else if(!*line || !mmm_ui_text_valid(line,80))bad(s);
        else {
            strcpy(u->direct.board,line);
            if(u->direct.kind==VS_DIRECT_REPLY)u->input=VS_POST_PARENT;
            else open_post_board(s);
        }
        return 1;
    case VS_POST_PARENT:
        if(key(c,"Q")) { u->input=VS_DIRECT_MENU;u->direct=(VsDirect){0}; }
        else if(mmm_number(c.key,&id) && !*c.argument) {
            u->direct.parent=id;open_post_board(s);
        } else bad(s);
        return 1;
    case VS_READ_START:
        u->input=VS_IDLE;
        if(!*line) { u->descending=1;page(s,VS_SELECT,0,0,1,0); }
        else if(mmm_number(line,&id)) { u->descending=0;read_body(s,id); }
        else if(!key(c,"Q"))bad(s);
        return 1;
    case VS_POINTER_MENU: case VS_POINTER_DATE: pointer(s,c);return 1;
    case VS_MODE_MENU: case VS_MODE_NEWS:
        if(key(c,"Q"))u->input=u->input==VS_MODE_NEWS?VS_MODE_MENU:VS_IDLE;
        else if(key(c,"B")) { u->input=VS_MODE_NEWS;vs_menu(s,1); }
        else if(key(c,"N") && u->input==VS_MODE_NEWS) {
            VsCommand order=vs_command(c.argument);
            if(key(order,"NUMBER"))setting(s,VS_MODE_SET,0,0,0,0);
            else if(key(order,"RELATED"))setting(s,VS_MODE_SET,1,0,0,0);
            else if(!*order.key)setting(s,VS_MODE_SET,!u->related,0,0,0);
            else bad(s);
        } else if(key(c,"P"))mmm_profile(s,c.argument);
        else if(key(c,"T")) {
            VsCommand choice=vs_command(c.argument);
            int on=key(choice,"ON");
            if(*choice.argument || (!on && !key(choice,"OFF")))bad(s);
            else {
                OperationRequest *q=mmm_request(s,'&');
                if(q) {
                    q->action='p';q->number=on?CHAT_PREF_RECEIVE_ON:CHAT_PREF_RECEIVE_OFF;
                    mmm_submit(s,q,JOB_CHAT);
                }
            }
        } else if(key(c,"PROTOCOL") && mmm_ui_transfer_protocol(s,c.argument)) {}
        else bad(s);
        return 1;
    case VS_WITHDRAW_CONFIRM:
        u->input=VS_IDLE;
        if(key(c,"Y") && u->current) {
            if(m->screen==MAIL_SCREEN)mmm_simple_operation(s,'m','D',u->current,JOB_MAIL);
            else {
                OperationRequest *q=mmm_request(s,'Z');
                if(q) { q->action=1;q->number=u->current;q->cursor=1;mmm_submit(s,q,JOB_NOTE_CHANGE); }
            }
        }
        return 1;
    case VS_FINDER:
        if(key(c,"Q"))u->input=VS_IDLE;
        else if(key(c,"S") && mmm_ui_filer_command(s,"find",c.argument)) {}
        else bad(s);
        return 1;
    case VS_BATCH: batch(s,c);return 1;
    default:return 0;
    }
}

static int direct_chain(Session *s,char *line) {
    const char *start=line+strspn(line," \t");
    VsDirect direct=vs_direct(start);
    if(direct.kind==VS_DIRECT_INVALID && !strchr(start,';'))return 0;
    if(direct.kind==VS_DIRECT_JUMP)jump(s);
    else if(direct.kind==VS_DIRECT_INVALID)bad(s);
    else {
        VsUi *u=vs(s);
        u->direct=direct;open_post_board(s);
    }
    return 1;
}

void vs_line(Session *s,char *line) {
    VsUi *u=vs(s);MmmUi *m=&u->common;
    if(m->screen==CHAT_SCREEN) { chat_line(s,line);return; }
    if(m->screen==TALK_SCREEN) {
        if(!strcmp(line,"Q") || !strcmp(line,"q"))home(s);
        else mmm_conference_line(s,line);
        return;
    }
    VsCommand c=vs_command(line);uint32_t id;
    if(!s->core.user) {
        if(key(c,"LOGIN"))mmm_ui_auth_start(s,c.argument,'a');
        else if(key(c,"REGISTER"))mmm_ui_auth_start(s,c.argument,'g');
        else if(key(c,"UI"))mmm_ui_choose_profile(s,c.argument);
        else if(!*line || key(c,"?") || key(c,"H"))vs_help(s,"");
        else if(mmm_ui_text_valid(line,32))mmm_ui_auth_start(s,line,'a');
        else bad(s);
        return;
    }
    if(u->input==VS_IDLE || u->input==VS_DIRECT_MENU) {
        if((m->screen==MAIN_SCREEN || m->screen==BOARD_SCREEN) && direct_chain(s,line))return;
        if(vs_direct(line).kind==VS_DIRECT_JUMP) { jump(s);return; }
    }
    if(key(c,"H") || key(c,"?")) { vs_help(s,c.argument);return; }
    if(pending_input(s,c,line))return;
    if(key(c,"/W")) { peers(s);return; }
    if(key(c,"/T")) { telegram(s,c.argument);return; }
    if(!u->scan && (m->screen==MAIN_SCREEN || m->screen==BOARD_SCREEN) && extensions(s,c))return;
    if(m->screen==OPEN_SCREEN || m->screen==NEW_SCREEN) { read_command(s,c);return; }
    if(m->screen==FILER_SCREEN) { files(s,c);return; }
    if(m->screen==MAIL_SCREEN) { mailbox(s,c);return; }
    if(m->screen==MANAGE_SCREEN) {
        if(key(c,"Q"))home(s);
        else {
            for(char *p=c.key;*p;p++)if(*p>='A' && *p<='Z')*p=(char)(*p-'A'+'a');
            if(!mmm_ui_manage_command(s,c.key,c.argument))bad(s);
        }
        return;
    }
    if(m->screen==GAMES_SCREEN) {
        if(key(c,"Q") || key(c,"0"))home(s);
        else if(mmm_number(c.key,&id))mmm_games(s,id);
        else bad(s);
        return;
    }
    if(m->screen==NOTES_SCREEN) {
        if(key(c,"Q"))home(s);
        else if(key(c,".")) {
            if(m->depth)m->parent=m->ancestors[--m->depth];
            else m->parent=0;
            mmm_ui_boards(s,0);
        } else if(key(c,"+") || !*line)mmm_ui_boards(s,0);
        else if(key(c,"MORE") && m->next)mmm_ui_boards(s,m->next);
        else if(key(c,"*"))start_targets(s,m->parent,0,1);
        else if(key(c,"A"))set_mask(s,c.argument,0);
        else if(key(c,"P"))pointer_open(s,m->parent,c.argument);
        else if(key(c,"N") || key(c,"NA"))start_targets(s,m->parent,key(c,"NA"),0);
        else if(mmm_number(c.key,&id) && id<=m->row_count && !*c.argument) {
            char target[16];snprintf(target,sizeof(target),"#%u",m->rows[id-1]);select_board(s,target);
        } else select_board(s,line);
        return;
    }
    if(m->screen==BOARD_SCREEN) {
        if(key(c,"Q"))home(s);
        else if(key(c,"."))mmm_ui_boards(s,0);
        else if(key(c,"R")) {
            u->selector_root=0;u->read_related=0;
            if(!*c.argument)u->input=VS_READ_START;
            else if(mmm_number(c.argument,&id)) { u->descending=0;read_body(s,id); }
            else bad(s);
        } else if(key(c,"L"))titles(s,c.argument);
        else if(key(c,"MORE") && m->next)page(s,VS_LIST,0,m->next,u->page_backwards,0);
        else if(key(c,"W"))start_post(s,0);
        else if(key(c,"A"))set_mask(s,c.argument,s->core.board);
        else if(key(c,"P"))pointer_open(s,s->core.board,c.argument);
        else if(key(c,"N") || key(c,"NA"))start_targets(s,s->core.board,key(c,"NA"),0);
        else if(key(c,"#"))mmm_simple_operation(s,'!','i',s->core.board,JOB_MANAGE);
        else if(key(c,"J") || key(c,"DJ"))jump(s);
        else bad(s);
        return;
    }
    if(key(c,"B")) {
        m->parent=m->depth=0;
        if(*c.argument)select_board(s,c.argument);
        else mmm_ui_boards(s,0);
    } else if(key(c,"N") || key(c,"NA"))start_targets(s,0,key(c,"NA"),0);
    else if(key(c,"NF")) {
        uint64_t ignored;
        if(!parse_date(c.argument,&ignored)) {
            mmm_say(s,"NF YYYY-MM-DD\r\n","NF 年-月-日\r\n");return;
        }
        m->screen=FILER_SCREEN;m->file_path[0]=0;m->file_action=VAULT_CMD_NEWS;
        for(unsigned i=0;i<10;i++)m->file_filter[i]=c.argument[i]=='/'?'-':c.argument[i];
        m->file_filter[10]=0;mmm_ui_file_page(s,0);
    } else if(key(c,"F")) { m->screen=FILER_SCREEN;u->vault_id=0;mmm_file_enter(s,c.argument); }
    else if(key(c,"M")) { m->screen=MAIL_SCREEN;u->current=0;mmm_mail_list(s,MAIL_LIST_INBOX,0); }
    else if(key(c,"C") && (!*c.argument || !strcmp(c.argument,"1")))mmm_chat_request(s,'J',0,"");
    else if(key(c,"D")) { u->input=VS_DIRECT_MENU;vs_menu(s,1); }
    else if(key(c,"J") || key(c,"DJ"))jump(s);
    else if(key(c,"U")) { u->input=VS_MODE_MENU;vs_menu(s,1); }
    else if(key(c,"P"))pointer_open(s,0,c.argument);
    else if(key(c,"A"))mmm_simple_operation(s,'!','U',0,JOB_MANAGE);
    else if(key(c,"#"))mmm_simple_operation(s,'!','V',0,JOB_MANAGE);
    else if(key(c,"I") || key(c,"_"))session_impl_queue_text(s,"Liner Notes BBS | VS profile\r\n");
    else if(key(c,"X")) { u->quiet=!u->quiet;vs_menu(s,0); }
    else if(key(c,"O"))mmm_off(s,1);
    else if(!*line)vs_menu(s,0);
    else bad(s);
}

static void settings_result(Session *s,const OperationResult *r,const OperationPage *p) {
    VsUi *u=vs(s);
    if(r->service_action==VS_MODE_GET || r->service_action==VS_MODE_SET) {
        u->restore_mode=0;u->related=r->service_number!=0;
        if(r->service_action==VS_MODE_SET)
            session_impl_queue_text(s,u->related?"News: related order.\r\n":"News: number order.\r\n");
        else vs_menu(s,0);
    } else if(r->service_action==VS_TARGETS && p && p->kind==OPERATION_PAGE_MMM_SCANLIST) {
        const MmmScanList *list=&p->data.mmm_scanlist;
        u->targets_next=r->service_next;
        if(u->manual_list) {
            for(unsigned i=0;i<list->count;i++) {
                char line[112];snprintf(line,sizeof(line),"#%u  %s\r\n",list->rows[i].board,list->rows[i].title);
                session_impl_queue_text(s,line);
            }
            if(u->targets_next)setting(s,VS_TARGETS,u->scope,1,u->targets_next,0);
            else u->manual_list=0;
        } else {
            u->board_count=list->count;u->board_at=0;
            for(unsigned i=0;i<list->count;i++)u->boards[i]=list->rows[i].board;
            next_board(s);
        }
    } else if(r->service_action==VS_WINDOW) {
        u->since=r->since;u->until=r->until;
        if(u->scan) {
            char banner[128];snprintf(banner,sizeof(banner),"News: %s%s\r\n",u->common.board_title,u->related?" (related)":"");
            session_impl_queue_text(s,banner);
            if(u->automatic)news_next(s);else u->await_start=1;
        } else {
            char line[112];snprintf(line,sizeof(line),"News pointer: %llu\r\n",(unsigned long long)r->since);
            session_impl_queue_text(s,line);
        }
    } else if(r->service_action==VS_COMMIT && u->scan)next_board(s);
    else mmm_say(s,"Setting saved.\r\n","設定を保存しました。\r\n");
}

void vs_result(Session *s,MmmJob job,const OperationResult *r,SessionResultView result) {
    VsUi *u=vs(s);MmmUi *m=&u->common;
    const OperationPage *p=result.page;
    const OperationContent *c=result.content;
    if(result.status!=SESSION_RESULT_OK) {
        if(job==JOB_BOARD || job==JOB_VS_LOOKUP || job==JOB_PREPARE) {
            u->direct=(VsDirect){0};u->posting=0;
        }
        if(job==JOB_BOARD || job==JOB_VS_BODY || job==JOB_VS_PAGE || job==JOB_VS_SETTING || job==JOB_VS_MARK) {
            stop_scan(u);u->restore_mode=0;
        }
        return;
    }
    switch(job) {
    case JOB_VS_LOOKUP:
        if(r->service_number) {
            char board[16];snprintf(board,sizeof(board),"#%u",r->service_number);select_board(s,board);
        }
        break;
    case JOB_VS_SETTING: settings_result(s,r,p);break;
    case JOB_VS_PAGE: if(p && p->kind==OPERATION_PAGE_ARTICLES)page_result(s,&p->data.articles);break;
    case JOB_VS_BODY:
        if(c && c->state==ARTICLE_VISIBLE && result.body) {
            char header[188];
            snprintf(header,sizeof(header),"Message #%u  %s  %s\r\n",c->id,c->author,c->title);
            session_impl_queue_text(s,header);session_impl_queue_text(s,result.body);session_impl_queue_text(s,"\r\n");
            u->current=c->id;u->root=c->root;strcpy(u->author,c->author);
            m->screen=u->scan?NEW_SCREEN:OPEN_SCREEN;
            if(s->core.active && !s->core.closing) {
                OperationRequest *q=mmm_request(s,'s');
                if(q) {
                    q->number=c->id;
                    if(!mmm_submit(s,q,JOB_VS_MARK))stop_scan(u);
                } else stop_scan(u);
            }
        } else {
            stop_scan(u);
            mmm_say(s,"Message unavailable.\r\n","この記事を表示できません。\r\n");
        }
        break;
    case JOB_VS_MARK:if(u->automatic)u->next_display=1;break;
    case JOB_MAIL:
        if(c && c->kind==2)u->current=c->id;
        else if(r->mail_action=='D')u->current=0;
        break;
    case JOB_FILES: case JOB_FILE_ADD:
        if(p && p->kind==OPERATION_PAGE_FILE_DETAIL)u->vault_id=p->data.file_detail.entry.id;
        break;
    case JOB_PREPARE:
        if(*u->direct.title)mmm_ui_editor(s,u->direct.title);
        u->direct=(VsDirect){0};
        break;
    case JOB_POST:u->posting=0;break;
    case JOB_VS_TOPIC:
        if(p && p->kind==OPERATION_PAGE_CONFERENCE) {
            const ConferenceReply *reply=&p->data.conference;
            if(reply->action==TOPIC_POST)session_impl_queue_text(s,"Topic posted.\r\n");
            else if(reply->action==TOPIC_DELETE)session_impl_queue_text(s,"Topic deleted.\r\n");
            else if(reply->action!=TOPIC_SEEN) {
                u->topic_action=reply->action;u->topic_next=reply->next;
                for(unsigned i=0;i<reply->count;i++) {
                    const ConferenceLine *entry=&reply->rows.lines[i];
                    char text[656];
                    snprintf(text,sizeof(text),"Topic #%u %s\r\n%s\r\n",entry->id,entry->name,entry->text);
                    session_impl_queue_text(s,text);
                }
                if(reply->action==TOPIC_UNREAD && reply->count && s->core.active && !s->core.closing)
                    topic(s,TOPIC_SEEN,reply->position,NULL);
            }
        }
        break;
    default:break;
    }
}

int vs_defer_telegram(Session *s,const ChatEvent *event) {
    VsUi *u=vs(s);
    if(u->telegram_user!=s->core.user || u->telegram_epoch!=s->core.user_epoch) {
        OPENSSL_cleanse(u->telegrams,sizeof(u->telegrams));u->telegram_count=0;
        u->telegram_user=s->core.user;u->telegram_epoch=s->core.user_epoch;
    }
    if(u->telegram_count==sizeof(u->telegrams)/sizeof(u->telegrams[0]))return 0;
    u->telegrams[u->telegram_count++]=*event;
    return 1;
}

void vs_tick(Session *s) {
    VsUi *u=vs(s);
    if(u->restore_mode) {
        u->restore_mode=0;
        if(!setting(s,VS_MODE_GET,0,0,0,0))mmm_prompt(s);
    } else if(u->automatic && u->next_display &&
              (!s->connection.io.drained || s->connection.io.drained(s->connection.io.context))) {
        u->next_display=0;news_next(s);
    } else if(u->telegram_count && !u->scan && vs_ready(s) && u->common.input==INPUT_NONE &&
              !u->common.application.step && !session_secret(s) && !s->connection.door &&
              !s->connection.zmodem && !s->connection.zmodem_start && !s->connection.binary) {
        unsigned count=u->telegram_count;
        u->telegram_count=0;
        if(s->core.user==u->telegram_user && s->core.user_epoch==u->telegram_epoch) {
            for(unsigned i=0;i<count;i++) {
                ChatEvent *event=&u->telegrams[i];
                event->replay=1;mmm_chat_event(s,event);
            }
            mmm_prompt(s);
        }
        OPENSSL_cleanse(u->telegrams,sizeof(u->telegrams));
    }
}
int vs_automatic(const Session *s) { return vs_view(s)->automatic; }

int vs_scan_cancelled(Session *s) {
    VsUi *u=vs(s);
    int cancelled=u->scan_cancelled;
    u->scan_cancelled=0;
    return cancelled;
}

void vs_scan_stop(Session *s) {
    VsUi *u=vs(s);
    if(!u->scan)return;
    home(s);
    u->scan_cancelled=s->core.pending!=0;
    mmm_prompt(s);
}

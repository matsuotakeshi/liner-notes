#define _POSIX_C_SOURCE 200809L
#include "session_net_cock.h"
#include "net_cock_core.h"
#include "date_filter.h"
#include "editor_core.h"
#include "mmm_input.h"
#include "session_policy_core.h"
#include "vault_bridge.h"
#include "chat.h"
#include "store_limits.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <string.h>

static NetCockUi *nc(Session *s) { return s->ui_binding.state; }
static const NetCockUi *nc_view(const Session *s) { return s->ui_binding.state; }
static int same(const char *a,const char *b) { return net_cock_equal(a,b); }
static void invalid(Session *s) { mmm_bad_service_command(s); }

static int operation(Session *s,NetCockOperation action,uint32_t id,int scope) {
    OperationRequest *q=mmm_request(s,BOARD_NET_COCK);
    if(!q)return 0;
    q->action=action;q->number=id;q->scope=scope;
    return mmm_submit(s,q,action==NET_HEADER?JOB_NET_HEADER:JOB_NET_OPERATION);
}

int net_cock_secret(const Session *s) {
    NetCockInput input=nc_view(s)->input;
    return input>=NC_PASSWORD_OLD && input<=NC_PASSWORD_CONFIRM;
}
int net_cock_ready(const Session *s) {
    const NetCockUi *u=nc_view(s);
    return u->input==NC_IDLE && !u->scanning && !u->in_batch;
}

static void date_text(uint64_t stamp,char out[32]) {
    time_t t=(time_t)stamp;
    struct tm civil;
    if(!stamp || t<0 || (uint64_t)t!=stamp || !localtime_r(&t,&civil) ||
       !strftime(out,32,"%y/%m/%d %H:%M",&civil))strcpy(out,"--/--/-- --:--");
}

void net_cock_login(Session *s,const OperationResult *r) {
    NetCockUi *u=nc(s);
    char before[32],now[32],detail[256];
    date_text(s->core.previous_login,before);date_text(u->common.login_at,now);
    snprintf(detail,sizeof(detail),mmm_local(s,
        "Previous login: %s  Current login: %s\r\nLevel: %d\r\n",
        "前回接続: %s  今回接続: %s\r\n会員レベル: %d\r\n"),before,now,r->member_level);
    session_impl_queue_text(s,detail);
    if(r->mail_notice)mmm_say(s,"You have unread mail.\r\n","未読メールがあります。\r\n");
    u->restore_mode=1;
}

int net_cock_prompt(Session *s) {
    const NetCockUi *u=nc_view(s);
    const MmmUi *m=&u->common;
    if(m->input==INPUT_EDITOR && u->input!=NC_BODY_EDIT) {
        mmm_say(s,"Write (Y/N/T/E/C/Q)> ","書込 (Y/N/T/E/C/Q)> ");return 1;
    }
    if(m->input==INPUT_EDITOR) {
        mmm_say(s,"Editor (w: confirm)> ","編集 (w: 確認)> ");return 1;
    }
    if(m->input!=INPUT_NONE || m->screen==TALK_SCREEN)return 0;
    if(u->restore_mode || u->continuous)return 1;
    switch(u->input) {
    case NC_TITLE_EDIT: mmm_say(s,"New title> ","新しい題名> ");return 1;
    case NC_SUBSCRIPTION: mmm_say(s,"Subscribe (Y/N/Enter/Q)> ","購読 (Y/N/Enter/Q)> ");return 1;
    case NC_DELETE: mmm_say(s,"Delete this item (y/N)> ","この項目を削除しますか (y/N)> ");return 1;
    case NC_PASSWORD_OLD: mmm_say(s,"Current password> ","現在のパスワード> ");return 1;
    case NC_PASSWORD_NEW: mmm_say(s,"New password> ","新しいパスワード> ");return 1;
    case NC_PASSWORD_CONFIRM: mmm_say(s,"Confirm password> ","新しいパスワードの確認> ");return 1;
    case NC_MAIL_TO: mmm_say(s,"Recipient> ","宛先> ");return 1;
    case NC_VAULT_UPLOAD: mmm_say(s,"File description> ","ファイル説明> ");return 1;
    default:break;
    }
    if(!s->core.user) { session_impl_queue_text(s,"ID> ");return 1; }
    const char *prompt=u->mode?"> ":"Menu> ";
    if(u->in_batch)prompt="Batch> ";
    else if(u->input==NC_BOARD_SELECT)prompt="Board> ";
    else if(m->screen==OPEN_SCREEN || m->screen==INDEX_SCREEN)prompt="Read> ";
    else if(m->screen==MAIL_SCREEN)prompt="Mail> ";
    else if(m->screen==FILER_SCREEN)prompt="PDS> ";
    else if(m->screen==CHAT_SCREEN)prompt="Chat> ";
    else if(m->screen==GAMES_SCREEN)prompt="Game> ";
    else if(m->screen==MANAGE_SCREEN)prompt="Manage> ";
    session_impl_queue_text(s,prompt);return 1;
}

void net_cock_menu(Session *s,int requested) {
    const NetCockUi *u=nc_view(s);
    if(!s->core.user) {
        mmm_say(s,"NET-COCK style | ID / JOIN name / ?\r\n","NET-COCK風 | 会員ID / JOIN 会員ID / ?\r\n");
        return;
    }
    if(u->in_batch) { session_impl_queue_text(s,"Y / O / D ID / L / C / N\r\n");return; }
    switch(u->common.screen) {
    case MAIN_SCREEN: break;
    case NOTES_SCREEN: mmm_say(s,"Number / A-O / more / 0 up\r\n","番号 / A〜O / more 続き / 0 上位\r\n");return;
    case OPEN_SCREEN: case INDEX_SCREEN:
        mmm_say(s,"R read / A,L titles / N,B,T,E move / P,F reply / U,I parent,return / W write / D delete / Q exit\r\n",
                  "R 本文 / A,L 題名 / N,B,T,E 移動 / P,F 返信 / U,I 返信元・復帰 / W 投稿 / D 削除 / Q 終了\r\n");return;
    case MAIL_SCREEN: session_impl_queue_text(s,"R [ID] / N / B / T / E / A / P / F / D / Q\r\n");return;
    case FILER_SCREEN: session_impl_queue_text(s,"A/L / R [ID] / X/Y/Z [ID] / S [ID] / W / Q\r\n");return;
    case CHAT_SCREEN: session_impl_queue_text(s,".L / .P LINE TEXT / .C TEXT / .T TEXT / .B TEXT / .Q\r\n");return;
    case GAMES_SCREEN: mmm_say(s,"Number: start / 0: exit\r\n","番号 起動 / 0 戻る\r\n");return;
    case MANAGE_SCREEN: mmm_manage_menu(s);return;
    case TALK_SCREEN: mmm_conference_menu(s);return;
    default:return;
    }
    if(u->mode) {
        if(requested) { session_ui_menu_hint(s);mmm_extensions_hint(s); }
        if(requested)session_impl_queue_text(s,"BREAD / BWRITE / BSET / RALL / RNALL / PMOVE / MREAD / MWRITE / PDS / CHAT / MODE / EXCOM / OFF\r\n");
        return;
    }
    switch(u->menu) {
    case NC_MAIN_MENU:
        session_ui_menu_hint(s);
        mmm_extensions_hint(s);
        if(s->core.admin)mmm_say(s,"1. Member  2. Boards  3. Mail  4. Mode  5. Board settings  6. Chat  7. Join  8. Report  9. Off\r\n",
            "1. 会員  2. 掲示板  3. メール  4. 設定  5. ボード管理  6. チャット  7. 入会  8. 状態  9. 切断\r\n");
        else mmm_say(s,"1. Member  2. Boards  3. Mail  4. Chat  5. Join  6. Off\r\n",
            "1. 会員  2. 掲示板  3. メール  4. チャット  5. 入会  6. 切断\r\n");
        break;
    case NC_USER_MENU: session_impl_queue_text(s,"1. UREAD  2. UWRITE  3. IDLIST  4. USTAT  5. MODE  6. PASS  0. Back\r\n");break;
    case NC_BOARD_MENU: session_impl_queue_text(s,"1. BREAD  2. BWRITE  3. BATCH  4. BSET  5. RALL  6. RNALL  7. PMOVE  8. EXCOM  0. Back\r\n");break;
    case NC_MAIL_MENU: session_impl_queue_text(s,"1. MREAD  2. MWRITE  3. MCHECK  4. MBSET  0. Back\r\n");break;
    case NC_CHAT_MENU: session_impl_queue_text(s,"1. CHAT  2. LLIST  3. TSET  4. CSET  5. ACCESS  0. Back\r\n");break;
    case NC_REGISTER_MENU: session_impl_queue_text(s,"1. JOIN  2. NEWMEM  0. Back\r\n");break;
    }
}

void net_cock_help(Session *s,const char *topic) { (void)topic;net_cock_menu(s,1); }

static void home(Session *s) {
    NetCockUi *u=nc(s);
    u->common.screen=MAIN_SCREEN;u->menu=NC_MAIN_MENU;u->input=NC_IDLE;
    u->scanning=u->continuous=u->in_batch=u->close_after_batch=0;
    net_cock_menu(s,0);
}

static void flat_page(Session *s,uint32_t cursor,int descending,NetCockPage page) {
    NetCockUi *u=nc(s);
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->action=u->scanning?'n':'F';q->cursor=cursor;q->descending=descending;
    if(u->scanning) { q->scope=ARTICLE_HERE;q->since=u->since;q->until=u->until; }
    if(mmm_submit(s,q,JOB_NET_PAGE)) { u->page=page;u->direction=descending; }
}

static void header(Session *s,uint32_t id) { operation(s,NET_HEADER,id,0); }

static void body(Session *s) {
    NetCockUi *u=nc(s);
    if(!u->selected) { invalid(s);return; }
    OperationRequest *q=mmm_request(s,'Z');
    if(q) { q->action=5;q->scope=1;q->number=u->selected;mmm_submit(s,q,JOB_NET_BODY); }
}

static void choose_board(Session *s,char *arg,int write) {
    NetCockUi *u=nc(s);
    u->write_board=write;
    if(!*arg || !strcmp(arg,".") || !strcmp(arg,"?")) {
        u->input=NC_BOARD_SELECT;u->common.parent=u->common.depth=0;mmm_ui_boards(s,0);return;
    }
    uint32_t id;
    if(mmm_number(*arg=='#'?arg+1:arg,&id)) {
        char number[16];snprintf(number,sizeof(number),"#%u",id);
        u->input=NC_IDLE;mmm_ui_enter_board(s,number,OPEN_BOARD_MENU);return;
    }
    if(!mmm_ui_text_valid(arg,80)) { invalid(s);return; }
    OperationRequest *q=mmm_request(s,BOARD_NET_COCK);
    if(q) { q->action=NET_LOOKUP;strcpy(q->title,arg);u->input=NC_IDLE;mmm_submit(s,q,JOB_NET_OPERATION); }
}

static void scan_board(Session *s) {
    NetCockUi *u=nc(s);
    if(u->scan_index>=u->scan_count) {
        if(u->scan_count && u->until) {
            OperationRequest *q=mmm_request(s,BOARD_NET_COCK);
            if(q) { q->action=NET_SCAN_COMMIT;q->until=u->until;mmm_submit(s,q,JOB_NET_OPERATION); }
        }
        u->scanning=u->continuous=0;
        mmm_say(s,"No more new messages.\r\n","新着巡回を終了しました。\r\n");home(s);return;
    }
    char id[16];snprintf(id,sizeof(id),"#%u",u->scan_boards[u->scan_index++]);
    choose_board(s,id,0);
}

void net_cock_board(Session *s) {
    NetCockUi *u=nc(s);
    u->selected=u->parent=u->last_read=u->list_cursor=0;u->history_count=0;
    u->common.screen=OPEN_SCREEN;
    if(u->write_board) { u->write_board=0;mmm_ui_prepare_post(s,0); }
    else flat_page(s,0,0,u->scanning?NC_HEADER_BODY:NC_HEADERS);
}

void net_cock_boards(Session *s,const ArticleBoardsPage *page) {
    MmmUi *m=&nc(s)->common;
    m->row_count=page->count;m->next=page->next;
    if(!page->count)mmm_say(s,"No available boards.\r\n","選べる掲示板はありません。\r\n");
    for(unsigned i=0;i<page->count;i++) {
        const ArticleBoardRow *row=&page->rows[i];
        m->rows[i]=row->id;
        char text[128];
        if(!m->parent)snprintf(text,sizeof(text),"%u/%c  #%u  %s\r\n",i+1,'A'+(int)i,row->id,row->title);
        else snprintf(text,sizeof(text),"%u  #%u  %s\r\n",i+1,row->id,row->title);
        session_impl_queue_text(s,text);
    }
    net_cock_menu(s,1);
}

static void read_header(Session *s,const OperationContent *c) {
    NetCockUi *u=nc(s);
    char date[32],line[320],parent[40]="";
    date_text(c->at,date);
    if(c->parent)snprintf(parent,sizeof(parent),"  Reply to #%u",c->parent);
    snprintf(line,sizeof(line),"%s (#%u) %s %s\r\n  %zu bytes  %s%s\r\n",
             u->common.board_title,c->id,date,c->title,c->size,c->author,parent);
    session_impl_queue_text(s,line);
}

static void show_page(Session *s,const ArticlePage *page) {
    NetCockUi *u=nc(s);
    u->common.screen=OPEN_SCREEN;
    if(!page->count) {
        if(page->next)flat_page(s,page->next,u->direction,u->page);
        else if(u->scanning)scan_board(s);
        else { u->continuous=0;mmm_say(s,"No more messages.\r\n","表示できるメッセージはありません。\r\n"); }
        return;
    }
    if(u->page==NC_HEADERS || u->page==NC_HEADER_BODY) {
        header(s,page->rows[0].id);return;
    }
    u->list_cursor=page->next;
    for(unsigned i=0;i<page->count;i++) {
        const ArticleSummary *a=&page->rows[i];char line[180],date[32];
        date_text(a->created,date);
        snprintf(line,sizeof(line),"#%u  %s  %s%s%s\r\n",a->id,date,a->title,
                 u->page==NC_LIST_NAMES?"  ":"",u->page==NC_LIST_NAMES?a->author:"");
        session_impl_queue_text(s,line);
    }
}

static void next_article(Session *s) {
    NetCockUi *u=nc(s);
    flat_page(s,u->selected?u->selected+1:0,0,u->continuous?NC_HEADER_BODY:NC_HEADERS);
}

static void read_line(Session *s,NetInput c) {
    NetCockUi *u=nc(s);
    uint32_t id;
    if(!*c.word || same(c.word,"R")) { body(s);return; }
    if(same(c.word,"V")) { u->continuous=1;body(s);return; }
    if(same(c.word,"Q") || !strcmp(c.word,"0")) { home(s);return; }
    if(same(c.word,"A") || same(c.word,"L") || same(c.word,"MORE")) {
        if(same(c.word,"MORE") && !u->list_cursor) { invalid(s);return; }
        NetCockPage page=same(c.word,"MORE")?u->page:same(c.word,"A")?NC_LIST_NAMES:NC_LIST_IDS;
        flat_page(s,same(c.word,"MORE")?u->list_cursor:0,0,page);return;
    }
    if(same(c.word,"N")) { next_article(s);return; }
    if(same(c.word,"B")) {
        if(u->selected>1)flat_page(s,u->selected-1,1,NC_HEADERS);
        else invalid(s);
        return;
    }
    if(same(c.word,"T") || same(c.word,"E")) {
        flat_page(s,0,same(c.word,"E"),NC_HEADERS);return;
    }
    if(same(c.word,"P") || same(c.word,"F") || same(c.word,"W")) {
        id=same(c.word,"P")?u->selected:same(c.word,"F")?u->last_read:0;
        if(id || same(c.word,"W"))mmm_ui_prepare_post(s,id);
        else invalid(s);
        return;
    }
    if(same(c.word,"U")) {
        if(!u->parent || u->history_count==10) { invalid(s);return; }
        if(operation(s,NET_HEADER,u->parent,0)) { u->back_action=1;u->pending_history=u->selected; }
        return;
    }
    if(same(c.word,"I")) {
        if(!u->history_count) { invalid(s);return; }
        if(operation(s,NET_HEADER,u->history[u->history_count-1],0))u->back_action=-1;
        return;
    }
    if(same(c.word,"D") && u->selected) { u->input=NC_DELETE;return; }
    if(!strcmp(c.word,"!") && !*c.argument) {
        u->common.screen=MANAGE_SCREEN;mmm_manage_menu(s);return;
    }
    if(!strcmp(c.word,"$") && u->selected && (same(c.argument,"Y") || same(c.argument,"N"))) {
        operation(s,NET_SUBSCRIBE,s->core.board,same(c.argument,"Y"));return;
    }
    if(mmm_number(c.word,&id) && !*c.argument) { u->page=NC_HEADERS;header(s,id);return; }
    invalid(s);
}

static void select_line(Session *s,NetInput c) {
    NetCockUi *u=nc(s);
    MmmUi *m=&u->common;
    uint32_t id;
    if(same(c.word,"Q") || !strcmp(c.word,"0")) {
        if(m->depth) { m->parent=m->ancestors[--m->depth];mmm_ui_boards(s,0); }
        else home(s);
    } else if(!strcmp(c.word,"?") || !strcmp(c.word,".") || !*c.word)mmm_ui_boards(s,0);
    else if(same(c.word,"MORE") && m->next)mmm_ui_boards(s,m->next);
    else if(c.word[0] && !c.word[1] && strchr("ABCDEFGHIJKLMNOabcdefghijklmno",c.word[0]) && !m->parent) {
        unsigned index=(unsigned)(c.word[0]>='a'?c.word[0]-'a':c.word[0]-'A');
        if(index<m->row_count)mmm_ui_subboards(s,m->rows[index]);
        else invalid(s);
    } else if(mmm_number(c.word,&id) && !*c.argument && id<=m->row_count) {
        char target[16];snprintf(target,sizeof(target),"#%u",m->rows[id-1]);choose_board(s,target,u->write_board);
    } else if(!*c.argument)choose_board(s,c.word,u->write_board);
    else invalid(s);
}

static void save_draft(Session *s) {
    MmmUi *m=&nc(s)->common;
    if(!*m->title) { invalid(s);return; }
    OperationRequest *q=mmm_request(s,m->mail_to?'m':'w');
    if(!q)return;
    q->action='W';q->number=m->mail_to?m->mail_to:m->post_parent;
    q->cursor=m->mail_to?m->mail_parent:m->note_draft_revision;
    q->mail_revision=m->mail_revision;q->mail_attachments=m->attachments;
    if(!m->mail_to)q->board=m->note_board;
    strcpy(q->title,m->title);strcpy(q->body,m->body);
    mmm_submit(s,q,JOB_NET_DRAFT_SAVE);
}

int net_cock_editor(Session *s,char *text) {
    NetCockUi *u=nc(s);
    MmmUi *m=&u->common;
    if(m->input!=INPUT_EDITOR)return 0;
    if(u->input==NC_BODY_EDIT) {
        if(same(text,"W") || same(text,"Q")) { u->input=NC_IDLE;return 1; }
        return 0;
    }
    if(!*text || same(text,"Y"))mmm_ui_editor(s,"w");
    else if(same(text,"N"))mmm_ui_editor(s,"q");
    else if(same(text,"T")) { m->input=INPUT_NONE;u->input=NC_TITLE_EDIT; }
    else if(same(text,"E"))u->input=NC_BODY_EDIT;
    else if(same(text,"C")) { session_impl_queue_text(s,m->title);session_impl_queue_text(s,"\r\n");session_impl_queue_text(s,m->body); }
    else if(same(text,"Q"))save_draft(s);
    else if(!strncmp(text,"attach ",7) || !strncmp(text,"detach ",7))return 0;
    else invalid(s);
    return 1;
}

static void password(Session *s,char *line) {
    NetCockUi *u=nc(s);
    char mode=u->input==NC_PASSWORD_OLD?'o':u->input==NC_PASSWORD_NEW?'p':'c';
    size_t n=strlen(line);
    PasswordAction action=password_flow_action(mode,1,same(line,"/CANCEL"),n,
        !strcmp(line,u->old_password),!strcmp(line,u->new_password));
    if(action==PASSWORD_STORE_OLD) { memcpy(u->old_password,line,n+1);u->input=NC_PASSWORD_NEW; }
    else if(action==PASSWORD_STORE_NEW) { memcpy(u->new_password,line,n+1);u->input=NC_PASSWORD_CONFIRM; }
    else {
        if(action==PASSWORD_CHANGE) {
            OperationRequest *q=mmm_request(s,'k');
            if(q) { strcpy(q->password,u->old_password);strcpy(q->new_password,u->new_password);mmm_submit(s,q,JOB_NET_OPERATION); }
            session_core_throttle(&s->core,s->core.now);
        } else if(action!=PASSWORD_CANCEL)invalid(s);
        u->input=NC_IDLE;
        OPENSSL_cleanse(u->old_password,sizeof(u->old_password));
        OPENSSL_cleanse(u->new_password,sizeof(u->new_password));
    }
    OPENSSL_cleanse(line,n);
}

static void move_pointer(Session *s,char *arg) {
    uint64_t value=0;
    if(same(arg,"E")) {
        time_t now=time(NULL);if(now<0) { invalid(s);return; }value=(uint64_t)now;
    } else if(same(arg,"L"))value=s->core.previous_login;
    else if(!same(arg,"T")) {
        NetInput date=net_cock_split(arg);
        if(!same(date.word,"D") || !date_filter(date.argument,&value)) { invalid(s);return; }
    }
    OperationRequest *q=mmm_request(s,BOARD_NET_COCK);
    if(q) { q->action=NET_MOVE;q->until=value;mmm_submit(s,q,JOB_NET_OPERATION); }
}

static void subscribe(Session *s,char *arg) {
    if(!*arg) { operation(s,NET_SUBSCRIPTION_NEXT,0,0);return; }
    if(same(arg,"LIST")) { operation(s,NET_SCANLIST,0,0);return; }
    NetInput c=net_cock_split(arg);
    uint32_t id;
    if(mmm_number(c.word,&id) && (same(c.argument,"Y") || same(c.argument,"N")))
        operation(s,NET_SUBSCRIBE,id,same(c.argument,"Y"));
    else invalid(s);
}

static void batch_line(Session *s,NetInput c) {
    NetCockUi *u=nc(s);
    BatchSelection *selection=&u->common.files;
    uint32_t id;
    if(same(c.word,"N") || same(c.word,"Q") || !strcmp(c.word,"0"))u->in_batch=0;
    else if(same(c.word,"C"))batch_reset(selection);
    else if(same(c.word,"D") && mmm_number(c.argument,&id))batch_remove(selection,id);
    else if(same(c.word,"Y") || same(c.word,"O")) {
        char empty[]="";
        mmm_ui_filer_command(s,"batch",empty);
        u->close_after_batch=same(c.word,"O") && s->connection.zmodem_start;
    } else if(!*c.word || same(c.word,"L") || !strcmp(c.word,"?")) {
        for(unsigned i=0;i<selection->count;i++) {
            char line[64];snprintf(line,sizeof(line),"#%u  %s\r\n",selection->ids[i],selection->done[i]?"done":"pending");
            session_impl_queue_text(s,line);
        }
        if(!selection->count)mmm_say(s,"Batch list is empty.\r\n","バッチ一覧は空です。\r\n");
        net_cock_menu(s,1);
    } else invalid(s);
}

void net_cock_transfer_finished(Session *s,int ok) {
    NetCockUi *u=nc(s);
    int close=u->close_after_batch;
    u->close_after_batch=0;
    if(close && ok && !batch_ids(&u->common.files,1).count)session_core_close(&s->core);
}

static void vault_line(Session *s,NetInput c) {
    NetCockUi *u=nc(s);
    char id_string[16];
    uint32_t id=u->transfer_file;
    if(same(c.word,"Q") || !strcmp(c.word,"0")) { home(s);return; }
    if(same(c.word,"W") && !*c.argument) { u->input=NC_VAULT_UPLOAD;return; }
    const char *action=NULL;
    const char *protocol=NULL;
    if(same(c.word,"L") || same(c.word,"A"))action="list";
    else if(same(c.word,"R") || !*c.word)action="info";
    else if(same(c.word,"S"))action="add";
    else if(same(c.word,"X")) { action="get";protocol="xmodem"; }
    else if(same(c.word,"Y")) { action="get";protocol="ymodem"; }
    else if(same(c.word,"Z")) { action="get";protocol="zmodem"; }
    if(action && (same(action,"list") || (*c.argument?mmm_number(c.argument,&id):id!=0))) {
        snprintf(id_string,sizeof(id_string),"%u",id);
        if(protocol && !mmm_ui_transfer_protocol(s,protocol)) { invalid(s);return; }
        mmm_ui_filer_command(s,action,id_string);
        return;
    }
    if(mmm_number(c.word,&id) && !*c.argument) {
        snprintf(id_string,sizeof(id_string),"%u",id);mmm_ui_filer_command(s,"info",id_string);
    } else if(!mmm_ui_filer_command(s,same(c.word,"RETRY")?"put":c.word,
                                   same(c.word,"RETRY")?u->upload_description:c.argument))invalid(s);
}

static void mail_sequence(Session *s,uint32_t cursor,int descending) {
    OperationRequest *q=mmm_request(s,'m');
    if(q) { q->action='f';q->cursor=cursor;q->descending=descending;mmm_submit(s,q,JOB_MAIL); }
}

static void mail_line(Session *s,NetInput c) {
    NetCockUi *u=nc(s);
    uint32_t id;
    char id_string[16];
    if(same(c.word,"V") && u->parent && !*c.argument) {
        mmm_simple_operation(s,'m','R',u->parent,JOB_MAIL);return;
    }
    if(same(c.word,"Q") || !strcmp(c.word,"0")) { home(s);return; }
    if(same(c.word,"T")) { mail_sequence(s,1,0);return; }
    if(!*c.word || same(c.word,"N")) { mail_sequence(s,u->selected+1,0);return; }
    if(same(c.word,"B")) {
        mail_sequence(s,u->selected>1?u->selected-1:0,1);
        return;
    }
    if(same(c.word,"E")) { mmm_simple_operation(s,'m','T',0,JOB_MAIL);return; }
    if(same(c.word,"R") && !*c.argument) {
        if(u->selected)mmm_simple_operation(s,'m','R',u->selected,JOB_MAIL);
        else mail_sequence(s,1,0);
        return;
    }
    if(same(c.word,"P") || same(c.word,"F")) {
        id=same(c.word,"P")?u->selected:u->last_read;
        if(id)mmm_simple_operation(s,'m','Q',id,JOB_MAIL);
        else invalid(s);
        return;
    }
    if(same(c.word,"D") && u->selected && !*c.argument) { u->input=NC_DELETE;return; }
    if(same(c.word,"W")) {
        if(!*c.argument)u->input=NC_MAIL_TO;
        else if(!mmm_ui_mail_command(s,"send",c.argument))invalid(s);
        return;
    }
    if(mmm_number(c.word,&id) || (same(c.word,"R") && mmm_number(c.argument,&id))) {
        snprintf(id_string,sizeof(id_string),"%u",id);mmm_ui_mail_command(s,"read",id_string);return;
    }
    if(!mmm_ui_mail_command(s,same(c.word,"L") || same(c.word,"A")?"inbox":c.word,c.argument))invalid(s);
}

static void list_peers(Session *s) {
    NetCockUi *u=nc(s);
    ChatPresence presence;
    u->peer_count=0;
    if(!chat_presence(s->core.id,s->core.user,0,&presence))return;
    for(unsigned i=0;i<presence.count;i++) {
        ChatPresenceRow *peer=&presence.rows[i];
        if(!*peer->login || peer->connection==s->core.id)continue;
        u->peers[u->peer_count++]=*peer;
        char text[192];snprintf(text,sizeof(text),"%lu  %s  %s%s\r\n",peer->connection,peer->login,
                               peer->name,peer->accepts_telegram?"":" (closed)");
        session_impl_queue_text(s,text);
    }
    if(!u->peer_count)mmm_say(s,"No other connections.\r\n","他の接続者はいません。\r\n");
}

static void private_line(Session *s,char *arg) {
    NetInput c=net_cock_split(arg);
    NetCockUi *u=nc(s);
    uint32_t connection;
    if(mmm_number(c.word,&connection) && chat_text_valid(c.argument)) {
        for(unsigned i=0;i<u->peer_count;i++) {
            ChatPresenceRow *peer=&u->peers[i];
            if(peer->connection!=connection || !peer->accepts_telegram)continue;
            OperationRequest *q=mmm_request(s,'&');
            if(q) {
                q->action='D';q->chat_target=peer->connection;q->chat_generation=peer->generation;
                strcpy(q->body,c.argument);mmm_submit(s,q,JOB_CHAT);
            }
            return;
        }
    }
    mmm_say(s,"Use .L to select a current line.\r\n",".L で現在の回線番号を確認してください。\r\n");
}

static void chat_line(Session *s,char *line) {
    if(*line!='.') {
        if(chat_text_valid(line))mmm_chat_request(s,'S',0,line);
        else invalid(s);
        return;
    }
    NetInput c=net_cock_split(line);
    if(same(c.word,".Q") || same(c.word,".OFF")) {
        chat_leave(s->core.id);nc(s)->common.chatting=0;home(s);
    } else if(same(c.word,".L"))list_peers(s);
    else if(same(c.word,".P"))private_line(s,c.argument);
    else if(same(c.word,".S") || same(c.word,".O")) {
        NetCockUi *u=nc(s);
        uint32_t id;
        if(mmm_number(c.argument,&id))for(unsigned i=0;i<u->peer_count;i++) {
            if(u->peers[i].connection!=id)continue;
            OperationRequest *q=mmm_request(s,'&');
            if(q) {
                q->action='p';q->number=same(c.word,".O")?CHAT_PREF_MUTE:CHAT_PREF_UNMUTE;
                strcpy(q->body,u->peers[i].login);mmm_submit(s,q,JOB_CHAT);
            }
            return;
        }
        invalid(s);
    }
    else if(same(c.word,".C") && chat_text_valid(c.argument))mmm_chat_request(s,'C',0,c.argument);
    else if((same(c.word,".T") || same(c.word,".B")) && chat_text_valid(c.argument))
        mmm_chat_request(s,'S',0,c.argument);
    else if(!strcmp(c.word,".?"))net_cock_menu(s,1);
    else invalid(s);
}

static void topic_request(Session *s,ConferenceAction action,uint32_t id,const char *text) {
    if(text && !mmm_ui_text_valid(text,CONFERENCE_TEXT)) { invalid(s);return; }
    OperationRequest *q=mmm_request(s,BOARD_CONFERENCE);
    if(q) {
        q->conference.action=action;q->conference.cursor=id;
        if(text)strcpy(q->conference.text,text);
        mmm_submit(s,q,JOB_NET_TOPIC);
    }
}

static void topic(Session *s,char *arg) {
    NetInput c=net_cock_split(arg);
    NetCockUi *u=nc(s);
    uint32_t id;
    if(!*c.word)topic_request(s,TOPIC_UNREAD,0,NULL);
    else if(same(c.word,"ALL"))topic_request(s,TOPIC_HISTORY,0,NULL);
    else if(same(c.word,"MORE") && u->topic_next)topic_request(s,u->topic_mode,u->topic_next,NULL);
    else if(same(c.word,"-W") && *c.argument)topic_request(s,TOPIC_POST,0,c.argument);
    else if(same(c.word,"-D") && mmm_number(c.argument,&id))topic_request(s,TOPIC_DELETE,id,NULL);
    else mmm_say(s,"TOPIC [ALL|MORE|-W TEXT|-D ID]\r\n","TOPIC [ALL 履歴|MORE 続き|-W 本文|-D 番号]\r\n");
}

static void find_article(Session *s,char *arg) {
    if(!*arg || !mmm_ui_text_valid(arg,80)) { invalid(s);return; }
    OperationRequest *q=mmm_request(s,'Z');
    if(q) {
        q->action=3;q->scope=ARTICLE_HERE;strcpy(q->username,"*");strcpy(q->title,arg);
        mmm_submit(s,q,JOB_NET_FIND);
    }
}

static void dispatch(Session *s,NetCommand cmd,char *arg) {
    NetCockUi *u=nc(s);
    MmmUi *m=&u->common;
    uint32_t id;
    switch(cmd) {
    case NC_BREAD: case NC_BWRITE:
        if(same(arg,"PDS")) { m->screen=FILER_SCREEN;u->transfer_file=0;mmm_file_enter(s,""); }
        else choose_board(s,arg,cmd==NC_BWRITE);
        break;
    case NC_MODE:
        if(same(arg,"MENU") || same(arg,"COMMAND"))operation(s,NET_SET_MODE,same(arg,"COMMAND"),0);
        else if(!*arg)mmm_say(s,"MODE MENU | MODE COMMAND\r\n","MODE MENU（番号）/ MODE COMMAND（コマンド）\r\n");
        else invalid(s);
        break;
    case NC_BSET: subscribe(s,arg);break;
    case NC_RALL: case NC_RNALL:
        if(*arg && !(cmd==NC_RNALL && same(arg,"N"))) { invalid(s);break; }
        if(operation(s,NET_SCAN_WINDOW,0,0)) {
            u->scanning=1;u->continuous=cmd==NC_RNALL;u->scan_index=0;
        }
        break;
    case NC_PMOVE: move_pointer(s,arg);break;
    case NC_BATCH: {
        u->in_batch=1;NetInput c=net_cock_split(arg);batch_line(s,c);break;
    }
    case NC_OFF: mmm_off(s,1);break;
    case NC_UI: mmm_ui_choose_profile(s,arg);break;
    case NC_PDS: m->screen=FILER_SCREEN;u->transfer_file=0;mmm_file_enter(s,arg);break;
    case NC_MREAD:
        m->screen=MAIL_SCREEN;u->selected=u->last_read=u->parent=0;
        if(!*arg)mmm_mail_list(s,MAIL_LIST_INBOX,0);
        else if(!mmm_ui_mail_command(s,"read",arg))invalid(s);
        break;
    case NC_MWRITE:
        m->screen=MAIL_SCREEN;
        if(!*arg)u->input=NC_MAIL_TO;
        else if(!mmm_ui_mail_command(s,"send",arg))invalid(s);
        break;
    case NC_MCHECK: m->screen=MAIL_SCREEN;mmm_mail_list(s,MAIL_LIST_SENT,0);break;
    case NC_MBSET:
        if(same(arg,"NOTIFY ON") || same(arg,"NOTIFY OFF")) {
            char on[]="on",off[]="off";
            mmm_ui_mail_command(s,"notify",same(arg,"NOTIFY ON")?on:off);
        } else mmm_say(s,"MBSET NOTIFY ON|OFF (notifications).\r\n","MBSET NOTIFY ON|OFF（通知設定）\r\n");
        break;
    case NC_CHAT: case NC_CON: mmm_chat_request(s,'J',0,"");break;
    case NC_COFF: chat_leave(s->core.id);m->chatting=0;home(s);break;
    case NC_LLIST: list_peers(s);break;
    case NC_TSET: {
        int mode=same(arg,"ON")?0:same(arg,"OFF")?1:-1;
        if(mode<0) { invalid(s);break; }
        OperationRequest *q=mmm_request(s,'&');
        if(q) { q->action='p';q->number=mode?CHAT_PREF_RECEIVE_OFF:CHAT_PREF_RECEIVE_ON;mmm_submit(s,q,JOB_CHAT); }
        break;
    }
    case NC_UREAD: mmm_profile(s,arg);break;
    case NC_UWRITE: if(!mmm_ui_term_command(s,"set",arg))invalid(s);break;
    case NC_USTAT: mmm_simple_operation(s,'!','V',0,JOB_MANAGE);break;
    case NC_LOG: {
        if(!*arg || mmm_number(arg,&id))mmm_simple_operation(s,'!','U',*arg?id:0,JOB_MANAGE);
        else invalid(s);
        break;
    }
    case NC_IDLIST: m->profile_filter[0]=0;m->profile_scope=0;mmm_ui_profile_search(s,0);break;
    case NC_PASS: if(!*arg)u->input=NC_PASSWORD_OLD;else invalid(s);break;
    case NC_VERSION: session_impl_queue_text(s,"Liner Notes | NET-COCK style\r\n");break;
    case NC_CLS: session_impl_queue_text(s,"\033[2J\033[H");break;
    case NC_JOIN: mmm_say(s,"Log out before applying for a new account.\r\n","入会申請の前にログアウトしてください。\r\n");break;
    case NC_GAMES:
        if(!*arg || mmm_number(arg,&id))mmm_games(s,*arg?id:0);
        else invalid(s);
        break;
    case NC_TALK: mmm_conference_enter(s,arg);break;
    case NC_TOPIC: topic(s,arg);break;
    case NC_DRAFT:
        if(same(arg,"MAIL")) {
            m->screen=MAIL_SCREEN;mmm_simple_operation(s,'m','O',0,JOB_MAIL);
        } else if(!*arg || same(arg,"ARTICLE"))mmm_simple_operation(s,'o',0,0,JOB_NET_DRAFT);
        else invalid(s);
        break;
    case NC_FIND: find_article(s,arg);break;
    case NC_ADMIN: m->screen=MANAGE_SCREEN;mmm_manage_menu(s);break;
    case NC_BCHANGE: if(!mmm_ui_manage_command(s,"boardset",arg))invalid(s);break;
    case NC_MAKEID: if(!mmm_ui_manage_command(s,"approve",arg))invalid(s);break;
    case NC_UDLIST: if(!mmm_ui_manage_command(s,"members",arg))invalid(s);break;
    case NC_REPORT: case NC_ACCESS: list_peers(s);break;
    case NC_EXCOM:
        mmm_say(s,"Liner Notes extensions: FIND / DRAFT / TOPIC / TALK / PDS / GAMES / ADMIN / UI\r\n",
                  "Liner Notes拡張: FIND / DRAFT / TOPIC / TALK / PDS / GAMES / ADMIN / UI\r\n");
        break;
    case NC_HELP: net_cock_help(s,arg);break;
    default: mmm_say(s,"Command unavailable in this profile.\r\n","このプロファイルでは利用できない操作です。\r\n");break;
    }
}

static int finish_input(Session *s,char *line) {
    NetCockUi *u=nc(s);
    MmmUi *m=&u->common;
    if(net_cock_secret(s)) { password(s,line);return 1; }
    if(u->input==NC_TITLE_EDIT) {
        EditorDocument d={m->title,m->body,m->undo_title,m->undo_body,&m->undo};
        if(same(line,"/CANCEL") || editor_apply(&d,EDITOR_SET_TITLE,0,0,line)==EDITOR_OK) {
            u->input=NC_IDLE;m->input=INPUT_EDITOR;
        } else invalid(s);
        return 1;
    }
    if(u->input==NC_SUBSCRIPTION) {
        if(same(line,"Q") || same(line,"/CANCEL"))u->input=NC_IDLE;
        else if(!*line)operation(s,NET_SUBSCRIPTION_NEXT,u->subscription_board,0);
        else if(same(line,"Y") || same(line,"N"))operation(s,NET_SUBSCRIBE,u->subscription_board,same(line,"Y"));
        else invalid(s);
        return 1;
    }
    if(u->input==NC_DELETE) {
        u->input=NC_IDLE;
        if(same(line,"Y")) {
            if(m->screen==MAIL_SCREEN)mmm_simple_operation(s,'m','D',u->selected,JOB_MAIL);
            else {
                OperationRequest *q=mmm_request(s,BOARD_NET_COCK);
                if(q) { q->action=NET_WITHDRAW;q->number=u->selected;q->cursor=1;mmm_submit(s,q,JOB_NET_OPERATION); }
            }
        }
        return 1;
    }
    if(u->input==NC_MAIL_TO) {
        u->input=NC_IDLE;
        if(!same(line,"/CANCEL") && !mmm_ui_mail_command(s,"send",line))invalid(s);
        return 1;
    }
    if(u->input==NC_VAULT_UPLOAD) {
        if(same(line,"/CANCEL"))u->input=NC_IDLE;
        else if(*line && mmm_ui_text_valid(line,240) && !strchr(line,'|')) {
            strcpy(u->upload_description,line);u->input=NC_IDLE;
            mmm_ui_filer_command(s,"put",u->upload_description);
        } else invalid(s);
        return 1;
    }
    return 0;
}

void net_cock_line(Session *s,char *line) {
    NetCockUi *u=nc(s);
    MmmUi *m=&u->common;
    if(finish_input(s,line))return;
    if(m->screen==CHAT_SCREEN) { chat_line(s,line);return; }
    if(m->screen==TALK_SCREEN) {
        if(same(line,"Q") || !strcmp(line,"0"))home(s);
        else mmm_conference_line(s,line);
        return;
    }
    NetInput c=net_cock_split(line);
    if(!s->core.user) {
        if(!*c.word || !strcmp(c.word,"?"))net_cock_menu(s,1);
        else if(same(c.word,"JOIN") || same(c.word,"REGISTER"))mmm_ui_auth_start(s,c.argument,'g');
        else if(same(c.word,"LOGIN"))mmm_ui_auth_start(s,c.argument,'a');
        else if(same(c.word,"UI"))mmm_ui_choose_profile(s,c.argument);
        else if(!*c.argument)mmm_ui_auth_start(s,c.word,'a');
        else invalid(s);
        return;
    }
    if(u->in_batch) { batch_line(s,c);return; }
    if(!strcmp(c.word,"?")) { net_cock_menu(s,1);return; }
    if(same(c.word,"LOGOUT") && m->screen==MAIN_SCREEN && !*c.argument) {
        char logout[]="logout";Language language=m->language;
        mmm_ui_shared_command(s,logout);
        if(!s->core.user) { OPENSSL_cleanse(u,sizeof(*u));u->common.language=language; }
        return;
    }
    if(*c.word=='\\' && !*c.argument) { home(s);choose_board(s,c.word+1,0);return; }
    if(u->input==NC_BOARD_SELECT || m->screen==NOTES_SCREEN) { select_line(s,c);return; }
    if(m->screen==OPEN_SCREEN || m->screen==INDEX_SCREEN) {
        if(same(c.word,"FIND"))dispatch(s,NC_FIND,c.argument);
        else if(same(c.word,"DRAFT"))dispatch(s,NC_DRAFT,c.argument);
        else read_line(s,c);
        return;
    }
    if(m->screen==MAIL_SCREEN) { mail_line(s,c);return; }
    if(m->screen==FILER_SCREEN) {
        if(same(c.word,"BATCH"))dispatch(s,NC_BATCH,c.argument);
        else if(same(c.word,"PROTOCOL")) {
            for(char *p=c.argument;*p;p++)if(*p>='A' && *p<='Z')*p=(char)(*p-'A'+'a');
            if(!mmm_ui_transfer_protocol(s,c.argument))invalid(s);
        } else vault_line(s,c);
        return;
    }
    if(m->screen==MANAGE_SCREEN) {
        if(same(c.word,"Q") || !strcmp(c.word,"0"))home(s);
        else if(net_cock_command(c.word)>=NC_BMAKE)dispatch(s,net_cock_command(c.word),c.argument);
        else if(!mmm_ui_manage_command(s,c.word,c.argument))invalid(s);
        return;
    }
    uint32_t number;
    if(m->screen==GAMES_SCREEN) {
        if(same(c.word,"Q") || !strcmp(c.word,"0"))home(s);
        else if(mmm_number(c.word,&number) && !*c.argument)mmm_games(s,number);
        else invalid(s);
        return;
    }
    if(!strcmp(c.word,"0")) { home(s);return; }
    NetCommand cmd;
    if(!u->mode && mmm_number(c.word,&number) && !*c.argument) {
        cmd=net_cock_menu_command(u->menu,number,1,s->core.admin);
        if(u->menu==NC_MAIN_MENU) {
            if(cmd==NC_UREAD)u->menu=NC_USER_MENU;
            else if(cmd==NC_BREAD)u->menu=NC_BOARD_MENU;
            else if(cmd==NC_MREAD)u->menu=NC_MAIL_MENU;
            else if(cmd==NC_CHAT)u->menu=NC_CHAT_MENU;
            else if(cmd==NC_JOIN)u->menu=NC_REGISTER_MENU;
            else { dispatch(s,cmd,c.argument);return; }
            net_cock_menu(s,1);return;
        }
    } else cmd=net_cock_command(c.word);
    if(!*c.word)net_cock_menu(s,1);
    else dispatch(s,cmd,c.argument);
}

static void setting_result(Session *s,const OperationResult *r,const OperationPage *page) {
    NetCockUi *u=nc(s);
    if(r->service_action==NET_MODE || r->service_action==NET_SET_MODE) {
        u->mode=r->service_number!=0;u->restore_mode=0;
        if(r->service_action==NET_SET_MODE)mmm_say(s,"Mode saved.\r\n","モードを保存しました。\r\n");
        net_cock_menu(s,0);
    } else if(r->service_action==NET_LOOKUP && r->service_number) {
        char id[16];snprintf(id,sizeof(id),"#%u",r->service_number);choose_board(s,id,u->write_board);
    } else if((r->service_action==NET_SCANLIST || r->service_action==NET_SCAN_WINDOW) &&
              page && page->kind==OPERATION_PAGE_MMM_SCANLIST) {
        const MmmScanList *list=&page->data.mmm_scanlist;
        if(r->service_action==NET_SCAN_WINDOW) {
            u->since=r->since;u->until=r->until;u->scan_index=0;u->scan_count=list->count;
            for(unsigned i=0;i<list->count;i++)u->scan_boards[i]=list->rows[i].board;
            scan_board(s);
        } else {
            mmm_say(s,"BSET BOARD Y|N\r\n","BSET 掲示板番号 Y|N\r\n");
            for(unsigned i=0;i<list->count;i++) {
                char line[110];snprintf(line,sizeof(line),"#%u  %s\r\n",list->rows[i].board,list->rows[i].title);
                session_impl_queue_text(s,line);
            }
        }
    } else if(r->service_action==NET_SUBSCRIPTION_NEXT) {
        if(!r->service_number) {
            u->input=NC_IDLE;mmm_say(s,"End of board settings.\r\n","購読設定を終了しました。\r\n");
        } else {
            char row[152];
            snprintf(row,sizeof(row),"#%u %s [%c]\r\n",r->service_number,r->service_text,r->service_next?'Y':'N');
            session_impl_queue_text(s,row);
            u->subscription_board=r->service_number;u->input=NC_SUBSCRIPTION;
        }
    } else if(r->service_action==NET_SUBSCRIBE && u->input==NC_SUBSCRIPTION) {
        operation(s,NET_SUBSCRIPTION_NEXT,u->subscription_board,0);
    } else if(r->service_action==NET_WITHDRAW) {
        mmm_say(s,"Message withdrawn.\r\n","メッセージを取り消しました。\r\n");next_article(s);
    } else mmm_say(s,"Setting saved.\r\n","設定を保存しました。\r\n");
}

static void topic_result(Session *s,const ConferenceReply *r) {
    NetCockUi *u=nc(s);
    if(r->action==TOPIC_POST)mmm_say(s,"Topic posted.\r\n","伝言を書き込みました。\r\n");
    else if(r->action==TOPIC_DELETE)mmm_say(s,"Topic deleted.\r\n","伝言を削除しました。\r\n");
    else if(r->action!=TOPIC_SEEN) {
        u->topic_next=r->next;u->topic_mode=r->action;
        for(unsigned i=0;i<r->count;i++) {
            const ConferenceLine *row=&r->rows.lines[i];
            char text[656];snprintf(text,sizeof(text),"Topic #%u  %s\r\n%s\r\n",row->id,row->name,row->text);
            session_impl_queue_text(s,text);
        }
        if(!r->count)mmm_say(s,"No topic entries.\r\n","伝言はありません。\r\n");
        if(r->action==TOPIC_UNREAD && r->count && s->core.active && !s->core.closing)
            topic_request(s,TOPIC_SEEN,r->position,NULL);
    }
}

void net_cock_result(Session *s,MmmJob job,const OperationResult *r,SessionResultView result) {
    NetCockUi *u=nc(s);
    MmmUi *m=&u->common;
    const OperationPage *page=result.page;
    const OperationContent *content=result.content;
    if(result.status!=SESSION_RESULT_OK) {
        if(job==JOB_NET_PAGE || job==JOB_NET_BODY || job==JOB_NET_HEADER ||
           job==JOB_NET_SEEN || job==JOB_BOARD || job==JOB_NET_OPERATION) {
            u->continuous=u->scanning=0;u->back_action=0;u->restore_mode=0;
        }
        if(job==JOB_TRANSFER)u->close_after_batch=0;
        return;
    }
    switch(job) {
    case JOB_NET_PAGE: case JOB_NET_FIND:
        if(page && page->kind==OPERATION_PAGE_ARTICLES) {
            if(job==JOB_NET_FIND)u->page=NC_LIST_NAMES;
            show_page(s,&page->data.articles);
        }
        break;
    case JOB_NET_HEADER:
        if(content && content->state==ARTICLE_VISIBLE) {
            if(u->back_action==1 && u->history_count<10)u->history[u->history_count++]=u->pending_history;
            if(u->back_action==-1 && u->history_count)u->history_count--;
            u->back_action=0;u->selected=content->id;u->parent=content->parent;
            read_header(s,content);
            if(u->page==NC_HEADER_BODY)body(s);
        }
        break;
    case JOB_NET_BODY:
        if(content && content->state==ARTICLE_VISIBLE && result.body) {
            session_impl_queue_text(s,result.body);
            session_impl_queue_text(s,"\r\n");
            u->last_read=content->id;
            if(!s->core.active || s->core.closing)break;
            OperationRequest *q=mmm_request(s,'s');
            if(q) { q->number=content->id;if(mmm_submit(s,q,JOB_NET_SEEN))break; }
            u->continuous=u->scanning=0;
        } else { u->continuous=u->scanning=0;mmm_say(s,"Message unavailable.\r\n","メッセージを表示できません。\r\n"); }
        break;
    case JOB_NET_SEEN: if(!u->continuous)next_article(s);break;
    case JOB_NET_OPERATION: setting_result(s,r,page);break;
    case JOB_NET_TOPIC: if(page && page->kind==OPERATION_PAGE_CONFERENCE)topic_result(s,&page->data.conference);break;
    case JOB_NET_DRAFT:
        if(r->draft_loaded) {
            mmm_clear_composition(m);m->note_board=r->board;m->note_draft_revision=r->draft_revision;
            m->post_parent=r->draft_parent;
            strcpy(m->title,r->draft_title);strcpy(m->body,r->draft_body);m->input=INPUT_EDITOR;
        } else mmm_say(s,"No article draft.\r\n","記事の下書きはありません。\r\n");
        break;
    case JOB_NET_DRAFT_SAVE:
        mmm_clear_composition(m);m->input=INPUT_NONE;
        mmm_say(s,"Draft saved.\r\n","下書きを保存しました。\r\n");break;
    case JOB_MAIL:
        if(content && content->kind==2) { u->selected=u->last_read=content->id;u->parent=content->parent; }
        else if(r->mail_action=='D')u->selected=0;
        break;
    case JOB_FILES: case JOB_FILE_ADD:
        if(page && page->kind==OPERATION_PAGE_FILE_DETAIL)u->transfer_file=page->data.file_detail.entry.id;
        break;
    default:break;
    }
}

void net_cock_tick(Session *s) {
    NetCockUi *u=nc(s);
    if(u->restore_mode) {
        u->restore_mode=0;
        if(!operation(s,NET_MODE,0,0))mmm_prompt(s);
    } else if(u->continuous && u->common.screen==OPEN_SCREEN &&
              (!s->connection.io.drained || s->connection.io.drained(s->connection.io.context)))next_article(s);
}

int net_cock_keys(const Session *s) {
    const NetCockUi *u=nc_view(s);
    return u->continuous && u->common.input==INPUT_NONE && u->input==NC_IDLE;
}

void net_cock_key(Session *s,unsigned char key) {
    if(key=='Q' || key=='q' || key==3) {
        home(s);
        mmm_prompt(s);
    }
}

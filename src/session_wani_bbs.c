#define _POSIX_C_SOURCE 200809L
#include "session_wani_bbs.h"
#include "wani_bbs_input.h"
#include "mmm_input.h"
#include "date_filter.h"
#include "editor_core.h"
#include "store_limits.h"
#include "session_policy_core.h"
#include "chat.h"
#include "vault_bridge.h"
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WaniUi *wani(Session *s) { return s->ui_binding.state; }
static const WaniUi *view(const Session *s) { return s->ui_binding.state; }

int wani_secret(const Session *s) { return view(s)->password_step!=0; }
int wani_ready(const Session *s) { return !view(s)->password_step && !view(s)->topic_step; }

int wani_prompt(Session *s) {
    const WaniUi *u=view(s);
    const MmmUi *m=&u->common;
    if(m->input==INPUT_EDITOR) {
        if(u->editor_cancel)mmm_say(s,"Discard edit (y/N)> ","編集を破棄しますか (y/N)> ");
        else session_impl_queue_text(s,"Editor> ");
        return 1;
    }
    if(m->input!=INPUT_NONE || m->screen==TALK_SCREEN)return 0;
    if(!s->core.user) { mmm_say(s,"Login> ","ログイン> ");return 1; }
    if(u->password_step) {
        static const char *const prompts[]={"","Current password> ","New password> ","Confirm password> "};
        session_impl_queue_text(s,prompts[u->password_step]);return 1;
    }
    if(u->topic_step) {
        session_impl_queue_text(s,u->topic_step==1?"Topic text> ":"Topic number> ");return 1;
    }
    char prompt[128];
    static const char *const paths[]={
        [NOTES_SCREEN]="/board", [FILER_SCREEN]="/pool", [GAMES_SCREEN]="/game",
        [TERM_SCREEN]="/system", [MANAGE_SCREEN]="/system/manage", [MAIL_SCREEN]="/system/mail",
        [CHAT_SCREEN]="/chat"
    };
    if(m->screen==BOARD_SCREEN || m->screen==INDEX_SCREEN || m->screen==OPEN_SCREEN)
        snprintf(prompt,sizeof(prompt),"/board/%u%s> ",s->core.board,
                 u->view==WANI_ARTICLE?" read":u->view==WANI_PAGES?" pages":u->view==WANI_INDEX?" index":"");
    else if(m->screen==FILER_SCREEN && *m->file_path)snprintf(prompt,sizeof(prompt),"/pool/%s> ",m->file_path);
    else {
        const char *path=m->screen<=CHAT_SCREEN || m->screen==TERM_SCREEN ||
                         m->screen==GAMES_SCREEN || m->screen==MANAGE_SCREEN?paths[m->screen]:NULL;
        snprintf(prompt,sizeof(prompt),"%s> ",path?path:"/");
    }
    session_impl_queue_text(s,prompt);
    return 1;
}

void wani_menu(Session *s,int requested) {
    (void)requested;
    const WaniUi *u=view(s);
    if(!s->core.user) {
        mmm_say(s,"WANI-BBS | LOGIN ID / REGISTER ID / ?\r\n","WANI-BBS | LOGIN 会員ID / REGISTER 入会 / ?\r\n");return;
    }
    switch(u->common.screen) {
    case MAIN_SCREEN:
        mmm_say(s,"board (b)  pool (p)  system (s)  game (g)\r\nl / cd PATH / read / open / topic / mail / ui / off / ?\r\n",
                  "board (b) 掲示板 / pool (p) 保管庫 / system (s) 設定 / game (g) ゲーム\r\nl / cd パス / read / open / topic / mail / ui / off / ?\r\n");break;
    case NOTES_SCREEN:
        mmm_say(s,"Number or name: select  l: list  more: next  q: up\r\n","番号・名前 選択 / l 一覧 / more 続き / q 上位\r\n");break;
    case BOARD_SCREEN: case INDEX_SCREEN: case OPEN_SCREEN:
        if(u->mode==WANI_VIEW_FLAT) {
            mmm_say(s,"Flat board: l / r [ID] / w / f TEXT / S personal / Q advance / Z interrupt\r\n",
                      "通常掲示板: l 一覧 / r [番号] / w 投稿・返信 / f 検索 / S 一時保存 / Q 更新 / Z 中断\r\n");
        } else {
            mmm_say(s,"Multi-notes: l index / number cover / . pages / w / S subscribe / U unsubscribe\r\n",
                      "マルチノーツ: l 索引 / 番号 表紙 / . ページ一覧 / w / S 購読 / U 解除\r\n");
        }
        break;
    case FILER_SCREEN: mmm_say(s,"l list / r ID / w description / x ID / f pattern / cd PATH / q\r\n",
                                  "l 一覧 / r 番号 / w 説明 / x 番号 / f 検索 / cd パス / q\r\n");break;
    case MAIL_SCREEN: mmm_say(s,"l inbox / r [ID] read / m USER / R reply / D ID confirm / S personal / q\r\n",
                                 "l 受信箱 / r [番号] / m 会員ID / R 返信 / D 番号 confirm / S 一時保存 / q\r\n");break;
    case TERM_SCREEN: mmm_say(s,"mail / prof / regprof / passwd / layout / seqset / subscriptions / manage\r\n",
                                 "mail 郵便受け / prof / regprof / passwd / layout / seqset / subscriptions / manage\r\n");break;
    case CHAT_SCREEN: mmm_say(s,"Enter text / /who / /wire ID TEXT / /leave\r\n","本文 発言 / /who / /wire 番号 本文 / /leave\r\n");break;
    case GAMES_SCREEN: mmm_say(s,"Number: start  l: list  q: up\r\n","番号 起動 / l 一覧 / q 上位\r\n");break;
    case MANAGE_SCREEN: mmm_manage_menu(s);break;
    case TALK_SCREEN: mmm_conference_menu(s);break;
    default:break;
    }
}

void wani_help(Session *s,const char *topic) { (void)topic;wani_menu(s,1); }

static int request(Session *s,int action,uint32_t number,int scope,MmmJob job) {
    OperationRequest *q=mmm_request(s,BOARD_WANI);
    if(!q)return 0;
    q->action=action;q->number=number;q->scope=scope;
    return mmm_submit(s,q,job);
}

static void board_id(Session *s,uint32_t id) {
    char target[16];
    snprintf(target,sizeof(target),"#%u",id);
    mmm_ui_enter_board(s,target,OPEN_BOARD_MENU);
}

static void board_name(Session *s,const char *target,uint32_t parent) {
    uint32_t id;
    if(mmm_number(*target=='#'?target+1:target,&id)) { board_id(s,id);return; }
    if(!*target || !mmm_ui_text_valid(target,80)) { mmm_bad_service_command(s);return; }
    OperationRequest *q=mmm_request(s,BOARD_WANI);
    if(q) { q->action=WANI_RESOLVE;q->number=parent;strcpy(q->title,target);mmm_submit(s,q,JOB_WANI_RESOLVE); }
}

static void home(Session *s) {
    WaniUi *u=wani(s);
    u->common.screen=MAIN_SCREEN;
    u->common.parent=u->common.depth=u->common.row_count=u->common.next=0;
    u->view=WANI_HOME_BOARD;u->traversal=0;u->opening=0;
}

static void list_articles(Session *s,WaniView target,uint32_t cursor,int descending,int first) {
    WaniUi *u=wani(s);
    OperationRequest *q=mmm_request(s,BOARD_WANI);
    if(!q)return;
    q->action=WANI_BROWSE;q->cursor=cursor;q->descending=descending;
    q->scope=target==WANI_PAGES?2:u->mode==WANI_VIEW_MNOTES?1:u->unread?3:0;
    q->number=u->root;q->notes_unread=u->unread && !descending;
    q->since=u->since;q->until=u->unread?u->until:0;
    if(target==WANI_INDEX && u->mode==WANI_VIEW_MNOTES && *u->query)strcpy(q->title,u->query);
    if(mmm_submit(s,q,JOB_WANI_PAGE)) {
        if(target==WANI_PAGES && !cursor)u->page_ordinal=1;
        u->view=target;u->descending=descending;u->read_first=first;
    }
}

static void open_article(Session *s,uint32_t id,uint32_t root,WaniView back) {
    WaniUi *u=wani(s);
    OperationRequest *q=mmm_request(s,'Z');
    if(!q)return;
    q->action=5;q->number=id;q->scope=root?2:1;q->cursor=root;
    if(mmm_submit(s,q,JOB_READ))u->back_from_read=back;
}

static void search_articles(Session *s,const char *term,uint32_t cursor) {
    WaniUi *u=wani(s);
    if(!*term || !mmm_ui_text_valid(term,80)) { mmm_bad_service_command(s);return; }
    if(u->mode==WANI_VIEW_MNOTES) {
        if(term!=u->query)strcpy(u->query,term);
        u->unread=0;list_articles(s,WANI_INDEX,cursor,0,0);return;
    }
    OperationRequest *q=mmm_request(s,'Z');
    if(!q)return;
    q->action=3;q->scope=ARTICLE_HERE;q->cursor=cursor;
    if(*term=='@' && term[1] && strlen(term+1)<=32 && !strpbrk(term+1," \t")) {
        strcpy(q->username,term+1);strcpy(q->title,"*");
    } else { strcpy(q->username,"*");strcpy(q->title,term); }
    if(mmm_submit(s,q,JOB_WANI_PAGE)) {
        if(term!=u->query)strcpy(u->query,term);
        u->view=WANI_INDEX;u->read_first=0;u->descending=0;
    }
}

static int update_sequence(Session *s) {
    WaniUi *u=wani(s);
    if(!u->until)return 0;
    OperationRequest *q=mmm_request(s,BOARD_WANI);
    if(!q)return 0;
    q->action=WANI_SEQUENCE_ADVANCE;q->until=u->until;
    u->until=0;
    return mmm_submit(s,q,JOB_WANI_CONFIG);
}

static void next_target(Session *s) {
    WaniUi *u=wani(s);
    if(u->scan_at>=u->scan_count) {
        int advance=u->traversal && u->scan_mark;
        u->traversal=u->opening=u->unread=0;u->common.screen=NOTES_SCREEN;
        if(advance)update_sequence(s);
        mmm_say(s,"End of board traversal.\r\n","掲示板巡回の末尾です。\r\n");return;
    }
    u->opening=1;
    board_id(s,u->scan[u->scan_at++]);
}

int wani_board_enter(Session *s) {
    return request(s,WANI_VIEW_GET,s->core.board,0,JOB_WANI_MODE);
}

static int start_scan(Session *s,char *args,int mode) {
    WaniUi *u=wani(s);
    int subscribed=0, option=0;
    while(*args=='-') {
        WaniCommand c=wani_command(args);
        if(!strcmp(c.word,"-f") && !subscribed)subscribed=1;
        else if(mode && !option && c.word[1] && !c.word[2] && strchr("six",c.word[1])) {
            option=1;mode=c.word[1]=='i'?2:c.word[1]=='x'?3:1;
        } else return 0;
        args=c.argument;
    }
    if((subscribed && *args) || !mmm_ui_text_valid(args,80))return 0;
    OperationRequest *q=mmm_request(s,BOARD_WANI);
    if(!q)return 1;
    q->action=subscribed?WANI_SUBSCRIPTIONS:WANI_SCAN_TARGETS;
    strcpy(q->title,args);
    if(mmm_submit(s,q,JOB_WANI_SCAN)) {
        u->scan_kind=mode;u->traversal=1;u->until=0;
        u->scan_mark=!subscribed && !*args && mode!=2;
    }
    return 1;
}

static void finish_read(Session *s,int update,int interrupt) {
    WaniUi *u=wani(s);
    WaniView previous=u->view;
    u->range_active=0;u->query[0]=0;u->view=WANI_HOME_BOARD;
    u->common.screen=BOARD_SCREEN;
    if(interrupt) {
        u->traversal=0;u->opening=0;u->unread=0;
        if(update)update_sequence(s);
    } else if(u->traversal) {
        if(!update)u->scan_mark=0;
        next_target(s);
    } else if(previous==WANI_HOME_BOARD)mmm_ui_boards(s,0);
}

static void render_list(Session *s,const ArticlePage *page) {
    WaniUi *u=wani(s);
    MmmUi *m=&u->common;
    if(!page->count && page->next) { list_articles(s,u->view,page->next,u->descending,u->read_first);return; }
    if(!page->count && u->view==WANI_INDEX && u->traversal && u->unread) {
        if(u->scan_kind==3) { u->unread=0;list_articles(s,WANI_INDEX,0,0,0); }
        else next_target(s);
        return;
    }
    if(u->read_first && u->view==WANI_PAGES && page->count) {
        u->row_count=0;
        open_article(s,page->rows[0].id,u->root,WANI_PAGES);
        return;
    }
    u->row_count=0;m->next=page->next;m->screen=INDEX_SCREEN;
    if(u->view==WANI_PAGES) {
        char note[64];snprintf(note,sizeof(note),"Note #%u pages\r\n",u->root);session_impl_queue_text(s,note);
    } else mmm_say(s,u->mode==WANI_VIEW_FLAT?"Flat articles\r\n":"Note index\r\n",
                    u->mode==WANI_VIEW_FLAT?"記事一覧\r\n":"ノート索引\r\n");
    for(unsigned i=0;i<page->count;i++) {
        const ArticleSummary *article=&page->rows[i];
        if(u->range_active && (u->descending?article->id<u->range_end:article->id>u->range_end)) { m->next=0;break; }
        u->row_ids[u->row_count++]=article->id;
        char row[180];
        if(u->view==WANI_PAGES)snprintf(row,sizeof(row),"Page %u  #%u  %s\r\n",u->page_ordinal+i,article->id,article->title);
        else snprintf(row,sizeof(row),"%s #%u  %s\r\n",u->mode==WANI_VIEW_FLAT?"Article":"Note",article->id,article->title);
        if(!u->read_first)session_impl_queue_text(s,row);
    }
    if(!u->row_count) {
        if(u->view==WANI_PAGES && u->read_first) {
            u->unread=0;list_articles(s,WANI_INDEX,0,0,0);
        } else mmm_say(s,"No matching visible entries.\r\n","表示できる対象がありません。\r\n");
    } else if(u->read_first)open_article(s,u->row_ids[0],u->view==WANI_PAGES?u->root:0,u->view);
}

void wani_render_article(Session *s,const OperationContent *entry) {
    WaniUi *u=wani(s);
    u->view=WANI_ARTICLE;u->root=entry->root;u->article=entry->id;u->parent=entry->parent;
    memcpy(u->author,entry->author,sizeof(u->author));
    const size_t size=strlen(entry->title);
    size_t length=size>76?76:size;
    while(length && ((unsigned char)entry->title[length]&0xc0)==0x80)length--;
    if(!strncmp(entry->title,"Re: ",4))strcpy(u->reply_title,entry->title);
    else snprintf(u->reply_title,sizeof(u->reply_title),"Re: %.*s",(int)length,entry->title);
    char line[196];
    const char *kind=u->mode==WANI_VIEW_FLAT?"Article":entry->id==entry->root?"Cover":"Page";
    snprintf(line,sizeof(line),"%s #%u  %s\r\n%s\r\n",kind,entry->id,entry->author,entry->title);
    session_impl_queue_text(s,line);
}

int wani_store_personal(Session *s,const char *body,size_t size) {
    MmmUi *m=&wani(s)->common;
    if(size>MMM_PFILE_MAX) {
        mmm_say(s,"Personal file exceeds 10 KiB.\r\n","一時ファイルの容量を超えます。\r\n");return 0;
    }
    OPENSSL_cleanse(m->pfile,sizeof(m->pfile));
    if(size)memcpy(m->pfile,body,size);
    mmm_say(s,"Personal file replaced.\r\n","一時ファイルを置き換えました。\r\n");
    return 1;
}

static void save_personal(Session *s,uint32_t id,int mail) {
    if(!id) { mmm_bad_service_command(s);return; }
    OperationRequest *q=mmm_request(s,mail?'m':'Z');
    if(q) { q->action=mail?'R':5;q->number=id;q->scope=mail?0:1;mmm_submit(s,q,JOB_WANI_PERSONAL); }
}

static void topic_result(Session *,const ConferenceReply *);

void wani_result(Session *s,MmmJob job,const OperationResult *r,SessionResultView result) {
    WaniUi *u=wani(s);
    MmmUi *m=&u->common;
    const OperationPage *page=result.page;
    if(job==JOB_WANI_MODE) {
        u->mode=r->service_number;u->view=WANI_HOME_BOARD;u->root=u->article=0;
        u->row_count=0;u->query[0]=0;u->range_active=0;u->unread=0;
        if(u->opening) {
            u->opening=0;
            if((u->scan_kind==0)!=(u->mode==WANI_VIEW_FLAT)) { next_target(s);return; }
            u->unread=u->scan_kind!=2;
            list_articles(s,WANI_INDEX,0,0,0);
        } else { session_impl_queue_text(s,m->board_title);session_impl_queue_text(s,"\r\n");wani_menu(s,1); }
    } else if(job==JOB_WANI_PAGE && page && page->kind==OPERATION_PAGE_ARTICLES)render_list(s,&page->data.articles);
    else if(job==JOB_WANI_RESOLVE && r->service_number)board_id(s,r->service_number);
    else if(job==JOB_WANI_WINDOW) {
        u->since=r->since;u->until=r->until;
        if(u->traversal)next_target(s);
        else {
            char line[112];snprintf(line,sizeof(line),"Sequencer: %llu\r\n",(unsigned long long)r->since);
            session_impl_queue_text(s,line);
        }
    } else if(job==JOB_WANI_SCAN && page && page->kind==OPERATION_PAGE_MMM_SCANLIST) {
        const MmmScanList *list=&page->data.mmm_scanlist;
        if(u->traversal) {
            u->scan_count=list->count;u->scan_at=0;
            for(unsigned i=0;i<list->count;i++)u->scan[i]=list->rows[i].board;
            request(s,WANI_SEQUENCE_GET,0,0,JOB_WANI_WINDOW);
        } else for(unsigned i=0;i<list->count;i++) {
            char line[112];snprintf(line,sizeof(line),"#%u  %s\r\n",list->rows[i].board,list->rows[i].title);
            session_impl_queue_text(s,line);
        }
    } else if(job==JOB_WANI_CONFIG) {
        mmm_say(s,"Setting saved.\r\n","設定を保存しました。\r\n");
        if(r->service_action==WANI_VIEW_SET && m->screen==BOARD_SCREEN)wani_board_enter(s);

    } else if(job==JOB_WANI_PERSONAL && result.content &&
              (result.content->kind==2 || result.content->state==ARTICLE_VISIBLE)) {
        wani_store_personal(s,result.body,result.content->size);
    } else if(job==JOB_WANI_PASSWORD)mmm_say(s,"Password changed.\r\n","パスワードを変更しました。\r\n");
    else if(job==JOB_WANI_SIGNATURE && m->input==INPUT_EDITOR) {
        EditorDocument d={m->title,m->body,m->undo_title,m->undo_body,&m->undo};
        if(!*r->draft_body || editor_apply(&d,EDITOR_APPEND_BLOCK,0,0,r->draft_body)!=EDITOR_OK)
            mmm_say(s,"Cannot insert signature.\r\n","署名を取り込めません。\r\n");
        else mmm_say(s,"Signature inserted.\r\n","署名を取り込みました。\r\n");
    }
    else if(job==JOB_WANI_TOPIC && page && page->kind==OPERATION_PAGE_CONFERENCE)topic_result(s,&page->data.conference);
    else if(job==JOB_MAIL && result.content && result.content->kind==2) {
        u->article=result.content->id;u->parent=result.content->parent;
        memcpy(u->author,result.content->author,sizeof(u->author));
    }
}

static void password_input(Session *s,char *line) {
    WaniUi *u=wani(s);
    const int mode=u->password_step==1?'o':u->password_step==2?'p':'c';
    size_t size=strlen(line);
    PasswordAction action=password_flow_action(mode,1,!strcmp(line,"/CANCEL"),size,
                                                !strcmp(line,u->old_password),!strcmp(line,u->new_password));
    if(action==PASSWORD_STORE_OLD) { strcpy(u->old_password,line);u->password_step=2; }
    else if(action==PASSWORD_STORE_NEW) { strcpy(u->new_password,line);u->password_step=3; }
    else {
        if(action==PASSWORD_CHANGE) {
            OperationRequest *q=mmm_request(s,'k');
            if(q) { strcpy(q->password,u->old_password);strcpy(q->new_password,u->new_password);mmm_submit(s,q,JOB_WANI_PASSWORD); }
            session_core_throttle(&s->core,s->core.now);
        } else if(action!=PASSWORD_CANCEL)mmm_say(s,"Password not changed.\r\n","パスワードを変更できませんでした。\r\n");
        OPENSSL_cleanse(u->old_password,sizeof(u->old_password));
        OPENSSL_cleanse(u->new_password,sizeof(u->new_password));
        u->password_step=0;
    }
    OPENSSL_cleanse(line,size);
}

static int insert_personal(EditorDocument *d,const char *personal,int quote) {
    if(!*personal)return 0;
    if(!quote)return editor_apply(d,EDITOR_APPEND_BLOCK,0,0,personal)==EDITOR_OK;
    char quoted[2*MMM_PFILE_MAX+1];
    size_t size=strlen(personal),cursor=0,used=0;
    EditorSpan line;
    int next;
    while((next=editor_next_line(personal,size,&cursor,&line))>0) {
        if(line.size+4>sizeof(quoted)-used-1)return 0;
        memcpy(quoted+used,"> ",2);used+=2;
        memcpy(quoted+used,line.text,line.size);used+=line.size;
        memcpy(quoted+used,"\r\n",2);used+=2;
    }
    quoted[used]=0;
    int ok=next==0 && editor_apply(d,EDITOR_APPEND_BLOCK,0,0,quoted)==EDITOR_OK;
    OPENSSL_cleanse(quoted,sizeof(quoted));
    return ok;
}

static int editor_command(Session *s,char *text) {
    MmmUi *m=&wani(s)->common;
    EditorDocument d={m->title,m->body,m->undo_title,m->undo_body,&m->undo};
    if(wani(s)->editor_cancel) {
        wani(s)->editor_cancel=0;
        if(wani_equal(text,"y"))mmm_ui_editor(s,"q");
        return 1;
    }
    if(wani_equal(text,"Q")) { wani(s)->editor_cancel=1;return 1; }
    if(!strcmp(text,"N")) {
        if(editor_remove_blank_lines(&d)!=EDITOR_OK)mmm_bad_service_command(s);
        return 1;
    }
    if(!strcmp(text,"X")) { mmm_simple_operation(s,BOARD_NOTE_VIEW,'g',0,JOB_WANI_SIGNATURE);return 1; }
    if(!strcmp(text,"S")) { wani_store_personal(s,m->body,strlen(m->body));return 1; }
    if(!strcmp(text,"K")) { wani_store_personal(s,"",0);return 1; }
    if(!strcmp(text,"F")) {
        char size[64];snprintf(size,sizeof(size),"Personal file: %zu bytes\r\n",strlen(m->pfile));session_impl_queue_text(s,size);return 1;
    }
    if(!strcmp(text,"R") || !strcmp(text,"Y")) {
        if(!insert_personal(&d,m->pfile,*text=='Y'))
            mmm_say(s,"Cannot insert personal file.\r\n","一時ファイルを取り込めません。\r\n");
        return 1;
    }
    if(*text=='E' || (*text && strchr("AUIDLPWQ",*text))) *text=*text=='E'?'r':(char)(*text-'A'+'a');
    return 0;
}

int wani_editor_line(Session *s,char **line) {
    WaniUi *u=wani(s);
    MmmUi *m=&u->common;
    if(m->input==INPUT_TITLE && u->returning) {
        u->returning=0;
        if(!**line) { mmm_ui_editor(s,u->reply_title);return 1; }
    }
    if(m->input==INPUT_BODY && !strcmp(*line,"\004")) { m->input=INPUT_EDITOR;return 1; }
    if(m->input==INPUT_EDITOR)return editor_command(s,*line);
    return 0;
}

static void open_mail(Session *s,char *recipient) {
    WaniUi *u=wani(s);
    u->common.screen=MAIL_SCREEN;u->article=u->parent=0;
    if(!mmm_ui_mail_command(s,*recipient?"send":"inbox",recipient))mmm_bad_service_command(s);
}

static int navigate(Session *s,const char *path) {
    WaniUi *u=wani(s);
    MmmUi *m=&u->common;
    if(!*path || !strcmp(path,"/")) { home(s);return 1; }
    if(!strcmp(path,"..")) {
        if(m->screen==NOTES_SCREEN && m->depth) { m->parent=m->ancestors[--m->depth];mmm_ui_boards(s,0); }
        else if(m->screen==BOARD_SCREEN || m->screen==INDEX_SCREEN || m->screen==OPEN_SCREEN) {
            u->view=WANI_HOME_BOARD;u->traversal=0;mmm_ui_boards(s,0);
        } else if(m->screen==MAIL_SCREEN || m->screen==MANAGE_SCREEN)mmm_ui_open_setup(s);
        else home(s);
        return 1;
    }
    if(*path=='/')path++;
    if(wani_equal(path,"board")) { m->parent=m->depth=0;mmm_ui_boards(s,0);return 1; }
    if(!strncmp(path,"board/",6) && !strchr(path+6,'/')) { board_name(s,path+6,0);return 1; }
    if(wani_equal(path,"pool") || !strncmp(path,"pool/",5)) {
        m->screen=FILER_SCREEN;mmm_file_enter(s,path[4]?path+5:"");return 1;
    }
    if(wani_equal(path,"system")) { mmm_ui_open_setup(s);return 1; }
    if(wani_equal(path,"system/mail")) { char inbox[]="";open_mail(s,inbox);return 1; }
    if(wani_equal(path,"game")) { mmm_games(s,0);return 1; }
    return 0;
}

static void board_key(Session *s,const char *word,char *arg) {
    WaniUi *u=wani(s);
    MmmUi *m=&u->common;
    uint32_t id,first,last;
    int body=u->view==WANI_ARTICLE, pages=u->view==WANI_PAGES || (body && u->back_from_read==WANI_PAGES);
    if(!strcmp(word,"S") && !body && u->mode==WANI_VIEW_MNOTES) {
        request(s,WANI_SUBSCRIBE,s->core.board,0,JOB_WANI_CONFIG);return;
    }
    if(!strcmp(word,"U") && u->mode==WANI_VIEW_MNOTES) {
        request(s,WANI_UNSUBSCRIBE,s->core.board,0,JOB_WANI_CONFIG);return;
    }
    if(!strcmp(word,"Q") || !strcmp(word,"q") || !strcmp(word,"Z") || !strcmp(word,"z") ||
       (u->mode==WANI_VIEW_MNOTES && !strcmp(word,"!"))) {
        if(body && u->mode==WANI_VIEW_MNOTES && (!strcmp(word,"Q") || !strcmp(word,"q"))) {
            u->unread=0;list_articles(s,WANI_INDEX,0,0,0);
        } else finish_read(s,!strcmp(word,"Q") || !strcmp(word,"z") || !strcmp(word,"!"),word[0]=='Z'||word[0]=='z');
        return;
    }
    if(!strcmp(word,".") && u->mode==WANI_VIEW_MNOTES && body && !u->parent) {
        u->unread=0;list_articles(s,WANI_PAGES,0,0,0);return;
    }
    if((!strcmp(word,"R") && body && *u->author) || !strcmp(word,"M")) { open_mail(s,*word=='R'?u->author:arg);return; }
    if(mmm_number(word,&id) || ((wani_equal(word,"r") || wani_equal(word,"l")) && mmm_number(arg,&id))) {
        if(pages) {
            if(id<u->page_ordinal || id-u->page_ordinal>=u->row_count) { mmm_bad_service_command(s);return; }
            open_article(s,u->row_ids[id-u->page_ordinal],u->root,WANI_PAGES);
        } else open_article(s,id,u->mode==WANI_VIEW_MNOTES?id:0,WANI_INDEX);
        return;
    }
    if(wani_equal(word,"l") && *arg && !pages && wani_range(arg,u->article,CONTENT_ID_MAX,&first,&last)) {
        u->range_active=1;u->range_end=last;u->unread=0;
        list_articles(s,WANI_INDEX,first,first>last,0);return;
    }
    if(wani_equal(word,"more")) {
        if(!m->next) { mmm_say(s,"End of list.\r\n","一覧の末尾です。\r\n");return; }
        if(*u->query && u->mode==WANI_VIEW_FLAT)search_articles(s,u->query,m->next);
        else {
            if(pages)u->page_ordinal+=u->row_count;
            list_articles(s,pages?WANI_PAGES:WANI_INDEX,m->next,u->descending,0);
        }
        return;
    }
    if((!strcmp(word,".") || !strcmp(word,"X")) && body) { open_article(s,u->article,pages?u->root:0,u->back_from_read);return; }
    if(wani_equal(word,"i") && u->mode==WANI_VIEW_MNOTES) { u->unread=0;list_articles(s,WANI_INDEX,0,0,0);return; }
    if(wani_equal(word,"l") || wani_equal(word,"r") || wani_equal(word,"dir")) {
        u->query[0]=0;u->unread=0;u->range_active=0;
        list_articles(s,pages?WANI_PAGES:WANI_INDEX,0,0,wani_equal(word,"r") && u->mode==WANI_VIEW_FLAT);return;
    }
    if(!strcmp(word,"=") || !strcmp(word,"*") || (body && (!*word || !strcmp(word,"B")))) {
        int down=*word=='*' || *word=='B';uint32_t cursor=0;
        if(body && (!*word || *word=='B')) {
            if(down && u->article<=1) { mmm_bad_service_command(s);return; }
            cursor=down?u->article-1:u->article+1;
        }
        if(body && u->mode==WANI_VIEW_MNOTES && !u->parent) {
            if(!*word) { u->unread=0;list_articles(s,WANI_PAGES,0,0,1);return; }
            if(*word=='B') { list_articles(s,WANI_INDEX,0,0,0);return; }
        }
        u->unread=0;u->query[0]=0;
        list_articles(s,pages?WANI_PAGES:WANI_INDEX,cursor,down,body || u->mode==WANI_VIEW_FLAT);return;
    }
    if(!strcmp(word,"D") || !strcmp(word,"e") || !strcmp(word,"E")) {
        id=body?u->article:0;
        size_t length=strcspn(arg," \t");
        if(length>0 && length<12) {
            char number[12];
            memcpy(number,arg,length);number[length]=0;
            if(mmm_number(number,&first)) { id=first;arg+=length;arg+=strspn(arg," \t"); }
        }
        int remove=*word=='D';
        if(!id || (remove?strcmp(arg,"confirm"):!*arg || !mmm_ui_text_valid(arg,80))) {
            mmm_say(s,"D [ID] confirm / e [ID] NEW TITLE\r\n","D [番号] confirm / e [番号] 新しい題名\r\n");return;
        }
        OperationRequest *q=mmm_request(s,BOARD_WANI);
        if(q) {
            q->action=remove?WANI_WITHDRAW:WANI_RETITLE;q->number=id;q->cursor=1;
            if(!remove)strcpy(q->title,arg);
            mmm_submit(s,q,JOB_NOTE_CHANGE);
        }
        return;
    }
    if(wani_equal(word,"w") || wani_equal(word,"post")) {
        uint32_t parent=0;
        if(body && !wani_equal(word,"post"))parent=u->mode==WANI_VIEW_MNOTES?u->root:u->article;
        u->returning=parent!=0;
        if(u->returning) {
            char msg[134];snprintf(msg,sizeof(msg),"Suggested title: %s (Enter accepts)\r\n",u->reply_title);
            session_impl_queue_text(s,msg);
        }
        mmm_ui_prepare_post(s,parent);return;
    }
    if(wani_equal(word,"f") || wani_equal(word,"find")) { search_articles(s,arg,0);return; }
    if(!strcmp(word,"S") && body) { save_personal(s,u->article,0);return; }
    if(!strcmp(word,"P") && body && *u->author) { mmm_profile(s,u->author);return; }
    if(wani_equal(word,"b")) { mmm_ui_subboards(s,s->core.board);return; }
    mmm_bad_service_command(s);
}

static void topic_request(Session *s,ConferenceAction action,uint32_t id,const char *text) {
    if(text && !mmm_ui_text_valid(text,CONFERENCE_TEXT)) { mmm_bad_service_command(s);return; }
    OperationRequest *q=mmm_request(s,BOARD_CONFERENCE);
    if(!q)return;
    q->conference.action=action;q->conference.cursor=id;
    if(text)strcpy(q->conference.text,text);
    mmm_submit(s,q,JOB_WANI_TOPIC);
}

static void topic_result(Session *s,const ConferenceReply *reply) {
    WaniUi *u=wani(s);
    switch(reply->action) {
    case TOPIC_POST: mmm_say(s,"Topic posted.\r\n","ボードトピックに書き込みました。\r\n");return;
    case TOPIC_DELETE: mmm_say(s,"Topic deleted.\r\n","ボードトピックを削除しました。\r\n");return;
    case TOPIC_SEEN:return;
    default:break;
    }
    u->topic_mode=reply->action;u->topic_next=reply->next;
    if(!reply->count)mmm_say(s,"No topic entries.\r\n","ボードトピックはありません。\r\n");
    for(unsigned i=0;i<reply->count;i++) {
        const ConferenceLine *line=&reply->rows.lines[i];
        char output[656];
        snprintf(output,sizeof(output),"Topic #%u  %s  [%llu]\r\n%s\r\n",line->id,line->name,
                 (unsigned long long)line->time,line->text);
        session_impl_queue_text(s,output);
        if(u->topic_latest)break;
    }
    if(s->core.closing || !s->core.active) { u->topic_latest=0;return; }
    if(u->topic_latest) {
        u->topic_latest=0;u->topic_next=0;
        if(reply->count)topic_request(s,TOPIC_SEEN,reply->rows.lines[0].id,NULL);
    } else if(reply->action==TOPIC_UNREAD && reply->position)
        topic_request(s,TOPIC_SEEN,reply->position,NULL);
}

static void topic_command(Session *s,char *arg) {
    WaniUi *u=wani(s);
    WaniCommand c=wani_command(arg);
    uint32_t id;
    u->topic_latest=0;
    if(!*c.word)topic_request(s,TOPIC_UNREAD,0,NULL);
    else if(wani_equal(c.word,"all") && !*c.argument)topic_request(s,TOPIC_HISTORY,0,NULL);
    else if(wani_equal(c.word,"more") && !*c.argument && u->topic_next)
        topic_request(s,u->topic_mode,u->topic_next,NULL);
    else if((!strcmp(c.word,"-n") || !strcmp(c.word,"-m")) && !*c.argument)topic_request(s,TOPIC_UNREAD,0,NULL);
    else if(!strcmp(c.word,"-w")) {
        if(*c.argument)topic_request(s,TOPIC_POST,0,c.argument);
        else u->topic_step=1;
    } else if(!strcmp(c.word,"-d")) {
        if(mmm_number(c.argument,&id))topic_request(s,TOPIC_DELETE,id,NULL);
        else if(!*c.argument)u->topic_step=2;
        else mmm_bad_service_command(s);
    } else if(!strcmp(c.word,"$") && !*c.argument) {
        u->topic_latest=1;topic_request(s,TOPIC_REVERSE,0,NULL);
    } else if(!strcmp(c.word,"$,1") && !*c.argument)topic_request(s,TOPIC_REVERSE,0,NULL);
    else if(!*c.argument && strlen(c.word)>1 && strchr(c.word,',')==c.word+strlen(c.word)-1 &&
            wani_range(c.word,0,CONFERENCE_LAST_ID,&id,&(uint32_t){0}) && id<=CONFERENCE_LAST_ID)
        topic_request(s,TOPIC_HISTORY,id-1,NULL);
    else mmm_say(s,"topic [-n|-w [TEXT]|-d [ID]|$|$,1|ID,|more]\r\n",
                   "topic [-n 未読|-w [発言]|-d [番号]|$ 最新・既読|$,1 逆順|番号,|more 続き]\r\n");
}

static void layout(Session *s,char *arg) {
    WaniCommand c=wani_command(arg);
    uint32_t id=s->core.board;
    if(*c.argument && !wani_equal(c.word,"default") && !mmm_number(c.word,&id)) { mmm_bad_service_command(s);return; }
    if(*c.argument) {
        if(wani_equal(c.word,"default"))id=0;
        c.word=c.argument;
    }
    int mode=wani_equal(c.word,"flat")?WANI_VIEW_FLAT:wani_equal(c.word,"mnotes")?WANI_VIEW_MNOTES:
        wani_equal(c.word,"inherit")?WANI_VIEW_DEFAULT:-1;
    if(mode>=0)request(s,WANI_VIEW_SET,id,mode,JOB_WANI_CONFIG);
    else mmm_say(s,"layout [BOARD|default] flat|mnotes|inherit\r\n","layout [掲示板番号|default] flat|mnotes|inherit\r\n");
}

static void sequencer(Session *s,char *arg) {
    if(!*arg) { request(s,WANI_SEQUENCE_GET,0,0,JOB_WANI_WINDOW);return; }
    uint64_t stamp=0;
    if(!wani_equal(arg,"reset") && !date_filter(arg,&stamp)) { mmm_bad_service_command(s);return; }
    OperationRequest *q=mmm_request(s,BOARD_WANI);
    if(q) { q->action=WANI_SEQUENCE_SET;q->until=stamp;mmm_submit(s,q,JOB_WANI_CONFIG); }
}

static void chat_input(Session *s,char *line) {
    if(!strcmp(line,"/leave") || !strcmp(line,".")) {
        chat_leave(s->core.id);wani(s)->common.chatting=0;home(s);wani_menu(s,1);
    } else if(!strcmp(line,"?"))wani_menu(s,1);
    else if(!strcmp(line,"/who"))mmm_chat_request(s,'O',0,"");
    else if(!strncmp(line,"/wire ",6)) {
        WaniCommand target=wani_command(line+6);
        uint32_t id;
        if(mmm_number(target.word,&id) && chat_text_valid(target.argument))
            mmm_chat_request(s,'D',id,target.argument);
        else mmm_bad_service_command(s);
    } else if(*line!='/' && chat_text_valid(line))mmm_chat_request(s,'S',0,line);
    else mmm_bad_service_command(s);
}

static int service(Session *s,const WaniCommand *c) {
    WaniUi *u=wani(s);
    MmmUi *m=&u->common;
    const char *word=c->word;
    char *arg=c->argument;
    uint32_t id;
    if(m->screen==MAIL_SCREEN) {
        if(!strcmp(word,"S")) { save_personal(s,u->article,1);return 1; }
        if(!strcmp(word,"V") && u->parent && !*arg) { mmm_simple_operation(s,'m','R',u->parent,JOB_MAIL);return 1; }
        if(!strcmp(word,"P") && *u->author && !*arg) { mmm_profile(s,u->author);return 1; }
        if(!strcmp(word,"*") && !*arg) { mmm_simple_operation(s,'m','T',0,JOB_MAIL);return 1; }
        if((!strcmp(word,"B") || !strcmp(word,"=") || !*word) && !*arg) {
            OperationRequest *q=mmm_request(s,'m');
            if(q) {
                q->action='f';q->descending=!strcmp(word,"B");
                q->cursor=!strcmp(word,"=")?0:u->article;
                mmm_submit(s,q,JOB_MAIL);
            }
            return 1;
        }
        if(!strcmp(word,"R") && u->article && !*arg)mmm_simple_operation(s,'m','Q',u->article,JOB_MAIL);
        else if(wani_equal(word,"r") && !*arg)mmm_ui_mail_command(s,"next",arg);
        else return mmm_ui_mail_command(s,wani_equal(word,"l")?"inbox":wani_equal(word,"m")?"send":
            wani_equal(word,"r")?"read":wani_equal(word,"d")?"delete":!strcmp(word,"K")?"killmail":word,arg);
        return 1;
    }
    if(m->screen==FILER_SCREEN) {
        if(wani_equal(word,"cd") && *arg=='/')return navigate(s,arg);
        if(wani_equal(word,"cd") && !strcmp(arg,"..")) {
            char parent[sizeof(m->file_path)];
            strcpy(parent,m->file_path);
            char *slash=strrchr(parent,'/');
            if(slash)*slash=0;
            else *parent=0;
            mmm_file_enter(s,parent);return 1;
        }
        if(wani_equal(word,"down") || wani_equal(word,"summary")) {
            uint64_t stamp;
            if(!date_filter(arg,&stamp)) { mmm_bad_service_command(s);return 1; }
            strcpy(m->file_filter,arg);m->file_action=VAULT_CMD_NEWS;mmm_ui_file_page(s,0);return 1;
        }
        return mmm_ui_filer_command(s,wani_equal(word,"l")?"list":wani_equal(word,"r")?"info":
            wani_equal(word,"w")?"put":wani_equal(word,"x")?"get":wani_equal(word,"f")?"find":word,arg);
    }
    if(m->screen==TERM_SCREEN)
        return mmm_ui_term_command(s,(char *)word,arg);
    if(m->screen==MANAGE_SCREEN)return mmm_ui_manage_command(s,word,arg);
    if(m->screen==GAMES_SCREEN) {
        if(!*arg && mmm_number(word,&id))mmm_games(s,id);
        else if(!*word || wani_equal(word,"l"))mmm_games(s,0);
        else return 0;
        return 1;
    }
    return 0;
}

void wani_line(Session *s,char *line) {
    WaniUi *u=wani(s);
    MmmUi *m=&u->common;
    if(u->password_step) { password_input(s,line);return; }
    if(u->topic_step) {
        int step=u->topic_step;
        uint32_t id;
        u->topic_step=0;
        if(step==1)topic_request(s,TOPIC_POST,0,line);
        else if(mmm_number(line,&id))topic_request(s,TOPIC_DELETE,id,NULL);
        else mmm_bad_service_command(s);
        return;
    }
    if(m->screen==CHAT_SCREEN) { chat_input(s,line);return; }
    if(m->screen==TALK_SCREEN) {
        if(!strcmp(line,"/") || !strcmp(line,"q"))home(s);
        else mmm_conference_line(s,line);
        return;
    }
    WaniCommand c=wani_command(line);
    const char *word=c.word;
    char *arg=c.argument;
    if(wani_equal(word,"ui")) { mmm_ui_choose_profile(s,arg);return; }
    if(!s->core.user) {
        if(!*word || !strcmp(word,"?") || wani_equal(word,"help"))wani_menu(s,1);
        else if(wani_equal(word,"login") || wani_equal(word,"register"))
            mmm_ui_auth_start(s,arg,wani_equal(word,"login")?'a':'g');
        else if(!*arg)mmm_ui_auth_start(s,word,'a');
        else mmm_bad_service_command(s);
        return;
    }
    if(!strcmp(word,"?") || wani_equal(word,"help") || wani_equal(word,"h")) { wani_help(s,arg);return; }
    if(wani_equal(word,"off") || wani_equal(word,"bye")) { mmm_off(s,1);return; }
    if(wani_equal(word,"logout")) {
        char logout[]="logout";
        Language language=m->language;
        mmm_ui_shared_command(s,logout);
        if(!s->core.user) { OPENSSL_cleanse(u,sizeof(*u));u->common.language=language; }
        return;
    }
    if(wani_equal(word,"topic")) { topic_command(s,arg);return; }
    if(wani_equal(word,"rtopic") || wani_equal(word,"rhello")) {
        char reverse[]="$,1";topic_command(s,reverse);return;
    }
    if(wani_equal(word,"layout")) { layout(s,arg);return; }
    if(wani_equal(word,"seqset")) { sequencer(s,arg);return; }
    if(wani_equal(word,"subscriptions")) { request(s,WANI_SUBSCRIPTIONS,0,0,JOB_WANI_SCAN);return; }
    if(wani_equal(word,"mail") || wani_equal(word,"postmail")) { open_mail(s,arg);return; }
    if(wani_equal(word,"readmail") || wani_equal(word,"check")) {
        m->screen=MAIL_SCREEN;u->article=u->parent=0;
        if(!mmm_ui_mail_command(s,wani_equal(word,"check")?"sent":*arg?"read":"next",arg))
            mmm_bad_service_command(s);
        return;
    }
    if(wani_equal(word,"talk")) { mmm_conference_enter(s,arg);return; }
    if(wani_equal(word,"chat") || wani_equal(word,"voice")) { mmm_chat_request(s,'J',0,"");return; }
    if(wani_equal(word,"call") && chat_text_valid(arg)) { mmm_chat_request(s,'C',0,arg);return; }
    if(wani_equal(word,"who")) { mmm_chat_request(s,'O',0,"");return; }
    if(wani_equal(word,"passwd")) { if(!*arg)u->password_step=1;else mmm_bad_service_command(s);return; }
    if(wani_equal(word,"prof")) { mmm_profile(s,arg);return; }
    if(wani_equal(word,"regprof")) { if(!mmm_ui_term_command(s,"set",arg))mmm_bad_service_command(s);return; }
    if(wani_equal(word,"name") || wani_equal(word,"handle")) {
        char field[90];
        if(mmm_ui_text_valid(arg,80)) {
            snprintf(field,sizeof(field),"HANDLE %s",arg);
            if(mmm_ui_term_command(s,"set",field))return;
        }
        mmm_bad_service_command(s);return;
    }
    if((wani_equal(word,"signature") || wani_equal(word,"regsign")) && mmm_ui_text_valid(arg,80)) {
        char command[90];snprintf(command,sizeof(command),"regsign %s",arg);
        mmm_ui_shared_command(s,command);return;
    }
    if(wani_equal(word,"protocol")) { if(!mmm_ui_transfer_protocol(s,arg))mmm_bad_service_command(s);return; }
    if(wani_equal(word,"manage")) { m->screen=MANAGE_SCREEN;mmm_manage_menu(s);return; }
    if(wani_equal(word,"draft")) { mmm_ui_note_draft_command(s,arg);return; }
    if(wani_equal(word,"type") && !*arg) { session_impl_queue_text(s,m->pfile);session_impl_queue_text(s,"\r\n");return; }
    if(wani_equal(word,"files") && !*arg) {
        char info[56];snprintf(info,sizeof(info),"Personal file: %zu / %u bytes\r\n",strlen(m->pfile),MMM_PFILE_MAX);
        session_impl_queue_text(s,info);return;
    }
    if(wani_equal(word,"edit") && !*arg) {
        mmm_clear_composition(m);strcpy(m->body,m->pfile);m->pfile_editing=1;m->input=INPUT_EDITOR;return;
    }
    if(wani_equal(word,"pfile")) {
        WaniCommand option=wani_command(arg);
        if(!strcmp(option.word,"-b"))mmm_pfile_receive(s,option.argument);
        else mmm_bad_service_command(s);
        return;
    }
    if(wani_equal(word,"cd") && m->screen!=FILER_SCREEN) {
        if(navigate(s,arg))return;
        if(m->screen==NOTES_SCREEN && *arg) { board_name(s,arg,m->parent);return; }
        mmm_bad_service_command(s);return;
    }
    if(wani_equal(word,"open") || wani_equal(word,"read") || wani_equal(word,"fastread") || wani_equal(word,"fast") || wani_equal(word,"cont")) {
        if(!start_scan(s,arg,wani_equal(word,"open")))mmm_bad_service_command(s);
        return;
    }
    if(wani_equal(word,"g") || wani_equal(word,"game")) {
        uint32_t id=0;
        if(!*arg || mmm_number(arg,&id))mmm_games(s,id);
        else mmm_bad_service_command(s);
        return;
    }
    if(wani_equal(word,"s") && m->screen==MAIN_SCREEN) { mmm_ui_open_setup(s);return; }
    if(wani_equal(word,"system")) { mmm_ui_open_setup(s);return; }
    if(wani_equal(word,"pool") || (wani_equal(word,"p") && m->screen==MAIN_SCREEN)) {
        m->screen=FILER_SCREEN;mmm_file_enter(s,arg);return;
    }
    if(wani_equal(word,"board") || (wani_equal(word,"b") && m->screen==MAIN_SCREEN)) {
        if(*arg)board_name(s,arg,0);
        else { m->parent=m->depth=0;mmm_ui_boards(s,0); }
        return;
    }
    if(m->screen==BOARD_SCREEN || m->screen==INDEX_SCREEN || m->screen==OPEN_SCREEN) {
        board_key(s,word,arg);return;
    }
    if(wani_equal(word,"q") || !strcmp(word,"..") || !strcmp(word,"/")) {
        navigate(s,!strcmp(word,"/")?"/":"..");return;
    }
    if(m->screen==NOTES_SCREEN) {
        if(wani_equal(word,"more") && m->next)mmm_ui_boards(s,m->next);
        else if(!*word || wani_equal(word,"l") || wani_equal(word,"dir"))mmm_ui_boards(s,0);
        else if(!*arg) {
            uint32_t row;
            if(mmm_number(word,&row) && row<=m->row_count)board_id(s,m->rows[row-1]);
            else board_name(s,word,m->parent);
        }
        else mmm_bad_service_command(s);
        return;
    }
    if(m->screen==MAIN_SCREEN && (!*word || wani_equal(word,"l") || wani_equal(word,"dir")))wani_menu(s,1);
    else if(!service(s,&c))mmm_bad_service_command(s);
}

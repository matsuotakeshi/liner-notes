#include "session_mmm_internal.h"
#include "ktbbs_input.h"
#include "mmm_input.h"
#include "vault_bridge.h"
#include "chat.h"
#include "date_filter.h"
#include "terminal_pager.h"
#include <openssl/crypto.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static KtbbsUi *kt(Session *s) { return s->ui_binding.state; }
static const KtbbsUi *view(const Session *s) { return s->ui_binding.state; }

static void main_menu(Session *s) {
    KtbbsUi *k=kt(s);
    k->shared.screen=MAIN_SCREEN;
    k->question=KT_READY;
    k->reply_after_board=k->mail_advance=k->find_active=0;
    k->shared.continuous=k->shared.stream_paused=0;
    k->shared.stream_seen=0;
    ktbbs_menu(s,0);
}

int ktbbs_prompt(Session *s) {
    const KtbbsUi *k=view(s);
    const MmmUi *m=&k->shared;
    if(m->input==INPUT_KTBBS_CONFIRM) {
        mmm_say(s,"Use this title? Y / N / 0 > ","この題名で進みますか Y / N / 0 > ");
        return 1;
    }
    if(m->input==INPUT_BODY) {
        mmm_say(s,"Text (// ends) > ","本文 (// で編集メニュー) > ");
        return 1;
    }
    if(m->input==INPUT_EDITOR) {
        session_impl_queue_text(s,"EDIT> ");
        return 1;
    }
    if(m->input!=INPUT_NONE)return 0;
    if(k->page_body) {
        mmm_say(s,"More [Enter/Space, 0 stop] > ","続き [Enter/空白、0 中止] > ");
        return 1;
    }
    static const char *const question[][2]={
        [KT_ARTICLE_BOARD]={"Board (? list / 0 back) > ","掲示板 (? 一覧 / 0 戻る) > "},
        [KT_FILE_BOARD]={"File board (? list / 0 back) > ","保管庫 (? 一覧 / 0 戻る) > "},
        [KT_LIST_START]={"Start article # (Enter: newest) > ","開始記事 # (Enter: 最新) > "},
        [KT_READ_START]={"Start reading # (Enter: newest) > ","読書の開始記事 # (Enter: 最新) > "},
        [KT_READ_ID]={"Article # (0 back) > ","記事番号 # (0 戻る) > "},
        [KT_REPLY_ID]={"Parent article # (0 back) > ","返信元の記事番号 # (0 戻る) > "},
        [KT_SEARCH_KIND]={"SEARCH [1 sender / 2 text / 0 back] > ","SEARCH [1 投稿者 / 2 検索語 / 0 戻る] > "},
        [KT_SEARCH_TEXT]={"Search value > ","検索値 > "},
        [KT_NEWS_DATE]={"NEWS date (Enter: since last scan) > ","NEWS 日付 (Enter: 前回の巡回以降) > "},
        [KT_MAIL_LIST]={"MAILLIST [Enter: all / S sent / R inbox / 0 back] > ","MAILLIST [Enter: 全部 / S 送信 / R 受信 / 0 戻る] > "},
        [KT_MAIL_TO]={"Recipient ID (0 back) > ","宛先ID (0 戻る) > "},
        [KT_FILE_TEXT]={"File search pattern > ","ファイル検索語 > "},
        [KT_FILE_ID]={"Download file # (0 back) > ","取得するファイル番号 # (0 戻る) > "},
        [KT_FILE_UPLOAD]={"Upload description (0 back) > ","登録の説明 (0 戻る) > "},
        [KT_TELEGRAM_TO]={"Connection # (0 back) > ","接続番号 # (0 戻る) > "},
        [KT_TELEGRAM_TEXT]={"Telegram text > ","電報の本文 > "},
        [KT_DELETE_ID]={"Withdraw article # (0 back) > ","取り消す記事番号 # (0 戻る) > "},
        [KT_DELETE_CONFIRM]={"Withdraw? Y / N > ","取り消しますか Y / N > "},
        [KT_MAIL_DELETE_CONFIRM]={"Delete mail? Y / N > ","メールを削除しますか Y / N > "},
        [KT_FILE_NEWS]={"FILE NEWS date (0 back) > ","FILE NEWS 日付 (0 戻る) > "}
    };
    if(k->question!=KT_READY) {
        mmm_say(s,question[k->question][0],question[k->question][1]);
        return 1;
    }
    if(m->screen==NEW_SCREEN && m->stream_paused) {
        mmm_say(s,"Continue news? Y / N > ","新着を続けますか Y / N > ");
        return 1;
    }
    if(!s->core.user) {
        session_impl_queue_text(s,"User ID > ");
        return 1;
    }
    static const char *const prompts[]={
        [MAIN_SCREEN]="MAIN> ", [NOTES_SCREEN]="BOARD> ", [BOARD_SCREEN]="READ> ",
        [INDEX_SCREEN]="LIST> ", [OPEN_SCREEN]="ARTICLE> ", [MAIL_SCREEN]="MAIL> ",
        [FILER_SCREEN]="FILE> ", [CHAT_SCREEN]="CHAT> ", [TERM_SCREEN]="INSTALL> ",
        [GAMES_SCREEN]="GAMES> ", [NEW_SCREEN]="NEWS> ", [MANAGE_SCREEN]="MANAGE> ",
        [TALK_SCREEN]="TALK> ", [KTBBS_ENTER_SCREEN]="ENTER> ", [KTBBS_EXT_SCREEN]="LINER> "
    };
    session_impl_queue_text(s,prompts[m->screen]);
    return 1;
}

void ktbbs_menu(Session *s,int requested) {
    if(!s->core.user) {
        mmm_say(s,"KTBBS mode. Enter your account ID, or REGISTER ID to apply.\r\n",
                  "KTBBS風。会員IDを入力してください。登録は REGISTER ID です。\r\n");
        return;
    }
    switch(mmm_view(s)->screen) {
    case MAIN_SCREEN:
        if(!view(s)->expert || requested)
            mmm_say(s,"KTBBS | R Read  E Enter  N News  M Mail  F Files  C Chat\r\n"
                      "I Install  O Profile  U Users  W Who  K Withdraw  X Expert  G Goodbye\r\n"
                      "A Apply  H Help  L History  Y System  # Status  ! Liner Notes\r\n",
                      "KTBBS | R 読書  E 書込  N 新着  M メール  F 保管庫  C チャット\r\n"
                      "I 端末設定  O プロフィール  U 会員  W 接続一覧  K 取消  X 案内切替  G 終了\r\n"
                      "A 入会  H ヘルプ  L 履歴  Y 局情報  # 状態  ! Liner Notes拡張\r\n");
        break;
    case NOTES_SCREEN: mmm_say(s,"Board number or name; ? list, L more, 0 back.\r\n","ボード番号・名前。? 一覧 L 続き 0 戻る。\r\n");break;
    case BOARD_SCREEN: mmm_say(s,"R Read / I Article # / S Search / L List / # Status / B Sub-boards / 0 Back\r\n",
                                 "R 読書 / I 番号指定 / S 検索 / L 一覧 / # 状態 / B 子ボード / 0 戻る\r\n");break;
    case INDEX_SCREEN: mmm_say(s,"Select displayed row, #ID opens an article, L more, 0 back.\r\n",
                                 "表示行の番号で選択。#ID は記事番号。L 続き 0 戻る。\r\n");break;
    case OPEN_SCREEN: mmm_say(s,"Enter Next / P Previous / R Reply / + - Jump / D Parent / 0 Back\r\n",
                                "Enter 次 / P 前 / R 返信 / + - 移動 / D 親記事 / 0 戻る\r\n");break;
    case MAIL_SCREEN: mmm_say(s,"Enter Read / L List / I # Open / R # Reply / K # Delete / S Search / 0 Back\r\n",
                                "Enter 順次読む / L 一覧 / I # 番号指定 / R # 返信 / K # 削除 / S 検索 / 0 戻る\r\n");break;
    case FILER_SCREEN: mmm_say(s,"L List / S Search / X Y Z M Protocol / U Upload / D Download / H Guide / 0 Back\r\n",
                                 "L 一覧 / S 検索 / X Y Z M 転送方式 / U 登録 / D 取得 / H 案内 / 0 戻る\r\n");break;
    case CHAT_SCREEN: mmm_say(s,"Type a message. W Who / T Telegram / S Call operator / // Leave\r\n",
                                "発言を入力。W 接続一覧 / T 電報 / S SYSOP呼出 / // 退出\r\n");break;
    case TERM_SCREEN: mmm_say(s,"I Show / lang ja|en / size COLS ROWS / pager on|off / save / 0 Back\r\n",
                                "I 設定表示 / lang ja|en / size 列 行 / pager on|off / save 保存 / 0 戻る\r\n");break;
    case GAMES_SCREEN: mmm_say(s,"Game number to start; 0 returns to Liner Notes extensions.\r\n",
                                 "ゲーム番号で起動。0 で Liner Notes拡張に戻ります。\r\n");break;
    case NEW_SCREEN: mmm_say(s,"Enter Read in order / displayed row Select / L More / 0 Exit\r\n",
                               "Enter 順次読む / 表示行番号を選択 / L 続き / 0 終了\r\n");break;
    case MANAGE_SCREEN: mmm_manage_menu(s);break;
    case TALK_SCREEN: mmm_conference_menu(s);break;
    case KTBBS_ENTER_SCREEN: mmm_say(s,"M Mail / W Write article / R Reply / 0 Back\r\n",
                                      "M メール / W 投稿 / R 返信 / 0 戻る\r\n");break;
    case KTBBS_EXT_SCREEN: mmm_say(s,"Liner Notes extensions: games / talk / manage / draft [resume] / UI [name] / 0 Back\r\n",
                                    "Liner Notes拡張: games / talk / manage / draft [resume] / UI [名前] / 0 戻る\r\n");break;
    default: break;
    }
}

void ktbbs_help(Session *s,const char *topic) {
    (void)topic;
    ktbbs_menu(s,1);
    mmm_say(s,"KTBBS commands use the active menu. At ARTICLE, keys act immediately.\r\n"
              "Write: title, Y, body, //, then S send, L review, C append, or 0 cancel.\r\n"
              "Liner Notes additions: #ID jump, D parent, UI, games and line conference.\r\n",
              "KTBBSは画面ごとにキーが変わります。ARTICLE ではキーがすぐに動作します。\r\n"
              "書込: 題名、Y、本文、//、S 送信・L 再読・C 追加・0 中止。\r\n"
              "Liner Notes拡張: #ID 指定、D 親記事、UI、ゲーム、一行会議。\r\n");
}

int ktbbs_editor_line(Session *s,char **text) {
    MmmUi *m=mmm_state(s);
    if(m->input!=INPUT_EDITOR && m->input!=INPUT_KTBBS_CONFIRM)return 0;
    KtbbsInput input=ktbbs_input(*text);
    if(m->input==INPUT_EDITOR && !strcasecmp(*text,"save")) { *text="s";return 0; }
    if(*input.argument)return 0;
    if(input.key=='0') { mmm_clear_composition(m);m->input=INPUT_NONE;return 1; }
    if(m->input==INPUT_KTBBS_CONFIRM) {
        if(input.key=='Y')m->input=INPUT_BODY;
        else if(input.key=='N')m->input=INPUT_TITLE;
        else mmm_say(s,"Choose Y, N or 0.\r\n","Y、N、0 を選択してください。\r\n");
        return 1;
    }
    switch(input.key) {
    case 'S': *text="w";break;
    case 'L': *text="p";break;
    case 'C': *text="u";break;
    case '?':
        mmm_say(s,"S Send / L Review / C Append / 0 Cancel / save: save a Liner Notes draft\r\n",
                  "S 送信 / L 再読 / C 追加 / 0 中止 / save: Liner Notes下書き\r\n");
        return 1;
    default: break;
    }
    return 0;
}

static void choose_board(Session *s,MmmBoardAction action,const char *argument) {
    KtbbsUi *k=kt(s);
    MmmUi *m=&k->shared;
    k->board_action=action;k->question=KT_ARTICLE_BOARD;
    m->screen=NOTES_SCREEN;m->parent=m->depth=0;m->row_count=m->next=0;
    if(*argument)ktbbs_line(s,(char *)argument);
}

int ktbbs_board_ready(Session *s) {
    KtbbsUi *k=kt(s);
    k->question=KT_READY;k->title_mode=k->find_active=0;
    if(!k->reply_after_board)return 0;
    k->reply_after_board=0;k->question=KT_REPLY_ID;
    return 1;
}

void ktbbs_render_articles(Session *s,const ArticlePage *page,int title_only) {
    KtbbsUi *k=kt(s);
    MmmUi *m=&k->shared;
    m->row_count=page->count;m->next=page->next;
    m->screen=title_only && page->count?OPEN_SCREEN:INDEX_SCREEN;
    k->selected_title=title_only && page->count?page->rows[0].id:0;
    if(k->selected_title) { m->article=k->selected_title;m->note_parent=0; }
    for(unsigned i=0;i<page->count;i++) {
        const ArticleSummary *row=&page->rows[i];
        m->rows[i]=row->id;
        if(title_only && i)continue;
        char text[152];
        snprintf(text,sizeof(text),"%u  #%u%s  %s\r\n",i+1,row->id,row->parent?"  Re":"",row->title);
        session_impl_queue_text(s,text);
    }
    if(!page->count)mmm_say(s,"No visible articles.\r\n","該当する記事はありません。\r\n");
    else if(title_only)mmm_say(s,"Enter Body / N Next title / P Previous / R Reply / 0 Back\r\n",
                                 "Enter 本文 / N 次の題名 / P 前 / R 返信 / 0 戻る\r\n");
}

static void browse(Session *s,MmmBrowse kind,uint32_t start,int reverse) {
    MmmUi *m=mmm_state(s);
    m->search_next=m->search_responses=0;
    mmm_ui_browse(s,kind,0,start,reverse,0);
}

static void find_page(Session *s,uint32_t cursor) {
    KtbbsUi *k=kt(s);
    OperationRequest *q=mmm_request(s,'Z');
    if(!q)return;
    q->action=3;q->scope=ARTICLE_HERE;q->cursor=cursor;
    strcpy(q->username,k->find_author?k->find_text:"*");
    strcpy(q->title,k->find_author?"*":k->find_text);
    mmm_submit(s,q,JOB_KTBBS_FIND);
}

void ktbbs_clear_page(Session *s) {
    KtbbsUi *k=kt(s);
    if(k->page_body) {
        OPENSSL_cleanse(k->page_body,strlen(k->page_body));
        free(k->page_body);
    }
    k->page_body=NULL;k->page_offset=0;k->page_article=k->page_board=0;
}

static void next_body_page(Session *s) {
    KtbbsUi *k=kt(s);
    char output[8192];
    int more=terminal_pager_render(k->page_body,&k->page_offset,s->connection.columns,
                                   s->connection.rows,output,sizeof(output));
    if(more>=0)session_impl_queue_text(s,output);
    if(more<=0) {
        uint32_t id=k->page_article,board=k->page_board;
        ktbbs_clear_page(s);
        OperationRequest *q=more==0?mmm_request(s,'s'):NULL;
        if(q) { q->board=board;q->number=id;mmm_submit(s,q,JOB_SEEN); }
    }
}

int ktbbs_page_article(Session *s,const OperationContent *c,const char *body) {
    KtbbsUi *k=kt(s);
    if(k->shared.screen!=OPEN_SCREEN || !k->shared.pager)return 0;
    ktbbs_clear_page(s);
    size_t length=strlen(body);
    k->page_body=malloc(length+1);
    if(!k->page_body) {
        mmm_say(s,"Article pagination unavailable.\r\n","本文のページ表示を利用できません。\r\n");
        return 1;
    }
    memcpy(k->page_body,body,length+1);
    k->page_article=c->id;k->page_board=c->board;
    next_body_page(s);
    return 1;
}

int ktbbs_keys(const Session *s) {
    const KtbbsUi *k=view(s);
    return s->core.user && k->question==KT_READY && k->shared.input==INPUT_NONE &&
           k->shared.screen==OPEN_SCREEN;
}

void ktbbs_key(Session *s,unsigned char ch) {
    KtbbsUi *k=kt(s);
    MmmUi *m=&k->shared;
    if(ch>='a' && ch<='z')ch-=32;
    if(k->page_body) {
        if(ch=='0' || ch==4)ktbbs_clear_page(s);
        else if(ch==' ' || ch=='\r' || ch=='\n')next_body_page(s);
        mmm_prompt(s);
        return;
    }
    uint32_t id=m->article;
    if(ch=='0' || ch==4) {
        k->selected_title=0;m->screen=BOARD_SCREEN;ktbbs_menu(s,1);
    } else if(ch=='?' || ch=='H')ktbbs_help(s,"read");
    else if(ch=='D' && !k->selected_title && m->note_parent)mmm_ui_read_article(s,m->note_parent);
    else if(ch=='R' && id)mmm_ui_prepare_post(s,id);
    else if((ch=='\r' || ch=='\n' || ch==' ') && k->selected_title) {
        k->selected_title=0;
        mmm_ui_read_article(s,id);
    } else if(id && (ch=='\r' || ch=='\n' || ch==' ' || ch=='N' || ch=='P' || ch=='+' || ch=='-')) {
        int reverse=k->reverse;
        unsigned step=1;
        if(ch=='P')reverse=!reverse;
        if(ch=='+' || ch=='-') { reverse=ch=='-';step=10; }
        if(reverse && id<=step)mmm_say(s,"Beginning of board.\r\n","先頭の記事です。\r\n");
        else browse(s,k->title_mode?BROWSE_KTBBS_TITLE:BROWSE_FLAT_BODY,reverse?id-step:id+step,reverse);
    }
    mmm_prompt(s);
}

void ktbbs_mail_page(Session *s,const MailPage *page) {
    KtbbsUi *k=kt(s);
    k->mail_count=page->count;k->mail_at=0;
    for(unsigned i=0;i<page->count;i++)k->mail_rows[i]=page->rows[i].id;
    if(k->mail_advance && k->mail_count)
        mmm_simple_operation(s,'m','R',k->mail_rows[k->mail_at++],JOB_MAIL);
    k->mail_advance=0;
}

void ktbbs_mail_read(Session *s,uint32_t id) { kt(s)->mail_id=id; }

void ktbbs_file_children(Session *s,const VaultQueryChildren *page) {
    KtbbsUi *k=kt(s);
    k->file_board_count=(unsigned)page->count;
    for(unsigned i=0;i<k->file_board_count;i++) {
        const VaultQueryChild *row=&page->rows[i];
        strcpy(k->file_boards[i],row->name);
        char text[154];snprintf(text,sizeof(text),"%u  %s  %s\r\n",i+1,row->name,row->display);
        session_impl_queue_text(s,text);
    }
}

static void news(Session *s,const char *text) {
    KtbbsUi *k=kt(s);
    MmmUi *m=&k->shared;
    int continuous=*text=='@';
    if(continuous)while(*++text==' ' || *text=='\t') {}
    char date[11];uint64_t stamp=0;
    if(*text && (!ktbbs_date(text,date) || !date_filter(date,&stamp))) {
        mmm_say(s,"Use YYYY-MM-DD or YY/MM/DD, optionally preceded by @.\r\n",
                  "YYYY-MM-DD または YY/MM/DD を入力してください。先頭に @ で連続表示します。\r\n");
        return;
    }
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->action='C';q->scope=1;
    if(mmm_submit(s,q,JOB_NEW_BEGIN)) {
        k->question=KT_READY;k->news_at=0;
        m->sequence_override=*text!=0;m->session_since=stamp;
        m->continuous=continuous;m->stream_paused=m->stream_is_board=0;
    }
}

static void telegram(Session *s,const char *argument) {
    KtbbsUi *k=kt(s);
    if(*argument && !ktbbs_id(argument,&k->telegram_target)) { mmm_bad_service_command(s);return; }
    k->question=*argument?KT_TELEGRAM_TEXT:KT_TELEGRAM_TO;
}

static void file_mode(Session *s,int protocol,const char *argument) {
    static const char *const names[]={"xmodem","ymodem","zmodem","mlink"};
    KtbbsUi *k=kt(s);
    if(!mmm_ui_transfer_protocol(s,names[protocol]))return;
    k->upload_protocol=1;
    if(*argument)ktbbs_line(s,(char *)argument);
    else mmm_say(s,"U Upload / D Download / 0 Back\r\n","U 登録 / D 取得 / 0 戻る\r\n");
}

static int question_line(Session *s,char *line) {
    KtbbsUi *k=kt(s);
    MmmUi *m=&k->shared;
    KtbbsQuestion question=k->question;
    if(question==KT_READY)return 0;
    uint32_t id;
    if(!strcmp(line,"0") && question!=KT_TELEGRAM_TEXT && question!=KT_SEARCH_TEXT) {
        k->question=KT_READY;
        if(question==KT_ARTICLE_BOARD || question==KT_FILE_BOARD)main_menu(s);
        return 1;
    }
    switch(question) {
    case KT_ARTICLE_BOARD:
        if(!strcmp(line,"?"))mmm_ui_boards(s,0);
        else if(!strcasecmp(line,"l") && m->next)mmm_ui_boards(s,m->next);
        else if(!*line || mmm_ui_text_valid(line,80))mmm_ui_enter_board(s,line,k->board_action);
        else mmm_bad_service_command(s);
        break;
    case KT_FILE_BOARD:
        if(!strcasecmp(line,"n")) {
            m->screen=FILER_SCREEN;k->question=KT_FILE_NEWS;
        } else {
            m->screen=FILER_SCREEN;
            const char *path=ktbbs_id(line,&id) && id<=k->file_board_count?k->file_boards[id-1]:line;
            mmm_file_enter(s,!strcmp(line,"?") || !*line?"-":path);
            k->question=!strcmp(line,"?")?KT_FILE_BOARD:KT_READY;
        }
        break;
    case KT_LIST_START: case KT_READ_START:
        id=0;
        if(*line && !ktbbs_id(line,&id))break;
        k->question=KT_READY;k->reverse=!id;k->find_active=0;
        k->title_mode=question==KT_READ_START;
        browse(s,k->title_mode?BROWSE_KTBBS_TITLE:BROWSE_FLAT_LIST,id,k->reverse);
        break;
    case KT_READ_ID: case KT_REPLY_ID:
        if(!ktbbs_id(line,&id))break;
        k->question=KT_READY;k->selected_title=k->title_mode=0;k->reverse=0;
        if(question==KT_READ_ID)mmm_ui_read_article(s,id);
        else mmm_ui_prepare_post(s,id);
        break;
    case KT_SEARCH_KIND:
        if(strcmp(line,"1") && strcmp(line,"2"))break;
        k->find_author=*line=='1';k->question=KT_SEARCH_TEXT;
        break;
    case KT_SEARCH_TEXT:
        if(!*line || !mmm_ui_text_valid(line,k->find_author?32:80))break;
        strcpy(k->find_text,line);k->find_active=1;k->question=KT_READY;
        find_page(s,0);
        break;
    case KT_NEWS_DATE: news(s,line);break;
    case KT_MAIL_LIST:
        if(*line && strcasecmp(line,"s") && strcasecmp(line,"r"))break;
        k->question=KT_READY;k->mail_advance=0;
        mmm_mail_list(s,!*line?MAIL_LIST_ALL:(*line=='s' || *line=='S')?MAIL_LIST_SENT:MAIL_LIST_INBOX,0);
        break;
    case KT_MAIL_TO:
        if(!*line || !mmm_ui_mail_command(s,"send",line))break;
        k->question=KT_READY;
        break;
    case KT_FILE_TEXT:
        if(!*line || !mmm_ui_filer_command(s,"find",line))break;
        k->question=KT_READY;
        break;
    case KT_FILE_NEWS: {
        char date[11];uint64_t stamp;
        if(!*line) {
            mmm_say(s,"Enter a date for file news.\r\n","ファイル新着の日付を入力してください。\r\n");
            break;
        }
        if(!ktbbs_date(line,date) || !date_filter(date,&stamp))break;
        strcpy(m->file_filter,date);
        m->file_action=VAULT_CMD_NEWS;k->question=KT_READY;
        mmm_ui_file_page(s,0);
        break;
    }
    case KT_FILE_ID:
        if(!ktbbs_id(line,&id))break;
        k->question=KT_READY;
        mmm_ui_transfer(s,0,id,"",0);
        break;
    case KT_FILE_UPLOAD:
        if(!mmm_ui_filer_command(s,"put",line))break;
        k->question=KT_READY;
        break;
    case KT_TELEGRAM_TO:
        if(!*line)k->question=KT_READY;
        else if(ktbbs_id(line,&k->telegram_target))k->question=KT_TELEGRAM_TEXT;
        break;
    case KT_TELEGRAM_TEXT:
        k->question=KT_READY;
        if(!*line)break;
        if(chat_text_valid(line))mmm_chat_request(s,'D',k->telegram_target,line);
        else mmm_bad_service_command(s);
        break;
    case KT_DELETE_ID:
        if(ktbbs_id(line,&k->delete_id))k->question=KT_DELETE_CONFIRM;
        break;
    case KT_MAIL_DELETE_CONFIRM:
        k->question=KT_READY;
        if(!strcasecmp(line,"y"))mmm_simple_operation(s,'m','D',k->delete_id,JOB_MAIL);
        break;
    case KT_DELETE_CONFIRM:
        k->question=KT_READY;
        if(!strcasecmp(line,"y")) {
            OperationRequest *q=mmm_request(s,'Z');
            if(q) {
                q->action=1;q->cursor=1;q->number=k->delete_id;
                mmm_submit(s,q,JOB_NOTE_CHANGE);
            }
        }
        break;
    default: return 0;
    }
    return 1;
}

static void mail_next(Session *s) {
    KtbbsUi *k=kt(s);
    MmmUi *m=&k->shared;
    if(k->mail_at<k->mail_count)mmm_simple_operation(s,'m','R',k->mail_rows[k->mail_at++],JOB_MAIL);
    else if(!k->mail_count || m->service_next) {
        k->mail_advance=1;
        mmm_mail_list(s,k->mail_count?m->mail_mode:MAIL_LIST_ALL,k->mail_count?m->service_next:0);
    } else mmm_say(s,"End of mail list.\r\n","メール一覧の末尾です。\r\n");
}

static int extension(Session *s,char *line,int area) {
    char *end=line+strcspn(line," \t");
    int split=*end!=0;
    char *argument=end;
    if(split) {
        *end=0;
        argument=end+1+strspn(end+1," \t");
    }
    char name[32];
    size_t n=strlen(line);
    if(n>=sizeof(name)) { if(split)*end=' ';return 0; }
    for(size_t i=0;i<=n;i++) {
        char c=line[i];name[i]=(c>='A' && c<='Z')?(char)(c+('a'-'A')):c;
    }
    MmmUi *m=mmm_state(s);
    int handled=1;
    if(!strcmp(name,"ui") && (area==MAIN_SCREEN || area==KTBBS_EXT_SCREEN || !s->core.user)) {
        if(area==KTBBS_EXT_SCREEN)m->screen=MAIN_SCREEN;
        mmm_ui_choose_profile(s,argument);
    } else if((!strcmp(name,"login") || !strcmp(name,"register")) && !s->core.user) {
        if(!*argument && name[0]=='l')m->input=INPUT_LOGIN;
        else mmm_ui_auth_start(s,argument,name[0]=='l'?'a':'g');
    } else if(!s->core.user)handled=0;
    else if(area==KTBBS_EXT_SCREEN) {
        if(!strcmp(name,"games") && !*argument)mmm_games(s,0);
        else if(!strcmp(name,"talk"))mmm_conference_enter(s,argument);
        else if(!strcmp(name,"draft"))mmm_ui_note_draft_command(s,argument);
        else if(!strcmp(name,"manage") && (s->core.admin || s->core.sigop)) {
            m->screen=MANAGE_SCREEN;mmm_manage_menu(s);
        } else handled=0;
    } else if(area==TERM_SCREEN && !strcmp(name,"pager")) {
        if(!strcasecmp(argument,"on"))m->pager=1;
        else if(!strcasecmp(argument,"off"))m->pager=0;
        else if(*argument)handled=0;
        if(handled)mmm_settings_summary(s);
    } else if(area==TERM_SCREEN)handled=mmm_ui_term_command(s,name,argument);
    else if(area==MANAGE_SCREEN)handled=mmm_ui_manage_command(s,name,argument);
    else if(area==MAIL_SCREEN) {
        if(!strcmp(name,"protocol"))handled=mmm_ui_transfer_protocol(s,argument);
        else handled=mmm_ui_mail_command(s,name,argument);
    } else if(area==FILER_SCREEN) {
        if(!strcmp(name,"protocol"))handled=mmm_ui_transfer_protocol(s,argument);
        else handled=mmm_ui_filer_command(s,name,argument);
    } else handled=0;
    if(split)*end=' ';
    return handled;
}

static void board_line(Session *s,KtbbsInput input) {
    KtbbsUi *k=kt(s);
    MmmUi *m=&k->shared;
    uint32_t id;
    if(input.key=='0') {
        if(m->screen==INDEX_SCREEN)m->screen=BOARD_SCREEN;
        else if(m->depth) {
            m->parent=m->ancestors[--m->depth];
            k->question=KT_ARTICLE_BOARD;mmm_ui_boards(s,0);
        } else main_menu(s);
    } else if(input.key=='L' && m->screen==INDEX_SCREEN) {
        if(m->next) {
            if(k->find_active)find_page(s,m->next);
            else browse(s,BROWSE_FLAT_LIST,m->next,k->reverse);
        } else mmm_say(s,"End of article list.\r\n","記事一覧の末尾です。\r\n");
    } else if(input.key=='L' || input.key=='R') {
        k->question=input.key=='L'?KT_LIST_START:KT_READ_START;
        if(*input.argument)question_line(s,(char *)input.argument);
    } else if(input.key=='I') {
        k->question=KT_READ_ID;
        if(*input.argument)question_line(s,(char *)input.argument);
    } else if(input.key=='B') {
        k->question=KT_ARTICLE_BOARD;k->board_action=OPEN_BOARD_MENU;
        mmm_ui_subboards(s,s->core.board);
    } else if(input.key=='S')k->question=KT_SEARCH_KIND;
    else if(input.key=='#') {
        if(ktbbs_id(input.argument,&id))mmm_ui_read_article(s,id);
        else if(!*input.argument)mmm_simple_operation(s,'L',0,0,JOB_KTBBS_STATUS);
        else mmm_bad_service_command(s);
    } else mmm_bad_service_command(s);
}

void ktbbs_line(Session *s,char *line) {
    KtbbsUi *k=kt(s);
    MmmUi *m=&k->shared;
    if(m->screen!=CHAT_SCREEN && k->question!=KT_TELEGRAM_TEXT)line+=strspn(line," \t");
    KtbbsInput input=ktbbs_input(line);
    if(m->screen==CHAT_SCREEN && (*line==' ' || *line=='\t'))input.key=0;
    char *arg=(char *)input.argument;
    uint32_t id;
    if(!s->core.user) {
        if(extension(s,line,MAIN_SCREEN))return;
        if(input.key=='?' || input.key=='H' || !*line)ktbbs_menu(s,1);
        else if(input.key=='G' && !*arg)mmm_off(s,1);
        else mmm_ui_auth_start(s,line,'a');
        return;
    }
    if(question_line(s,line))return;
    if(m->screen!=CHAT_SCREEN && !strcmp(line,"?")) { ktbbs_help(s,"");return; }
    if(m->screen!=CHAT_SCREEN && input.key=='T') { telegram(s,arg);return; }
    if(m->screen==MAIN_SCREEN) {
        if(extension(s,line,MAIN_SCREEN))return;
        if(!strcmp(line,"!")) { m->screen=KTBBS_EXT_SCREEN;ktbbs_menu(s,1);return; }
        switch(input.key) {
        case '\r': case 'H': ktbbs_help(s,"");break;
        case 'R': choose_board(s,OPEN_BOARD_MENU,arg);break;
        case 'E': m->screen=KTBBS_ENTER_SCREEN;ktbbs_menu(s,1);break;
        case 'N': k->question=KT_NEWS_DATE;if(*arg)news(s,arg);break;
        case 'M':
            m->screen=MAIL_SCREEN;m->service_next=0;k->mail_at=k->mail_count=k->mail_id=0;
            ktbbs_menu(s,1);break;
        case 'F':
            k->question=KT_FILE_BOARD;k->file_board_count=0;
            if(*arg)question_line(s,arg);
            break;
        case 'C': mmm_chat_request(s,'J',0,"");break;
        case 'I': mmm_ui_open_setup(s);break;
        case 'O': mmm_profile(s,arg);break;
        case 'U': if(*arg) { if(strlen(arg)>80) { mmm_bad_service_command(s);break; }strcpy(m->profile_filter,arg); } else m->profile_filter[0]=0;
                  m->profile_scope=m->profile_group=0;mmm_ui_profile_search(s,0);break;
        case 'W': mmm_chat_request(s,'O',0,"");break;
        case 'L': mmm_simple_operation(s,'!','U',0,JOB_MANAGE);break;
        case 'Y': session_impl_queue_text(s,LINER_VERSION_TEXT "\r\n");break;
        case 'K': k->question=KT_DELETE_ID;if(*arg)question_line(s,arg);break;
        case 'G': mmm_off(s,0);break;
        case 'X': k->expert=!k->expert;ktbbs_menu(s,0);break;
        case 'A': mmm_say(s,"Apply from the sign-in screen with REGISTER ID.\r\n","入会は認証前に REGISTER ID を使います。\r\n");break;
        case '#': mmm_settings_summary(s);break;
        default: mmm_bad_service_command(s);break;
        }
        return;
    }
    switch(m->screen) {
    case KTBBS_ENTER_SCREEN:
        if(input.key=='0')main_menu(s);
        else if(input.key=='W' || input.key=='R') {
            k->reply_after_board=input.key=='R';
            choose_board(s,k->reply_after_board?OPEN_BOARD_MENU:OPEN_POST,arg);
        } else if(input.key=='M') {
            m->screen=MAIL_SCREEN;k->question=KT_MAIL_TO;
            if(*arg)question_line(s,arg);
        } else mmm_bad_service_command(s);
        return;
    case NOTES_SCREEN:
        if(input.key=='L' && m->next)mmm_ui_boards(s,m->next);
        else if(input.key=='0')main_menu(s);
        else mmm_ui_enter_board(s,line,k->board_action);
        return;
    case BOARD_SCREEN: case INDEX_SCREEN:
        if(m->screen==INDEX_SCREEN && *line!='#' && ktbbs_id(line,&id) && id<=m->row_count) {
            k->title_mode=k->selected_title=0;
            mmm_ui_read_article(s,m->rows[id-1]);
        } else if(*line=='#' && ktbbs_id(line,&id)) {
            k->title_mode=k->selected_title=0;
            mmm_ui_read_article(s,id);
        } else board_line(s,input);
        return;
    case NEW_SCREEN:
        if(input.key=='0')main_menu(s);
        else if(ktbbs_id(line,&id) && id<=m->row_count)
            mmm_simple_operation(s,'Z',5,m->rows[id-1],JOB_NEW_READ);
        else if(input.key=='\r' && k->news_at<m->row_count)
            mmm_simple_operation(s,'Z',5,m->rows[k->news_at++],JOB_NEW_READ);
        else if((input.key=='L' || input.key=='\r') && m->service_next)mmm_new_page(s,m->service_next);
        else mmm_bad_service_command(s);
        return;
    case MAIL_SCREEN:
        switch(input.key) {
        case '\r': mail_next(s);return;
        case '0': main_menu(s);return;
        case 'L': k->question=KT_MAIL_LIST;if(*arg)question_line(s,arg);return;
        case 'I': case 'R': case 'K':
            id=k->mail_id;
            if((*arg && !ktbbs_id(arg,&id)) || !id)break;
            if(input.key=='K') {
                k->delete_id=id;k->question=KT_MAIL_DELETE_CONFIRM;return;
            }
            mmm_simple_operation(s,'m',input.key=='R'?'Q':'R',id,JOB_MAIL);return;
        case 'S': if(mmm_ui_mail_command(s,"find",arg))return;break;
        case '#':
            mmm_say(s,"L shows sent and received mail.\r\n","L で送受信メールを確認できます。\r\n");return;
        default: break;
        }
        break;
    case FILER_SCREEN:
        switch(input.key) {
        case '0': if(k->upload_protocol)k->upload_protocol=0;else main_menu(s);return;
        case 'L': mmm_ui_filer_command(s,"list",arg);return;
        case 'S': k->question=KT_FILE_TEXT;if(*arg)question_line(s,arg);return;
        case 'X': case 'Y': case 'Z': case 'M': file_mode(s,input.key=='X'?0:input.key=='Y'?1:input.key=='Z'?2:3,arg);return;
        case 'D': k->question=KT_FILE_ID;if(*arg)question_line(s,arg);return;
        case 'U': k->question=KT_FILE_UPLOAD;if(*arg)question_line(s,arg);return;
        case 'H': {
            OperationRequest *q=mmm_request(s,'Y');
            if(q) { q->action=VAULT_CMD_BOARDINFO;strcpy(q->title,*m->file_path?m->file_path:"-");mmm_submit(s,q,JOB_FILES); }
            return;
        }
        case 'N': k->question=KT_FILE_NEWS;if(*arg)question_line(s,arg);return;
        default: break;
        }
        break;
    case CHAT_SCREEN:
        if(!strcmp(line,"//")) { m->chatting=0;chat_leave(s->core.id);main_menu(s); }
        else if(input.key=='W' && !*arg)mmm_chat_request(s,'O',0,"");
        else if(input.key=='T')telegram(s,arg);
        else if(input.key=='S' && !*arg)mmm_chat_request(s,'C',0,"");
        else if(input.key=='O' && *arg)mmm_profile(s,arg);
        else if(input.key=='?')ktbbs_menu(s,1);
        else if(chat_text_valid(line))mmm_chat_request(s,'S',0,line);
        else mmm_bad_service_command(s);
        return;
    case GAMES_SCREEN:
        if(input.key=='0') { m->screen=KTBBS_EXT_SCREEN;ktbbs_menu(s,1); }
        else if(ktbbs_id(line,&id))mmm_games(s,id);
        else if(!*line)mmm_games(s,0);
        else mmm_bad_service_command(s);
        return;
    case TALK_SCREEN:
        if(input.key=='0') { m->screen=KTBBS_EXT_SCREEN;ktbbs_menu(s,1); }
        else mmm_conference_line(s,line);
        return;
    case KTBBS_EXT_SCREEN: case MANAGE_SCREEN: case TERM_SCREEN:
        if(input.key=='0') { main_menu(s);return; }
        if(m->screen==TERM_SCREEN && input.key=='I') { mmm_settings_summary(s);return; }
        break;
    default: break;
    }
    if(!extension(s,line,m->screen))mmm_bad_service_command(s);
}

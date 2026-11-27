#define _POSIX_C_SOURCE 200809L
#include "session_big_model.h"
#include "chat.h"
#include "big_model_input.h"
#include "mmm_input.h"
#include "session_policy_core.h"
#include "terminal_pager.h"
#include <ctype.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static BigModelUi *big(Session *s) { return s->ui_binding.state; }
static const BigModelUi *big_view(const Session *s) { return s->ui_binding.state; }

int big_model_ready(const Session *s) { return big_view(s)->input==BIG_MODEL_READY; }
int big_model_secret(const Session *s) {
    BigModelInput input=big_view(s)->input;
    return input>=BIG_MODEL_OLD_PASSWORD && input<=BIG_MODEL_CONFIRM_PASSWORD;
}

int big_model_prompt(Session *s) {
    const BigModelUi *ui=big_view(s);
    if(ui->shared.input!=INPUT_NONE)return 0;
    if(!s->core.user)mmm_say(s,"ID > ","会員ID > ");
    else if(ui->input==BIG_MODEL_DESTINATION)mmm_say(s,"Task number (0 cancels) > ","送り先タスク番号 (0 中止) > ");
    else if(ui->input==BIG_MODEL_TELEGRAM)mmm_say(s,"Telegram (21 characters) > ","電文 (21文字) > ");
    else if(ui->input==BIG_MODEL_OLD_PASSWORD)mmm_say(s,"Current password > ","現在のパスワード > ");
    else if(ui->input==BIG_MODEL_NEW_PASSWORD)mmm_say(s,"New password > ","新しいパスワード > ");
    else if(ui->input==BIG_MODEL_CONFIRM_PASSWORD)mmm_say(s,"Confirm password > ","新しいパスワードの確認 > ");
    else if(ui->input==BIG_MODEL_TERM_DATE)mmm_say(s,"Date (YYYY-MM-DD or RESET) > ","日時 (YYYY-MM-DD または RESET) > ");
    else if(ui->page_body)mmm_say(s,"Space next page / Q stop > ","空白 次ページ / Q 中止 > ");
    else {
        static const char *const prompts[]={
            [MAIN_SCREEN]="BIG> ",[NOTES_SCREEN]="SIG> ",[BOARD_SCREEN]="BOARD> ",
            [INDEX_SCREEN]="LIST> ",[OPEN_SCREEN]="ARTICLE> ",[MAIL_SCREEN]="MAIL> ",
            [FILER_SCREEN]="FILES> ",[CHAT_SCREEN]="CHAT> ",[TERM_SCREEN]="SETUP> ",
            [GAMES_SCREEN]="GAMES> ",[NEW_SCREEN]="NEWS> ",[MANAGE_SCREEN]="SYSOP> ",
            [BIG_MODEL_EXT_SCREEN]="LINER> "
        };
        if(ui->shared.screen==TALK_SCREEN)return 0;
        session_impl_queue_text(s,prompts[ui->shared.screen]);
    }
    return 1;
}

void big_model_menu(Session *s,int requested) {
    const BigModelUi *ui=big_view(s);
    if(!s->core.user) {
        mmm_say(s,"BIG-Model | Enter your ID. REGISTER ID to apply.\r\n",
                  "BIG-Model | 会員IDを入力してください。入会は REGISTER ID。\r\n");
        return;
    }
    if(ui->quiet && !requested)return;
    switch(ui->shared.screen) {
    case MAIN_SCREEN:
        session_ui_menu_hint(s);
        mmm_extensions_hint(s);
        mmm_say(s,"BIG-Model | B SIGs  C Chat  E Exit  H Help  M Mail\r\n"
                  "N New articles (N resume)  O New headlines  P Files  T Telegram  U Settings  W Who  X All SIGs\r\n"
                  "+ Liner Notes extensions / TOPIC short notices\r\n",
                  "BIG-Model | B 掲示板・SIG  C チャット  E 終了  H 案内  M メール\r\n"
                  "N 新着記事 (N resume 再開)  O 新着見出し  P 保管庫  T 電報  U 端末設定  W 接続一覧  X 全SIG探索\r\n"
                  "+ Liner Notes拡張 / TOPIC 短文伝言板\r\n");
        break;
    case NOTES_SCREEN:
        mmm_say(s,"Number: select  L: more  0: up  /: main\r\n","番号 選択  L 続き  0 上位  / 主メニュー\r\n");
        break;
    case BOARD_SCREEN: case INDEX_SCREEN: case OPEN_SCREEN:
        mmm_say(s,"R [NUM]: read/list  W: write  A: reply  L: list/more  S TERM: find\r\n"
                  "N: new/next  P: previous  G: parent  B: children  !: telegram  0: back  /: main\r\n",
                  "R [番号] 読む・一覧  W 書く  A 返信  L 一覧・続き  S 語句 検索\r\n"
                  "N 新着・次  P 前  G 親  B 配下  ! 電報  0 戻る  / 主メニュー\r\n");
        break;
    case NEW_SCREEN:
        mmm_say(s,"NUM or R NUM: read  Enter: next row  L: more  Q: suspend  0: back\r\n",
                  "番号または R 番号 本文  Enter 次の記事  L 続き  Q 中断  0 戻る\r\n");
        break;
    case MAIL_SCREEN:
        mmm_say(s,"R [NUM]: inbox/read  W USER: send  N: unread  A NUM: reply  K NUM confirm: delete\r\n"
                  "L: more  FIND/FILES/UPLOAD/GET/GETALL/RESUME: Liner Notes extensions  0: back\r\n",
                  "R [番号] 受信一覧・本文  W 会員ID 送信  N 未読  A 番号 返信  K 番号 confirm 削除\r\n"
                  "L 続き  FIND/FILES/UPLOAD/GET/GETALL/RESUME は拡張  0 戻る\r\n");
        break;
    case FILER_SCREEN:
        mmm_say(s,"L: list  S PATTERN: find  I NUM: info  D NUM: get  U [DESC]: upload  R: resume\r\n"
                  "CD/DIRS/PROTOCOL/ADD/BATCH/PUTBATCH/UNLOCK: Liner Notes extensions  0: back\r\n",
                  "L 一覧  S パターン 検索  I 番号 詳細  D 番号 取得  U [説明] 登録  R 再開\r\n"
                  "CD/DIRS/PROTOCOL/ADD/BATCH/PUTBATCH/UNLOCK は拡張  0 戻る\r\n");
        break;
    case CHAT_SCREEN:
        mmm_say(s,"Enter text to chat. ! telegram  /who online  /leave back\r\n",
                  "本文を入力して発言。! 電報  /who 接続一覧  /leave 戻る\r\n");
        break;
    case TERM_SCREEN:
        mmm_say(s,"1 Menu mode  2 Password  3 Profile  4 Members  5 Temporary exploration date\r\n"
                  "6 #BOARD ...|RESET  7 Read list  8 Default  9 ON|OFF Telegram  10 PROTOCOL\r\n"
                  "LANG/SIZE/SET/SAVE/LOAD  0 Back\r\n",
                  "1 メニュー表示量  2 パスワード  3 プロフィール  4 会員一覧  5 探索日時仮設定\r\n"
                  "6 #番号列|RESET  7 探索リスト  8 既定  9 ON|OFF 電報受信  10 転送方式\r\n"
                  "LANG/SIZE/SET/SAVE/LOAD  0 戻る\r\n");
        break;
    case GAMES_SCREEN:
        mmm_say(s,"Number: play  Enter: list  0: back\r\n","番号 起動  Enter 一覧  0 戻る\r\n");
        break;
    case MANAGE_SCREEN: mmm_manage_menu(s);break;
    case TALK_SCREEN: mmm_conference_menu(s);break;
    case BIG_MODEL_EXT_SCREEN:
        mmm_say(s,"TALK  GAMES  MANAGE  DRAFT  0 Back\r\n","TALK 一行会議  GAMES ゲーム  MANAGE 管理  DRAFT 下書き  0 戻る\r\n");
        break;
    default: break;
    }
}

void big_model_help(Session *s,const char *topic) {
    (void)topic;
    big_model_menu(s,1);
    if(big_view(s)->shared.screen==MAIN_SCREEN)
        mmm_say(s,"A/D/L/Q/Z legacy services are unavailable here. Use + for Liner Notes extensions.\r\n",
                  "旧A/D/L/Q/Zの機能は利用できません。Liner Notes拡張は +。\r\n");
}

static void main_menu(Session *s) {
    BigModelUi *ui=big(s);
    ui->input=BIG_MODEL_READY;
    ui->shared.screen=MAIN_SCREEN;
    ui->shared.parent=ui->shared.depth=ui->shared.row_count=ui->shared.next=0;
    big_model_menu(s,0);
}

void big_model_header(Session *s,uint32_t id,uint64_t timestamp,const char *author,const char *title) {
    char line[180];
    if(big_model_format_header(line,sizeof(line),id,timestamp,author,title))
        session_impl_queue_text(s,line);
}

static void list_page(Session *s,uint32_t cursor);

static void article_columns(Session *s) {
    session_impl_queue_text(s,"NUM    R.DATE   R.TIME    SENDER  CONTENTS\r\n");
}

static void read_article(Session *s,uint32_t id) {
    BigModelUi *ui=big(s);
    if(ui->shared.screen!=OPEN_SCREEN)ui->article_return=ui->shared.screen;
    OperationRequest *q=mmm_request(s,'Z');
    if(!q)return;
    q->action=5;q->number=id;q->scope=ui->article_return==NEW_SCREEN?0:1;
    mmm_submit(s,q,JOB_READ);
}

void big_model_articles(Session *s,const ArticlePage *page,int read_first) {
    BigModelUi *ui=big(s);
    MmmUi *m=&ui->shared;
    if(read_first) {
        if(page->count)read_article(s,page->rows[0].id);
        else mmm_say(s,"No further visible articles.\r\n","これ以上表示できる記事はありません。\r\n");
        return;
    }
    if(ui->list==BIG_MODEL_NEWS && !page->count) {
        if(page->next) { list_page(s,page->next);return; }
        if(m->scan_custom && m->scan_at+1<m->scan_count) { m->scan_at++;list_page(s,0);return; }
    }
    m->screen=ui->list==BIG_MODEL_NEWS?NEW_SCREEN:INDEX_SCREEN;
    m->row_count=page->count;m->next=page->next;m->stream_at=0;
    article_columns(s);
    for(unsigned i=0;i<page->count;i++) {
        const ArticleSummary *row=&page->rows[i];
        m->rows[i]=row->id;
        big_model_header(s,row->id,row->created,row->author,row->title);
    }
    if(!page->count)mmm_say(s,"No visible articles on this page.\r\n","このページに表示できる記事はありません。\r\n");
    if(page->next || (ui->list==BIG_MODEL_NEWS && m->scan_custom && m->scan_at+1<m->scan_count))mmm_say(s,"L shows the next page.\r\n","L で次ページを表示します。\r\n");
}

static void list_page(Session *s,uint32_t cursor) {
    BigModelUi *ui=big(s);
    if(ui->list==BIG_MODEL_BOARD) {
        mmm_ui_browse(s,BROWSE_FLAT_LIST,0,cursor,0,0);
        return;
    }
    OperationRequest *q=mmm_request(s,ui->list==BIG_MODEL_FIND?'Z':BOARD_NOTE_VIEW);
    if(!q)return;
    q->cursor=cursor;
    if(ui->list==BIG_MODEL_FIND) {
        q->action=3;q->scope=ARTICLE_HERE;
        strcpy(q->username,"*");strcpy(q->title,ui->query);
    } else {
        const MmmUi *m=&ui->shared;
        q->action='n';q->scope=ui->scope;q->board=ui->news_board;
        q->since=ui->since;q->until=ui->until;q->notes_unread=ui->unread;
        if(m->scan_custom && m->scan_at<m->scan_count) {
            q->board=m->scan_boards[m->scan_at];q->scope=ARTICLE_HERE;q->number=1;
        }
    }
    if(mmm_submit(s,q,JOB_BIG_MODEL_PAGE) && ui->list==BIG_MODEL_NEWS)ui->news_cursor=cursor;
}

void big_model_scanlist(Session *s,const MmmScanList *list) {
    if(!list->custom) {
        mmm_say(s,"Default: all accessible boards in article order.\r\n","既定: 閲覧可能な全ボードの記事番号順。\r\n");
        return;
    }
    for(unsigned i=0;i<list->count;i++) {
        char line[116];
        snprintf(line,sizeof(line),"#%u  %s\r\n",list->rows[i].board,list->rows[i].title);
        session_impl_queue_text(s,line);
    }
    if(!list->count)mmm_say(s,"No accessible boards in your list.\r\n","リストに閲覧可能なボードがありません。\r\n");
}

void big_model_window(Session *s,uint64_t since,uint64_t until,const MmmScanList *list) {
    BigModelUi *ui=big(s);
    MmmUi *m=&ui->shared;
    ui->since=ui->override_date?ui->override_since:s->core.previous_login?s->core.previous_login:since;
    ui->until=until;
    m->scan_at=0;m->scan_count=list?list->count:0;m->scan_custom=list && list->custom;
    for(unsigned i=0;i<m->scan_count;i++)m->scan_boards[i]=list->rows[i].board;
    if(m->scan_custom && !m->scan_count) {
        ArticlePage empty={0};
        big_model_articles(s,&empty,0);
    } else list_page(s,0);
}

static void news(Session *s,ArticleScope scope,int unread,int use_list) {
    BigModelUi *ui=big(s);
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(!q)return;
    q->action='c';q->scope=use_list;
    if(mmm_submit(s,q,JOB_BIG_MODEL_WINDOW)) {
        ui->news_return=ui->shared.screen==MAIN_SCREEN?MAIN_SCREEN:BOARD_SCREEN;
        ui->news_suspended=0;ui->news_board=s->core.board;ui->scope=scope;ui->unread=unread;ui->list=BIG_MODEL_NEWS;
    }
}

void big_model_clear_page(Session *s) {
    BigModelUi *ui=big(s);
    if(ui->page_body) {
        OPENSSL_cleanse(ui->page_body,strlen(ui->page_body));
        free(ui->page_body);
    }
    ui->page_body=NULL;ui->page_offset=0;ui->page_id=ui->page_board=0;
}

static void body_page(Session *s) {
    BigModelUi *ui=big(s);
    char output[8192];
    int more=terminal_pager_render(ui->page_body,&ui->page_offset,s->connection.columns,
                                  s->connection.rows,output,sizeof(output));
    if(more>=0)session_impl_queue_text(s,output);
    if(more>0)return;
    uint32_t id=ui->page_id,board=ui->page_board;
    big_model_clear_page(s);
    OperationRequest *q=more==0?mmm_request(s,'s'):NULL;
    if(q) { q->board=board;q->number=id;mmm_submit(s,q,JOB_SEEN); }
}

int big_model_article_body(Session *s,const OperationContent *article,const char *body) {
    BigModelUi *ui=big(s);
    if(!ui->shared.pager)return 0;
    big_model_clear_page(s);
    size_t length=strlen(body);
    ui->page_body=malloc(length+1);
    if(!ui->page_body) {
        mmm_say(s,"Article page unavailable.\r\n","本文のページ表示を利用できません。\r\n");
        return 1;
    }
    memcpy(ui->page_body,body,length+1);
    ui->page_id=article->id;ui->page_board=article->board;
    body_page(s);
    return 1;
}

int big_model_keys(const Session *s) { return big_view(s)->page_body!=NULL; }

void big_model_key(Session *s,unsigned char ch) {
    if(ch==' ' || ch=='\r')body_page(s);
    else if(ch=='Q' || ch=='q' || ch==3)big_model_clear_page(s);
    mmm_prompt(s);
}

void big_model_destinations(Session *s) {
    BigModelUi *ui=big(s);
    ChatPresence peers;
    ui->recipient_count=0;
    if(chat_presence(s->core.id,s->core.user,0,&peers)) {
        for(unsigned i=0;i<peers.count;i++) {
            const ChatPresenceRow *peer=&peers.rows[i];
            if(peer->connection==s->core.id || !*peer->login)continue;
            char row[164];
            if(peer->accepts_telegram) {
                ui->recipients[ui->recipient_count++]=*peer;
                snprintf(row,sizeof(row),"[%u] %s  %s\r\n",ui->recipient_count,peer->login,peer->name);
            } else snprintf(row,sizeof(row),"[-] %s  %s\r\n",peer->login,peer->name);
            session_impl_queue_text(s,row);
        }
    }
    if(ui->recipient_count)ui->input=BIG_MODEL_DESTINATION;
    else mmm_say(s,"No eligible telegram recipients.\r\n","選択できる電報の送信先がありません。\r\n");
}

static void password_line(Session *s,char *line) {
    BigModelUi *ui=big(s);
    int mode=ui->input==BIG_MODEL_OLD_PASSWORD?'o':
             ui->input==BIG_MODEL_NEW_PASSWORD?'p':'c';
    size_t length=strlen(line);
    PasswordAction action=password_flow_action(mode,1,!strcmp(line,"/CANCEL"),length,
                                                !strcmp(line,ui->old_password),!strcmp(line,ui->new_password));
    if(action==PASSWORD_STORE_OLD) {
        memcpy(ui->old_password,line,length+1);ui->input=BIG_MODEL_NEW_PASSWORD;
    } else if(action==PASSWORD_STORE_NEW) {
        memcpy(ui->new_password,line,length+1);ui->input=BIG_MODEL_CONFIRM_PASSWORD;
    } else {
        if(action==PASSWORD_CHANGE) {
            OperationRequest *q=mmm_request(s,'k');
            if(q) {
                strcpy(q->password,ui->old_password);strcpy(q->new_password,ui->new_password);
                mmm_submit(s,q,JOB_BIG_MODEL_PASSWORD);
            }
            session_core_throttle(&s->core,s->core.now);
        } else if(action!=PASSWORD_CANCEL)
            mmm_say(s,"Password not changed.\r\n","パスワードを変更できませんでした。\r\n");
        ui->input=BIG_MODEL_READY;
        OPENSSL_cleanse(ui->old_password,sizeof(ui->old_password));
        OPENSSL_cleanse(ui->new_password,sizeof(ui->new_password));
    }
    OPENSSL_cleanse(line,length);
}

static int input_line(Session *s,char *line) {
    BigModelUi *ui=big(s);
    if(ui->input==BIG_MODEL_READY)return 0;
    if(big_model_secret(s)) { password_line(s,line);return 1; }
    if(!strcmp(line,"/CANCEL") || (ui->input==BIG_MODEL_DESTINATION && !strcmp(line,"0"))) {
        ui->input=BIG_MODEL_READY;ui->recipient_count=0;
        memset(&ui->destination,0,sizeof(ui->destination));
        return 1;
    }
    uint32_t number;
    switch(ui->input) {
    case BIG_MODEL_DESTINATION:
        if(mmm_number(line,&number) && number<=ui->recipient_count) {
            ui->destination=ui->recipients[number-1];
            ui->input=BIG_MODEL_TELEGRAM;
        } else mmm_bad_service_command(s);
        break;
    case BIG_MODEL_TELEGRAM:
        if(big_model_telegram_valid(line)) {
            OperationRequest *q=mmm_request(s,'&');
            if(q) {
                q->action='D';q->chat_target=ui->destination.connection;q->chat_generation=ui->destination.generation;
                strcpy(q->body,line);mmm_submit(s,q,JOB_CHAT);
            }
            ui->input=BIG_MODEL_READY;ui->recipient_count=0;
            memset(&ui->destination,0,sizeof(ui->destination));
        } else mmm_say(s,"Enter 1–21 UTF-8 characters, or /CANCEL.\r\n","1〜21文字のUTF-8本文、または /CANCEL を入力してください。\r\n");
        break;
    case BIG_MODEL_TERM_DATE:
        if(!strcasecmp(line,"reset")) { ui->override_date=0;ui->input=BIG_MODEL_READY; }
        else if(big_model_date(line,&ui->override_since)) { ui->override_date=1;ui->input=BIG_MODEL_READY; }
        else mmm_bad_service_command(s);
        break;
    default: break;
    }
    return 1;
}

static void move_article(Session *s,int previous) {
    const BigModelUi *ui=big_view(s);
    uint32_t id=ui->shared.article;
    if(previous && id<=1) {
        mmm_say(s,"No earlier visible article.\r\n","これより前に表示できる記事はありません。\r\n");
        return;
    }
    mmm_ui_browse(s,BROWSE_FLAT_BODY,0,previous?id-1:id+1,previous,0);
}

static void board_line(Session *s,const char *line,int key,char *arg) {
    BigModelUi *ui=big(s);
    MmmUi *m=&ui->shared;
    uint32_t id;
    if(!strcmp(line,"0")) {
        if(m->screen==OPEN_SCREEN)m->screen=ui->article_return;
        else if(m->screen==BOARD_SCREEN)mmm_ui_boards(s,0);
        else m->screen=BOARD_SCREEN;
    } else if(mmm_number(line,&id) || (key=='R' && mmm_number(arg,&id)))read_article(s,id);
    else if(key=='R' || key=='L') {
        if(m->screen==INDEX_SCREEN && key=='L' && m->next)list_page(s,m->next);
        else if(m->screen==INDEX_SCREEN && key=='L')
            mmm_say(s,"End of list.\r\n","一覧の末尾です。\r\n");
        else { ui->list=BIG_MODEL_BOARD;list_page(s,0); }
    } else if(key=='B')mmm_ui_subboards(s,s->core.board);
    else if(key=='W')mmm_ui_prepare_post(s,0);
    else if(m->screen==OPEN_SCREEN && key=='A')mmm_ui_prepare_post(s,m->article);
    else if(m->screen==OPEN_SCREEN && key=='G' && m->note_parent)read_article(s,m->note_parent);
    else if(m->screen==OPEN_SCREEN && (key=='N' || key=='P' || !*line))move_article(s,key=='P');
    else if(key=='N' || key=='X')news(s,key=='X'?ARTICLE_SUBTREE:ARTICLE_HERE,1,0);
    else if(key=='S' && *arg && mmm_ui_text_valid(arg,80)) {
        ui->list=BIG_MODEL_FIND;strcpy(ui->query,arg);list_page(s,0);
    } else if(big_model_date(line,&ui->since)) {
        m->scan_custom=0;ui->news_suspended=0;ui->news_return=BOARD_SCREEN;ui->news_board=s->core.board;
        ui->list=BIG_MODEL_NEWS;ui->scope=ARTICLE_HERE;ui->until=0;ui->unread=0;list_page(s,0);
    } else mmm_bad_service_command(s);
}

static void sig_line(Session *s,const char *line,int key) {
    MmmUi *m=mmm_state(s);
    uint32_t index;
    if(!strcmp(line,"0")) {
        if(m->depth) { m->parent=m->ancestors[--m->depth];mmm_ui_boards(s,0); }
        else main_menu(s);
    } else if(key=='L' && m->next)mmm_ui_boards(s,m->next);
    else if(mmm_number(line,&index) && index<=m->row_count) {
        char id[16];snprintf(id,sizeof(id),"#%u",m->rows[index-1]);
        mmm_ui_enter_board(s,id,OPEN_BOARD_MENU);
    } else mmm_bad_service_command(s);
}

static int save_scanlist(Session *s,char *arg) {
    OperationRequest *q=mmm_request(s,BOARD_NOTE_VIEW);
    if(!q)return 1;
    q->action='w';
    int valid=!strcasecmp(arg,"reset") || (*arg && mmm_scanlist_parse(arg,&q->mmm_scanlist));
    if(valid)mmm_submit(s,q,JOB_BIG_MODEL_LIST_SAVE);
    else free(q);
    return valid;
}

static int service(Session *s,char *line,int key,char *arg) {
    MmmUi *m=mmm_state(s);
    if(m->screen==MAIL_SCREEN) {
        if(key=='R')return mmm_ui_mail_command(s,*arg?"read":"inbox",arg);
        if(key=='W')return mmm_ui_mail_command(s,"send",arg);
        if(key=='N')return mmm_ui_mail_command(s,"unread",arg);
        if(key=='A')return mmm_ui_mail_command(s,"reply",arg);
        if(key=='K')return mmm_ui_mail_command(s,"delete",arg);
        if(key=='L')return mmm_ui_mail_command(s,"more",arg);
    } else if(m->screen==FILER_SCREEN) {
        if(key=='L')return mmm_ui_filer_command(s,"list",arg);
        if(key=='S')return mmm_ui_filer_command(s,"find",arg);
        if(key=='I')return mmm_ui_filer_command(s,"info",arg);
        if(key=='D')return mmm_ui_filer_command(s,"get",arg);
        if(key=='U')return mmm_ui_filer_command(s,"put",arg);
        if(key=='R')return mmm_ui_filer_command(s,"resume",arg);
    } else if(m->screen==TERM_SCREEN) {
        if(!strcmp(line,"1")) {
            mmm_simple_operation(s,'}','v',big(s)->quiet?MEMBER_MENU_STANDARD:MEMBER_MENU_EXPERT,JOB_BIG_MODEL_MENU);
            return 1;
        }
        if(!strcmp(line,"2")) { big(s)->input=BIG_MODEL_OLD_PASSWORD;return 1; }
        if(!strcmp(line,"3")) { mmm_profile(s,"");return 1; }
        if(!strcmp(line,"4")) {
            m->profile_scope=m->profile_group=0;m->profile_filter[0]=0;
            mmm_ui_profile_search(s,0);return 1;
        }
        if(!strcmp(line,"5")) { big(s)->input=BIG_MODEL_TERM_DATE;return 1; }
        if(key=='6' && *arg)return save_scanlist(s,arg);
        if(!strcmp(line,"7")) { mmm_simple_operation(s,BOARD_NOTE_VIEW,'d',0,JOB_BIG_MODEL_LIST);return 1; }
        if(!strcmp(line,"8")) { MmmScanList defaults={0};big_model_scanlist(s,&defaults);return 1; }
        if(!strncasecmp(line,"9 ",2)) {
            const char *mode=line+2;
            if(strcasecmp(mode,"ON") && strcasecmp(mode,"OFF"))return 0;
            mmm_chat_request(s,'P',0,!strcasecmp(mode,"ON")?"/TALK ON":"/TALK OFF");
            return 1;
        }
        if(!strncmp(line,"10 ",3))return mmm_ui_transfer_protocol(s,line+3);
    }
    char *end=line+strcspn(line," \t");
    int split=*end!=0;
    if(split)*end=0;
    for(char *p=line;*p;p++)*p=(char)tolower((unsigned char)*p);
    char *value=split?end+1:end;
    value+=strspn(value," \t");
    int ok=0;
    if((m->screen==MAIL_SCREEN || m->screen==FILER_SCREEN) && !strcmp(line,"protocol"))
        return mmm_ui_transfer_protocol(s,value);
    if(m->screen==MAIL_SCREEN)ok=mmm_ui_mail_command(s,line,value);
    if(m->screen==FILER_SCREEN)ok=mmm_ui_filer_command(s,line,value);
    if(m->screen==TERM_SCREEN)ok=mmm_ui_term_command(s,line,value);
    if(m->screen==MANAGE_SCREEN)ok=mmm_ui_manage_command(s,line,value);
    return ok;
}

static void friends(Session *s) {
    mmm_simple_operation(s,'&','O',0,JOB_BIG_MODEL_DESTINATIONS);
}

void big_model_line(Session *s,char *line) {
    BigModelUi *ui=big(s);
    MmmUi *m=&ui->shared;
    if(input_line(s,line))return;
    if(m->screen!=CHAT_SCREEN)line+=strspn(line," \t");
    if((m->screen==MAIN_SCREEN || !s->core.user) && !strncasecmp(line,"ui",2) &&
       (!line[2] || line[2]==' ' || line[2]=='\t')) {
        mmm_ui_choose_profile(s,line+2+strspn(line+2," \t"));
        return;
    }
    if(!s->core.user) {
        if(!*line || !strcmp(line,"?"))big_model_menu(s,1);
        else if(!strncasecmp(line,"register ",9))mmm_ui_auth_start(s,line+9,'r');
        else if(!strncasecmp(line,"login ",6))mmm_ui_auth_start(s,line+6,'a');
        else mmm_ui_auth_start(s,line,'a');
        return;
    }
    if(!strcmp(line,"?")) { big_model_help(s,"");return; }
    if((m->screen==BOARD_SCREEN || m->screen==INDEX_SCREEN || m->screen==OPEN_SCREEN ||
        m->screen==NEW_SCREEN || m->screen==CHAT_SCREEN) && !strcmp(line,"!")) { friends(s);return; }
    if(m->screen==CHAT_SCREEN) {
        if(!strcmp(line,"/leave")) { chat_leave(s->core.id);m->chatting=0;main_menu(s); }
        else if(!strcmp(line,"/who"))mmm_chat_request(s,'O',0,"");
        else if(chat_text_valid(line))mmm_chat_request(s,'S',0,line);
        else mmm_bad_service_command(s);
        return;
    }
    if(m->screen==TALK_SCREEN && strcmp(line,"0")) { mmm_conference_line(s,line);return; }
    if(m->screen==NEW_SCREEN && !strcasecmp(line,"q")) {
        ui->news_suspended=1;main_menu(s);return;
    }
    if((m->screen==GAMES_SCREEN || m->screen==MANAGE_SCREEN || m->screen==TALK_SCREEN) && !strcmp(line,"0")) {
        m->screen=BIG_MODEL_EXT_SCREEN;big_model_menu(s,1);return;
    }
    if(m->screen==NEW_SCREEN && !strcmp(line,"0") && ui->news_return==BOARD_SCREEN) {
        char name[16];snprintf(name,sizeof(name),"#%u",ui->news_board);
        mmm_ui_enter_board(s,name,OPEN_BOARD_MENU);return;
    }
    if(!strcmp(line,"/") || (!strcmp(line,"0") && m->screen!=NOTES_SCREEN &&
       m->screen!=BOARD_SCREEN && m->screen!=INDEX_SCREEN && m->screen!=OPEN_SCREEN)) {
        main_menu(s);return;
    }
    BigModelCommand command=big_model_command(line);
    char *arg=command.argument;
    int key=command.key;
    uint32_t id;
    if(m->screen==MAIN_SCREEN) {
        switch(key) {
        case 'B': mmm_ui_boards(s,0);return;
        case 'C': mmm_chat_request(s,'J',0,"");return;
        case 'E': mmm_off(s,1);return;
        case 'H': big_model_help(s,"");return;
        case 'M': m->screen=MAIL_SCREEN;big_model_menu(s,1);return;
        case 'N': case 'O': case 'X':
            if(key=='N' && ui->news_suspended && !strcasecmp(arg,"resume")) {
                ui->news_suspended=0;ui->list=BIG_MODEL_NEWS;list_page(s,ui->news_cursor);
            } else if(!*arg)news(s,ARTICLE_ALL,key!='O',key!='X');
            else mmm_bad_service_command(s);
            return;
        case 'P': m->screen=FILER_SCREEN;mmm_file_enter(s,"");return;
        case 'T': friends(s);return;
        case 'U': mmm_ui_open_setup(s);return;
        case 'W': mmm_chat_request(s,'O',0,"");return;
        case '+': m->screen=BIG_MODEL_EXT_SCREEN;big_model_menu(s,1);return;
        case 'A': case 'D': case 'L': case 'Q': case 'Z':
            mmm_say(s,"This original service is unavailable.\r\n","この旧機能は利用できません。\r\n");return;
        default: break;
        }
    } else if(m->screen==NOTES_SCREEN) { sig_line(s,line,key);return; }
    else if(m->screen==BOARD_SCREEN || m->screen==INDEX_SCREEN || m->screen==OPEN_SCREEN) {
        board_line(s,line,key,arg);return;
    } else if(m->screen==NEW_SCREEN) {
        if(mmm_number(line,&id) || (key=='R' && mmm_number(arg,&id))) {
            for(unsigned i=0;i<m->row_count;i++)if(m->rows[i]==id) { read_article(s,id);return; }
        } else if(!*line && m->stream_at<m->row_count) { read_article(s,m->rows[m->stream_at++]);return; }
        else if(key=='L' || !*line) {
            if(m->next) { list_page(s,m->next);return; }
            if(m->scan_custom && m->scan_at+1<m->scan_count) { m->scan_at++;list_page(s,0);return; }
        }
    } else if(m->screen==GAMES_SCREEN) {
        if(mmm_number(line,&id) || !*line) { mmm_games(s,*line?id:0);return; }
    } else if(m->screen==BIG_MODEL_EXT_SCREEN) {
        if(!strcasecmp(line,"games")) { mmm_games(s,0);return; }
        if(!strcasecmp(line,"talk")) { mmm_conference_enter(s,"");return; }
        if(!strcasecmp(line,"manage")) { m->screen=MANAGE_SCREEN;mmm_manage_menu(s);return; }
        if(!strncasecmp(line,"draft",5) && (!line[5] || line[5]==' ')) {
            mmm_ui_note_draft_command(s,line+5+strspn(line+5," "));return;
        }
    } else if(service(s,line,key,arg))return;
    mmm_bad_service_command(s);
}

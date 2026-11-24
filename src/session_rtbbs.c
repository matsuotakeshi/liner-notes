#include "session_internal.h"
#include "menu.h"
#include <stdio.h>
#include <openssl/crypto.h>

static void enter(Session *s,int initial,int menu,Language language) {
    s->core.structured=0;
    if(!rtbbs(s)->initialized) { rtbbs(s)->initialized=1;rtbbs(s)->presentation.menu_mode=1; }
    if(initial) {
        rtbbs(s)->presentation.menu_mode=1;rtbbs(s)->reading.check.automatic=menu;
        rtbbs(s)->presentation.ui.language=language;
        char greeting[128];snprintf(greeting,sizeof(greeting),"RTBBS-PROBE %lu\r\n",s->core.id);
        session_impl_queue_text(s,greeting);
        rtbbs(s)->presentation.ui.enabled=menu!=0;
        if(menu) { session_impl_queue_text(s,banner_text());session_impl_queue_text(s,login_text(0));session_impl_queue_text(s,login_text(1)); }
    } else {
        rtbbs(s)->presentation.ui.view=UI_HOME;rtbbs(s)->presentation.ui.input=UI_INPUT_NONE;
        rtbbs(s)->presentation.classic_area=CM_MAIN;rtbbs(s)->presentation.menu_shown=0;
        rtbbs(s)->presentation.ui.enabled=menu!=0;rtbbs(s)->presentation.ui.language=language;
    }
    if(menu) { session_impl_show_menu(s,1);session_impl_prompt(s); }
}

static void leave(Session *s) { (void)s; /* Safe main menu; retained drafts/settings are not discarded. */ }

static int can_leave(const Session *s) {
    return rtbbs_const(s)->presentation.classic_area==CM_MAIN && rtbbs_const(s)->presentation.ui.view==UI_HOME &&
        !rtbbs_const(s)->presentation.ui.input && !rtbbs_const(s)->presentation.classic_input && !rtbbs_const(s)->presentation.game_menu &&
        !rtbbs_const(s)->presentation.page[0] && !rtbbs_const(s)->presentation.preferences.exit_action &&
        !rtbbs_const(s)->auth.application.step && !rtbbs_const(s)->editor.editing && !rtbbs_const(s)->editor.editor_upload &&
        !rtbbs_const(s)->editor.editor_confirm && !rtbbs_const(s)->editor.editor_upload_question &&
        !rtbbs_const(s)->reading.reader.active && !rtbbs_const(s)->reading.reader.automatic && !rtbbs_const(s)->reading.check.active &&
        !rtbbs_const(s)->reading.profile_walk.pending && !rtbbs_const(s)->reading.report.pending &&
        !rtbbs_const(s)->files.vault_mode && !rtbbs_const(s)->files.batch_screen && !rtbbs_const(s)->files.batch_copying && !rtbbs_const(s)->files.file_selecting &&
        !rtbbs_const(s)->mail_state.mail_mode && !rtbbs_const(s)->mail_state.mail_input && !rtbbs_const(s)->mail_state.mail_pending && !rtbbs_const(s)->mail_state.mail.delete_id &&
        !rtbbs_const(s)->conversation.chatting && !rtbbs_const(s)->conversation.gather.mode && !rtbbs_const(s)->conversation.outside_talk &&
        !rtbbs_const(s)->conversation.talk_select && !rtbbs_const(s)->conversation.talk_once && !rtbbs_const(s)->conversation.memo_input &&
        !rtbbs_const(s)->conversation.status_pending && !rtbbs_const(s)->conversation.chat_pending;
}
static void line(Session *s,char *text,size_t size) {
    session_impl_normalize_command(s,text,size);session_impl_command(s,text);
}
static int keep_position(Session *s,const OperationResult *result) {
    return reader_cancel_completion(&rtbbs(s)->reading.reader,result->sequence);
}

static SessionUiSnapshot snapshot(const Session *s) {
    const SessionRtbbsState *u=rtbbs_const(s);
    return (SessionUiSnapshot){.language=u->presentation.ui.language,.menu=u->presentation.ui.enabled,
        .secret=u->auth.password_mode!=0,.editing=u->editor.editing,.chatting=u->conversation.chatting,
        .chat_pending=u->conversation.chat_pending,.automatic=u->reading.reader.automatic,
        .redraw=u->conversation.chat_notice && u->conversation.chat_prompting && u->presentation.ui.enabled &&
            !u->auth.password_mode && !s->connection.binary && !s->connection.zmodem && !s->connection.door};
}
void session_rtbbs_cleanup(Session *s,int reason,int notify) {
    Language language=rtbbs(s)->presentation.ui.language;int menu=rtbbs(s)->presentation.ui.enabled;
    if(reason!=2)chat_disconnect(s->core.id);
    OPENSSL_cleanse(rtbbs(s),sizeof(*rtbbs(s)));
    if(reason==1) {
        rtbbs(s)->presentation.ui.language=language;rtbbs(s)->presentation.ui.enabled=menu;
        if(notify)session_impl_emit(s,"ERR session revoked\r\n");
    }
}
static void notify(Session *s,MessageId id,uint32_t number,const char *body) {
    char text[900];const Ui *ui=&rtbbs(s)->presentation.ui;
    if(id==MSG_SHUTDOWN_NOTICE)snprintf(text,sizeof(text),ui_text(ui,id),number,body);
    else if(id==MSG_ADMIN_NOTICE)snprintf(text,sizeof(text),ui_text(ui,id),body);
    else snprintf(text,sizeof(text),"%s",ui_text(ui,id));
    session_announce(s,text);
}
static int notice(const Session *s,SessionNotice kind,unsigned number,uint64_t bytes,uint64_t total,char *out,size_t size) {
    const Ui *ui=&rtbbs_const(s)->presentation.ui;int ja=ui->language==LANGUAGE_JA,n;
    const char *text=NULL;
    switch(kind) {
    case SESSION_NOTICE_DOOR:text=ja?"ゲーム開始。Ctrl-] でBBSへ戻ります。\r\n":"Game started. Ctrl-] returns to BBS.\r\n";break;
    case SESSION_NOTICE_PROGRESS:
        n=snprintf(out,size,ja?"\r\n受信データを登録中: %u%% (%llu / %llu bytes)\r\n":"\r\nRegistering received data: %u%% (%llu / %llu bytes)\r\n",number,(unsigned long long)bytes,(unsigned long long)total);
        return n>=0 && (size_t)n<size;
    case SESSION_NOTICE_TIME_WARNING:text=ja?"接続時間の上限が近づいています。\r\n":"Your session time limit is approaching.\r\n";break;
    case SESSION_NOTICE_TIME_GRACE:text=ja?"接続時間の上限です。猶予時間後に切断します。保存して終了してください。\r\n":"Session limit reached. Save your work before the grace period ends.\r\n";break;
    case SESSION_NOTICE_IDLE_WARNING:text=ui_text(ui,MSG_IDLE_WARNING);break;
    case SESSION_NOTICE_IDLE_CLOSED:text=ui->enabled?ui_text(ui,MSG_IDLE_CLOSED):"BYE idle timeout\r\n";break;
    default:return 0;
    }
    n=snprintf(out,size,"%s",text);return n>=0 && (size_t)n<size;
}

static int chat_event(Session *s,const ChatEvent *e) { return session_impl_chat_event(s,e); }

static const SessionUiAdapter adapter={
    .name="rtbbs",.state_size=sizeof(SessionRtbbsState),.snapshot=snapshot,.cleanup=session_rtbbs_cleanup,
    .connector_line=session_impl_connector_line,.binary_byte=session_rtbbs_binary_byte,
    .prepare_request=session_rtbbs_prepare_request,.preferences=session_rtbbs_disconnect_preferences,
    .signature=session_rtbbs_ui_signature,.scan_pause=session_rtbbs_scan_pause,.scan_stop=session_rtbbs_scan_stop,
    .board_refresh=session_rtbbs_board_refresh,.access_refresh=session_rtbbs_access_refresh,.chat_kick=session_rtbbs_chat_kick,.chat_event=chat_event,
    .announce=session_rtbbs_announce,.notify=notify,.mail_arrived=session_rtbbs_mail_arrived,.mail_notify=session_rtbbs_mail_notify,
    .transfer_receipt=session_rtbbs_transfer_receipt,.transfer_finish=session_rtbbs_zmodem_finish,.notice=notice,
    .enter=enter,.leave=leave,.can_leave=can_leave,.line=line,
    .keep_position=keep_position,.result=session_rtbbs_result,.emit=session_rtbbs_emit,
    .prompt=session_rtbbs_prompt,.menu=session_rtbbs_menu,.help=session_rtbbs_help,
    .uppercase=session_rtbbs_uppercase,.tick=session_rtbbs_tick,.door_finish=session_rtbbs_door_finish
};
const SessionUiAdapter *session_ui_rtbbs(void) { return &adapter; }

static NotificationAudience notification_audience(const Session *s) {
    unsigned blocked=(s->connection.door?NOTICE_DOOR:0)|(s->connection.binary?NOTICE_BINARY:0)|(s->connection.zmodem?NOTICE_TRANSFER:0)|
        (rtbbs_const(s)->auth.password_mode?NOTICE_SECRET:0)|(s->core.pending?NOTICE_PENDING:0)|(rtbbs_const(s)->presentation.page[0]?NOTICE_PAGE:0)|
        (rtbbs_const(s)->editor.editing?NOTICE_EDITOR:0)|(s->core.wake?NOTICE_TIMER:0)|
        ((rtbbs_const(s)->presentation.ui.input || rtbbs_const(s)->presentation.classic_input || rtbbs_const(s)->mail_state.mail_input || rtbbs_const(s)->mail_state.mail.delete_id)?NOTICE_INPUT:0)|
        (rtbbs_const(s)->conversation.chatting?NOTICE_CHAT:0)|(rtbbs_const(s)->reading.reader.active?NOTICE_SCAN:0);
    return (NotificationAudience){s->core.user,blocked,s->core.active,s->core.closing,rtbbs_const(s)->presentation.ui.enabled,s->connection.binary_menu};
}

void session_rtbbs_mail_notify(Session *s,uint32_t user,int deny,uint32_t revision) {
    notification_mail_preference(&rtbbs(s)->conversation.notifications,s->core.user,user,deny,revision);
}

void session_rtbbs_mail_arrived(Session *s,uint32_t recipient) {
    NotificationAudience audience=notification_audience(s);
    notification_mail_arrived(&rtbbs(s)->conversation.notifications,&audience,recipient);
}

void session_rtbbs_announce(Session *s,const char *text) {
    NotificationAudience audience=notification_audience(s);
    notification_announce(&rtbbs(s)->conversation.notifications,&audience,text);
}

void session_rtbbs_tick(Session *s,uint64_t now) {
    if(check_traversal_auto_ready(&rtbbs(s)->reading.check,rtbbs(s)->presentation.page[0]!=0,s->core.pending,s->core.closing,1) && (!s->connection.io.drained || s->connection.io.drained(s->connection.io.context))) { session_impl_extended_command(s,"CHECK DONE");return; }
    if(rtbbs(s)->reading.reader.automatic && s->core.active && !s->core.pending) {
        if(rtbbs(s)->reading.reader.active && rtbbs(s)->reading.scan.cursor)session_impl_submit(s,&rtbbs(s)->reading.scan);
        else { rtbbs(s)->reading.reader.automatic=0;session_impl_prompt(s); }
    }
    s->core.now=now;
    if(rtbbs(s)->editor.editing && rtbbs(s)->editor.editor_upload && now>=rtbbs(s)->editor.editor_deadline && !s->core.pending) { rtbbs(s)->editor.editor_upload=0;rtbbs(s)->editor.editor_input=0;session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"受信終了。編集を続けられます。\r\n":"Text received. Continue editing.\r\n");session_impl_prompt(s); }
    NotificationAudience audience=notification_audience(s);
    const char *announcement=notification_announcement_peek(&rtbbs(s)->conversation.notifications,&audience);
    if(announcement) { session_impl_queue_text(s,announcement);notification_announcement_clear(&rtbbs(s)->conversation.notifications); }
    /* A failed announcement write may abort the session before mail delivery. */
    audience=notification_audience(s);
    uint32_t arrivals=notification_mail_take(&rtbbs(s)->conversation.notifications,&audience);
    if(arrivals) {
        char text[256];snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,MSG_MAIL_ARRIVED),arrivals);
        rtbbs(s)->conversation.chat_notice=1;session_impl_queue_text(s,text);rtbbs(s)->conversation.chat_prompting=1;session_impl_prompt(s);rtbbs(s)->conversation.chat_prompting=0;rtbbs(s)->conversation.chat_notice=0;
    }
    if(s->core.active && s->core.wake && now>=s->core.wake) { s->core.wake=0; session_impl_emit(s,"AWAKE\r\n"); session_impl_prompt(s); }
}

void session_impl_selection_reset(Session *s) {
    rtbbs(s)->files.file_selected=rtbbs(s)->files.file_selecting=0;rtbbs(s)->files.batch_screen=rtbbs(s)->files.batch_copying=0;
    batch_reset(&rtbbs(s)->files.batch);
    vault_navigation_reset(&rtbbs(s)->files.vault);
    rtbbs(s)->presentation.game_menu=0;rtbbs(s)->files.vault_mode=0;
    session_core_clear_vault_grants(&s->core);
    profile_form_target(&rtbbs(s)->reading.profile_form,0);profile_walk_reset(&rtbbs(s)->reading.profile_walk);
    rtbbs(s)->conversation.talk_once=rtbbs(s)->conversation.talk_return_editor=rtbbs(s)->conversation.status_pending=rtbbs(s)->conversation.memo_input=0;
    rtbbs(s)->conversation.talk_target=rtbbs(s)->conversation.reply_target=0;rtbbs(s)->conversation.talk_select=rtbbs(s)->conversation.talk_lost=rtbbs(s)->conversation.outside_talk=0;
    rtbbs(s)->presentation.classic_input=0;rtbbs(s)->presentation.classic_id=0;rtbbs(s)->presentation.classic_area=CM_MAIN;
}


void session_rtbbs_prepare_request(Session *s,OperationRequest *q) {
    if(q->op=='V')session_impl_preferences_request(s,q);
    if(q!=&rtbbs(s)->reading.scan)q->board=s->core.board;
    if(q->op=='Z' && q->until && rtbbs(s)->reading.check.active==1)q->board=rtbbs(s)->reading.check.board;
    if(q->op=='v')rtbbs(s)->presentation.ui.unread_root=q->number;
}

void session_impl_submit(Session *s,BoardRequest *q) {
    s->ui_binding.active->prepare_request(s,q);
    SessionOperationStatus result=session_submit(s,q);
    if(result==SESSION_OPERATION_THROTTLED)session_impl_emit(s,"ERR try later\r\n");
    else if(result==SESSION_OPERATION_LOGIN_REQUIRED)session_impl_emit(s,"ERR login required\r\n");
    else if(result!=SESSION_OPERATION_ACCEPTED)session_impl_emit(s,"ERR board busy\r\n");
}

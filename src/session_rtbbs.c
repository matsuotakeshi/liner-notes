#include "session_internal.h"
#include "menu.h"
#include <stdio.h>

static void enter(Session *s,int initial,int menu,Language language) {
    if(initial) {
        s->presentation.menu_mode=1;s->reading.check.automatic=menu;
        s->presentation.ui.language=language;
        char greeting[128];snprintf(greeting,sizeof(greeting),"RTBBS-PROBE %lu\r\n",s->core.id);
        session_impl_queue_text(s,greeting);
        s->presentation.ui.enabled=menu!=0;
        if(menu) { session_impl_queue_text(s,banner_text());session_impl_queue_text(s,login_text(0));session_impl_queue_text(s,login_text(1)); }
    } else {
        s->presentation.ui.view=UI_HOME;s->presentation.ui.input=UI_INPUT_NONE;
        s->presentation.classic_area=CM_MAIN;s->presentation.menu_shown=0;
        s->presentation.ui.enabled=menu!=0;s->presentation.ui.language=language;
    }
    if(menu) { session_impl_show_menu(s,1);session_impl_prompt(s); }
}

static void leave(Session *s) { (void)s; /* Safe main menu; retained drafts/settings are not discarded. */ }

static int can_leave(const Session *s) {
    return s->presentation.classic_area==CM_MAIN && s->presentation.ui.view==UI_HOME &&
        !s->presentation.ui.input && !s->presentation.classic_input && !s->presentation.game_menu &&
        !s->presentation.page[0] && !s->presentation.preferences.exit_action &&
        !s->auth.application.step && !s->editor.editing && !s->editor.editor_upload &&
        !s->editor.editor_confirm && !s->editor.editor_upload_question &&
        !s->reading.reader.active && !s->reading.reader.automatic && !s->reading.check.active &&
        !s->reading.profile_walk.pending && !s->reading.report.pending &&
        !s->files.vault_mode && !s->files.batch_screen && !s->files.batch_copying && !s->files.file_selecting &&
        !s->mail_state.mail_mode && !s->mail_state.mail_input && !s->mail_state.mail_pending && !s->mail_state.mail.delete_id &&
        !s->conversation.chatting && !s->conversation.gather.mode && !s->conversation.outside_talk &&
        !s->conversation.talk_select && !s->conversation.talk_once && !s->conversation.memo_input &&
        !s->conversation.status_pending && !s->conversation.chat_pending;
}
static void line(Session *s,char *text,size_t size) {
    session_impl_normalize_command(s,text,size);session_impl_command(s,text);
}
static int keep_position(Session *s,const OperationResult *result) {
    return reader_cancel_completion(&s->reading.reader,result->sequence);
}

static const SessionUiAdapter adapter={
    .name="rtbbs",.enter=enter,.leave=leave,.can_leave=can_leave,.line=line,
    .keep_position=keep_position,.result=session_rtbbs_result,.emit=session_rtbbs_emit,
    .prompt=session_rtbbs_prompt,.menu=session_rtbbs_menu,.help=session_rtbbs_help,
    .uppercase=session_rtbbs_uppercase,.tick=session_rtbbs_tick,.door_finish=session_rtbbs_door_finish
};
const SessionUiAdapter *session_ui_rtbbs(void) { return &adapter; }

static NotificationAudience notification_audience(const Session *s) {
    unsigned blocked=(s->connection.door?NOTICE_DOOR:0)|(s->connection.binary?NOTICE_BINARY:0)|(s->connection.zmodem?NOTICE_TRANSFER:0)|
        (s->auth.password_mode?NOTICE_SECRET:0)|(s->core.pending?NOTICE_PENDING:0)|(s->presentation.page[0]?NOTICE_PAGE:0)|
        (s->editor.editing?NOTICE_EDITOR:0)|(s->core.wake?NOTICE_TIMER:0)|
        ((s->presentation.ui.input || s->presentation.classic_input || s->mail_state.mail_input || s->mail_state.mail.delete_id)?NOTICE_INPUT:0)|
        (s->conversation.chatting?NOTICE_CHAT:0)|(s->reading.reader.active?NOTICE_SCAN:0);
    return (NotificationAudience){s->core.user,blocked,s->core.active,s->core.closing,s->presentation.ui.enabled,s->connection.binary_menu};
}

void session_mail_notify(Session *s,uint32_t user,int deny,uint32_t revision) {
    notification_mail_preference(&s->conversation.notifications,s->core.user,user,deny,revision);
}

void session_mail_arrived(Session *s,uint32_t recipient) {
    NotificationAudience audience=notification_audience(s);
    notification_mail_arrived(&s->conversation.notifications,&audience,recipient);
}

void session_announce(Session *s,const char *text) {
    NotificationAudience audience=notification_audience(s);
    notification_announce(&s->conversation.notifications,&audience,text);
}

void session_rtbbs_tick(Session *s,uint64_t now) {
    if(check_traversal_auto_ready(&s->reading.check,s->presentation.page[0]!=0,s->core.pending,s->core.closing,1) && (!s->connection.io.drained || s->connection.io.drained(s->connection.io.context))) { session_impl_extended_command(s,"CHECK DONE");return; }
    if(s->reading.reader.automatic && s->core.active && !s->core.pending) {
        if(s->reading.reader.active && s->reading.scan.cursor)session_impl_submit(s,&s->reading.scan);
        else { s->reading.reader.automatic=0;session_impl_prompt(s); }
    }
    s->core.now=now;
    if(s->editor.editing && s->editor.editor_upload && now>=s->editor.editor_deadline && !s->core.pending) { s->editor.editor_upload=0;s->editor.editor_input=0;session_impl_queue_text(s,s->presentation.ui.language==LANGUAGE_JA?"受信終了。編集を続けられます。\r\n":"Text received. Continue editing.\r\n");session_impl_prompt(s); }
    NotificationAudience audience=notification_audience(s);
    const char *announcement=notification_announcement_peek(&s->conversation.notifications,&audience);
    if(announcement) { session_impl_queue_text(s,announcement);notification_announcement_clear(&s->conversation.notifications); }
    /* A failed announcement write may abort the session before mail delivery. */
    audience=notification_audience(s);
    uint32_t arrivals=notification_mail_take(&s->conversation.notifications,&audience);
    if(arrivals) {
        char text[256];snprintf(text,sizeof(text),ui_text(&s->presentation.ui,MSG_MAIL_ARRIVED),arrivals);
        s->conversation.chat_notice=1;session_impl_queue_text(s,text);s->conversation.chat_prompting=1;session_impl_prompt(s);s->conversation.chat_prompting=0;s->conversation.chat_notice=0;
    }
    if(s->core.active && s->core.wake && now>=s->core.wake) { s->core.wake=0; session_impl_emit(s,"AWAKE\r\n"); session_impl_prompt(s); }
}

#define _POSIX_C_SOURCE 200809L
#include "editor_core.h"
#include "batch_protocol.h"
#include "list_navigation_protocol.h"
#include "traversal_protocol.h"
#include "editor_commands.h"
#include "editor_view.h"
#include "classic_input.h"
#include "terminal_pager.h"
#include "connector_protocol.h"
#include "runtime_limits.h"
#include "app_limits.h"
#include "activity.h"
#include "vault.h"
#include "session_internal.h"
#include "members.h"
#include "catalog.h"
#include "store_limits.h"
#include "profile_store.h"
#include "menu.h"
#include "chat.h"
#include "date_filter.h"
#include "vault_bridge.h"
#include "transfer_line.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <openssl/crypto.h>

static void chat_prompt(Session *s);
static void chat_error(Session *s,MessageId id,const char *raw);
static void talk_start(Session *s,unsigned long target);

int session_impl_conversation_command(Session *s,char *line) {
    if(!strcmp(line,"TALK") || !strncmp(line,"TALK ",5) || !strcmp(line,"/REPLY") || !strcmp(line,"ONLINE")) {
        if(!board_feature_enabled("chat")) { session_impl_emit(s,"ERR feature disabled\r\n");return 1; }
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        BoardRequest q={0};q.op='&';q.action=!strcmp(line,"ONLINE")?'O':'V';
        if(!strcmp(line,"/REPLY")) { if(!rtbbs(s)->conversation.reply_target) { chat_error(s,MSG_TALK_INVALID,"CHAT ERROR target\r\n");return 1; }q.chat_target=rtbbs(s)->conversation.reply_target; }
        else if(!strncmp(line,"TALK ",5) && !session_impl_talk_number(line+5,&q.chat_target)) { session_impl_emit(s,"ERR command\r\n");return 1; }
        session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;return 1;
    }
    if(!strcmp(line,"MEMO") || !strncmp(line,"MEMO ",5) || !strcmp(line,"CALL") || !strncmp(line,"CALL ",5)) {
        BoardRequest q={0};q.op='&';q.action=*line=='M'?'M':'C';
        const char *text=line[4]?line+5:q.action=='C'?"SYSOP":"";
        if(strlen(text)>(q.action=='M'?80u:512u) || (*text && !session_impl_safe_text(text))) { session_impl_emit(s,"ERR command\r\n");return 1; }
        strcpy(q.body,text);session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;return 1;
    }
    if(!strcmp(line,"CHAT")) {
        if(!board_feature_enabled("chat")) { session_impl_emit(s,"ERR feature disabled\r\n");return 1; }
        if(!s->core.user || !s->core.member_mode) { session_impl_emit(s,"ERR login required\r\n");return 1; }
        BoardRequest q={0};q.op='&';q.action='J';session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;return 1;
    }

    return 0;
}

int session_impl_status_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(rtbbs(s)->conversation.status_pending) { rtbbs(s)->conversation.status_pending=0;session_impl_emit(s,completion->text);if(rtbbs(s)->conversation.chatting)chat_prompt(s);else session_impl_prompt(s);return 1; }

    return 0;
}

const char *session_impl_chat_delivery(Session *s,const OperationResult *r,uint64_t now,MessageId *message) {
    if(!rtbbs(s)->conversation.chatting) { *message=MSG_TALK_GONE;return "CHAT ERROR target\r\n"; }
    int status;
    if(r->chat_action=='D') {
        status=chat_private(s->core.id,r->chat_target,r->text,now);
        if(status>0 && rtbbs(s)->conversation.talk_once) {
            rtbbs(s)->conversation.talk_once=0;rtbbs(s)->conversation.talk_target=0;
            rtbbs(s)->conversation.talk_select=rtbbs(s)->conversation.talk_lost=0;
            if(rtbbs(s)->conversation.talk_return_editor) {
                rtbbs(s)->conversation.talk_return_editor=0;
                rtbbs(s)->conversation.outside_talk=0;rtbbs(s)->conversation.chatting=0;
            }
            if(rtbbs(s)->conversation.chatting)chat_prompt(s);else session_impl_prompt(s);
        }
        if(status==-3) { *message=MSG_TALK_REFUSED;return "CHAT ERROR refused\r\n"; }
        if(status==-2) {
            rtbbs(s)->conversation.talk_lost=1;
            *message=MSG_TALK_GONE;return "CHAT ERROR target\r\n";
        }
    } else {
        status=chat_say(s->core.id,r->text,now);
        if(status==-4) { *message=MSG_CHAT_STORAGE;return "CHAT ERROR storage\r\n"; }
    }
    if(status>0)return NULL;
    *message=status<0?MSG_CHAT_RATE:MSG_CHAT_INVALID;
    return status<0?"CHAT ERROR rate\r\n":"CHAT ERROR text\r\n";
}

int session_impl_chat_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(rtbbs(s)->conversation.chat_pending) {
        rtbbs(s)->conversation.chat_pending=0;
        if(!completion->chat_action) {
            if(rtbbs(s)->conversation.chatting) { rtbbs(s)->conversation.chatting=0;chat_leave(s->core.id); }
            session_impl_emit(s,completion->text);session_impl_prompt(s);return 1;
        }
        if(completion->chat_action=='M') {
            int ok=chat_memo(s->core.id,completion->text);
            session_impl_queue_text(s,ok?(rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_MEMO_SAVED):"MEMO SAVED\r\n"):"ERR command\r\n");session_impl_prompt(s);
        } else if(completion->chat_action=='C') {
            int count=chat_call(s->core.id,completion->text,now);char text[256];
            if(count<0)session_impl_queue_text(s,rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_SYSOP_RATE):"CALL ERROR rate\r\n");
            else { snprintf(text,sizeof(text),rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_SYSOP_RESULT):"CALL DELIVERED %u\r\n",(unsigned)count);session_impl_queue_text(s,text); }
            session_impl_prompt(s);
        } else if(completion->chat_action=='V') {
            rtbbs(s)->conversation.chatting=1;rtbbs(s)->conversation.outside_talk=1;rtbbs(s)->conversation.talk_lost=0;rtbbs(s)->conversation.talk_target=0;rtbbs(s)->conversation.talk_select=1;
            session_impl_page_clear(s);rtbbs(s)->reading.reader.active=0;
            if(completion->chat_target)talk_start(s,completion->chat_target);else chat_prompt(s);
        } else if(completion->chat_action=='O') { chat_online(s->core.id);
        } else if(completion->chat_action=='J') {
            rtbbs(s)->conversation.outside_talk=0;
            rtbbs(s)->conversation.talk_target=rtbbs(s)->conversation.reply_target=0;rtbbs(s)->conversation.talk_select=rtbbs(s)->conversation.talk_lost=0;
            rtbbs(s)->conversation.chatting=1;session_impl_page_clear(s);rtbbs(s)->reading.reader.active=0;
            session_impl_queue_text(s,rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_CHAT_ENTER):"CHAT OPEN\r\n");
            if(!chat_join_config(s->core.id,s->core.user,completion->chat_login,completion->chat_name,session_chat_event,s,&completion->chat_preferences)) {
                rtbbs(s)->conversation.chatting=0;chat_error(s,MSG_CHAT_FULL,"CHAT ERROR full\r\n");session_impl_prompt(s);
            }
        } else if(rtbbs(s)->conversation.chatting && completion->chat_action=='K') {
            char text[256];snprintf(text,sizeof(text),rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_CHAT_KICK_DONE):"CHAT KICK %s %u\r\n",completion->text,completion->chat_kick_count);
            session_impl_queue_text(s,text);chat_prompt(s);
        } else if(rtbbs(s)->conversation.chatting && completion->chat_action=='!') {
            session_impl_emit(s,completion->text);chat_prompt(s);
        } else if(rtbbs(s)->conversation.chatting && completion->chat_action=='P') {
            chat_settings(s->core.id);
        } else if(rtbbs(s)->conversation.chatting && completion->chat_action=='E') {
            chat_error(s,MSG_TALK_MUTE_ERROR,"CHAT ERROR mute\r\n");
        } else if(rtbbs(s)->conversation.chatting && completion->chat_action=='T') {
            if(chat_target(s->core.id,completion->chat_target)) {
                rtbbs(s)->conversation.talk_target=completion->chat_target;rtbbs(s)->conversation.talk_lost=0;
                char text[256];snprintf(text,sizeof(text),rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_OPEN):"CHAT TALK OPEN %lu\r\n",rtbbs(s)->conversation.talk_target);session_impl_queue_text(s,text);chat_prompt(s);
            } else { rtbbs(s)->conversation.talk_lost=1;chat_error(s,MSG_TALK_INVALID,"CHAT ERROR target\r\n"); }
        } else if(rtbbs(s)->conversation.chatting && completion->chat_action=='W')chat_who(s->core.id);
        else if(rtbbs(s)->conversation.chatting && (completion->chat_action=='S' || completion->chat_action=='D')) {
            MessageId message;
            const char *error=session_impl_chat_delivery(s,completion,now,&message);
            if(error)chat_error(s,message,error);
        }
        return 1;
    }

    return 0;
}

static void chat_prompt(Session *s) {
    if(!rtbbs(s)->presentation.ui.enabled || !s->core.active)return;
    char text[128];
    if(rtbbs(s)->conversation.talk_select)snprintf(text,sizeof(text),"%s",ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_SELECT));
    else if(rtbbs(s)->conversation.talk_lost)snprintf(text,sizeof(text),"%s",ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_BLOCKED));
    else if(rtbbs(s)->conversation.talk_target)snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_PROMPT),rtbbs(s)->conversation.talk_target);
    else snprintf(text,sizeof(text),"%s",ui_text(&rtbbs(s)->presentation.ui,MSG_CHAT_PROMPT));
    rtbbs(s)->conversation.chat_prompting=1;session_impl_queue_text(s,text);rtbbs(s)->conversation.chat_prompting=0;
}

int session_impl_chat_event(void *context,const ChatEvent *e) {
    Session *s=context;if(!s->core.active || s->core.closing || !s->core.user || s->connection.zmodem || s->connection.door)return 0;
    if(e->kind=='X' && e->connection!=rtbbs(s)->conversation.talk_target)return 1;
    if((e->kind=='I' || e->kind=='C') && !rtbbs(s)->conversation.chatting && (rtbbs(s)->auth.password_mode || rtbbs(s)->editor.editing || s->connection.binary || s->connection.zmodem || s->core.pending || rtbbs(s)->presentation.page[0] || rtbbs(s)->presentation.ui.input || rtbbs(s)->presentation.classic_input || rtbbs(s)->mail_state.mail_input || rtbbs(s)->mail_state.mail.delete_id || s->core.wake))return 0;
    if((e->kind=='J' || e->kind=='S') && (!rtbbs(s)->conversation.chatting || rtbbs(s)->conversation.outside_talk))return 1;
    if((e->kind=='L' || e->kind=='X') && (!rtbbs(s)->conversation.chatting || rtbbs(s)->conversation.outside_talk) && e->connection!=rtbbs(s)->conversation.talk_target)return 1;
    char text[1400];
    if(e->kind=='I')rtbbs(s)->conversation.reply_target=e->connection;
    if((e->kind=='L' || e->kind=='X') && e->connection==rtbbs(s)->conversation.talk_target)rtbbs(s)->conversation.talk_lost=1;
    if(rtbbs(s)->presentation.ui.enabled) {
        MessageId id=e->kind=='J'?MSG_CHAT_JOIN:(e->kind=='L' || e->kind=='X')?MSG_CHAT_LEAVE:e->kind=='W'?MSG_CHAT_WHO:MSG_CHAT_SAY;
        if(e->kind=='A')snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_RECEIVE),e->text);
        else if(e->kind=='M')snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_MUTED),e->login);
        else if(e->kind=='E')snprintf(text,sizeof(text),"\r\n%s",ui_text(&rtbbs(s)->presentation.ui,MSG_CHAT_END));
        else if(e->kind=='I' || e->kind=='O')snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,e->kind=='I'?MSG_TALK_IN:MSG_TALK_OUT),e->connection,e->name,e->login,e->text);
        else if(e->kind=='C')snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,MSG_SYSOP_CALL),e->connection,e->name,e->login,e->text);
        else if(e->kind=='W')snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,MSG_ONLINE_MEMO),e->connection,e->name,e->login,e->text);
        else if(e->kind=='S')snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,id),e->name,e->login,e->text);
        else snprintf(text,sizeof(text),ui_text(&rtbbs(s)->presentation.ui,id),e->name,e->login);
    } else {
        if(e->kind=='A')snprintf(text,sizeof(text),"CHAT RECEIVE %s\r\n",e->text);
        else if(e->kind=='M')snprintf(text,sizeof(text),"CHAT MUTED %s\r\n",e->login);
        else if(e->kind=='E')strcpy(text,"CHAT END\r\n");
        else snprintf(text,sizeof(text),"CHAT %s %lu %u %s\t%s\t%s\r\n",e->kind=='C'?"SYSOP CALL":e->kind=='I'?"PRIVATE FROM":e->kind=='O'?"PRIVATE TO":e->kind=='J'?"JOIN":(e->kind=='L' || e->kind=='X')?"LEAVE":e->kind=='W'?"WHO":"SAY",e->connection,e->user,e->login,e->name,e->text);
    }
    rtbbs(s)->conversation.chat_notice=1;session_impl_queue_text(s,text);
    if((e->kind=='L' || e->kind=='X') && e->connection==rtbbs(s)->conversation.talk_target)session_impl_queue_text(s,rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_GONE):"CHAT TALK GONE\r\n");
    if(!e->replay && !(e->kind=='O' && rtbbs(s)->conversation.talk_once)) { if(rtbbs(s)->conversation.chatting)chat_prompt(s);else { rtbbs(s)->conversation.chat_prompting=1;session_impl_prompt(s);rtbbs(s)->conversation.chat_prompting=0; } }
    rtbbs(s)->conversation.chat_notice=0;return s->core.active;
}

static void chat_error(Session *s,MessageId id,const char *raw) {
    session_impl_queue_text(s,rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,id):raw);chat_prompt(s);
}

int session_impl_talk_number(const char *text,unsigned long *id) {
    unsigned long value=0;if(!*text)return 0;
    for(;*text;text++) { if(*text<'0'||*text>'9')return 0;unsigned digit=(unsigned)(*text-'0');if(value>(ULONG_MAX-digit)/10)return 0;value=value*10+digit; }
    if(!value)return 0;
    *id=value;return 1;
}

static void talk_start(Session *s,unsigned long target) {
    BoardRequest q={0};q.op='&';q.action='T';q.chat_target=target;
    /* A failed selection must never turn subsequent private text into public text. */
    rtbbs(s)->conversation.talk_select=0;rtbbs(s)->conversation.talk_lost=1;session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;
    if(!s->core.pending)chat_prompt(s);
}

void session_impl_session_status(Session *s) {
    char out[320],stamp[80];time_t now=time(NULL);struct tm *local=localtime(&now);if(local)strftime(stamp,sizeof(stamp),date_pattern(),local);else strcpy(stamp,"unknown");
    snprintf(out,sizeof(out),"STATUS\r\nTIME %s\r\nCONNECTION %lu / SECONDS %llu / ONLINE %u\r\nMEMBER %u / LEVEL %u / BOARD %u\r\n",stamp,s->core.id,(unsigned long long)(s->core.now>=s->core.connected_at?(s->core.now-s->core.connected_at)/1000:0),s->connection.io.online?s->connection.io.online(s->connection.io.context):0,s->core.user,s->core.member_level,s->core.board);session_impl_queue_text(s,out);
    if(!s->core.user) { if(rtbbs(s)->conversation.chatting)chat_prompt(s);else session_impl_prompt(s);return; }
    BoardRequest q={0};q.op='!';q.action='V';session_impl_submit(s,&q);rtbbs(s)->conversation.status_pending=s->core.pending;
}

void session_impl_chat_input(Session *s,char *line) {
    if(rtbbs(s)->conversation.memo_input) { rtbbs(s)->conversation.memo_input=0;if(!strcmp(line,"/CANCEL")) { session_impl_emit(s,"CANCELLED\r\n");chat_prompt(s);return; }BoardRequest q={0};q.op='&';q.action='M';if(strlen(line)>80 || (*line && !session_impl_safe_text(line))) { chat_error(s,MSG_CHAT_INVALID,"CHAT ERROR text\r\n");return; }strcpy(q.body,line);session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;return; }
    if(!strcmp(line,"\\") || !strcmp(line,"``")) { rtbbs(s)->conversation.memo_input=1;session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"接続者メモ (80バイト以内): ":"Connection memo (up to 80 bytes): ");return; }
    if(!strcmp(line,"#")) { session_impl_session_status(s);return; }
    if(!strcmp(line,"D") || !strcmp(line,"d") || !strcmp(line,"#")) { BoardRequest q={0};q.op='&';q.action=*line=='#'?'O':'C';if(q.action=='C')strcpy(q.body,"SYSOP");
        session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;return; }

    if(!strncmp(line,"/KICK ",6)) {
        BoardRequest q={0};q.op='&';q.action='K';snprintf(q.body,sizeof(q.body),"%s",line+6);
        session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;if(!s->core.pending)chat_prompt(s);return;
    }
    if(!strcmp(line,"/TALK ON") || !strcmp(line,"/TALK OFF") || !strcmp(line,"/MUTES") ||
       !strncmp(line,"/MUTE ",6) || !strncmp(line,"/UNMUTE ",8)) {
        BoardRequest q={0};q.op='&';q.action='P';snprintf(q.body,sizeof(q.body),"%s",line);
        session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;if(!s->core.pending)chat_prompt(s);return;
    }
    if(rtbbs(s)->conversation.outside_talk && ((!*line && rtbbs(s)->conversation.talk_select) || !strcmp(line,"//") || !strcmp(line,"/END") || !strcmp(line,"/LEAVE") || !strcmp(line,"/QUIT"))) {
        rtbbs(s)->conversation.outside_talk=0;rtbbs(s)->conversation.chatting=0;rtbbs(s)->conversation.talk_target=0;rtbbs(s)->conversation.talk_select=rtbbs(s)->conversation.talk_lost=0;rtbbs(s)->conversation.talk_once=rtbbs(s)->conversation.talk_return_editor=0;
        session_impl_queue_text(s,rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_OUTSIDE_END):"TALK CLOSED\r\n");
        if(!strcmp(line,"/QUIT"))session_rtbbs_command(s,"QUIT");else session_impl_prompt(s);return;
    }
    if((!strcmp(line,"//") || !strcmp(line,"/END")) && (rtbbs(s)->conversation.talk_target || rtbbs(s)->conversation.talk_select || rtbbs(s)->conversation.talk_lost)) {
        rtbbs(s)->conversation.talk_target=0;rtbbs(s)->conversation.talk_select=0;rtbbs(s)->conversation.talk_lost=0;
        session_impl_queue_text(s,rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_TALK_END):"CHAT TALK CLOSED\r\n");chat_prompt(s);return;
    }
    if(!strcmp(line,"T") || !strcmp(line,"t") || !strcmp(line,"/TALK")) {
        rtbbs(s)->conversation.talk_once=!rtbbs(s)->conversation.outside_talk && *line!='/';rtbbs(s)->conversation.talk_select=1;chat_prompt(s);return;
    }
    if(!strcmp(line,"/REPLY")) { talk_start(s,rtbbs(s)->conversation.reply_target);return; }
    if(!strncmp(line,"T ",2) || !strncmp(line,"t ",2) || !strncmp(line,"/TALK ",6)) {
        unsigned long target;
        if(!session_impl_talk_number(line+(*line=='/'?6:2),&target)) { rtbbs(s)->conversation.talk_lost=1;chat_error(s,MSG_TALK_INVALID,"CHAT ERROR target\r\n");return; }
        rtbbs(s)->conversation.talk_once=!rtbbs(s)->conversation.outside_talk && *line!='/';talk_start(s,target);return;
    }
    if(!strcmp(line,"//") || !strcmp(line,"/LEAVE") || !strcmp(line,"/QUIT")) {
        int quit=!strcmp(line,"/QUIT");rtbbs(s)->conversation.talk_target=rtbbs(s)->conversation.reply_target=0;rtbbs(s)->conversation.talk_select=rtbbs(s)->conversation.talk_lost=0;rtbbs(s)->conversation.chatting=0;chat_leave(s->core.id);
        session_impl_queue_text(s,rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_CHAT_EXIT):"CHAT CLOSED\r\n");
        if(quit) { session_rtbbs_command(s,"QUIT");return; }
        if(rtbbs(s)->presentation.ui.enabled)session_impl_show_menu(s,1);
        session_impl_prompt(s);return;
    }
    if(!strcmp(line,"?") || !strcmp(line,"/HELP")) { session_impl_queue_text(s,help_text(rtbbs(s)->presentation.ui.language,"CHAT")?help_text(rtbbs(s)->presentation.ui.language,"CHAT"):ui_text(&rtbbs(s)->presentation.ui,MSG_HELP_CHAT));if(!rtbbs(s)->presentation.ui.enabled)session_impl_queue_text(s,"END HELP\r\n");chat_prompt(s);return; }
    if(!strncmp(line,"/LANG ",6)) {
        Language language;if(language_parse(line+6,&language))rtbbs(s)->presentation.ui.language=language;
        else { chat_error(s,MSG_CHAT_COMMAND,"CHAT ERROR command\r\n");return; }
        chat_prompt(s);return;
    }
    if(!*line) { chat_prompt(s);return; }
    BoardRequest q={0};q.op='&';q.action='S';
    if(!strcmp(line,"W") || !strcmp(line,"w") || !strcmp(line,"/WHO"))q.action='O';
    else {
        if(rtbbs(s)->conversation.talk_select) {
            unsigned long target;if(!session_impl_talk_number(line,&target)) { chat_error(s,MSG_TALK_INVALID,"CHAT ERROR target\r\n");return; }
            talk_start(s,target);return;
        }
        if(rtbbs(s)->conversation.talk_lost || (rtbbs(s)->conversation.outside_talk && !rtbbs(s)->conversation.talk_target)) { chat_error(s,MSG_TALK_GONE,"CHAT ERROR target\r\n");return; }
        const char *text=!strncmp(line,"/SAY ",5)?line+5:line;
        if(text==line && *line=='/') { chat_error(s,MSG_CHAT_COMMAND,"CHAT ERROR command\r\n");return; }
        if(!chat_text_valid(text)) { chat_error(s,MSG_CHAT_INVALID,"CHAT ERROR text\r\n");return; }
        strcpy(q.body,text);
        if(rtbbs(s)->conversation.talk_target) { q.action='D';q.chat_target=rtbbs(s)->conversation.talk_target; }
    }
    session_impl_submit(s,&q);rtbbs(s)->conversation.chat_pending=s->core.pending;if(!s->core.pending)chat_prompt(s);
}

void session_rtbbs_chat_kick(Session *s) {
    chat_disconnect(s->core.id);
    if(!s->core.active || s->core.closing)return;
    if(rtbbs(s)->conversation.chatting) { rtbbs(s)->conversation.chatting=0;chat_leave(s->core.id); }
    rtbbs(s)->conversation.chat_pending=0;s->core.pending=0;rtbbs(s)->conversation.talk_target=rtbbs(s)->conversation.reply_target=0;
    session_impl_queue_text(s,rtbbs(s)->presentation.ui.enabled?ui_text(&rtbbs(s)->presentation.ui,MSG_CHAT_KICKED):"CHAT KICKED\r\n");session_core_close(&s->core);
}

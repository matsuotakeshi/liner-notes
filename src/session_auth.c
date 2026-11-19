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
#include "session_policy_core.h"
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

static void application_prompt(Session *s);

int session_impl_application_input(Session *s,char *line) {

    if(rtbbs(s)->auth.application.step) {
        ApplicationInput input=(!strcmp(line,"/CANCEL") || !strcmp(line,"/cancel"))?APPLICATION_CANCEL:
            (!strcmp(line,"Y") || !strcmp(line,"y"))?APPLICATION_CONFIRM:
            (!strcmp(line,"E") || !strcmp(line,"e"))?APPLICATION_RESTART:APPLICATION_VALUE;
        const char *key=application_form_field(&rtbbs(s)->auth.application);int field=key?profile_field(key):-1;
        ApplicationEffect effect=application_form_input(&rtbbs(s)->auth.application,input,line,field<0 || profile_text_valid((unsigned)field,line));
        switch(effect) {
        case APPLICATION_ABORTED:session_impl_emit(s,"CANCELLED\r\n");break;
        case APPLICATION_PASSWORD:rtbbs(s)->auth.password_mode='g';session_impl_emit(s,"PASSWORD\r\n");break;
        case APPLICATION_CONFIRM_PROMPT:session_impl_queue_text(s,"Y / E / /CANCEL > ");break;
        case APPLICATION_INVALID:session_impl_emit(s,"ERR application value\r\n");application_prompt(s);break;
        case APPLICATION_PROMPT:application_prompt(s);break;
        case APPLICATION_REVIEW:session_impl_queue_text(s,rtbbs(s)->auth.application.body);session_impl_queue_text(s,rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"この内容で申請しますか？ Y:確定 E:入力し直す /CANCEL:中止 > ":"Submit these details? Y:confirm E:edit /CANCEL:abort > ");break;
        default:break;
        }
        return 1;
    }

    return 0;
}

int session_impl_password_input(Session *s,char *line) {
    PasswordAction action=password_flow_action(rtbbs(s)->auth.password_mode,rtbbs(s)->presentation.ui.enabled,!strcmp(line,"/CANCEL"),strlen(line),!strcmp(line,rtbbs(s)->auth.old_password),!strcmp(line,rtbbs(s)->auth.new_password));
    if(rtbbs(s)->auth.password_mode=='f' || rtbbs(s)->auth.password_mode=='v' || rtbbs(s)->auth.password_mode=='w') {
        int mode=rtbbs(s)->auth.password_mode;rtbbs(s)->auth.password_mode=0;session_core_throttle(&s->core,s->core.now);
        if(action==PASSWORD_CANCEL)session_impl_emit(s,"CANCELLED\r\n");
        else if(action==PASSWORD_LENGTH)session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else if(action==PASSWORD_FILE_CONFIRM) { strcpy(rtbbs(s)->auth.new_password,line);rtbbs(s)->auth.password_mode='w';session_impl_emit(s,"CONFIRM PASSWORD\r\n"); }
        else if(action==PASSWORD_MISMATCH)session_impl_emit(s,"ERR password mismatch\r\n");
        else { BoardRequest q={0};q.op='Y';q.action=mode=='f'?VAULT_CMD_UNLOCK:VAULT_CMD_PASSWORD;snprintf(q.body,sizeof(q.body),"%s|%s",rtbbs(s)->auth.filepass_path,line);session_impl_submit(s,&q);OPENSSL_cleanse(&q,sizeof(q)); }
        if(mode!='v')OPENSSL_cleanse(rtbbs(s)->auth.new_password,sizeof(rtbbs(s)->auth.new_password));
        OPENSSL_cleanse(line,strlen(line));return 1;
    }
    if(rtbbs(s)->auth.password_mode=='b') {
        rtbbs(s)->auth.password_mode=0;session_core_throttle(&s->core,s->core.now);
        if(action==PASSWORD_CANCEL)session_impl_emit(s,"CANCELLED\r\n");
        else if(action==PASSWORD_LENGTH)session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else { BoardRequest q={0};q.op='(';q.action='U';q.number=rtbbs(s)->auth.board_enter;q.cursor=rtbbs(s)->auth.board_lock;strcpy(q.password,line);session_impl_submit(s,&q);OPENSSL_cleanse(&q,sizeof(q)); }
        OPENSSL_cleanse(line,strlen(line));return 1;
    }
    if(rtbbs(s)->auth.password_mode=='o' || rtbbs(s)->auth.password_mode=='p' || rtbbs(s)->auth.password_mode=='c') {
        int mode=rtbbs(s)->auth.password_mode;
        if(action==PASSWORD_CANCEL) { session_impl_emit(s,"CANCELLED\r\n"); }
        else if(action==PASSWORD_LENGTH) { session_impl_emit(s,"ERR password 8..128 bytes\r\n"); }
        else if(action==PASSWORD_STORE_OLD) { strcpy(rtbbs(s)->auth.old_password,line); rtbbs(s)->auth.password_mode='p'; session_impl_emit(s,"NEW PASSWORD\r\n"); return 1; }
        else if(action==PASSWORD_STORE_NEW) { strcpy(rtbbs(s)->auth.new_password,line); rtbbs(s)->auth.password_mode='c'; session_impl_emit(s,"CONFIRM PASSWORD\r\n"); return 1; }
        else if(action==PASSWORD_CHANGE) {
            BoardRequest q={0}; q.op=rtbbs(s)->auth.boardpass_target?'(':rtbbs(s)->auth.reset_target?'~':'k';q.number=rtbbs(s)->auth.boardpass_target?rtbbs(s)->auth.boardpass_target:rtbbs(s)->auth.reset_target;q.action=rtbbs(s)->auth.boardpass_target?'S':1; strcpy(q.password,rtbbs(s)->auth.old_password); strcpy(q.new_password,rtbbs(s)->auth.new_password);
            session_impl_submit(s,&q); OPENSSL_cleanse(&q,sizeof(q));
        } else session_impl_emit(s,mode=='c'?"ERR password mismatch\r\n":"ERR password unchanged\r\n");
        rtbbs(s)->auth.password_mode=0;rtbbs(s)->auth.reset_target=0;rtbbs(s)->auth.boardpass_target=0; session_core_throttle(&s->core,s->core.now);
        OPENSSL_cleanse(rtbbs(s)->auth.old_password,sizeof(rtbbs(s)->auth.old_password)); OPENSSL_cleanse(rtbbs(s)->auth.new_password,sizeof(rtbbs(s)->auth.new_password));
        return 1;
    }
    if(rtbbs(s)->auth.password_mode) {
        if(rtbbs(s)->presentation.ui.enabled && action==PASSWORD_CANCEL) { rtbbs(s)->auth.password_mode=0; session_impl_emit(s,"CANCELLED\r\n"); return 1; }
        BoardRequest q={0}; q.op=(char)rtbbs(s)->auth.password_mode; strcpy(q.username,rtbbs(s)->auth.login);if(q.op=='g') { strcpy(q.body,rtbbs(s)->auth.application.body);application_form_clear(&rtbbs(s)->auth.application); }
        rtbbs(s)->auth.password_mode=0;
        if(action==PASSWORD_LENGTH) session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else { strcpy(q.password,line); session_impl_submit(s,&q); }
        session_core_throttle(&s->core,s->core.now);OPENSSL_cleanse(q.password,sizeof(q.password)); OPENSSL_cleanse(line,strlen(line)); return 1;
    }

    return 0;
}

int session_impl_password_reset_command(Session *s,char *line) {
    if(!strncmp(line,"RESETPASS ",10)) {
        if(!rtbbs(s)->presentation.ui.admin) { session_impl_emit(s,"ERR admin required\r\n");return 1; }
        BoardRequest q={0};const char *tail;q.op='~';
        if(!session_impl_number_arg(line+10,&q.number,&tail) || *tail) { session_impl_emit(s,"ERR command\r\n");return 1; }
        if(s->core.now<s->core.auth_after) { session_impl_emit(s,"ERR try later\r\n");return 1; }
        session_impl_submit(s,&q);return 1;
    }

    return 0;
}

int session_impl_password_change_command(Session *s,char *line) {
    if(!strcmp(line,"PASSWD")) {
        if(!s->core.user) { session_impl_emit(s,"ERR login required\r\n"); return 1; }
        if(s->core.now<s->core.auth_after) { session_impl_emit(s,"ERR try later\r\n"); return 1; }
        rtbbs(s)->auth.password_mode='o'; session_impl_emit(s,"CURRENT PASSWORD\r\n"); return 1;
    }

    return 0;
}

int session_impl_account_command(Session *s,char *line) {
    char out[128];
    if(!strncmp(line,"LOGIN ",6) || !strncmp(line,"REGISTER ",9)) {
        if(!s->core.member_mode) { session_impl_emit(s,"ERR members disabled\r\n"); return 1; }
        if(s->core.user) { session_impl_emit(s,"ERR logout first\r\n"); return 1; }
        if(s->core.now<s->core.auth_after) { session_impl_emit(s,"ERR try later\r\n"); return 1; }
        const char *name=line+(line[0]=='L'?6:9); size_t n=strlen(name);
        if(!n || n>32) { session_impl_emit(s,"ERR username\r\n"); return 1; }
        for(size_t i=0;i<n;i++) if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='0'&&name[i]<='9')||name[i]=='_'||name[i]=='-')) { session_impl_emit(s,"ERR username\r\n"); return 1; }
        strcpy(rtbbs(s)->auth.login,name);
        if(line[0]=='R' && members_detailed_form()) { application_form_begin(&rtbbs(s)->auth.application);application_prompt(s);return 1; }
        rtbbs(s)->auth.password_mode=line[0]=='L'?'a':'g'; session_impl_emit(s,"PASSWORD\r\n");
    } else if(!strcmp(line,"ACCOUNT")) {
        if(!s->core.user) session_impl_emit(s,"ERR login required\r\n");
        else { snprintf(out,sizeof(out),"ACCOUNT %u %s\r\n",s->core.user,s->core.username); session_impl_emit(s,out); }
    } else if(!strcmp(line,"LOGOUT")) {
        if(preference_exit_begin(&rtbbs(s)->presentation.preferences,s->core.user,PREF_EXIT_LOGOUT)) { session_rtbbs_command(s,"PREFS SAVE");preference_exit_submitted(&rtbbs(s)->presentation.preferences,s->core.pending);return 1; }
        if(!session_logout(s)) { session_impl_emit(s,"ERR board busy\r\n");return 1; }
        rtbbs(s)->presentation.preferences.exit_action=PREF_EXIT_NONE;session_impl_selection_reset(s);
        rtbbs(s)->presentation.classic_input=0;rtbbs(s)->presentation.classic_id=0;rtbbs(s)->presentation.classic_area=CM_MAIN;check_traversal_reset(&rtbbs(s)->reading.check);session_core_clear_vault_grants(&s->core);
        chat_disconnect(s->core.id);rtbbs(s)->mail_state.mail_mode=0;mail_navigation_clear_filter(&rtbbs(s)->mail_state.mail);
        batch_reset(&rtbbs(s)->files.batch);rtbbs(s)->presentation.game_menu=0;rtbbs(s)->files.vault_mode=0;notification_mail_discard(&rtbbs(s)->conversation.notifications);rtbbs(s)->presentation.ui.admin=0;rtbbs(s)->presentation.ui.sigop=0;rtbbs(s)->auth.reset_target=0;  memset(&rtbbs(s)->editor.draft,0,sizeof(rtbbs(s)->editor.draft)); rtbbs(s)->editor.draft_revision=0; session_impl_undo_clear(s); session_impl_emit(s,"LOGGED OUT\r\n");
    }
    else return 0;
    return 1;
}

int session_impl_authentication_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->preferences) {
        PreferenceValues current=session_impl_session_preferences(s);
        PreferenceRecord stored={completion->language,completion->pager,completion->editor_preference,completion->columns,completion->rows,completion->ui_aux};
        PreferenceValues loaded=preference_unpack(&stored,&current);
        rtbbs(s)->presentation.menu_mode=completion->menu_mode;rtbbs(s)->presentation.menu_shown=0;rtbbs(s)->presentation.ui.language=(Language)loaded.language;
        rtbbs(s)->presentation.pager_enabled=loaded.pager;rtbbs(s)->editor.editor_line=loaded.editor;rtbbs(s)->editor.editor_numbers=(int)loaded.line_numbers;s->connection.transfer_protocol=loaded.transfer_protocol;
        if(completion->columns && completion->rows)session_dimensions(s,loaded.columns,loaded.rows);
    }
    if(completion->auth) { session_impl_selection_reset(s);mail_navigation_clear_filter(&rtbbs(s)->mail_state.mail);notification_mail_reset(&rtbbs(s)->conversation.notifications);chat_disconnect(s->core.id); }
    if(completion->auth>0)session_mail_notify(s,completion->mail_notify_user,completion->mail_notify_deny,completion->mail_notify_revision);
    if(completion->auth>0) { rtbbs(s)->presentation.menu_mode=completion->menu_mode;rtbbs(s)->presentation.menu_shown=0; }
    if(completion->auth>0 && rtbbs(s)->presentation.ui.enabled) { session_impl_queue_text(s,login_text(1));time_t greet_now=time(NULL);struct tm *greet_tm=localtime(&greet_now);if(greet_tm)session_impl_queue_text(s,hourly_greeting((unsigned)greet_tm->tm_hour));if(completion->birthday)session_impl_queue_text(s,*login_text(2)?login_text(2):rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"お誕生日おめでとうございます！\r\n":"Happy birthday!\r\n"); }
    if(completion->auth>0 || completion->preferences) { PreferenceValues values=session_impl_session_preferences(s);preference_observed(&rtbbs(s)->presentation.preferences,&values); }
    if(completion->auth>0 && board_feature_enabled("chat"))chat_register(s->core.id,s->core.user,completion->chat_login,completion->chat_name,session_chat_event,s,&completion->chat_preferences);

    return 0;
}

int session_impl_password_prompt_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='3') { rtbbs(s)->auth.boardpass_target=completion->service_number;rtbbs(s)->auth.password_mode='p'; }

    return 0;
}

int session_impl_account_flags_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='p') { rtbbs(s)->auth.reset_target=completion->service_number;rtbbs(s)->auth.password_mode='p'; }
    if(completion->auth)rtbbs(s)->presentation.ui.admin=completion->auth>0 && completion->chat_admin;
    if(completion->auth>0)chat_admin(s->core.id,completion->chat_admin);
    if(completion->auth>0 && rtbbs(s)->presentation.ui.enabled && rtbbs(s)->presentation.ui.admin)session_impl_queue_text(s,ui_text(&rtbbs(s)->presentation.ui,MSG_MENU_ADMIN));

    return 0;
}

static void application_prompt(Session *s) { char text[180];snprintf(text,sizeof(text),rtbbs(s)->presentation.ui.language==LANGUAGE_JA?"%s（省略はEnter、中止は/CANCEL）: ":"%s (Enter to skip, /CANCEL to abort): ",application_form_field(&rtbbs(s)->auth.application));session_impl_queue_text(s,text); }

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

    if(s->auth.application.step) {
        ApplicationInput input=(!strcmp(line,"/CANCEL") || !strcmp(line,"/cancel"))?APPLICATION_CANCEL:
            (!strcmp(line,"Y") || !strcmp(line,"y"))?APPLICATION_CONFIRM:
            (!strcmp(line,"E") || !strcmp(line,"e"))?APPLICATION_RESTART:APPLICATION_VALUE;
        const char *key=application_form_field(&s->auth.application);int field=key?profile_field(key):-1;
        ApplicationEffect effect=application_form_input(&s->auth.application,input,line,field<0 || profile_text_valid((unsigned)field,line));
        switch(effect) {
        case APPLICATION_ABORTED:session_impl_emit(s,"CANCELLED\r\n");break;
        case APPLICATION_PASSWORD:s->auth.password_mode='g';session_impl_emit(s,"PASSWORD\r\n");break;
        case APPLICATION_CONFIRM_PROMPT:session_impl_queue_text(s,"Y / E / /CANCEL > ");break;
        case APPLICATION_INVALID:session_impl_emit(s,"ERR application value\r\n");application_prompt(s);break;
        case APPLICATION_PROMPT:application_prompt(s);break;
        case APPLICATION_REVIEW:session_impl_queue_text(s,s->auth.application.body);session_impl_queue_text(s,s->presentation.ui.language==LANGUAGE_JA?"この内容で申請しますか？ Y:確定 E:入力し直す /CANCEL:中止 > ":"Submit these details? Y:confirm E:edit /CANCEL:abort > ");break;
        default:break;
        }
        return 1;
    }

    return 0;
}

int session_impl_password_input(Session *s,char *line) {
    PasswordAction action=password_flow_action(s->auth.password_mode,s->presentation.ui.enabled,!strcmp(line,"/CANCEL"),strlen(line),!strcmp(line,s->auth.old_password),!strcmp(line,s->auth.new_password));
    if(s->auth.password_mode=='f' || s->auth.password_mode=='v' || s->auth.password_mode=='w') {
        int mode=s->auth.password_mode;s->auth.password_mode=0;s->core.auth_after=s->core.now+1000;
        if(action==PASSWORD_CANCEL)session_impl_emit(s,"CANCELLED\r\n");
        else if(action==PASSWORD_LENGTH)session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else if(action==PASSWORD_FILE_CONFIRM) { strcpy(s->auth.new_password,line);s->auth.password_mode='w';session_impl_emit(s,"CONFIRM PASSWORD\r\n"); }
        else if(action==PASSWORD_MISMATCH)session_impl_emit(s,"ERR password mismatch\r\n");
        else { BoardRequest q={0};q.op='Y';q.action=mode=='f'?VAULT_CMD_UNLOCK:VAULT_CMD_PASSWORD;snprintf(q.body,sizeof(q.body),"%s|%s",s->auth.filepass_path,line);session_impl_submit(s,&q);OPENSSL_cleanse(&q,sizeof(q)); }
        if(mode!='v')OPENSSL_cleanse(s->auth.new_password,sizeof(s->auth.new_password));
        OPENSSL_cleanse(line,strlen(line));return 1;
    }
    if(s->auth.password_mode=='b') {
        s->auth.password_mode=0;s->core.auth_after=s->core.now+1000;
        if(action==PASSWORD_CANCEL)session_impl_emit(s,"CANCELLED\r\n");
        else if(action==PASSWORD_LENGTH)session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else { BoardRequest q={0};q.op='(';q.action='U';q.number=s->auth.board_enter;q.cursor=s->auth.board_lock;strcpy(q.password,line);session_impl_submit(s,&q);OPENSSL_cleanse(&q,sizeof(q)); }
        OPENSSL_cleanse(line,strlen(line));return 1;
    }
    if(s->auth.password_mode=='o' || s->auth.password_mode=='p' || s->auth.password_mode=='c') {
        int mode=s->auth.password_mode;
        if(action==PASSWORD_CANCEL) { session_impl_emit(s,"CANCELLED\r\n"); }
        else if(action==PASSWORD_LENGTH) { session_impl_emit(s,"ERR password 8..128 bytes\r\n"); }
        else if(action==PASSWORD_STORE_OLD) { strcpy(s->auth.old_password,line); s->auth.password_mode='p'; session_impl_emit(s,"NEW PASSWORD\r\n"); return 1; }
        else if(action==PASSWORD_STORE_NEW) { strcpy(s->auth.new_password,line); s->auth.password_mode='c'; session_impl_emit(s,"CONFIRM PASSWORD\r\n"); return 1; }
        else if(action==PASSWORD_CHANGE) {
            BoardRequest q={0}; q.op=s->auth.boardpass_target?'(':s->auth.reset_target?'~':'k';q.number=s->auth.boardpass_target?s->auth.boardpass_target:s->auth.reset_target;q.action=s->auth.boardpass_target?'S':1; strcpy(q.password,s->auth.old_password); strcpy(q.new_password,s->auth.new_password);
            session_impl_submit(s,&q); OPENSSL_cleanse(&q,sizeof(q));
        } else session_impl_emit(s,mode=='c'?"ERR password mismatch\r\n":"ERR password unchanged\r\n");
        s->auth.password_mode=0;s->auth.reset_target=0;s->auth.boardpass_target=0; s->core.auth_after=s->core.now+1000;
        OPENSSL_cleanse(s->auth.old_password,sizeof(s->auth.old_password)); OPENSSL_cleanse(s->auth.new_password,sizeof(s->auth.new_password));
        return 1;
    }
    if(s->auth.password_mode) {
        if(s->presentation.ui.enabled && action==PASSWORD_CANCEL) { s->auth.password_mode=0; session_impl_emit(s,"CANCELLED\r\n"); return 1; }
        BoardRequest q={0}; q.op=(char)s->auth.password_mode; strcpy(q.username,s->core.username);if(q.op=='g') { strcpy(q.body,s->auth.application.body);application_form_clear(&s->auth.application); }
        s->auth.password_mode=0;
        if(action==PASSWORD_LENGTH) session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else { strcpy(q.password,line); session_impl_submit(s,&q); }
        s->core.auth_after=s->core.now+1000;OPENSSL_cleanse(q.password,sizeof(q.password)); OPENSSL_cleanse(line,strlen(line)); return 1;
    }

    return 0;
}

int session_impl_password_reset_command(Session *s,char *line) {
    if(!strncmp(line,"RESETPASS ",10)) {
        if(!s->presentation.ui.admin) { session_impl_emit(s,"ERR admin required\r\n");return 1; }
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
        s->auth.password_mode='o'; session_impl_emit(s,"CURRENT PASSWORD\r\n"); return 1;
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
        strcpy(s->core.username,name);
        if(line[0]=='R' && members_detailed_form()) { application_form_begin(&s->auth.application);application_prompt(s);return 1; }
        s->auth.password_mode=line[0]=='L'?'a':'g'; session_impl_emit(s,"PASSWORD\r\n");
    } else if(!strcmp(line,"ACCOUNT")) {
        if(!s->core.user) session_impl_emit(s,"ERR login required\r\n");
        else { snprintf(out,sizeof(out),"ACCOUNT %u %s\r\n",s->core.user,s->core.username); session_impl_emit(s,out); }
    } else if(!strcmp(line,"LOGOUT")) {
        if(preference_exit_begin(&s->presentation.preferences,s->core.user,PREF_EXIT_LOGOUT)) { session_impl_command_core(s,"PREFS SAVE");preference_exit_submitted(&s->presentation.preferences,s->core.pending);return 1; }
        s->presentation.preferences.exit_action=PREF_EXIT_NONE;session_impl_selection_reset(s);
        s->presentation.classic_input=0;s->presentation.classic_id=0;s->presentation.classic_area=CM_MAIN;check_traversal_reset(&s->reading.check);memset(s->core.vault_grants,0,sizeof(s->core.vault_grants));
        chat_disconnect(s->core.id);s->mail_state.mail_mode=0;mail_navigation_clear_filter(&s->mail_state.mail);
        memset(s->core.board_grants,0,sizeof(s->core.board_grants));batch_reset(&s->files.batch);s->presentation.game_menu=0;s->files.vault_mode=0;notification_mail_discard(&s->conversation.notifications);s->core.user=0;s->presentation.ui.admin=0;s->presentation.ui.sigop=0;s->auth.reset_target=0; s->core.user_epoch=0; s->core.current=0; s->core.board=1; memset(&s->editor.draft,0,sizeof(s->editor.draft)); s->editor.draft_revision=0; session_impl_undo_clear(s); session_impl_emit(s,"LOGGED OUT\r\n");
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
        s->presentation.menu_mode=completion->menu_mode;s->presentation.menu_shown=0;s->presentation.ui.language=(Language)loaded.language;
        s->presentation.pager_enabled=loaded.pager;s->editor.editor_line=loaded.editor;s->editor.editor_numbers=(int)loaded.line_numbers;s->connection.transfer_protocol=loaded.transfer_protocol;
        if(completion->columns && completion->rows)session_dimensions(s,loaded.columns,loaded.rows);
    }
    if(completion->auth) { session_impl_selection_reset(s);memset(s->core.board_grants,0,sizeof(s->core.board_grants));mail_navigation_clear_filter(&s->mail_state.mail);notification_mail_reset(&s->conversation.notifications);chat_disconnect(s->core.id);session_core_identity(&s->core,completion,now); }
    if(completion->auth>0)session_mail_notify(s,completion->mail_notify_user,completion->mail_notify_deny,completion->mail_notify_revision);
    if(completion->auth>0) { s->presentation.menu_mode=completion->menu_mode;s->presentation.menu_shown=0; }
    if(completion->auth>0 && s->presentation.ui.enabled) { session_impl_queue_text(s,login_text(1));time_t greet_now=time(NULL);struct tm *greet_tm=localtime(&greet_now);if(greet_tm)session_impl_queue_text(s,hourly_greeting((unsigned)greet_tm->tm_hour));if(completion->birthday)session_impl_queue_text(s,*login_text(2)?login_text(2):s->presentation.ui.language==LANGUAGE_JA?"お誕生日おめでとうございます！\r\n":"Happy birthday!\r\n"); }
    if(completion->auth>0 || completion->preferences) { PreferenceValues values=session_impl_session_preferences(s);preference_observed(&s->presentation.preferences,&values); }
    if(completion->auth>0 && board_feature_enabled("chat"))chat_register(s->core.id,s->core.user,completion->chat_login,completion->chat_name,session_impl_chat_event,s,&completion->chat_preferences);

    return 0;
}

int session_impl_password_prompt_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='3') { s->auth.boardpass_target=completion->service_number;s->auth.password_mode='p'; }

    return 0;
}

int session_impl_account_flags_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='p') { s->auth.reset_target=completion->service_number;s->auth.password_mode='p'; }
    if(completion->auth)s->presentation.ui.admin=completion->auth>0 && completion->chat_admin;
    if(completion->auth>0)chat_admin(s->core.id,completion->chat_admin);
    if(completion->auth>0 && s->presentation.ui.enabled && s->presentation.ui.admin)session_impl_queue_text(s,ui_text(&s->presentation.ui,MSG_MENU_ADMIN));
    if(completion->password_changed) { s->core.user_epoch=completion->account_epoch; s->core.auth_after=now+1000; }

    return 0;
}

static void application_prompt(Session *s) { char text[180];snprintf(text,sizeof(text),s->presentation.ui.language==LANGUAGE_JA?"%s（省略はEnter、中止は/CANCEL）: ":"%s (Enter to skip, /CANCEL to abort): ",application_form_field(&s->auth.application));session_impl_queue_text(s,text); }

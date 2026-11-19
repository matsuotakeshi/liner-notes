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

static void application_prompt(Session *s);

int session_impl_application_input(Session *s,char *line) {

    if(s->application.step) {
        ApplicationInput input=(!strcmp(line,"/CANCEL") || !strcmp(line,"/cancel"))?APPLICATION_CANCEL:
            (!strcmp(line,"Y") || !strcmp(line,"y"))?APPLICATION_CONFIRM:
            (!strcmp(line,"E") || !strcmp(line,"e"))?APPLICATION_RESTART:APPLICATION_VALUE;
        const char *key=application_form_field(&s->application);int field=key?profile_field(key):-1;
        ApplicationEffect effect=application_form_input(&s->application,input,line,field<0 || profile_text_valid((unsigned)field,line));
        switch(effect) {
        case APPLICATION_ABORTED:session_impl_emit(s,"CANCELLED\r\n");break;
        case APPLICATION_PASSWORD:s->password_mode='g';session_impl_emit(s,"PASSWORD\r\n");break;
        case APPLICATION_CONFIRM_PROMPT:session_impl_queue_text(s,"Y / E / /CANCEL > ");break;
        case APPLICATION_INVALID:session_impl_emit(s,"ERR application value\r\n");application_prompt(s);break;
        case APPLICATION_PROMPT:application_prompt(s);break;
        case APPLICATION_REVIEW:session_impl_queue_text(s,s->application.body);session_impl_queue_text(s,s->ui.language==LANGUAGE_JA?"この内容で申請しますか？ Y:確定 E:入力し直す /CANCEL:中止 > ":"Submit these details? Y:confirm E:edit /CANCEL:abort > ");break;
        default:break;
        }
        return 1;
    }

    return 0;
}

int session_impl_password_input(Session *s,char *line) {
    if(s->password_mode=='f' || s->password_mode=='v' || s->password_mode=='w') {
        int mode=s->password_mode;s->password_mode=0;s->auth_after=s->now+1000;
        if(!strcmp(line,"/CANCEL"))session_impl_emit(s,"CANCELLED\r\n");
        else if(strlen(line)<8 || strlen(line)>128)session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else if(mode=='v') { strcpy(s->new_password,line);s->password_mode='w';session_impl_emit(s,"CONFIRM PASSWORD\r\n"); }
        else if(mode=='w' && strcmp(line,s->new_password))session_impl_emit(s,"ERR password mismatch\r\n");
        else { BoardRequest q={0};q.op='Y';q.action=mode=='f'?VAULT_CMD_UNLOCK:VAULT_CMD_PASSWORD;snprintf(q.body,sizeof(q.body),"%s|%s",s->filepass_path,line);session_impl_submit(s,&q);OPENSSL_cleanse(&q,sizeof(q)); }
        if(mode!='v')OPENSSL_cleanse(s->new_password,sizeof(s->new_password));
        OPENSSL_cleanse(line,strlen(line));return 1;
    }
    if(s->password_mode=='b') {
        s->password_mode=0;s->auth_after=s->now+1000;
        if(!strcmp(line,"/CANCEL"))session_impl_emit(s,"CANCELLED\r\n");
        else if(strlen(line)<8 || strlen(line)>128)session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else { BoardRequest q={0};q.op='(';q.action='U';q.number=s->board_enter;q.cursor=s->board_lock;strcpy(q.password,line);session_impl_submit(s,&q);OPENSSL_cleanse(&q,sizeof(q)); }
        OPENSSL_cleanse(line,strlen(line));return 1;
    }
    if(s->password_mode=='o' || s->password_mode=='p' || s->password_mode=='c') {
        int mode=s->password_mode;
        if(!strcmp(line,"/CANCEL")) { session_impl_emit(s,"CANCELLED\r\n"); }
        else if(strlen(line)<8 || strlen(line)>128) { session_impl_emit(s,"ERR password 8..128 bytes\r\n"); }
        else if(mode=='o') { strcpy(s->old_password,line); s->password_mode='p'; session_impl_emit(s,"NEW PASSWORD\r\n"); return 1; }
        else if(mode=='p' && strcmp(line,s->old_password)) { strcpy(s->new_password,line); s->password_mode='c'; session_impl_emit(s,"CONFIRM PASSWORD\r\n"); return 1; }
        else if(mode=='c' && !strcmp(line,s->new_password)) {
            BoardRequest q={0}; q.op=s->boardpass_target?'(':s->reset_target?'~':'k';q.number=s->boardpass_target?s->boardpass_target:s->reset_target;q.action=s->boardpass_target?'S':1; strcpy(q.password,s->old_password); strcpy(q.new_password,s->new_password);
            session_impl_submit(s,&q); OPENSSL_cleanse(&q,sizeof(q));
        } else session_impl_emit(s,mode=='c'?"ERR password mismatch\r\n":"ERR password unchanged\r\n");
        s->password_mode=0;s->reset_target=0;s->boardpass_target=0; s->auth_after=s->now+1000;
        OPENSSL_cleanse(s->old_password,sizeof(s->old_password)); OPENSSL_cleanse(s->new_password,sizeof(s->new_password));
        return 1;
    }
    if(s->password_mode) {
        if(s->ui.enabled && !strcmp(line,"/CANCEL")) { s->password_mode=0; session_impl_emit(s,"CANCELLED\r\n"); return 1; }
        BoardRequest q={0}; q.op=(char)s->password_mode; strcpy(q.username,s->username);if(q.op=='g') { strcpy(q.body,s->application.body);application_form_clear(&s->application); }
        s->password_mode=0; s->auth_after=s->now+1000;
        if(strlen(line)<8 || strlen(line)>128) session_impl_emit(s,"ERR password 8..128 bytes\r\n");
        else { strcpy(q.password,line); session_impl_submit(s,&q); }
        OPENSSL_cleanse(q.password,sizeof(q.password)); OPENSSL_cleanse(line,strlen(line)); return 1;
    }

    return 0;
}

int session_impl_password_reset_command(Session *s,char *line) {
    if(!strncmp(line,"RESETPASS ",10)) {
        if(!s->ui.admin) { session_impl_emit(s,"ERR admin required\r\n");return 1; }
        BoardRequest q={0};const char *tail;q.op='~';
        if(!session_impl_number_arg(line+10,&q.number,&tail) || *tail) { session_impl_emit(s,"ERR command\r\n");return 1; }
        if(s->now<s->auth_after) { session_impl_emit(s,"ERR try later\r\n");return 1; }
        session_impl_submit(s,&q);return 1;
    }

    return 0;
}

int session_impl_password_change_command(Session *s,char *line) {
    if(!strcmp(line,"PASSWD")) {
        if(!s->user) { session_impl_emit(s,"ERR login required\r\n"); return 1; }
        if(s->now<s->auth_after) { session_impl_emit(s,"ERR try later\r\n"); return 1; }
        s->password_mode='o'; session_impl_emit(s,"CURRENT PASSWORD\r\n"); return 1;
    }

    return 0;
}

int session_impl_account_command(Session *s,char *line) {
    char out[128];
    if(!strncmp(line,"LOGIN ",6) || !strncmp(line,"REGISTER ",9)) {
        if(!s->member_mode) { session_impl_emit(s,"ERR members disabled\r\n"); return 1; }
        if(s->user) { session_impl_emit(s,"ERR logout first\r\n"); return 1; }
        if(s->now<s->auth_after) { session_impl_emit(s,"ERR try later\r\n"); return 1; }
        const char *name=line+(line[0]=='L'?6:9); size_t n=strlen(name);
        if(!n || n>32) { session_impl_emit(s,"ERR username\r\n"); return 1; }
        for(size_t i=0;i<n;i++) if(!((name[i]>='a'&&name[i]<='z')||(name[i]>='0'&&name[i]<='9')||name[i]=='_'||name[i]=='-')) { session_impl_emit(s,"ERR username\r\n"); return 1; }
        strcpy(s->username,name);
        if(line[0]=='R' && members_detailed_form()) { application_form_begin(&s->application);application_prompt(s);return 1; }
        s->password_mode=line[0]=='L'?'a':'g'; session_impl_emit(s,"PASSWORD\r\n");
    } else if(!strcmp(line,"ACCOUNT")) {
        if(!s->user) session_impl_emit(s,"ERR login required\r\n");
        else { snprintf(out,sizeof(out),"ACCOUNT %u %s\r\n",s->user,s->username); session_impl_emit(s,out); }
    } else if(!strcmp(line,"LOGOUT")) {
        if(preference_exit_begin(&s->preferences,s->user,PREF_EXIT_LOGOUT)) { session_impl_command_core(s,"PREFS SAVE");preference_exit_submitted(&s->preferences,s->pending);return 1; }
        s->preferences.exit_action=PREF_EXIT_NONE;session_impl_selection_reset(s);
        s->session_impl_classic_input=0;s->classic_id=0;s->classic_area=CM_MAIN;check_traversal_reset(&s->check);memset(s->vault_grants,0,sizeof(s->vault_grants));
        chat_disconnect(s->id);s->mail_mode=0;mail_navigation_clear_filter(&s->mail);
        memset(s->board_grants,0,sizeof(s->board_grants));batch_reset(&s->batch);s->game_menu=0;s->vault_mode=0;notification_mail_discard(&s->notifications);s->user=0;s->ui.admin=0;s->ui.sigop=0;s->reset_target=0; s->user_epoch=0; s->current=0; s->board=1; memset(&s->draft,0,sizeof(s->draft)); s->draft_revision=0; session_impl_undo_clear(s); session_impl_emit(s,"LOGGED OUT\r\n");
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
        s->menu_mode=completion->menu_mode;s->menu_shown=0;s->ui.language=(Language)loaded.language;
        s->pager_enabled=loaded.pager;s->editor_line=loaded.editor;s->editor_numbers=(int)loaded.line_numbers;s->transfer_protocol=loaded.transfer_protocol;
        if(completion->columns && completion->rows)session_dimensions(s,loaded.columns,loaded.rows);
    }
    if(completion->auth) { session_impl_selection_reset(s);memset(s->board_grants,0,sizeof(s->board_grants));mail_navigation_clear_filter(&s->mail);notification_mail_reset(&s->notifications);chat_disconnect(s->id);s->user=completion->auth>0?(uint32_t)completion->auth:0; s->user_epoch=completion->user_epoch; s->current=0; s->board=1; s->auth_after=now+1000; }
    if(completion->auth>0)snprintf(s->username,sizeof(s->username),"%s",completion->chat_login);
    if(completion->auth>0)session_mail_notify(s,completion->mail_notify_user,completion->mail_notify_deny,completion->mail_notify_revision);
    if(completion->auth>0)s->member_level=(unsigned)completion->member_level;
    if(completion->auth>0) { s->menu_mode=completion->menu_mode;s->menu_shown=0; }
    if(completion->auth>0 && s->ui.enabled) { session_impl_queue_text(s,login_text(1));time_t greet_now=time(NULL);struct tm *greet_tm=localtime(&greet_now);if(greet_tm)session_impl_queue_text(s,hourly_greeting((unsigned)greet_tm->tm_hour));if(completion->birthday)session_impl_queue_text(s,*login_text(2)?login_text(2):s->ui.language==LANGUAGE_JA?"お誕生日おめでとうございます！\r\n":"Happy birthday!\r\n"); }
    if(completion->auth>0 || completion->preferences) { PreferenceValues values=session_impl_session_preferences(s);preference_observed(&s->preferences,&values); }
    if(completion->auth>0 && board_feature_enabled("chat"))chat_register(s->id,s->user,completion->chat_login,completion->chat_name,session_impl_chat_event,s,&completion->chat_preferences);

    return 0;
}

int session_impl_password_prompt_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='3') { s->boardpass_target=completion->service_number;s->password_mode='p'; }

    return 0;
}

int session_impl_account_flags_result(Session *s,const BoardResult *completion,uint64_t now) {
    (void)now;
    if(completion->service_action=='p') { s->reset_target=completion->service_number;s->password_mode='p'; }
    if(completion->auth)s->ui.admin=completion->auth>0 && completion->chat_admin;
    if(completion->auth>0)chat_admin(s->id,completion->chat_admin);
    if(completion->auth>0 && s->ui.enabled && s->ui.admin)session_impl_queue_text(s,ui_text(&s->ui,MSG_MENU_ADMIN));
    if(completion->password_changed) { s->user_epoch=completion->account_epoch; s->auth_after=now+1000; }

    return 0;
}

static void application_prompt(Session *s) { char text[180];snprintf(text,sizeof(text),s->ui.language==LANGUAGE_JA?"%s（省略はEnter、中止は/CANCEL）: ":"%s (Enter to skip, /CANCEL to abort): ",application_form_field(&s->application));session_impl_queue_text(s,text); }

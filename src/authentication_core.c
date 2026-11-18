#define _POSIX_C_SOURCE 200809L
#include "authentication_core.h"
#include "authentication_repository.h"
#include "application_repository.h"
#include "members.h"
#include "member_core.h"
#include "mail_core.h"
#include "moderation.h"
#include "extras.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/crypto.h>

static int form_valid(const AuthenticationForm *form) {
    if(!form)return 1;
    if(!form->valid || form->count>AUTH_FORM_FIELDS || (form->count && !form->fields))return 0;
    size_t bytes=0;
    for(size_t i=0;i<form->count;i++) {
        const AuthenticationField *f=&form->fields[i];
        if(f->field>AUTH_SYSOP_NOTE || !f->value)return 0;
        size_t n=strlen(f->value);if(n>320 || strchr(f->value,'\n'))return 0;
        const char *key=f->field<PROFILE_FIELDS?profile_key(f->field):f->field==AUTH_FROM_BBS?"FROMBBS":"SYSOPNOTE";
        bytes+=strlen(key)+n+2;if(bytes>AUTH_FORM_BYTES)return 0;
        if(f->field<PROFILE_FIELDS && !profile_text_valid(f->field,f->value))return 0;
    }
    return 1;
}
static int form_save(uint32_t user,const AuthenticationForm *form) {
    if(!form)return 1;
    for(size_t i=0;i<form->count;i++) {
        const AuthenticationField *f=&form->fields[i];
        if(f->field<PROFILE_FIELDS) { if(profile_set(user,f->field,f->value)!=1)return 0; }
        else if(!application_repository_save(user,f->field==AUTH_FROM_BBS?APPLICATION_FROM_BBS:APPLICATION_SYSOP_NOTE,f->value))return 0;
    }
    return 1;
}
static void bootstrap(uint32_t id,AuthenticationSession *out) {
    out->user=id;out->epoch=moderation_epoch(id);
    MailActor mail_actor={id,out->epoch};MailNotice notice;
    if(mail_core_notice(&mail_actor,&notice)==MAIL_CORE_OK) {
        out->mail_unread=notice.unread;out->mail_draft=notice.draft_revision;
    }
    members_mail_notify(id,&out->mail_deny,&out->mail_revision);
    out->admin=moderation_admin(id);members_chat_preferences(id,&out->chat);
    snprintf(out->login,sizeof(out->login),"%s",members_name(id));
    snprintf(out->handle,sizeof(out->handle),"%s",*profile_value(id,0)?profile_value(id,0):members_name(id));
    MemberActor actor={id,out->epoch};MemberMenuMode mode;
    out->menu_mode=member_core_menu_get(&actor,&mode)==MEMBER_CORE_OK?(int)mode:MEMBER_MENU_STANDARD;
    out->preferences=members_preferences(id,&out->language,&out->pager);
    MemberDraft draft={0};
    if(members_draft(id,&draft))out->saved_draft=draft.revision;
    OPENSSL_cleanse(&draft,sizeof(draft));
    authentication_repository_unread(id,&out->unread_articles,&out->unread_boards);
    const char *birth=profile_value(id,8);time_t now=time(NULL);struct tm today;
#ifdef _WIN32
    struct tm *local=localtime(&now);if(local)today=*local;
#else
    struct tm *local=localtime_r(&now,&today);
#endif
    out->birthday=local && strlen(birth)==10 && atoi(birth+5)==today.tm_mon+1 && atoi(birth+8)==today.tm_mday;
}
AuthenticationOutcome authentication_core(const AuthenticationRequest *request,AuthenticationResult *out) {
    if(!out)return AUTH_REJECTED;
    memset(out,0,sizeof(*out));
    if(!authentication_repository_healthy())return AUTH_BOARD_STORAGE;
    if(extras_broken() || profile_store_broken())return AUTH_EXTRAS_STORAGE;
    if(members_broken())return AUTH_MEMBER_STORAGE;
    if(moderation_broken())return AUTH_MODERATION_STORAGE;
    if(!request || (request->mode!=AUTH_LOGIN && request->mode!=AUTH_REGISTER))return AUTH_REJECTED;
    int create=request->mode==AUTH_REGISTER;
    if(create && members_registration_mode()==2)return AUTH_REGISTRATION_CLOSED;
    if(create && !form_valid(request->form))return AUTH_APPLICATION_INVALID;
    if(!request->login || !request->password)return AUTH_REJECTED;
    int id=members_auth(request->login,request->password,create);
    if(!id)return AUTH_REJECTED;
    if(!members_name((uint32_t)id))return AUTH_MEMBER_STORAGE;
    if(create && !form_save((uint32_t)id,request->form)) {
        snprintf(out->account_name,sizeof(out->account_name),"%s",members_name((uint32_t)id));return AUTH_APPLICATION_STORAGE;
    }
    if(create && members_pending((uint32_t)id)) {
        snprintf(out->account_name,sizeof(out->account_name),"%s",members_name((uint32_t)id));out->automatic_id=members_auto_ids();
        return AUTH_APPLICATION_PENDING;
    }
    if(moderation_suspended((uint32_t)id))return AUTH_REJECTED;
    bootstrap((uint32_t)id,&out->session);return AUTHENTICATED;
}

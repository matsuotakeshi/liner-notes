#include "authentication_core.h"
#include "authentication_form.h"
#include "authentication_repository.h"
#include "application_repository.h"
#include "members.h"
#include "member_core.h"
#include "mail_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static int healthy=1,extra_bad,profile_bad,member_bad,moderation_bad,registration,pending,suspended,automatic;
static int auth_id=42,auth_calls,create_seen,save_calls,fail_save,notice_ok=1,menu_ok=1,prefs=1,profile_valid=1;
static unsigned saved_fields[2048];static char saved_values[2048][321];
static const char *keys[]={"HANDLE","BIO","LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT","MACHINE","BIRTHDAY","REALNAME","KANA","ADDRESS","PHONE"};
static char birthday[16],handle[81]="Display";
static AuthenticationRequest request={AUTH_LOGIN,"alice","test-password",NULL};
static void zero(const void *v,size_t n) { const unsigned char *p=v;for(size_t i=0;i<n;i++)assert(!p[i]); }
static void reset(void) {
    healthy=1;extra_bad=profile_bad=member_bad=moderation_bad=registration=pending=suspended=automatic=0;
    auth_id=42;auth_calls=create_seen=save_calls=fail_save=0;notice_ok=menu_ok=prefs=profile_valid=1;
    request=(AuthenticationRequest){AUTH_LOGIN,"alice","test-password",NULL};strcpy(handle,"Display");
    time_t now=time(NULL);struct tm *today=localtime(&now);assert(today);strftime(birthday,sizeof(birthday),"2000-%m-%d",today);
}
int authentication_repository_healthy(void) { return healthy; }
int extras_broken(void) { return extra_bad; }
int profile_store_broken(void) { return profile_bad; }
int members_broken(void) { return member_bad; }
int moderation_broken(void) { return moderation_bad; }
int members_registration_mode(void) { return registration; }
int members_auto_ids(void) { return automatic; }
int members_key_auth(const char *login,const char *proof) { assert(!strcmp(login,"alice") && !strcmp(proof,"test-password"));auth_calls++;return pending?0:auth_id; }
int members_auth(const char *login,const char *password,int create) {
    assert(!strcmp(login,"alice") && !strcmp(password,"test-password"));auth_calls++;create_seen=create;return auth_id;
}
const char *members_name(uint32_t id) { assert(id==42);return automatic?"u000042":"alice"; }
int members_pending(uint32_t id) { assert(id==42);return pending; }
int moderation_suspended(uint32_t id) { assert(id==42);return suspended; }
uint32_t moderation_epoch(uint32_t id) { assert(id==42);return 19; }
int moderation_admin(uint32_t id) { assert(id==42);return 1; }
const char *profile_key(unsigned field) { assert(field<PROFILE_FIELDS);return keys[field]; }
int profile_field(const char *key) { for(int i=0;i<PROFILE_FIELDS;i++)if(!strcmp(keys[i],key))return i;return -1; }
int profile_text_valid(unsigned field,const char *value) { return profile_valid && field<PROFILE_FIELDS && strlen(value)<=(field?320u:80u); }
static int save(uint32_t id,unsigned field,const char *value) {
    assert(id==42 && save_calls<2048);saved_fields[save_calls]=field;strcpy(saved_values[save_calls++],value);
    return !fail_save || save_calls!=fail_save;
}
int profile_set(uint32_t id,unsigned field,const char *value) { return save(id,field,value); }
int application_repository_save(uint32_t id,ApplicationNoteField field,const char *value) { return save(id,AUTH_FROM_BBS+(unsigned)field,value); }
const char *profile_value(uint32_t id,unsigned field) { assert(id==42);return field==8?birthday:handle; }
MailOutcome mail_core_notice(const MailActor *actor,MailNotice *out) {
    assert(actor->user==42 && actor->epoch==19);out->unread=5;out->draft_revision=6;return notice_ok?MAIL_CORE_OK:MAIL_CORE_STORAGE;
}
void members_mail_notify(uint32_t id,int *deny,uint32_t *revision) { assert(id==42);*deny=1;*revision=7; }
void members_chat_preferences(uint32_t id,ChatPreferences *out) { assert(id==42);out->revision=8;out->deny=1;strcpy(out->muted[0],"bob"); }
MemberOutcome member_core_menu_get(const MemberActor *actor,MemberMenuMode *out) {
    assert(actor->user==42 && actor->epoch==19);*out=MEMBER_MENU_STANDARD;return menu_ok?MEMBER_CORE_OK:MEMBER_CORE_STORAGE;
}
int members_preferences(uint32_t id,int *lang,int *pager) { assert(id==42);if(prefs) { *lang=1;*pager=1; }return prefs; }
int members_draft(uint32_t id,MemberDraft *out) { assert(id==42);out->revision=9;strcpy(out->body,"private draft");return 1; }
void authentication_repository_unread(uint32_t id,uint32_t *articles,uint32_t *boards) { assert(id==42);*articles=10;*boards=2; }
static void health_order(void) {
    reset();AuthenticationResult out;memset(&out,0xff,sizeof(out));
    healthy=0;extra_bad=member_bad=moderation_bad=1;
    assert(authentication_core(&request,&out)==AUTH_BOARD_STORAGE);zero(&out,sizeof(out));
    healthy=1;assert(authentication_core(&request,&out)==AUTH_EXTRAS_STORAGE);
    extra_bad=0;profile_bad=1;assert(authentication_core(&request,&out)==AUTH_EXTRAS_STORAGE);
    profile_bad=0;assert(authentication_core(&request,&out)==AUTH_MEMBER_STORAGE);
    member_bad=0;assert(authentication_core(&request,&out)==AUTH_MODERATION_STORAGE);
    assert(!auth_calls && !save_calls);
}
static void rejection_and_policy(void) {
    reset();AuthenticationResult out;auth_id=0;
    assert(authentication_core(&request,&out)==AUTH_REJECTED && auth_calls==1);zero(&out,sizeof(out));
    auth_id=42;suspended=1;assert(authentication_core(&request,&out)==AUTH_REJECTED);zero(&out,sizeof(out));
    reset();request.mode=AUTH_SSH_KEY;assert(authentication_core(&request,&out)==AUTHENTICATED);
    pending=1;assert(authentication_core(&request,&out)==AUTH_REJECTED);zero(&out,sizeof(out));
    pending=0;suspended=1;assert(authentication_core(&request,&out)==AUTH_REJECTED);zero(&out,sizeof(out));
    reset();request.mode=AUTH_REGISTER;registration=2;AuthenticationForm invalid={0};request.form=&invalid;
    assert(authentication_core(&request,&out)==AUTH_REGISTRATION_CLOSED && !auth_calls);
    registration=0;assert(authentication_core(&request,&out)==AUTH_APPLICATION_INVALID && !auth_calls);
    request.mode=AUTH_LOGIN;request.form=&invalid;assert(authentication_core(&request,&out)==AUTHENTICATED && !save_calls);
}
static void decoding_and_validation(void) {
    reset();AuthenticationDecodedForm form;AuthenticationResult out;request.mode=AUTH_REGISTER;request.form=&form.form;
    const char *bad[]={"HANDLE|missing newline","UNKNOWN|value\n","BIO\n","|value\n","BIO|ok\ntrailing"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        authentication_form_decode(bad[i],&form);assert(!form.form.valid);
        assert(authentication_core(&request,&out)==AUTH_APPLICATION_INVALID && !auth_calls);zero(&out,sizeof(out));
    }
    authentication_form_decode("HANDLE|a\nBIO|\nFROMBBS|old|bbs\nSYSOPNOTE|note\nHANDLE|b\n",&form);
    assert(form.form.valid && form.form.count==5);
    assert(authentication_core(&request,&out)==AUTHENTICATED && create_seen && save_calls==5);
    assert(saved_fields[2]==AUTH_FROM_BBS && !strcmp(saved_values[2],"old|bbs") && !strcmp(saved_values[4],"b"));
    reset();request.mode=AUTH_REGISTER;request.form=&form.form;profile_valid=0;
    assert(authentication_core(&request,&out)==AUTH_APPLICATION_INVALID && !auth_calls);
    AuthenticationField bad_field={AUTH_SYSOP_NOTE+1,"x"};AuthenticationForm typed={1,1,&bad_field};request.form=&typed;
    assert(authentication_core(&request,&out)==AUTH_APPLICATION_INVALID);
    bad_field.field=AUTH_FROM_BBS;bad_field.value="two\nlines";assert(authentication_core(&request,&out)==AUTH_APPLICATION_INVALID);
}
static void form_boundary(void) {
    reset();AuthenticationDecodedForm form;AuthenticationResult out;char body[AUTH_FORM_BYTES+2];size_t used=0;
    for(int i=0;i<25;i++) { memcpy(body+used,"BIO|",4);used+=4;memset(body+used,'x',320);used+=320;body[used++]='\n'; }
    memcpy(body+used,"BIO|",4);used+=4;memset(body+used,'x',62);used+=62;body[used++]='\n';body[used]=0;assert(used==8192);
    authentication_form_decode(body,&form);request.mode=AUTH_REGISTER;request.form=&form.form;
    assert(authentication_core(&request,&out)==AUTHENTICATED && save_calls==26);
    body[used++]='x';body[used]=0;authentication_form_decode(body,&form);
    assert(authentication_core(&request,&out)==AUTH_APPLICATION_INVALID);
    char long_value[322];memset(long_value,'a',321);long_value[321]=0;
    AuthenticationField field={AUTH_SYSOP_NOTE,long_value};AuthenticationForm typed={1,1,&field};request.form=&typed;
    assert(authentication_core(&request,&out)==AUTH_APPLICATION_INVALID);
}
static void pending_and_partial_save(void) {
    reset();AuthenticationDecodedForm form;AuthenticationResult out;request.mode=AUTH_REGISTER;request.form=&form.form;
    authentication_form_decode("HANDLE|new\nSYSOPNOTE|private\nBIO|later\n",&form);
    fail_save=2;assert(authentication_core(&request,&out)==AUTH_APPLICATION_STORAGE && save_calls==2);
    assert(!strcmp(out.account_name,"alice"));zero(&out.session,sizeof(out.session));assert(!out.automatic_id);
    fail_save=0;save_calls=0;pending=automatic=1;suspended=1;
    assert(authentication_core(&request,&out)==AUTH_APPLICATION_PENDING && save_calls==3 && out.automatic_id);
    assert(!strcmp(out.account_name,"u000042"));zero(&out.session,sizeof(out.session));
    automatic=0;assert(authentication_core(&request,&out)==AUTH_APPLICATION_PENDING && !out.automatic_id);
}
static void bootstrap_and_cleanup(void) {
    reset();AuthenticationResult out;assert(authentication_core(&request,&out)==AUTHENTICATED);
    AuthenticationSession *s=&out.session;
    assert(s->user==42 && s->epoch==19 && s->admin && !strcmp(s->login,"alice") && !strcmp(s->handle,"Display"));
    assert(s->mail_unread==5 && s->mail_draft==6 && s->mail_deny && s->mail_revision==7);
    assert(s->chat.revision==8 && !strcmp(s->chat.muted[0],"bob") && s->saved_draft==9);
    assert(s->preferences && s->language==1 && s->pager==1 && s->birthday && s->unread_articles==10 && s->unread_boards==2);
    handle[0]=0;birthday[0]=0;notice_ok=menu_ok=prefs=0;
    assert(authentication_core(&request,&out)==AUTHENTICATED && !strcmp(s->handle,"alice"));
    assert(!s->mail_unread && !s->mail_draft && !s->preferences && !s->language && !s->pager && !s->birthday && s->menu_mode==MEMBER_MENU_STANDARD);
    request.password=NULL;assert(authentication_core(&request,&out)==AUTH_REJECTED);zero(&out,sizeof(out));
    assert(authentication_core(NULL,&out)==AUTH_REJECTED);zero(&out,sizeof(out));
    assert(authentication_core(&request,NULL)==AUTH_REJECTED);
}
int main(void) {
    health_order();rejection_and_policy();decoding_and_validation();form_boundary();pending_and_partial_save();bootstrap_and_cleanup();
    puts("PASS: 6 authentication groups (health/admission, policy, forms/bounds, pending/partial save, bootstrap/cleanup)");return 0;
}

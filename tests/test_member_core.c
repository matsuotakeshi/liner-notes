/* Direct account Core tests; persistence and catalog authority are stubbed. */
#include "member_core.h"
#include "member_settings_repository.h"
#include "members.h"
#include "moderation.h"
#include "extras.h"
#include "profile_store.h"
#include "catalog.h"
#include "app_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int settings_ready,settings_get_ok,settings_save_ok,settings_clock_ok;
static unsigned settings_reads,settings_writes,settings_mode,settings_kind;
static uint32_t settings_user;static uint64_t settings_stamp,settings_now;
static char settings_scope[81];
static char saved_ui[32];
int member_settings_repository_ui_get(uint32_t user,char out[32]) { settings_reads++;settings_user=user;strcpy(out,saved_ui);return settings_get_ok; }
int member_settings_repository_ui_save(uint32_t user,const char *name) { settings_user=user;if(!settings_save_ok)return 0;strcpy(saved_ui,name);settings_writes++;return 1; }
int member_settings_repository_ready(void) { return settings_ready; }
int member_settings_repository_now(uint64_t *out) { *out=settings_now;return settings_clock_ok; }
int member_settings_repository_menu_get(uint32_t user,unsigned *out) {
    settings_reads++;settings_user=user;*out=settings_mode;return settings_get_ok;
}
int member_settings_repository_menu_save(uint32_t user,unsigned mode) {
    settings_user=user;if(!settings_save_ok)return 0;settings_mode=mode;settings_writes++;return 1;
}
int member_settings_repository_check_get(uint32_t user,unsigned kind,const char *scope,uint64_t *out) {
    settings_reads++;settings_user=user;settings_kind=kind;strcpy(settings_scope,scope);*out=settings_stamp;return settings_get_ok;
}
int member_settings_repository_check_save(uint32_t user,unsigned kind,const char *scope,uint64_t stamp) {
    settings_user=user;settings_kind=kind;strcpy(settings_scope,scope);if(!settings_save_ok)return 0;
    if(stamp>settings_stamp)settings_stamp=stamp;
    settings_writes++;return 1;
}
int members_rtbbs_policy;
static const char *profile_values[15][PROFILE_BUFFER+1];
static unsigned profile_access[15][PROFILE_FIELDS],profile_read_count,profile_match_count,profile_admin_calls;
static unsigned profile_read_failure;
static int profile_error;
static uint32_t profile_admin_actor;
static char profile_scratch[CONTENT_BODY+2];
static ApplicationNotes application_notes[15];
static unsigned application_reads,application_saves;
static int application_load_ok,application_save_ok;
static uint32_t application_target;
int application_repository_load(uint32_t user,ApplicationNotes *out) {
    assert(user>0 && user<15);application_reads++;application_target=user;
    *out=application_notes[user]; /* May expose a partial result when reporting failure. */
    return application_load_ok;
}
int application_repository_save(uint32_t user,ApplicationNoteField field,const char *value) {
    assert(user>0 && user<15 && field>=APPLICATION_FROM_BBS && field<=APPLICATION_SYSOP_NOTE);
    assert(value && strlen(value)<=320);application_saves++;application_target=user;
    if(!application_save_ok)return 0;
    application_notes[user].present[field]=1;strcpy(application_notes[user].values[field],value);return 1;
}
const char *profile_value(uint32_t owner,unsigned field) {
    assert(owner>0 && owner<15 && field<=PROFILE_BUFFER);
    profile_read_count++;if(field+1==profile_read_failure)profile_error=1;
    if(!profile_values[owner][field])return NULL;
    snprintf(profile_scratch,sizeof(profile_scratch),"%s",profile_values[owner][field]);return profile_scratch;
}
const char *profile_visible(uint32_t owner,uint32_t viewer,unsigned field,char scratch[16]) {
    assert(owner>0 && owner<15 && viewer>0 && viewer<15 && field<PROFILE_FIELDS);
    profile_read_count++;if(field+1==profile_read_failure)profile_error=1;
    if(!members_active(owner) || !members_active(viewer))return "";
    const char *value=profile_values[owner][field];
    if(owner!=viewer && !moderation_admin(viewer)) {
        if(!profile_access[owner][field])return "";
        if(field==8 && profile_access[owner][field]==2 && strlen(value)==10) {
            memcpy(scratch,value+5,6);return scratch;
        }
    }
    snprintf(profile_scratch,sizeof(profile_scratch),"%s",value);
    return profile_scratch; /* Reused on every read, like the store cache. */
}
int profile_match(uint32_t owner,uint32_t viewer,int field,const char *query) {
    profile_match_count++;
    if(!strcmp(query,"*"))return 1;
    for(unsigned f=0;f<PROFILE_FIELDS;f++)if(field<0 || f==(unsigned)field) {
        char scratch[16];const char *v=profile_visible(owner,viewer,f,scratch);
        if(*v && strstr(v,query))return 1;
    }
    return 0;
}
static unsigned profile_sets,profile_modes,profile_writes,profile_field_seen,profile_mode_seen;
static uint32_t profile_owner;
static const char *profile_borrowed;
static int profile_result;
int profile_store_broken(void) { return profile_error; }
int profile_set(uint32_t user,unsigned field,const char *text) {
    assert(members_active(user) && !moderation_suspended(user));
    assert(text && field<=PROFILE_BUFFER);
    profile_sets++;profile_owner=user;profile_field_seen=field;profile_borrowed=text;
    if(profile_result==1)profile_writes++;
    return profile_result;
}
int profile_visibility(uint32_t user,unsigned field,unsigned mode) {
    assert(members_active(user) && !moderation_suspended(user));
    assert(field<PROFILE_FIELDS && mode<=2);
    profile_modes++;profile_owner=user;profile_field_seen=field;profile_mode_seen=mode;
    if(profile_result==1)profile_writes++;
    return profile_result;
}
int profile_admin_set(uint32_t actor,uint32_t target,unsigned field,const char *text) {
    assert(moderation_admin(actor) && members_active(target) && field<PROFILE_FIELDS && text);
    profile_admin_calls++;profile_admin_actor=actor;profile_owner=target;profile_field_seen=field;
    if(profile_result==1) { profile_writes++;profile_values[target][field]=text; }
    return profile_result;
}
uint32_t members_find(const char *name) {
    for(uint32_t id=1;id<15;id++) { const char *value=members_name(id);if(value && !strcmp(value,name))return id; }
    return 0;
}
static uint32_t count,epoch;
static char names[15][34];
static unsigned char present[15],pending[15],suspended[15],retired[15];
static int member_error,moderation_error,admin,read_failure;
static unsigned count_calls,target_reads;
static uint32_t epoch_changes[15],mod_count,extra_count,protected_target;
static uint32_t changed_user,changed_actor;
static unsigned suspend_calls,durable_writes;
static int extras_error,commit_ok,changed_flag;
static unsigned approval_calls,approval_writes,credential_changes;
static unsigned level_calls,level_writes;
static MemberPreferences saved_preferences[15];
static unsigned prefs_reads,prefs_calls,prefs_writes,prefs_user;
static int prefs_read_failure;
int members_preferences(uint32_t user,int *language,int *pager) {
    assert(user && user<15);prefs_reads++;prefs_user=user;
    if(prefs_read_failure==1) { member_error=1;return 0; }
    *language=saved_preferences[user].language;*pager=saved_preferences[user].pager;
    return saved_preferences[user].present;
}
void members_ui_preferences(uint32_t user,int *editor,unsigned *columns,unsigned *rows,unsigned *aux) {
    assert(user && user<15);MemberPreferences *p=&saved_preferences[user];
    *editor=p->editor;*columns=p->columns;*rows=p->rows;*aux=p->aux;
    if(prefs_read_failure==2)member_error=1;
}
int members_save_ui_preferences(uint32_t user,int language,int pager,int editor,unsigned columns,unsigned rows,unsigned aux) {
    assert(user && user<15 && members_active(user));prefs_calls++;prefs_user=user;
    if(language<0 || language>1 || pager<0 || pager>1 || editor<0 || editor>1 || columns<20 || columns>240 || rows<5 || rows>100 || aux>15)return 0;
    MemberPreferences value={1,language,pager,editor,columns,rows,aux};
    if(!memcmp(&saved_preferences[user],&value,sizeof(value)))return 1;
    if(!commit_ok) { member_error=1;return 0; }
    saved_preferences[user]=value;prefs_writes++;return 1;
}
static int notify_deny[15],notify_read_failure;
static uint32_t notify_revision[15];
static unsigned notify_reads,notify_calls,notify_writes;
void members_mail_notify(uint32_t user,int *deny,uint32_t *revision) {
    assert(user && user<15 && members_active(user));notify_reads++;
    *deny=notify_deny[user];*revision=notify_revision[user];
    if(notify_read_failure==1)member_error=1;
    if(notify_read_failure==2)moderation_error=1;
}
int members_set_mail_notify(uint32_t user,int deny) {
    assert(user && user<15 && members_active(user) && (deny==0 || deny==1));notify_calls++;
    if(notify_deny[user]==deny)return 1;
    if(notify_revision[user]==UINT32_MAX)return 0;
    if(!commit_ok) { member_error=1;return 0; }
    notify_deny[user]=deny;notify_revision[user]++;notify_writes++;return 1;
}
static unsigned kick_calls,kick_writes;
static uint32_t kick_read_failure;
int moderation_chat_kick(uint32_t user,uint32_t actor) {
    assert(actor==1 && admin && !moderation_suspended(actor));
    assert(members_name(user) && user!=actor && !moderation_protected(user));kick_calls++;
    if(!commit_ok) { moderation_error=1;return 0; }
    kick_writes++;mod_count++;changed_user=user;changed_actor=actor;return 1;
}
static ChatPreferences chat_saved[15];
static unsigned chat_reads,chat_updates;
static int chat_update_result,chat_read_failure;
static uint32_t chat_owner;
static ChatPreferenceAction chat_action;
static char chat_login[33];
void members_chat_preferences(uint32_t user,ChatPreferences *out) {
    assert(user && user<15);chat_reads++;chat_owner=user;*out=chat_saved[user];
    if(chat_read_failure==1)member_error=1;
    if(chat_read_failure==2)moderation_error=1;
}
int members_chat_update(uint32_t user,ChatPreferenceAction action,const char *login) {
    assert(user && user<15);chat_updates++;chat_owner=user;chat_action=action;
    if(login)strcpy(chat_login,login);else chat_login[0]=0;
    return chat_update_result;
}
static unsigned password_calls,reset_calls,password_writes;
static const char *test_current="test-current-password",*test_new="test-new-password";
int members_password(uint32_t user,const char *current,const char *replacement) {
    assert(members_active(user) && !moderation_suspended(user));password_calls++;
    if(strcmp(current,test_current) || !strcmp(current,replacement))return 0;
    if(!commit_ok) { member_error=1;return -1; }
    password_writes++;changed_user=changed_actor=user;return 1;
}
int members_reset_password(uint32_t user,uint32_t actor,const char *replacement) {
    assert(actor==1 && admin && !moderation_suspended(actor) && user!=actor && !moderation_protected(user));
    assert(strlen(replacement)>=8 && strlen(replacement)<=128);reset_calls++;
    if(!members_active(user))return -1;
    if(!commit_ok) { member_error=1;return -1; }
    password_writes++;changed_user=user;changed_actor=actor;return 1;
}
static unsigned remove_calls,remove_writes,removed_kind;
int members_remove(uint32_t user,uint32_t actor,unsigned kind) {
    assert(members_active(actor) && members_active(user));
    assert(!moderation_protected(user) && ((kind==MEMBER_RETIRE && user==actor) || (actor==1 && admin && user!=actor)));
    remove_calls++;
    if(kind<1 || kind>4 || (kind<=2?!pending[user]:pending[user]))return 0;
    if(!commit_ok) { member_error=1;return -1; }
    retired[user]=(unsigned char)kind;remove_writes++;removed_kind=kind;
    changed_user=user;changed_actor=actor;return 1;
}
static int levels[15];
static uint32_t group_words[15][4];
static unsigned group_calls,group_writes,delegation_checks,group_checks;
static int sigop_enabled,group_enabled;
static unsigned move_calls,move_writes;
static int move_result;
int members_group_move(uint32_t actor,unsigned from,unsigned to) {
    assert(actor==1 && admin && !moderation_suspended(actor));
    assert(from && from<=100 && to && to<=100 && from!=to);move_calls++;
    if(move_result!=1)return move_result;
    if(!commit_ok) { member_error=1;return -1; }
    move_writes++;changed_actor=actor;
    for(uint32_t user=1;user<=count && user<15;user++)if(members_in_group(user,from)) {
        group_words[user][(from-1)/32]&=~(1u<<((from-1)%32));
        group_words[user][(to-1)/32]|=1u<<((to-1)%32);
    }
    return 1;
}
static uint32_t seen_board,seen_user;
int catalog_sigop(uint32_t board,uint32_t user) {
    delegation_checks++;seen_board=board;seen_user=user;
    return sigop_enabled && board==3 && user==2;
}
int catalog_group(uint32_t board,unsigned group) {
    group_checks++;assert(board==3);
    return group_enabled && group==10;
}
int members_group_change(uint32_t user,uint32_t actor,unsigned group,int add) {
    assert(members_active(user) && !moderation_suspended(actor));
    assert((actor==1 && admin) || (actor==2 && sigop_enabled && group_enabled && group==10 && user!=protected_target));
    assert(group && group<=100 && (add==0 || add==1));
    group_calls++;
    if(members_in_group(user,group)==add)return 2;
    if(!commit_ok) { member_error=1;return -1; }
    uint32_t mask=1u<<((group-1)%32);
    if(add)group_words[user][(group-1)/32]|=mask;
    else group_words[user][(group-1)/32]&=~mask;
    group_writes++;changed_user=user;changed_actor=actor;return 1;
}
int members_level(uint32_t user) { return user<15?levels[user]:0; }
uint32_t members_groups(uint32_t user) { return user<15?group_words[user][0]:0; }
int members_in_group(uint32_t user,unsigned group) {
    assert(group && group<=100);
    return user<15 && !!(group_words[user][(group-1)/32] & (1u<<((group-1)%32)));
}
int members_rights(uint32_t user,uint32_t actor,int level,uint32_t groups) {
    assert(actor==1 && admin && !moderation_suspended(actor) && members_active(user));
    assert(level>=0 && level<=5 && groups==group_words[user][0]);
    level_calls++;
    if(levels[user]==level)return 2;
    if(!commit_ok) { member_error=1;return -1; }
    levels[user]=level;group_words[user][0]=groups;level_writes++;
    changed_user=user;changed_actor=actor;return 1;
}
int members_active(uint32_t id) { return id && id<15 && present[id] && !retired[id]; }
int members_approve(uint32_t user,uint32_t actor) {
    assert(actor==1 && admin && !moderation_suspended(actor));
    approval_calls++;
    if(!members_pending(user))return 0;
    if(!commit_ok) { member_error=1;return -1; }
    pending[user]=0;approval_writes++;changed_user=user;changed_actor=actor;return 1;
}
void moderation_credentials_changed(uint32_t user) {
    assert(user && user<15 && (approval_writes || level_writes || group_writes || move_writes || remove_writes || password_writes));
    credential_changes++;epoch_changes[user]++;
}
int extras_broken(void) { return extras_error; }
uint32_t moderation_count(void) { return mod_count; }
uint32_t extras_audit_count(void) { return extra_count; }
int moderation_protected(uint32_t user) { return user==protected_target; }
int moderation_suspend(uint32_t user,uint32_t actor,int suspend) {
    assert(user && user<15 && actor==1 && admin && !moderation_suspended(actor));
    assert(user!=actor && !moderation_protected(user) && (suspend==0 || suspend==1));
    suspend_calls++;
    if(!commit_ok) { moderation_error=1;return 0; }
    if(suspended[user]==suspend)return 1;
    durable_writes++;mod_count++;suspended[user]=(unsigned char)suspend;epoch_changes[user]++;
    changed_user=user;changed_actor=actor;changed_flag=suspend;return 1;
}
int members_broken(void) { return member_error; }
int moderation_broken(void) { return moderation_error; }
uint32_t members_count(void) { count_calls++;return count; }
const char *members_name(uint32_t id) {
    if(id && id==kick_read_failure)member_error=1;
    if(id!=1)target_reads++;
    if(id==MEMBER_MAX && count==MEMBER_MAX)return "last";
    return id && id<15 && present[id]?names[id]:NULL;
}
int members_pending(uint32_t id) {
    if(id==(uint32_t)read_failure)member_error=1;
    return members_active(id)?pending[id]:0;
}
int moderation_suspended(uint32_t id) { return id<15?(suspended[id] || pending[id] || retired[id]):0; }
uint32_t moderation_epoch(uint32_t id) { return epoch+(id<15?epoch_changes[id]:0); }
int moderation_admin(uint32_t id) { return id==1 && admin; }
static MemberActor reset(void) {
    settings_ready=settings_get_ok=settings_save_ok=settings_clock_ok=1;settings_mode=1;
    settings_reads=settings_writes=settings_kind=settings_user=0;settings_stamp=0;settings_now=1000;settings_scope[0]=0;
    for(unsigned u=0;u<15;u++)profile_values[u][PROFILE_BUFFER]="";
    memset(application_notes,0,sizeof(application_notes));application_reads=application_saves=application_target=0;
    application_load_ok=application_save_ok=1;
    for(unsigned u=0;u<15;u++)for(unsigned f=0;f<PROFILE_FIELDS;f++) {
        profile_values[u][f]="";profile_access[u][f]=f<7;
    }
    profile_read_count=profile_match_count=profile_admin_calls=profile_read_failure=profile_admin_actor=0;
    profile_scratch[0]=0;
    members_rtbbs_policy=profile_error=0;profile_result=1;
    profile_sets=profile_modes=profile_writes=profile_field_seen=profile_mode_seen=profile_owner=0;profile_borrowed=NULL;
    memset(notify_deny,0,sizeof(notify_deny));memset(notify_revision,0,sizeof(notify_revision));
    notify_read_failure=0;notify_reads=notify_calls=notify_writes=0;
    memset(chat_saved,0,sizeof(chat_saved));chat_reads=chat_updates=chat_owner=0;
    chat_update_result=1;chat_read_failure=0;chat_action=CHAT_PREF_QUERY;chat_login[0]=0;
    kick_calls=kick_writes=0;kick_read_failure=0;
    count=14;epoch=9;admin=1;member_error=moderation_error=read_failure=0;
    count_calls=target_reads=0;approval_calls=approval_writes=credential_changes=0;
    memset(saved_preferences,0,sizeof(saved_preferences));prefs_reads=prefs_calls=prefs_writes=prefs_user=0;prefs_read_failure=0;
    level_calls=level_writes=0;password_calls=reset_calls=password_writes=0;remove_calls=remove_writes=removed_kind=0;memset(group_words,0,sizeof(group_words));
    move_calls=move_writes=0;move_result=1;
    group_calls=group_writes=delegation_checks=group_checks=0;sigop_enabled=group_enabled=1;seen_board=seen_user=0;
    for(unsigned i=0;i<15;i++)levels[i]=2;
    memset(retired,0,sizeof(retired));
    memset(epoch_changes,0,sizeof(epoch_changes));mod_count=extra_count=0;protected_target=7;
    extras_error=0;commit_ok=1;suspend_calls=durable_writes=0;changed_user=changed_actor=0;changed_flag=-1;
    memset(names,0,sizeof(names));memset(present,1,sizeof(present));present[0]=0;
    memset(pending,0,sizeof(pending));memset(suspended,0,sizeof(suspended));
    for(unsigned i=1;i<15;i++)snprintf(names[i],sizeof(names[i]),"user%u",i);
    present[3]=present[10]=0;pending[5]=1;suspended[6]=1;
    return (MemberActor){1,epoch};
}
static void cleared(const void *data,size_t size) {
    const unsigned char *p=data;for(size_t i=0;i<size;i++)assert(!p[i]);
}
static void role_tests(void) {
    MemberActor actor=reset();MemberRole role;
    assert(member_core_role(NULL,&role)==MEMBER_CORE_OK && role==MEMBER_ROLE_GUEST);
    MemberActor guest={0,99};assert(member_core_role(&guest,&role)==MEMBER_CORE_OK && role==MEMBER_ROLE_GUEST);
    assert(member_core_role(&actor,&role)==MEMBER_CORE_OK && role==MEMBER_ROLE_ADMIN);
    actor.user=2;assert(member_core_role(&actor,&role)==MEMBER_CORE_OK && role==MEMBER_ROLE_MEMBER);
    actor.user=1;admin=0;assert(member_core_role(&actor,&role)==MEMBER_CORE_OK && role==MEMBER_ROLE_MEMBER);
    epoch++;assert(member_core_role(&actor,&role)==MEMBER_CORE_REVOKED && role==MEMBER_ROLE_NONE);
    actor=reset();suspended[1]=1;
    assert(member_core_role(&actor,&role)==MEMBER_CORE_REVOKED && role==MEMBER_ROLE_NONE);
    actor=reset();actor.user=99;
    assert(member_core_role(&actor,&role)==MEMBER_CORE_REVOKED && role==MEMBER_ROLE_NONE);
    puts("PASS: own role, guest behavior, role changes and credential revocation");
}
static void directory_tests(void) {
    MemberActor actor=reset();MemberPage page;MemberSummary member;
    assert(member_core_list(&actor,0,&page)==MEMBER_CORE_OK && page.count==8 && page.next==11);
    const uint32_t ids[]={1,2,4,5,6,7,8,9};
    for(unsigned i=0;i<8;i++)assert(page.rows[i].id==ids[i]);
    assert(page.rows[3].state==MEMBER_STATE_PENDING && page.rows[4].state==MEMBER_STATE_SUSPENDED);
    assert(member_core_list(&actor,page.next,&page)==MEMBER_CORE_OK && page.count==4 && !page.next && page.rows[0].id==11);
    assert(member_core_get(&actor,2,&member)==MEMBER_CORE_OK && !strcmp(member.name,"user2") && member.state==MEMBER_STATE_ACTIVE);
    strcpy(names[2],"changed");assert(!strcmp(member.name,"user2"));
    suspended[5]=1;assert(member_core_get(&actor,5,&member)==MEMBER_CORE_OK && member.state==MEMBER_STATE_PENDING);
    pending[5]=0;assert(member_core_get(&actor,5,&member)==MEMBER_CORE_OK && member.state==MEMBER_STATE_SUSPENDED);
    assert(member_core_get(&actor,0,&member)==MEMBER_CORE_NOT_FOUND);cleared(&member,sizeof(member));
    assert(member_core_get(&actor,3,&member)==MEMBER_CORE_NOT_FOUND);cleared(&member,sizeof(member));
    assert(member_core_list(&actor,UINT32_MAX,&page)==MEMBER_CORE_OK);cleared(&page,sizeof(page));
    count=0;assert(member_core_list(&actor,0,&page)==MEMBER_CORE_OK);cleared(&page,sizeof(page));
    actor=reset();count=MEMBER_MAX;
    assert(member_core_list(&actor,MEMBER_MAX,&page)==MEMBER_CORE_OK && page.count==1 && !page.next && page.rows[0].id==MEMBER_MAX);
    assert(member_core_get(&actor,MEMBER_MAX,&member)==MEMBER_CORE_OK && !strcmp(member.name,"last"));
    puts("PASS: eight-row directory, gaps, inclusive cursors, owned names, status precedence and highest member ID");
}
static void denial_tests(void) {
    MemberPage page;MemberSummary member;
    for(unsigned i=0;i<6;i++) {
        MemberActor actor=reset();MemberOutcome expected=MEMBER_CORE_ADMIN_REQUIRED;
        if(i==0)actor.user=0;
        else if(i==1)actor.user=2;
        else if(i==2)admin=0;
        else if(i==3) { epoch++;expected=MEMBER_CORE_REVOKED; }
        else if(i==4) { suspended[1]=1;expected=MEMBER_CORE_REVOKED; }
        else { actor.user=99;expected=MEMBER_CORE_REVOKED; }
        memset(&page,0x7f,sizeof(page));memset(&member,0x7f,sizeof(member));
        assert(member_core_list(&actor,0,&page)==expected);cleared(&page,sizeof(page));
        assert(member_core_get(&actor,6,&member)==expected);cleared(&member,sizeof(member));
        assert(!count_calls); /* No enumeration before authorization. */
    }
    MemberActor actor=reset();
    assert(member_core_list(&actor,0,&page)==MEMBER_CORE_OK);admin=0;
    assert(member_core_list(&actor,page.next,&page)==MEMBER_CORE_ADMIN_REQUIRED);cleared(&page,sizeof(page));
    assert(member_core_get(NULL,2,&member)==MEMBER_CORE_ADMIN_REQUIRED);cleared(&member,sizeof(member));
    puts("PASS: direct Core admin gate, revoked continuation and no target/status leakage");
}
static void failure_tests(void) {
    MemberPage page;MemberSummary member;MemberRole role;
    for(unsigned i=0;i<2;i++) {
        MemberActor actor=reset();MemberOutcome expected=i?MEMBER_CORE_MODERATION_STORAGE:MEMBER_CORE_STORAGE;
        if(i)moderation_error=1;else member_error=1;
        assert(member_core_role(&actor,&role)==expected && role==MEMBER_ROLE_NONE);
        assert(member_core_list(&actor,0,&page)==expected);cleared(&page,sizeof(page));
        assert(member_core_get(&actor,2,&member)==expected);cleared(&member,sizeof(member));
        assert(!count_calls && !target_reads);
    }
    MemberActor actor=reset();read_failure=4;
    assert(member_core_list(&actor,0,&page)==MEMBER_CORE_STORAGE);cleared(&page,sizeof(page));
    actor=reset();read_failure=4;
    assert(member_core_get(&actor,4,&member)==MEMBER_CORE_STORAGE);cleared(&member,sizeof(member));
    actor=reset();memset(names[4],'x',33);names[4][33]=0;
    assert(member_core_list(&actor,0,&page)==MEMBER_CORE_STORAGE);cleared(&page,sizeof(page));
    assert(member_core_get(&actor,4,&member)==MEMBER_CORE_STORAGE);cleared(&member,sizeof(member));
    names[4][32]=0;assert(member_core_get(&actor,4,&member)==MEMBER_CORE_OK && strlen(member.name)==32);
    count=MEMBER_MAX+1;
    assert(member_core_list(&actor,0,&page)==MEMBER_CORE_STORAGE);cleared(&page,sizeof(page));
    assert(member_core_role(&actor,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_get(&actor,1,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_list(&actor,0,NULL)==MEMBER_CORE_INVALID);
    puts("PASS: broken stores, partial-page clearing, metadata bounds and invalid output contracts");
}
static void suspension_tests(void) {
    MemberActor actor=reset();MemberChange change;MemberRole role;
    MemberActor target={2,epoch};
    assert(member_core_suspend(&actor,2,1,&change)==MEMBER_CORE_OK);
    assert(change.user==2 && change.epoch==10 && suspended[2] && mod_count==1 && durable_writes==1);
    assert(changed_user==2 && changed_actor==1 && changed_flag==1);
    assert(member_core_role(&target,&role)==MEMBER_CORE_REVOKED && role==MEMBER_ROLE_NONE);
    assert(member_core_suspend(&actor,2,1,&change)==MEMBER_CORE_OK && change.epoch==10 && suspend_calls==2 && durable_writes==1);
    assert(member_core_suspend(&actor,2,0,&change)==MEMBER_CORE_OK && change.user==2 && change.epoch==11);
    assert(!suspended[2] && durable_writes==2 && mod_count==2 && changed_flag==0);
    /* A resumed account never revives its old connection epoch. */
    assert(member_core_role(&target,&role)==MEMBER_CORE_REVOKED);
    target.epoch=change.epoch;
    assert(member_core_role(&target,&role)==MEMBER_CORE_OK && role==MEMBER_ROLE_MEMBER);
    assert(member_core_suspend(&actor,2,0,&change)==MEMBER_CORE_OK && change.epoch==11 && durable_writes==2);
    assert(moderation_epoch(actor.user)==actor.epoch);
    /* Approval is separate: resume must not approve a pending application. */
    assert(member_core_suspend(&actor,5,0,&change)==MEMBER_CORE_OK && change.user==5 && change.epoch==9);
    assert(pending[5] && moderation_suspended(5) && durable_writes==2);
    puts("PASS: durable suspension/resume, actor attribution, no-op idempotence and old-session revocation");
}
static void suspension_denials(void) {
    MemberChange change;
    for(unsigned i=0;i<6;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_ADMIN_REQUIRED;
        if(i==0)actor.user=0;
        else if(i==1)actor.user=2;
        else if(i==2)admin=0;
        else if(i==3) { actor.epoch--;want=MEMBER_CORE_REVOKED; }
        else if(i==4) { suspended[1]=1;want=MEMBER_CORE_REVOKED; }
        else { actor.user=99;want=MEMBER_CORE_REVOKED; }
        for(int flag=0;flag<=1;flag++) {
            memset(&change,0x7f,sizeof(change));
            assert(member_core_suspend(&actor,2,flag,&change)==want);cleared(&change,sizeof(change));
            assert(!suspend_calls && !durable_writes && !suspended[2]);
        }
    }
    MemberActor actor=reset();
    for(int flag=0;flag<=1;flag++) {
        assert(member_core_suspend(&actor,1,flag,&change)==MEMBER_CORE_PROTECTED);cleared(&change,sizeof(change));
        assert(member_core_suspend(&actor,7,flag,&change)==MEMBER_CORE_PROTECTED);cleared(&change,sizeof(change));
        assert(member_core_suspend(&actor,0,flag,&change)==MEMBER_CORE_NOT_FOUND);
        assert(member_core_suspend(&actor,3,flag,&change)==MEMBER_CORE_NOT_FOUND);
        assert(member_core_suspend(&actor,UINT32_MAX,flag,&change)==MEMBER_CORE_NOT_FOUND);
        cleared(&change,sizeof(change));
    }
    assert(member_core_suspend(NULL,2,1,&change)==MEMBER_CORE_ADMIN_REQUIRED);
    assert(member_core_suspend(&actor,2,-1,&change)==MEMBER_CORE_INVALID);
    assert(member_core_suspend(&actor,2,2,&change)==MEMBER_CORE_INVALID);cleared(&change,sizeof(change));
    assert(member_core_suspend(&actor,2,1,NULL)==MEMBER_CORE_INVALID);
    assert(!suspend_calls && !durable_writes);
    puts("PASS: suspend/resume direct authorization, self/protected targets and invalid requests");
}
static void suspension_failures(void) {
    MemberChange change;
    int *errors[]={&member_error,&moderation_error,&extras_error};
    MemberOutcome wanted[]={MEMBER_CORE_STORAGE,MEMBER_CORE_MODERATION_STORAGE,MEMBER_CORE_EXTRAS_STORAGE};
    for(unsigned i=0;i<3;i++) {
        MemberActor actor=reset();*errors[i]=1;
        for(int flag=0;flag<=1;flag++) {
            assert(member_core_suspend(&actor,2,flag,&change)==wanted[i]);cleared(&change,sizeof(change));
            assert(!suspend_calls && !durable_writes);
        }
    }
    MemberActor actor=reset();mod_count=UINT32_MAX-1;extra_count=1;
    assert(member_core_suspend(&actor,2,1,&change)==MEMBER_CORE_MODERATION_UNCERTAIN);
    assert(member_core_suspend(&actor,2,0,&change)==MEMBER_CORE_MODERATION_UNCERTAIN);
    assert(!suspend_calls);cleared(&change,sizeof(change));
    mod_count=extra_count=UINT32_MAX;
    assert(member_core_suspend(&actor,2,1,&change)==MEMBER_CORE_MODERATION_UNCERTAIN && !suspend_calls);
    mod_count=UINT32_MAX-2;extra_count=1;
    assert(member_core_suspend(&actor,2,1,&change)==MEMBER_CORE_OK && change.epoch==10 && durable_writes==1);
    assert(member_core_suspend(&actor,2,1,&change)==MEMBER_CORE_MODERATION_UNCERTAIN && suspend_calls==1);
    cleared(&change,sizeof(change));
    actor=reset();commit_ok=0;
    assert(member_core_suspend(&actor,2,1,&change)==MEMBER_CORE_MODERATION_UNCERTAIN);
    cleared(&change,sizeof(change));assert(suspend_calls==1 && !durable_writes && !suspended[2] && moderation_epoch(2)==9);
    assert(member_core_suspend(&actor,2,1,&change)==MEMBER_CORE_MODERATION_STORAGE && suspend_calls==1);
    actor=reset();suspended[2]=1;commit_ok=0;
    assert(member_core_suspend(&actor,2,0,&change)==MEMBER_CORE_MODERATION_UNCERTAIN);
    cleared(&change,sizeof(change));assert(suspended[2] && !durable_writes && moderation_epoch(2)==9);
    puts("PASS: combined audit limit/overflow, failed durable writes and no success invalidation result on error");
}
static void application_pages(void) {
    MemberActor actor=reset();MemberPage page;
    assert(member_core_applications(&actor,0,&page)==MEMBER_CORE_OK);
    assert(page.count==1 && page.rows[0].id==5 && page.rows[0].state==MEMBER_STATE_PENDING && !page.next);
    assert(!strcmp(page.rows[0].name,"user5"));
    for(unsigned i=2;i<15;i++)pending[i]=1;
    assert(member_core_applications(&actor,0,&page)==MEMBER_CORE_OK && page.count==8 && page.next==12);
    const uint32_t ids[]={2,4,5,6,7,8,9,11};
    for(unsigned i=0;i<8;i++)assert(page.rows[i].id==ids[i] && page.rows[i].state==MEMBER_STATE_PENDING);
    assert(member_core_applications(&actor,page.next,&page)==MEMBER_CORE_OK && page.count==3 && !page.next);
    assert(page.rows[0].id==12 && page.rows[2].id==14);
    retired[12]=1;
    assert(member_core_applications(&actor,12,&page)==MEMBER_CORE_OK && page.count==2 && page.rows[0].id==13);
    assert(member_core_applications(&actor,UINT32_MAX,&page)==MEMBER_CORE_OK);cleared(&page,sizeof(page));
    memset(pending,0,sizeof(pending));
    assert(member_core_applications(&actor,0,&page)==MEMBER_CORE_OK);cleared(&page,sizeof(page));
    count=MEMBER_MAX;
    assert(member_core_applications(&actor,MEMBER_MAX,&page)==MEMBER_CORE_OK);cleared(&page,sizeof(page));
    count=MEMBER_MAX+1;
    assert(member_core_applications(&actor,0,&page)==MEMBER_CORE_STORAGE);cleared(&page,sizeof(page));
    actor=reset();read_failure=6;
    assert(member_core_applications(&actor,0,&page)==MEMBER_CORE_STORAGE);cleared(&page,sizeof(page));
    actor=reset();memset(names[5],'x',sizeof(names[5]));
    assert(member_core_applications(&actor,0,&page)==MEMBER_CORE_STORAGE);cleared(&page,sizeof(page));
    assert(!approval_calls && !credential_changes);
    puts("PASS: pending-only application paging, retired IDs, bounds and cleared partial results");
}
static void approval_tests(void) {
    MemberActor actor=reset();MemberChange change;MemberPage page;MemberRole role;
    MemberActor applicant={5,epoch};
    assert(member_core_role(&applicant,&role)==MEMBER_CORE_REVOKED);
    assert(member_core_approve(&actor,5,&change)==MEMBER_CORE_OK);
    assert(change.user==5 && change.epoch==10 && !pending[5]);
    assert(approval_calls==1 && approval_writes==1 && credential_changes==1);
    assert(changed_user==5 && changed_actor==1 && moderation_epoch(1)==9);
    assert(member_core_role(&applicant,&role)==MEMBER_CORE_REVOKED);
    applicant.epoch=change.epoch;
    assert(member_core_role(&applicant,&role)==MEMBER_CORE_OK && role==MEMBER_ROLE_MEMBER);
    assert(member_core_applications(&actor,0,&page)==MEMBER_CORE_OK && !page.count);
    assert(member_core_approve(&actor,5,&change)==MEMBER_CORE_RIGHTS_INVALID);cleared(&change,sizeof(change));
    assert(approval_writes==1 && credential_changes==1 && moderation_epoch(5)==10);
    actor=reset();suspended[5]=1;
    assert(member_core_approve(&actor,5,&change)==MEMBER_CORE_OK && suspended[5]);
    applicant.epoch=change.epoch;
    assert(member_core_role(&applicant,&role)==MEMBER_CORE_REVOKED);
    actor=reset();retired[5]=1;
    assert(member_core_approve(&actor,5,&change)==MEMBER_CORE_NOT_FOUND);
    assert(!approval_calls && pending[5]);cleared(&change,sizeof(change));
    for(uint32_t id=0;id<=3;id+=3) {
        assert(member_core_approve(&actor,id,&change)==MEMBER_CORE_NOT_FOUND);
        cleared(&change,sizeof(change));
    }
    assert(member_core_approve(&actor,UINT32_MAX,&change)==MEMBER_CORE_NOT_FOUND);
    /* Approval and suspension protection are distinct existing policies. */
    actor=reset();pending[7]=1;
    assert(member_core_approve(&actor,7,&change)==MEMBER_CORE_OK && change.user==7);
    puts("PASS: durable approval, attribution, single epoch update, repeat rejection and suspension preservation");
}
static void application_denials(void) {
    MemberPage page;MemberChange change;
    for(unsigned i=0;i<8;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_ADMIN_REQUIRED;
        if(i==0) { actor.user=0;want=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1)actor.user=2;
        else if(i==2)admin=0;
        else if(i==3) { actor.epoch--;want=MEMBER_CORE_REVOKED; }
        else if(i==4) { suspended[1]=1;want=MEMBER_CORE_REVOKED; }
        else if(i==5) { actor.user=99;want=MEMBER_CORE_REVOKED; }
        else if(i==6) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        memset(&page,0x7f,sizeof(page));memset(&change,0x7f,sizeof(change));
        assert(member_core_applications(&actor,0,&page)==want);cleared(&page,sizeof(page));
        assert(member_core_approve(&actor,5,&change)==want);cleared(&change,sizeof(change));
        assert(!approval_calls && !credential_changes && pending[5]);
    }
    MemberActor actor=reset();
    assert(member_core_applications(NULL,0,&page)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_approve(NULL,5,&change)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_applications(&actor,0,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_approve(&actor,5,NULL)==MEMBER_CORE_INVALID);
    assert(!approval_calls);
    commit_ok=0;
    assert(member_core_approve(&actor,5,&change)==MEMBER_CORE_STORAGE);cleared(&change,sizeof(change));
    assert(approval_calls==1 && !approval_writes && !credential_changes && pending[5] && moderation_epoch(5)==9);
    assert(member_core_approve(&actor,5,&change)==MEMBER_CORE_STORAGE && approval_calls==1);
    puts("PASS: application/approval authorization, failed writes and no epoch notification on failure");
}
static void rights_queries(void) {
    MemberActor actor=reset();MemberRights rights;
    uint32_t expected[]={0x80000001u,1u,0x80000000u,8u};
    memcpy(group_words[2],expected,sizeof(expected));
    actor.user=2;
    assert(member_core_rights(&actor,2,&rights)==MEMBER_CORE_OK && rights.user==2 && rights.level==2);
    assert(rights.state==MEMBER_STATE_ACTIVE);
    for(unsigned i=0;i<100;i++)assert(rights.groups[i]==(i==0 || i==31 || i==32 || i==95 || i==99));
    group_words[2][0]=0;assert(rights.groups[0] && rights.groups[31]); /* owned snapshot */
    assert(member_core_rights(&actor,5,&rights)==MEMBER_CORE_ADMIN_REQUIRED);cleared(&rights,sizeof(rights));
    actor.user=1;
    suspended[5]=1;
    assert(member_core_rights(&actor,5,&rights)==MEMBER_CORE_OK && rights.state==MEMBER_STATE_PENDING);
    assert(member_core_rights(&actor,6,&rights)==MEMBER_CORE_OK && rights.state==MEMBER_STATE_SUSPENDED);
    retired[2]=1;
    assert(member_core_rights(&actor,2,&rights)==MEMBER_CORE_NOT_FOUND);cleared(&rights,sizeof(rights));
    for(unsigned i=0;i<2;i++) {
        assert(member_core_rights(&actor,i?UINT32_MAX:0,&rights)==MEMBER_CORE_NOT_FOUND);
        cleared(&rights,sizeof(rights));
    }
    assert(!level_calls && !credential_changes);
    puts("PASS: own/admin rights snapshots, all 100 group positions and pending state precedence");
}
static void rights_failures(void) {
    MemberRights rights;
    for(unsigned i=0;i<7;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_REVOKED;
        if(i==0) { actor.user=0;want=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1)actor.epoch--;
        else if(i==2)suspended[1]=1;
        else if(i==3)actor.user=99;
        else if(i==4) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else if(i==5) { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        else { admin=0;want=MEMBER_CORE_ADMIN_REQUIRED; }
        memset(&rights,0x7f,sizeof(rights));
        assert(member_core_rights(&actor,2,&rights)==want);cleared(&rights,sizeof(rights));
    }
    MemberActor actor=reset();read_failure=2;
    assert(member_core_rights(&actor,2,&rights)==MEMBER_CORE_STORAGE);cleared(&rights,sizeof(rights));
    actor=reset();levels[2]=6;
    assert(member_core_rights(&actor,2,&rights)==MEMBER_CORE_STORAGE);cleared(&rights,sizeof(rights));
    levels[2]=-1;
    assert(member_core_rights(&actor,2,&rights)==MEMBER_CORE_STORAGE);cleared(&rights,sizeof(rights));
    assert(member_core_rights(NULL,2,&rights)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_rights(&actor,2,NULL)==MEMBER_CORE_INVALID);
    assert(!level_calls && !credential_changes);
    puts("PASS: rights access denial and cleared results after store/metadata failures");
}
static void level_updates(void) {
    MemberActor actor=reset();MemberChange change;
    uint32_t original[]={0x80000001u,1u,0x80000000u,8u};
    memcpy(group_words[2],original,sizeof(original));
    for(unsigned level=0;level<=5;level++) {
        assert(member_core_level(&actor,2,level,&change)==MEMBER_CORE_OK && levels[2]==(int)level);
        assert(change.user==2 && change.epoch==10+level && credential_changes==level+1);
        assert(changed_user==2 && changed_actor==1 && !memcmp(group_words[2],original,sizeof(original)));
        assert(member_core_level(&actor,2,level,&change)==MEMBER_CORE_OK);cleared(&change,sizeof(change));
        assert(level_writes==level+1 && credential_changes==level+1);
    }
    assert(moderation_epoch(1)==actor.epoch);
    /* Existing policy permits an admin to update self and protected accounts. */
    assert(member_core_level(&actor,1,4,&change)==MEMBER_CORE_OK && change.user==1);
    assert(member_core_level(&actor,2,0,&change)==MEMBER_CORE_REVOKED);cleared(&change,sizeof(change));
    actor.epoch=moderation_epoch(1);
    assert(member_core_level(&actor,7,4,&change)==MEMBER_CORE_OK && change.user==7);
    suspended[5]=1;
    assert(member_core_level(&actor,5,3,&change)==MEMBER_CORE_OK && pending[5] && suspended[5]);
    puts("PASS: levels 0..5, full group preservation, no-op epochs, self/protected targets and pending state");
}
static void level_denials(void) {
    MemberChange change;
    for(unsigned i=0;i<8;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_ADMIN_REQUIRED;
        if(i==0) { actor.user=0;want=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1)actor.user=2;
        else if(i==2)admin=0;
        else if(i==3) { actor.epoch--;want=MEMBER_CORE_REVOKED; }
        else if(i==4) { suspended[1]=1;want=MEMBER_CORE_REVOKED; }
        else if(i==5) { actor.user=99;want=MEMBER_CORE_REVOKED; }
        else if(i==6) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        memset(&change,0x7f,sizeof(change));
        assert(member_core_level(&actor,2,5,&change)==want);cleared(&change,sizeof(change));
        assert(!level_calls && !credential_changes && levels[2]==2);
    }
    MemberActor actor=reset();
    assert(member_core_level(&actor,2,6,&change)==MEMBER_CORE_RIGHTS_INVALID);
    assert(member_core_level(&actor,2,UINT32_MAX,&change)==MEMBER_CORE_RIGHTS_INVALID);
    assert(member_core_level(&actor,0,5,&change)==MEMBER_CORE_NOT_FOUND);
    assert(member_core_level(&actor,UINT32_MAX,5,&change)==MEMBER_CORE_NOT_FOUND);
    retired[2]=1;
    assert(member_core_level(&actor,2,5,&change)==MEMBER_CORE_NOT_FOUND);
    assert(member_core_level(NULL,5,5,&change)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_level(&actor,5,5,NULL)==MEMBER_CORE_INVALID);
    assert(!level_calls && !credential_changes);cleared(&change,sizeof(change));
    actor=reset();commit_ok=0;
    assert(member_core_level(&actor,2,5,&change)==MEMBER_CORE_STORAGE);cleared(&change,sizeof(change));
    assert(level_calls==1 && !level_writes && !credential_changes && levels[2]==2 && moderation_epoch(2)==9);
    assert(member_core_level(&actor,2,5,&change)==MEMBER_CORE_STORAGE && level_calls==1);
    puts("PASS: level authorization, bounds, retired targets and failed writes without epoch publication");
}
static void group_updates(void) {
    MemberActor actor=reset();MemberChange change;
    unsigned changed=0;
    for(unsigned group=1;group<=100;group++) {
        assert(member_core_group(&actor,0,4,group,1,&change)==MEMBER_CORE_OK);
        changed++;assert(change.user==4 && change.epoch==epoch+changed);
        assert(member_core_group(&actor,0,4,group,1,&change)==MEMBER_CORE_OK);cleared(&change,sizeof(change));
        assert(group_writes==changed && credential_changes==changed);
    }
    for(unsigned group=1;group<=100;group++)assert(members_in_group(4,group));
    for(unsigned group=1;group<=100;group++) {
        assert(member_core_group(&actor,UINT32_MAX,4,group,0,&change)==MEMBER_CORE_OK);
        changed++;assert(change.user==4 && change.epoch==epoch+changed && !members_in_group(4,group));
        assert(member_core_group(&actor,UINT32_MAX,4,group,0,&change)==MEMBER_CORE_OK);cleared(&change,sizeof(change));
        assert(group_writes==changed && credential_changes==changed);
        for(unsigned rest=group+1;rest<=100;rest++)assert(members_in_group(4,rest));
    }
    assert(changed_user==4 && changed_actor==1 && levels[4]==2 && !delegation_checks && !group_checks);
    assert(member_core_group(&actor,0,7,100,1,&change)==MEMBER_CORE_OK); /* protected allowed for admin */
    suspended[5]=1;
    assert(member_core_group(&actor,0,5,33,1,&change)==MEMBER_CORE_OK && pending[5] && suspended[5]);
    puts("PASS: all 100 groups, add/remove no-ops, preserved memberships/state and single epoch updates");
}
static void delegated_groups(void) {
    MemberActor actor=reset();actor.user=2;MemberChange change;
    assert(member_core_group(&actor,3,4,10,1,&change)==MEMBER_CORE_OK);
    assert(seen_board==3 && seen_user==2 && changed_actor==2 && change.user==4 && change.epoch==10);
    assert(member_core_group(&actor,3,4,10,1,&change)==MEMBER_CORE_OK);cleared(&change,sizeof(change));
    assert(group_writes==1 && credential_changes==1);
    assert(member_core_group(&actor,3,4,10,0,&change)==MEMBER_CORE_OK && change.epoch==11);
    for(int add=0;add<=1;add++) {
        assert(member_core_group(&actor,4,4,10,add,&change)==MEMBER_CORE_ADMIN_REQUIRED);
        assert(member_core_group(&actor,0,4,10,add,&change)==MEMBER_CORE_ADMIN_REQUIRED);
        assert(member_core_group(&actor,UINT32_MAX,4,10,add,&change)==MEMBER_CORE_ADMIN_REQUIRED);
        assert(member_core_group(&actor,3,4,11,add,&change)==MEMBER_CORE_ADMIN_REQUIRED);
        assert(member_core_group(&actor,3,7,10,add,&change)==MEMBER_CORE_ADMIN_REQUIRED);
        cleared(&change,sizeof(change));
    }
    assert(group_writes==2);
    sigop_enabled=0;
    assert(member_core_group(&actor,3,4,10,1,&change)==MEMBER_CORE_ADMIN_REQUIRED);
    sigop_enabled=1;group_enabled=0;
    assert(member_core_group(&actor,3,4,10,1,&change)==MEMBER_CORE_ADMIN_REQUIRED);
    group_enabled=1;
    assert(member_core_group(&actor,3,3,10,1,&change)==MEMBER_CORE_NOT_FOUND);
    retired[4]=1;
    assert(member_core_group(&actor,3,4,10,1,&change)==MEMBER_CORE_NOT_FOUND);
    assert(group_writes==2);
    assert(member_core_group(&actor,3,2,10,1,&change)==MEMBER_CORE_OK && change.user==2);
    assert(member_core_group(&actor,3,2,10,0,&change)==MEMBER_CORE_REVOKED);cleared(&change,sizeof(change));
    actor.epoch=moderation_epoch(2);
    assert(member_core_group(&actor,3,2,10,0,&change)==MEMBER_CORE_OK);
    puts("PASS: live SIGOP/ACL rechecks, exact board/group scope, protected targets and self revocation");
}
static void group_failures(void) {
    MemberChange change;
    for(unsigned i=0;i<7;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_REVOKED;
        if(i==0) { actor.user=0;want=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1) { actor.user=4;want=MEMBER_CORE_ADMIN_REQUIRED; }
        else if(i==2)actor.epoch--;
        else if(i==3)suspended[1]=1;
        else if(i==4)actor.user=99;
        else if(i==5) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        for(int add=0;add<=1;add++) {
            memset(&change,0x7f,sizeof(change));
            assert(member_core_group(&actor,3,4,10,add,&change)==want);cleared(&change,sizeof(change));
        }
        assert(!group_calls && !credential_changes);
    }
    MemberActor actor=reset();
    assert(member_core_group(&actor,3,4,0,1,&change)==MEMBER_CORE_RIGHTS_INVALID);
    assert(member_core_group(&actor,3,4,101,1,&change)==MEMBER_CORE_RIGHTS_INVALID);
    assert(member_core_group(&actor,3,4,UINT32_MAX,1,&change)==MEMBER_CORE_RIGHTS_INVALID);
    assert(member_core_group(&actor,3,4,10,-1,&change)==MEMBER_CORE_RIGHTS_INVALID);
    assert(member_core_group(&actor,3,4,10,2,&change)==MEMBER_CORE_RIGHTS_INVALID);
    assert(member_core_group(NULL,3,4,10,1,&change)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_group(&actor,3,4,10,1,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_group(&actor,3,0,10,1,&change)==MEMBER_CORE_NOT_FOUND);
    assert(!group_calls);cleared(&change,sizeof(change));
    for(int add=0;add<=1;add++) {
        actor=reset();commit_ok=0;
        group_words[4][0]=add?0:1u<<9;
        assert(member_core_group(&actor,3,4,10,add,&change)==MEMBER_CORE_STORAGE);cleared(&change,sizeof(change));
        assert(group_calls==1 && !group_writes && !credential_changes && moderation_epoch(4)==9);
        assert(members_in_group(4,10)==!add);
        assert(member_core_group(&actor,3,4,10,add,&change)==MEMBER_CORE_STORAGE && group_calls==1);
    }
    puts("PASS: group authorization, invalid arguments and failed add/remove without epoch publication");
}
static void lifecycle_success(void) {
    for(unsigned kind=1;kind<=4;kind++) {
        MemberActor actor=reset();MemberChange change;MemberRole role;
        uint32_t target=kind<=2?5:4;
        if(kind==MEMBER_RETIRE)actor.user=target;
        /* RETIRE ignores a supplied other-member target, even a protected ID. */
        uint32_t supplied=kind==MEMBER_RETIRE?7:target;
        char original[34];memcpy(original,names[target],sizeof(original));
        uint32_t old_count=count;
        assert(member_core_remove(&actor,supplied,kind,&change)==MEMBER_CORE_OK);
        assert(change.user==target && change.epoch==10 && retired[target]==kind && removed_kind==kind);
        assert(remove_calls==1 && remove_writes==1 && credential_changes==1);
        assert(changed_user==target && changed_actor==actor.user && count==old_count);
        assert(!strcmp(members_name(target),original) && !members_active(target) && !retired[7]);
        assert(levels[target]==2 && members_groups(target)==0);
        MemberActor removed={target,change.epoch};
        assert(member_core_role(&removed,&role)==MEMBER_CORE_REVOKED && role==MEMBER_ROLE_NONE);
        assert(member_core_remove(&actor,supplied,kind,&change)==(kind==MEMBER_RETIRE?MEMBER_CORE_REVOKED:MEMBER_CORE_NOT_FOUND));
        cleared(&change,sizeof(change));assert(remove_writes==1 && credential_changes==1);
    }
    puts("PASS: all four terminal transitions, reserved identity, self-only retirement and single epoch publication");
}
static void lifecycle_denials(void) {
    MemberChange change;
    for(unsigned kind=1;kind<=4;kind++) {
        MemberActor actor=reset();actor.user=0;
        assert(member_core_remove(&actor,5,kind,&change)==(kind==MEMBER_RETIRE?MEMBER_CORE_NOT_FOUND:MEMBER_CORE_ADMIN_REQUIRED));
        assert(member_core_remove(NULL,5,kind,&change)==(kind==MEMBER_RETIRE?MEMBER_CORE_NOT_FOUND:MEMBER_CORE_ADMIN_REQUIRED));
        actor.user=2;
        if(kind!=MEMBER_RETIRE)assert(member_core_remove(&actor,5,kind,&change)==MEMBER_CORE_ADMIN_REQUIRED);
        actor.user=1;actor.epoch--;
        assert(member_core_remove(&actor,5,kind,&change)==MEMBER_CORE_REVOKED);
        actor.epoch=epoch;suspended[1]=1;
        assert(member_core_remove(&actor,5,kind,&change)==MEMBER_CORE_REVOKED);
        assert(!remove_calls);cleared(&change,sizeof(change));
        actor=reset();
        if(kind!=MEMBER_RETIRE) {
            assert(member_core_remove(&actor,1,kind,&change)==MEMBER_CORE_PROTECTED);
            assert(member_core_remove(&actor,7,kind,&change)==MEMBER_CORE_PROTECTED);
            assert(member_core_remove(&actor,0,kind,&change)==MEMBER_CORE_NOT_FOUND);
            assert(member_core_remove(&actor,UINT32_MAX,kind,&change)==MEMBER_CORE_NOT_FOUND);
            retired[5]=1;
            assert(member_core_remove(&actor,5,kind,&change)==MEMBER_CORE_NOT_FOUND);
        } else {
            protected_target=1;
            assert(member_core_remove(&actor,0,kind,&change)==MEMBER_CORE_PROTECTED);
            actor.user=5;
            assert(member_core_remove(&actor,0,kind,&change)==MEMBER_CORE_REVOKED);
        }
        assert(!remove_calls);cleared(&change,sizeof(change));
    }
    MemberActor actor=reset();
    assert(member_core_remove(&actor,4,0,&change)==MEMBER_CORE_STATE);
    assert(member_core_remove(&actor,4,5,&change)==MEMBER_CORE_STATE);
    assert(member_core_remove(&actor,4,UINT32_MAX,&change)==MEMBER_CORE_STATE);
    assert(member_core_remove(&actor,4,MEMBER_DELETE,NULL)==MEMBER_CORE_INVALID);
    assert(!remove_calls);
    for(unsigned kind=1;kind<=2;kind++)assert(member_core_remove(&actor,4,kind,&change)==MEMBER_CORE_STATE);
    assert(member_core_remove(&actor,5,MEMBER_DELETE,&change)==MEMBER_CORE_STATE);
    assert(!remove_writes && !credential_changes && !retired[4] && !retired[5]);cleared(&change,sizeof(change));
    puts("PASS: lifecycle authority, protected/self targets, state mismatches and invalid kinds");
}
static void lifecycle_failures(void) {
    for(unsigned kind=1;kind<=4;kind++) {
        MemberActor actor=reset();MemberChange change;uint32_t target=kind<=2?5:4;
        if(kind==MEMBER_RETIRE)actor.user=target;
        member_error=1;
        assert(member_core_remove(&actor,target,kind,&change)==MEMBER_CORE_STORAGE);
        member_error=0;moderation_error=1;
        assert(member_core_remove(&actor,target,kind,&change)==MEMBER_CORE_MODERATION_STORAGE);
        assert(!remove_calls);moderation_error=0;commit_ok=0;
        memset(&change,0x7f,sizeof(change));
        assert(member_core_remove(&actor,target,kind,&change)==MEMBER_CORE_STORAGE);cleared(&change,sizeof(change));
        assert(remove_calls==1 && !remove_writes && !credential_changes && members_active(target));
        assert(moderation_epoch(target)==9 && pending[target]==(kind<=2));
        assert(member_core_remove(&actor,target,kind,&change)==MEMBER_CORE_STORAGE && remove_calls==1);
    }
    puts("PASS: lifecycle store failures retain state and publish no epoch/change result");
}
static void password_updates(void) {
    MemberActor actor=reset();actor.user=2;MemberChange change;MemberRole role;
    char current[129],replacement[129];strcpy(current,test_current);strcpy(replacement,test_new);
    assert(member_core_password(&actor,current,replacement,&change)==MEMBER_CORE_OK);
    assert(change.user==2 && change.epoch==10 && password_writes==1 && credential_changes==1);
    assert(changed_user==2 && changed_actor==2 && !strcmp(current,test_current) && !strcmp(replacement,test_new));
    assert(member_core_role(&actor,&role)==MEMBER_CORE_REVOKED);
    actor.epoch=change.epoch;
    assert(member_core_role(&actor,&role)==MEMBER_CORE_OK && role==MEMBER_ROLE_MEMBER);
    assert(!reset_calls);
    actor=reset();
    assert(member_core_password(&actor,test_current,"incorrect",&change)==MEMBER_CORE_OK && change.user==1);
    /* Store verifies current secret; Core does not invent a second verifier. */
    actor=reset();
    assert(member_core_password(&actor,"wrong-current",test_new,&change)==MEMBER_CORE_PASSWORD);
    assert(member_core_password(&actor,test_current,test_current,&change)==MEMBER_CORE_PASSWORD);
    cleared(&change,sizeof(change));assert(password_calls==2 && !password_writes && !credential_changes);
    char limit[129];memset(limit,'x',128);limit[128]=0;
    assert(member_core_password(&actor,test_current,limit,&change)==MEMBER_CORE_OK && change.epoch==10);
    actor=reset();
    assert(member_core_password(&actor,test_current,"12345678",&change)==MEMBER_CORE_OK);
    puts("PASS: own password change, borrowed inputs, current/unchanged checks, length boundaries and epochs");
}
static void reset_authority(void) {
    MemberChange change;
    for(unsigned i=0;i<8;i++) {
        MemberActor actor=reset();assert(member_core_reset_check(&actor,4)==MEMBER_CORE_OK);
        MemberOutcome want=MEMBER_CORE_ADMIN_REQUIRED;
        if(i==0)actor.user=0;
        else if(i==1)actor.user=2;
        else if(i==2)admin=0;
        else if(i==3) { actor.epoch--;want=MEMBER_CORE_REVOKED; }
        else if(i==4) { suspended[1]=1;want=MEMBER_CORE_REVOKED; }
        else if(i==5) { protected_target=4;want=MEMBER_CORE_PROTECTED; }
        else if(i==6) { present[4]=0;want=MEMBER_CORE_NOT_FOUND; }
        else { actor.user=99;want=MEMBER_CORE_REVOKED; }
        assert(member_core_reset_check(&actor,4)==want);
        assert(member_core_reset_password(&actor,4,test_new,&change)==want);cleared(&change,sizeof(change));
        assert(!reset_calls && !password_writes && !credential_changes);
    }
    MemberActor actor=reset();
    assert(member_core_reset_check(&actor,1)==MEMBER_CORE_PROTECTED);
    assert(member_core_reset_check(&actor,7)==MEMBER_CORE_PROTECTED);
    assert(member_core_reset_check(&actor,0)==MEMBER_CORE_NOT_FOUND);
    assert(member_core_reset_check(&actor,UINT32_MAX)==MEMBER_CORE_NOT_FOUND);
    assert(member_core_reset_check(NULL,4)==MEMBER_CORE_ADMIN_REQUIRED);
    assert(!reset_calls);
    assert(member_core_reset_password(&actor,4,test_new,&change)==MEMBER_CORE_OK);
    assert(change.user==4 && change.epoch==10 && changed_actor==1 && changed_user==4);
    assert(password_writes==1 && credential_changes==1 && moderation_epoch(1)==9);
    /* Reset accepts the same supplied password: durable salt refresh remains a change. */
    assert(member_core_reset_password(&actor,4,test_new,&change)==MEMBER_CORE_OK && change.epoch==11);
    assert(password_writes==2 && credential_changes==2);
    suspended[5]=1;
    assert(member_core_reset_password(&actor,5,test_new,&change)==MEMBER_CORE_OK && pending[5] && suspended[5]);
    retired[4]=1;
    assert(member_core_reset_check(&actor,4)==MEMBER_CORE_OK); /* existing name-based prompt policy */
    assert(member_core_reset_password(&actor,4,test_new,&change)==MEMBER_CORE_STORAGE);cleared(&change,sizeof(change));
    assert(password_writes==3 && credential_changes==3);
    puts("PASS: reset prompt/commit reauthorization, protected targets, durable attribution and target state preservation");
}
static void password_failures(void) {
    MemberChange change;
    char too_long[130];memset(too_long,'x',129);too_long[129]=0;
    const char *invalid[]={NULL,"","short",too_long};
    MemberActor actor=reset();
    for(unsigned i=0;i<4;i++) {
        assert(member_core_password(&actor,invalid[i],test_new,&change)==MEMBER_CORE_PASSWORD);cleared(&change,sizeof(change));
        assert(member_core_password(&actor,test_current,invalid[i],&change)==MEMBER_CORE_PASSWORD);cleared(&change,sizeof(change));
        assert(member_core_reset_password(&actor,4,invalid[i],&change)==MEMBER_CORE_PASSWORD);cleared(&change,sizeof(change));
    }
    assert(!password_calls && !reset_calls);
    assert(member_core_password(NULL,test_current,test_new,&change)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_password(&actor,test_current,test_new,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_reset_password(&actor,4,test_new,NULL)==MEMBER_CORE_INVALID);
    for(unsigned i=0;i<5;i++) {
        actor=reset();MemberOutcome want=MEMBER_CORE_REVOKED;
        if(i==0) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else if(i==1) { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        else if(i==2)actor.epoch--;
        else if(i==3)retired[1]=1;
        else pending[1]=1;
        assert(member_core_password(&actor,test_current,test_new,&change)==want);cleared(&change,sizeof(change));
        assert(member_core_reset_password(&actor,4,test_new,&change)==want);cleared(&change,sizeof(change));
        assert(!password_calls && !reset_calls);
    }
    for(unsigned reset_mode=0;reset_mode<2;reset_mode++) {
        actor=reset();commit_ok=0;
        MemberOutcome result=reset_mode?member_core_reset_password(&actor,4,test_new,&change):member_core_password(&actor,test_current,test_new,&change);
        assert(result==MEMBER_CORE_STORAGE);cleared(&change,sizeof(change));
        assert(!password_writes && !credential_changes && moderation_epoch(1)==9 && moderation_epoch(4)==9);
    }
    puts("PASS: password null/bounds contracts, revoked actors and failed stores without epoch publication");
}
static void preference_roundtrip(void) {
    MemberActor actor=reset();actor.user=2;MemberPreferences out;
    assert(member_core_preferences(&actor,&out)==MEMBER_CORE_OK);cleared(&out,sizeof(out));
    MemberPreferences input={0,1,1,1,100,35,15};
    assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_OK);
    assert(out.present && out.language==1 && out.pager==1 && out.editor==1 && out.columns==100 && out.rows==35 && out.aux==15);
    assert(prefs_user==2 && prefs_writes==1 && !saved_preferences[1].present && !input.present);
    assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_OK && prefs_writes==1);
    input.language=0;input.pager=0;input.editor=0;input.columns=20;input.rows=5;input.aux=0;
    assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_OK && prefs_writes==2);
    assert(out.columns==20 && out.rows==5 && !out.language && !out.pager && !out.editor && !out.aux);
    input.columns=240;input.rows=100;input.aux=15;
    assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_OK && out.columns==240 && out.rows==100);
    MemberPreferences copy=out;saved_preferences[2].rows=25;assert(out.rows==copy.rows);
    actor.user=4;
    assert(member_core_preferences(&actor,&out)==MEMBER_CORE_OK);cleared(&out,sizeof(out));
    saved_preferences[4]=(MemberPreferences){1,1,0,0,0,0,0};
    assert(member_core_preferences(&actor,&out)==MEMBER_CORE_OK && out.present && out.language==1 && !out.columns && !out.rows);
    assert(!credential_changes && moderation_epoch(2)==9 && moderation_epoch(4)==9);
    puts("PASS: own preference roundtrip, missing/legacy records, full fields, bounds, no-op and session isolation");
}
static void preference_authority(void) {
    MemberPreferences input={0,0,1,0,80,24,0},out;
    for(unsigned i=0;i<7;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_REVOKED;
        if(i==0) { actor.user=0;want=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1)actor.epoch--;
        else if(i==2)suspended[1]=1;
        else if(i==3)pending[1]=1;
        else if(i==4)retired[1]=1;
        else if(i==5) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        memset(&out,0x7f,sizeof(out));
        assert(member_core_preferences(&actor,&out)==want);cleared(&out,sizeof(out));
        assert(member_core_save_preferences(&actor,&input,&out)==want);cleared(&out,sizeof(out));
        assert(!prefs_reads && !prefs_calls && !prefs_writes);
    }
    MemberActor actor=reset();actor.user=99;
    assert(member_core_preferences(&actor,&out)==MEMBER_CORE_REVOKED);
    assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_REVOKED);
    assert(member_core_preferences(NULL,&out)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_save_preferences(NULL,&input,&out)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_preferences(&actor,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_save_preferences(&actor,&input,NULL)==MEMBER_CORE_INVALID);
    assert(!prefs_reads && !prefs_calls);
    puts("PASS: preferences guest/revoked/deleted actor denial without private data access");
}
static void preference_failures(void) {
    MemberActor actor=reset();MemberPreferences out,input={0,0,0,0,80,24,0};
    assert(member_core_save_preferences(&actor,NULL,&out)==MEMBER_CORE_PREFERENCES);cleared(&out,sizeof(out));
    for(int value=-1;value<=2;value+=3) {
        input.language=value;
        assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_PREFERENCES);
        input.language=0;input.pager=value;
        assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_PREFERENCES);
        input.pager=0;
    }
    assert(!prefs_calls);cleared(&out,sizeof(out));
    for(unsigned i=0;i<7;i++) {
        input=(MemberPreferences){0,0,0,0,80,24,0};
        if(i==0)input.editor=-1;else if(i==1)input.editor=2;
        else if(i==2)input.columns=19;else if(i==3)input.columns=241;
        else if(i==4)input.rows=4;else if(i==5)input.rows=101;else input.aux=16;
        assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_STORAGE);cleared(&out,sizeof(out));
    }
    assert(!prefs_writes);
    actor=reset();input=(MemberPreferences){0,1,1,1,80,24,15};commit_ok=0;
    assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_STORAGE);cleared(&out,sizeof(out));
    assert(!saved_preferences[1].present && !prefs_writes && !credential_changes);
    for(int failure=1;failure<=2;failure++) {
        actor=reset();saved_preferences[1]=(MemberPreferences){1,1,1,1,80,24,15};prefs_read_failure=failure;
        assert(member_core_preferences(&actor,&out)==MEMBER_CORE_STORAGE);cleared(&out,sizeof(out));
    }
    actor=reset();prefs_read_failure=2;
    assert(member_core_save_preferences(&actor,&input,&out)==MEMBER_CORE_STORAGE);cleared(&out,sizeof(out));
    assert(prefs_writes==1 && saved_preferences[1].present && !credential_changes);
    puts("PASS: preferences validation, failed writes/reads, cleared output and committed readback failure");
}
static void notification_roundtrip(void) {
    MemberActor actor=reset();actor.user=2;MemberMailNotify out;
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_QUERY,&out)==MEMBER_CORE_OK);
    assert(out.user==2 && !out.deny && !out.revision && !notify_calls);
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_ON,&out)==MEMBER_CORE_OK && !notify_writes);
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_OFF,&out)==MEMBER_CORE_OK);
    assert(out.user==2 && out.deny && out.revision==1 && notify_writes==1);
    MemberMailNotify first=out;
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_OFF,&out)==MEMBER_CORE_OK && out.revision==1 && notify_writes==1);
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_QUERY,&out)==MEMBER_CORE_OK && out.revision==1 && notify_writes==1);
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_ON,&out)==MEMBER_CORE_OK && !out.deny && out.revision==2 && notify_writes==2);
    assert(first.deny && first.revision==1);
    actor.user=4;
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_QUERY,&out)==MEMBER_CORE_OK && out.user==4 && !out.deny && !out.revision);
    assert(!credential_changes && moderation_epoch(2)==9 && moderation_epoch(4)==9);
    puts("PASS: own notification defaults, ON/OFF snapshots, duplicate/query no-write, isolation and stable auth epochs");
}
static void notification_authority(void) {
    for(unsigned i=0;i<8;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_REVOKED;MemberMailNotify out;
        if(i==0) { actor.user=0;want=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1)actor.epoch--;
        else if(i==2)suspended[1]=1;
        else if(i==3)pending[1]=1;
        else if(i==4)retired[1]=1;
        else if(i==5) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else if(i==6) { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        else actor.user=99;
        for(unsigned action=0;action<3;action++) {
            memset(&out,0x7f,sizeof(out));
            assert(member_core_mail_notify(&actor,(MemberNotifyAction)action,&out)==want);cleared(&out,sizeof(out));
        }
        assert(!notify_reads && !notify_calls && !notify_writes);
    }
    MemberActor actor=reset();MemberMailNotify out;
    assert(member_core_mail_notify(NULL,MEMBER_NOTIFY_OFF,&out)==MEMBER_CORE_LOGIN_REQUIRED);cleared(&out,sizeof(out));
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_OFF,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_mail_notify(&actor,(MemberNotifyAction)-1,&out)==MEMBER_CORE_INVALID);cleared(&out,sizeof(out));
    assert(member_core_mail_notify(&actor,(MemberNotifyAction)3,&out)==MEMBER_CORE_INVALID);cleared(&out,sizeof(out));
    assert(!notify_reads && !notify_calls);
    puts("PASS: notification guest/revoked/store-health denial, invalid actions and zeroed outputs");
}
static void notification_failures(void) {
    MemberActor actor=reset();MemberMailNotify out;notify_revision[1]=UINT32_MAX;
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_QUERY,&out)==MEMBER_CORE_OK && out.revision==UINT32_MAX);
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_ON,&out)==MEMBER_CORE_OK && out.revision==UINT32_MAX);
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_OFF,&out)==MEMBER_CORE_STORAGE);cleared(&out,sizeof(out));
    assert(!notify_writes && !notify_deny[1] && notify_revision[1]==UINT32_MAX);
    actor=reset();notify_revision[1]=UINT32_MAX-1;
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_OFF,&out)==MEMBER_CORE_OK && out.deny && out.revision==UINT32_MAX);
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_ON,&out)==MEMBER_CORE_STORAGE);cleared(&out,sizeof(out));
    assert(notify_writes==1 && notify_deny[1]);
    actor=reset();commit_ok=0;
    assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_OFF,&out)==MEMBER_CORE_STORAGE);cleared(&out,sizeof(out));
    assert(!notify_reads && !notify_writes && !notify_deny[1] && !notify_revision[1]);
    for(int failure=1;failure<=2;failure++) {
        actor=reset();notify_deny[1]=1;notify_revision[1]=8;notify_read_failure=failure;
        MemberOutcome want=failure==1?MEMBER_CORE_STORAGE:MEMBER_CORE_MODERATION_STORAGE;
        assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_QUERY,&out)==want);cleared(&out,sizeof(out));
        actor=reset();notify_read_failure=failure;
        assert(member_core_mail_notify(&actor,MEMBER_NOTIFY_OFF,&out)==want);cleared(&out,sizeof(out));
        assert(notify_deny[1] && notify_revision[1]==1 && notify_writes==1 && !credential_changes);
    }
    puts("PASS: notification revision saturation, failed writes/partial reads and no failed completion publication");
}
static void chat_kick_success(void) {
    MemberActor actor=reset();MemberChatKick out;char login[]="user2";
    assert(member_core_chat_kick(&actor,login,&out)==MEMBER_CORE_OK);
    assert(out.user==2 && !strcmp(out.name,"user2") && kick_writes==1 && mod_count==1);
    assert(changed_user==2 && changed_actor==1 && !credential_changes && moderation_epoch(2)==9);
    login[0]='X';assert(!strcmp(out.name,"user2"));
    assert(member_core_chat_kick(&actor,"user2",&out)==MEMBER_CORE_OK && kick_writes==2 && mod_count==2);
    /* An order is audited even without a live connection, and changes no state. */
    for(unsigned target=4;target<=6;target++) {
        if(target==4)retired[target]=1;
        assert(member_core_chat_kick(&actor,names[target],&out)==MEMBER_CORE_OK && out.user==target);
    }
    assert(retired[4] && pending[5] && suspended[6] && !credential_changes);
    memset(names[2],'a',32);names[2][32]=0;
    assert(member_core_chat_kick(&actor,names[2],&out)==MEMBER_CORE_OK && strlen(out.name)==32);
    puts("PASS: chat removal attribution, copied names, repeated audit orders, target states and stable epochs");
}
static void chat_kick_authority(void) {
    for(unsigned i=0;i<8;i++) {
        MemberActor actor=reset();MemberChatKick out;MemberOutcome want=MEMBER_CORE_REVOKED;
        if(i==0) { actor.user=0;want=MEMBER_CORE_ADMIN_REQUIRED; }
        else if(i==1) { actor.user=2;want=MEMBER_CORE_ADMIN_REQUIRED; }
        else if(i==2)actor.epoch--;
        else if(i==3)suspended[1]=1;
        else if(i==4)pending[1]=1;
        else if(i==5)retired[1]=1;
        else if(i==6) { admin=0;want=MEMBER_CORE_ADMIN_REQUIRED; }
        else actor.user=99;
        memset(&out,0x7f,sizeof(out));
        assert(member_core_chat_kick(&actor,"user4",&out)==want);cleared(&out,sizeof(out));
        assert(!count_calls && !kick_calls);
    }
    MemberActor actor=reset();MemberChatKick out;
    assert(member_core_chat_kick(&actor,"user1",&out)==MEMBER_CORE_PROTECTED);cleared(&out,sizeof(out));
    assert(member_core_chat_kick(&actor,"user7",&out)==MEMBER_CORE_PROTECTED);cleared(&out,sizeof(out));
    assert(member_core_chat_kick(&actor,"user3",&out)==MEMBER_CORE_NOT_FOUND);
    assert(member_core_chat_kick(&actor,"User2",&out)==MEMBER_CORE_NOT_FOUND);
    assert(member_core_chat_kick(&actor,"",&out)==MEMBER_CORE_NOT_FOUND);
    char long_name[34];memset(long_name,'a',33);long_name[33]=0;
    assert(member_core_chat_kick(&actor,long_name,&out)==MEMBER_CORE_NOT_FOUND);cleared(&out,sizeof(out));
    assert(member_core_chat_kick(&actor,NULL,&out)==MEMBER_CORE_INVALID);cleared(&out,sizeof(out));
    assert(member_core_chat_kick(NULL,"user2",&out)==MEMBER_CORE_ADMIN_REQUIRED);
    assert(member_core_chat_kick(&actor,"user2",NULL)==MEMBER_CORE_INVALID);
    assert(!kick_calls && !kick_writes);
    puts("PASS: chat removal admin/current-actor gate, self/protected/unknown targets and input boundaries");
}
static void chat_kick_failures(void) {
    for(unsigned i=0;i<8;i++) {
        MemberActor actor=reset();MemberChatKick out;MemberOutcome want=MEMBER_CORE_STORAGE;
        if(i==0)member_error=1;
        else if(i==1) { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        else if(i==2) { extras_error=1;want=MEMBER_CORE_EXTRAS_STORAGE; }
        else if(i==3)count=MEMBER_MAX+1u;
        else if(i==4)kick_read_failure=2;
        else if(i==5) { mod_count=UINT32_MAX;want=MEMBER_CORE_MODERATION_UNCERTAIN; }
        else if(i==6) { mod_count=UINT32_MAX-1;extra_count=2;want=MEMBER_CORE_MODERATION_UNCERTAIN; }
        else { commit_ok=0;want=MEMBER_CORE_MODERATION_UNCERTAIN; }
        memset(&out,0x7f,sizeof(out));
        assert(member_core_chat_kick(&actor,"user2",&out)==want);cleared(&out,sizeof(out));
        assert(kick_calls==(i==7?1u:0u) && !kick_writes && !credential_changes);
    }
    MemberActor actor=reset();MemberChatKick out;mod_count=UINT32_MAX-2;extra_count=1;
    assert(member_core_chat_kick(&actor,"user2",&out)==MEMBER_CORE_OK && kick_writes==1);
    assert(member_core_chat_kick(&actor,"user2",&out)==MEMBER_CORE_MODERATION_UNCERTAIN);cleared(&out,sizeof(out));
    assert(kick_calls==1 && kick_writes==1 && !credential_changes);
    puts("PASS: chat removal failed stores/lookups, combined audit capacity and no failed order publication");
}
static void chat_settings_snapshots(void) {
    MemberActor actor=reset();actor.user=2;ChatPreferences out;
    assert(member_core_chat_preferences(&actor,CHAT_PREF_QUERY,NULL,&out)==MEMBER_CORE_OK);
    cleared(&out,sizeof(out));assert(chat_owner==2 && !chat_updates);
    chat_saved[2].deny=1;chat_saved[2].revision=UINT32_MAX;
    for(unsigned i=0;i<32;i++)snprintf(chat_saved[2].muted[i],33,"muted%u",i);
    assert(member_core_chat_preferences(&actor,CHAT_PREF_QUERY,NULL,&out)==MEMBER_CORE_OK);
    assert(out.deny && out.revision==UINT32_MAX && !chat_updates);
    for(unsigned i=0;i<32;i++)assert(!strcmp(out.muted[i],chat_saved[2].muted[i]));
    chat_saved[2].muted[31][0]='X';assert(!strcmp(out.muted[31],"muted31"));
    for(unsigned a=CHAT_PREF_RECEIVE_ON;a<=CHAT_PREF_UNMUTE;a++) {
        const char *login=a>=CHAT_PREF_MUTE?"user4":NULL;
        assert(member_core_chat_preferences(&actor,(ChatPreferenceAction)a,login,&out)==MEMBER_CORE_OK);
        assert(chat_owner==2 && chat_action==(ChatPreferenceAction)a && !strcmp(chat_login,login?login:""));
        assert(out.revision==UINT32_MAX && out.deny);
    }
    char maximum[33];memset(maximum,'a',32);maximum[32]=0;
    assert(member_core_chat_preferences(&actor,CHAT_PREF_MUTE,maximum,&out)==MEMBER_CORE_OK && !strcmp(chat_login,maximum));
    actor.user=4;
    assert(member_core_chat_preferences(&actor,CHAT_PREF_QUERY,NULL,&out)==MEMBER_CORE_OK);cleared(&out,sizeof(out));
    assert(!credential_changes && moderation_epoch(2)==9);
    puts("PASS: own chat setting snapshots, all 32 slots, structured action/target delegation and stable epochs");
}
static void chat_settings_authority(void) {
    for(unsigned i=0;i<8;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_REVOKED;ChatPreferences out;
        if(i==0) { actor.user=0;want=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1)actor.epoch--;
        else if(i==2)suspended[1]=1;
        else if(i==3)pending[1]=1;
        else if(i==4)retired[1]=1;
        else if(i==5) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else if(i==6) { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        else actor.user=99;
        for(unsigned a=CHAT_PREF_QUERY;a<=CHAT_PREF_UNMUTE;a++) {
            memset(&out,0x7f,sizeof(out));
            assert(member_core_chat_preferences(&actor,(ChatPreferenceAction)a,a>=CHAT_PREF_MUTE?"user4":NULL,&out)==want);
            cleared(&out,sizeof(out));
        }
        assert(!chat_reads && !chat_updates);
    }
    MemberActor actor=reset();ChatPreferences out;char long_name[34];memset(long_name,'x',33);long_name[33]=0;
    assert(member_core_chat_preferences(NULL,CHAT_PREF_QUERY,NULL,&out)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_chat_preferences(&actor,CHAT_PREF_QUERY,NULL,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_chat_preferences(&actor,(ChatPreferenceAction)-1,NULL,&out)==MEMBER_CORE_INVALID);
    assert(member_core_chat_preferences(&actor,(ChatPreferenceAction)5,NULL,&out)==MEMBER_CORE_INVALID);
    assert(member_core_chat_preferences(&actor,CHAT_PREF_QUERY,"user4",&out)==MEMBER_CORE_INVALID);
    assert(member_core_chat_preferences(&actor,CHAT_PREF_RECEIVE_ON,"user4",&out)==MEMBER_CORE_INVALID);
    assert(member_core_chat_preferences(&actor,CHAT_PREF_MUTE,NULL,&out)==MEMBER_CORE_INVALID);
    assert(member_core_chat_preferences(&actor,CHAT_PREF_MUTE,"",&out)==MEMBER_CORE_INVALID);
    assert(member_core_chat_preferences(&actor,CHAT_PREF_UNMUTE,long_name,&out)==MEMBER_CORE_INVALID);cleared(&out,sizeof(out));
    assert(!chat_reads && !chat_updates);
    puts("PASS: chat settings owner/current-actor checks, enum/target bounds and no unauthorized store access");
}
static void chat_settings_failures(void) {
    for(int store_result=-1;store_result<=0;store_result++) {
        MemberActor actor=reset();ChatPreferences out;chat_update_result=store_result;
        memset(&out,0x7f,sizeof(out));
        assert(member_core_chat_preferences(&actor,CHAT_PREF_MUTE,"user4",&out)==(store_result<0?MEMBER_CORE_STORAGE:MEMBER_CORE_INVALID));
        cleared(&out,sizeof(out));assert(chat_updates==1 && !chat_reads && !credential_changes);
    }
    for(int failure=1;failure<=2;failure++)for(unsigned a=0;a<2;a++) {
        MemberActor actor=reset();ChatPreferences out;chat_read_failure=failure;
        chat_saved[1].deny=1;chat_saved[1].revision=7;strcpy(chat_saved[1].muted[0],"private-target");
        assert(member_core_chat_preferences(&actor,a?CHAT_PREF_RECEIVE_OFF:CHAT_PREF_QUERY,NULL,&out)==
               (failure==1?MEMBER_CORE_STORAGE:MEMBER_CORE_MODERATION_STORAGE));
        cleared(&out,sizeof(out));assert(chat_updates==a && !credential_changes);
    }
    puts("PASS: rejected/failed chat updates and partial-read clearing without successful publication");
}
static void group_move_success(void) {
    for(unsigned from=1;from<=100;from++) {
        MemberActor actor=reset();MemberGroupMove out;
        unsigned to=from==100?1:from+1,other=to==100?1:to+1;
        retired[8]=1;
        for(unsigned user=1;user<15;user++) {
            group_words[user][(other-1)/32]|=1u<<((other-1)%32);
            if(user!=9)group_words[user][(from-1)/32]|=1u<<((from-1)%32);
        }
        assert(member_core_group_move(&actor,from,to,&out)==MEMBER_CORE_OK);
        assert(out.through==14 && out.actor_epoch==10 && move_writes==1 && changed_actor==1);
        assert(credential_changes==12 && !delegation_checks && !group_checks);
        for(unsigned user=1;user<15;user++) {
            assert(!members_in_group(user,from) && members_in_group(user,other));
            assert(members_in_group(user,to)==(user!=9));
            assert(epoch_changes[user]==present[user]);
            assert(levels[user]==2);
        }
        assert(pending[5] && suspended[6] && retired[8] && protected_target==7);
        assert(member_core_group_move(&actor,from,to,&out)==MEMBER_CORE_REVOKED);
        cleared(&out,sizeof(out));assert(move_writes==1);
        actor.epoch=moderation_epoch(actor.user);
        assert(member_core_group_move(&actor,from,to,&out)==MEMBER_CORE_OK);
        assert(out.actor_epoch==11 && move_writes==2 && credential_changes==24);
    }
    puts("PASS: bulk migration across all group words, account states, epochs and empty-source repetition");
}
static void group_move_denials(void) {
    MemberActor actor=reset();MemberGroupMove out;
    assert(member_core_group_move(NULL,100,99,&out)==MEMBER_CORE_LOGIN_REQUIRED);
    cleared(&out,sizeof(out));assert(!move_calls && !count_calls);
    actor.user=2;assert(member_core_group_move(&actor,100,99,&out)==MEMBER_CORE_ADMIN_REQUIRED);
    actor.user=1;admin=0;assert(member_core_group_move(&actor,100,99,&out)==MEMBER_CORE_ADMIN_REQUIRED);
    assert(!move_calls && !count_calls && !delegation_checks);
    for(unsigned state=0;state<5;state++) {
        actor=reset();
        if(state==0)actor.epoch--;
        if(state==1)suspended[1]=1;
        if(state==2)pending[1]=1;
        if(state==3)retired[1]=1;
        if(state==4)present[1]=0;
        memset(&out,0x7f,sizeof(out));
        assert(member_core_group_move(&actor,100,99,&out)==MEMBER_CORE_REVOKED);
        cleared(&out,sizeof(out));assert(!move_calls && !count_calls && !credential_changes);
    }
    actor=reset();
    const unsigned invalid[][2]={{0,1},{1,0},{101,1},{1,101},{100,100},{UINT32_MAX,1}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        assert(member_core_group_move(&actor,invalid[i][0],invalid[i][1],&out)==MEMBER_CORE_RIGHTS_INVALID);
        cleared(&out,sizeof(out));
    }
    assert(member_core_group_move(&actor,100,99,NULL)==MEMBER_CORE_INVALID);
    assert(!move_calls && !count_calls && !credential_changes);
    puts("PASS: bulk migration requires current global admin and distinct valid groups");
}
static void group_move_failures(void) {
    for(unsigned failure=0;failure<6;failure++) {
        MemberActor actor=reset();MemberGroupMove out;MemberOutcome expected=MEMBER_CORE_STORAGE;
        group_words[2][3]=8;
        if(failure==0)member_error=1;
        if(failure==1) { moderation_error=1;expected=MEMBER_CORE_MODERATION_STORAGE; }
        if(failure==2)count=MEMBER_MAX+1;
        if(failure==3)commit_ok=0;
        if(failure==4) { move_result=0;expected=MEMBER_CORE_RIGHTS_INVALID; }
        if(failure==5)move_result=-1;
        memset(&out,0x7f,sizeof(out));
        assert(member_core_group_move(&actor,100,99,&out)==expected);
        cleared(&out,sizeof(out));assert(!move_writes && !credential_changes && group_words[2][3]==8);
        assert(move_calls==(failure>=3));
    }
    puts("PASS: failed bulk writes/storage bounds publish no epoch or notification result");
}
static void profile_owned_updates(void) {
    MemberActor actor=reset();actor.user=2;
    const char *texts[]={"", "private text", "line one\r\nline two\tend", "2000-02-29"};
    for(unsigned field=0;field<=PROFILE_BUFFER;field++)for(unsigned t=0;t<4;t++) {
        assert(member_core_profile_set(&actor,field,texts[t])==MEMBER_CORE_OK);
        assert(profile_owner==2 && profile_field_seen==field && profile_borrowed==texts[t]);
    }
    assert(profile_sets==56 && profile_writes==56 && !credential_changes);
    for(unsigned field=0;field<PROFILE_FIELDS;field++)for(unsigned mode=0;mode<3;mode++) {
        unsigned before=profile_modes;
        int allowed=mode==1 || (mode==0 && field!=0) || (mode==2 && field==8);
        assert(member_core_profile_visibility(&actor,field,mode)==(allowed?MEMBER_CORE_OK:MEMBER_CORE_PROFILE_TEXT));
        assert(profile_modes==before+(unsigned)allowed);
        if(allowed)assert(profile_owner==2 && profile_field_seen==field && profile_mode_seen==mode);
    }
    assert(!credential_changes && !delegation_checks);
    puts("PASS: profile owner-only structured fields, borrowed text delegation and privacy mode matrix");
}
static void profile_authority(void) {
    MemberActor actor=reset();actor.user=2;members_rtbbs_policy=1;
    for(unsigned field=0;field<=PROFILE_BUFFER;field++) {
        unsigned before=profile_sets;
        int restricted=field==8 || field==11;
        assert(member_core_profile_set(&actor,field,"")==
               (restricted?MEMBER_CORE_PROFILE_ADMINISTRATOR:MEMBER_CORE_OK));
        assert(profile_sets==before+!restricted);
    }
    /* Compatibility restricts content edits, not the owner's visibility controls. */
    assert(member_core_profile_visibility(&actor,8,2)==MEMBER_CORE_OK);
    assert(member_core_profile_visibility(&actor,11,0)==MEMBER_CORE_OK);
    members_rtbbs_policy=0;
    assert(member_core_profile_set(&actor,11,"own address")==MEMBER_CORE_OK);
    members_rtbbs_policy=1;actor.user=1;
    assert(member_core_profile_set(&actor,8,"2000-02-29")==MEMBER_CORE_OK);
    admin=0;
    assert(member_core_profile_set(&actor,8,"2000-02-29")==MEMBER_CORE_PROFILE_ADMINISTRATOR);
    for(unsigned state=0;state<7;state++) {
        actor=reset();
        if(state==0)actor.user=0;
        if(state==1)actor.epoch--;
        if(state==2)suspended[1]=1;
        if(state==3)pending[1]=1;
        if(state==4)retired[1]=1;
        if(state==5)present[1]=0;
        const MemberActor *input=state==6?NULL:&actor;
        MemberOutcome expected=(state==0 || state==6)?MEMBER_CORE_LOGIN_REQUIRED:MEMBER_CORE_REVOKED;
        assert(member_core_profile_set(input,7,"private")==expected);
        assert(member_core_profile_visibility(input,7,1)==expected);
        assert(!profile_sets && !profile_modes && !profile_writes && !credential_changes);
    }
    puts("PASS: live profile policy/admin changes, guest/stale/retired actor denial and no SIGOP bypass");
}
static void profile_failures(void) {
    MemberActor actor=reset();
    assert(member_core_profile_set(&actor,0,NULL)==MEMBER_CORE_PROFILE_TEXT);
    assert(member_core_profile_set(&actor,PROFILE_BUFFER+1,"")==MEMBER_CORE_PROFILE_TEXT);
    assert(member_core_profile_set(&actor,UINT32_MAX,"")==MEMBER_CORE_PROFILE_TEXT);
    assert(member_core_profile_visibility(&actor,PROFILE_BUFFER,1)==MEMBER_CORE_PROFILE_TEXT);
    assert(member_core_profile_visibility(&actor,UINT32_MAX,1)==MEMBER_CORE_PROFILE_TEXT);
    assert(member_core_profile_visibility(&actor,7,3)==MEMBER_CORE_PROFILE_TEXT);
    assert(member_core_profile_visibility(&actor,8,UINT32_MAX)==MEMBER_CORE_PROFILE_TEXT);
    assert(!profile_sets && !profile_modes);
    for(int failure=0;failure<4;failure++) {
        actor=reset();MemberOutcome expected=MEMBER_CORE_EXTRAS_STORAGE;
        if(failure==0) { member_error=1;expected=MEMBER_CORE_STORAGE; }
        if(failure==1) { moderation_error=1;expected=MEMBER_CORE_MODERATION_STORAGE; }
        if(failure==2)extras_error=1;
        if(failure==3)profile_error=1;
        assert(member_core_profile_set(&actor,7,"private")==expected);
        assert(member_core_profile_visibility(&actor,7,1)==expected);
        assert(!profile_sets && !profile_modes && !profile_writes && !credential_changes);
    }
    for(int saved=-1;saved<=0;saved++) {
        actor=reset();profile_result=saved;
        MemberOutcome expected=saved<0?MEMBER_CORE_PROFILE_TEXT:MEMBER_CORE_EXTRAS_STORAGE;
        assert(member_core_profile_set(&actor,7,"rejected or failed")==expected);
        assert(member_core_profile_visibility(&actor,7,1)==expected);
        assert(profile_sets==1 && profile_modes==1 && !profile_writes && !credential_changes);
    }
    puts("PASS: profile invalid inputs, broken stores and persistence failures without successful completion");
}
static void profile_snapshots(void) {
    MemberActor actor=reset();actor.user=2;MemberProfile out;
    static char long_bio[8193];memset(long_bio,'x',8192);long_bio[4]='\n';long_bio[8192]=0;
    profile_values[4][0]="Handle";profile_values[4][1]=long_bio;
    profile_values[4][8]="1968-02-29";profile_values[4][9]="hidden name";profile_access[4][8]=2;
    assert(member_core_profile_get(&actor,4,NULL,&out)==MEMBER_CORE_OK);
    assert(out.user==4 && !strcmp(out.name,"user4") && !strcmp(out.handle,"Handle"));
    assert(!strcmp(out.bio,long_bio) && !strcmp(member_profile_value(&out,8),"02-29"));
    assert(!*member_profile_value(&out,9));
    memset(profile_scratch,'z',100);profile_scratch[100]=0;
    assert(!strcmp(out.handle,"Handle") && !strcmp(out.bio,long_bio));
    assert(!*member_profile_value(NULL,0) && !*member_profile_value(&out,PROFILE_FIELDS));
    actor.user=4;assert(member_core_profile_get(&actor,0,NULL,&out)==MEMBER_CORE_OK);
    assert(!strcmp(member_profile_value(&out,9),"hidden name"));
    actor.user=1;assert(member_core_profile_get(&actor,0,"user4",&out)==MEMBER_CORE_OK);
    assert(!strcmp(member_profile_value(&out,8),"1968-02-29"));
    assert(member_core_profile_get(&actor,0,"User4",&out)==MEMBER_CORE_PROFILE_UNAVAILABLE);cleared(&out,sizeof(out));
    for(uint32_t target=3;target<=6;target++)if(target!=4) {
        assert(member_core_profile_get(&actor,target,NULL,&out)==MEMBER_CORE_PROFILE_UNAVAILABLE);cleared(&out,sizeof(out));
    }
    retired[4]=1;assert(member_core_profile_get(&actor,4,NULL,&out)==MEMBER_CORE_PROFILE_UNAVAILABLE);
    cleared(&out,sizeof(out));assert(!profile_writes && !credential_changes);
    puts("PASS: owned visible profile snapshots, long biography, exact lookup, owner/admin and unavailable targets");
}
static void profile_search_pages(void) {
    MemberActor actor=reset();actor.user=2;MemberProfilePage page;
    MemberProfileQuery q={MEMBER_PROFILE_ALL,0,0,0,8,"*"};
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK);
    const uint32_t expected[]={1,2,4,7,8,9,11,12};
    assert(page.count==8 && page.next==13);
    for(unsigned i=0;i<8;i++)assert(page.rows[i].user==expected[i]);
    q.start=page.next;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK);
    assert(page.count==2 && page.rows[0].user==13 && page.rows[1].user==14 && !page.next);
    q.start=UINT32_MAX;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK);
    assert(!page.count && !page.next);
    q.start=0;q.limit=1;q.text="user4";
    profile_values[4][0]="public handle";
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK);
    assert(page.count==1 && page.rows[0].user==4 && page.next==5 && !strcmp(page.rows[0].handle,"public handle"));
    memset(profile_scratch,'z',20);profile_scratch[20]=0;assert(!strcmp(page.rows[0].handle,"public handle"));
    q.start=5;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && !page.count && !page.next);
    q.start=0;q.text="private-needle";profile_values[4][9]="private-needle";
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && !page.count);
    q.scope=MEMBER_PROFILE_FIELD;q.field=9;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && !page.count);
    profile_access[4][9]=1;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && page.count==1 && page.rows[0].user==4);
    profile_access[4][9]=0;actor.user=1;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && page.count==1);
    actor.user=2;q.field=8;q.text="1968";profile_values[4][8]="1968-02-29";profile_access[4][8]=2;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && !page.count);
    q.text="02-29";assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && page.count==1);
    q.text="*";q.field=9; /* Existing field wildcard enumerates eligible accounts, not secret content. */
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && page.count==1 && page.rows[0].user==1);
    puts("PASS: profile paging, name/field search, ownership and month/day privacy without hidden matching values");
}
static void profile_search_policy(void) {
    MemberActor actor=reset();actor.user=4;members_rtbbs_policy=1;MemberProfilePage page;
    MemberProfileQuery q={MEMBER_PROFILE_GROUP,0,100,0,8,"*"};
    group_words[7][3]=8;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_SEARCH_DENIED);cleared(&page,sizeof(page));
    actor.user=2;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK);
    assert(page.count==1 && page.rows[0].user==7);
    sigop_enabled=0;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_SEARCH_DENIED);cleared(&page,sizeof(page));
    sigop_enabled=1;q.scope=MEMBER_PROFILE_FIELD;
    for(q.field=9;q.field<PROFILE_FIELDS;q.field++) {
        assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_SEARCH_DENIED);cleared(&page,sizeof(page));
    }
    actor.user=1;q.field=9;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK && page.count==8);
    admin=0;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_SEARCH_DENIED);
    members_rtbbs_policy=0;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_OK);
    assert(!profile_writes && !credential_changes);
    puts("PASS: live compatibility search permissions, global SIGOP group access and no sensitive-field delegation");
}
static void profile_service_failures(void) {
    MemberProfile out;MemberProfilePage page;MemberProfileQuery q={MEMBER_PROFILE_ALL,0,0,0,8,"*"};
    for(unsigned failure=0;failure<8;failure++) {
        MemberActor actor=reset();MemberOutcome expected=MEMBER_CORE_REVOKED;
        if(failure==0) { actor.user=0;expected=MEMBER_CORE_LOGIN_REQUIRED; }
        if(failure==1)actor.epoch--;
        if(failure==2)retired[1]=1;
        if(failure==3) { member_error=1;expected=MEMBER_CORE_STORAGE; }
        if(failure==4) { moderation_error=1;expected=MEMBER_CORE_MODERATION_STORAGE; }
        if(failure==5) { extras_error=1;expected=MEMBER_CORE_EXTRAS_STORAGE; }
        if(failure==6) { profile_error=1;expected=MEMBER_CORE_EXTRAS_STORAGE; }
        if(failure==7)pending[1]=1;
        memset(&out,0x7f,sizeof(out));memset(&page,0x7f,sizeof(page));
        assert(member_core_profile_get(&actor,4,NULL,&out)==expected);
        assert(member_core_profile_search(&actor,&q,&page)==expected);
        assert(member_core_profile_admin_set(&actor,4,7,"x")==expected);
        cleared(&out,sizeof(out));cleared(&page,sizeof(page));assert(!profile_read_count && !profile_admin_calls);
    }
    MemberActor actor=reset();char long_query[82];memset(long_query,'x',81);long_query[81]=0;
    const char *bad[]={NULL,"","control\n",long_query};
    for(unsigned i=0;i<4;i++) { q.text=bad[i];assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_TEXT);cleared(&page,sizeof(page)); }
    q.text="*";q.limit=9;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_TEXT);
    q.limit=0;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_TEXT);
    q.limit=8;q.scope=MEMBER_PROFILE_FIELD;q.field=PROFILE_FIELDS;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_TEXT);
    q.scope=MEMBER_PROFILE_GROUP;q.group=101;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_TEXT);
    q.group=0;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_TEXT);
    q.scope=(MemberProfileScope)99;assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_PROFILE_TEXT);
    assert(member_core_profile_search(&actor,NULL,&page)==MEMBER_CORE_PROFILE_TEXT);
    assert(member_core_profile_search(&actor,&q,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_profile_get(&actor,4,NULL,NULL)==MEMBER_CORE_INVALID);
    assert(member_core_profile_get(&actor,4,"user4",&out)==MEMBER_CORE_PROFILE_UNAVAILABLE);cleared(&out,sizeof(out));
    assert(member_core_profile_get(&actor,0,"",&out)==MEMBER_CORE_PROFILE_UNAVAILABLE);
    assert(member_core_profile_get(&actor,0,long_query,&out)==MEMBER_CORE_PROFILE_UNAVAILABLE);
    q.scope=MEMBER_PROFILE_ALL;count=MEMBER_MAX+1;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_STORAGE);cleared(&page,sizeof(page));
    actor=reset();profile_read_failure=1;
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_EXTRAS_STORAGE);cleared(&page,sizeof(page));
    actor=reset();profile_read_failure=13;profile_values[4][9]="secret";
    assert(member_core_profile_get(&actor,4,NULL,&out)==MEMBER_CORE_EXTRAS_STORAGE);cleared(&out,sizeof(out));
    actor=reset();profile_values[4][0]=long_query;
    assert(member_core_profile_get(&actor,4,NULL,&out)==MEMBER_CORE_EXTRAS_STORAGE);cleared(&out,sizeof(out));
    assert(member_core_profile_search(&actor,&q,&page)==MEMBER_CORE_EXTRAS_STORAGE);cleared(&page,sizeof(page));
    assert(!profile_writes && !credential_changes);
    puts("PASS: profile service input/store bounds, revoked actors and whole-output clearing after partial reads");
}
static void profile_admin_edits(void) {
    MemberActor actor=reset();members_rtbbs_policy=1;
    const uint32_t targets[]={1,2,5,6,7};
    for(unsigned i=0;i<5;i++)for(unsigned field=0;field<PROFILE_FIELDS;field++) {
        unsigned mode=profile_access[targets[i]][field];
        assert(member_core_profile_admin_set(&actor,targets[i],field,"replacement")==MEMBER_CORE_OK);
        assert(profile_admin_actor==1 && profile_owner==targets[i] && profile_field_seen==field);
        assert(!strcmp(profile_values[targets[i]][field],"replacement") && profile_access[targets[i]][field]==mode);
    }
    assert(profile_writes==65 && profile_admin_calls==65 && pending[5] && suspended[6] && !credential_changes);
    puts("PASS: admin profile edits preserve target identity/state/privacy and attribution across all fields");
}
static void profile_admin_denials(void) {
    MemberActor actor=reset();actor.user=2;
    assert(member_core_profile_admin_set(&actor,4,7,"x")==MEMBER_CORE_PROFILE_EDIT);
    actor.user=1;admin=0;assert(member_core_profile_admin_set(&actor,4,7,"x")==MEMBER_CORE_PROFILE_EDIT);
    admin=1;retired[4]=1;
    assert(member_core_profile_admin_set(&actor,4,7,"x")==MEMBER_CORE_PROFILE_EDIT);
    assert(member_core_profile_admin_set(&actor,3,7,"x")==MEMBER_CORE_PROFILE_EDIT);
    assert(member_core_profile_admin_set(&actor,2,PROFILE_BUFFER,"x")==MEMBER_CORE_PROFILE_EDIT);
    assert(member_core_profile_admin_set(&actor,2,7,NULL)==MEMBER_CORE_PROFILE_EDIT);
    assert(!profile_admin_calls && !profile_writes);
    for(int saved=-1;saved<=0;saved++) {
        actor=reset();profile_result=saved;profile_values[4][7]="preserved";
        assert(member_core_profile_admin_set(&actor,4,7,"bad")==
               (saved<0?MEMBER_CORE_PROFILE_EDIT:MEMBER_CORE_EXTRAS_STORAGE));
        assert(profile_admin_calls==1 && !profile_writes && !strcmp(profile_values[4][7],"preserved") && !credential_changes);
    }
    puts("PASS: admin profile edit denials, buffer exclusion and store rejection/failure without state publication");
}
static void application_review_views(void) {
    MemberActor actor=reset();MemberApplication out;MemberProfile public_view;
    profile_values[5][8]="2000-02-29";profile_access[5][8]=2;
    profile_values[5][9]="Private Name";profile_values[5][11]="Private Address";
    application_notes[5].present[APPLICATION_FROM_BBS]=1;
    strcpy(application_notes[5].values[APPLICATION_FROM_BBS],"Origin BBS");
    application_notes[5].present[APPLICATION_SYSOP_NOTE]=1;
    strcpy(application_notes[5].values[APPLICATION_SYSOP_NOTE],"Private Note");
    assert(member_core_profile_get(&actor,5,NULL,&public_view)==MEMBER_CORE_PROFILE_UNAVAILABLE);
    assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_OK);
    assert(out.profile.user==5 && !strcmp(out.profile.name,"user5") && application_target==5);
    assert(!strcmp(member_profile_value(&out.profile,8),"2000-02-29"));
    assert(!strcmp(member_profile_value(&out.profile,9),"Private Name"));
    assert(!strcmp(member_profile_value(&out.profile,11),"Private Address"));
    assert(!strcmp(out.notes.values[APPLICATION_SYSOP_NOTE],"Private Note"));
    memset(profile_scratch,'x',100);profile_scratch[100]=0;
    strcpy(application_notes[5].values[APPLICATION_SYSOP_NOTE],"Changed");
    assert(!strcmp(out.notes.values[APPLICATION_SYSOP_NOTE],"Private Note"));
    assert(!strcmp(member_profile_value(&out.profile,11),"Private Address"));
    suspended[5]=1;protected_target=5;
    assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_OK); /* Pending still reviewable. */
    application_notes[5].present[0]=0;application_notes[5].values[0][0]=0;
    application_notes[5].present[1]=1;application_notes[5].values[1][0]=0;
    assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_OK);
    assert(!out.notes.present[0] && out.notes.present[1] && !*out.notes.values[1]);
    assert(!profile_writes && !application_saves && !credential_changes);
    puts("PASS: admin-only full pending review, owned private fields/notes and missing versus empty notes");
}
static void application_review_authority(void) {
    MemberApplication out;
    for(unsigned state=0;state<8;state++) {
        MemberActor actor=reset();MemberOutcome expected=MEMBER_CORE_REVOKED;
        if(state==0) { actor.user=0;expected=MEMBER_CORE_LOGIN_REQUIRED; }
        if(state==1) { actor.user=2;expected=MEMBER_CORE_ADMIN_REQUIRED; }
        if(state==2) { admin=0;expected=MEMBER_CORE_ADMIN_REQUIRED; }
        if(state==3)actor.epoch--;
        if(state==4)pending[1]=1;
        if(state==5)suspended[1]=1;
        if(state==6)retired[1]=1;
        if(state==7)present[1]=0;
        memset(&out,0x7f,sizeof(out));
        assert(member_core_application_get(&actor,5,&out)==expected);cleared(&out,sizeof(out));
        assert(member_core_application_set(&actor,5,APPLICATION_SYSOP_NOTE,"x")==expected);
        assert(!application_reads && !application_saves && !profile_read_count);
    }
    MemberActor actor=reset();
    assert(member_core_application_get(NULL,5,&out)==MEMBER_CORE_LOGIN_REQUIRED);
    assert(member_core_application_set(NULL,5,APPLICATION_SYSOP_NOTE,"x")==MEMBER_CORE_LOGIN_REQUIRED);
    const uint32_t invalid[]={0,2,3,MEMBER_MAX,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        assert(member_core_application_get(&actor,invalid[i],&out)==MEMBER_CORE_APPLICATION_INVALID);cleared(&out,sizeof(out));
        assert(member_core_application_set(&actor,invalid[i],APPLICATION_FROM_BBS,"x")==MEMBER_CORE_APPLICATION_INVALID);
    }
    retired[5]=1;assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_APPLICATION_INVALID);
    assert(!application_reads && !application_saves && !profile_read_count);
    puts("PASS: application review has no guest/SIGOP/stale-session path and rejects ineligible targets before private reads");
}
static void application_note_edits(void) {
    MemberActor actor=reset();MemberApplication out;char max[321],too_long[322];
    memset(max,'x',320);max[320]=0;memset(too_long,'y',321);too_long[321]=0;
    for(unsigned field=0;field<2;field++) {
        assert(member_core_application_set(&actor,5,(ApplicationNoteField)field,max)==MEMBER_CORE_OK);
        assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_OK);
        assert(out.notes.present[field] && !strcmp(out.notes.values[field],max));
        assert(member_core_application_set(&actor,5,(ApplicationNoteField)field,"")==MEMBER_CORE_OK);
        assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_OK);
        assert(out.notes.present[field] && !*out.notes.values[field]);
    }
    unsigned writes=application_saves;
    assert(member_core_application_set(&actor,5,APPLICATION_FROM_BBS,too_long)==MEMBER_CORE_APPLICATION_INVALID);
    assert(member_core_application_set(&actor,5,APPLICATION_FROM_BBS,NULL)==MEMBER_CORE_APPLICATION_INVALID);
    assert(member_core_application_set(&actor,5,(ApplicationNoteField)-1,"")==MEMBER_CORE_APPLICATION_INVALID);
    assert(member_core_application_set(&actor,5,(ApplicationNoteField)2,"")==MEMBER_CORE_APPLICATION_INVALID);
    assert(application_saves==writes && pending[5] && !profile_writes && !credential_changes);
    MemberChange change;
    assert(member_core_approve(&actor,5,&change)==MEMBER_CORE_OK);
    assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_APPLICATION_INVALID);cleared(&out,sizeof(out));
    assert(member_core_application_set(&actor,5,APPLICATION_SYSOP_NOTE,"late")==MEMBER_CORE_APPLICATION_INVALID);
    assert(application_saves==writes);
    actor=reset();assert(member_core_remove(&actor,5,MEMBER_REJECT,&change)==MEMBER_CORE_OK);
    assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_APPLICATION_INVALID);cleared(&out,sizeof(out));
    assert(member_core_application_set(&actor,5,APPLICATION_FROM_BBS,"late")==MEMBER_CORE_APPLICATION_INVALID);
    puts("PASS: application note limits/empty values and immediate approval/rejection eligibility rechecks");
}
static void application_review_failures(void) {
    MemberApplication out;
    for(unsigned store=0;store<4;store++) {
        MemberActor actor=reset();MemberOutcome expected=MEMBER_CORE_EXTRAS_STORAGE;
        if(store==0) { member_error=1;expected=MEMBER_CORE_STORAGE; }
        if(store==1) { moderation_error=1;expected=MEMBER_CORE_MODERATION_STORAGE; }
        if(store==2)extras_error=1;
        if(store==3)profile_error=1;
        memset(&out,0x7f,sizeof(out));
        assert(member_core_application_get(&actor,5,&out)==expected);cleared(&out,sizeof(out));
        assert(member_core_application_set(&actor,5,APPLICATION_SYSOP_NOTE,"x")==expected);
        assert(!application_reads && !application_saves && !profile_read_count);
    }
    MemberActor actor=reset();profile_values[5][9]="secret";
    application_notes[5].present[1]=1;strcpy(application_notes[5].values[1],"private note");application_load_ok=0;
    assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_SETTINGS_STORAGE);cleared(&out,sizeof(out));
    assert(application_reads==1);
    application_save_ok=0;
    assert(member_core_application_set(&actor,5,APPLICATION_SYSOP_NOTE,"failure")==MEMBER_CORE_SETTINGS_STORAGE);
    assert(!strcmp(application_notes[5].values[1],"private note") && !credential_changes);
    actor=reset();profile_read_failure=13;
    assert(member_core_application_get(&actor,5,&out)==MEMBER_CORE_EXTRAS_STORAGE);cleared(&out,sizeof(out));
    assert(!application_reads);
    assert(member_core_application_get(&actor,5,NULL)==MEMBER_CORE_INVALID);
    puts("PASS: broken/partial application reads clear all private output; failed notes do not publish a state change");
}
static void document_views(void) {
    MemberActor actor=reset();actor.user=2;
    char body[128];profile_values[2][1]="self intro\nsecond line";
    profile_values[2][PROFILE_BUFFER]="private draft\n";
    profile_values[1][PROFILE_BUFFER]="administrator buffer";
    assert(member_core_document_get(&actor,MEMBER_DOCUMENT_BIO,body,sizeof(body))==MEMBER_CORE_OK);
    assert(!strcmp(body,"self intro\nsecond line"));
    assert(member_core_document_get(&actor,MEMBER_DOCUMENT_BUFFER,body,sizeof(body))==MEMBER_CORE_OK);
    assert(!strcmp(body,"private draft\n"));memset(profile_scratch,'x',32);
    assert(!strcmp(body,"private draft\n"));
    assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BUFFER,body)==MEMBER_CORE_OK);
    assert(profile_owner==2 && profile_field_seen==PROFILE_BUFFER && profile_borrowed==body);
    assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BIO,"bio")==MEMBER_CORE_OK && profile_field_seen==1);
    assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BUFFER,"")==MEMBER_CORE_OK && !*profile_borrowed);
    assert(profile_writes==3 && !profile_modes && !credential_changes);
    puts("PASS: owned BIO/buffer snapshots and save/clear isolation without profile visibility or auth changes");
}
static void document_bounds(void) {
    MemberActor actor=reset();char *body=malloc(CONTENT_BODY+2);assert(body);
    memset(body,'x',CONTENT_BODY+1);body[CONTENT_BODY+1]=0;
    assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BUFFER,body)==MEMBER_CORE_PROFILE_TEXT && !profile_sets);
    body[CONTENT_BODY]=0;
    assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BUFFER,body)==MEMBER_CORE_OK);
    profile_values[1][PROFILE_BUFFER]=body;
    char *out=malloc(CONTENT_BODY+2);assert(out);
    assert(member_core_document_get(&actor,MEMBER_DOCUMENT_BUFFER,out,CONTENT_BODY+1)==MEMBER_CORE_OK && !strcmp(body,out));
    assert(member_core_document_get(&actor,MEMBER_DOCUMENT_BUFFER,out,CONTENT_BODY)==MEMBER_CORE_PROFILE_TEXT);cleared(out,CONTENT_BODY);
    body[CONTENT_BODY]='x';
    assert(member_core_document_get(&actor,MEMBER_DOCUMENT_BUFFER,out,CONTENT_BODY+2)==MEMBER_CORE_EXTRAS_STORAGE);cleared(out,CONTENT_BODY+2);
    body[8193]=0;assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BIO,body)==MEMBER_CORE_PROFILE_TEXT);
    body[8192]=0;assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BIO,body)==MEMBER_CORE_OK);
    assert(member_core_document_get(&actor,(MemberDocument)99,out,20)==MEMBER_CORE_INVALID);cleared(out,20);
    assert(member_core_document_save(&actor,(MemberDocument)99,"")==MEMBER_CORE_INVALID);
    assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BUFFER,NULL)==MEMBER_CORE_PROFILE_TEXT);
    profile_values[1][PROFILE_BUFFER]=NULL;
    assert(member_core_document_get(&actor,MEMBER_DOCUMENT_BUFFER,out,20)==MEMBER_CORE_EXTRAS_STORAGE);cleared(out,20);
    free(body);free(out);puts("PASS: document byte limits, owned capacity, invalid kind and corrupt/null storage values");
}
static void document_denials(void) {
    for(unsigned i=0;i<7;i++) {
        MemberActor actor=reset();MemberOutcome expected=MEMBER_CORE_REVOKED;char body[64];
        if(i==0) { actor.user=0;expected=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1)actor.epoch++;
        else if(i==2)suspended[1]=1;
        else if(i==3) { member_error=1;expected=MEMBER_CORE_STORAGE; }
        else if(i==4) { moderation_error=1;expected=MEMBER_CORE_MODERATION_STORAGE; }
        else if(i==5) { profile_error=1;expected=MEMBER_CORE_EXTRAS_STORAGE; }
        else { extras_error=1;expected=MEMBER_CORE_EXTRAS_STORAGE; }
        memset(body,'x',sizeof(body));
        assert(member_core_document_get(&actor,MEMBER_DOCUMENT_BUFFER,body,sizeof(body))==expected);cleared(body,sizeof(body));
        assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BIO,"bio")==expected && !profile_sets && !profile_read_count);
    }
    MemberActor actor=reset();char body[64];profile_values[1][1]="partial private text";profile_read_failure=2;
    assert(member_core_document_get(&actor,MEMBER_DOCUMENT_BIO,body,sizeof(body))==MEMBER_CORE_EXTRAS_STORAGE);cleared(body,sizeof(body));
    for(int saved=-1;saved<=0;saved++) {
        actor=reset();profile_result=saved;
        assert(member_core_document_save(&actor,MEMBER_DOCUMENT_BUFFER,"kept")== (saved<0?MEMBER_CORE_PROFILE_TEXT:MEMBER_CORE_EXTRAS_STORAGE));
        assert(!profile_writes);
    }
    assert(member_core_document_get(NULL,MEMBER_DOCUMENT_BUFFER,body,sizeof(body))==MEMBER_CORE_LOGIN_REQUIRED);
    puts("PASS: direct document auth/epoch/health gates, partial read clearing and failed saves");
}
static void checkpoint_windows(void) {
    MemberActor actor=reset();actor.user=2;MemberCheckWindow out;
    const char *scopes[]={"","2:0","1024:2","","","archive/tools","日本語"};
    MemberCheckKind kinds[]={MEMBER_CHECK_ARTICLES,MEMBER_CHECK_ARTICLES,MEMBER_CHECK_ARTICLES,MEMBER_CHECK_MAIL,MEMBER_CHECK_FILES,MEMBER_CHECK_FILES,MEMBER_CHECK_FILES};
    for(unsigned i=0;i<7;i++) {
        settings_stamp=100;
        assert(member_core_check_begin(&actor,kinds[i],scopes[i],&out)==MEMBER_CORE_OK);
        assert(out.since==100 && out.until==1000 && settings_user==2 && settings_kind==(unsigned)kinds[i] && !strcmp(settings_scope,scopes[i]));
    }
    assert(!settings_writes && !credential_changes);
    assert(member_core_check_commit(&actor,MEMBER_CHECK_ARTICLES,"2:0",1000)==MEMBER_CORE_OK && settings_stamp==1000);
    assert(member_core_check_commit(&actor,MEMBER_CHECK_ARTICLES,"2:0",500)==MEMBER_CORE_OK && settings_stamp==1000);
    settings_now=900; /* Clock rollback retains the checkpoint, never silently resets it. */
    assert(member_core_check_begin(&actor,MEMBER_CHECK_ARTICLES,"2:0",&out)==MEMBER_CORE_OK && out.since==1000 && out.until==900);
    assert(member_core_check_commit(&actor,MEMBER_CHECK_ARTICLES,"2:0",1000)==MEMBER_CORE_INVALID);
    puts("PASS: own typed checkpoint kinds/scopes, frozen begin window, explicit monotonic commit and clock rollback");
}
static void checkpoint_validation(void) {
    MemberActor actor=reset();MemberCheckWindow out;
    const char *invalid[]={"0:0","01:0","1025:0","2:1","2:2x","2:","2:00","999999999999:0","-1:0"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        assert(member_core_check_begin(&actor,MEMBER_CHECK_ARTICLES,invalid[i],&out)==MEMBER_CORE_INVALID);cleared(&out,sizeof(out));
        assert(member_core_check_commit(&actor,MEMBER_CHECK_ARTICLES,invalid[i],0)==MEMBER_CORE_INVALID);
    }
    assert(member_core_check_begin(&actor,MEMBER_CHECK_MAIL,"private",&out)==MEMBER_CORE_INVALID);
    assert(member_core_check_begin(&actor,(MemberCheckKind)3,"",&out)==MEMBER_CORE_INVALID);
    assert(member_core_check_begin(&actor,MEMBER_CHECK_FILES,NULL,&out)==MEMBER_CORE_INVALID);
    assert(member_core_check_begin(&actor,MEMBER_CHECK_FILES,"a|b",&out)==MEMBER_CORE_INVALID);
    assert(member_core_check_begin(&actor,MEMBER_CHECK_FILES,"a\nb",&out)==MEMBER_CORE_INVALID);
    char path[50];memset(path,'x',49);path[49]=0;
    assert(member_core_check_begin(&actor,MEMBER_CHECK_FILES,path,&out)==MEMBER_CORE_INVALID);
    assert(!settings_reads && !settings_writes);
    path[48]=0;assert(member_core_check_begin(&actor,MEMBER_CHECK_FILES,path,&out)==MEMBER_CORE_OK);
    assert(member_core_check_commit(&actor,MEMBER_CHECK_MAIL,"",1001)==MEMBER_CORE_INVALID);
    assert(member_core_check_commit(&actor,MEMBER_CHECK_MAIL,"",UINT64_MAX)==MEMBER_CORE_INVALID);
    assert(member_core_check_begin(&actor,MEMBER_CHECK_MAIL,"",NULL)==MEMBER_CORE_INVALID);
    assert(!settings_writes);puts("PASS: checkpoint key/enum/path/timestamp bounds before persistence");
}
static void reading_settings_authority(void) {
    for(unsigned i=0;i<7;i++) {
        MemberActor actor=reset();MemberOutcome want=MEMBER_CORE_REVOKED;
        if(i==0) { actor.user=0;want=MEMBER_CORE_LOGIN_REQUIRED; }
        else if(i==1)actor.epoch++;
        else if(i==2)suspended[1]=1;
        else if(i==3) { member_error=1;want=MEMBER_CORE_STORAGE; }
        else if(i==4) { moderation_error=1;want=MEMBER_CORE_MODERATION_STORAGE; }
        else if(i==5) { settings_ready=0;want=MEMBER_CORE_SETTINGS_STORAGE; }
        else actor.user=99;
        MemberCheckWindow window;MemberMenuMode mode=MEMBER_MENU_EXPERT;
        assert(member_core_check_begin(&actor,MEMBER_CHECK_MAIL,"",&window)==want);cleared(&window,sizeof(window));
        assert(member_core_check_commit(&actor,MEMBER_CHECK_MAIL,"",100)==want);
        assert(member_core_menu_get(&actor,&mode)==want && mode==0);
        assert(member_core_menu_save(&actor,MEMBER_MENU_BEGINNER)==want);
        char ui_name[32];assert(member_core_ui_get(&actor,ui_name)==want && !*ui_name);
        assert(member_core_ui_save(&actor,"rtbbs")==want);
        assert(!settings_reads && !settings_writes);
    }
    MemberCheckWindow window;assert(member_core_check_begin(NULL,MEMBER_CHECK_MAIL,"",&window)==MEMBER_CORE_LOGIN_REQUIRED);
    puts("PASS: direct checkpoint/menu auth, suspension, credential and settings-health gates");
}
static void reading_settings_failures(void) {
    MemberActor actor=reset();MemberCheckWindow out;MemberMenuMode mode;
    char ui_name[32];assert(member_core_ui_save(&actor,"future-ui")==MEMBER_CORE_OK);
    assert(member_core_ui_get(&actor,ui_name)==MEMBER_CORE_OK && !strcmp(ui_name,"future-ui"));
    assert(member_core_ui_save(&actor,"bad name")==MEMBER_CORE_INVALID);
    settings_save_ok=0;assert(member_core_ui_save(&actor,"rtbbs")==MEMBER_CORE_SETTINGS_STORAGE);settings_save_ok=1;
    assert(member_core_ui_get(&actor,ui_name)==MEMBER_CORE_OK && !strcmp(ui_name,"future-ui"));
    assert(member_core_ui_save(&actor,"")==MEMBER_CORE_OK);
    for(unsigned i=0;i<3;i++) {
        assert(member_core_menu_save(&actor,(MemberMenuMode)i)==MEMBER_CORE_OK);
        assert(member_core_menu_get(&actor,&mode)==MEMBER_CORE_OK && (unsigned)mode==i);
    }
    assert(member_core_menu_save(&actor,(MemberMenuMode)3)==MEMBER_CORE_INVALID);
    settings_mode=3;assert(member_core_menu_get(&actor,&mode)==MEMBER_CORE_SETTINGS_STORAGE && !mode);
    settings_mode=2;settings_get_ok=0;settings_stamp=123;
    assert(member_core_menu_get(&actor,&mode)==MEMBER_CORE_SETTINGS_STORAGE && !mode);
    assert(member_core_check_begin(&actor,MEMBER_CHECK_MAIL,"",&out)==MEMBER_CORE_SETTINGS_STORAGE);cleared(&out,sizeof(out));
    settings_get_ok=1;settings_clock_ok=0;
    assert(member_core_check_begin(&actor,MEMBER_CHECK_MAIL,"",&out)==MEMBER_CORE_SETTINGS_STORAGE);cleared(&out,sizeof(out));
    assert(member_core_check_commit(&actor,MEMBER_CHECK_MAIL,"",123)==MEMBER_CORE_SETTINGS_STORAGE);
    settings_clock_ok=1;settings_stamp=UINT64_MAX;
    assert(member_core_check_begin(&actor,MEMBER_CHECK_MAIL,"",&out)==MEMBER_CORE_SETTINGS_STORAGE);cleared(&out,sizeof(out));
    settings_stamp=123;settings_save_ok=0;unsigned writes=settings_writes;
    assert(member_core_check_commit(&actor,MEMBER_CHECK_MAIL,"",1000)==MEMBER_CORE_SETTINGS_STORAGE && settings_stamp==123);
    assert(member_core_menu_save(&actor,MEMBER_MENU_BEGINNER)==MEMBER_CORE_SETTINGS_STORAGE && settings_writes==writes);
    puts("PASS: menu modes, dirty-output clearing, clock/store failures and no failed-save acknowledgement");
}
int main(void) {
    checkpoint_windows();checkpoint_validation();reading_settings_authority();reading_settings_failures();
    document_views();document_bounds();document_denials();
    application_review_views();application_review_authority();application_note_edits();application_review_failures();
    profile_snapshots();profile_search_pages();profile_search_policy();profile_service_failures();
    profile_admin_edits();profile_admin_denials();
    profile_owned_updates();profile_authority();profile_failures();
    role_tests();directory_tests();denial_tests();failure_tests();
    suspension_tests();suspension_denials();suspension_failures();
    application_pages();approval_tests();application_denials();
    rights_queries();rights_failures();level_updates();level_denials();
    group_updates();delegated_groups();group_failures();
    group_move_success();group_move_denials();group_move_failures();
    lifecycle_success();lifecycle_denials();lifecycle_failures();
    password_updates();reset_authority();password_failures();
    preference_roundtrip();preference_authority();preference_failures();
    notification_roundtrip();notification_authority();notification_failures();
    chat_kick_success();chat_kick_authority();chat_kick_failures();
    chat_settings_snapshots();chat_settings_authority();chat_settings_failures();return 0;
}

/* Direct account Core tests; persistence and catalog authority are stubbed. */
#include "member_core.h"
#include "members.h"
#include "moderation.h"
#include "extras.h"
#include "catalog.h"
#include "app_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    assert(user && user<15 && (approval_writes || level_writes || group_writes || remove_writes || password_writes));
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
    count=14;epoch=9;admin=1;member_error=moderation_error=read_failure=0;
    count_calls=target_reads=0;approval_calls=approval_writes=credential_changes=0;
    level_calls=level_writes=0;password_calls=reset_calls=password_writes=0;remove_calls=remove_writes=removed_kind=0;memset(group_words,0,sizeof(group_words));
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
int main(void) {
    role_tests();directory_tests();denial_tests();failure_tests();
    suspension_tests();suspension_denials();suspension_failures();
    application_pages();approval_tests();application_denials();
    rights_queries();rights_failures();level_updates();level_denials();
    group_updates();delegated_groups();group_failures();
    lifecycle_success();lifecycle_denials();lifecycle_failures();
    password_updates();reset_authority();password_failures();return 0;
}

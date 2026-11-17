/* Direct account Core tests; only account/moderation persistence is stubbed. */
#include "member_core.h"
#include "members.h"
#include "moderation.h"
#include "extras.h"
#include "app_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t count,epoch;
static char names[15][34];
static unsigned char present[15],pending[15],suspended[15];
static int member_error,moderation_error,admin,read_failure;
static unsigned count_calls,target_reads;
static uint32_t epoch_changes[15],mod_count,extra_count,protected_target;
static uint32_t changed_user,changed_actor;
static unsigned suspend_calls,durable_writes;
static int extras_error,commit_ok,changed_flag;
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
    return id<15?pending[id]:0;
}
int moderation_suspended(uint32_t id) { return id<15?(suspended[id] || pending[id]):0; }
uint32_t moderation_epoch(uint32_t id) { return epoch+(id<15?epoch_changes[id]:0); }
int moderation_admin(uint32_t id) { return id==1 && admin; }
static MemberActor reset(void) {
    count=14;epoch=9;admin=1;member_error=moderation_error=read_failure=0;
    count_calls=target_reads=0;
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
int main(void) {
    role_tests();directory_tests();denial_tests();failure_tests();
    suspension_tests();suspension_denials();suspension_failures();return 0;
}

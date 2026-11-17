/* Direct account Core tests; only account/moderation persistence is stubbed. */
#include "member_core.h"
#include "members.h"
#include "moderation.h"
#include "app_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t count,epoch;
static char names[15][34];
static unsigned char present[15],pending[15],suspended[15];
static int member_error,moderation_error,admin,read_failure;
static unsigned count_calls,target_reads;
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
int moderation_suspended(uint32_t id) { return id<15?suspended[id]:0; }
uint32_t moderation_epoch(uint32_t id) { (void)id;return epoch; }
int moderation_admin(uint32_t id) { return id==1 && admin; }
static MemberActor reset(void) {
    count=14;epoch=9;admin=1;member_error=moderation_error=read_failure=0;
    count_calls=target_reads=0;
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
int main(void) { role_tests();directory_tests();denial_tests();failure_tests();return 0; }

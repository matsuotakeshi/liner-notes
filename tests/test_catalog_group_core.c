#include "catalog_group_core.h"
#include "catalog_group_repository.h"
#include "members.h"
#include "moderation.h"
#include "app_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static CatalogOutcome permission;
static int ready,save_delegate,save_move,committed;
static unsigned writes_delegate,writes_move,changes,authority_calls,scope_reads;
static uint32_t total,last_slot,epochs[8];
static unsigned saved_group,saved_from,saved_to;
CatalogOutcome catalog_core_admin(const CatalogActor *actor) {
    assert(actor && actor->user==1 && actor->epoch==8);authority_calls++;return permission;
}
int catalog_exists(uint32_t b) { scope_reads++;return b==2 || b==3; }
int catalog_group(uint32_t b,unsigned g) { scope_reads++;return b==2 && (g==10 || g==100); }
int catalog_group_repository_ready(void) { return ready; }
int catalog_group_repository_delegate(uint32_t actor,uint32_t b,unsigned g) {
    assert(actor==1 && (b==2 || (b==3 && !g)));assert(!changes);writes_delegate++;saved_group=g;return save_delegate;
}
int catalog_group_repository_move(uint32_t actor,uint32_t b,unsigned from,unsigned to) {
    assert(actor==1 && b==2 && !changes);writes_move++;saved_from=from;saved_to=to;
    committed=save_move;return save_move;
}
uint32_t members_count(void) { return total; }
void moderation_credentials_changed(uint32_t user) {
    assert(committed && user==last_slot+1 && user<=total && user<8);last_slot=user;changes++;epochs[user]++;
}
uint32_t moderation_epoch(uint32_t user) { assert(user<8);return epochs[user]; }
static CatalogActor reset(void) {
    permission=CATALOG_CORE_OK;ready=save_delegate=save_move=1;committed=0;
    writes_delegate=writes_move=changes=authority_calls=scope_reads=0;
    total=5;last_slot=0;saved_group=saved_from=saved_to=0;
    for(unsigned i=0;i<8;i++)epochs[i]=8;
    return (CatalogActor){1,8};
}
static void zero(const void *data,size_t size) { const unsigned char *p=data;for(size_t i=0;i<size;i++)assert(!p[i]); }
int main(void) {
    CatalogActor actor=reset();CatalogGroupChange out;
    assert(catalog_group_delegate(&actor,2,10)==GROUP_CORE_OK && saved_group==10);
    assert(catalog_group_delegate(&actor,2,100)==GROUP_CORE_OK && saved_group==100);
    assert(catalog_group_delegate(&actor,3,0)==GROUP_CORE_OK && saved_group==0 && writes_delegate==3);
    assert(!writes_move && !changes);
    puts("PASS: own-board ACL group records, upper boundary and scoped clear without epoch changes");

    actor=reset();assert(catalog_group_delegate(&actor,2,11)==GROUP_CORE_INVALID);
    assert(catalog_group_delegate(&actor,3,10)==GROUP_CORE_INVALID);
    assert(catalog_group_delegate(&actor,0,0)==GROUP_CORE_INVALID);
    assert(catalog_group_delegate(&actor,2,101)==GROUP_CORE_INVALID);
    assert(!writes_delegate && !changes);
    save_delegate=0;assert(catalog_group_delegate(&actor,2,10)==GROUP_CORE_SETTINGS_STORAGE && writes_delegate==1);
    puts("PASS: missing/inherited-only/out-of-range group rejected; persistence failure propagated");

    actor=reset();memset(&out,0xff,sizeof(out));
    assert(catalog_group_move(&actor,2,10,11,&out)==GROUP_CORE_OK);
    assert(saved_from==10 && saved_to==11 && writes_move==1 && changes==5 && last_slot==5);
    assert(out.through==5 && out.actor_epoch==9);
    for(unsigned i=1;i<=total;i++)assert(epochs[i]==9);
    puts("PASS: commit precedes inclusive-slot invalidation and initiating-session epoch publication");

    const unsigned invalid[][3]={{0,10,11},{3,10,11},{2,0,11},{2,101,11},{2,10,0},{2,10,101},{2,10,10},{2,11,12}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        actor=reset();memset(&out,0xff,sizeof(out));
        assert(catalog_group_move(&actor,invalid[i][0],invalid[i][1],invalid[i][2],&out)==GROUP_CORE_INVALID);
        zero(&out,sizeof(out));assert(!writes_move && !changes);
    }
    actor=reset();assert(catalog_group_move(&actor,2,100,1,&out)==GROUP_CORE_OK && saved_from==100 && saved_to==1);
    assert(catalog_group_move(&actor,2,10,11,NULL)==GROUP_CORE_INVALID);
    puts("PASS: migration source/destination boundaries, exact source ACL and invalid-output handling");

    actor=reset();save_move=0;memset(&out,0xff,sizeof(out));
    assert(catalog_group_move(&actor,2,10,11,&out)==GROUP_CORE_SETTINGS_STORAGE);
    zero(&out,sizeof(out));assert(writes_move==1 && !changes);
    actor=reset();total=MEMBER_MAX+1u;
    assert(catalog_group_move(&actor,2,10,11,&out)==GROUP_CORE_MEMBER_STORAGE);
    assert(!writes_move && !changes);zero(&out,sizeof(out));
    puts("PASS: failed/partially durable migration publishes no successful connection refresh");

    const CatalogOutcome errors[]={CATALOG_CORE_DENIED,CATALOG_CORE_LOGIN_REQUIRED,CATALOG_CORE_REVOKED,CATALOG_CORE_MEMBER_STORAGE,CATALOG_CORE_MODERATION_STORAGE,CATALOG_CORE_POLICY_STORAGE,CATALOG_CORE_STORAGE};
    const CatalogGroupOutcome expected[]={GROUP_CORE_ADMIN_REQUIRED,GROUP_CORE_ADMIN_REQUIRED,GROUP_CORE_REVOKED,GROUP_CORE_MEMBER_STORAGE,GROUP_CORE_MODERATION_STORAGE,GROUP_CORE_POLICY_STORAGE,GROUP_CORE_CATALOG_STORAGE};
    for(unsigned i=0;i<sizeof(errors)/sizeof(*errors);i++) {
        actor=reset();permission=errors[i];memset(&out,0xff,sizeof(out));
        assert(catalog_group_delegate(&actor,2,10)==expected[i]);
        assert(catalog_group_move(&actor,2,10,11,&out)==expected[i]);
        assert(authority_calls==2 && !scope_reads && !writes_delegate && !writes_move && !changes);
        zero(&out,sizeof(out));
    }
    puts("PASS: every call checks current administrator/identity/storage before target or mutation");

    actor=reset();ready=0;
    assert(catalog_group_delegate(&actor,2,10)==GROUP_CORE_SETTINGS_STORAGE);
    assert(catalog_group_move(&actor,2,10,11,&out)==GROUP_CORE_SETTINGS_STORAGE);
    assert(!writes_move && !writes_delegate && !changes);zero(&out,sizeof(out));
    actor=reset();assert(catalog_group_delegate(&actor,2,10)==GROUP_CORE_OK);
    permission=CATALOG_CORE_REVOKED;
    assert(catalog_group_move(&actor,2,10,11,&out)==GROUP_CORE_REVOKED && !writes_move && !changes);
    puts("PASS: unavailable settings and authority revoked between calls fail closed");
    return 0;
}

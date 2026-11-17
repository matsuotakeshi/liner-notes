/* Exercise the service without Session, UI, transport or journal files. */
#include "catalog_core.h"
#include "members.h"
#include "moderation.h"
#include "board_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int member_error,moderation_error,policy_error,store_error,suspended,admin;
static uint32_t epoch=8;
static unsigned char present[CATALOG_MAX+1],managed[CATALOG_MAX+1];
static uint32_t parents[CATALOG_MAX+1],assigned[CATALOG_MAX+1];
static CatalogConfig config;
static CatalogChange records[301];
static uint32_t count,read_failure,role_failure;
static int info_ok,decode_ok,save_ok;
static unsigned info_reads,role_reads,history_reads,decode_calls,saves;
static char saved[1024];
int members_broken(void) { return member_error; }
int moderation_broken(void) { return moderation_error; }
int policy_broken(void) { return policy_error; }
int catalog_storage_broken(void) { return store_error; }
const char *members_name(uint32_t user) { return user==1?"actor":NULL; }
int moderation_suspended(uint32_t user) { (void)user;return suspended; }
uint32_t moderation_epoch(uint32_t user) { (void)user;return epoch; }
int moderation_admin(uint32_t user) { return user==1 && admin; }
int catalog_exists(uint32_t id) { return id>0 && id<=CATALOG_MAX && present[id]; }
uint32_t catalog_parent(uint32_t id) { assert(catalog_exists(id));return parents[id]; }
int catalog_sigop(uint32_t id,uint32_t user) { return user==1 && id<=CATALOG_MAX && managed[id]; }
int catalog_manage(uint32_t id,uint32_t user) { return catalog_exists(id) && (moderation_admin(user) || catalog_sigop(id,user)); }
int catalog_config(uint32_t id,CatalogConfig *out) {
    assert(id==2);info_reads++;*out=config;return info_ok;
}
int catalog_decode(const char *text,CatalogConfig *out) {
    assert(text && strlen(text)<1024);decode_calls++;*out=config;return decode_ok;
}
int catalog_set(uint32_t user,const char *text,uint32_t *id) {
    assert(user==1);saves++;strcpy(saved,text);*id=config.id;return save_ok;
}
int catalog_role_get(uint32_t board,uint32_t *user) {
    role_reads++;*user=assigned[board];return board!=role_failure;
}
uint32_t catalog_change_count(void) { return count; }
int catalog_change_get(uint32_t sequence,CatalogChange *out) {
    assert(sequence && sequence<=count);history_reads++;*out=records[sequence];return sequence!=read_failure;
}
static CatalogActor reset(void) {
    member_error=moderation_error=policy_error=store_error=suspended=admin=0;
    epoch=8;memset(present,0,sizeof(present));memset(managed,0,sizeof(managed));
    memset(parents,0,sizeof(parents));memset(assigned,0,sizeof(assigned));
    present[1]=present[2]=present[3]=1;parents[2]=1;parents[3]=1;managed[2]=1;
    memset(&config,0,sizeof(config));config.id=2;config.parent=1;
    strcpy(config.read_acl,"all");strcpy(config.write_acl,"member");
    strcpy(config.title,"managed");strcpy(config.description,"private settings");
    info_ok=decode_ok=save_ok=1;count=read_failure=role_failure=0;
    info_reads=role_reads=history_reads=decode_calls=saves=0;saved[0]=0;
    memset(records,0,sizeof(records));return (CatalogActor){1,epoch};
}
static void zero(const void *data,size_t size) {
    const unsigned char *p=data;for(size_t i=0;i<size;i++)assert(!p[i]);
}
static void records_make(unsigned n) {
    count=n;for(uint32_t i=1;i<=n;i++) {
        records[i].sequence=i;records[i].actor=1;records[i].board=2;records[i].time=1000+i;
        snprintf(records[i].specification,sizeof(records[i].specification),"2|1|all|member|title%u|description",i);
    }
}
int main(void) {
    CatalogActor actor=reset();CatalogConfig info;CatalogRoles roles;CatalogHistory page;uint32_t id;
    assert(catalog_core_info(&actor,2,&info)==CATALOG_CORE_OK && info.id==2 && info.parent==1);
    strcpy(config.title,"changed");assert(!strcmp(info.title,"managed"));
    assert(catalog_core_info(&actor,3,&info)==CATALOG_CORE_DENIED);zero(&info,sizeof(info));
    info_ok=0;assert(catalog_core_info(&actor,2,&info)==CATALOG_CORE_STORAGE);zero(&info,sizeof(info));
    puts("PASS: settings snapshot ownership, actual-board authority and failed-output clearing");

    actor=reset();const char *text="002|1|all|member|title|description";
    assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_OK && id==2 && saves==1 && !strcmp(saved,text));
    config.parent=3;assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_INVALID && !id && saves==1);
    managed[3]=1;assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_OK && saves==2);
    managed[2]=0;assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_INVALID && saves==2 && !id);
    config.id=4;assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_OK && id==4);
    managed[3]=0;assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_INVALID && !id);
    admin=1;assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_OK && id==4);
    puts("PASS: edit/create/reparent checks both old and proposed authority scopes");

    actor=reset();save_ok=-1;
    assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_STORAGE && !id);
    save_ok=0;assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_INVALID && !id);
    unsigned before=saves;decode_ok=0;
    assert(catalog_core_update(&actor,text,&id)==CATALOG_CORE_INVALID && saves==before && !id);
    char too_long[1025];memset(too_long,'x',1024);too_long[1024]=0;
    assert(catalog_core_update(&actor,too_long,&id)==CATALOG_CORE_INVALID && saves==before);
    assert(catalog_core_update(&actor,NULL,&id)==CATALOG_CORE_INVALID);
    assert(catalog_core_update(&actor,"",&id)==CATALOG_CORE_INVALID);
    puts("PASS: invalid/oversized input and durable failures do not return an updated board ID");

    actor=reset();assigned[2]=7;
    assert(catalog_core_roles(&actor,&roles)==CATALOG_CORE_OK && roles.count==1 && roles.rows[0].board==2 && roles.rows[0].user==7);
    managed[2]=0;assert(catalog_core_roles(&actor,&roles)==CATALOG_CORE_OK && !roles.count);
    admin=1;role_failure=3;
    assert(catalog_core_roles(&actor,&roles)==CATALOG_CORE_STORAGE);zero(&roles,sizeof(roles));
    role_failure=0;for(unsigned i=1;i<=CATALOG_MAX;i++)present[i]=1;
    assert(catalog_core_roles(&actor,&roles)==CATALOG_CORE_OK && roles.count==CATALOG_MAX);
    assert(roles.rows[CATALOG_MAX-1].board==CATALOG_MAX);
    puts("PASS: complete bounded role list, ordinary-member empty view and atomic read failure");

    actor=reset();records_make(20);records[20].board=3;
    assert(catalog_core_history(&actor,2,0,&page)==CATALOG_CORE_OK && page.count==8 && page.next==12);
    assert(page.rows[0].sequence==19 && page.rows[7].sequence==12 && history_reads==9);
    assert(catalog_core_history(&actor,2,page.next,&page)==CATALOG_CORE_OK && page.count==8 && page.rows[0].sequence==11 && page.next==4);
    assert(catalog_core_history(&actor,2,page.next,&page)==CATALOG_CORE_OK && page.count==3 && !page.next);
    assert(catalog_core_history(&actor,2,1,&page)==CATALOG_CORE_OK && !page.count && !page.next);
    assert(catalog_core_history(&actor,2,22,&page)==CATALOG_CORE_CURSOR);zero(&page,sizeof(page));
    assert(catalog_core_history(&actor,3,0,&page)==CATALOG_CORE_DENIED);zero(&page,sizeof(page));
    puts("PASS: descending history and legacy cursors with per-record management filtering");

    actor=reset();records_make(300);for(unsigned i=1;i<=300;i++)records[i].board=3;
    assert(catalog_core_history(&actor,2,0,&page)==CATALOG_CORE_OK && !page.count && page.next==45 && history_reads==256);
    managed[2]=0;assert(catalog_core_history(&actor,2,page.next,&page)==CATALOG_CORE_DENIED);zero(&page,sizeof(page));
    actor=reset();records_make(5);read_failure=3;
    assert(catalog_core_history(&actor,2,0,&page)==CATALOG_CORE_STORAGE && history_reads==3);zero(&page,sizeof(page));
    puts("PASS: 256-candidate history bound, authority revocation and no partial data on failure");

    for(unsigned mode=0;mode<8;mode++) {
        actor=reset();CatalogOutcome wanted=CATALOG_CORE_REVOKED;
        switch(mode) {
        case 0:actor.user=0;wanted=CATALOG_CORE_LOGIN_REQUIRED;break;
        case 1:actor.epoch--;break;
        case 2:suspended=1;break;
        case 3:actor.user=99;break;
        case 4:member_error=1;wanted=CATALOG_CORE_MEMBER_STORAGE;break;
        case 5:moderation_error=1;wanted=CATALOG_CORE_MODERATION_STORAGE;break;
        case 6:policy_error=1;wanted=CATALOG_CORE_POLICY_STORAGE;break;
        case 7:store_error=1;wanted=CATALOG_CORE_STORAGE;break;
        }
        memset(&info,0xff,sizeof(info));memset(&roles,0xff,sizeof(roles));memset(&page,0xff,sizeof(page));id=99;
        assert(catalog_core_info(&actor,2,&info)==wanted);zero(&info,sizeof(info));
        assert(catalog_core_roles(&actor,&roles)==wanted);zero(&roles,sizeof(roles));
        assert(catalog_core_history(&actor,2,0,&page)==wanted);zero(&page,sizeof(page));
        assert(catalog_core_update(&actor,text,&id)==wanted && !id);
        assert(!info_reads && !role_reads && !history_reads && !decode_calls && !saves);
    }
    puts("PASS: all catalog APIs recheck identity/storage before reads or writes");
    return 0;
}

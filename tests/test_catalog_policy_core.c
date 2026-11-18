#include "catalog_policy_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static CatalogOutcome authority;
static int broken,prepare_ok,has_articles,settings_ok,options_ok,prune_ok,password_ok,info_ok;
static unsigned auth_calls,prepares,option_writes,prunes,password_writes,info_writes,settings_reads;
static char information[BOARD_INFO_MAX+2],password_value[129];
static BoardPolicySettings settings;
static BoardAccessRecord records[301];
static uint32_t count,keep,read_failure;static unsigned record_reads,fail_on_call;
CatalogOutcome catalog_core_manage(const CatalogActor *actor,uint32_t board) {
    assert(actor && actor->user==1 && actor->epoch==8);auth_calls++;
    return authority!=CATALOG_CORE_OK?authority:board==2?CATALOG_CORE_OK:CATALOG_CORE_DENIED;
}
int board_policy_repository_broken(void) { return broken; }
int board_policy_repository_prepare(void) { prepares++;return prepare_ok; }
int board_policy_repository_has_articles(uint32_t board) { assert(board==2);return has_articles; }
int board_policy_repository_prune(void) { prunes++;assert(option_writes && options_ok>0);return prune_ok; }
uint32_t board_policy_repository_access_count(void) { return count; }
int board_policy_repository_access_get(uint32_t index,BoardAccessRecord *out) {
    assert(index<count);record_reads++;*out=records[index];return index!=read_failure && record_reads!=fail_on_call;
}
int policy_settings(uint32_t board,BoardPolicySettings *out) { assert(board==2);settings_reads++;*out=settings;return settings_ok; }
int policy_options(uint32_t board,uint32_t actor,const char *text) {
    assert(board==2 && actor==1 && text && prepares);option_writes++;return options_ok;
}
int policy_password(uint32_t board,uint32_t actor,const char *text) {
    assert(board==2 && actor==1 && text && prepares);password_writes++;
    assert(strlen(text)<sizeof(password_value));strcpy(password_value,text);return password_ok;
}
const char *policy_info(uint32_t board) { assert(board==2);return information; }
int policy_information(uint32_t board,uint32_t actor,const char *body) {
    assert(board==2 && actor==1 && body && prepares);info_writes++;
    if(strlen(body)>BOARD_INFO_MAX)return -1;
    return info_ok;
}
uint32_t policy_log_keep(uint32_t board) { assert(board==2);return keep; }
static CatalogActor reset(void) {
    authority=CATALOG_CORE_OK;broken=has_articles=0;
    prepare_ok=settings_ok=options_ok=prune_ok=password_ok=info_ok=1;
    auth_calls=prepares=option_writes=prunes=password_writes=info_writes=settings_reads=record_reads=0;
    strcpy(information,"old guide\r\n");password_value[0]=0;
    memset(&settings,0,sizeof(settings));settings.order=7;settings.log_keep=1000;settings.password_set=1;
    memset(records,0,sizeof(records));count=0;keep=1000;read_failure=UINT32_MAX;fail_on_call=0;
    return (CatalogActor){1,8};
}
static void zero(const void *data,size_t size) { const unsigned char *p=data;for(size_t i=0;i<size;i++)assert(!p[i]); }
static void make_records(unsigned n) {
    count=n;for(unsigned i=0;i<n;i++)records[i]=(BoardAccessRecord){i+1,2,100+i,3,1000+i,i%2?'R':'P'};
}
int main(void) {
    CatalogActor actor=reset();BoardPolicySettings snapshot;BoardAccessPage page;char body[BOARD_INFO_MAX+1];int attempted;
    assert(catalog_policy_get(&actor,2,&snapshot)==CATALOG_CORE_OK && snapshot.password_set && snapshot.order==7);
    settings.order=9;assert(snapshot.order==7 && !prepares);
    settings_ok=0;assert(catalog_policy_get(&actor,2,&snapshot)==CATALOG_CORE_POLICY_STORAGE);zero(&snapshot,sizeof(snapshot));
    puts("PASS: policy snapshot owns only public values and clears failed reads");

    actor=reset();assert(catalog_policy_set(&actor,2,"BOARD 1 0 0 2")==CATALOG_CORE_OK && prepares==1 && option_writes==1 && prunes==1);
    actor=reset();has_articles=1;assert(catalog_policy_set(&actor,2,"MENU 1 0 0 2")==CATALOG_CORE_NOT_EMPTY && !option_writes && !prunes);
    actor=reset();prepare_ok=0;assert(catalog_policy_set(&actor,2,"BOARD 1 0 0 2")==CATALOG_CORE_POLICY_STORAGE && !option_writes);
    actor=reset();options_ok=-1;assert(catalog_policy_set(&actor,2,"bad")==CATALOG_CORE_INVALID && !prunes);
    options_ok=0;assert(catalog_policy_set(&actor,2,"BOARD 1 0 0 2")==CATALOG_CORE_POLICY_STORAGE && !prunes);
    options_ok=1;prune_ok=0;assert(catalog_policy_set(&actor,2,"BOARD 1 0 0 2")==CATALOG_CORE_POLICY_STORAGE && prunes==1);
    puts("PASS: menu admission, format preparation, durable options and retention failure ordering");

    actor=reset();assert(catalog_password_prepare(&actor,2)==CATALOG_CORE_OK && !prepares && !password_writes);
    assert(catalog_password_set(&actor,2,"test-only-password")==CATALOG_CORE_OK && !strcmp(password_value,"test-only-password"));
    assert(catalog_password_set(&actor,2,"")==CATALOG_CORE_OK && !password_value[0]);
    password_ok=0;assert(catalog_password_set(&actor,2,"test-only-password")==CATALOG_CORE_POLICY_STORAGE);
    actor=reset();prepare_ok=0;assert(catalog_password_set(&actor,2,"test-only-password")==CATALOG_CORE_POLICY_STORAGE && !password_writes);
    puts("PASS: password prompt does not write; save/clear prepare storage and propagate failures");

    actor=reset();assert(catalog_information_get(&actor,2,body,sizeof(body))==CATALOG_CORE_OK && !strcmp(body,information));
    strcpy(information,"changed");assert(!strcmp(body,"old guide\r\n"));
    assert(catalog_information_get(&actor,2,body,3)==CATALOG_CORE_BUFFER);zero(body,3);
    memset(information,'a',BOARD_INFO_MAX);information[BOARD_INFO_MAX]=0;
    assert(catalog_information_get(&actor,2,body,sizeof(body))==CATALOG_CORE_OK && strlen(body)==BOARD_INFO_MAX);
    information[BOARD_INFO_MAX]='a';information[BOARD_INFO_MAX+1]=0;
    assert(catalog_information_get(&actor,2,body,sizeof(body))==CATALOG_CORE_POLICY_STORAGE);zero(body,sizeof(body));
    assert(catalog_information_set(&actor,2,information,&attempted)==CATALOG_CORE_INFO_LIMIT && attempted);
    actor=reset();info_ok=-1;assert(catalog_information_set(&actor,2,"invalid",&attempted)==CATALOG_CORE_INFO_LIMIT && attempted);
    info_ok=0;assert(catalog_information_set(&actor,2,"new",&attempted)==CATALOG_CORE_POLICY_STORAGE && attempted);
    info_ok=1;assert(catalog_information_set(&actor,2,"",&attempted)==CATALOG_CORE_OK && attempted);
    prepare_ok=0;assert(catalog_information_set(&actor,2,"new",&attempted)==CATALOG_CORE_POLICY_STORAGE && !attempted);
    puts("PASS: guide ownership, 8192-byte bounds and editor outcome distinguishes write/preparation failures");

    actor=reset();make_records(20);records[19].board=3;keep=10;
    assert(catalog_access_page(&actor,2,0,&page)==CATALOG_CORE_OK && page.count==8 && page.rows[0].sequence==19 && page.next==12);
    assert(catalog_access_page(&actor,2,page.next,&page)==CATALOG_CORE_OK && page.count==2 && !page.next && page.rows[1].sequence==10);
    keep=0;assert(catalog_access_page(&actor,2,0,&page)==CATALOG_CORE_OK && !page.count && !page.next);
    assert(catalog_access_page(&actor,2,22,&page)==CATALOG_CORE_CURSOR);zero(&page,sizeof(page));
    actor=reset();make_records(300);for(unsigned i=1;i<300;i++)records[i].board=3;
    assert(catalog_access_page(&actor,2,0,&page)==CATALOG_CORE_OK && !page.count && page.next==45);
    assert(catalog_access_page(&actor,2,page.next,&page)==CATALOG_CORE_OK && page.count==1 && page.rows[0].sequence==1 && !page.next);
    puts("PASS: access retention, descending cursors, zero retention and 256-candidate page bound");

    actor=reset();make_records(10);keep=2;read_failure=7;
    /* Lower bound needs rows 9 and 8; then reading the displayed window is safe. */
    assert(catalog_access_page(&actor,2,0,&page)==CATALOG_CORE_OK && page.count==2);
    keep=10;assert(catalog_access_page(&actor,2,0,&page)==CATALOG_CORE_POLICY_STORAGE);zero(&page,sizeof(page));
    actor=reset();make_records(10);keep=10;fail_on_call=13;
    assert(catalog_access_page(&actor,2,0,&page)==CATALOG_CORE_POLICY_STORAGE && record_reads==13);zero(&page,sizeof(page));
    puts("PASS: access read failure returns no partial audit metadata");

    CatalogOutcome failures[]={CATALOG_CORE_LOGIN_REQUIRED,CATALOG_CORE_REVOKED,CATALOG_CORE_DENIED,
        CATALOG_CORE_MEMBER_STORAGE,CATALOG_CORE_MODERATION_STORAGE,CATALOG_CORE_POLICY_STORAGE,CATALOG_CORE_STORAGE};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);i++) {
        actor=reset();authority=failures[i];memset(body,1,sizeof(body));attempted=9;
        assert(catalog_policy_get(&actor,2,&snapshot)==authority);zero(&snapshot,sizeof(snapshot));
        assert(catalog_policy_set(&actor,2,"BOARD 1 0 0 2")==authority);
        assert(catalog_password_prepare(&actor,2)==authority);
        assert(catalog_password_set(&actor,2,"test-only-password")==authority);
        assert(catalog_information_get(&actor,2,body,sizeof(body))==authority);zero(body,sizeof(body));
        assert(catalog_information_set(&actor,2,"new",&attempted)==authority && !attempted);
        assert(catalog_access_page(&actor,2,0,&page)==authority);zero(&page,sizeof(page));
        assert(auth_calls==7 && !prepares && !option_writes && !prunes && !password_writes && !info_writes && !record_reads && !settings_reads);
    }
    actor=reset();broken=1;assert(catalog_password_prepare(&actor,2)==CATALOG_CORE_POLICY_STORAGE);
    assert(catalog_policy_set(&actor,2,"BOARD 1 0 0 2")==CATALOG_CORE_POLICY_STORAGE && !prepares);
    puts("PASS: every policy operation reauthorizes before storage or secret processing");
    return 0;
}

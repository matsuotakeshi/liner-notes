#include "link_core.h"
#include "link_repository.h"
#include "members.h"
#include "moderation.h"
#include "catalog.h"
#include "board_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static LinkActor actor={7,3,NULL};
static char origin[65],key[65];
static LinkEvent event;
static LinkOutcome health,post_failure;
static int enabled,readable,writable,suspended,menu,version,done,removed,on_board,match;
static int lookup_ok,intent_ok,recover_ok,commit_ok,next_fail,body_ok,failures;
static unsigned lookups,intents,posts,recovers,body_reads,next_reads;
static LinkReference stored;
static LinkItem items[40];static unsigned item_count;
static char saved_body[128],saved_title[81],saved_alias[33];
static uint32_t intent_revision;static char intent_kind;
static const uint32_t *seen_grants;
static void zero(const void *p,size_t n) { const unsigned char *b=p;for(size_t i=0;i<n;i++)assert(!b[i]); }
static void reset(void) {
    memset(origin,'a',64);origin[64]=0;memset(key,'b',64);key[64]=0;
    event=(LinkEvent){1,1,0,123,'P',origin,key,"alice","title","body\r\n"};
    actor=(LinkActor){7,3,NULL};health=post_failure=LINK_OK;
    enabled=readable=writable=done=on_board=match=lookup_ok=intent_ok=recover_ok=commit_ok=body_ok=1;
    version=4;suspended=menu=removed=next_fail=failures=0;
    lookups=intents=posts=recovers=body_reads=next_reads=item_count=0;stored=(LinkReference){0};
    memset(items,0,sizeof(items));saved_body[0]=saved_title[0]=saved_alias[0]=0;seen_grants=NULL;
}
LinkOutcome link_repository_health(void) { return health; }
int link_repository_enabled(uint32_t board,uint32_t user) { return enabled && board==1 && user==7; }
int link_repository_version(void) { return version; }
void link_repository_fail(void) { failures++; }
void policy_context(const uint32_t *grants) { seen_grants=grants; }
int moderation_suspended(uint32_t user) { (void)user;return suspended; }
uint32_t moderation_epoch(uint32_t user) { (void)user;return 3; }
const char *members_name(uint32_t user) { return user==7?"link":NULL; }
int catalog_read(uint32_t board,uint32_t user) { assert(board==1 && user==7);return readable; }
int catalog_write(uint32_t board,uint32_t user) { assert(board==1 && user==7);return writable; }
int policy_menu(uint32_t board) { assert(board==1);return menu; }
int link_repository_lookup(uint32_t board,const char *o,const char *k,LinkReference *out,int *complete) {
    assert(board==1 && !strcmp(o,origin) && !strcmp(k,key));lookups++;*out=stored;*complete=done;return lookup_ok;
}
int link_repository_on_board(uint32_t id,uint32_t board) { assert(id && board==1);return on_board; }
int link_repository_removed(uint32_t id) { assert(id);return removed; }
int link_repository_next(uint32_t board,uint32_t start,LinkItem *out) {
    assert(board==1);next_reads++;if(next_fail && next_reads==(unsigned)next_fail)return -1;
    for(unsigned i=0;i<item_count;i++)if(items[i].id>=start) { *out=items[i];return 1; }
    return 0;
}
int link_repository_body(uint32_t id,char *out,size_t capacity,size_t *size) {
    assert(id);body_reads++;*size=4;
    if(!body_ok || capacity<5)return 0;
    memcpy(out,"body",5);return 1;
}
int link_repository_matches(uint32_t id,const LinkEvent *e,const char *alias,const char *hash) {
    assert(id && e->kind=='C' && *alias=='@' && strlen(hash)==64);return match;
}
LinkOutcome link_repository_post(const LinkActor *a,const LinkEvent *e,const char *alias,int validate,uint32_t *id) {
    assert(a->user==7 && e->board==1 && *alias=='@');posts++;
    if(post_failure!=LINK_OK)return post_failure;
    *id=validate || commit_ok?42:43;return LINK_OK;
}
int link_repository_intent(const LinkActor *a,const LinkEvent *e,uint32_t id,const char *alias,const char *hash) {
    assert(a->user==7 && id==42 && strlen(hash)==64);intents++;
    snprintf(saved_body,sizeof(saved_body),"%s",e->body);snprintf(saved_title,sizeof(saved_title),"%s",e->title);
    snprintf(saved_alias,sizeof(saved_alias),"%s",alias);intent_revision=e->revision;intent_kind=e->kind;return intent_ok;
}
int link_repository_recover(void) {
    recovers++;if(recover_ok) { stored=(LinkReference){42,intent_revision};removed=intent_kind=='W'||intent_kind=='D'; }return recover_ok;
}
static void admission(void) {
    reset();LinkApplied out;memset(&out,0xff,sizeof(out));health=LINK_BOARD_STORAGE;
    assert(link_core_apply(&actor,&event,&out)==LINK_BOARD_STORAGE);zero(&out,sizeof(out));assert(!lookups);
    health=LINK_OK;suspended=1;assert(link_core_apply(&actor,&event,&out)==LINK_REVOKED);
    suspended=0;actor.epoch=2;assert(link_core_apply(&actor,&event,&out)==LINK_REVOKED);
    actor.epoch=3;enabled=0;assert(link_core_apply(&actor,&event,&out)==LINK_DENIED);
    enabled=1;readable=0;assert(link_core_apply(&actor,&event,&out)==LINK_DENIED);assert(!lookups);
    readable=1;writable=0;assert(link_core_apply(&actor,&event,&out)==LINK_POSTING_DENIED);assert(!intents);
    writable=1;version=3;assert(link_core_apply(&actor,&event,&out)==LINK_POSTING_DENIED);
    version=4;menu=1;assert(link_core_apply(&actor,&event,&out)==LINK_POSTING_DENIED);
    menu=0;actor.user=0;assert(link_core_apply(&actor,&event,&out)==LINK_DENIED);
    assert(link_core_apply(NULL,&event,&out)==LINK_DENIED);zero(&out,sizeof(out));
}
static void lookup_and_failure(void) {
    reset();LinkReference ref;stored=(LinkReference){42,5};
    assert(link_core_lookup(&actor,1,origin,key,&ref)==LINK_OK && ref.id==42 && ref.revision==5);
    assert(link_core_lookup(&actor,1,"bad",key,&ref)==LINK_IDENTITY);zero(&ref,sizeof(ref));
    done=0;assert(link_core_lookup(&actor,1,origin,key,&ref)==LINK_STORAGE);zero(&ref,sizeof(ref));assert(failures==1);
    done=1;lookup_ok=0;assert(link_core_lookup(&actor,1,origin,key,&ref)==LINK_STORAGE);zero(&ref,sizeof(ref));
}
static void add_items(unsigned count) {
    item_count=count;
    for(unsigned i=0;i<count;i++) { items[i].id=i+1;items[i].parent=i;items[i].created=123;strcpy(items[i].title,"secret title");strcpy(items[i].origin,origin);strcpy(items[i].key,key); }
}
static void visibility_and_pages(void) {
    reset();add_items(34);LinkPage page;char body[16];uint32_t grants[2]={0,9};actor.grants=grants;
    assert(link_core_scan(&actor,1,LINK_BATCH,0,&page,body,sizeof(body))==LINK_OK && page.count==32 && !page.end);
    assert(page.items[31].id==32 && !body_reads && seen_grants==grants);
    assert(link_core_scan(&actor,1,LINK_BATCH,33,&page,body,sizeof(body))==LINK_OK && page.count==2 && page.end);
    items[0].state=LINK_WITHDRAWN;items[1].state=LINK_HIDDEN;
    for(unsigned id=1;id<=2;id++) {
        memset(body,'x',sizeof(body));assert(link_core_scan(&actor,1,LINK_SNAPSHOT,id,&page,body,sizeof(body))==LINK_OK);
        assert(page.count==1 && page.items[0].id==id && !*page.items[0].title && !page.body_size);zero(body,sizeof(body));
    }
    assert(!body_reads);
    assert(link_core_scan(&actor,1,LINK_SNAPSHOT,3,&page,body,sizeof(body))==LINK_OK && page.body_size==4 && !strcmp(body,"body"));
    next_reads=0;next_fail=2;
    assert(link_core_scan(&actor,1,LINK_BATCH,1,&page,body,sizeof(body))==LINK_STORAGE);zero(&page,sizeof(page));zero(body,sizeof(body));
    next_fail=0;body_ok=0;
    assert(link_core_scan(&actor,1,LINK_SNAPSHOT,3,&page,body,sizeof(body))==LINK_STORAGE);zero(&page,sizeof(page));zero(body,sizeof(body));
}
static void revisions_and_immutability(void) {
    reset();LinkApplied out;event.revision=0;assert(link_core_apply(&actor,&event,&out)==LINK_REVISION);
    event.revision=2;assert(link_core_apply(&actor,&event,&out)==LINK_PREDECESSOR);
    event.revision=1;event.kind='T';assert(link_core_apply(&actor,&event,&out)==LINK_PREDECESSOR);
    stored=(LinkReference){42,2};event.revision=1;event.kind='P';removed=1;
    assert(link_core_apply(&actor,&event,&out)==LINK_OK && out.reference.revision==2 && !out.created && !intents);
    event.revision=3;event.kind='T';assert(link_core_apply(&actor,&event,&out)==LINK_UNAVAILABLE);zero(&out,sizeof(out));
    removed=0;event.kind='C';event.revision=8;match=0;
    assert(link_core_apply(&actor,&event,&out)==LINK_CONFLICT && !intents);
    match=-1;assert(link_core_apply(&actor,&event,&out)==LINK_STORAGE && !intents);
    match=1;assert(link_core_apply(&actor,&event,&out)==LINK_OK && out.reference.revision==8 && !out.created && !posts);
}
static void create_and_tombstone(void) {
    reset();LinkApplied out;event.parent=1;on_board=0;
    assert(link_core_apply(&actor,&event,&out)==LINK_PARENT && !posts);
    on_board=1;assert(link_core_apply(&actor,&event,&out)==LINK_OK && out.created && out.reference.id==42);
    assert(posts==2 && intents==1 && recovers==1 && !strcmp(saved_alias,"@aaaaaaaa:alice"));
    reset();event.kind='W';event.revision=19;
    assert(link_core_apply(&actor,&event,&out)==LINK_OK && out.created && out.reference.revision==19);
    assert(!strcmp(saved_title,"Withdrawn") && !strcmp(saved_body,"[Article withdrawn at origin]\r\n"));
    assert(link_core_apply(&actor,&event,&out)==LINK_OK && !out.created && intents==1);
    event.revision=20;event.kind='C';assert(link_core_apply(&actor,&event,&out)==LINK_UNAVAILABLE);
    event.kind='W';assert(link_core_apply(&actor,&event,&out)==LINK_OK && !out.created && posts==2);
}
static void durable_failures_and_validation(void) {
    LinkApplied out;
    reset();post_failure=LINK_POST_FULL;assert(link_core_apply(&actor,&event,&out)==LINK_POST_FULL && !intents && !failures);zero(&out,sizeof(out));
    reset();intent_ok=0;assert(link_core_apply(&actor,&event,&out)==LINK_STORAGE && posts==1 && !recovers);zero(&out,sizeof(out));
    reset();commit_ok=0;assert(link_core_apply(&actor,&event,&out)==LINK_STORAGE && intents==1 && !recovers);zero(&out,sizeof(out));
    reset();recover_ok=0;assert(link_core_apply(&actor,&event,&out)==LINK_STORAGE && recovers==1);zero(&out,sizeof(out));
    reset();event.title="bad\ntitle";assert(link_core_apply(&actor,&event,&out)==LINK_TITLE && !intents);
    event.title="title";event.author="bad\nauthor";assert(link_core_apply(&actor,&event,&out)==LINK_ACTION && !intents);
    event.author="alice";event.body="bad\001body";assert(link_core_apply(&actor,&event,&out)==LINK_ACTION && !intents);
    event.body="body";event.created=0;assert(link_core_apply(&actor,&event,&out)==LINK_ACTION);
    event.created=123;event.kind='X';assert(link_core_apply(&actor,&event,&out)==LINK_PREDECESSOR);
    char digest[65];assert(link_content_hash("abc",digest));assert(!strcmp(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}
int main(void) {
    admission();lookup_and_failure();visibility_and_pages();revisions_and_immutability();create_and_tombstone();durable_failures_and_validation();
    puts("PASS: 6 link Core groups (admission, lookup, visibility/pages, revisions, checkpoints, durable failures)");return 0;
}

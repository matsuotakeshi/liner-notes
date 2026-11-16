/* Exercise Core directly: no Session, UI, transport, socket or live DB.
 * Replace only existing persistence/identity boundaries with controlled state. */
#include "article_core.h"
#include "article_repository.h"
#include "members.h"
#include "catalog.h"
#include "moderation.h"
#include "board_policy.h"
#include "extras.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ArticleMetadata stored;
static const uint32_t *context;
static uint32_t epoch,grants[1025];
static int member_error,moderation_error,policy_error,extras_error,suspended;
static int readable,writable,manager,hidden,imported,commit_ok,locked,grant_revision;
static unsigned commits,publications,body_reads;
static int removed,body_ok;
static char persisted_body[64];
static char journal_kind,journal_text[81];
static uint32_t journal_actor;
static uint32_t moderation_entries,extras_entries;
static unsigned moderation_writes;
static int local_hidden,moderation_commit_ok;
static int paging,indexed;
static uint32_t page_count,missing_id,visibility_error,body_error,bad_candidate;
static ArticleMetadata page_items[301];
static char page_bodies[301][32];
static unsigned char page_hidden[301],candidates[301];
static unsigned body_probes[301],candidate_calls;
static uint32_t tree_next_ids[301];
static unsigned tree_steps;
static unsigned char read_flags[4][301];
static unsigned mark_calls,seen_calls;
static uint32_t seen_error,mark_user,mark_id;
static int mark_ok,mark_value;

int members_seen(uint32_t user,uint32_t id) {
    assert(user>0 && user<4 && id>0 && id<=300 && paging);
    assert(page_items[id].board==2 && !page_hidden[id]);seen_calls++;
    if(id==seen_error)member_error=1;
    return read_flags[user][id];
}
int members_mark(uint32_t user,uint32_t id,int seen) {
    assert(user>0 && user<4 && id>0 && id<=300 && (seen==0 || seen==1));
    assert(page_items[id].board==2 && !page_hidden[id] && catalog_read(2,user));
    mark_calls++;mark_user=user;mark_id=id;mark_value=seen;
    if(!mark_ok)return 0;
    read_flags[user][id]=(unsigned char)seen;return 1;
}

uint32_t article_repository_tree_next(uint32_t id) {
    assert(paging && id && id<=page_count && id<=300);tree_steps++;return tree_next_ids[id];
}

uint32_t article_repository_count(void) { return page_count; }
uint32_t article_repository_search_next(uint32_t start,const char *query) {
    (void)query;candidate_calls++;
    if(bad_candidate)return bad_candidate;
    if(!indexed)return start;
    for(uint32_t i=start;i<=page_count;i++)if(candidates[i])return i;
    return 0;
}
int article_repository_body_contains(uint32_t id,const char *query) {
    assert(paging && id && id<=page_count && page_items[id].board==2 && !page_hidden[id]);
    assert(catalog_read(2,0));body_probes[id]++;
    return id==body_error?-1:strstr(page_bodies[id],query)!=NULL;
}

uint32_t moderation_count(void) { return moderation_entries; }
uint32_t extras_audit_count(void) { return extras_entries; }
int moderation_set(uint32_t id,uint32_t board,uint32_t actor,int hide) {
    assert(id==7 && board==2 && actor && catalog_manage(board,actor));
    assert(catalog_read(board,actor) && !removed && (hide==0 || hide==1));
    moderation_writes++;journal_actor=actor;
    if(!moderation_commit_ok)return 0;
    local_hidden=hide;return 1;
}

int members_broken(void) { return member_error; }
int moderation_broken(void) { return moderation_error; }
int policy_broken(void) { return policy_error; }
int extras_broken(void) { return extras_error; }
const char *members_name(uint32_t user) {
    return user==1?"alice":user==2?"bob":user==3?"guest":NULL;
}
int moderation_suspended(uint32_t user) { (void)user;return suspended; }
uint32_t moderation_epoch(uint32_t user) { (void)user;return epoch; }
void policy_context(const uint32_t *p) { context=p; }
int catalog_read(uint32_t board,uint32_t user) {
    (void)user;
    return board==2 && readable && (!locked || (context && context[2]==(uint32_t)grant_revision));
}
int catalog_manage(uint32_t board,uint32_t user) { return board==2 && user && manager; }
int catalog_write(uint32_t board,uint32_t user) { return catalog_read(board,user) && writable; }
int article_repository_find(uint32_t id,ArticleMetadata *out) {
    if(paging) {
        if(!id || id>page_count || id==missing_id)return 0;
        assert(id<=300);*out=page_items[id];return 1;
    }
    if(id!=7)return 0;
    *out=stored;return 1;
}
int article_repository_unavailable(uint32_t id) {
    if(paging) { assert(id && id<=page_count);return id==visibility_error?-1:page_hidden[id]; }
    assert(id==7);return hidden;
}
int article_repository_imported(uint32_t id) { assert(id==7);return imported; }
int extras_removed(uint32_t id) { assert(id==7);return removed; }
int article_repository_body(uint32_t id,char *body,size_t capacity) {
    assert(id==7 && capacity>strlen(persisted_body));body_reads++;
    strcpy(body,persisted_body);return body_ok;
}
int extras_set(char kind,uint32_t id,uint32_t actor,const char *text) {
    assert(id==7);commits++;journal_kind=kind;journal_actor=actor;
    assert(strlen(text)<=80);strcpy(journal_text,text);return commit_ok;
}
void article_repository_title_changed(uint32_t id,const char *title) {
    assert(id==7 && commits && commit_ok && journal_kind=='T');
    assert(!strcmp(title,journal_text));publications++;
}
static ArticleActor reset(void) {
    paging=0;
    memset(read_flags,0,sizeof(read_flags));mark_calls=seen_calls=seen_error=mark_user=mark_id=0;mark_ok=1;mark_value=0;
    memset(&stored,0,sizeof(stored));stored.board=2;stored.parent=5;strcpy(stored.author,"alice");
    strcpy(stored.title,"article-title");strcpy(persisted_body,"secret body\r\n");
    stored.body_size=(uint32_t)strlen(persisted_body);removed=0;body_ok=1;body_reads=0;
    memset(grants,0,sizeof(grants));context=NULL;epoch=8;
    member_error=moderation_error=policy_error=extras_error=suspended=0;
    readable=writable=commit_ok=1;manager=hidden=imported=locked=0;grant_revision=9;
    commits=publications=0;journal_kind=0;journal_actor=0;journal_text[0]=0;
    moderation_entries=extras_entries=moderation_writes=0;
    local_hidden=0;moderation_commit_ok=1;
    ArticleActor actor={1,epoch,grants};return actor;
}
static void denied(ArticleOutcome got,ArticleOutcome wanted) {
    assert(got==wanted && commits==0 && publications==0 && !moderation_writes && !mark_calls && context==NULL);
}
static void cleared(const ArticleView *view,const char *body,size_t size) {
    const unsigned char *p=(const unsigned char *)view;
    for(size_t i=0;i<sizeof(*view);i++)assert(p[i]==0);
    for(size_t i=0;i<size;i++)assert(body[i]==0);
    assert(!commits && !publications && !context);
}
static void tombstone(const ArticleView *view,const char *body,size_t size,ArticleViewState state) {
    assert(view->id==7 && view->board==2 && view->parent==5 && view->state==state);
    assert(!*view->author && !*view->title && !view->body_size && !view->inspected_hidden);
    for(size_t i=0;i<size;i++)assert(body[i]==0);
    assert(!body_reads && !commits && !publications && !context);
}
static void read_tests(void) {
    ArticleActor actor=reset();ArticleView view;char body[64];
    assert(article_read(NULL,7,0,&view,body,sizeof(body))==ARTICLE_OK);
    assert(view.id==7 && view.board==2 && view.parent==5 && view.state==ARTICLE_VISIBLE);
    assert(!strcmp(view.author,"alice") && !strcmp(view.title,"article-title"));
    assert(!strcmp(body,persisted_body) && view.body_size==strlen(body) && body_reads==1);
    assert(!view.inspected_hidden && !context && !commits && !publications);
    strcpy(stored.title,"changed");strcpy(stored.author,"changed");persisted_body[0]='X';
    assert(!strcmp(view.title,"article-title") && !strcmp(view.author,"alice") && body[0]=='s');
    readable=0;
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_NOT_FOUND);
    cleared(&view,body,sizeof(body));assert(body_reads==1);

    actor=reset();manager=1;readable=0;
    assert(article_read(&actor,7,1,&view,body,sizeof(body))==ARTICLE_NOT_FOUND);
    cleared(&view,body,sizeof(body));assert(!body_reads);
    readable=1;manager=0;
    assert(article_read(&actor,7,1,&view,body,sizeof(body))==ARTICLE_ADMIN_REQUIRED);
    cleared(&view,body,sizeof(body));assert(!body_reads);
    manager=1;
    assert(article_read(NULL,7,1,&view,body,sizeof(body))==ARTICLE_ADMIN_REQUIRED);
    cleared(&view,body,sizeof(body));assert(!body_reads);
    assert(article_read(&actor,999,0,&view,body,sizeof(body))==ARTICLE_NOT_FOUND);
    cleared(&view,body,sizeof(body));assert(!body_reads);

    actor=reset();hidden=1;manager=1;
    memset(body,'x',sizeof(body));memset(&view,0xff,sizeof(view));
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_OK);
    tombstone(&view,body,sizeof(body),ARTICLE_CONCEALED);
    assert(article_read(&actor,7,1,&view,body,sizeof(body))==ARTICLE_OK);
    assert(view.state==ARTICLE_VISIBLE && view.inspected_hidden && body_reads==1);
    assert(!strcmp(body,persisted_body));
    /* A withdrawn parent remains a tombstone even for an authorized inspector. */
    removed=1;body_reads=0;
    assert(article_read(&actor,7,1,&view,body,sizeof(body))==ARTICLE_OK);
    tombstone(&view,body,sizeof(body),ARTICLE_WITHDRAWN);
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_OK);
    tombstone(&view,body,sizeof(body),ARTICLE_WITHDRAWN);

    actor=reset();locked=1;grants[2]=9;
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_OK);
    grant_revision++;
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_NOT_FOUND);
    cleared(&view,body,sizeof(body));assert(body_reads==1);
    actor=reset();epoch++;
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_REVOKED);
    cleared(&view,body,sizeof(body));assert(!body_reads);
    actor=reset();suspended=1;
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_REVOKED);
    cleared(&view,body,sizeof(body));assert(!body_reads);
    actor=reset();actor.user=99;
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_REVOKED);
    cleared(&view,body,sizeof(body));assert(!body_reads);

    actor=reset();
    assert(article_read(&actor,7,0,&view,NULL,0)==ARTICLE_BUFFER_TOO_SMALL);
    cleared(&view,body,0);assert(!body_reads);
    assert(article_read(&actor,7,0,&view,body,stored.body_size)==ARTICLE_BUFFER_TOO_SMALL);
    cleared(&view,body,stored.body_size);assert(!body_reads);
    assert(article_read(&actor,7,0,&view,body,stored.body_size+1)==ARTICLE_OK);
    assert(!strcmp(body,persisted_body));
    assert(article_read(&actor,7,0,NULL,body,sizeof(body))==ARTICLE_INVALID_READ);
    for(size_t i=0;i<sizeof(body);i++)assert(!body[i]);
    actor=reset();stored.body_size=CONTENT_BODY+1;
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_STORAGE);
    cleared(&view,body,sizeof(body));assert(!body_reads);
    actor=reset();body_ok=0;
    assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_STORAGE);
    cleared(&view,body,sizeof(body));assert(body_reads==1);
    actor=reset();hidden=-1;
    assert(article_read(&actor,7,1,&view,body,sizeof(body))==ARTICLE_ADMIN_REQUIRED);
    manager=1;
    assert(article_read(&actor,7,1,&view,body,sizeof(body))==ARTICLE_STORAGE);
    cleared(&view,body,sizeof(body));assert(!body_reads);
    denied(article_retitle(&actor,2,7,"new"),ARTICLE_STORAGE);

    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome expected[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,
                               ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        actor=reset();*errors[i]=1;memset(body,'x',sizeof(body));
        assert(article_read(&actor,7,0,&view,body,sizeof(body))==expected[i]);
        cleared(&view,body,sizeof(body));assert(!body_reads);
    }
    puts("PASS: structured reads, guest/inspection ACL, tombstone non-disclosure, owned output and failure cleansing");
}
static void moderation_tests(void) {
    typedef ArticleOutcome (*Moderate)(const ArticleActor *,uint32_t,uint32_t);
    Moderate operations[]={article_hide,article_restore};
    for(unsigned op=0;op<2;op++) {
        Moderate apply=operations[op];ArticleActor actor=reset();
        denied(apply(NULL,2,7),ARTICLE_LOGIN_REQUIRED);
        actor.user=0;denied(apply(&actor,2,7),ARTICLE_LOGIN_REQUIRED);
        actor.user=99;denied(apply(&actor,2,7),ARTICLE_REVOKED);
        actor=reset();epoch++;denied(apply(&actor,2,7),ARTICLE_REVOKED);
        actor=reset();suspended=1;denied(apply(&actor,2,7),ARTICLE_REVOKED);
        actor=reset();
        /* Authorship alone never grants local moderation authority. */
        denied(apply(&actor,2,7),ARTICLE_ADMIN_REQUIRED);
        manager=1;
        denied(apply(&actor,2,0),ARTICLE_NOT_FOUND);
        denied(apply(&actor,2,8),ARTICLE_NOT_FOUND);
        denied(apply(&actor,3,7),ARTICLE_ADMIN_REQUIRED);
        stored.board=3;denied(apply(&actor,2,7),ARTICLE_NOT_FOUND);
        stored.board=2;readable=0;denied(apply(&actor,2,7),ARTICLE_NOT_FOUND);
        readable=1;locked=1;denied(apply(&actor,2,7),ARTICLE_NOT_FOUND);
        grants[2]=9;grant_revision++;denied(apply(&actor,2,7),ARTICLE_NOT_FOUND);
        locked=0;manager=0;denied(apply(&actor,2,7),ARTICLE_ADMIN_REQUIRED);

        actor=reset();manager=1;removed=1;imported=1;
        denied(apply(&actor,2,7),ARTICLE_WITHDRAWN_TARGET);
        imported=0;denied(apply(&actor,2,7),ARTICLE_WITHDRAWN_TARGET);
        for(unsigned i=0;i<3;i++) {
            actor=reset();manager=1;
            moderation_entries=i?UINT32_MAX-2:UINT32_MAX;
            extras_entries=i==1?2:i==2?UINT32_MAX:0;
            denied(apply(&actor,2,7),ARTICLE_MODERATION_WRITE);
        }
        int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
        ArticleOutcome expected[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,
                                   ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
        for(unsigned i=0;i<4;i++) {
            actor=reset();manager=1;*errors[i]=1;denied(apply(&actor,2,7),expected[i]);
        }
        /* Imported/local articles, including hidden descendants, use only the
         * local moderation journal. No origin lookup/withdrawal event needed. */
        for(int origin=-1;origin<=1;origin++) {
            actor=reset();actor.user=2;manager=1;writable=0;imported=origin;hidden=1;
            moderation_entries=UINT32_MAX-3;extras_entries=1;
            local_hidden=(int)op;
            assert(apply(&actor,2,7)==ARTICLE_OK);
            assert(moderation_writes==1 && local_hidden==(op==0) && journal_actor==2);
            assert(!commits && !publications && !body_reads && !context && !removed);
            /* Restore does not override the visibility of an ancestor. */
            ArticleView view;char body[64];
            assert(article_read(&actor,7,0,&view,body,sizeof(body))==ARTICLE_OK);
            tombstone(&view,body,sizeof(body),ARTICLE_CONCEALED);
        }
        actor=reset();manager=1;moderation_commit_ok=0;local_hidden=(int)op;
        assert(apply(&actor,2,7)==ARTICLE_MODERATION_WRITE);
        assert(local_hidden==(int)op && moderation_writes==1 && !context);
        assert(!commits && !publications && !body_reads);
    }
    puts("PASS: local moderation authority, revoked identity/grants, withdrawn rejection, audit bounds and write failure");
}
static ArticleActor page_reset(uint32_t count) {
    ArticleActor actor=reset();paging=1;indexed=0;page_count=count;
    missing_id=visibility_error=body_error=bad_candidate=candidate_calls=0;
    memset(page_items,0,sizeof(page_items));memset(page_hidden,0,sizeof(page_hidden));
    memset(candidates,0,sizeof(candidates));memset(body_probes,0,sizeof(body_probes));
    memset(page_bodies,0,sizeof(page_bodies));
    for(uint32_t i=1;i<=count;i++) {
        page_items[i].board=2;snprintf(page_items[i].title,sizeof(page_items[i].title),"row %u",i);
        strcpy(page_bodies[i],"ordinary body");
    }
    return actor;
}
static ArticleActor tree_reset(uint32_t count) {
    ArticleActor actor=page_reset(count);memset(tree_next_ids,0,sizeof(tree_next_ids));tree_steps=0;
    for(uint32_t i=1;i<=count;i++) {
        page_items[i].root=1;page_items[i].depth=i==1?0:1;page_items[i].parent=i==1?0:1;
        tree_next_ids[i]=i<count?i+1:0;
    }
    return actor;
}
static void tree_cleared(const ArticleTreePage *page) {
    const unsigned char *p=(const unsigned char *)page;
    for(size_t i=0;i<sizeof(*page);i++)assert(!p[i]);
    assert(!context && !commits && !moderation_writes && !publications && !body_reads);
    for(unsigned i=0;i<=300;i++)assert(!body_probes[i]);
}
static void tree_tests(void) {
    ArticleTreePage page;ArticleActor actor=tree_reset(6);
    tree_next_ids[2]=4;tree_next_ids[4]=3;tree_next_ids[3]=6;
    page_items[4].parent=2;page_items[4].depth=2;page_items[5].root=5;
    assert(article_tree(NULL,2,4,0,&page)==ARTICLE_OK);
    assert(page.root==1 && page.count==5 && !page.next && !context);
    uint32_t expected[]={1,2,4,3,6};
    for(unsigned i=0;i<5;i++)assert(page.rows[i].summary.id==expected[i]);
    assert(page.rows[2].summary.parent==2 && page.rows[2].depth==2);
    strcpy(page_items[4].title,"changed");assert(!strcmp(page.rows[2].summary.title,"row 4"));
    page_hidden[2]=page_hidden[4]=1;
    assert(article_tree(&actor,2,4,2,&page)==ARTICLE_OK);
    assert(page.root==1 && page.count==2 && page.rows[0].summary.id==3 && page.rows[1].summary.id==6);
    assert(article_tree(&actor,2,1,5,&page)==ARTICLE_INVALID_CURSOR);tree_cleared(&page);
    assert(article_tree(&actor,2,1,7,&page)==ARTICLE_INVALID_CURSOR);tree_cleared(&page);
    assert(article_tree(&actor,2,1,UINT32_MAX,&page)==ARTICLE_INVALID_CURSOR);tree_cleared(&page);
    assert(article_tree(&actor,2,0,0,&page)==ARTICLE_NOT_FOUND);tree_cleared(&page);
    assert(article_tree(&actor,2,7,0,&page)==ARTICLE_NOT_FOUND);tree_cleared(&page);
    page_items[6].board=3;
    assert(article_tree(&actor,2,6,0,&page)==ARTICLE_NOT_FOUND);tree_cleared(&page);
    assert(article_tree(&actor,2,1,6,&page)==ARTICLE_INVALID_CURSOR);tree_cleared(&page);
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);

    actor=tree_reset(12);page_hidden[9]=page_hidden[10]=1;
    page_items[8].depth=10000;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_OK);
    assert(page.count==8 && page.next==11 && page.rows[7].depth==10000);
    assert(article_tree(&actor,2,1,page.next,&page)==ARTICLE_OK);
    assert(page.count==2 && page.rows[0].summary.id==11 && !page.next);
    page_hidden[11]=1;
    assert(article_tree(&actor,2,1,11,&page)==ARTICLE_OK);
    assert(page.count==1 && page.rows[0].summary.id==12 && !page.next);
    page_hidden[12]=1;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_OK);
    assert(page.count==8 && !page.next);
    memset(page_hidden,1,sizeof(page_hidden));
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_OK);
    assert(page.root==1 && !page.count && !page.next);
    for(unsigned i=0;i<8;i++)assert(!page.rows[i].summary.id && !page.rows[i].summary.title[0]);
    page_hidden[1]=0;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_OK);assert(page.count==1);

    actor=tree_reset(3);epoch++;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_REVOKED);tree_cleared(&page);
    epoch--;suspended=1;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_REVOKED);tree_cleared(&page);
    suspended=0;actor.user=99;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_REVOKED);tree_cleared(&page);
    actor.user=1;manager=1;readable=0;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_BOARD_DENIED);tree_cleared(&page);
    readable=1;locked=1;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_BOARD_DENIED);tree_cleared(&page);
    grants[2]=9;assert(article_tree(&actor,2,1,0,&page)==ARTICLE_OK);
    grant_revision++;
    assert(article_tree(&actor,2,1,2,&page)==ARTICLE_BOARD_DENIED);tree_cleared(&page);
    locked=0;
    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,
                             ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        *errors[i]=1;assert(article_tree(&actor,2,1,0,&page)==wanted[i]);tree_cleared(&page);*errors[i]=0;
    }
    actor=tree_reset(12);visibility_error=9;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);
    visibility_error=0;missing_id=2;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);
    missing_id=0;page_items[2].root=2;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);
    page_items[2].root=1;tree_next_ids[2]=13;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);
    tree_next_ids[2]=1;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);
    memset(page_hidden,1,sizeof(page_hidden));tree_steps=0;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);
    assert(tree_steps==12);
    page_items[1].root=0;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);
    page_count=UINT32_MAX;
    assert(article_tree(&actor,2,1,0,&page)==ARTICLE_STORAGE);tree_cleared(&page);
    assert(article_tree(&actor,2,1,0,NULL)==ARTICLE_INVALID_READ);
    puts("PASS: structured tree DFS, thread/board cursor bounds, hidden pages, revoked grants and partial-result clearing");
}
static void page_cleared(const ArticlePage *page) {
    const unsigned char *p=(const unsigned char *)page;
    for(size_t i=0;i<sizeof(*page);i++)assert(!p[i]);
    assert(!context && !commits && !moderation_writes && !publications);
}
static void page_tests(void) {
    ArticlePage page;ArticleActor actor=page_reset(9);
    page_hidden[2]=page_hidden[5]=1;page_items[4].board=3;page_items[9].parent=1;
    assert(article_list(NULL,2,0,&page)==ARTICLE_OK);
    assert(page.total==6 && page.count==5 && page.next==9 && !context);
    uint32_t expected[]={1,3,6,7,8};
    for(unsigned i=0;i<5;i++)assert(page.rows[i].id==expected[i] && page.rows[i].board==2);
    assert(!strcmp(page.rows[0].title,"row 1"));strcpy(page_items[1].title,"changed");
    assert(!strcmp(page.rows[0].title,"row 1"));
    assert(article_list(&actor,2,page.next,&page)==ARTICLE_OK);
    assert(page.count==1 && !page.next && page.rows[0].id==9 && page.rows[0].parent==1);
    assert(article_list(&actor,2,UINT32_MAX,&page)==ARTICLE_OK);
    assert(page.total==6 && !page.count && !page.next);
    for(unsigned i=1;i<=9;i++)assert(!body_probes[i]);
    actor=page_reset(0);
    assert(article_list(NULL,2,0,&page)==ARTICLE_OK);page_cleared(&page);
    assert(article_search(NULL,2,0,"needle",&page)==ARTICLE_OK);page_cleared(&page);

    actor=page_reset(10);
    for(unsigned i=1;i<=10;i++)strcpy(page_items[i].title,"needle");
    assert(article_search(NULL,2,0,"needle",&page)==ARTICLE_OK);
    assert(!page.total && page.count==8 && page.next==9 && candidate_calls==8);
    assert(article_search(&actor,2,page.next,"needle",&page)==ARTICLE_OK);
    assert(page.count==2 && !page.next && page.rows[0].id==9 && page.rows[1].id==10);
    assert(article_search(&actor,2,11,"needle",&page)==ARTICLE_OK);page_cleared(&page);
    assert(article_search(&actor,2,12,"needle",&page)==ARTICLE_INVALID_CURSOR);page_cleared(&page);
    assert(article_search(&actor,2,UINT32_MAX,"needle",&page)==ARTICLE_INVALID_CURSOR);page_cleared(&page);
    const char *bad[]={NULL,"","bad\tquery","bad\177query"};
    for(unsigned i=0;i<4;i++) {
        assert(article_search(&actor,2,0,bad[i],&page)==ARTICLE_INVALID_QUERY);page_cleared(&page);
    }
    char query[82];memset(query,'x',81);query[81]=0;
    assert(article_search(&actor,2,0,query,&page)==ARTICLE_INVALID_QUERY);page_cleared(&page);
    query[80]=0;strcpy(page_items[1].title,query);
    assert(article_search(&actor,2,0,query,&page)==ARTICLE_OK);
    assert(page.count==1 && page.rows[0].id==1);

    actor=page_reset(4);strcpy(page_items[1].title,"needle");strcpy(page_bodies[2],"a needle here");
    strcpy(page_bodies[3],"needle");page_hidden[3]=1;
    strcpy(page_bodies[4],"needle");page_items[4].board=3;
    assert(article_search(&actor,2,0,"needle",&page)==ARTICLE_OK);
    assert(page.count==2 && page.rows[0].id==1 && page.rows[1].id==2 && !page.next);
    assert(!body_probes[1] && body_probes[2]==1 && !body_probes[3] && !body_probes[4]);
    assert(article_search(&actor,2,0,"Needle",&page)==ARTICLE_OK);page_cleared(&page);
    page_hidden[1]=page_hidden[2]=1;
    assert(article_search(&actor,2,0,"needle",&page)==ARTICLE_OK);page_cleared(&page);
    page_hidden[2]=0;
    assert(article_search(&actor,2,0,"needle",&page)==ARTICLE_OK);
    assert(page.count==1 && page.rows[0].id==2);

    actor=page_reset(300);strcpy(page_items[300].title,"x");
    assert(article_search(&actor,2,0,"x",&page)==ARTICLE_OK);
    assert(!page.count && page.next==257 && candidate_calls==256);
    assert(article_search(&actor,2,page.next,"x",&page)==ARTICLE_OK);
    assert(page.count==1 && !page.next && page.rows[0].id==300 && candidate_calls==300);
    indexed=1;candidate_calls=0;candidates[275]=candidates[300]=1;
    strcpy(page_bodies[275],"needle");strcpy(page_items[300].title,"needle");
    assert(article_search(&actor,2,1,"needle",&page)==ARTICLE_OK);
    assert(page.count==2 && !page.next && page.rows[0].id==275 && page.rows[1].id==300);
    assert(candidate_calls==2);
    bad_candidate=2;
    assert(article_search(&actor,2,3,"needle",&page)==ARTICLE_STORAGE);page_cleared(&page);
    bad_candidate=301;
    assert(article_search(&actor,2,3,"needle",&page)==ARTICLE_STORAGE);page_cleared(&page);

    for(int search=0;search<2;search++) {
        actor=page_reset(3);strcpy(page_items[1].title,"needle");
        #define PAGE() (search?article_search(&actor,2,0,"needle",&page):article_list(&actor,2,0,&page))
        assert(PAGE()==ARTICLE_OK);
        epoch++;assert(PAGE()==ARTICLE_REVOKED);page_cleared(&page);
        epoch--;suspended=1;assert(PAGE()==ARTICLE_REVOKED);page_cleared(&page);
        suspended=0;actor.user=99;assert(PAGE()==ARTICLE_REVOKED);page_cleared(&page);
        actor.user=1;manager=1;readable=0;
        assert(PAGE()==ARTICLE_BOARD_DENIED);page_cleared(&page);
        readable=1;locked=1;assert(PAGE()==ARTICLE_BOARD_DENIED);page_cleared(&page);
        grants[2]=9;assert(PAGE()==ARTICLE_OK);
        grant_revision++;assert(PAGE()==ARTICLE_BOARD_DENIED);page_cleared(&page);
        locked=0;visibility_error=2;
        assert(PAGE()==ARTICLE_STORAGE);page_cleared(&page);
        visibility_error=0;missing_id=2;
        assert(PAGE()==ARTICLE_STORAGE);page_cleared(&page);
        missing_id=0;page_count=UINT32_MAX;
        assert(PAGE()==ARTICLE_STORAGE);page_cleared(&page);page_count=3;
        int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
        ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,
                                 ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
        for(unsigned i=0;i<4;i++) {
            *errors[i]=1;assert(PAGE()==wanted[i]);page_cleared(&page);*errors[i]=0;
        }
        #undef PAGE
    }
    actor=page_reset(2);strcpy(page_items[1].title,"needle");body_error=2;
    assert(article_search(&actor,2,0,"needle",&page)==ARTICLE_STORAGE);page_cleared(&page);
    assert(article_list(&actor,2,0,NULL)==ARTICLE_INVALID_READ);
    assert(article_search(&actor,2,0,"needle",NULL)==ARTICLE_INVALID_READ);
    puts("PASS: structured pages, visible totals, pagination/index bounds, ACL/grant revocation and failure non-disclosure");
}
static void unread_cleared(const ArticleUnreadPage *page) {
    const unsigned char *p=(const unsigned char *)page;
    for(size_t i=0;i<sizeof(*page);i++)assert(!p[i]);
    assert(!context && !commits && !moderation_writes && !publications && !body_reads && !mark_calls);
    for(unsigned i=0;i<=300;i++)assert(!body_probes[i]);
}
static void unread_tests(void) {
    ArticleUnreadPage page;ArticleActor actor=tree_reset(12);
    page_hidden[2]=1;read_flags[1][3]=1;page_items[4].board=3;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_OK);
    assert(page.total==9 && page.first==1 && page.count==8 && page.next==12);
    uint32_t expected[]={1,5,6,7,8,9,10,11};
    for(unsigned i=0;i<8;i++)assert(page.rows[i].summary.id==expected[i] && page.rows[i].root==1);
    strcpy(page_items[1].title,"changed");assert(!strcmp(page.rows[0].summary.title,"row 1"));
    assert(article_unread(&actor,2,0,page.next,&page)==ARTICLE_OK);
    assert(page.total==9 && page.first==1 && page.count==1 && !page.next && page.rows[0].summary.id==12);
    assert(article_unread(&actor,2,0,UINT32_MAX,&page)==ARTICLE_OK);
    assert(page.total==9 && page.first==1 && !page.count && !page.next && !mark_calls);
    for(unsigned i=6;i<=8;i++)page_items[i].root=6;
    assert(article_unread(&actor,2,7,0,&page)==ARTICLE_OK);
    assert(page.total==3 && page.first==6 && page.count==3 && !page.next);
    assert(article_mark_seen(&actor,2,6,2)==ARTICLE_OK);
    assert(mark_calls==1 && mark_user==1 && mark_id==6 && mark_value==1 && !context);
    assert(article_unread(&actor,2,7,0,&page)==ARTICLE_OK);assert(page.total==2 && page.first==7 && mark_calls==1);
    actor.user=2;
    assert(article_unread(&actor,2,7,0,&page)==ARTICLE_OK);assert(page.total==3 && page.first==6);
    assert(article_mark_seen(&actor,2,7,1)==ARTICLE_OK);
    assert(read_flags[2][7] && !read_flags[1][7] && mark_user==2);
    actor.user=1;writable=0;
    assert(article_mark_seen(&actor,2,6,0)==ARTICLE_OK);
    assert(!read_flags[1][6] && read_flags[2][7] && !mark_value);
    assert(article_unread(&actor,2,6,0,&page)==ARTICLE_OK);assert(page.total==3 && page.first==6);

    actor=tree_reset(2);memset(page_hidden,1,sizeof(page_hidden));
    assert(article_unread(&actor,2,1,0,&page)==ARTICLE_OK);unread_cleared(&page);assert(!seen_calls);
    actor=tree_reset(0);
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_OK);unread_cleared(&page);
    actor=tree_reset(3);memset(read_flags[1],1,sizeof(read_flags[1]));
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_OK);unread_cleared(&page);

    actor=tree_reset(3);
    assert(article_unread(NULL,2,0,0,&page)==ARTICLE_LOGIN_REQUIRED);unread_cleared(&page);
    denied(article_mark_seen(NULL,2,1,1),ARTICLE_LOGIN_REQUIRED);
    actor.user=0;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_LOGIN_REQUIRED);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,1,1),ARTICLE_LOGIN_REQUIRED);
    actor.user=99;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_REVOKED);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,1,1),ARTICLE_REVOKED);
    actor.user=1;epoch++;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_REVOKED);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,1,1),ARTICLE_REVOKED);
    epoch--;suspended=1;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_REVOKED);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,1,1),ARTICLE_REVOKED);
    suspended=0;readable=0;manager=1;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_BOARD_DENIED);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,1,1),ARTICLE_BOARD_DENIED);
    readable=1;locked=1;grants[2]=9;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_OK);
    grant_revision++;
    assert(article_unread(&actor,2,0,2,&page)==ARTICLE_BOARD_DENIED);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,1,1),ARTICLE_BOARD_DENIED);
    locked=0;page_items[2].board=3;
    assert(article_unread(&actor,2,2,0,&page)==ARTICLE_NOT_FOUND);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,2,1),ARTICLE_NOT_FOUND);
    assert(article_unread(&actor,2,4,0,&page)==ARTICLE_NOT_FOUND);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,4,1),ARTICLE_NOT_FOUND);
    denied(article_mark_seen(&actor,2,0,1),ARTICLE_NOT_FOUND);
    page_hidden[1]=1;
    denied(article_mark_seen(&actor,2,1,1),ARTICLE_HIDDEN);
    denied(article_mark_seen(&actor,2,1,0),ARTICLE_HIDDEN);

    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,
                             ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        actor=tree_reset(3);*errors[i]=1;
        assert(article_unread(&actor,2,0,0,&page)==wanted[i]);unread_cleared(&page);
        denied(article_mark_seen(&actor,2,1,1),wanted[i]);
    }
    actor=tree_reset(12);visibility_error=12;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_STORAGE);unread_cleared(&page);
    denied(article_mark_seen(&actor,2,12,1),ARTICLE_STORAGE);
    visibility_error=0;missing_id=12;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_STORAGE);unread_cleared(&page);
    missing_id=0;seen_error=12;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_READ_STATE_STORAGE);unread_cleared(&page);
    actor=tree_reset(3);page_items[2].root=0;
    assert(article_unread(&actor,2,2,0,&page)==ARTICLE_STORAGE);unread_cleared(&page);
    page_count=UINT32_MAX;
    assert(article_unread(&actor,2,0,0,&page)==ARTICLE_STORAGE);unread_cleared(&page);
    assert(article_unread(&actor,2,0,0,NULL)==ARTICLE_INVALID_READ);
    for(int seen=0;seen<2;seen++) {
        actor=tree_reset(3);mark_ok=0;read_flags[1][1]=(unsigned char)!seen;
        assert(article_mark_seen(&actor,2,1,seen)==ARTICLE_READ_STATE_STORAGE);
        assert(mark_calls==1 && read_flags[1][1]==!seen && !context && !commits && !moderation_writes);
    }
    puts("PASS: member-owned read state, unread scope/pagination, explicit marks, revoked authority and failure clearing");
}
int main(void) {
    ArticleActor actor=reset();
    denied(article_withdraw(NULL,2,7,1),ARTICLE_LOGIN_REQUIRED);
    actor.user=0;denied(article_retitle(&actor,2,7,"new"),ARTICLE_LOGIN_REQUIRED);
    actor.user=99;denied(article_withdraw(&actor,2,7,1),ARTICLE_REVOKED);
    actor=reset();actor.epoch--;
    denied(article_retitle(&actor,2,7,"new"),ARTICLE_REVOKED);
    actor.epoch=epoch;suspended=1;
    denied(article_withdraw(&actor,2,7,1),ARTICLE_REVOKED);

    actor=reset();
    denied(article_withdraw(&actor,2,0,1),ARTICLE_NOT_FOUND);
    denied(article_retitle(&actor,2,8,"new"),ARTICLE_NOT_FOUND);
    denied(article_withdraw(&actor,3,7,1),ARTICLE_NOT_FOUND);
    readable=0;manager=1;
    denied(article_withdraw(&actor,2,7,1),ARTICLE_NOT_FOUND);

    actor=reset();actor.user=2;
    denied(article_withdraw(&actor,2,7,1),ARTICLE_OWNER_REQUIRED);
    denied(article_retitle(&actor,2,7,"new"),ARTICLE_OWNER_REQUIRED);
    manager=1;assert(article_withdraw(&actor,2,7,1)==ARTICLE_OK);
    assert(commits==1 && journal_actor==2 && journal_kind=='D' && !*journal_text);
    assert(!publications && !context);
    actor=reset();strcpy(stored.author,"guest");actor.user=3;
    denied(article_withdraw(&actor,2,7,1),ARTICLE_OWNER_REQUIRED);
    denied(article_retitle(&actor,2,7,"new"),ARTICLE_OWNER_REQUIRED);
    stored.author[0]=0;actor.user=1;
    denied(article_withdraw(&actor,2,7,1),ARTICLE_OWNER_REQUIRED);

    /* Confirmation never grants authority for a subsequent request. */
    actor=reset();assert(article_withdraw(&actor,2,7,0)==ARTICLE_CONFIRM);
    assert(!commits && !context);
    epoch++;denied(article_withdraw(&actor,2,7,1),ARTICLE_REVOKED);
    actor=reset();locked=1;
    denied(article_withdraw(&actor,2,7,1),ARTICLE_NOT_FOUND);
    grants[2]=9;assert(article_withdraw(&actor,2,7,0)==ARTICLE_CONFIRM);
    grant_revision++;denied(article_withdraw(&actor,2,7,1),ARTICLE_NOT_FOUND);
    actor=reset();actor.user=2;manager=1;
    assert(article_withdraw(&actor,2,7,0)==ARTICLE_CONFIRM);
    manager=0;denied(article_withdraw(&actor,2,7,1),ARTICLE_OWNER_REQUIRED);

    /* Imported withdrawal is forbidden even to a local manager, before confirm. */
    actor=reset();manager=1;imported=1;
    denied(article_withdraw(&actor,2,7,0),ARTICLE_IMPORTED);
    denied(article_withdraw(&actor,2,7,1),ARTICLE_IMPORTED);
    imported=-1;denied(article_withdraw(&actor,2,7,1),ARTICLE_LINK_STORAGE);

    actor=reset();hidden=1;
    denied(article_retitle(&actor,2,7,"new"),ARTICLE_HIDDEN);
    /* Withdrawal of an already hidden/withdrawn local article remains allowed;
     * durable idempotence and ancestor visibility belong to existing stores. */
    assert(article_withdraw(&actor,2,7,1)==ARTICLE_OK);
    actor=reset();writable=0;
    denied(article_retitle(&actor,2,7,"new"),ARTICLE_POSTING_DENIED);
    assert(article_withdraw(&actor,2,7,1)==ARTICLE_OK);

    actor=reset();
    denied(article_retitle(&actor,2,7,NULL),ARTICLE_INVALID_TITLE);
    denied(article_retitle(&actor,2,7,""),ARTICLE_INVALID_TITLE);
    denied(article_retitle(&actor,2,7,"bad\rtitle"),ARTICLE_INVALID_TITLE);
    denied(article_retitle(&actor,2,7,"bad\177title"),ARTICLE_INVALID_TITLE);
    char title[82];memset(title,'x',81);title[81]=0;
    denied(article_retitle(&actor,2,7,title),ARTICLE_INVALID_TITLE);
    title[80]=0;assert(article_retitle(&actor,2,7,title)==ARTICLE_OK);
    assert(commits==1 && publications==1 && journal_actor==1 && !context);

    actor=reset();actor.user=2;manager=1;imported=1;
    /* Keep existing local-title editing behavior for imported articles. */
    assert(article_retitle(&actor,2,7,"local title")==ARTICLE_OK);
    assert(journal_actor==2 && publications==1);
    actor=reset();commit_ok=0;
    assert(article_retitle(&actor,2,7,"new")==ARTICLE_EXTRAS_STORAGE);
    assert(commits==1 && !publications && !context);
    actor=reset();commit_ok=0;
    assert(article_withdraw(&actor,2,7,1)==ARTICLE_EXTRAS_STORAGE);
    assert(commits==1 && !publications && !context);

    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome expected[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,
                               ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        actor=reset();*errors[i]=1;
        denied(article_withdraw(&actor,2,7,1),expected[i]);
        denied(article_retitle(&actor,2,7,"new"),expected[i]);
    }
    puts("PASS: typed article Core identity/ACL, revoked grants, origin authority, validation and durable publication");
    read_tests();
    moderation_tests();
    page_tests();
    tree_tests();
    unread_tests();
    return 0;
}

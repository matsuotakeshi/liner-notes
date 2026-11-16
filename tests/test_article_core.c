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
    if(id!=7)return 0;
    *out=stored;return 1;
}
int article_repository_unavailable(uint32_t id) { assert(id==7);return hidden; }
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
    memset(&stored,0,sizeof(stored));stored.board=2;stored.parent=5;strcpy(stored.author,"alice");
    strcpy(stored.title,"article-title");strcpy(persisted_body,"secret body\r\n");
    stored.body_size=(uint32_t)strlen(persisted_body);removed=0;body_ok=1;body_reads=0;
    memset(grants,0,sizeof(grants));context=NULL;epoch=8;
    member_error=moderation_error=policy_error=extras_error=suspended=0;
    readable=writable=commit_ok=1;manager=hidden=imported=locked=0;grant_revision=9;
    commits=publications=0;journal_kind=0;journal_actor=0;journal_text[0]=0;
    ArticleActor actor={1,epoch,grants};return actor;
}
static void denied(ArticleOutcome got,ArticleOutcome wanted) {
    assert(got==wanted && commits==0 && publications==0 && context==NULL);
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
    return 0;
}

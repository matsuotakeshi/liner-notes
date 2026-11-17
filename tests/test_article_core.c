/* Exercise Core directly: no Session, UI, transport, socket or live DB.
 * Replace only existing persistence/identity boundaries with controlled state. */
#include "article_core.h"
#include "article_repository.h"
#include "members.h"
#include "catalog.h"
#include "moderation.h"
#include "board_policy.h"
#include "extras.h"
#include "profile_store.h"
#include "store_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int profile_error,buffer_result;
static unsigned buffer_calls;static uint32_t buffer_owner;static char saved_buffer[64];
int profile_store_broken(void) { return profile_error; }
int profile_set(uint32_t user,unsigned field,const char *body) {
    assert(field==PROFILE_BUFFER && strlen(body)<sizeof(saved_buffer));buffer_calls++;buffer_owner=user;
    if(buffer_result==1)strcpy(saved_buffer,body);
    return buffer_result;
}
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
static int other_readable;
static uint32_t probe_user;
static int lookup_error;
static ArticlePostingPolicy posting_rules;
static int posting_rules_ok,posting_capacity,append_ok,append_bad_id;
static unsigned append_calls;
static uint32_t appended_board,appended_parent,appended_user;
static char appended_author[33],appended_title[81];
static size_t appended_size;
int article_repository_posting_policy(uint32_t board,uint32_t user,ArticlePostingPolicy *out) {
    (void)board;(void)user;*out=posting_rules;return posting_rules_ok;
}
int article_repository_post_capacity(uint32_t board,size_t size) {
    (void)board;(void)size;return posting_capacity;
}
int article_repository_append(uint32_t board,uint32_t parent,uint32_t user,
                              const char *author,const char *title,const char *body,uint32_t *id) {
    assert(catalog_read(board,user) && catalog_write(board,user));
    append_calls++;*id=0;
    if(!append_ok) { posting_rules.storage_failed=1;return 0; }
    appended_board=board;appended_parent=parent;appended_user=user;
    strcpy(appended_author,author);strcpy(appended_title,title);appended_size=strlen(body);
    page_count++;if(user)posting_rules.user_posts++;
    *id=append_bad_id?page_count+1:page_count;return 1;
}

/* Saved-copy store keeps per-member revisions, including delete tombstones. */
static MemberDraft saved_copies[4];
static unsigned draft_reads,draft_writes;
static int draft_read_error,draft_write_error;
int members_draft(uint32_t user,MemberDraft *out) {
    assert(user>0 && user<4);draft_reads++;*out=saved_copies[user];
    if(draft_read_error) { strcpy(out->body,"partial secret");member_error=1;return 0; }
    return out->board!=0;
}
int members_store_draft(uint32_t user,const MemberDraft *draft,uint32_t expected,int remove,uint32_t *revision) {
    assert(user>0 && user<4);draft_writes++;
    MemberDraft *current=&saved_copies[user];
    if(draft_write_error) { *revision=999;return -1; }
    if(expected!=current->revision && !(expected==0 && !current->board && !remove))return 0;
    if(remove && !current->board)return 0;
    *revision=current->revision+1;
    if(remove)memset(current,0,sizeof(*current));else *current=*draft;
    current->revision=*revision;return 1;
}

static int discovery;
static int entry_menu,entry_inherited,entry_lock_fault;
static const char *entry_title_override,*entry_description="",*entry_information="";
static unsigned entry_metadata_reads;
int policy_menu(uint32_t board) { (void)board;return entry_menu; }
int policy_locked(uint32_t board,uint32_t user) {
    (void)user;
    if(entry_lock_fault)return entry_lock_fault;
    if(manager)return 0;
    return locked && (board==2 || (entry_inherited && board==4)) &&
           (!context || context[2]!=(uint32_t)grant_revision)?2:0;
}
const char *policy_info(uint32_t board) { (void)board;entry_metadata_reads++;return entry_information; }
const char *catalog_description(uint32_t board) { (void)board;entry_metadata_reads++;return entry_description; }

static unsigned char board_present[CATALOG_MAX+1],board_visible[CATALOG_MAX+1],board_readable[CATALOG_MAX+1],board_writable[CATALOG_MAX+1];
static uint32_t board_next[CATALOG_MAX+1],board_parent[CATALOG_MAX+1],board_depth[CATALOG_MAX+1];
static char board_titles[CATALOG_MAX+1][81];
int catalog_exists(uint32_t id) { return id>0 && id<=CATALOG_MAX && board_present[id]; }
int catalog_visible(uint32_t id,uint32_t user) { (void)user;return catalog_exists(id) && board_visible[id]; }
uint32_t catalog_next(uint32_t id) { assert(catalog_exists(id));return board_next[id]; }
uint32_t catalog_parent(uint32_t id) { assert(catalog_exists(id));return board_parent[id]; }
unsigned catalog_depth(uint32_t id) { assert(catalog_exists(id));return board_depth[id]; }
const char *catalog_title(uint32_t id) { assert(catalog_exists(id));entry_metadata_reads++;return entry_title_override?entry_title_override:board_titles[id]; }


int members_seen(uint32_t user,uint32_t id) {
    assert(user>0 && user<4 && id>0 && id<=300 && paging);
    assert((discovery?catalog_read(page_items[id].board,user):page_items[id].board==2) && !page_hidden[id]);seen_calls++;
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
uint32_t article_repository_search_ordered(uint32_t start,const char *query,int descending) {
    if(!descending)return article_repository_search_next(start,query);
    candidate_calls++;
    if(bad_candidate)return bad_candidate;
    if(!indexed)return start;
    for(uint32_t i=start;i;i--)if(candidates[i])return i;
    return 0;
}
int article_repository_body_contains(uint32_t id,const char *query) {
    assert(paging && id && id<=page_count && !page_hidden[id]);
    assert(catalog_read(page_items[id].board,probe_user));body_probes[id]++;
    return id==body_error?-1:strstr(page_bodies[id],query)!=NULL;
}

static ModerationEntry audit_rows[301];
static uint32_t audit_mod_sequence[301],audit_extra_failure,audit_mod_failure;
static unsigned audit_reads;static int audit_admin;
static unsigned char audit_sigop[CATALOG_MAX+1];
int moderation_admin(uint32_t user) { return user && audit_admin; }
int catalog_sigop(uint32_t board,uint32_t user) { return user && board<=CATALOG_MAX && audit_sigop[board]; }
int extras_audit_entry(uint32_t id,ModerationEntry *entry,uint32_t *mod_sequence) {
    assert(id && id<=300);audit_reads++;
    if(id==audit_extra_failure)return 0;
    *entry=audit_rows[id];*mod_sequence=audit_mod_sequence[id];return 1;
}
int moderation_entry(uint32_t sequence,ModerationEntry *entry) {
    if(sequence==audit_mod_failure)return 0;
    for(unsigned i=1;i<=300;i++)if(audit_mod_sequence[i]==sequence) { *entry=audit_rows[i];return 1; }
    return 0;
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
uint32_t members_find(const char *name) {
    if(lookup_error) { member_error=1;return 0; }
    for(uint32_t i=1;i<=3;i++)if(!strcmp(name,members_name(i)))return i;
    return 0;
}
int moderation_suspended(uint32_t user) { (void)user;return suspended; }
uint32_t moderation_epoch(uint32_t user) { (void)user;return epoch; }
void policy_context(const uint32_t *p) { context=p; }
int catalog_read(uint32_t board,uint32_t user) {
    if(discovery)return catalog_visible(board,user) && board_readable[board] && !policy_locked(board,user);
    (void)user;
    if(board==3 || board==4)return other_readable && user==1;
    return board==2 && readable && (!locked || (context && context[2]==(uint32_t)grant_revision));
}
int catalog_under(uint32_t board,uint32_t root) {
    return (root==2 && (board==2 || board==3)) ||
           ((root==3 || root==4) && board==root);
}
int catalog_manage(uint32_t board,uint32_t user) { return board==2 && user && manager; }
int catalog_write(uint32_t board,uint32_t user) { return discovery?(catalog_visible(board,user) && board_writable[board]):(catalog_read(board,user) && writable); }
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
static uint32_t pending_target,disabled_target,role_previous;
static int role_ok,guest_ok;
static unsigned role_calls,epoch_changes,guest_saves,guest_publications,author_publications;
int members_active(uint32_t user) { return members_name(user) && user!=disabled_target; }
int members_pending(uint32_t user) { return user && user==pending_target; }
void moderation_credentials_changed(uint32_t user) { assert(user);epoch_changes++; }
int catalog_role_set(uint32_t board,uint32_t user,uint32_t actor,uint32_t *previous) {
    assert(board==2 && actor==1 && user<=3);role_calls++;*previous=role_previous;return role_ok;
}
int article_repository_guest_save(uint32_t board,unsigned enabled) {
    assert(board==2 && enabled<=1);guest_saves++;return guest_ok;
}
void catalog_guest(uint32_t board,int enabled) {
    assert(board==2 && enabled>=0 && enabled<=1 && guest_saves && guest_ok);guest_publications++;
}
void article_repository_author_changed(uint32_t id,uint32_t user,const char *name) {
    assert(id==7 && commits && commit_ok && journal_kind=='A');
    assert(!strcmp(name,members_name(user)) && !strcmp(name,journal_text));author_publications++;
}
static ArticleActor reset(void) {
    pending_target=disabled_target=0;role_previous=3;role_ok=guest_ok=1;
    role_calls=epoch_changes=guest_saves=guest_publications=author_publications=0;
    profile_error=0;buffer_result=1;buffer_calls=buffer_owner=0;strcpy(saved_buffer,"preserved");
    audit_admin=0;memset(audit_sigop,0,sizeof(audit_sigop));
    discovery=0;entry_menu=entry_inherited=entry_lock_fault=0;entry_title_override=NULL;
    entry_description=entry_information="";entry_metadata_reads=0;
    paging=0;other_readable=0;probe_user=0;lookup_error=0;
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
static ArticleActor discovery_reset(void) {
    ArticleActor actor=page_reset(6);discovery=1;
    memset(board_present,0,sizeof(board_present));memset(board_visible,0,sizeof(board_visible));
    memset(board_readable,0,sizeof(board_readable));memset(board_writable,0,sizeof(board_writable));
    memset(board_parent,0,sizeof(board_parent));memset(board_depth,0,sizeof(board_depth));
    memset(board_next,0,sizeof(board_next));memset(board_titles,0,sizeof(board_titles));
    for(uint32_t id=1;id<=11;id++) {
        board_present[id]=board_visible[id]=board_readable[id]=board_writable[id]=1;
        board_next[id]=id<11?id+1:0;
        snprintf(board_titles[id],sizeof(board_titles[id]),"board %u",id);
    }
    board_visible[3]=0;board_readable[5]=0;board_writable[5]=0;
    board_parent[3]=board_parent[4]=board_parent[5]=2;
    board_depth[3]=board_depth[4]=board_depth[5]=1;
    board_parent[6]=4;board_depth[6]=2;
    page_items[1].board=1;page_items[2].board=page_items[3].board=2;
    page_items[4].board=3;page_items[5].board=4;page_items[6].board=5;
    page_hidden[3]=1;read_flags[1][5]=1;
    return actor;
}
static void discovery_cleared(const void *out,size_t size) {
    const unsigned char *p=out;
    for(size_t i=0;i<size;i++)assert(!p[i]);
    assert(!context && !mark_calls && !commits && !moderation_writes && !publications && !body_reads);
    for(unsigned i=0;i<=300;i++)assert(!body_probes[i]);
}
static void discovery_tests(void) {
    ArticleBoardsPage page;ArticleBoardRow next;ArticleActor actor=discovery_reset();
    assert(article_boards(&actor,0,0,&page)==ARTICLE_OK);
    assert(page.count==8 && page.next==10 && page.rows[0].id==1 && page.rows[2].id==4);
    assert(page.rows[0].unread==1 && page.rows[1].unread==1 && page.rows[2].unread==0);
    assert(page.rows[2].unread_known && page.rows[2].parent==2 && page.rows[2].depth==1);
    assert(page.rows[3].id==5 && !page.rows[3].unread_known && !page.rows[3].writable);
    board_titles[1][0]='X';assert(page.rows[0].title[0]!='X');
    assert(article_boards(&actor,page.next,0,&page)==ARTICLE_OK);assert(page.count==2 && page.rows[0].id==10 && !page.next);
    assert(article_boards(&actor,2,1,&page)==ARTICLE_OK);assert(page.count==2 && page.rows[0].id==4 && page.rows[1].id==5 && !page.next);
    assert(article_boards(&actor,11,1,&page)==ARTICLE_OK);discovery_cleared(&page,sizeof(page));
    unsigned before=seen_calls;missing_id=1;
    assert(article_boards(NULL,0,0,&page)==ARTICLE_OK);assert(seen_calls==before);
    for(unsigned i=0;i<page.count;i++)assert(!page.rows[i].unread_known && !page.rows[i].unread);
    missing_id=0;
    assert(article_next_board(NULL,1,&next)==ARTICLE_LOGIN_REQUIRED);discovery_cleared(&next,sizeof(next));
    assert(article_next_board(&actor,1,&next)==ARTICLE_OK);assert(next.id==2 && next.unread==1);
    assert(article_next_board(&actor,2,&next)==ARTICLE_OK);assert(next.id==1);
    read_flags[1][1]=1;
    assert(article_next_board(&actor,2,&next)==ARTICLE_OK);discovery_cleared(&next,sizeof(next));
    assert(article_boards(&actor,12,0,&page)==ARTICLE_BOARD_DENIED);discovery_cleared(&page,sizeof(page));
    assert(article_boards(&actor,CATALOG_MAX+1,0,&page)==ARTICLE_BOARD_DENIED);discovery_cleared(&page,sizeof(page));
    assert(article_next_board(&actor,0,&next)==ARTICLE_BOARD_DENIED);discovery_cleared(&next,sizeof(next));
    assert(article_boards(&actor,1,0,NULL)==ARTICLE_INVALID_READ);
    assert(article_next_board(&actor,1,NULL)==ARTICLE_INVALID_READ);
    actor=discovery_reset();board_next[1]=4;board_next[4]=2;board_next[3]=5;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_OK);assert(page.rows[1].id==4 && page.rows[2].id==2);
    actor=discovery_reset();locked=1;
    assert(article_boards(&actor,2,0,&page)==ARTICLE_OK);assert(!page.rows[0].unread_known);
    assert(article_next_board(&actor,1,&next)==ARTICLE_OK);discovery_cleared(&next,sizeof(next));
    grants[2]=(uint32_t)grant_revision;
    assert(article_next_board(&actor,1,&next)==ARTICLE_OK);assert(next.id==2);
    grant_revision++;
    assert(article_next_board(&actor,1,&next)==ARTICLE_OK);discovery_cleared(&next,sizeof(next));
    actor=discovery_reset();epoch++;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_REVOKED);discovery_cleared(&page,sizeof(page));
    assert(article_next_board(&actor,1,&next)==ARTICLE_REVOKED);discovery_cleared(&next,sizeof(next));
    epoch--;suspended=1;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_REVOKED);discovery_cleared(&page,sizeof(page));
    suspended=0;actor.user=99;
    assert(article_next_board(&actor,1,&next)==ARTICLE_REVOKED);discovery_cleared(&next,sizeof(next));
    actor=discovery_reset();board_visible[2]=0;
    assert(article_next_board(&actor,1,&next)==ARTICLE_OK);discovery_cleared(&next,sizeof(next));
    actor=discovery_reset();seen_error=2;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_READ_STATE_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=discovery_reset();missing_id=2;
    assert(article_next_board(&actor,1,&next)==ARTICLE_STORAGE);discovery_cleared(&next,sizeof(next));
    missing_id=0;visibility_error=2;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));
    visibility_error=0;page_items[2].board=CATALOG_MAX+1;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));
    page_items[2].board=0;
    assert(article_next_board(&actor,1,&next)==ARTICLE_STORAGE);discovery_cleared(&next,sizeof(next));
    actor=discovery_reset();page_count=CONTENT_ID_MAX+1;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=discovery_reset();board_next[2]=1;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));
    read_flags[1][1]=read_flags[1][2]=1;
    assert(article_next_board(&actor,4,&next)==ARTICLE_STORAGE);discovery_cleared(&next,sizeof(next));
    actor=discovery_reset();board_next[2]=CATALOG_MAX+1;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=discovery_reset();board_next[2]=12;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=discovery_reset();board_parent[4]=4;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=discovery_reset();board_depth[4]=CATALOG_MAX;
    assert(article_boards(&actor,1,0,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));
    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        actor=discovery_reset();*errors[i]=1;
        assert(article_boards(&actor,1,0,&page)==wanted[i]);discovery_cleared(&page,sizeof(page));
        assert(article_next_board(&actor,1,&next)==wanted[i]);discovery_cleared(&next,sizeof(next));
    }
    actor=discovery_reset();page_count=0;
    assert(article_next_board(&actor,1,&next)==ARTICLE_OK);discovery_cleared(&next,sizeof(next));
    assert(!mark_calls && !body_reads && !commits && !publications && !context);
    puts("PASS: catalog-order board discovery, private/locked unread counts, wrap, revoked grants and fail-closed traversal");
}
static ArticleActor posting_reset(void) {
    ArticleActor actor=discovery_reset();
    memset(&posting_rules,0,sizeof(posting_rules));posting_rules.version=4;
    posting_rules.members_required=1;posting_rules.body_limit=CONTENT_BODY;
    posting_rules_ok=posting_capacity=append_ok=1;append_bad_id=0;
    append_calls=0;appended_board=appended_parent=appended_user=0;
    appended_author[0]=appended_title[0]=0;appended_size=0;
    return actor;
}
static void post_rejected(ArticleOutcome wanted,const ArticleActor *actor,uint32_t board,uint32_t parent,
                          const char *guest,const char *title,const char *body) {
    uint32_t id=999;unsigned before=append_calls;
    assert(article_post(actor,board,parent,guest,title,body,&id)==wanted);
    assert(!id && append_calls==before && !context && !body_reads && !mark_calls);
}
static void posting_tests(void) {
    ArticleActor actor=posting_reset();uint32_t id;
    assert(article_prepare_post(&actor,2,2)==ARTICLE_OK);assert(!append_calls && !context);
    assert(article_post(&actor,2,0,"forged-author","new note","body\r\n",&id)==ARTICLE_OK);
    assert(id==7 && appended_board==2 && !appended_parent && appended_user==1);
    assert(!strcmp(appended_author,"alice") && !strcmp(appended_title,"new note") && appended_size==6);
    assert(article_post(&actor,2,2,NULL,"reply","body",&id)==ARTICLE_OK);assert(id==8 && appended_parent==2);
    posting_rules.user_limit=2;
    post_rejected(ARTICLE_FULL,&actor,2,0,NULL,"quota recheck","body");
    assert(!mark_calls && !seen_calls && !body_reads && !context);
    actor=posting_reset();
    post_rejected(ARTICLE_LOGIN_REQUIRED,NULL,2,0,"visitor","title","body");
    assert(article_prepare_post(NULL,2,0)==ARTICLE_LOGIN_REQUIRED);
    posting_rules.guest_allowed=1;
    assert(article_post(NULL,2,0,"visitor","title","body",&id)==ARTICLE_OK);
    assert(!strcmp(appended_author,"visitor") && !appended_user && !posting_rules.user_posts);
    assert(article_post(NULL,2,0,NULL,"title","body",&id)==ARTICLE_OK);assert(!strcmp(appended_author,"guest"));
    char long_name[34];memset(long_name,'x',33);long_name[33]=0;
    post_rejected(ARTICLE_INVALID_DRAFT,NULL,2,0,long_name,"title","body");
    post_rejected(ARTICLE_INVALID_DRAFT,NULL,2,0,"bad\nname","title","body");
    actor=posting_reset();posting_rules.members_required=0;
    assert(article_prepare_post(NULL,2,0)==ARTICLE_OK);
    assert(article_post(NULL,2,0,"","title","body",&id)==ARTICLE_OK);assert(!strcmp(appended_author,"guest"));
    actor=posting_reset();assert(article_prepare_post(&actor,2,2)==ARTICLE_OK);page_hidden[2]=1;
    post_rejected(ARTICLE_HIDDEN,&actor,2,2,NULL,"title","body");
    assert(article_prepare_post(&actor,2,2)==ARTICLE_HIDDEN);
    page_hidden[2]=0;page_items[2].board=1;
    post_rejected(ARTICLE_NOT_FOUND,&actor,2,2,NULL,"title","body");
    post_rejected(ARTICLE_NOT_FOUND,&actor,2,100,NULL,"title","body");
    actor=posting_reset();visibility_error=2;
    post_rejected(ARTICLE_STORAGE,&actor,2,2,NULL,"title","body");
    actor=posting_reset();board_readable[2]=0;
    post_rejected(ARTICLE_BOARD_DENIED,&actor,2,0,NULL,"title","body");
    board_readable[2]=1;board_writable[2]=0;
    post_rejected(ARTICLE_POSTING_DENIED,&actor,2,0,NULL,"title","body");
    actor=posting_reset();locked=1;grants[2]=(uint32_t)grant_revision;
    assert(article_prepare_post(&actor,2,0)==ARTICLE_OK);grant_revision++;
    post_rejected(ARTICLE_BOARD_DENIED,&actor,2,0,NULL,"title","body");
    actor=posting_reset();assert(article_prepare_post(&actor,2,0)==ARTICLE_OK);epoch++;
    post_rejected(ARTICLE_REVOKED,&actor,2,0,NULL,"title","body");
    epoch--;suspended=1;post_rejected(ARTICLE_REVOKED,&actor,2,0,NULL,"title","body");
    suspended=0;actor.user=99;post_rejected(ARTICLE_REVOKED,&actor,2,0,NULL,"title","body");
    actor=posting_reset();posting_rules.total_limit=page_count;
    assert(article_prepare_post(&actor,2,0)==ARTICLE_OK);
    post_rejected(ARTICLE_FULL,&actor,2,0,NULL,"title","body");
    posting_rules.total_limit=0;posting_rules.user_limit=2;posting_rules.user_posts=2;
    post_rejected(ARTICLE_FULL,&actor,2,0,NULL,"title","body");
    posting_rules.user_limit=0;posting_rules.user_posts=10000;
    assert(article_post(&actor,2,0,NULL,"title","body",&id)==ARTICLE_OK);assert(id==7);
    actor=posting_reset();posting_capacity=0;
    post_rejected(ARTICLE_FULL,&actor,2,0,NULL,"title","body");
    posting_capacity=-1;post_rejected(ARTICLE_STORAGE,&actor,2,0,NULL,"title","body");
    actor=posting_reset();page_count=CONTENT_ID_MAX;
    post_rejected(ARTICLE_FULL,&actor,2,0,NULL,"title","body");
    actor=posting_reset();posting_rules.body_limit=3;
    post_rejected(ARTICLE_BODY_LIMIT,&actor,2,0,NULL,"title","body");
    posting_rules.body_limit=CONTENT_BODY;
    post_rejected(ARTICLE_INVALID_DRAFT,&actor,2,0,NULL,"title",NULL);
    post_rejected(ARTICLE_INVALID_DRAFT,&actor,2,0,NULL,NULL,"body");
    post_rejected(ARTICLE_INVALID_DRAFT,&actor,2,0,NULL,"","body");
    post_rejected(ARTICLE_INVALID_DRAFT,&actor,2,0,NULL,"title","");
    post_rejected(ARTICLE_INVALID_DRAFT,&actor,2,0,NULL,"bad\ntitle","body");
    post_rejected(ARTICLE_INVALID_DRAFT,&actor,2,0,NULL,"bad\177title","body");
    char title[82];memset(title,'x',81);title[81]=0;
    post_rejected(ARTICLE_INVALID_DRAFT,&actor,2,0,NULL,title,"body");
    title[80]=0;assert(article_post(&actor,2,0,NULL,title,"body",&id)==ARTICLE_OK);
    static char large[CONTENT_BODY+2];memset(large,'x',CONTENT_BODY+1);large[CONTENT_BODY+1]=0;
    post_rejected(ARTICLE_BODY_LIMIT,&actor,2,0,NULL,"title",large);
    large[CONTENT_BODY]=0;assert(article_post(&actor,2,0,NULL,"title",large,&id)==ARTICLE_OK);
    assert(appended_size==CONTENT_BODY);
    actor=posting_reset();posting_rules.version=3;
    post_rejected(ARTICLE_LEGACY_BOARDS,&actor,2,0,NULL,"title","body");
    large[1025]=0;post_rejected(ARTICLE_LEGACY_BODY,&actor,1,0,NULL,"title",large);
    large[1024]=0;assert(article_post(&actor,1,0,NULL,"title",large,&id)==ARTICLE_OK);
    posting_rules.version=2;post_rejected(ARTICLE_LEGACY_MEMBERS,&actor,1,0,NULL,"title","body");
    posting_rules.members_required=0;
    assert(article_post(NULL,1,0,NULL,"title","body",&id)==ARTICLE_OK);
    posting_rules.version=1;assert(article_post(NULL,1,0,NULL,"title","body",&id)==ARTICLE_OK);
    actor=posting_reset();posting_rules.storage_failed=1;
    post_rejected(ARTICLE_POST_UNAVAILABLE,&actor,2,0,NULL,"title","body");
    actor=posting_reset();posting_rules_ok=0;
    post_rejected(ARTICLE_STORAGE,&actor,2,0,NULL,"title","body");
    posting_rules_ok=1;posting_rules.version=0;
    post_rejected(ARTICLE_STORAGE,&actor,2,0,NULL,"title","body");
    actor=posting_reset();append_ok=0;id=999;
    assert(article_post(&actor,2,0,NULL,"title","body",&id)==ARTICLE_POST_UNCERTAIN);
    assert(!id && append_calls==1 && page_count==6 && !context);
    post_rejected(ARTICLE_POST_UNAVAILABLE,&actor,2,0,NULL,"title","body");
    actor=posting_reset();append_bad_id=1;id=999;
    assert(article_post(&actor,2,0,NULL,"title","body",&id)==ARTICLE_POST_UNCERTAIN);assert(!id && !context);
    actor=posting_reset();assert(article_post(&actor,2,0,NULL,"title","body",NULL)==ARTICLE_INVALID_DRAFT);assert(!append_calls);
    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        actor=posting_reset();*errors[i]=1;
        assert(article_prepare_post(&actor,2,0)==wanted[i]);assert(!context);
        post_rejected(wanted[i],&actor,2,0,NULL,"title","body");
    }
    puts("PASS: local posting admission/commit ACL, guest identity, quotas, legacy formats, body boundaries and uncertain append");
}
static void navigation_cleared(const ArticleSummary *row) {
    const unsigned char *p=(const unsigned char *)row;
    for(size_t i=0;i<sizeof(*row);i++)assert(!p[i]);
    assert(!context && !mark_calls && !seen_calls && !commits && !moderation_writes && !publications && !body_reads);
    for(unsigned i=0;i<=300;i++)assert(!body_probes[i]);
}
static ArticleActor navigation_reset(void) {
    ArticleActor actor=page_reset(6);
    page_items[1].child=2;
    for(unsigned i=2;i<=4;i++) {
        page_items[i].parent=1;page_items[i].next=i==4?0:i+1;page_items[i].prev=i==2?0:i-1;
    }
    page_items[2].child=5;page_items[5].parent=2;
    return actor;
}
static void navigation_tests(void) {
    ArticleSummary row;ArticleActor actor=navigation_reset();
    assert(article_navigate(NULL,2,1,ARTICLE_CHILD,&row)==ARTICLE_OK);assert(row.id==2 && row.board==2 && row.parent==1);
    page_items[2].title[0]='X';assert(row.title[0]!='X');
    assert(article_navigate(&actor,2,2,ARTICLE_NEXT,&row)==ARTICLE_OK);assert(row.id==3);
    assert(article_navigate(&actor,2,3,ARTICLE_PREVIOUS,&row)==ARTICLE_OK);assert(row.id==2);
    assert(article_navigate(&actor,2,5,ARTICLE_PARENT,&row)==ARTICLE_OK);assert(row.id==2);
    page_hidden[2]=page_hidden[3]=page_hidden[5]=1;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_OK);assert(row.id==4);
    assert(article_navigate(&actor,2,2,ARTICLE_NEXT,&row)==ARTICLE_OK);assert(row.id==4);
    assert(article_navigate(&actor,2,4,ARTICLE_PREVIOUS,&row)==ARTICLE_OK);navigation_cleared(&row);
    assert(article_navigate(&actor,2,5,ARTICLE_PARENT,&row)==ARTICLE_OK);assert(row.id==1);
    page_hidden[1]=1;
    assert(article_navigate(&actor,2,5,ARTICLE_PARENT,&row)==ARTICLE_OK);navigation_cleared(&row);
    assert(article_navigate(&actor,2,6,ARTICLE_CHILD,&row)==ARTICLE_OK);navigation_cleared(&row);
    assert(article_navigate(&actor,2,1,ARTICLE_PARENT,&row)==ARTICLE_OK);navigation_cleared(&row);
    assert(article_navigate(&actor,2,1,(ArticleDirection)99,&row)==ARTICLE_INVALID_QUERY);navigation_cleared(&row);
    assert(article_navigate(&actor,2,0,ARTICLE_CHILD,&row)==ARTICLE_NOT_FOUND);navigation_cleared(&row);
    assert(article_navigate(&actor,2,7,ARTICLE_CHILD,&row)==ARTICLE_NOT_FOUND);navigation_cleared(&row);
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,NULL)==ARTICLE_INVALID_READ);
    page_items[1].board=3;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_NOT_FOUND);navigation_cleared(&row);
    actor=navigation_reset();page_items[2].board=3;other_readable=1;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    page_items[2].board=2;page_items[2].parent=6;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    assert(article_navigate(&actor,2,3,ARTICLE_PREVIOUS,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    actor=navigation_reset();missing_id=2;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    missing_id=0;visibility_error=2;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    visibility_error=0;page_items[1].child=7;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    page_items[1].child=1;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    actor=navigation_reset();page_hidden[2]=page_hidden[3]=1;page_items[3].next=2;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    page_items[2].parent=3;page_items[3].parent=2;
    assert(article_navigate(&actor,2,5,ARTICLE_PARENT,&row)==ARTICLE_STORAGE);navigation_cleared(&row);
    actor=navigation_reset();page_count=CONTENT_ID_MAX+1;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_STORAGE);navigation_cleared(&row);page_count=6;
    epoch++;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_REVOKED);navigation_cleared(&row);epoch--;
    suspended=1;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_REVOKED);navigation_cleared(&row);suspended=0;
    actor.user=99;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_REVOKED);navigation_cleared(&row);actor.user=1;
    readable=0;manager=1;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_BOARD_DENIED);navigation_cleared(&row);readable=1;
    locked=1;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_BOARD_DENIED);navigation_cleared(&row);
    grants[2]=(uint32_t)grant_revision;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_OK);assert(row.id==2);
    grant_revision++;
    assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==ARTICLE_BOARD_DENIED);navigation_cleared(&row);locked=0;
    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        *errors[i]=1;
        assert(article_navigate(&actor,2,1,ARTICLE_CHILD,&row)==wanted[i]);navigation_cleared(&row);*errors[i]=0;
    }
    actor=page_reset(300);
    for(unsigned i=2;i<=300;i++) { page_items[i].parent=i-1;page_hidden[i]=1; }
    assert(article_navigate(&actor,2,300,ARTICLE_PARENT,&row)==ARTICLE_OK);assert(row.id==1);
    page_hidden[1]=1;
    assert(article_navigate(&actor,2,300,ARTICLE_PARENT,&row)==ARTICLE_OK);navigation_cleared(&row);
    puts("PASS: related article navigation, hidden chains, revoked identity/ACL and bounded corrupt references");
}
static void scan_cleared(const ArticleScan *scan) {
    const unsigned char *p=(const unsigned char *)scan;
    for(size_t i=0;i<sizeof(*scan);i++)assert(!p[i]);
    assert(!context && !mark_calls && !commits && !moderation_writes && !publications);
}
static void filter_denied(const ArticleActor *actor,const ArticleFilter *filter,ArticleOutcome wanted) {
    ArticlePage page;ArticleScan scan;
    memset(&page,0xa5,sizeof(page));memset(&scan,0xa5,sizeof(scan));
    assert(article_find(actor,filter,0,&page)==wanted);page_cleared(&page);
    assert(article_scan(actor,filter,0,0,&scan)==wanted);scan_cleared(&scan);
}
static void find_scan_tests(void) {
    ArticlePage page;ArticleScan scan;
    ArticleActor actor=page_reset(12);ArticleFilter filter={2,ARTICLE_HERE,"*","*"};
    assert(article_find(NULL,&filter,0,&page)==ARTICLE_OK);
    assert(page.count==8 && page.next==9 && !page.total && page.rows[0].id==1 && page.rows[7].id==8);
    assert(article_find(&actor,&filter,page.next,&page)==ARTICLE_OK);
    assert(page.count==4 && page.rows[3].id==12 && !page.next);
    page_items[12].title[0]='X';assert(page.rows[3].title[0]!='X');
    assert(article_find(&actor,&filter,CONTENT_ID_MAX+1,&page)==ARTICLE_OK);page_cleared(&page);
    assert(article_find(&actor,&filter,UINT32_MAX,&page)==ARTICLE_INVALID_CURSOR);page_cleared(&page);
    assert(article_scan(&actor,&filter,UINT32_MAX,1,&scan)==ARTICLE_OK);
    assert(scan.article.id==12 && scan.next==11 && !context);
    assert(article_scan(&actor,&filter,0,1,&scan)==ARTICLE_OK);assert(scan.article.id==1 && !scan.next);
    assert(article_scan(&actor,&filter,13,0,&scan)==ARTICLE_OK);scan_cleared(&scan);
    assert(article_scan(&actor,&filter,UINT32_MAX,0,&scan)==ARTICLE_INVALID_CURSOR);scan_cleared(&scan);
    assert(article_find(&actor,&filter,0,NULL)==ARTICLE_INVALID_READ);
    assert(article_scan(&actor,&filter,0,0,NULL)==ARTICLE_INVALID_READ);

    actor=page_reset(6);probe_user=1;other_readable=1;
    for(unsigned i=1;i<=6;i++)strcpy(page_items[i].author,i==5?"bob":"alice");
    strcpy(page_items[1].title,"needle-title");strcpy(page_bodies[2],"body needle");
    page_hidden[2]=1;page_items[3].board=3;strcpy(page_bodies[3],"body needle");
    page_items[4].board=4;strcpy(page_bodies[4],"body needle");
    page_items[5].parent=1;strcpy(page_bodies[5],"body needle");
    filter=(ArticleFilter){2,ARTICLE_SUBTREE,"alice","needle"};
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_OK);
    assert(page.count==2 && page.rows[0].id==1 && page.rows[1].id==3 && page.rows[1].board==3);
    assert(!body_probes[1] && !body_probes[2] && body_probes[3] && !body_probes[4] && !body_probes[5]);
    filter.scope=ARTICLE_ALL;
    assert(article_scan(&actor,&filter,UINT32_MAX,1,&scan)==ARTICLE_OK);assert(scan.article.id==4 && scan.next==3);
    assert(article_scan(&actor,&filter,scan.next,1,&scan)==ARTICLE_OK);assert(scan.article.id==3 && scan.next==2);
    page_hidden[3]=1; /* visibility rechecked when the cursor is reused */
    assert(article_scan(&actor,&filter,3,1,&scan)==ARTICLE_OK);assert(scan.article.id==1 && !scan.next);
    probe_user=0;
    assert(article_find(NULL,&filter,0,&page)==ARTICLE_OK);assert(page.count==1 && page.rows[0].id==1);
    actor.user=2;probe_user=2;
    assert(article_scan(&actor,&filter,UINT32_MAX,1,&scan)==ARTICLE_OK);assert(scan.article.id==1);
    filter.author="bob";
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_OK);assert(page.count==1 && page.rows[0].id==5 && page.rows[0].parent==1);
    filter.query="Needle";
    assert(article_scan(&actor,&filter,0,0,&scan)==ARTICLE_OK);scan_cleared(&scan);
    filter.author="*";filter.query="*";readable=0;manager=1;
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_OK);page_cleared(&page); /* manager cannot read private results */
    actor.user=1;probe_user=1;
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_OK);assert(page.count==1 && page.rows[0].id==4);
    filter.scope=ARTICLE_HERE;filter_denied(&actor,&filter,ARTICLE_BOARD_DENIED);
    readable=1;locked=1;filter_denied(&actor,&filter,ARTICLE_BOARD_DENIED);
    grants[2]=(uint32_t)grant_revision;
    assert(article_scan(&actor,&filter,0,0,&scan)==ARTICLE_OK);assert(scan.article.id==1);
    grant_revision++;filter_denied(&actor,&filter,ARTICLE_BOARD_DENIED);locked=0;

    filter_denied(&actor,NULL,ARTICLE_INVALID_QUERY);
    filter.scope=(ArticleScope)3;filter_denied(&actor,&filter,ARTICLE_INVALID_QUERY);filter.scope=ARTICLE_ALL;
    const char *bad[]={NULL,"","unknown","Alice","bad\nname"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) { filter.author=bad[i];filter_denied(&actor,&filter,ARTICLE_INVALID_QUERY); }
    filter.author="*";
    const char *bad_query[]={NULL,"","bad\nterm","bad\177term"};
    for(unsigned i=0;i<sizeof(bad_query)/sizeof(*bad_query);i++) { filter.query=bad_query[i];filter_denied(&actor,&filter,ARTICLE_INVALID_QUERY); }
    char query[82];memset(query,'q',81);query[81]=0;filter.query=query;
    filter_denied(&actor,&filter,ARTICLE_INVALID_QUERY);query[80]=0;
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_OK);page_cleared(&page);
    filter.query="*";epoch++;filter_denied(&actor,&filter,ARTICLE_REVOKED);epoch--;
    suspended=1;filter_denied(&actor,&filter,ARTICLE_REVOKED);suspended=0;
    actor.user=99;filter_denied(&actor,&filter,ARTICLE_REVOKED);actor.user=1;
    filter.author="alice";lookup_error=1;filter_denied(&actor,&filter,ARTICLE_MEMBER_STORAGE);lookup_error=member_error=0;
    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) { *errors[i]=1;filter_denied(&actor,&filter,wanted[i]);*errors[i]=0; }

    actor=page_reset(300);filter=(ArticleFilter){2,ARTICLE_ALL,"*","needle"};
    strcpy(page_items[300].title,"needle");
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_OK);assert(!page.count && page.next==257 && candidate_calls==256);
    assert(article_find(&actor,&filter,page.next,&page)==ARTICLE_OK);assert(page.count==1 && page.rows[0].id==300 && !page.next);
    assert(article_scan(&actor,&filter,0,0,&scan)==ARTICLE_OK);assert(scan.article.id==300 && !scan.next);
    strcpy(page_items[300].title,"no match");strcpy(page_items[1].title,"needle");
    assert(article_scan(&actor,&filter,UINT32_MAX,1,&scan)==ARTICLE_OK);assert(scan.article.id==1 && !scan.next);
    indexed=1;candidates[1]=candidates[300]=1;strcpy(page_items[300].title,"needle");candidate_calls=0;
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_OK);assert(page.count==2 && candidate_calls==2);
    assert(article_scan(&actor,&filter,299,1,&scan)==ARTICLE_OK);assert(scan.article.id==1);
    memset(candidates,0,sizeof(candidates));
    assert(article_scan(&actor,&filter,UINT32_MAX,1,&scan)==ARTICLE_OK);scan_cleared(&scan);
    bad_candidate=301;filter_denied(&actor,&filter,ARTICLE_STORAGE);
    bad_candidate=1;
    assert(article_find(&actor,&filter,2,&page)==ARTICLE_STORAGE);page_cleared(&page);
    assert(article_scan(&actor,&filter,2,0,&scan)==ARTICLE_STORAGE);scan_cleared(&scan);
    bad_candidate=3;
    assert(article_scan(&actor,&filter,2,1,&scan)==ARTICLE_STORAGE);scan_cleared(&scan);
    bad_candidate=0;indexed=0;missing_id=2;
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_STORAGE);page_cleared(&page);
    assert(article_scan(&actor,&filter,2,0,&scan)==ARTICLE_STORAGE);scan_cleared(&scan);
    missing_id=0;visibility_error=2;
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_STORAGE);page_cleared(&page);
    assert(article_scan(&actor,&filter,2,0,&scan)==ARTICLE_STORAGE);scan_cleared(&scan);
    visibility_error=0;body_error=2;
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_STORAGE);page_cleared(&page);
    assert(article_scan(&actor,&filter,2,0,&scan)==ARTICLE_STORAGE);scan_cleared(&scan);
    body_error=0;page_count=CONTENT_ID_MAX+1;filter_denied(&actor,&filter,ARTICLE_STORAGE);
    actor=page_reset(0);filter=(ArticleFilter){2,ARTICLE_ALL,"*","*"};
    assert(article_find(&actor,&filter,0,&page)==ARTICLE_OK);page_cleared(&page);
    assert(article_scan(&actor,&filter,UINT32_MAX,1,&scan)==ARTICLE_OK);scan_cleared(&scan);
    assert(!mark_calls && !seen_calls && !body_reads);
    puts("PASS: FIND/SCAN exact filters, per-result ACL, ordered index bounds, owned pages and failure clearing");
}
static void news_tests(void) {
    ArticlePage page;ArticleActor actor=page_reset(12);
    for(uint32_t i=1;i<=12;i++)page_items[i].created=100+i;
    assert(article_news(&actor,2,ARTICLE_HERE,0,101,108,&page)==ARTICLE_OK);
    assert(page.count==8 && page.next==9 && !page.total && page.rows[0].id==1 && page.rows[7].id==8);
    page_items[1].title[0]='X';assert(page.rows[0].title[0]!='X'); /* owned copy */
    assert(article_news(&actor,2,ARTICLE_HERE,page.next,101,108,&page)==ARTICLE_OK);page_cleared(&page);
    page_items[2].created=0;page_hidden[3]=1;page_items[4].parent=3;page_hidden[4]=1;
    page_items[5].board=3;page_items[6].board=4;other_readable=1;
    assert(article_news(&actor,2,ARTICLE_SUBTREE,0,0,106,&page)==ARTICLE_OK);
    assert(page.count==2 && page.rows[0].id==1 && page.rows[1].id==5 && page.rows[1].board==3 && !page.next);
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,106,&page)==ARTICLE_OK);
    assert(page.count==3 && page.rows[2].id==6);
    assert(article_news(NULL,2,ARTICLE_ALL,0,0,106,&page)==ARTICLE_OK);
    assert(page.count==1 && page.rows[0].id==1); /* private child/sibling */
    actor.user=2;assert(article_news(&actor,2,ARTICLE_ALL,0,0,106,&page)==ARTICLE_OK);
    assert(page.count==1);actor.user=1;
    readable=0;manager=1;
    assert(article_news(&actor,2,ARTICLE_HERE,0,0,0,&page)==ARTICLE_BOARD_DENIED);page_cleared(&page);
    assert(article_news(&actor,2,ARTICLE_SUBTREE,0,0,0,&page)==ARTICLE_OK);
    assert(page.count==1 && page.rows[0].id==5); /* unreadable parent does not grant/deny child */
    assert(article_news(&actor,999,ARTICLE_SUBTREE,0,0,0,&page)==ARTICLE_OK);page_cleared(&page);
    readable=1;locked=1;other_readable=0;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_OK);page_cleared(&page);
    grants[2]=(uint32_t)grant_revision;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_OK);assert(page.count);
    grant_revision++;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_OK);page_cleared(&page);
    assert(article_news(&actor,2,ARTICLE_HERE,0,0,0,&page)==ARTICLE_BOARD_DENIED);page_cleared(&page);
    locked=0;
    assert(article_news(&actor,2,(ArticleScope)-1,0,0,0,&page)==ARTICLE_INVALID_QUERY);page_cleared(&page);
    assert(article_news(&actor,2,(ArticleScope)3,0,0,0,&page)==ARTICLE_INVALID_QUERY);page_cleared(&page);
    assert(article_news(&actor,2,ARTICLE_ALL,UINT32_MAX,0,0,&page)==ARTICLE_INVALID_CURSOR);page_cleared(&page);
    assert(article_news(&actor,2,ARTICLE_ALL,CONTENT_ID_MAX+1,0,0,&page)==ARTICLE_OK);page_cleared(&page);
    assert(article_news(&actor,2,ARTICLE_ALL,0,106,101,&page)==ARTICLE_OK);page_cleared(&page);
    assert(article_news(&actor,2,ARTICLE_ALL,0,UINT64_MAX,0,&page)==ARTICLE_OK);page_cleared(&page);
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,NULL)==ARTICLE_INVALID_READ);
    epoch++;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_REVOKED);page_cleared(&page);
    epoch--;suspended=1;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_REVOKED);page_cleared(&page);
    suspended=0;actor.user=99;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_REVOKED);page_cleared(&page);

    actor=page_reset(3);
    for(uint32_t i=1;i<=3;i++)page_items[i].created=100;
    missing_id=2;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_STORAGE);page_cleared(&page);
    missing_id=0;visibility_error=2;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_STORAGE);page_cleared(&page);
    visibility_error=0;page_count=CONTENT_ID_MAX+1;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_STORAGE);page_cleared(&page);page_count=3;
    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        *errors[i]=1;
        assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==wanted[i]);page_cleared(&page);
        *errors[i]=0;
    }
    actor=page_reset(300);page_items[300].created=100;
    assert(article_news(&actor,2,ARTICLE_ALL,0,0,0,&page)==ARTICLE_OK);
    assert(!page.count && page.next==257); /* rejected candidates still consume budget */
    assert(article_news(&actor,2,ARTICLE_ALL,page.next,0,0,&page)==ARTICLE_OK);
    assert(page.count==1 && page.rows[0].id==300 && !page.next);
    assert(!body_reads && !candidate_calls && !mark_calls && !context);
    for(unsigned i=0;i<=300;i++)assert(!body_probes[i]);
    puts("PASS: NEWS scope/ACL, timestamp bounds, paging, no body reads, revoked grants and atomic errors");
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
static ArticleActor notes_reset(uint32_t count) {
    ArticleActor actor=page_reset(count);memset(tree_next_ids,0,sizeof(tree_next_ids));tree_steps=0;
    for(uint32_t i=1;i<=count;i++) { page_items[i].root=i;page_items[i].created=1000+i; }
    return actor;
}
static void notes_cleared(const ArticleNotesPage *page) {
    const unsigned char *p=(const unsigned char *)page;
    for(size_t i=0;i<sizeof(*page);i++)assert(!p[i]);
    assert(!context && !commits && !moderation_writes && !publications && !body_reads && !mark_calls);
    for(unsigned i=0;i<=300;i++)assert(!body_probes[i]);
}
static void notes_tests(void) {
    ArticleNotesPage page;ArticleActor actor=notes_reset(10);
    page_items[2].root=page_items[3].root=1;page_items[2].parent=page_items[3].parent=1;
    tree_next_ids[1]=3;tree_next_ids[3]=2;
    page_items[2].created=9000;page_items[3].created=5000;
    page_hidden[5]=1;page_items[6].board=3;read_flags[1][1]=1;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_OK);
    assert(page.total==6 && page.count==5 && page.next==10 && page.unread_known);
    uint32_t expected[]={1,4,7,8,9};
    for(unsigned i=0;i<5;i++)assert(page.rows[i].summary.id==expected[i]);
    assert(page.rows[0].replies==2 && page.rows[0].latest==3 && page.rows[0].updated==5000 && page.rows[0].unread==2);
    strcpy(page_items[1].title,"changed");assert(!strcmp(page.rows[0].summary.title,"row 1"));
    assert(article_notes(&actor,2,page.next,0,&page)==ARTICLE_OK);
    assert(page.total==6 && page.count==1 && !page.next && page.rows[0].summary.id==10);
    assert(page.rows[0].updated==1010 && page.rows[0].latest==10 && !page.rows[0].replies);
    assert(article_notes(&actor,2,2,0,&page)==ARTICLE_OK);
    assert(page.total==6 && page.count==5 && !page.next && page.rows[0].summary.id==4);
    assert(article_notes(&actor,2,UINT32_MAX,0,&page)==ARTICLE_OK);
    assert(page.total==6 && !page.count && !page.next);
    page_hidden[3]=1;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_OK);
    assert(page.rows[0].replies==1 && page.rows[0].latest==2 && page.rows[0].updated==9000 && page.rows[0].unread==1);
    read_flags[1][2]=1;
    assert(article_notes(&actor,2,0,1,&page)==ARTICLE_OK);
    assert(page.total==5 && page.count==5 && page.rows[0].summary.id==4 && !page.next);
    actor.user=2;
    assert(article_notes(&actor,2,0,1,&page)==ARTICLE_OK);
    assert(page.total==6 && page.rows[0].summary.id==1 && page.rows[0].unread==2);
    unsigned calls=seen_calls;
    assert(article_notes(NULL,2,0,0,&page)==ARTICLE_OK);
    assert(!page.unread_known && !page.rows[0].unread && seen_calls==calls);
    assert(article_notes(NULL,2,0,1,&page)==ARTICLE_LOGIN_REQUIRED);notes_cleared(&page);
    actor.user=0;
    assert(article_notes(&actor,2,0,1,&page)==ARTICLE_LOGIN_REQUIRED);notes_cleared(&page);
    page_hidden[1]=1;
    assert(article_notes(NULL,2,0,0,&page)==ARTICLE_OK);
    assert(page.total==5 && page.rows[0].summary.id==4); /* Concealed parent hides the whole thread. */

    actor=notes_reset(0);
    assert(article_notes(NULL,2,0,0,&page)==ARTICLE_OK);notes_cleared(&page);
    actor=notes_reset(3);memset(read_flags[1],1,sizeof(read_flags[1]));
    assert(article_notes(&actor,2,0,1,&page)==ARTICLE_OK);
    assert(page.unread_known && !page.total && !page.count && !page.next);
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_OK);assert(page.total==3 && !page.rows[0].unread);
    page_items[1].created=0;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_OK);assert(!page.rows[0].updated);
    actor.user=99;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_REVOKED);notes_cleared(&page);
    actor.user=1;epoch++;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_REVOKED);notes_cleared(&page);
    epoch--;suspended=1;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_REVOKED);notes_cleared(&page);
    suspended=0;readable=0;manager=1;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_BOARD_DENIED);notes_cleared(&page);
    readable=1;locked=1;grants[2]=9;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_OK);
    grant_revision++;
    assert(article_notes(&actor,2,2,0,&page)==ARTICLE_BOARD_DENIED);notes_cleared(&page);

    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        actor=notes_reset(7);*errors[i]=1;
        assert(article_notes(&actor,2,0,0,&page)==wanted[i]);notes_cleared(&page);
    }
    actor=notes_reset(7);visibility_error=7;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_STORAGE);notes_cleared(&page);
    visibility_error=0;missing_id=7;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_STORAGE);notes_cleared(&page);
    missing_id=0;seen_error=7;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_READ_STATE_STORAGE);notes_cleared(&page);
    actor=notes_reset(7);page_items[7].root=6;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_STORAGE);notes_cleared(&page);
    actor=tree_reset(3);tree_next_ids[3]=2;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_STORAGE);notes_cleared(&page);assert(tree_steps<=3);
    actor=tree_reset(3);page_hidden[2]=page_hidden[3]=1;tree_next_ids[3]=2;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_STORAGE);notes_cleared(&page);assert(tree_steps<=3);
    actor=tree_reset(3);page_items[2].board=3;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_STORAGE);notes_cleared(&page);
    actor=tree_reset(3);tree_next_ids[3]=4;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_STORAGE);notes_cleared(&page);
    actor=notes_reset(300);
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_OK);
    assert(page.total==300 && page.count==5 && page.next==6 && tree_steps==300 && seen_calls==300);
    actor=tree_reset(300);
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_OK);
    assert(page.total==1 && page.rows[0].replies==299 && page.rows[0].latest==300 && page.rows[0].unread==300 && tree_steps==300);
    page_count=UINT32_MAX;
    assert(article_notes(&actor,2,0,0,&page)==ARTICLE_STORAGE);notes_cleared(&page);
    assert(article_notes(&actor,2,0,0,NULL)==ARTICLE_INVALID_READ);
    puts("PASS: structured thread summaries, visible statistics, member unread filter, guest pages, revoked authority and bounded traversal");
}
static ArticleActor draft_reset(void) {
    ArticleActor actor=reset();
    memset(saved_copies,0,sizeof(saved_copies));draft_reads=draft_writes=0;
    draft_read_error=draft_write_error=0;
    return actor;
}
static void draft_error(ArticleOutcome actual,ArticleOutcome expected,const ArticleDraftView *view,const char *body,size_t size) {
    ArticleDraftView empty={0};assert(actual==expected && !context);
    assert(!memcmp(view,&empty,sizeof(empty)));
    for(size_t i=0;i<size;i++)assert(!body[i]);
}
static void draft_tests(void) {
    ArticleActor actor=draft_reset(),other=actor;other.user=2;
    ArticleDraftView view;char body[64];uint32_t revision=999;
    assert(article_draft_status(&actor,&revision)==ARTICLE_OK && !revision);
    assert(article_draft_resume(&actor,&view,body,sizeof(body))==ARTICLE_OK && !view.revision && !*body);
    assert(article_draft_save(&actor,2,7,"draft","text\r\n",0,&revision)==ARTICLE_OK && revision==1);
    assert(saved_copies[1].board==2 && saved_copies[1].parent==7 && !context);
    assert(article_draft_status(&other,&revision)==ARTICLE_OK && !revision);
    assert(article_draft_resume(&other,&view,body,sizeof(body))==ARTICLE_OK && !view.revision && !*body);
    manager=1;assert(article_draft_remove(&other,1,&revision)==ARTICLE_DRAFT_CONFLICT && !revision);
    assert(article_draft_resume(&actor,&view,body,sizeof(body))==ARTICLE_OK);
    assert(view.board==2 && view.parent==7 && view.revision==1 && !strcmp(view.title,"draft") && !strcmp(body,"text\r\n"));
    assert(article_draft_save(&actor,2,7,"newer","new\r\n",view.revision,&revision)==ARTICLE_OK && revision==2);
    assert(article_draft_save(&actor,2,7,"stale","old\r\n",view.revision,&revision)==ARTICLE_DRAFT_CONFLICT && !revision);
    assert(article_draft_save(&actor,2,7,"fresh","",0,&revision)==ARTICLE_DRAFT_CONFLICT && !revision);
    assert(article_draft_remove(&actor,1,&revision)==ARTICLE_DRAFT_CONFLICT && !revision);
    assert(!strcmp(saved_copies[1].title,"newer") && saved_copies[1].revision==2);
    /* Losing board access prevents resume/save, but not checking/removing own copy. */
    readable=0;
    memset(&view,0x7f,sizeof(view));memset(body,0x7f,sizeof(body));
    draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),ARTICLE_BOARD_DENIED,&view,body,sizeof(body));
    unsigned before=draft_writes;
    assert(article_draft_save(&actor,2,7,"title","",2,&revision)==ARTICLE_BOARD_DENIED && !revision && draft_writes==before);
    assert(article_draft_status(&actor,&revision)==ARTICLE_OK && revision==2);
    assert(article_draft_remove(&actor,2,&revision)==ARTICLE_OK && revision==3);
    assert(!saved_copies[1].board && saved_copies[1].revision==3);
    assert(article_draft_status(&actor,&revision)==ARTICLE_OK && !revision);
    assert(article_draft_remove(&actor,3,&revision)==ARTICLE_DRAFT_CONFLICT && !revision);
    readable=1;
    assert(article_draft_save(&actor,2,0,"empty","",0,&revision)==ARTICLE_OK && revision==4);
    assert(article_draft_resume(&actor,&view,body,1)==ARTICLE_OK && view.revision==4 && !*body);
    /* Save is not a post: no article publication, quota reservation or read marks. */
    assert(!commits && !publications && !mark_calls && !seen_calls);

    actor=draft_reset();
    assert(article_draft_save(&actor,2,7,"draft","secret\r\n",0,&revision)==ARTICLE_OK);
    before=draft_writes;writable=0;
    draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),ARTICLE_BOARD_DENIED,&view,body,sizeof(body));
    assert(article_draft_save(&actor,2,7,"x","",1,&revision)==ARTICLE_BOARD_DENIED);
    writable=1;locked=1;
    draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),ARTICLE_BOARD_DENIED,&view,body,sizeof(body));
    grants[2]=(uint32_t)grant_revision;
    assert(article_draft_resume(&actor,&view,body,sizeof(body))==ARTICLE_OK);
    grant_revision++;
    draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),ARTICLE_BOARD_DENIED,&view,body,sizeof(body));
    locked=0;hidden=1;
    draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),ARTICLE_HIDDEN,&view,body,sizeof(body));
    assert(article_draft_save(&actor,2,7,"x","",1,&revision)==ARTICLE_HIDDEN && !revision);
    hidden=-1;
    draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),ARTICLE_STORAGE,&view,body,sizeof(body));
    hidden=0;stored.board=3;
    draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),ARTICLE_NOT_FOUND,&view,body,sizeof(body));
    stored.board=2;saved_copies[1].parent=8;
    draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),ARTICLE_NOT_FOUND,&view,body,sizeof(body));
    saved_copies[1].parent=7;
    draft_error(article_draft_resume(&actor,&view,body,8),ARTICLE_BUFFER_TOO_SMALL,&view,body,8);
    assert(article_draft_resume(&actor,&view,body,9)==ARTICLE_OK && !strcmp(body,"secret\r\n"));
    assert(article_draft_resume(&actor,&view,NULL,0)==ARTICLE_BUFFER_TOO_SMALL && !view.revision);
    assert(draft_writes==before && saved_copies[1].revision==1);

    /* All entry points reject guests, revoked/unknown identities and broken stores. */
    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome failures[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<9;i++) {
        actor=draft_reset();ArticleOutcome want=ARTICLE_REVOKED;
        if(i<4) { *errors[i]=1;want=failures[i]; }
        else if(i==4) { actor.user=0;want=ARTICLE_LOGIN_REQUIRED; }
        else if(i==5)epoch++;
        else if(i==6)suspended=1;
        else if(i==7)actor.user=99;
        else { draft_read_error=1;want=ARTICLE_MEMBER_STORAGE; }
        assert(article_draft_status(&actor,&revision)==want && !revision);
        draft_error(article_draft_resume(&actor,&view,body,sizeof(body)),want,&view,body,sizeof(body));
        assert(article_draft_save(&actor,2,0,"x","",0,&revision)==want && !revision);
        assert(article_draft_remove(&actor,1,&revision)==want && !revision);
        assert(!draft_writes && !context);
    }
    /* Inject read failure independently at every entry, including partial body. */
    for(unsigned op=0;op<4;op++) {
        actor=draft_reset();draft_read_error=1;ArticleOutcome result;
        if(op==0)result=article_draft_status(&actor,&revision);
        else if(op==1)result=article_draft_resume(&actor,&view,body,sizeof(body));
        else if(op==2)result=article_draft_save(&actor,2,0,"x","",0,&revision);
        else result=article_draft_remove(&actor,1,&revision);
        assert(result==ARTICLE_MEMBER_STORAGE && draft_reads==1 && !draft_writes && !context);
        if(op==1)draft_error(result,ARTICLE_MEMBER_STORAGE,&view,body,sizeof(body));
        else assert(!revision);
    }
    actor=draft_reset();
    assert(article_draft_status(NULL,&revision)==ARTICLE_LOGIN_REQUIRED && !revision);
    assert(article_draft_save(&actor,2,0,NULL,"",0,&revision)==ARTICLE_INVALID_DRAFT && !revision);
    assert(article_draft_save(&actor,2,0,"", "",0,&revision)==ARTICLE_INVALID_DRAFT);
    assert(article_draft_save(&actor,2,0,"x",NULL,0,&revision)==ARTICLE_INVALID_DRAFT);
    char title[82];memset(title,'x',81);title[81]=0;
    assert(article_draft_save(&actor,2,0,title,"",0,&revision)==ARTICLE_INVALID_DRAFT);
    static char large[CONTENT_BODY+2];memset(large,'x',sizeof(large)-1);large[sizeof(large)-1]=0;
    assert(article_draft_save(&actor,2,0,"x",large,0,&revision)==ARTICLE_INVALID_DRAFT && !draft_writes);
    title[80]=0;large[CONTENT_BODY-2]='\r';large[CONTENT_BODY-1]='\n';large[CONTENT_BODY]=0;
    assert(article_draft_save(&actor,2,0,title,large,0,&revision)==ARTICLE_OK && revision==1);
    memset(large,0x7f,sizeof(large));
    assert(article_draft_resume(&actor,&view,large,sizeof(large))==ARTICLE_OK && strlen(large)==CONTENT_BODY && strlen(view.title)==80);
    draft_write_error=1;
    assert(article_draft_save(&actor,2,0,"failure","",1,&revision)==ARTICLE_MEMBER_STORAGE && !revision);
    assert(article_draft_remove(&actor,1,&revision)==ARTICLE_MEMBER_STORAGE && !revision);
    assert(saved_copies[1].revision==1 && !context);
    assert(article_draft_status(&actor,NULL)==ARTICLE_INVALID_DRAFT);
    assert(article_draft_save(&actor,2,0,"x","",1,NULL)==ARTICLE_INVALID_DRAFT);
    assert(article_draft_remove(&actor,1,NULL)==ARTICLE_INVALID_DRAFT);
    memset(body,0x7f,sizeof(body));assert(article_draft_resume(&actor,NULL,body,sizeof(body))==ARTICLE_INVALID_DRAFT);
    for(unsigned i=0;i<sizeof(body);i++)assert(!body[i]);
    puts("PASS: saved draft Core ownership, revision conflicts, ACL/parent rechecks, failed-output clearing and body boundaries");
}

static void entry_tests(void) {
    ArticleActor actor=discovery_reset();ArticleBoardEntry entry;ArticleBoardInfo info;
    char text[64];
    entry_description="board description";entry_information="line 1\r\nline 2\r\n";
    assert(article_enter_board(NULL,1,&entry)==ARTICLE_OK && entry.id==1 && !entry.menu && !entry.password_board);
    assert(!strcmp(entry.title,"board 1") && !context);
    entry_menu=1;
    assert(article_enter_board(&actor,4,&entry)==ARTICLE_OK && entry.id==4 && entry.menu);
    assert(article_board_info(&actor,4,&info,text,sizeof(text))==ARTICLE_OK);
    assert(info.id==4 && !strcmp(info.title,"board 4") && !strcmp(info.description,entry_description));
    assert(info.information_size==strlen(entry_information) && !strcmp(text,entry_information));
    /* Owned output is independent of later store changes. */
    entry_information="changed";assert(!strcmp(text,"line 1\r\nline 2\r\n"));
    entry_information="";
    assert(article_board_info(NULL,1,&info,text,1)==ARTICLE_OK && !info.information_size && !*text);
    unsigned before=entry_metadata_reads;
    assert(article_enter_board(&actor,3,&entry)==ARTICLE_BOARD_DENIED);
    discovery_cleared(&entry,sizeof(entry));
    memset(text,0x7f,sizeof(text));
    assert(article_board_info(&actor,3,&info,text,sizeof(text))==ARTICLE_BOARD_DENIED);
    discovery_cleared(&info,sizeof(info));discovery_cleared(text,sizeof(text));
    assert(entry_metadata_reads==before);
    for(unsigned i=0;i<3;i++) {
        uint32_t invalid[]={0,12,CATALOG_MAX+1};
        assert(article_enter_board(&actor,invalid[i],&entry)==ARTICLE_BOARD_DENIED);
        discovery_cleared(&entry,sizeof(entry));
        assert(article_board_info(&actor,invalid[i],&info,text,sizeof(text))==ARTICLE_BOARD_DENIED);
        discovery_cleared(&info,sizeof(info));
    }
    /* Ancestor password ID is distinct from the requested destination/revision. */
    locked=entry_inherited=1;before=entry_metadata_reads;
    assert(article_enter_board(&actor,4,&entry)==ARTICLE_OK && entry.id==4 && entry.password_board==2);
    assert(!entry.menu && !*entry.title && entry_metadata_reads==before);
    assert(article_board_info(&actor,4,&info,text,sizeof(text))==ARTICLE_BOARD_DENIED);
    discovery_cleared(&info,sizeof(info));discovery_cleared(text,sizeof(text));
    assert(entry_metadata_reads==before);
    grants[2]=(uint32_t)grant_revision;
    assert(article_enter_board(&actor,4,&entry)==ARTICLE_OK && !entry.password_board && entry.menu);
    assert(article_board_info(&actor,4,&info,text,sizeof(text))==ARTICLE_OK);
    grant_revision++;
    assert(article_enter_board(&actor,4,&entry)==ARTICLE_OK && entry.password_board==2 && !*entry.title);
    assert(article_board_info(&actor,4,&info,text,sizeof(text))==ARTICLE_BOARD_DENIED);
    manager=1;
    assert(article_enter_board(&actor,4,&entry)==ARTICLE_OK && !entry.password_board);
    assert(article_board_info(&actor,4,&info,text,sizeof(text))==ARTICLE_OK);
    manager=0;board_visible[4]=0;
    assert(article_enter_board(&actor,4,&entry)==ARTICLE_BOARD_DENIED);discovery_cleared(&entry,sizeof(entry));
    assert(!mark_calls && !seen_calls && !body_reads && !commits && !publications);

    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome wanted[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<7;i++) {
        actor=discovery_reset();ArticleOutcome expected=ARTICLE_REVOKED;
        if(i<4) { *errors[i]=1;expected=wanted[i]; }
        else if(i==4)epoch++;
        else if(i==5)suspended=1;
        else actor.user=99;
        before=entry_metadata_reads;
        assert(article_enter_board(&actor,2,&entry)==expected);discovery_cleared(&entry,sizeof(entry));
        memset(text,0x7f,sizeof(text));
        assert(article_board_info(&actor,2,&info,text,sizeof(text))==expected);
        discovery_cleared(&info,sizeof(info));discovery_cleared(text,sizeof(text));assert(entry_metadata_reads==before);
    }
    actor=discovery_reset();entry_information="1234";
    assert(article_board_info(&actor,2,&info,text,4)==ARTICLE_BUFFER_TOO_SMALL);
    discovery_cleared(&info,sizeof(info));discovery_cleared(text,4);
    assert(article_board_info(&actor,2,&info,text,5)==ARTICLE_OK && !strcmp(text,"1234"));
    assert(article_board_info(&actor,2,&info,NULL,0)==ARTICLE_BUFFER_TOO_SMALL);
    assert(article_enter_board(&actor,2,NULL)==ARTICLE_INVALID_READ);
    memset(text,0x7f,sizeof(text));assert(article_board_info(&actor,2,NULL,text,sizeof(text))==ARTICLE_INVALID_READ);
    discovery_cleared(text,sizeof(text));
    static char long_info[BOARD_INFO_MAX+2],long_output[BOARD_INFO_MAX+2],description[322],title[82];
    memset(long_info,'x',BOARD_INFO_MAX+1);long_info[BOARD_INFO_MAX+1]=0;entry_information=long_info;
    assert(article_board_info(&actor,2,&info,long_output,sizeof(long_output))==ARTICLE_POLICY_STORAGE);
    discovery_cleared(&info,sizeof(info));discovery_cleared(long_output,sizeof(long_output));
    long_info[BOARD_INFO_MAX]=0;memset(description,'d',320);description[320]=0;entry_description=description;
    memset(title,'t',80);title[80]=0;entry_title_override=title;
    assert(article_board_info(&actor,2,&info,long_output,sizeof(long_output))==ARTICLE_OK);
    assert(info.information_size==BOARD_INFO_MAX && strlen(info.description)==320 && strlen(info.title)==80);
    assert(!strcmp(long_output,long_info));
    description[320]='d';description[321]=0;
    assert(article_board_info(&actor,2,&info,long_output,sizeof(long_output))==ARTICLE_STORAGE);
    discovery_cleared(&info,sizeof(info));discovery_cleared(long_output,sizeof(long_output));
    description[320]=0;title[80]='t';title[81]=0;
    assert(article_enter_board(&actor,2,&entry)==ARTICLE_STORAGE);discovery_cleared(&entry,sizeof(entry));
    assert(article_board_info(&actor,2,&info,text,sizeof(text))==ARTICLE_STORAGE);discovery_cleared(&info,sizeof(info));
    entry_lock_fault=CATALOG_MAX+1;
    assert(article_enter_board(&actor,2,&entry)==ARTICLE_POLICY_STORAGE);discovery_cleared(&entry,sizeof(entry));
    entry_lock_fault=-1;
    assert(article_enter_board(&actor,2,&entry)==ARTICLE_POLICY_STORAGE);discovery_cleared(&entry,sizeof(entry));
    assert(!context);
    puts("PASS: board entry/INFO Core ACL, ancestor password challenge, revoked grants, menu destinations and owned bounded output");
}

static ArticleActor audit_reset(void) {
    ArticleActor actor=discovery_reset();manager=1;audit_admin=1;
    memset(audit_rows,0,sizeof(audit_rows));memset(audit_mod_sequence,0,sizeof(audit_mod_sequence));
    audit_extra_failure=audit_mod_failure=audit_reads=0;
    moderation_entries=5;extras_entries=7;audit_sigop[2]=1;
    page_items[1].board=2;page_items[2].board=3;page_items[3].board=4;
    for(uint32_t i=1;i<=12;i++)audit_rows[i]=(ModerationEntry){i,1,2,2,1700000000+i,'T',0};
    audit_rows[1].kind='M';audit_rows[1].enabled=1;audit_rows[1].actor=1;audit_mod_sequence[1]=1;
    audit_rows[2].kind='S';audit_rows[2].enabled=1;audit_rows[2].actor=1;audit_rows[2].target=2;audit_rows[2].board=0;audit_mod_sequence[2]=2;
    audit_rows[3].kind='D';
    audit_rows[4].kind='M';audit_rows[4].target=3;audit_rows[4].board=4;audit_mod_sequence[4]=3;
    audit_rows[5].kind='A';audit_rows[5].target=3;
    audit_rows[6].kind='K';audit_rows[6].enabled=1;audit_rows[6].actor=1;audit_rows[6].target=2;audit_rows[6].board=0;audit_mod_sequence[6]=4;
    audit_rows[7].kind='S';audit_rows[7].actor=1;audit_rows[7].target=2;audit_rows[7].board=0;audit_mod_sequence[7]=5;
    audit_rows[9].kind='D';audit_rows[9].target=2;
    audit_rows[11].kind='A';audit_rows[11].actor=1;
    audit_rows[12].target=3;
    return actor;
}
static void audit_tests(void) {
    ArticleActor actor=audit_reset();ArticleAuditPage page,by_id;
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_OK);
    uint32_t expected[]={12,11,10,8,7,6,5,4};
    assert(page.start==12 && page.next==3 && page.count==8 && !context);
    for(unsigned i=0;i<8;i++)assert(page.rows[i].sequence==expected[i]);
    assert(page.rows[0].board==4 && page.rows[0].actor==2 && !strcmp(page.rows[0].actor_name,"bob"));
    assert(page.rows[4].action==ARTICLE_AUDIT_RESUME && page.rows[5].action==ARTICLE_AUDIT_CHAT_KICK);
    assert(page.rows[7].action==ARTICLE_AUDIT_RESTORE && page.rows[0].time==1700000012);
    assert(article_audit(&actor,2,page.next,0,0,NULL,&page)==ARTICLE_OK && page.count==3 && !page.next);
    assert(page.rows[0].action==ARTICLE_AUDIT_WITHDRAW && page.rows[1].action==ARTICLE_AUDIT_SUSPEND && page.rows[2].action==ARTICLE_AUDIT_HIDE);
    assert(article_audit(&actor,2,0,1,0,"bob",&page)==ARTICLE_OK);
    assert(page.count==3 && page.rows[0].sequence==10 && page.rows[1].sequence==8 && page.rows[2].sequence==3 && page.actor==2 && page.article==1);
    assert(article_audit(&actor,2,0,1,2,NULL,&by_id)==ARTICLE_OK && !memcmp(&page,&by_id,sizeof(page)));
    page_hidden[1]=1; /* Hidden/withdrawn events remain auditable. */
    assert(article_audit(&actor,2,0,1,2,NULL,&page)==ARTICLE_OK && page.count==3);
    assert(article_audit(&actor,2,0,2,0,NULL,&page)==ARTICLE_OK && !page.count && !page.next);
    audit_admin=0;
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_OK && page.count==5);
    uint32_t own[]={11,10,8,3,1};for(unsigned i=0;i<5;i++)assert(page.rows[i].sequence==own[i]);
    audit_sigop[4]=1; /* Effective inherited role is supplied by catalog. */
    assert(article_audit(&actor,2,0,3,0,NULL,&page)==ARTICLE_OK && page.count==3);
    audit_sigop[4]=0;
    assert(article_audit(&actor,2,0,3,0,NULL,&page)==ARTICLE_OK && !page.count);
    /* Managers bypass board passwords; administrator status does not bypass
     * a board's independent read ACL. Do not model SIGOPs as password-blocked. */
    audit_admin=1;board_readable[2]=0;
    assert(article_audit(&actor,2,0,1,0,NULL,&page)==ARTICLE_OK && !page.count);
    board_readable[2]=1;
    assert(article_audit(&actor,2,0,1,0,NULL,&page)==ARTICLE_OK && page.count==5);
    board_readable[2]=0;
    assert(article_audit(&actor,2,0,1,0,NULL,&page)==ARTICLE_OK && !page.count);
    manager=0;unsigned reads=audit_reads;
    assert(article_audit(&actor,2,0,0,0,"unknown",&page)==ARTICLE_ADMIN_REQUIRED);discovery_cleared(&page,sizeof(page));
    assert(audit_reads==reads);
    assert(!body_reads && !mark_calls && !seen_calls && !commits && !publications);

    actor=audit_reset();
    for(unsigned which=0;which<3;which++) {
        ArticleActor denied=actor;if(which==0)denied.user=0;else if(which==1)denied.epoch++;else denied.user=99;
        assert(article_audit(&denied,2,0,0,0,NULL,&page)==(which?ARTICLE_REVOKED:ARTICLE_ADMIN_REQUIRED));
        discovery_cleared(&page,sizeof(page));assert(!audit_reads);
    }
    assert(article_audit(&actor,3,0,0,0,NULL,&page)==ARTICLE_ADMIN_REQUIRED);
    assert(article_audit(&actor,2,13,0,0,NULL,&page)==ARTICLE_INVALID_CURSOR);
    assert(article_audit(&actor,2,0,0,99,NULL,&page)==ARTICLE_INVALID_QUERY);
    assert(article_audit(&actor,2,0,0,0,"missing",&page)==ARTICLE_INVALID_QUERY);
    assert(article_audit(&actor,2,0,0,2,"bob",&page)==ARTICLE_INVALID_QUERY);
    discovery_cleared(&page,sizeof(page));assert(!audit_reads);
    lookup_error=1;
    assert(article_audit(&actor,2,0,0,0,"bob",&page)==ARTICLE_MEMBER_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=audit_reset();audit_extra_failure=10;
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_EXTRAS_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=audit_reset();audit_mod_failure=5;
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_MODERATION_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=audit_reset();audit_mod_sequence[10]=6;
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_MODERATION_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=audit_reset();audit_rows[10].actor=99;
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_MEMBER_STORAGE);discovery_cleared(&page,sizeof(page));
    actor=audit_reset();audit_rows[10].kind='?';
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_EXTRAS_STORAGE);discovery_cleared(&page,sizeof(page));
    int *errors[]={&member_error,&moderation_error,&policy_error,&extras_error};
    ArticleOutcome failures[]={ARTICLE_MEMBER_STORAGE,ARTICLE_MODERATION_STORAGE,ARTICLE_POLICY_STORAGE,ARTICLE_EXTRAS_STORAGE};
    for(unsigned i=0;i<4;i++) {
        actor=audit_reset();*errors[i]=1;
        assert(article_audit(&actor,2,0,0,0,NULL,&page)==failures[i]);discovery_cleared(&page,sizeof(page));assert(!audit_reads);
    }
    actor=audit_reset();moderation_entries=UINT32_MAX;extras_entries=1;
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_STORAGE);discovery_cleared(&page,sizeof(page));assert(!audit_reads);
    actor=audit_reset();moderation_entries=extras_entries=0;
    assert(article_audit(&actor,2,0,0,0,NULL,&page)==ARTICLE_OK);discovery_cleared(&page,sizeof(page));
    /* Filtered sparse pages still stop at 256 scanned events, not 256 matches. */
    actor=audit_reset();moderation_entries=0;extras_entries=300;memset(audit_mod_sequence,0,sizeof(audit_mod_sequence));
    for(unsigned i=1;i<=300;i++)audit_rows[i]=(ModerationEntry){i,1,2,1,0,'T',0};
    audit_rows[1].actor=2;
    assert(article_audit(&actor,2,0,0,2,NULL,&page)==ARTICLE_OK && page.start==300 && !page.count && page.next==44 && audit_reads==256);
    assert(article_audit(&actor,2,page.next,0,2,NULL,&page)==ARTICLE_OK && page.count==1 && page.rows[0].sequence==1 && !page.next && audit_reads==300);
    assert(article_audit(&actor,2,0,0,0,NULL,NULL)==ARTICLE_INVALID_READ);
    assert(!context);
    puts("PASS: audit Core manager/SIGOP scope, merged ordering, actor/article filters, bounded paging and atomic failure clearing");
}

static void import_tests(void) {
    ArticleActor actor=reset();char scratch[64];writable=0;
    assert(article_import_buffer(&actor,2,7,scratch,sizeof(scratch))==ARTICLE_OK);
    assert(buffer_calls==1 && buffer_owner==1 && !strcmp(saved_buffer,persisted_body));
    for(unsigned i=0;i<sizeof(scratch);i++)assert(!scratch[i]);
    assert(!mark_calls && !commits && !context);
    puts("PASS: article import needs read not post permission; preserves read state and clears successful scratch");
    for(unsigned i=0;i<10;i++) {
        actor=reset();ArticleOutcome expected=ARTICLE_NOT_FOUND;uint32_t board=2;
        if(i==0)hidden=1;
        else if(i==1) { hidden=1;manager=1; }
        else if(i==2)removed=1;
        else if(i==3)readable=0;
        else if(i==4)board=3;
        else if(i==5)locked=1;
        else if(i==6) { actor.user=0;expected=ARTICLE_LOGIN_REQUIRED; }
        else if(i==7) { actor.epoch++;expected=ARTICLE_REVOKED; }
        else if(i==8) { suspended=1;expected=ARTICLE_REVOKED; }
        else { policy_error=1;expected=ARTICLE_POLICY_STORAGE; }
        memset(scratch,'x',sizeof(scratch));
        assert(article_import_buffer(&actor,board,7,scratch,sizeof(scratch))==expected);
        assert(!buffer_calls && !body_reads && !strcmp(saved_buffer,"preserved") && !context);
        for(unsigned j=0;j<sizeof(scratch);j++)assert(!scratch[j]);
    }
    actor=reset();locked=1;grants[2]=grant_revision;
    assert(article_import_buffer(&actor,2,7,scratch,sizeof(scratch))==ARTICLE_OK && !context);
    grants[2]--;buffer_calls=body_reads=0;
    assert(article_import_buffer(&actor,2,7,scratch,sizeof(scratch))==ARTICLE_NOT_FOUND && !buffer_calls && !body_reads);
    puts("PASS: hidden/withdrawn/wrong-board/locked import denial, no admin override and revoked actor/grant");
    for(unsigned i=0;i<4;i++) {
        actor=reset();
        if(i==0)body_ok=0;
        else if(i==1)profile_error=1;
        else if(i==2)buffer_result=0;
        else buffer_result=-1;
        assert(article_import_buffer(&actor,2,7,scratch,sizeof(scratch))==ARTICLE_STORAGE);
        assert(!strcmp(saved_buffer,"preserved") && !mark_calls && !context);
        for(unsigned j=0;j<sizeof(scratch);j++)assert(!scratch[j]);
    }
    actor=reset();assert(article_import_buffer(&actor,2,7,scratch,2)==ARTICLE_BUFFER_TOO_SMALL && !buffer_calls);
    assert(article_import_buffer(&actor,2,7,NULL,0)==ARTICLE_BUFFER_TOO_SMALL);
    puts("PASS: article import body/store failure, invalid scratch and preserved former buffer");
}
static void management_tests(void) {
    ArticleActor actor=reset();manager=1;
    assert(article_author_set(&actor,7,2)==ARTICLE_OK);
    assert(author_publications==1 && journal_actor==1 && journal_kind=='A' && !strcmp(journal_text,"bob"));
    actor=reset();manager=1;commit_ok=0;
    assert(article_author_set(&actor,7,2)==ARTICLE_SETTINGS_STORAGE && !author_publications);
    actor=reset();manager=1;pending_target=2;
    denied(article_author_set(&actor,7,2),ARTICLE_MANAGEMENT_INVALID);
    pending_target=0;disabled_target=2;
    denied(article_author_set(&actor,7,2),ARTICLE_MANAGEMENT_INVALID);
    denied(article_author_set(&actor,7,99),ARTICLE_MANAGEMENT_INVALID);
    actor=reset();manager=1;hidden=1;
    denied(article_author_set(&actor,7,2),ARTICLE_ADMIN_REQUIRED);
    hidden=-1;denied(article_author_set(&actor,7,2),ARTICLE_STORAGE);
    actor=reset();denied(article_author_set(&actor,7,2),ARTICLE_ADMIN_REQUIRED);
    manager=1;denied(article_author_set(&actor,99,2),ARTICLE_ADMIN_REQUIRED);
    actor=reset();manager=1;imported=1;
    assert(article_author_set(&actor,7,2)==ARTICLE_OK && journal_kind=='A');
    puts("PASS: author ownership validates target/actual board and publishes after journal only");

    actor=reset();manager=1;
    assert(article_guest_set(&actor,2,1)==ARTICLE_OK && guest_publications==1);
    assert(article_guest_set(&actor,2,0)==ARTICLE_OK && guest_publications==2);
    actor=reset();manager=1;guest_ok=0;
    assert(article_guest_set(&actor,2,1)==ARTICLE_SETTINGS_STORAGE && !guest_publications);
    actor=reset();manager=1;
    denied(article_guest_set(&actor,2,2),ARTICLE_MANAGEMENT_INVALID);
    denied(article_guest_set(&actor,3,1),ARTICLE_ADMIN_REQUIRED);
    manager=0;denied(article_guest_set(&actor,2,1),ARTICLE_ADMIN_REQUIRED);
    assert(!guest_saves);
    puts("PASS: guest posting policy enforces scope and durable-before-visible ordering");

    ArticleRoleChange change;
    actor=reset();board_present[2]=1;board_parent[2]=1;audit_sigop[2]=1;
    memset(&change,0xff,sizeof(change));
    assert(article_sigop_set(&actor,2,2,&change)==ARTICLE_MANAGEMENT_INVALID);
    assert(!role_calls && !epoch_changes && !change.changed && !change.previous && !change.current);
    audit_sigop[1]=1;
    assert(article_sigop_set(&actor,2,2,&change)==ARTICLE_OK);
    assert(role_calls==1 && epoch_changes==2 && change.changed && change.previous==3 && change.current==2);
    assert(change.previous_epoch==epoch && change.current_epoch==epoch);
    role_ok=2;epoch_changes=0;
    assert(article_sigop_set(&actor,2,2,&change)==ARTICLE_OK && !epoch_changes && !change.changed);
    role_ok=-1;
    assert(article_sigop_set(&actor,2,2,&change)==ARTICLE_CATALOG_STORAGE && !epoch_changes && !change.current);
    role_ok=0;
    assert(article_sigop_set(&actor,2,2,&change)==ARTICLE_MANAGEMENT_INVALID && !epoch_changes);
    actor=reset();audit_admin=1;board_present[2]=1;
    assert(article_sigop_set(&actor,2,0,&change)==ARTICLE_OK && change.previous==3 && !change.current && epoch_changes==1);
    actor=reset();audit_admin=1;pending_target=2;
    assert(article_sigop_set(&actor,2,2,&change)==ARTICLE_MANAGEMENT_INVALID && !role_calls);
    pending_target=0;
    assert(article_sigop_set(&actor,0,2,&change)==ARTICLE_MANAGEMENT_INVALID && !role_calls);
    puts("PASS: SIGOP delegation requires ancestor authority, with owned epoch changes and cleared failures");

    for(unsigned mode=0;mode<7;mode++) {
        actor=reset();manager=1;audit_admin=1;
        ArticleOutcome wanted=ARTICLE_REVOKED;
        if(mode==0)actor.user=0,wanted=ARTICLE_LOGIN_REQUIRED;
        if(mode==1)actor.epoch--;
        if(mode==2)suspended=1;
        if(mode==3)member_error=1,wanted=ARTICLE_MEMBER_STORAGE;
        if(mode==4)moderation_error=1,wanted=ARTICLE_MODERATION_STORAGE;
        if(mode==5)policy_error=1,wanted=ARTICLE_POLICY_STORAGE;
        if(mode==6)extras_error=1,wanted=ARTICLE_EXTRAS_STORAGE;
        assert(article_author_set(&actor,7,2)==wanted);
        assert(article_guest_set(&actor,2,1)==wanted);
        memset(&change,0xff,sizeof(change));
        assert(article_sigop_set(&actor,2,2,&change)==wanted);
        assert(!commits && !author_publications && !guest_saves && !role_calls && !epoch_changes);
        assert(!change.changed && !change.previous && !change.current && !change.previous_epoch && !change.current_epoch);
    }
    puts("PASS: management rejects stale/guest/suspended actors and broken stores before mutation");
}

int main(void) {
    import_tests();
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
    notes_tests();
    news_tests();
    find_scan_tests();
    navigation_tests();
    discovery_tests();
    posting_tests();
    draft_tests();
    entry_tests();
    audit_tests();
    management_tests();
    return 0;
}

/* Direct Core API tests: no UI authorization guard, live DB or socket. */
#include "mail_core.h"
#include "mail_repository.h"
#include "mailbox.h"
#include "mail_files.h"
#include "members.h"
#include "moderation.h"
#include "content_limits.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const MailActor alice={1,9},bob={2,9},admin={3,9};
static const char *names[]={NULL,"alice","bob","admin","pending","retired","suspended"};
static int member_error,moderation_error,mail_error,enabled,blocked[7],failure,body_failure;
static unsigned reads,writes,cleanup,ownership_checks,drop_calls,body_reads;
static uint32_t last_user,next_id,next_revision;
static MailMessage drafts[7],letters[3],cache;
static char saved_body[CONTENT_BODY+1];
static MailAttachments committed_attachments;
static MailWrite input;
static void reset(void) {
    member_error=moderation_error=mail_error=failure=body_failure=0;enabled=1;
    reads=writes=cleanup=ownership_checks=drop_calls=body_reads=0;last_user=0;
    memset(blocked,0,sizeof(blocked));blocked[4]=blocked[5]=blocked[6]=1;
    memset(drafts,0,sizeof(drafts));memset(letters,0,sizeof(letters));memset(&cache,0,sizeof(cache));
    memset(&committed_attachments,0,sizeof(committed_attachments));strcpy(saved_body,"private body\r\n");
    letters[0]=(MailMessage){.id=10,.sender=2,.recipient=1,.title="incoming"};
    letters[1]=(MailMessage){.id=11,.sender=1,.recipient=2,.title="outgoing"};
    letters[2]=(MailMessage){.id=12,.sender=3,.recipient=2,.title="other private"};
    input=(MailWrite){.to=2,.title="subject",.body="body\r\n"};next_id=20;next_revision=40;
}
int members_broken(void) { return member_error; }
int moderation_broken(void) { return moderation_error; }
const char *members_name(uint32_t id) { return id<7?names[id]:NULL; }
uint32_t members_find(const char *name) { for(unsigned i=1;i<7;i++)if(!strcmp(names[i],name))return i;return 0; }
int moderation_suspended(uint32_t id) { return id<7?blocked[id]:1; }
uint32_t moderation_epoch(uint32_t id) { (void)id;return 9; }
int mail_repository_enabled(void) { return enabled; }
int mailbox_broken(void) { return mail_error; }
const MailMessage *mailbox_get(uint32_t id,uint32_t user) {
    reads++;last_user=user;
    for(unsigned i=0;i<3;i++)if(letters[i].id==id && !letters[i].deleted &&
        (letters[i].sender==user || letters[i].recipient==user)) { cache=letters[i];return &cache; }
    return NULL;
}
const MailMessage *mailbox_draft(uint32_t user) {
    reads++;last_user=user;if(user>=7 || !drafts[user].id)return NULL;
    cache=drafts[user];return &cache;
}
const char *mailbox_body(const MailMessage *draft) {
    body_reads++;assert(draft->id==drafts[draft->sender].id);
    if(body_failure)mail_error=1;
    return saved_body;
}
static int write_failed(void) { if(failure==2)mail_error=1;return failure!=0; }
static int reply_valid(uint32_t user,uint32_t parent,uint32_t to) {
    if(!parent)return 1;
    const MailMessage *m=mailbox_get(parent,user);return m && m->recipient==user && m->sender==to;
}
int mailbox_save_draft(uint32_t user,uint32_t to,uint32_t parent,const char *title,const char *body,
                        const MailAttachments *attachments,uint32_t expected,uint32_t *revision) {
    writes++;last_user=user;*revision=999; /* failed store may dirty output */
    if(write_failed() || !members_name(to) || drafts[user].id!=expected || !reply_valid(user,parent,to) || !*title)return 0;
    drafts[user]=(MailMessage){.id=++next_revision,.sender=user,.recipient=to,.parent=parent,.attachments=*attachments};
    strcpy(drafts[user].title,title);strcpy(saved_body,body);committed_attachments=*attachments;
    *revision=drafts[user].id;return 1;
}
int mailbox_delete_draft(uint32_t user,uint32_t revision) {
    writes++;last_user=user;if(write_failed() || !drafts[user].id || drafts[user].id!=revision)return 0;
    drafts[user].id=0;return 1;
}
int mailbox_send(uint32_t user,uint32_t to,uint32_t parent,const char *title,const char *body,
                  const MailAttachments *attachments,uint32_t revision,uint32_t *id) {
    writes++;last_user=user;*id=999;
    if(write_failed() || !*title || !*body || (revision && drafts[user].id!=revision) || !reply_valid(user,parent,to))return 0;
    committed_attachments=*attachments;if(revision)drafts[user].id=0;*id=++next_id;return 1;
}
int mailbox_remove(uint32_t id,uint32_t user) {
    writes++;last_user=user;if(write_failed())return 0;
    for(unsigned i=0;i<3;i++)if(letters[i].id==id) { letters[i].deleted=1;return 1; }
    return 0;
}
int mail_files_owned(uint32_t user,uint32_t id) {
    ownership_checks++;last_user=user;return (user==1 && id>=101 && id<=117) || (user==2 && id==201);
}
void mail_files_cleanup(void) { cleanup++; }
int mail_files_drop(uint32_t user,uint32_t id) {
    drop_calls++;last_user=user;return id!=102 && mail_files_owned(user,id);
}
static int zero(const void *p,size_t n) { const unsigned char *b=p;for(size_t i=0;i<n;i++)if(b[i])return 0;return 1; }
static void seed(void) { drafts[1]=(MailMessage){.id=7,.sender=1,.recipient=2,.parent=10,.title="draft"}; }

static void authority(void) {
    MailActor actors[]={{0,0},{1,8},{4,9},{5,9},{6,9},{99,9}};
    for(unsigned i=0;i<sizeof(actors)/sizeof(*actors);i++) {
        reset();seed();MailOutcome expected=i?MAIL_CORE_REVOKED:MAIL_CORE_LOGIN_REQUIRED;
        MailCompose compose;MailDraft draft;MailAttachments selection={0},out;char body[32];uint32_t id=99;
        memset(&compose,1,sizeof(compose));memset(&draft,1,sizeof(draft));memset(body,1,sizeof(body));
        assert(mail_core_prepare(&actors[i],"bob",0,"title",&compose)==expected && zero(&compose,sizeof(compose)));
        assert(mail_core_draft_get(&actors[i],1,&draft,body,sizeof(body))==expected && zero(&draft,sizeof(draft)) && zero(body,sizeof(body)));
        assert(mail_core_draft_save(&actors[i],&input,&id)==expected && !id);
        id=99;assert(mail_core_send(&actors[i],&input,&id)==expected && !id);
        assert(mail_core_draft_delete(&actors[i],7)==expected);
        assert(mail_core_delete(&actors[i],10)==expected);
        assert(mail_core_attachment_select(&actors[i],&selection,101,0,&out)==expected && zero(&out,sizeof(out)));
        assert(mail_core_attachment_drop(&actors[i],101)==expected);
        assert(!reads && !writes && !cleanup && !ownership_checks && !drop_calls);
    }
    reset();MailCompose out;assert(mail_core_prepare(NULL,"bob",0,"subject",&out)==MAIL_CORE_LOGIN_REQUIRED);
    puts("PASS mail actor gates across all operations, no unauthorized reads/writes");
}
static void health(void) {
    for(unsigned i=0;i<4;i++) {
        reset();if(i==0)member_error=1;if(i==1)moderation_error=1;if(i==2)enabled=0;if(i==3)mail_error=1;
        MailOutcome expected[]={MAIL_CORE_MEMBER_STORAGE,MAIL_CORE_MODERATION_STORAGE,MAIL_CORE_DISABLED,MAIL_CORE_STORAGE};
        uint32_t id=9;MailCompose out;assert(mail_core_prepare(&alice,"bob",0,"subject",&out)==expected[i]);
        assert(mail_core_send(&alice,&input,&id)==expected[i] && !id);
        assert(!reads && !writes && !cleanup);
    }
    puts("PASS health and live mail feature policy fail closed");
}
static void prepare(void) {
    reset();MailCompose out;
    assert(mail_core_prepare(&alice,"bob",0,"new",&out)==MAIL_CORE_OK && out.to==2 && !out.parent && !strcmp(out.title,"new"));
    assert(mail_core_prepare(&alice,NULL,10,"",&out)==MAIL_CORE_OK && out.to==2 && out.parent==10 && !strcmp(out.title,"incoming"));
    assert(mail_core_prepare(&alice,NULL,10,"override",&out)==MAIL_CORE_OK && !strcmp(out.title,"override"));
    assert(mail_core_prepare(&alice,NULL,11,"",&out)==MAIL_CORE_UNAVAILABLE && zero(&out,sizeof(out)));
    assert(mail_core_prepare(&admin,NULL,10,"",&out)==MAIL_CORE_UNAVAILABLE); /* admin is not a participant */
    for(unsigned i=4;i<=6;i++)assert(mail_core_prepare(&alice,names[i],0,"title",&out)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_prepare(&alice,"missing",0,"title",&out)==MAIL_CORE_UNAVAILABLE);
    assert(!writes && !cleanup);puts("PASS exact recipient and incoming reply selection, no admin override/read side effect");
}
static void draft_views(void) {
    reset();seed();MailDraft out;char body[64];
    assert(mail_core_draft_get(&alice,0,&out,NULL,0)==MAIL_CORE_OK && out.revision==7 && !body_reads);
    assert(mail_core_draft_get(&alice,1,&out,body,sizeof(body))==MAIL_CORE_OK);
    assert(out.parent==10 && out.to==2 && !strcmp(out.title,"draft") && !strcmp(body,"private body\r\n"));
    cache.id=123;strcpy(cache.title,"overwritten");assert(out.revision==7 && !strcmp(out.title,"draft"));
    assert(mail_core_draft_get(&admin,1,&out,body,sizeof(body))==MAIL_CORE_UNAVAILABLE && zero(body,sizeof(body)));
    blocked[2]=1;
    assert(mail_core_draft_get(&alice,1,&out,body,sizeof(body))==MAIL_CORE_UNAVAILABLE && zero(&out,sizeof(out)));
    assert(mail_core_draft_get(&alice,0,&out,NULL,0)==MAIL_CORE_OK); /* metadata for cleanup */
    blocked[2]=0;letters[0].deleted=1;
    assert(mail_core_draft_get(&alice,1,&out,body,sizeof(body))==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_draft_get(&alice,0,&out,NULL,0)==MAIL_CORE_OK);
    assert(mail_core_draft_delete(&alice,7)==MAIL_CORE_OK && cleanup==1);
    puts("PASS owned draft snapshots, cache reuse, blocked recipient/deleted parent, metadata and cleanup");
}
static void draft_failures(void) {
    reset();seed();MailDraft out;char body[32];
    body_failure=1;assert(mail_core_draft_get(&alice,1,&out,body,sizeof(body))==MAIL_CORE_STORAGE);
    assert(zero(&out,sizeof(out)) && zero(body,sizeof(body)) && !writes);
    reset();seed();assert(mail_core_draft_get(&alice,1,&out,body,3)==MAIL_CORE_UNAVAILABLE && zero(body,3) && zero(&out,sizeof(out)));
    assert(mail_core_draft_get(&alice,2,&out,body,sizeof(body))==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_draft_get(&alice,1,&out,NULL,0)==MAIL_CORE_UNAVAILABLE);
    puts("PASS partial draft-body errors and buffer bounds clear whole output");
}
static void lifecycle(void) {
    reset();uint32_t revision=0,id=0;
    input.body="";assert(mail_core_draft_save(&alice,&input,&revision)==MAIL_CORE_OK && revision==41 && cleanup==1);
    assert(mail_core_draft_save(&alice,&input,&revision)==MAIL_CORE_UNAVAILABLE && !revision && cleanup==1);
    input.revision=41;input.body="second\r\n";
    assert(mail_core_draft_save(&alice,&input,&revision)==MAIL_CORE_OK && revision==42);
    assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_UNAVAILABLE && !id && drafts[1].id==42 && cleanup==2);
    assert(mail_core_draft_delete(&alice,41)==MAIL_CORE_UNAVAILABLE && cleanup==2);
    input.revision=42;assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_OK && id==21 && !drafts[1].id && cleanup==3);
    assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_UNAVAILABLE && !id && cleanup==3);
    input.revision=0;assert(mail_core_draft_save(&alice,&input,&revision)==MAIL_CORE_OK && revision==43);
    assert(mail_core_draft_delete(&bob,43)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_draft_delete(&alice,43)==MAIL_CORE_OK && cleanup==5);
    puts("PASS stale editor saves/sends/deletes, exact revisions, atomic send consumption and owner isolation");
}
static void attachment_selection(void) {
    reset();MailAttachments current={0},out;
    assert(mail_core_attachment_select(&alice,&current,101,0,&out)==MAIL_CORE_OK && out.count==1);
    current=out;assert(mail_core_attachment_select(&alice,&current,101,0,&out)==MAIL_CORE_OK && out.count==1);
    assert(mail_core_attachment_select(&admin,&current,101,0,&out)==MAIL_CORE_UNAVAILABLE && zero(&out,sizeof(out)));
    assert(mail_core_attachment_select(&alice,&current,201,0,&out)==MAIL_CORE_UNAVAILABLE);
    current.count=16;for(unsigned i=0;i<16;i++)current.ids[i]=101+i;
    assert(mail_core_attachment_select(&alice,&current,117,0,&out)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_attachment_select(&alice,&current,101,0,&out)==MAIL_CORE_OK && out.count==16);
    assert(mail_core_attachment_select(&alice,&current,101,1,&out)==MAIL_CORE_OK && out.count==15 && out.ids[0]==102);
    unsigned checks=ownership_checks;assert(mail_core_attachment_select(&alice,&current,201,1,&out)==MAIL_CORE_OK && out.count==16 && checks==ownership_checks);
    assert(mail_core_attachment_select(&alice,&current,0,0,&out)==MAIL_CORE_OK && zero(&out,sizeof(out)));
    current.count=17;assert(mail_core_attachment_select(&alice,&current,0,0,&out)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_attachment_select(&alice,NULL,0,0,&out)==MAIL_CORE_UNAVAILABLE);
    assert(!writes && !cleanup);puts("PASS attachment selection ownership, duplicate/full/detach/clear semantics");
}
static void attachment_writes(void) {
    reset();uint32_t id;input.attachments=(MailAttachments){.count=2,.ids={101,201}};
    assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_UNAVAILABLE && !id && !writes && !cleanup);
    assert(mail_core_draft_save(&alice,&input,&id)==MAIL_CORE_UNAVAILABLE && !writes);
    input.attachments.ids[1]=102;assert(mail_core_draft_save(&alice,&input,&id)==MAIL_CORE_OK && committed_attachments.count==2);
    input.revision=id;assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_OK && committed_attachments.ids[1]==102);
    assert(mail_core_attachment_drop(&alice,102)==MAIL_CORE_UNAVAILABLE); /* referenced file */
    assert(mail_core_attachment_drop(&bob,101)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_attachment_drop(&alice,101)==MAIL_CORE_OK);
    puts("PASS save/send revalidate attachments and drop preserves store reference/ownership rules");
}
static void failures(void) {
    for(int mode=1;mode<=2;mode++) {
        reset();failure=mode;uint32_t id=7;MailOutcome expected=mode==1?MAIL_CORE_UNAVAILABLE:MAIL_CORE_STORAGE;
        assert(mail_core_draft_save(&alice,&input,&id)==expected && !id && !cleanup);
        mail_error=0;id=7;assert(mail_core_send(&alice,&input,&id)==expected && !id && !cleanup);
        mail_error=0;assert(mail_core_draft_delete(&alice,7)==expected && !cleanup);
        mail_error=0;assert(mail_core_delete(&alice,10)==MAIL_CORE_STORAGE && !cleanup);
    }
    puts("PASS failed persistence cannot publish revisions/delivery or trigger attachment reclamation");
}
static void delete_private(void) {
    reset();assert(mail_core_delete(&admin,10)==MAIL_CORE_UNAVAILABLE && !writes && !cleanup);
    assert(mail_core_delete(&alice,10)==MAIL_CORE_OK && cleanup==1 && letters[0].deleted);
    assert(mail_core_delete(&bob,10)==MAIL_CORE_UNAVAILABLE && cleanup==1);
    assert(mail_core_delete(&alice,11)==MAIL_CORE_OK && cleanup==2);
    puts("PASS sender/recipient deletion only, no admin privilege or repeat cleanup");
}
static void boundaries(void) {
    reset();uint32_t id;MailCompose out;char title[82];memset(title,'x',81);title[81]=0;
    assert(mail_core_prepare(&alice,"bob",0,title,&out)==MAIL_CORE_UNAVAILABLE);
    title[80]=0;assert(mail_core_prepare(&alice,"bob",0,title,&out)==MAIL_CORE_OK);
    input.title=title;assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_OK);
    input.title=NULL;assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_UNAVAILABLE && !id);
    input.title="subject";input.body=NULL;assert(mail_core_draft_save(&alice,&input,&id)==MAIL_CORE_UNAVAILABLE);
    input.body="body\r\n";input.attachments.count=17;assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_send(&alice,NULL,&id)==MAIL_CORE_UNAVAILABLE && !id);
    assert(mail_core_send(&alice,&input,NULL)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_prepare(&alice,"bob",0,"title",NULL)==MAIL_CORE_UNAVAILABLE);
    puts("PASS typed input/null/title/attachment bounds");
}
static void body_limits(void) {
    reset();char *large=malloc(CONTENT_BODY+2);assert(large);memset(large,'x',CONTENT_BODY+1);large[CONTENT_BODY+1]=0;
    uint32_t id;input.body=large;
    assert(mail_core_send(&alice,&input,&id)==MAIL_CORE_BODY_LIMIT && !id && !writes);
    large[CONTENT_BODY]=0;assert(mail_core_draft_save(&alice,&input,&id)==MAIL_CORE_OK);
    free(large);puts("PASS maximum draft body and oversized body rejected before persistence");
}
int main(void) {
    authority();health();prepare();draft_views();draft_failures();lifecycle();attachment_selection();
    attachment_writes();failures();delete_private();boundaries();body_limits();return 0;
}

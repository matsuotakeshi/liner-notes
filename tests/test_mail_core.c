/* Direct Core API tests: no UI authorization guard, live DB or socket. */
#include "mail_core.h"
#include "mail_repository.h"
#include "mailbox.h"
#include "mail_files.h"
#include "members.h"
#include "moderation.h"
#include "content_limits.h"
#include "store_limits.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const MailActor alice={1,9},bob={2,9},admin={3,9};
static const char *names[]={NULL,"alice","bob","admin","pending","retired","suspended"};
static int member_error,moderation_error,mail_error,enabled,blocked[7],failure,body_failure;
static unsigned reads,writes,cleanup,ownership_checks,drop_calls,body_reads;
static uint32_t last_user,next_id,next_revision;
static MailMessage drafts[7],letters[32],cache;
static char saved_body[CONTENT_BODY+1];
static char letter_bodies[32][128];
static unsigned marks,search_calls;
static uint32_t body_fail_id,get_fail_id,total_override;
static int search_backwards;
static MailAttachments committed_attachments;
static MailWrite input;
static void reset(void) {
    member_error=moderation_error=mail_error=failure=body_failure=0;enabled=1;
    reads=writes=cleanup=ownership_checks=drop_calls=body_reads=0;last_user=0;
    memset(blocked,0,sizeof(blocked));blocked[4]=blocked[5]=blocked[6]=1;
    marks=search_calls=body_fail_id=get_fail_id=total_override=0;search_backwards=0;memset(letter_bodies,0,sizeof(letter_bodies));
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
    if(mail_error || (get_fail_id && get_fail_id==id)) { mail_error=1;return NULL; }
    for(unsigned i=0;i<32;i++)if(letters[i].id==id && !letters[i].deleted &&
        (letters[i].sender==user || letters[i].recipient==user)) { cache=letters[i];return &cache; }
    return NULL;
}
const MailMessage *mailbox_draft(uint32_t user) {
    reads++;last_user=user;if(user>=7 || !drafts[user].id)return NULL;
    cache=drafts[user];return &cache;
}
uint32_t mailbox_count(void) {
    if(total_override)return total_override;
    uint32_t total=0;for(unsigned i=0;i<32;i++)if(letters[i].id>total)total=letters[i].id;return total;
}
unsigned mailbox_unread(uint32_t user) {
    unsigned n=0;for(unsigned i=0;i<32;i++)if(letters[i].id && !letters[i].deleted && letters[i].recipient==user && !letters[i].read)n++;
    return n;
}
uint32_t mailbox_search_next(uint32_t start,const char *query) {
    (void)query;search_calls++;return search_backwards?start-1:start<=mailbox_count()?start:0;
}
int mailbox_mark(uint32_t id,uint32_t user) {
    marks++;if(failure) { if(failure==2)mail_error=1;return 0; }
    for(unsigned i=0;i<32;i++)if(letters[i].id==id && !letters[i].deleted && (letters[i].recipient==user || letters[i].sender==user)) {
        if(letters[i].recipient==user)letters[i].read=1;
        memset(&cache,0,sizeof(cache));return 1;
    }
    return 0;
}
const char *mailbox_body(const MailMessage *message) {
    body_reads++;
    if(body_failure || (body_fail_id && message->id==body_fail_id))mail_error=1;
    for(unsigned i=0;i<32;i++)if(letters[i].id==message->id) {
        memset(&cache,0,sizeof(cache));return letter_bodies[i];
    }
    assert(message->id==drafts[message->sender].id);return saved_body;
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

static void read_fixture(void) {
    reset();memset(letters,0,sizeof(letters));
    for(unsigned i=0;i<9;i++) {
        letters[i]=(MailMessage){.id=2*(i+1),.sender=2,.recipient=1,.time=100+i,.body_size=8};
        snprintf(letters[i].title,sizeof(letters[i].title),"subject%u",i);
        strcpy(letter_bodies[i],"Needle\r\n");
    }
    letters[9]=(MailMessage){.id=3,.sender=2,.recipient=3,.title="private-secret",.time=104};
    strcpy(letter_bodies[9],"private-secret Needle\r\n");
    letters[10]=(MailMessage){.id=19,.sender=1,.recipient=2,.title="sent",.body_size=8};
    strcpy(letter_bodies[10],"outbox\r\n");
    letters[11]=(MailMessage){.id=20,.sender=0,.recipient=1,.title="guest",.time=109};
}
static void list_pages(void) {
    read_fixture();MailQuery q={.mode=MAIL_LIST_ALL};MailPage page;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==8 && page.next==17 && page.unread==10);
    assert(page.rows[0].id==2 && page.rows[7].id==16 && !strcmp(page.rows[0].from,"bob"));
    strcpy(cache.title,"changed");assert(!strcmp(page.rows[0].title,"subject0"));
    q.cursor=page.next;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==3 && !page.next);
    assert(page.rows[0].id==18 && page.rows[1].id==19 && page.rows[2].id==20 && !strcmp(page.rows[2].from,"guest"));
    q.cursor=CONTENT_ID_MAX;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && !page.count && !page.next);
    q.cursor=0;assert(mail_core_list(&admin,&q,&page)==MAIL_CORE_OK && page.count==1 && page.rows[0].id==3);
    assert(!marks && !body_reads);puts("PASS mail list paging, ID holes, owned names, guest senders and no admin override");
}
static void list_filters(void) {
    read_fixture();MailQuery q={.mode=MAIL_LIST_SENT};MailPage page;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==1 && page.rows[0].id==19);
    letters[0].read=1;letters[1].deleted=1;q.mode=MAIL_LIST_UNREAD;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==8 && page.unread==8 && !page.next && page.rows[0].id==6);
    q.mode=MAIL_LIST_INBOX;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==8 && page.next==19 && page.rows[0].id==2);
    q.mode=MAIL_LIST_NEWS;q.since=102;q.until=104;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==3 && page.rows[0].id==6 && page.rows[2].id==10);
    q.since=105;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && !page.count);
    q.until=0;q.since=109;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==1 && page.rows[0].id==20);
    assert(!marks && !body_reads);puts("PASS sent/inbox/unread/news filters and inclusive timestamp bounds without marking");
}
static void search_privacy(void) {
    read_fixture();MailPage page;MailQuery q={.mode=MAIL_LIST_SEARCH,.query="Needle"};
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==8 && page.next==17 && body_reads==9);
    q.cursor=page.next;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==1 && page.rows[0].id==18);
    q.cursor=0;q.query="private-secret";body_reads=0;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && !page.count && body_reads==11);
    q.query="needle";assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && !page.count);
    q.query="subject4";assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_OK && page.count==1 && page.rows[0].id==10);
    assert(!marks);puts("PASS indexed candidate filtering: private bodies never loaded, exact case and title/body search");
}
static void reads_and_sequences(void) {
    read_fixture();MailRead out;char body[128];
    letters[0].attachments=(MailAttachments){.count=2,.ids={101,102}};
    assert(mail_core_read(&admin,MAIL_READ_ID,2,0,&out,body,sizeof(body))==MAIL_CORE_UNAVAILABLE && !marks && zero(body,sizeof(body)));
    assert(mail_core_read(&alice,MAIL_READ_ID,19,0,&out,body,sizeof(body))==MAIL_CORE_OK && !out.message.read);
    assert(mail_core_read(&alice,MAIL_READ_UNREAD,0,0,&out,body,sizeof(body))==MAIL_CORE_OK && out.message.id==2 && out.message.read && !strcmp(body,"Needle\r\n"));
    assert(out.attachments.count==2 && out.attachments.ids[0]==101 && out.attachments.ids[1]==102);
    assert(mail_core_read(&alice,MAIL_READ_UNREAD,0,0,&out,body,sizeof(body))==MAIL_CORE_OK && out.message.id==4);
    assert(mail_core_read(&alice,MAIL_READ_LATEST,0,0,&out,body,sizeof(body))==MAIL_CORE_OK && out.message.id==20 && !strcmp(out.message.from,"guest"));
    assert(mail_core_read(&alice,MAIL_READ_SEQUENCE,3,0,&out,body,sizeof(body))==MAIL_CORE_OK && out.message.id==4 && out.sequence && out.next==5);
    assert(mail_core_read(&alice,MAIL_READ_SEQUENCE,3,1,&out,body,sizeof(body))==MAIL_CORE_OK && out.message.id==2 && out.next==1);
    assert(mail_core_read(&alice,MAIL_READ_SEQUENCE,UINT32_MAX,1,&out,body,sizeof(body))==MAIL_CORE_OK && out.message.id==20 && out.next==19);
    unsigned before=marks;
    assert(mail_core_read(&alice,MAIL_READ_SEQUENCE,UINT32_MAX,0,&out,body,sizeof(body))==MAIL_CORE_OK && out.sequence && !out.message.id);
    assert(mail_core_read(&alice,MAIL_READ_SEQUENCE,0,1,&out,body,sizeof(body))==MAIL_CORE_OK && out.sequence && !out.message.id && !out.next);
    assert(marks==before);puts("PASS read marking, next unread/latest and ascending/descending participant-only navigation");
}
static void reading_failures(void) {
    read_fixture();MailPage page;MailQuery q={.mode=MAIL_LIST_SEARCH,.query="Needle"};MailRead out;char body[128];
    body_fail_id=6;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_BODY_STORAGE && zero(&page,sizeof(page)) && !marks);
    read_fixture();body_fail_id=2;
    assert(mail_core_read(&alice,MAIL_READ_ID,2,0,&out,body,sizeof(body))==MAIL_CORE_BODY_STORAGE && zero(&out,sizeof(out)) && zero(body,sizeof(body)));
    assert(letters[0].read); /* existing durable mark-before-body ordering */
    read_fixture();failure=1;
    assert(mail_core_read(&alice,MAIL_READ_ID,2,0,&out,body,sizeof(body))==MAIL_CORE_STORAGE && !body_reads && zero(&out,sizeof(out)));
    read_fixture();q.mode=MAIL_LIST_ALL;get_fail_id=6;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_STORAGE && zero(&page,sizeof(page)));
    read_fixture();q.mode=MAIL_LIST_SEARCH;q.cursor=3;search_backwards=1;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_STORAGE && zero(&page,sizeof(page)));
    read_fixture();total_override=UINT32_MAX;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_STORAGE);
    assert(mail_core_read(&alice,MAIL_READ_LATEST,0,0,&out,body,sizeof(body))==MAIL_CORE_STORAGE && zero(&out,sizeof(out)));
    puts("PASS partial search/read/mark failures and invalid index bounds expose no partial output");
}
static void reading_authority_bounds(void) {
    read_fixture();MailPage page;MailRead out;char body[128];MailQuery q={.mode=MAIL_LIST_ALL};
    MailActor denied[]={{0,0},{1,8},{4,9},{5,9},{6,9},{99,9}};
    for(unsigned i=0;i<sizeof(denied)/sizeof(*denied);i++) {
        MailOutcome expected=i?MAIL_CORE_REVOKED:MAIL_CORE_LOGIN_REQUIRED;
        assert(mail_core_list(&denied[i],&q,&page)==expected && zero(&page,sizeof(page)));
        assert(mail_core_read(&denied[i],MAIL_READ_ID,2,0,&out,body,sizeof(body))==expected && zero(&out,sizeof(out)) && zero(body,sizeof(body)));
    }
    assert(!reads && !marks);enabled=0;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_DISABLED);
    assert(mail_core_read(&alice,MAIL_READ_ID,2,0,&out,body,sizeof(body))==MAIL_CORE_DISABLED);enabled=1;
    q.mode=MAIL_LIST_SEARCH;q.query="";assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_UNAVAILABLE);
    q.query="bad\nquery";assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_UNAVAILABLE);
    char long_query[82];memset(long_query,'x',81);long_query[81]=0;q.query=long_query;
    assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_UNAVAILABLE);
    q.mode=MAIL_LIST_ALL;q.cursor=UINT32_MAX;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_UNAVAILABLE);
    q.cursor=0;q.mode=(MailListMode)99;assert(mail_core_list(&alice,&q,&page)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_read(&alice,(MailReadMode)99,2,0,&out,body,sizeof(body))==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_read(&alice,MAIL_READ_ID,2,2,&out,body,sizeof(body))==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_read(&alice,MAIL_READ_ID,2,0,&out,body,8)==MAIL_CORE_UNAVAILABLE && !marks);
    assert(mail_core_read(&alice,MAIL_READ_ID,2,0,&out,NULL,0)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_list(&alice,NULL,&page)==MAIL_CORE_UNAVAILABLE);
    letters[0].attachments.count=MAIL_ATTACH_MAX+1;
    assert(mail_core_read(&alice,MAIL_READ_ID,2,0,&out,body,sizeof(body))==MAIL_CORE_STORAGE && zero(&out,sizeof(out)));
    assert(!marks);puts("PASS read/list actor and feature gates, query/cursor/mode/body buffer bounds");
}
int main(void) {
    authority();health();prepare();draft_views();draft_failures();lifecycle();attachment_selection();
    attachment_writes();failures();delete_private();boundaries();body_limits();
    list_pages();list_filters();search_privacy();reads_and_sequences();reading_failures();reading_authority_bounds();return 0;
}

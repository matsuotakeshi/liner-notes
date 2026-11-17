/* Direct Core API tests: no UI authorization guard, live DB or socket. */
#include "mail_core.h"
#include "mail_repository.h"
#include "mailbox.h"
#include "mail_files.h"
#include "members.h"
#include "moderation.h"
#include "content_limits.h"
#include "app_limits.h"
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
static unsigned file_reads,buffer_writes;
static int file_failure,buffer_failure;
static MailFilePage file_page;
static char common_buffer[CONTENT_BODY+1];
static const char *body_override;
static int admins[7],notice_failure;
static uint32_t member_total,last_recipient,last_parent,last_revision;
static unsigned notice_reads,transfer_calls;
static VaultResult transfer_status;
static int oversize_reply;
static TransferOperation last_operation;
static void reset(void) {
    member_error=moderation_error=mail_error=failure=body_failure=0;enabled=1;
    memset(admins,0,sizeof(admins));admins[3]=1;member_total=6;
    notice_failure=0;notice_reads=transfer_calls=0;transfer_status=VAULT_OK;oversize_reply=0;
    last_recipient=last_parent=last_revision=0;
    file_reads=buffer_writes=0;file_failure=buffer_failure=0;body_override=NULL;
    memset(&file_page,0,sizeof(file_page));strcpy(common_buffer,"old buffer");
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
uint32_t members_count(void) { return member_total; }
int moderation_admin(uint32_t id) { return id<7?admins[id]:0; }
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
    reads++;last_user=user;if(notice_failure==2) { mail_error=1;return NULL; }
    if(user>=7 || !drafts[user].id)return NULL;
    cache=drafts[user];return &cache;
}
uint32_t mailbox_count(void) {
    if(total_override)return total_override;
    uint32_t total=0;for(unsigned i=0;i<32;i++)if(letters[i].id>total)total=letters[i].id;return total;
}
unsigned mailbox_unread(uint32_t user) {
    notice_reads++;if(notice_failure==1)mail_error=1;
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
    if(body_override)return body_override;
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
    writes++;last_user=user;last_recipient=to;last_parent=parent;last_revision=revision;*id=999;
    if(write_failed() || !*title || !*body || (revision && drafts[user].id!=revision) || !reply_valid(user,parent,to))return 0;
    committed_attachments=attachments?*attachments:(MailAttachments){0};if(revision)drafts[user].id=0;*id=++next_id;return 1;
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
int mail_files_info(uint32_t user,uint32_t id,MailFileInfo *out) {
    file_reads++;last_user=user;memset(out,0xa5,sizeof(*out));
    if(file_failure || id!=101 || (user!=1 && user!=2))return 0;
    *out=(MailFileInfo){.id=id,.size=1024,.name="private.bin"};return 1;
}
int mail_files_page(uint32_t user,uint32_t cursor,MailFilePage *out) {
    file_reads++;last_user=user;memset(out,0xa5,sizeof(*out));
    if(file_failure || cursor>0x7ffffffeu)return 0;
    *out=file_page;return 1;
}
int mail_repository_buffer_save(uint32_t user,const char *body) {
    buffer_writes++;last_user=user;
    if(buffer_failure)return 0;
    strcpy(common_buffer,body);return 1;
}
VaultResult mail_files_execute(uint32_t user,const TransferRequest *q,TransferReply *out) {
    transfer_calls++;last_user=user;last_operation=q->operation;
    memset(out,0xa5,sizeof(*out));
    if(transfer_status!=VAULT_OK)return transfer_status;
    memset(out,0,sizeof(*out));out->id=101;
    if(q->operation==TRANSFER_READ) { out->count=oversize_reply?VAULT_BLOCK+1:4;memcpy(out->bytes,"data",4); }
    if(q->operation==TRANSFER_WRITE) { assert(q->bytes[0]==42);out->received=q->offset+(uint32_t)q->count; }
    return VAULT_OK;
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
static void attachment_views(void) {
    reset();MailFileInfo info;MailFilePage page;
    file_page.count=2;file_page.next=8;
    file_page.rows[0]=(MailFileInfo){.id=4,.size=4096,.active=1,.name="-"};
    file_page.rows[1]=(MailFileInfo){.id=7,.size=1024,.name="private.bin"};
    assert(mail_core_files(&alice,0,&page)==MAIL_CORE_OK && last_user==1);
    assert(page.count==2 && page.next==8 && page.rows[0].active && !page.rows[1].active);
    memset(&file_page,0,sizeof(file_page));assert(!strcmp(page.rows[1].name,"private.bin"));
    assert(mail_core_attachment_info(&bob,101,&info)==MAIL_CORE_OK && last_user==2 && info.size==1024);
    assert(mail_core_attachment_info(&admin,101,&info)==MAIL_CORE_UNAVAILABLE && zero(&info,sizeof(info)));
    assert(mail_core_attachment_info(&alice,0,&info)==MAIL_CORE_UNAVAILABLE && zero(&info,sizeof(info)));
    assert(mail_core_files(&alice,UINT32_MAX,&page)==MAIL_CORE_STORAGE && zero(&page,sizeof(page)));
    file_failure=1;
    assert(mail_core_files(&alice,0,&page)==MAIL_CORE_STORAGE && zero(&page,sizeof(page)));
    assert(mail_core_attachment_info(&alice,101,&info)==MAIL_CORE_UNAVAILABLE && zero(&info,sizeof(info)));
    file_failure=0;file_page.count=9;
    assert(mail_core_files(&alice,0,&page)==MAIL_CORE_STORAGE && zero(&page,sizeof(page)));
    file_page.count=1;memset(file_page.rows[0].name,'x',sizeof(file_page.rows[0].name));
    assert(mail_core_files(&alice,0,&page)==MAIL_CORE_STORAGE && zero(&page,sizeof(page)));
    assert(!marks && !writes && !body_reads);
    puts("PASS owned attachment views, recipient info, admin denial and partial failure clearing");
}
static void attachment_batches(void) {
    reset();MailAttachments out;
    letters[0].attachments.count=MAIL_ATTACH_MAX;
    for(unsigned i=0;i<MAIL_ATTACH_MAX;i++)letters[0].attachments.ids[i]=200-i;
    assert(mail_core_attachment_batch(&alice,10,&out)==MAIL_CORE_OK);
    assert(out.count==MAIL_ATTACH_MAX && out.ids[0]==200 && out.ids[15]==185);
    assert(mail_core_attachment_batch(&bob,10,&out)==MAIL_CORE_OK);
    assert(mail_core_attachment_batch(&admin,10,&out)==MAIL_CORE_UNAVAILABLE && zero(&out,sizeof(out)));
    assert(mail_core_attachment_batch(&alice,11,&out)==MAIL_CORE_UNAVAILABLE && zero(&out,sizeof(out)));
    letters[0].attachments.count=MAIL_ATTACH_MAX+1;
    assert(mail_core_attachment_batch(&alice,10,&out)==MAIL_CORE_STORAGE && zero(&out,sizeof(out)));
    letters[0].deleted=1;
    assert(mail_core_attachment_batch(&alice,10,&out)==MAIL_CORE_UNAVAILABLE && zero(&out,sizeof(out)));
    assert(!marks && !writes && !body_reads && !file_reads);
    puts("PASS attachment batches preserve 16 IDs/order and participant ACL without read marking");
}
static void buffer_imports(void) {
    reset();strcpy(letter_bodies[0],"private body\r\nsecond line\r\n");seed();
    assert(mail_core_import(&alice,10)==MAIL_CORE_OK && last_user==1 && buffer_writes==1);
    assert(!strcmp(common_buffer,letter_bodies[0]) && drafts[1].id==7);
    assert(mail_core_import(&bob,10)==MAIL_CORE_OK && last_user==2);
    unsigned before=body_reads;
    assert(mail_core_import(&admin,10)==MAIL_CORE_UNAVAILABLE && body_reads==before && buffer_writes==2);
    strcpy(common_buffer,"keep on failure");buffer_failure=1;
    assert(mail_core_import(&alice,10)==MAIL_CORE_BUFFER_STORAGE && !strcmp(common_buffer,"keep on failure"));
    buffer_failure=0;body_failure=1;before=buffer_writes;
    assert(mail_core_import(&alice,10)==MAIL_CORE_BODY_STORAGE && buffer_writes==before && !strcmp(common_buffer,"keep on failure"));
    assert(!marks && !writes && !cleanup);
    reset();memset(saved_body,'x',CONTENT_BODY);saved_body[CONTENT_BODY]=0;body_override=saved_body;
    assert(mail_core_import(&alice,10)==MAIL_CORE_OK && strlen(common_buffer)==CONTENT_BODY);
    assert(!marks && !writes);
    puts("PASS common buffer imports preserve unread/drafts, participant ACL and failure ordering");
}
static void attachment_authority(void) {
    MailActor denied[]={{0,0},{1,8},{4,9},{5,9},{6,9},{99,9}};
    for(unsigned i=0;i<sizeof(denied)/sizeof(*denied)+4;i++) {
        reset();const MailActor *actor=&alice;MailOutcome expected;
        if(i<6) { actor=&denied[i];expected=i?MAIL_CORE_REVOKED:MAIL_CORE_LOGIN_REQUIRED; }
        else if(i==6) { member_error=1;expected=MAIL_CORE_MEMBER_STORAGE; }
        else if(i==7) { moderation_error=1;expected=MAIL_CORE_MODERATION_STORAGE; }
        else if(i==8) { enabled=0;expected=MAIL_CORE_DISABLED; }
        else { mail_error=1;expected=MAIL_CORE_STORAGE; }
        MailFilePage page;MailFileInfo info;MailAttachments out;
        assert(mail_core_files(actor,0,&page)==expected && zero(&page,sizeof(page)));
        assert(mail_core_attachment_info(actor,101,&info)==expected && zero(&info,sizeof(info)));
        assert(mail_core_attachment_batch(actor,10,&out)==expected && zero(&out,sizeof(out)));
        assert(mail_core_import(actor,10)==expected);
        assert(!reads && !file_reads && !body_reads && !buffer_writes);
    }
    reset();
    assert(mail_core_files(&alice,0,NULL)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_attachment_info(&alice,101,NULL)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_attachment_batch(&alice,10,NULL)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_import(NULL,10)==MAIL_CORE_LOGIN_REQUIRED && !reads);
    puts("PASS attachment/import identity, epoch, store and feature gates before any private I/O");
}
static void sysop_delivery(void) {
    reset();MailDelivery out;MailActor guest={0,0};seed();
    admins[2]=1;admins[4]=1;
    assert(mail_core_sysop_send(&guest,"visitor","guest body",&out)==MAIL_CORE_OK);
    assert(out.id==21 && out.recipient==2 && last_user==0 && last_parent==0 && last_revision==0 && !committed_attachments.count);
    blocked[2]=1;
    assert(mail_core_sysop_send(&alice,"member","member body",&out)==MAIL_CORE_OK);
    assert(out.id==22 && out.recipient==3 && last_user==1 && drafts[1].id==7);
    assert(!cleanup && !marks && !ownership_checks);
    blocked[3]=1;
    assert(mail_core_sysop_send(&guest,"visitor","body",&out)==MAIL_CORE_SYSOP_UNAVAILABLE && zero(&out,sizeof(out)) && writes==2);
    puts("PASS SYSOP guest/member routing chooses lowest eligible admin and preserves drafts");
}
static void sysop_failures(void) {
    reset();MailDelivery out;MailActor guest={0,0};
    for(int fail=1;fail<=2;fail++) {
        reset();failure=fail;
        assert(mail_core_sysop_send(&guest,"visitor","body",&out)==(fail==2?MAIL_CORE_STORAGE:MAIL_CORE_UNAVAILABLE));
        assert(zero(&out,sizeof(out)) && !cleanup);
    }
    reset();member_total=MEMBER_MAX+1u;
    assert(mail_core_sysop_send(&guest,"visitor","body",&out)==MAIL_CORE_MEMBER_STORAGE && !writes);
    reset();char title[82];memset(title,'x',81);title[81]=0;
    assert(mail_core_sysop_send(&guest,title,"body",&out)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_sysop_send(&guest,"","body",&out)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_sysop_send(&guest,"title",NULL,&out)==MAIL_CORE_UNAVAILABLE);
    assert(mail_core_sysop_send(&guest,"title","",&out)==MAIL_CORE_UNAVAILABLE && !writes);
    char *large=malloc(CONTENT_BODY+2);assert(large);memset(large,'x',CONTENT_BODY+1);large[CONTENT_BODY+1]=0;
    assert(mail_core_sysop_send(&guest,"title",large,&out)==MAIL_CORE_BODY_LIMIT && !writes && zero(&out,sizeof(out)));
    free(large);
    puts("PASS SYSOP commit failures, missing recipient bounds and quota never publish delivery");
}
static void login_notices(void) {
    reset();seed();MailNotice notice;
    assert(mail_core_notice(&alice,&notice)==MAIL_CORE_OK && notice.unread==1 && notice.draft_revision==7);
    assert(!marks && !writes && !body_reads && !file_reads);
    assert(mail_core_notice(&admin,&notice)==MAIL_CORE_OK && !notice.unread && !notice.draft_revision);
    for(int fail=1;fail<=2;fail++) {
        reset();seed();notice_failure=fail;
        assert(mail_core_notice(&alice,&notice)==MAIL_CORE_STORAGE && zero(&notice,sizeof(notice)));
    }
    reset();seed();enabled=0;
    assert(mail_core_notice(&alice,&notice)==MAIL_CORE_DISABLED && zero(&notice,sizeof(notice)) && !reads && !notice_reads);
    puts("PASS login notices are owner-only metadata, no body/mark, and clear partial failures");
}
static void transfer_operations(void) {
    reset();TransferRequest q={0};TransferReply out;VaultResult status;
    for(int op=TRANSFER_INFO;op<=TRANSFER_CHECK;op++) {
        q=(TransferRequest){.operation=(TransferOperation)op,.id=101,.offset=3,.size=4,.count=4,.name="file.bin",.description="private"};
        q.bytes[0]=42;
        assert(mail_core_transfer(&alice,&q,&out,&status)==MAIL_CORE_OK && status==VAULT_OK);
        assert(last_user==1 && last_operation==(TransferOperation)op);
        if(op==TRANSFER_READ)assert(out.count==4 && !memcmp(out.bytes,"data",4));
        if(op==TRANSFER_WRITE)assert(out.received==7);
    }
    assert(transfer_calls==8 && !writes && !marks);
    for(int r=VAULT_DENIED;r<=VAULT_LIMIT;r++) {
        transfer_status=(VaultResult)r;
        assert(mail_core_transfer(&alice,&q,&out,&status)==MAIL_CORE_OK && status==(VaultResult)r && zero(&out,sizeof(out)));
    }
    transfer_status=VAULT_OK;q.operation=TRANSFER_READ;oversize_reply=1;
    assert(mail_core_transfer(&alice,&q,&out,&status)==MAIL_CORE_OK && status==VAULT_STORAGE && zero(&out,sizeof(out)));
    blocked[1]=1;unsigned before=transfer_calls;q.operation=TRANSFER_FINISH;
    assert(mail_core_transfer(&alice,&q,&out,&status)==MAIL_CORE_REVOKED && status==VAULT_DENIED && zero(&out,sizeof(out)) && transfer_calls==before);
    puts("PASS all private transfer operations, typed storage errors, partial payload clearing and revoke-before-finish");
}
static void transfer_bounds(void) {
    reset();TransferReply out;VaultResult status;
    TransferRequest bad[9]={
        {.operation=(TransferOperation)99}, {.operation=TRANSFER_READ},
        {.operation=TRANSFER_READ,.id=VAULT_ID_MAX+1u},
        {.operation=TRANSFER_READ,.id=1,.count=VAULT_BLOCK+1},
        {.operation=TRANSFER_WRITE,.id=1},
        {.operation=TRANSFER_WRITE,.id=1,.count=1,.offset=VAULT_MAX_BYTES+1u},
        {.operation=TRANSFER_CHECK,.category="public"},
        {.operation=TRANSFER_START,.name="file",.size=VAULT_MAX_BYTES+1u},
        {.operation=TRANSFER_START,.name="file",.category="public"}};
    for(unsigned i=0;i<9;i++)assert(mail_core_transfer(&alice,&bad[i],&out,&status)==MAIL_CORE_OK && status==VAULT_INVALID && zero(&out,sizeof(out)));
    TransferRequest q={.operation=TRANSFER_START};memset(q.name,'x',sizeof(q.name));
    assert(mail_core_transfer(&alice,&q,&out,&status)==MAIL_CORE_OK && status==VAULT_INVALID);
    strcpy(q.name,"file");memset(q.description,'x',sizeof(q.description));
    assert(mail_core_transfer(&alice,&q,&out,&status)==MAIL_CORE_OK && status==VAULT_INVALID);
    q.operation=TRANSFER_CHECK;memset(q.category,'x',sizeof(q.category));
    assert(mail_core_transfer(&alice,&q,&out,&status)==MAIL_CORE_OK && status==VAULT_INVALID);
    assert(mail_core_transfer(&alice,NULL,&out,&status)==MAIL_CORE_OK && status==VAULT_INVALID);
    assert(!transfer_calls);
    puts("PASS typed transfer bounds reject malformed buffers/IDs/categories before backend I/O");
}
static void delivery_authority(void) {
    MailActor denied[]={{1,8},{4,9},{5,9},{6,9},{99,9},{0,9}};
    for(unsigned i=0;i<10;i++) {
        reset();const MailActor *actor=&alice;MailOutcome expected=MAIL_CORE_REVOKED;
        if(i<6)actor=&denied[i];
        else if(i==6) { member_error=1;expected=MAIL_CORE_MEMBER_STORAGE; }
        else if(i==7) { moderation_error=1;expected=MAIL_CORE_MODERATION_STORAGE; }
        else if(i==8) { enabled=0;expected=MAIL_CORE_DISABLED; }
        else { mail_error=1;expected=MAIL_CORE_STORAGE; }
        MailDelivery delivery;MailNotice notice;TransferRequest q={.operation=TRANSFER_CHECK};TransferReply out;VaultResult status;
        assert(mail_core_sysop_send(actor,"title","body",&delivery)==expected && zero(&delivery,sizeof(delivery)));
        MailOutcome member_expected=i==5?MAIL_CORE_LOGIN_REQUIRED:expected;
        assert(mail_core_notice(actor,&notice)==member_expected && zero(&notice,sizeof(notice)));
        assert(mail_core_transfer(actor,&q,&out,&status)==member_expected && status==VAULT_DENIED && zero(&out,sizeof(out)));
        assert(!reads && !writes && !transfer_calls && !notice_reads);
    }
    reset();MailDelivery delivery;MailNotice notice;MailActor guest={0,0};
    assert(mail_core_sysop_send(NULL,"title","body",&delivery)==MAIL_CORE_LOGIN_REQUIRED);
    assert(mail_core_notice(&guest,&notice)==MAIL_CORE_LOGIN_REQUIRED);
    assert(mail_core_sysop_send(&guest,"title","body",NULL)==MAIL_CORE_UNAVAILABLE);
    assert(!writes);puts("PASS delivery, notice and transfer actor/epoch/health gates independent of UI");
}
int main(void) {
    authority();health();prepare();draft_views();draft_failures();lifecycle();attachment_selection();
    attachment_writes();failures();delete_private();boundaries();body_limits();
    list_pages();list_filters();search_privacy();reads_and_sequences();reading_failures();reading_authority_bounds();
    attachment_views();attachment_batches();buffer_imports();attachment_authority();
    sysop_delivery();sysop_failures();login_notices();transfer_operations();transfer_bounds();delivery_authority();return 0;
}

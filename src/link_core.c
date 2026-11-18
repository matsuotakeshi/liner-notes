#include "link_core.h"
#include "link_repository.h"
#include "members.h"
#include "moderation.h"
#include "catalog.h"
#include "board_policy.h"
#include "extras.h"
#include "content_limits.h"
#include "store_limits.h"
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>

static int text_valid(const char *text,size_t maximum,int empty) {
    size_t n=strlen(text);if(n>maximum || (!empty && !n))return 0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++)if(*p<32 || *p==127)return 0;
    return 1;
}
static int hex_id(const char *text) {
    if(!text || strlen(text)!=64)return 0;
    for(unsigned i=0;i<64;i++)if(!strchr("0123456789abcdef",text[i]))return 0;
    return 1;
}
int link_content_hash(const char *body,char out[65]) {
    unsigned char digest[32];unsigned size=0;
    if(!EVP_Digest(body,strlen(body),digest,&size,EVP_sha256(),NULL) || size!=32)return 0;
    for(unsigned i=0;i<32;i++)snprintf(out+i*2,3,"%02x",digest[i]);
    return 1;
}
static LinkOutcome storage(void) { link_repository_fail();return LINK_STORAGE; }
static LinkOutcome admit(const LinkActor *actor,uint32_t board) {
    LinkOutcome health=link_repository_health();if(health!=LINK_OK)return health;
    if(!actor)return LINK_DENIED;
    policy_context(actor->grants);
    if(actor->user && (moderation_suspended(actor->user) || actor->epoch!=moderation_epoch(actor->user)))return LINK_REVOKED;
    if(!link_repository_enabled(board,actor->user) || !members_name(actor->user) || !catalog_read(board,actor->user))return LINK_DENIED;
    return LINK_OK;
}
LinkOutcome link_core_scan(const LinkActor *actor,uint32_t board,LinkQuery query,uint32_t start,
                           LinkPage *out,char *body,size_t capacity) {
    if(body && capacity)OPENSSL_cleanse(body,capacity);
    if(!out)return LINK_ACTION;
    memset(out,0,sizeof(*out));LinkOutcome result=admit(actor,board);if(result!=LINK_OK)return result;
    if(query!=LINK_METADATA && query!=LINK_SNAPSHOT && query!=LINK_BATCH)return LINK_ACTION;
    uint32_t id=start?start:1;unsigned limit=query==LINK_BATCH?32:1;
    for(unsigned n=0;n<limit;n++) {
        LinkItem item={0};int found=link_repository_next(board,id,&item);
        if(found<0)goto failure;
        if(!found) { out->end=1;return LINK_OK; }
        if(item.state!=LINK_LIVE)item.title[0]=0;
        if(query==LINK_SNAPSHOT && item.state==LINK_LIVE &&
           (!body || !capacity || !link_repository_body(item.id,body,capacity,&out->body_size)))goto failure;
        out->items[out->count++]=item;id=item.id+1;
    }
    return LINK_OK;
failure:
    memset(out,0,sizeof(*out));if(body && capacity)OPENSSL_cleanse(body,capacity);return storage();
}
static LinkOutcome lookup(uint32_t board,const char *origin,const char *key,LinkReference *out) {
    if(!hex_id(origin) || !hex_id(key))return LINK_IDENTITY;
    int done=1;
    if(!link_repository_lookup(board,origin,key,out,&done) || !done) { memset(out,0,sizeof(*out));return storage(); }
    return LINK_OK;
}
LinkOutcome link_core_lookup(const LinkActor *actor,uint32_t board,const char *origin,const char *key,LinkReference *out) {
    if(!out)return LINK_ACTION;
    memset(out,0,sizeof(*out));LinkOutcome result=admit(actor,board);
    return result==LINK_OK?lookup(board,origin,key,out):result;
}
LinkOutcome link_core_apply(const LinkActor *actor,const LinkEvent *input,LinkApplied *out) {
    if(!out)return LINK_ACTION;
    memset(out,0,sizeof(*out));if(!input)return LINK_ACTION;
    LinkOutcome result=admit(actor,input->board);if(result!=LINK_OK)return result;
    LinkReference ref={0};result=lookup(input->board,input->origin,input->key,&ref);if(result!=LINK_OK)return result;
    if(!catalog_write(input->board,actor->user) || policy_menu(input->board) || link_repository_version()!=4)return LINK_POSTING_DENIED;
    if(!input->revision || input->revision>CONTENT_ID_MAX)return LINK_REVISION;
    if(ref.id && input->revision<=ref.revision) { out->reference=ref;return LINK_OK; }
    int checkpoint=input->kind=='C' || input->kind=='W';
    if((!checkpoint && input->revision!=ref.revision+1) || (!ref.id && input->kind!='P' && !checkpoint) || (ref.id && input->kind=='P'))return LINK_PREDECESSOR;
    if(!input->kind || !strchr("PTDCW",input->kind))return LINK_ACTION;
    if(ref.id && ((link_repository_removed(ref.id) && input->kind!='W') || !link_repository_on_board(ref.id,input->board)))return LINK_UNAVAILABLE;
    if(!input->title || !input->body || !input->author || strlen(input->author)>32 || strlen(input->body)>CONTENT_BODY)return LINK_ACTION;
    if((input->kind=='P' || input->kind=='T' || input->kind=='C') && !text_valid(input->title,80,0))return LINK_TITLE;
    if(!text_valid(input->author,32,1) || !text_valid(input->title,80,1) || !input->created || input->created>253402300799ULL)return LINK_ACTION;
    for(const unsigned char *p=(const unsigned char *)input->body;*p;p++)
        if((*p<32 && *p!='\r' && *p!='\n' && *p!='\t') || *p==127)return LINK_ACTION;
    LinkEvent event=*input;int create=!ref.id;
    if(create && event.kind=='W') { event.title="Withdrawn";event.body="[Article withdrawn at origin]\r\n"; }
    char alias[33],hash[65];if(!link_content_hash(event.body,hash))return storage();
    size_t author_size=strlen(event.author);
    if(author_size>22) { author_size=22;while(author_size && ((unsigned char)event.author[author_size]&0xc0)==0x80)author_size--; }
    snprintf(alias,sizeof(alias),"@%.8s:%.*s",event.origin,(int)author_size,event.author);
    if(!create && event.kind=='C') {
        int match=link_repository_matches(ref.id,&event,alias,hash);
        if(match<0)return storage();
        if(!match)return LINK_CONFLICT;
    }
    if(create) {
        if(event.parent && !link_repository_on_board(event.parent,event.board))return LINK_PARENT;
        result=link_repository_post(actor,&event,alias,1,&ref.id);if(result!=LINK_OK)return result;
    }
    if(!link_repository_intent(actor,&event,ref.id,alias,hash))return storage();
    if(create) {
        uint32_t committed=0;
        if(link_repository_post(actor,&event,alias,0,&committed)!=LINK_OK || committed!=ref.id)return storage();
    }
    if(!link_repository_recover())return storage();
    out->reference.id=ref.id;out->reference.revision=event.revision;out->created=create;return LINK_OK;
}

#define _POSIX_C_SOURCE 200809L
#include "board_policy.h"
#include "catalog.h"
#include "members.h"
#include "durable.h"
#include "store_limits.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <inttypes.h>
#include <stdlib.h>
typedef struct { int deleted,menu;uint32_t order,max_articles,keep,revision;uint64_t max_bytes;unsigned char salt[16],hash[32];char info[BOARD_INFO_MAX+1]; } Policy;
static Policy boards[CATALOG_MAX+1];
static Durable journal={-1,0,0};
static const uint32_t *unlocked;
static uint32_t info_board,info_actor,info_size;static char pending_info[BOARD_INFO_MAX+1];
static int parse_options(const char *text,Policy *p) {
    char kind[8],canonical[128];uint32_t order,articles,keep;uint64_t bytes;int n=0;
    if(sscanf(text,"%7s %u %u %" SCNu64 " %u%n",kind,&order,&articles,&bytes,&keep,&n)!=5 || text[n] || (strcmp(kind,"BOARD") && strcmp(kind,"MENU")) || articles>CONTENT_ID_MAX || bytes>1099511627776ULL || keep>100000)return 0;
    snprintf(canonical,sizeof(canonical),"%s %u %u %" PRIu64 " %u",kind,order,articles,bytes,keep);if(strcmp(text,canonical))return 0;
    p->menu=!strcmp(kind,"MENU");p->order=order;p->max_articles=articles;p->max_bytes=bytes;p->keep=keep;return 1;
}
static int hex(const char *s,unsigned char *out,size_t size) {
    for(size_t i=0;i<size;i++) { unsigned x=0;for(unsigned j=0;j<2;j++) { unsigned c=(unsigned char)s[i*2+j];if(c>='0' && c<='9')c-='0';else if(c>='a' && c<='f')c=c-'a'+10;else return 0;x=x*16+c; }out[i]=(unsigned char)x; }return 1;
}
static int information_valid(const char *s) {
    for(const unsigned char *p=(const unsigned char *)s;*p;p++)if((*p<32 && *p!='\r' && *p!='\n' && *p!='\t') || *p==127)return 0;
    return 1;
}
static int apply(void *unused,const DurableRow *r) {
    (void)unused;if(!catalog_known(r->target) || !members_name(r->actor))return 0;Policy *p=&boards[r->target];
    if(info_board) {
        if(r->target!=info_board || r->actor!=info_actor)return 0;
        size_t n=strlen(pending_info),add=strlen(r->text);
        if(r->kind=='J' && add && n+add<=info_size) { memcpy(pending_info+n,r->text,add+1);return 1; }
        if(r->kind!='E' || add || n!=info_size || !information_valid(pending_info))return 0;
        strcpy(p->info,pending_info);info_board=0;return 1;
    }
    if(r->kind=='I') {
        char canonical[20];unsigned length;int n=0;if(sscanf(r->text,"%u%n",&length,&n)!=1 || r->text[n] || length>BOARD_INFO_MAX)return 0;
        snprintf(canonical,sizeof(canonical),"%u",length);if(strcmp(canonical,r->text))return 0;
        info_board=r->target;info_actor=r->actor;info_size=length;pending_info[0]=0;return 1;
    }
    if(r->kind=='O')return parse_options(r->text,p);
    if(r->kind=='D') { if(*r->text || r->target==1 || p->deleted)return 0;p->deleted=1;return 1; }
    if(r->kind=='P') {
        if(!*r->text) { p->revision=0;OPENSSL_cleanse(p->salt,16);OPENSSL_cleanse(p->hash,32);return 1; }
        if(strlen(r->text)!=97 || r->text[32]!=' ' || !hex(r->text,p->salt,16) || !hex(r->text+33,p->hash,32))return 0;
        p->revision=r->sequence;return 1;
    }
    return 0;
}
int policy_open(const char *base) {
    memset(boards,0,sizeof(boards));for(unsigned i=1;i<=CATALOG_MAX;i++) { boards[i].order=i;boards[i].keep=1000; }
    unlocked=NULL;info_board=0;char path[4096];int n=snprintf(path,sizeof(path),"%s.boardpolicy",base);
    if(n<=0 || (size_t)n>=sizeof(path) || !dj_open(&journal,path,"LNBDP001",apply,NULL) || info_board) { policy_close();return 0; }return 1;
}
void policy_close(void) { dj_close(&journal);unlocked=NULL;memset(boards,0,sizeof(boards)); }
int policy_broken(void) { return journal.broken; }
void policy_context(const uint32_t *grants) { unlocked=grants; }
int policy_deleted(uint32_t b) { return b && b<=CATALOG_MAX && boards[b].deleted; }
int policy_menu(uint32_t b) { return b && b<=CATALOG_MAX && boards[b].menu; }
uint32_t policy_order(uint32_t b) { return boards[b].order; }
uint32_t policy_log_keep(uint32_t b) { return boards[b].keep; }
uint32_t policy_max_articles(uint32_t b) { return boards[b].max_articles; }
uint64_t policy_max_bytes(uint32_t b) { return boards[b].max_bytes; }
const char *policy_info(uint32_t b) { return boards[b].info; }
int policy_locked(uint32_t b,uint32_t user) {
    if(catalog_manage(b,user))return 0;
    for(uint32_t p=b;p;p=catalog_parent(p))if(boards[p].revision && (!unlocked || unlocked[p]!=boards[p].revision))return (int)p;
    return 0;
}
static int append(uint32_t board,uint32_t actor,char kind,const char *text) {
    DurableRow r={0};r.target=board;r.actor=actor;r.kind=kind;snprintf(r.text,sizeof(r.text),"%s",text);
    if(!dj_append(&journal,&r) || !apply(NULL,&r)) { journal.broken=1;return 0; }return 1;
}
int policy_options(uint32_t b,uint32_t actor,const char *text) { Policy p={0};if(!parse_options(text,&p))return -1;return append(b,actor,'O',text); }
int policy_password(uint32_t b,uint32_t actor,const char *password) {
    if(!*password)return append(b,actor,'P',"");
    size_t n=strlen(password);unsigned char salt[16],hash[32];char text[100];
    if(n<8 || n>128 || RAND_bytes(salt,16)!=1 || PKCS5_PBKDF2_HMAC(password,(int)n,salt,16,600000,EVP_sha256(),32,hash)!=1)return 0;
    for(unsigned i=0;i<16;i++)snprintf(text+i*2,3,"%02x",salt[i]);
    text[32]=' ';
    for(unsigned i=0;i<32;i++)snprintf(text+33+i*2,3,"%02x",hash[i]);
    int ok=append(b,actor,'P',text);OPENSSL_cleanse(hash,sizeof(hash));OPENSSL_cleanse(text,sizeof(text));return ok;
}
uint32_t policy_unlock(uint32_t b,const char *password) {
    if(!b || b>CATALOG_MAX || !boards[b].revision || strlen(password)>128)return 0;
    unsigned char hash[32];Policy *p=&boards[b];
    int ok=PKCS5_PBKDF2_HMAC(password,(int)strlen(password),p->salt,16,600000,EVP_sha256(),32,hash)==1 && !CRYPTO_memcmp(hash,p->hash,32);OPENSSL_cleanse(hash,sizeof(hash));return ok?p->revision:0;
}
int policy_information(uint32_t b,uint32_t actor,const char *body) {
    size_t n=strlen(body);if(n>BOARD_INFO_MAX || !information_valid(body))return -1;char size[20];snprintf(size,sizeof(size),"%zu",n);
    if(!append(b,actor,'I',size))return 0;
    for(size_t pos=0;pos<n;) { char chunk[DJ_TEXT+1];size_t len=n-pos;if(len>DJ_TEXT) { len=DJ_TEXT;while(len && ((unsigned char)body[pos+len]&0xc0)==0x80)len--;if(!len)len=DJ_TEXT; }memcpy(chunk,body+pos,len);chunk[len]=0;if(!append(b,actor,'J',chunk))return 0;pos+=len; }
    return append(b,actor,'E',"");
}
int policy_delete(uint32_t b,uint32_t actor) { return append(b,actor,'D',""); }
void policy_report(uint32_t b,char *out,size_t size) {
    Policy *p=&boards[b];snprintf(out,size,"BOARDOPTIONS %u %s ORDER %u ARTICLES %u BYTES %" PRIu64 " LOG %u PASSWORD %s\r\n",b,p->menu?"MENU":"BOARD",p->order,p->max_articles,p->max_bytes,p->keep,p->revision?"ON":"OFF");
}

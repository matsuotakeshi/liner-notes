#include "download_core.h"
#include "download_repository.h"
#include "download.h"
#include <openssl/rand.h>
#include <stdio.h>
#include <time.h>
#define TICKETS 1024
typedef struct { char token[33],subject[80],protocol[12];uint32_t file,size,received;uint64_t at;unsigned char hash[32]; } Ticket;
static Ticket tickets[TICKETS];
static int (*current_guard)(void *);static void *guard_context;
void downloads_guard(int (*guard)(void *),void *context) { current_guard=guard;guard_context=context; }
int downloads_open(const char *directory) { memset(tickets,0,sizeof(tickets));return download_repository_open(directory); }
void downloads_close(void) { download_repository_close();memset(tickets,0,sizeof(tickets)); }
static DownloadOutcome result(VaultResult r) {
    switch(r) {
    case VAULT_OK:return DOWNLOAD_OK;
    case VAULT_DENIED:return DOWNLOAD_DENIED;
    case VAULT_BUSY:return DOWNLOAD_BUSY;
    case VAULT_STORAGE:return DOWNLOAD_STORAGE;
    case VAULT_INVALID:return DOWNLOAD_INVALID;
    default:return DOWNLOAD_NOT_FOUND;
    }
}
static int principal_valid(const Principal *p) { return p && *p->subject && memchr(p->subject,0,sizeof(p->subject)); }
DownloadOutcome download_core_access(const Principal *p) {
    if(!download_repository_ready())return DOWNLOAD_STORAGE;
    return principal_valid(p) && (p->capabilities&CAP_VAULT_READ)?DOWNLOAD_OK:DOWNLOAD_DENIED;
}
static Ticket *find(const Principal *p,const char *token) {
    uint64_t now=(uint64_t)time(NULL);
    for(unsigned i=0;i<TICKETS;i++)if(tickets[i].file && now>=tickets[i].at && now-tickets[i].at>3600)memset(&tickets[i],0,sizeof(tickets[i]));
    for(unsigned i=0;i<TICKETS;i++)if(tickets[i].file && !strcmp(tickets[i].token,token) && !strcmp(tickets[i].subject,p->subject))return &tickets[i];
    return NULL;
}

DownloadOutcome download_core_open(Vault *v,const Principal *p,uint32_t id,DownloadProtocol protocol,DownloadOpened *out) {
    if(!out)return DOWNLOAD_INVALID;
    memset(out,0,sizeof(*out));
    DownloadOutcome status=download_core_access(p);if(status!=DOWNLOAD_OK)return status;
    const char *protocols[]={"LINE","ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};
    if(!id || id>VAULT_ID_MAX || (unsigned)protocol>=sizeof(protocols)/sizeof(*protocols))return DOWNLOAD_INVALID;
    VaultEntry e;status=result(vault_info(v,p,id,&e));if(status!=DOWNLOAD_OK)return status;
    (void)find(p,"");Ticket *t=NULL;unsigned owned=0;
    for(unsigned i=0;i<TICKETS;i++) { if(!tickets[i].file && !t)t=&tickets[i];if(tickets[i].file && !strcmp(tickets[i].subject,p->subject))owned++; }
    if(!t || owned>=16)return DOWNLOAD_BUSY;
    unsigned char random[16];if(RAND_bytes(random,sizeof(random))!=1)return DOWNLOAD_STORAGE;
    memset(t,0,sizeof(*t));for(unsigned i=0;i<16;i++)snprintf(t->token+i*2,3,"%02x",random[i]);
    strcpy(t->protocol,protocols[protocol]);strcpy(t->subject,p->subject);
    t->file=id;t->size=e.size;t->at=(uint64_t)time(NULL);memcpy(t->hash,e.sha256,32);
    strcpy(out->token,t->token);out->size=e.size;memcpy(out->sha256,e.sha256,32);return DOWNLOAD_OK;
}
static DownloadOutcome current(Vault *v,const Principal *p,const Ticket *t) {
    VaultEntry e;DownloadOutcome status=result(vault_info(v,p,t->file,&e));if(status!=DOWNLOAD_OK)return status;
    return e.size==t->size && !memcmp(e.sha256,t->hash,32)?DOWNLOAD_OK:DOWNLOAD_NOT_FOUND;
}
DownloadOutcome download_core_fetch(Vault *v,const Principal *p,const char *token,uint32_t offset,size_t capacity,DownloadBlock *out) {
    if(!out)return DOWNLOAD_INVALID;
    memset(out,0,sizeof(*out));
    DownloadOutcome status=download_core_access(p);if(status!=DOWNLOAD_OK)return status;
    if(!download_token_valid(token) || (capacity!=VAULT_CHUNK && capacity!=VAULT_BLOCK))return DOWNLOAD_INVALID;
    Ticket *t=find(p,token);if(!t)return DOWNLOAD_TICKET;
    status=current(v,p,t);if(status!=DOWNLOAD_OK)return status;
    if(offset>t->received)return DOWNLOAD_INVALID;
    size_t got=0;status=result(vault_read_block(v,p,t->file,offset,out->bytes,capacity,&got));
    if(status!=DOWNLOAD_OK) { memset(out,0,sizeof(*out));return status; }
    if(offset+got>t->received)t->received=offset+(uint32_t)got;
    out->offset=offset;out->size=got;return DOWNLOAD_OK;
}
DownloadOutcome download_core_finish(Vault *v,const Principal *p,const char *token,const DownloadDigest *digest,DownloadCompleted *out) {
    if(!out)return DOWNLOAD_INVALID;
    memset(out,0,sizeof(*out));
    DownloadOutcome status=download_core_access(p);if(status!=DOWNLOAD_OK)return status;
    if(!download_token_valid(token) || !digest)return DOWNLOAD_INVALID;
    Ticket *t=find(p,token);
    if(!t) {
        for(size_t i=download_repository_count();i>0;i--) {
            DownloadReceipt r;if(!download_repository_get(i-1,&r))return DOWNLOAD_STORAGE;
            if(!strcmp(r.token,token) && !strcmp(r.subject,p->subject)) {
                VaultEntry e;
                if(!digest->valid || vault_info(v,p,r.file,&e)!=VAULT_OK || memcmp(digest->bytes,e.sha256,32))break;
                out->file=r.file;return DOWNLOAD_OK;
            }
        }
        return DOWNLOAD_TICKET;
    }
    status=current(v,p,t);if(status!=DOWNLOAD_OK)return status;
    if(!digest->valid || memcmp(digest->bytes,t->hash,32) || t->received!=t->size)return DOWNLOAD_INVALID;
    if(current_guard && !current_guard(guard_context))return DOWNLOAD_DENIED;
    DownloadReceipt receipt={0};receipt.file=t->file;receipt.size=t->size;
    strcpy(receipt.subject,p->subject);strcpy(receipt.token,t->token);strcpy(receipt.protocol,t->protocol);
    if(!download_repository_commit(v,&receipt))return DOWNLOAD_STORAGE;
    out->file=t->file;memset(t,0,sizeof(*t));return DOWNLOAD_OK;
}
DownloadOutcome download_core_abort(const Principal *p,const char *token) {
    DownloadOutcome status=download_core_access(p);if(status!=DOWNLOAD_OK)return status;
    if(!download_token_valid(token))return DOWNLOAD_INVALID;
    Ticket *t=find(p,token);if(t)memset(t,0,sizeof(*t));return DOWNLOAD_OK;
}
DownloadOutcome download_core_history(const Principal *p,uint32_t file,uint32_t cursor,DownloadHistory *out) {
    if(!out)return DOWNLOAD_INVALID;
    memset(out,0,sizeof(*out));
    if(!download_repository_ready())return DOWNLOAD_STORAGE;
    if(file>VAULT_ID_MAX)return DOWNLOAD_INVALID;
    if(!principal_valid(p) || !(p->capabilities&CAP_VAULT_MANAGE))return DOWNLOAD_DENIED;
    size_t count=download_repository_count(),index=cursor?cursor-1:count;if(index>count)return DOWNLOAD_INVALID;
    DownloadHistory page={0};unsigned scanned=0;
    while(index && page.count<8 && scanned++<256) {
        DownloadReceipt r;if(!download_repository_get(--index,&r))return DOWNLOAD_STORAGE;
        if(file && r.file!=file)continue;
        DownloadHistoryRow *row=&page.rows[page.count++];row->sequence=index+1;row->file=r.file;
        row->size=r.size;row->at=r.at;strcpy(row->subject,r.subject);strcpy(row->protocol,r.protocol);
    }
    page.next=index?index+1:0;*out=page;return DOWNLOAD_OK;
}
DownloadOutcome download_core_stats(Vault *v,const Principal *p,uint32_t file,uint32_t cursor,DownloadStats *out) {
    if(!out)return DOWNLOAD_INVALID;
    memset(out,0,sizeof(*out));
    if(!download_repository_ready())return DOWNLOAD_STORAGE;
    if(file>VAULT_ID_MAX)return DOWNLOAD_INVALID;
    if(!principal_valid(p))return DOWNLOAD_DENIED;
    uint32_t first=file?file:cursor?cursor:1;if(first>VAULT_ID_MAX)return DOWNLOAD_INVALID;
    DownloadStats page={0};uint32_t i=first;
    for(;i<=vault_last_id(v) && (!file || i==file);i++) {
        VaultEntry e;VaultResult vr=vault_info(v,p,i,&e);
        if(vr!=VAULT_OK && vr!=VAULT_NOT_FOUND && vr!=VAULT_DENIED)return result(vr);
        if(vr!=VAULT_OK && (!(p->capabilities&CAP_VAULT_MANAGE) || !downloads_count(i)))continue;
        if(page.count==8)break;
        DownloadCount *row=&page.rows[page.count++];row->file=i;row->count=downloads_count(i);
    }
    page.next=!file && i<=vault_last_id(v)?i:0;*out=page;return DOWNLOAD_OK;
}

#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "download.h"
#include "durable.h"
#include "store_limits.h"
#include <stdlib.h>
#include <inttypes.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#define TICKETS 1024
typedef struct { char token[33],subject[80],protocol[12];uint32_t file,size,received;uint64_t at;unsigned char hash[32]; } Ticket;
typedef struct { char token[33],subject[80],protocol[12];uint32_t file,size;uint64_t at; } Receipt;
static Durable journal={-1,0,0};static char journal_path[4096];
static Ticket tickets[TICKETS];
static int (*current_guard)(void *);static void *guard_context;
void downloads_guard(int (*guard)(void *),void *context) { current_guard=guard;guard_context=context; }
static Receipt *receipts;static size_t count,capacity;
static uint64_t *totals;static size_t total_capacity;
static int reserve(void) {
    if(count<capacity)return 1;
    size_t next=capacity?capacity*2:128;if(next<capacity || next>SIZE_MAX/sizeof(*receipts))return 0;
    Receipt *p=realloc(receipts,next*sizeof(*p));if(!p)return 0;receipts=p;capacity=next;return 1;
}
static int token_valid(const char *p) { if(strlen(p)!=32)return 0;for(;*p;p++)if(!(*p>='0' && *p<='9') && !(*p>='a' && *p<='f'))return 0;return 1; }
static int apply(void *unused,const DurableRow *row) {
    (void)unused;Receipt r={0};int n=0;
    if(!row->target || row->target>VAULT_ID_MAX || !store_reserve((void **)&totals,&total_capacity,(size_t)row->target+1,sizeof(*totals)))return 0;
    if(row->kind=='S') {
        uint64_t total;char canonical[24];
        if(count || row->actor || !row->target || row->target>VAULT_ID_MAX || totals[row->target] ||
           sscanf(row->text,"%" SCNu64 "%n",&total,&n)!=1 || row->text[n] || !total)return 0;
        snprintf(canonical,sizeof(canonical),"%" PRIu64,total);
        if(strcmp(canonical,row->text))return 0;
        totals[row->target]=total;return 1;
    }
    if((row->kind!='D' && row->kind!='d') || row->actor || !row->target || row->target>VAULT_ID_MAX ||
       (row->kind=='D'?sscanf(row->text,"%79s %32s %u%n",r.subject,r.token,&r.size,&n)!=3:sscanf(row->text,"%79s %32s %u %11s%n",r.subject,r.token,&r.size,r.protocol,&n)!=4) || row->text[n] || !token_valid(r.token) || r.size>VAULT_MAX_BYTES || !reserve())return 0;
    if(row->kind=='D')strcpy(r.protocol,"UNKNOWN");
    if(strcmp(r.protocol,"UNKNOWN") && strcmp(r.protocol,"LINE") && strcmp(r.protocol,"ZMODEM") && strcmp(r.protocol,"XMODEM") && strcmp(r.protocol,"YMODEM") && strcmp(r.protocol,"YMODEM-G") && strcmp(r.protocol,"MLINK") && strcmp(r.protocol,"TEXT") && strcmp(r.protocol,"XMODEM-SUM") && strcmp(r.protocol,"XMODEM-1K"))return 0;
    r.file=row->target;r.at=row->time;if(totals[r.file]==UINT64_MAX)return 0;receipts[count++]=r;totals[r.file]++;return 1;
}
int downloads_open(const char *directory) {
    downloads_close();char path[4096];int n=snprintf(path,sizeof(path),"%s/downloads.dat",directory);
    if(n>0 && (size_t)n<sizeof(path))strcpy(journal_path,path);
    return n>0 && (size_t)n<sizeof(path) && dj_open(&journal,path,"LNDOWN01",apply,NULL);
}
void downloads_close(void) { dj_close(&journal);free(receipts);receipts=NULL;count=capacity=0;memset(tickets,0,sizeof(tickets));free(totals);totals=NULL;total_capacity=0; }
static Ticket *find(const Principal *p,const char *token) {
    uint64_t now=(uint64_t)time(NULL);
    for(unsigned i=0;i<TICKETS;i++)if(tickets[i].file && now>=tickets[i].at && now-tickets[i].at>3600)memset(&tickets[i],0,sizeof(tickets[i]));
    for(unsigned i=0;i<TICKETS;i++)if(tickets[i].file && !strcmp(tickets[i].token,token) && !strcmp(tickets[i].subject,p->subject))return &tickets[i];
    return NULL;
}
static int parse_hash(const char *text,unsigned char out[32]) {
    if(strlen(text)!=64)return 0;
    for(unsigned i=0;i<32;i++) { unsigned x=0;for(unsigned j=0;j<2;j++) { unsigned c=(unsigned char)text[i*2+j];if(c>='0' && c<='9')c-='0';else if(c>='a' && c<='f')c=c-'a'+10;else return 0;x=x*16+c; }out[i]=(unsigned char)x; }return 1;
}
static const char *vault_error(VaultResult r) { return r==VAULT_BUSY?"ERR vault busy":r==VAULT_STORAGE?"ERR vault storage":r==VAULT_DENIED?"ERR vault denied":r==VAULT_INVALID?"ERR vault syntax":"ERR no file"; }
int downloads_command(Vault *v,const Principal *p,const char *line,const char *protocol,char *out,size_t size) {
    int block=!strncmp(line,"FILEFETCHBLOCK ",15);
    if(!block && strncmp(line,"FILEOPEN ",9) && strncmp(line,"FILEFETCH ",10) && strncmp(line,"FILEDONE ",9) && strncmp(line,"FILEABORT ",10))return 0;
    const char *error="ERR vault syntax";char token[33]={0},hash[65]={0};uint32_t id=0,offset=0;int n=0;
    if(journal.broken || journal.fd<0) { error="ERR vault storage";goto fail; }
    if(!(p->capabilities&CAP_VAULT_READ)) { error="ERR vault denied";goto fail; }
    if(!strncmp(line,"FILEOPEN ",9)) {
        if(sscanf(line+9,"%u%n",&id,&n)!=1 || line[9+n] || !id || id>VAULT_ID_MAX)goto fail;
        VaultEntry e;VaultResult result=vault_info(v,p,id,&e);if(result!=VAULT_OK) { error=vault_error(result);goto fail; }
        (void)find(p,"");Ticket *t=NULL;unsigned owned=0;
        for(unsigned i=0;i<TICKETS;i++) { if(!tickets[i].file && !t)t=&tickets[i];if(tickets[i].file && !strcmp(tickets[i].subject,p->subject))owned++; }
        if(!t || owned>=16) { error="ERR vault busy";goto fail; }
        unsigned char random[16];if(RAND_bytes(random,sizeof(random))!=1) { error="ERR vault storage";goto fail; }
        memset(t,0,sizeof(*t));for(unsigned i=0;i<16;i++)snprintf(t->token+i*2,3,"%02x",random[i]);
        snprintf(t->protocol,sizeof(t->protocol),"%s",protocol);snprintf(t->subject,sizeof(t->subject),"%s",p->subject);t->file=id;t->size=e.size;t->at=(uint64_t)time(NULL);memcpy(t->hash,e.sha256,32);vault_hex(e.sha256,hash);
        snprintf(out,size,"DOWNLOAD %s %u %s\r\n",t->token,e.size,hash);return 1;
    }
    if(block || !strncmp(line,"FILEFETCH ",10)) {
        int start=block?15:10;
        if(sscanf(line+start,"%32s %u%n",token,&offset,&n)!=2 || line[start+n] || !token_valid(token))goto fail;
    } else if(!strncmp(line,"FILEDONE ",9)) {
        if(sscanf(line+9,"%32s %64s%n",token,hash,&n)!=2 || line[9+n] || !token_valid(token))goto fail;
    } else if(sscanf(line+10,"%32s%n",token,&n)!=1 || line[10+n] || !token_valid(token))goto fail;
    Ticket *t=find(p,token);
    if(!strncmp(line,"FILEABORT ",10)) { if(t)memset(t,0,sizeof(*t));snprintf(out,size,"DOWNLOAD ABORTED\r\n");return 1; }
    if(!t) {
        if(!strncmp(line,"FILEDONE ",9))for(size_t i=count;i>0;i--) {
            Receipt *r=&receipts[i-1];if(!strcmp(r->token,token) && !strcmp(r->subject,p->subject)) {
                VaultEntry e;unsigned char digest[32];
                if(!parse_hash(hash,digest) || vault_info(v,p,r->file,&e)!=VAULT_OK || memcmp(digest,e.sha256,32))break;
                snprintf(out,size,"DOWNLOADED %u\r\n",r->file);return 1;
            }
        }
        error="ERR download ticket";goto fail;
    }
    VaultEntry e;VaultResult result=vault_info(v,p,t->file,&e);if(result!=VAULT_OK) { error=vault_error(result);goto fail; }
    if(e.size!=t->size || memcmp(e.sha256,t->hash,32)) { error="ERR no file";goto fail; }
    if(block || !strncmp(line,"FILEFETCH ",10)) {
        if(offset>t->received)goto fail;
        unsigned char bytes[VAULT_BLOCK],encoded[(VAULT_BLOCK+2)/3*4+1];size_t got;
        result=vault_read_block(v,p,t->file,offset,bytes,block?VAULT_BLOCK:VAULT_CHUNK,&got);if(result!=VAULT_OK) { error=vault_error(result);goto fail; }
        if(offset+got>t->received)t->received=offset+(uint32_t)got;
        EVP_EncodeBlock(encoded,bytes,(int)got);snprintf(out,size,"DOWNLOADDATA %u %zu %s\r\n",offset,got,got?(char *)encoded:"-");return 1;
    }
    unsigned char digest[32];if(!parse_hash(hash,digest) || memcmp(digest,t->hash,32) || t->received!=t->size)goto fail;
    if(current_guard && !current_guard(guard_context)) { error="ERR vault denied";goto fail; }
    if(!reserve()) { error="ERR vault storage";goto fail; }
    DurableRow row={0};row.kind='d';row.target=t->file;snprintf(row.text,sizeof(row.text),"%s %s %u %s",p->subject,t->token,t->size,t->protocol);
    if(!dj_append(&journal,&row) || !apply(NULL,&row) || !downloads_prune(v)) { error="ERR vault storage";goto fail; }
    snprintf(out,size,"DOWNLOADED %u\r\n",t->file);memset(t,0,sizeof(*t));return 1;
fail:snprintf(out,size,"%s\r\n",error);return 1;
}
void downloads_report(Vault *v,const Principal *p,int history,uint32_t file,uint32_t cursor,char *out,size_t size) {
    if(journal.fd<0 || journal.broken) { snprintf(out,size,"ERR vault storage\r\n");return; }
    if(file>VAULT_ID_MAX) { snprintf(out,size,"ERR vault syntax\r\n");return; }
    if(history) {
        if(!(p->capabilities&CAP_VAULT_MANAGE)) { snprintf(out,size,"ERR vault denied\r\n");return; }
        size_t index=cursor?cursor-1:count;if(index>count) { snprintf(out,size,"ERR vault syntax\r\n");return; }
        size_t used=(size_t)snprintf(out,size,"FILEHISTORY\r\n");unsigned shown=0,scanned=0;
        while(index && shown<8 && scanned++<256) { Receipt *r=&receipts[--index];if(file && r->file!=file)continue;
            used+=(size_t)snprintf(out+used,size-used,"DOWNLOADROW %zu %u %s %" PRIu64 " %u %s\r\n",index+1,r->file,r->subject,r->at,r->size,r->protocol);shown++;
        }
        snprintf(out+used,size-used,"END NEXT %zu\r\n",index?index+1:0);return;
    }
    uint32_t first=file?file:cursor?cursor:1;if(first>VAULT_ID_MAX) { snprintf(out,size,"ERR vault syntax\r\n");return; }
    size_t used=(size_t)snprintf(out,size,"FILESTATS\r\n");unsigned shown=0;uint32_t i=first;
    for(;i<=vault_last_id(v) && (!file || i==file);i++) { VaultEntry e;
        VaultResult result=vault_info(v,p,i,&e);
        if(result!=VAULT_OK && result!=VAULT_NOT_FOUND && result!=VAULT_DENIED) { snprintf(out,size,"%s\r\n",vault_error(result));return; }
        int visible=result==VAULT_OK;
        if(!visible && (!(p->capabilities&CAP_VAULT_MANAGE) || !(i<total_capacity?totals[i]:0)))continue;
        if(shown==8)break;
        used+=(size_t)snprintf(out+used,size-used,"DOWNLOADCOUNT %u %" PRIu64 "\r\n",i,i<total_capacity?totals[i]:0);shown++;
    }
    snprintf(out+used,size-used,"END NEXT %u\r\n",!file && i<=vault_last_id(v)?i:0);
}

static int download_snapshot(Durable *d,DurableRow *r) {
    unsigned char raw[DJ_ROW]={0};size_t n=strlen(r->text);if(n>DJ_TEXT)return 0;raw[0]=(unsigned char)r->kind;dj_put(raw+4,++d->sequence);dj_put(raw+8,r->actor);dj_put(raw+12,r->target);dj_put(raw+16,(uint32_t)r->time);dj_put(raw+20,(uint32_t)(r->time>>32));dj_put(raw+24,(uint32_t)n);memcpy(raw+28,r->text,n);dj_put(raw+DJ_ROW-4,dj_sum(raw));return dj_io(d->fd,raw,DJ_ROW,1)==DJ_ROW;
}
int downloads_prune(Vault *v) {
    if(!count)return 1;
    unsigned counts[1025]={0};unsigned char *keep=calloc(count,1);uint64_t *retained=calloc(total_capacity,sizeof(*retained));if(!keep || !retained) { free(keep);free(retained);return 0; }
    size_t total=0;int ok=1;
    for(size_t i=count;i>0;i--) { unsigned board,limit;if(!vault_log_policy(v,receipts[i-1].file,&board,&limit)) { ok=0;break; }if(!limit || counts[board]<limit) { keep[i-1]=1;counts[board]++;total++;retained[receipts[i-1].file]++; } }
    if(!ok || total==count) { free(keep);free(retained);return ok; }
    unsigned char random[8];char suffix[17],temp[4160];if(RAND_bytes(random,8)!=1) { free(keep);free(retained);return 0; }for(unsigned i=0;i<8;i++)snprintf(suffix+i*2,3,"%02x",random[i]);snprintf(temp,sizeof(temp),"%s.retaining-%s",journal_path,suffix);
    Durable next={-1,0,0};if(!dj_open(&next,temp,"LNDOWN01",NULL,NULL))ok=0;
    for(uint32_t id=1;ok && id<total_capacity;id++)if(totals[id]>retained[id]) { DurableRow row={0};row.kind='S';row.target=id;snprintf(row.text,sizeof(row.text),"%" PRIu64,totals[id]-retained[id]);ok=download_snapshot(&next,&row); }
    for(size_t i=0;ok && i<count;i++)if(keep[i]) { Receipt *r=&receipts[i];DurableRow row={0};row.kind='d';row.target=r->file;row.time=r->at;snprintf(row.text,sizeof(row.text),"%s %s %u %s",r->subject,r->token,r->size,r->protocol);ok=download_snapshot(&next,&row); }
if(ok && DJ_SYNC(next.fd))ok=0;
#ifdef _WIN32
    if(ok) { dj_close(&journal);dj_close(&next);ok=MoveFileExA(temp,journal_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0; }
#else
    if(ok)ok=storage_rename(temp,journal_path)==0;
#endif
    if(ok) {
#ifdef _WIN32
        count=0;memset(totals,0,total_capacity*sizeof(*totals));ok=dj_open(&journal,journal_path,"LNDOWN01",apply,NULL);free(keep);free(retained);return ok;
#else
        dj_close(&journal);
        if(storage_database()) {
            uint32_t sequence=next.sequence;dj_close(&next);
            next.fd=storage_open(journal_path,O_RDWR,0600);next.sequence=sequence;
            if(next.fd<0) { journal.broken=1;free(keep);free(retained);return 0; }
        }
        journal=next;
        char directory[4096];strcpy(directory,journal_path);char *slash=strrchr(directory,'/');if(slash) { if(slash==directory)slash[1]=0;else *slash=0; }else strcpy(directory,".");
        int dir=open(directory,O_RDONLY);if(dir<0 || storage_sync(dir)) { if(dir>=0)storage_close(dir);journal.broken=1;free(keep);free(retained);return 0; }storage_close(dir);
#endif
        size_t n=0;for(size_t i=0;i<count;i++)if(keep[i])receipts[n++]=receipts[i];count=n; }
    else { dj_close(&next);journal.broken=1; }
    free(keep);free(retained);return ok;
}

uint64_t downloads_count(uint32_t id) { return id<total_capacity?totals[id]:0; }

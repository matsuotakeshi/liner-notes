#define _POSIX_C_SOURCE 200809L
#include "download.h"
#include "download_repository.h"
#include "durable.h"
#include "store_limits.h"
#include <stdlib.h>
#include <inttypes.h>
#include <openssl/rand.h>
typedef DownloadReceipt Receipt;
static Durable journal={-1,0,0};static char journal_path[4096];
static Receipt *receipts;static size_t count,capacity;
static uint64_t *totals;static size_t total_capacity;
static int reserve(void) {
    if(count<capacity)return 1;
    size_t next=capacity?capacity*2:128;if(next<capacity || next>SIZE_MAX/sizeof(*receipts))return 0;
    Receipt *p=realloc(receipts,next*sizeof(*p));if(!p)return 0;receipts=p;capacity=next;return 1;
}
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
       (row->kind=='D'?sscanf(row->text,"%79s %32s %u%n",r.subject,r.token,&r.size,&n)!=3:sscanf(row->text,"%79s %32s %u %11s%n",r.subject,r.token,&r.size,r.protocol,&n)!=4) || row->text[n] || !download_token_valid(r.token) || r.size>VAULT_MAX_BYTES || !reserve())return 0;
    if(row->kind=='D')strcpy(r.protocol,"UNKNOWN");
    if(strcmp(r.protocol,"UNKNOWN") && strcmp(r.protocol,"LINE") && strcmp(r.protocol,"ZMODEM") && strcmp(r.protocol,"XMODEM") && strcmp(r.protocol,"YMODEM") && strcmp(r.protocol,"YMODEM-G") && strcmp(r.protocol,"MLINK") && strcmp(r.protocol,"TEXT") && strcmp(r.protocol,"XMODEM-SUM") && strcmp(r.protocol,"XMODEM-1K"))return 0;
    r.file=row->target;r.at=row->time;if(totals[r.file]==UINT64_MAX)return 0;receipts[count++]=r;totals[r.file]++;return 1;
}
int download_repository_open(const char *directory) {
    download_repository_close();char path[4096];int n=snprintf(path,sizeof(path),"%s/downloads.dat",directory);
    if(n>0 && (size_t)n<sizeof(path))strcpy(journal_path,path);
    return n>0 && (size_t)n<sizeof(path) && dj_open(&journal,path,"LNDOWN01",apply,NULL);
}
void download_repository_close(void) { dj_close(&journal);free(receipts);receipts=NULL;count=capacity=0;free(totals);totals=NULL;total_capacity=0; }

int download_repository_ready(void) { return journal.fd>=0 && !journal.broken; }
size_t download_repository_count(void) { return count; }
int download_repository_get(size_t index,DownloadReceipt *out) {
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!download_repository_ready() || index>=count)return 0;
    *out=receipts[index];return 1;
}
int download_repository_commit(Vault *v,const DownloadReceipt *r) {
    if(!reserve())return 0;
    DurableRow row={0};row.kind='d';row.target=r->file;
    snprintf(row.text,sizeof(row.text),"%s %s %u %s",r->subject,r->token,r->size,r->protocol);
    return dj_append(&journal,&row) && apply(NULL,&row) && downloads_prune(v);
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

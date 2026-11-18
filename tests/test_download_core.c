#include "download_core.h"
#include "download_repository.h"
#include "download.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static int ready=1,random_ok=1,guard_ok=1,write_ok=1,deny,read_failure;
static uint32_t size=3,last_id=12,error_file;
static unsigned char hash_byte=7;
static time_t now=1000;
static unsigned random_counter,commits;
static DownloadReceipt receipts[1024];static size_t receipt_count;
static size_t bad_index=SIZE_MAX;
static Vault *vault=(Vault *)1;
static Principal alice={.subject="member:1",.capabilities=CAP_VAULT_READ};
static Principal bob={.subject="member:2",.capabilities=CAP_VAULT_READ};
static Principal admin={.subject="member:3",.capabilities=CAP_VAULT_READ|CAP_VAULT_MANAGE};
time_t time(time_t *out) { if(out)*out=now;return now; }
int RAND_bytes(unsigned char *p,int n) { if(!random_ok)return 0;memset(p,0,(size_t)n);unsigned value=++random_counter;for(int i=0;i<n && i<4;i++)p[i]=(unsigned char)(value>>(8*i));return 1; }
int download_repository_ready(void) { return ready; }
int download_repository_open(const char *directory) { (void)directory;ready=1;return 1; }
void download_repository_close(void) { ready=0;receipt_count=0; }
size_t download_repository_count(void) { return receipt_count; }
int download_repository_get(size_t index,DownloadReceipt *out) { if(index>=receipt_count || index==bad_index)return 0;*out=receipts[index];return 1; }
int download_repository_commit(Vault *v,const DownloadReceipt *r) { assert(v==vault);commits++;if(!write_ok)return 0;assert(receipt_count<1024);receipts[receipt_count++]=*r;return 1; }
uint64_t downloads_count(uint32_t id) { uint64_t n=0;for(size_t i=0;i<receipt_count;i++)if(receipts[i].file==id)n++;return n; }
uint32_t vault_last_id(Vault *v) { assert(v==vault);return last_id; }
VaultResult vault_info(Vault *v,const Principal *p,uint32_t id,VaultEntry *out) {
    assert(v==vault);
    if(id==error_file)return VAULT_STORAGE;
    if(deny || !(p->capabilities&CAP_VAULT_READ) || id==2)return VAULT_DENIED;
    if(id>last_id)return VAULT_NOT_FOUND;
    memset(out,0,sizeof(*out));out->id=id;out->size=size;out->sha256[0]=hash_byte;return VAULT_OK;
}
VaultResult vault_read_block(Vault *v,const Principal *p,uint32_t id,uint32_t offset,unsigned char *out,size_t capacity,size_t *got) {
    VaultEntry e;VaultResult r=vault_info(v,p,id,&e);if(r!=VAULT_OK)return r;
    if(read_failure) { out[0]='!';return VAULT_STORAGE; }
    *got=size-offset;if(*got>capacity)*got=capacity;
    for(size_t i=0;i<*got;i++)out[i]=(unsigned char)('A'+(offset+i)%26);
    return VAULT_OK;
}
void vault_hex(const unsigned char digest[32],char out[65]) { for(unsigned i=0;i<32;i++)snprintf(out+i*2,3,"%02x",digest[i]); }
static int guard(void *ctx) { assert(ctx==&guard_ok);return guard_ok; }
static int zero(const void *p,size_t n) { const unsigned char *s=p;for(size_t i=0;i<n;i++)if(s[i])return 0;return 1; }
static void reset(void) {
    downloads_close();assert(downloads_open("ignored"));downloads_guard(guard,&guard_ok);
    random_ok=guard_ok=write_ok=1;deny=read_failure=0;size=3;last_id=12;error_file=0;
    hash_byte=7;now=1000;commits=0;bad_index=SIZE_MAX;
}
static DownloadOpened open_file(Principal *p) { DownloadOpened out;assert(download_core_open(vault,p,1,DOWNLOAD_ZMODEM,&out)==DOWNLOAD_OK);return out; }
static DownloadDigest digest_of(const DownloadOpened *o) { DownloadDigest d={.valid=1};memcpy(d.bytes,o->sha256,32);return d; }
int main(void) {
    reset();DownloadOpened opened;DownloadCompleted done;DownloadBlock data;DownloadHistory history;DownloadStats stats;
    ready=0;assert(download_core_open(vault,&alice,1,DOWNLOAD_LINE,&opened)==DOWNLOAD_STORAGE && zero(&opened,sizeof(opened)));ready=1;
    Principal nobody={0};assert(download_core_open(vault,&nobody,1,DOWNLOAD_LINE,&opened)==DOWNLOAD_DENIED);
    assert(download_core_open(vault,&alice,0,DOWNLOAD_LINE,&opened)==DOWNLOAD_INVALID);
    assert(download_core_open(vault,&alice,1,(DownloadProtocol)99,&opened)==DOWNLOAD_INVALID);
    random_ok=0;assert(download_core_open(vault,&alice,1,DOWNLOAD_LINE,&opened)==DOWNLOAD_STORAGE);random_ok=1;
    opened=open_file(&alice);DownloadDigest digest=digest_of(&opened);
    assert(download_core_fetch(vault,&bob,opened.token,0,VAULT_CHUNK,&data)==DOWNLOAD_TICKET && zero(&data,sizeof(data)));
    assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_INVALID && !commits);
    assert(download_core_fetch(vault,&alice,opened.token,1,VAULT_CHUNK,&data)==DOWNLOAD_INVALID);
    assert(download_core_fetch(vault,&alice,opened.token,0,1,&data)==DOWNLOAD_INVALID);
    hash_byte++;assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_CHUNK,&data)==DOWNLOAD_NOT_FOUND);hash_byte--;
    size++;assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_CHUNK,&data)==DOWNLOAD_NOT_FOUND);size--;
    read_failure=1;assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_BLOCK,&data)==DOWNLOAD_STORAGE && zero(&data,sizeof(data)));read_failure=0;
    assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_CHUNK,&data)==DOWNLOAD_OK && data.size==3 && !memcmp(data.bytes,"ABC",3));
    assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_BLOCK,&data)==DOWNLOAD_OK);
    digest.valid=0;assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_INVALID);digest.valid=1;
    guard_ok=0;assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_DENIED && !commits);guard_ok=1;
    write_ok=0;memset(&done,0xff,sizeof(done));assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_STORAGE && zero(&done,sizeof(done)));write_ok=1;
    assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_OK && done.file==1 && downloads_count(1)==1);
    assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_OK && downloads_count(1)==1);
    assert(download_core_finish(vault,&bob,opened.token,&digest,&done)==DOWNLOAD_TICKET);
    deny=1;assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_TICKET);deny=0;
    assert(!strcmp(receipts[0].protocol,"ZMODEM"));
    puts("PASS: resolved identity/ACL, partial/replayed reads, current metadata, guard and commit failure, duplicate receipt");

    reset();opened=open_file(&alice);assert(download_core_abort(&bob,opened.token)==DOWNLOAD_OK);
    assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_CHUNK,&data)==DOWNLOAD_OK);
    assert(download_core_abort(&alice,opened.token)==DOWNLOAD_OK);
    assert(download_core_abort(&alice,opened.token)==DOWNLOAD_OK);
    assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_CHUNK,&data)==DOWNLOAD_TICKET);
    for(int i=0;i<16;i++)opened=open_file(&alice);
    assert(download_core_open(vault,&alice,1,DOWNLOAD_LINE,&opened)==DOWNLOAD_BUSY);
    (void)open_file(&bob);now=4600;
    assert(download_core_open(vault,&alice,1,DOWNLOAD_LINE,&opened)==DOWNLOAD_BUSY);
    now=4601;opened=open_file(&alice);digest=digest_of(&opened);
    assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_CHUNK,&data)==DOWNLOAD_OK);
    downloads_close();assert(downloads_open("ignored"));assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_TICKET);
    reset();size=0;opened=open_file(&alice);digest=digest_of(&opened);
    assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_OK);
    reset();for(int u=0;u<64;u++) { Principal user=alice;snprintf(user.subject,sizeof(user.subject),"user:%d",u);for(int i=0;i<16;i++)(void)open_file(&user); }
    assert(download_core_open(vault,&alice,1,DOWNLOAD_LINE,&opened)==DOWNLOAD_BUSY);
    puts("PASS: subject-scoped idempotent abort, per-subject/global quotas, exact expiry, restart tickets and empty file");

    reset();size=VAULT_BLOCK+1;opened=open_file(&alice);digest=digest_of(&opened);
    assert(download_core_fetch(vault,&alice,opened.token,0,VAULT_BLOCK,&data)==DOWNLOAD_OK && data.size==VAULT_BLOCK);
    assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_INVALID);
    assert(download_core_fetch(vault,&alice,opened.token,VAULT_BLOCK,VAULT_BLOCK,&data)==DOWNLOAD_OK && data.size==1);
    assert(download_core_finish(vault,&alice,opened.token,&digest,&done)==DOWNLOAD_OK);
    puts("PASS: full 64KiB block and final partial block completion boundary");

    reset();receipt_count=260;for(size_t i=0;i<receipt_count;i++) { DownloadReceipt *r=&receipts[i];memset(r,0,sizeof(*r));r->file=i<4?1:2;r->size=3;r->at=100+i;strcpy(r->subject,"member:1");strcpy(r->protocol,"LINE"); }
    assert(download_core_history(&alice,0,0,&history)==DOWNLOAD_DENIED && zero(&history,sizeof(history)));
    assert(download_core_history(&admin,1,0,&history)==DOWNLOAD_OK && !history.count && history.next==5);
    assert(download_core_history(&admin,1,5,&history)==DOWNLOAD_OK && history.count==4 && !history.next && history.rows[0].sequence==4);
    assert(download_core_history(&admin,0,0,&history)==DOWNLOAD_OK && history.count==8 && history.next==253);
    assert(!strcmp(history.rows[0].protocol,"LINE") && history.rows[0].sequence==260);
    assert(download_core_history(&admin,0,262,&history)==DOWNLOAD_INVALID && zero(&history,sizeof(history)));
    bad_index=258;assert(download_core_history(&admin,0,0,&history)==DOWNLOAD_STORAGE && zero(&history,sizeof(history)));bad_index=SIZE_MAX;
    assert(download_core_stats(vault,&alice,0,0,&stats)==DOWNLOAD_OK && stats.count==8 && stats.next==10 && stats.rows[1].file==3);
    assert(download_core_stats(vault,&admin,0,0,&stats)==DOWNLOAD_OK && stats.count==8 && stats.next==9 && stats.rows[1].file==2 && stats.rows[1].count==256);
    assert(download_core_stats(vault,&admin,2,99,&stats)==DOWNLOAD_OK && stats.count==1 && !stats.next);
    error_file=3;assert(download_core_stats(vault,&alice,0,0,&stats)==DOWNLOAD_STORAGE && zero(&stats,sizeof(stats)));error_file=0;
    puts("PASS: private history, filtered scan continuation, stable order, visible counts and no partial results on failure");

    reset();char output[100000],command[160],hash[65];
    assert(!downloads_command(vault,&alice,"UNRELATED","LINE",output,sizeof(output)));
    assert(downloads_command(vault,&alice,"FILEOPEN 1 trailing","LINE",output,sizeof(output)) && !strcmp(output,"ERR vault syntax\r\n"));
    assert(downloads_command(vault,&alice,"FILEOPEN 1","LINE",output,sizeof(output)));
    char token[33];unsigned length;assert(sscanf(output,"DOWNLOAD %32s %u %64s",token,&length,hash)==3 && length==3);
    snprintf(command,sizeof(command),"FILEFETCHBLOCK %s 0",token);downloads_command(vault,&alice,command,"LINE",output,sizeof(output));assert(!strcmp(output,"DOWNLOADDATA 0 3 QUJD\r\n"));
    snprintf(command,sizeof(command),"FILEFETCH %s 3",token);downloads_command(vault,&alice,command,"LINE",output,sizeof(output));assert(!strcmp(output,"DOWNLOADDATA 3 0 -\r\n"));
    snprintf(command,sizeof(command),"FILEDONE %s %s",token,hash);downloads_command(vault,&alice,command,"LINE",output,sizeof(output));assert(!strcmp(output,"DOWNLOADED 1\r\n"));
    downloads_command(vault,&alice,command,"LINE",output,sizeof(output));assert(!strcmp(output,"DOWNLOADED 1\r\n") && downloads_count(1)==1);
    downloads_report(vault,&admin,1,1,0,output,sizeof(output));assert(strstr(output,"FILEHISTORY\r\nDOWNLOADROW 1 1 member:1 ") && strstr(output," 3 LINE\r\nEND NEXT 0\r\n"));
    downloads_report(vault,&alice,0,1,0,output,sizeof(output));assert(!strcmp(output,"FILESTATS\r\nDOWNLOADCOUNT 1 1\r\nEND NEXT 0\r\n"));
    ready=0;downloads_command(vault,&nobody,"FILEOPEN bad","LINE",output,sizeof(output));assert(!strcmp(output,"ERR vault storage\r\n"));ready=1;
    downloads_command(vault,&nobody,"FILEOPEN bad","LINE",output,sizeof(output));assert(!strcmp(output,"ERR vault denied\r\n"));
    puts("PASS: actual legacy parser/formatter, base64 block/EOF, report markers and error precedence");return 0;
}

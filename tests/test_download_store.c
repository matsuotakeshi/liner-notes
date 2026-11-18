#define _POSIX_C_SOURCE 200809L
#include "download.h"
#include "download_repository.h"
#include "durable.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned keep_limit;
static int policy_ok=1;
int vault_log_policy(Vault *v,uint32_t id,unsigned *board,unsigned *limit) {
    (void)v;*board=id==1?0:1;*limit=keep_limit;return policy_ok;
}
static DownloadReceipt receipt(unsigned number,uint32_t file,const char *protocol) {
    DownloadReceipt r={0};r.file=file;r.size=123;strcpy(r.subject,"member:1");
    snprintf(r.token,sizeof(r.token),"%032x",number);strcpy(r.protocol,protocol);return r;
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==3);
#ifndef _WIN32
    if(argc==3)assert(storage_init(argv[2]));
#endif
    char path[4096];assert(snprintf(path,sizeof(path),"%s/downloads.dat",argv[1])>0);
    /* Real legacy D row, before transfer protocol was recorded. */
    Durable old={-1,0,0};assert(dj_open(&old,path,"LNDOWN01",NULL,NULL));
    DurableRow legacy={0};legacy.kind='D';legacy.target=1;
    strcpy(legacy.text,"member:1 00000000000000000000000000000001 123");
    assert(dj_append(&old,&legacy));dj_close(&old);
    assert(download_repository_open(argv[1]) && download_repository_ready());
    DownloadReceipt row;assert(download_repository_get(0,&row));
    assert(!strcmp(row.protocol,"UNKNOWN") && row.file==1 && downloads_count(1)==1);
    const char *protocols[]={"LINE","ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};
    for(unsigned i=0;i<9;i++) { row=receipt(i+2,1,protocols[i]);assert(download_repository_commit(NULL,&row)); }
    row=receipt(11,2,"LINE");assert(download_repository_commit(NULL,&row));
    assert(download_repository_count()==11 && downloads_count(1)==10 && downloads_count(2)==1);
    download_repository_close();assert(!download_repository_ready());
    assert(download_repository_open(argv[1]));
    for(unsigned i=0;i<9;i++) { assert(download_repository_get(i+1,&row));assert(!strcmp(row.protocol,protocols[i])); }
    assert(downloads_count(1)==10 && downloads_count(2)==1);
    puts("PASS: legacy D/new d journal replay, all protocols, cumulative totals and restart");

    keep_limit=2;assert(downloads_prune(NULL));
    assert(download_repository_count()==3 && downloads_count(1)==10 && downloads_count(2)==1);
    assert(download_repository_get(0,&row) && !strcmp(row.protocol,"XMODEM-SUM"));
    assert(download_repository_get(1,&row) && !strcmp(row.protocol,"XMODEM-1K"));
    assert(download_repository_get(2,&row) && row.file==2);
    download_repository_close();
#ifndef _WIN32
    if(argc==3) { storage_shutdown();assert(storage_init(argv[2])); }
#endif
    assert(download_repository_open(argv[1]));
    assert(download_repository_count()==3 && downloads_count(1)==10 && downloads_count(2)==1);
    assert(downloads_prune(NULL));assert(download_repository_count()==3);
    puts("PASS: actual retention replacement, S summaries, per-board windows, cumulative totals after reopen");

    /* Preserve the existing partial-commit boundary. A failed prune can follow
     * a durable receipt. It is not a rollback and must not be reported as one. */
    policy_ok=0;row=receipt(12,2,"TEXT");assert(!download_repository_commit(NULL,&row));
    assert(downloads_count(2)==2);download_repository_close();policy_ok=1;
    assert(download_repository_open(argv[1]));assert(downloads_count(2)==2);download_repository_close();
    int fd=DJ_OPEN(path);assert(fd>=0);long long before=(long long)DJ_SEEK(fd,0,SEEK_END);assert(before>8);
    assert(DJ_WRITE(fd,"bad",3)==3);assert(!DJ_SYNC(fd));assert(!DJ_CLOSE(fd));
    assert(!download_repository_open(argv[1]));download_repository_close();
    fd=DJ_OPEN(path);assert(fd>=0 && (long long)DJ_SEEK(fd,0,SEEK_END)==before+3);assert(!DJ_CLOSE(fd));
#ifndef _WIN32
    storage_shutdown();
#endif
    puts("PASS: failed prune after committed receipt, restart consistency and damaged-tail refusal without truncation");return 0;
}

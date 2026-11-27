#define _POSIX_C_SOURCE 200809L
#include "zmodem_host.h"
#include "app_limits.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Exercise the real child startup with one failed descriptor operation. */
static int reject_stderr;
static int fixture_dup2(int oldfd,int newfd) {
    if(reject_stderr && newfd==STDERR_FILENO) { errno=EBADF;return -1; }
    return dup2(oldfd,newfd);
}
#define dup2 fixture_dup2
#include "../src/zmodem_host.c"
#undef dup2
static uint64_t now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static int sink(void *c,const unsigned char *data,size_t size) { (void)c;(void)data;(void)size;return 1; }
static void personal(const char *path,const char *body,int expected) {
    FILE *file=fopen(path,"w");assert(file);
    assert(fputs("#!/bin/sh\n",file)>=0 && fputs(body,file)>=0 && !fclose(file));
    ZHost *host=zhost_start(path,ZHOST_RECEIVE_PERSONAL,111,"alice","",1,now(),0);
    assert(host);int done=0;
    uint64_t limit=now()+5000;
    while(!done && now()<limit) {
        done=zhost_pump(host,sink,NULL,now());
        struct timespec pause={0,10000000};nanosleep(&pause,NULL);
    }
    assert(done==expected);
    if(done==1) {
        size_t size;
        const char *payload=zhost_personal_payload(host,&size);
        assert(payload && size==3 && !memcmp(payload,"a\r\n",size));
    }
    zhost_stop(host);
    while(zhost_pending() && now()<limit)zhost_reap(now());
    assert(!zhost_pending());
}
int main(void) {
    char path[]="/tmp/liner-zhost-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    const char code[]="#!/bin/sh\nexec sleep 30\n";assert(write(fd,code,sizeof(code)-1)==sizeof(code)-1);assert(!fchmod(fd,0700));close(fd);
    for(int round=0;round<2;round++) {
        ZHost *hosts[ZMODEM_MAX];for(int i=0;i<ZMODEM_MAX;i++) { hosts[i]=zhost_start(path,1,0,"alice","",1,now(),0);assert(hosts[i]); }
        assert(!zhost_start(path,1,0,"alice","",1,now(),0));
        for(int i=0;i<ZMODEM_MAX;i++)zhost_stop(hosts[i]);
        uint64_t limit=now()+5000;while(zhost_pending() && now()<limit) { zhost_reap(now());struct timespec pause={0,10000000};nanosleep(&pause,NULL); }
        assert(!zhost_pending());
    }
    FILE *f=fopen(path,"w");assert(f);assert(fputs("#!/bin/sh\nprintf 'DONE OK 16 17179869184\\n' >&3\n",f)>=0);assert(!fclose(f));
    ZHost *h=zhost_start(path,10,1,"alice","",1,now(),0);assert(h);int done=0;
    uint64_t limit=now()+5000;while(!done && now()<limit) { done=zhost_pump(h,sink,NULL,now());struct timespec pause={0,10000000};nanosleep(&pause,NULL); }
    assert(done==1);unsigned id=0;uint64_t bytes=0;int recovery=1;zhost_receipt(h,&id,&bytes,&recovery);
    assert(id==16 && bytes==UINT64_C(17179869184) && !recovery);zhost_stop(h);
    puts("PASS: 16GiB receipt preserved across helper control channel");
    f=fopen(path,"w");assert(f);assert(fputs("#!/bin/sh\nexec sleep 30\n",f)>=0);assert(!fclose(f));
    h=zhost_start(path,1,0,"alice","",1,0,0);assert(h);
    assert(zhost_input(h,'a'));assert(zhost_pump(h,sink,NULL,500000)==0);
    assert(zhost_input(h,'b'));assert(zhost_pump(h,sink,NULL,1000000)==0);
    assert(zhost_pump(h,sink,NULL,1600001)==-1);
    zhost_receipt(h,&id,&bytes,&recovery);assert(recovery==5);zhost_stop(h);
    limit=now()+5000;while(zhost_pending() && now()<limit) { zhost_reap(now());struct timespec pause={0,10000000};nanosleep(&pause,NULL); }assert(!zhost_pending());
    puts("PASS: host watchdog permits continuing activity beyond ten minutes and terminates an idle helper");
    const char valid[]="printf 'PFILE 610d0a\\nDONE OK 0 3\\n' >&3\n";
    reject_stderr=1;
    personal(path,valid,-1);
    reject_stderr=0;
    personal(path,valid,1);
    personal(path,"printf 'PFILE 610d0a\\nDONE OK 0 2\\n' >&3\n",-1);
    personal(path,"printf 'PFILE 61000a\\nDONE OK 0 3\\n' >&3\n",-1);
    personal(path,"printf 'PFILE 610d0a\\nDONE OK 0 3\\nPFILE 62\\n' >&3\n",-1);
    personal(path,"printf 'PFILE 610d0a\\nDONE OK 0 3\\n' >&3\nexit 1\n",-1);
    personal(path,"printf 'FILE 1\\n' >&3\n",-1);
    assert(!unlink(path));puts("PASS: 24 concurrent ZMODEM children, twenty-fifth refused, all reaped and slots reusable");return 0;
}

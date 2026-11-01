#define _POSIX_C_SOURCE 200809L
#include "runtime_limits.h"
#include "zmodem_host.h"
#include <inttypes.h>
#include "app_limits.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
struct ZHost { int unused; };
int zhost_pending(void) { return 0; }
int zhost_supported(const char *p) { (void)p;return 0; }
ZHost *zhost_start(const char *p,int r,unsigned i,const char *u,const char *t,unsigned port,uint64_t n,unsigned protocol) { (void)protocol;(void)p;(void)r;(void)i;(void)u;(void)t;(void)port;(void)n;return NULL; }
int zhost_room(ZHost *h) { (void)h;return 0; }
int zhost_input(ZHost *h,unsigned char b) { (void)h;(void)b;return 0; }
int zhost_pump(ZHost *h,int (*w)(void *,const unsigned char *,size_t),void *c,uint64_t n) { (void)h;(void)w;(void)c;(void)n;return -1; }
const char *zhost_request(ZHost *h) { (void)h;return NULL; }
void zhost_submitted(ZHost *h) { (void)h; }
int zhost_answer(ZHost *h,const char *s) { (void)h;(void)s;return 0; }
void zhost_receipt(ZHost *h,unsigned *i,uint64_t *s,int *r) { (void)h;*i=*s=0;*r=0; }
void zhost_stop(ZHost *h) { (void)h; }
void zhost_reap(uint64_t n) { (void)n; }
int zhost_progress(ZHost *h,uint64_t *b,uint64_t *t) { (void)h;(void)b;(void)t;return 0; }
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
struct ZHost {
    int progress,wire_drained;uint64_t progress_bytes,progress_total,idle;
    pid_t pid;int wire,control,waiting,done,eof,exited,status,recovery;
    uint64_t started,size;unsigned id;
    unsigned char input[8192];size_t input_size;
    char command[1100],answer[4096];size_t command_size,answer_size;
};
static struct { pid_t pid;uint64_t at; } retired[ZMODEM_MAX];
static unsigned active;
int zhost_pending(void) { return active!=0; }
static uint64_t clock_now;
static int nonblock(int fd) { int f=fcntl(fd,F_GETFL);return f<0 || fcntl(fd,F_SETFL,f|O_NONBLOCK)<0; }
static int again(void) { return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR; }
int zhost_supported(const char *path) { return path && path[0]=='/' && access(path,X_OK)==0; }
void zhost_reap(uint64_t now) {
    clock_now=now;
    for(int i=0;i<ZMODEM_MAX;i++)if(retired[i].pid) {
        int status;pid_t p=waitpid(retired[i].pid,&status,WNOHANG);
        if(p==retired[i].pid || (p<0 && errno==ECHILD)) { retired[i].pid=0;if(active)active--; }
        else if(now-retired[i].at>2000)kill(-retired[i].pid,SIGKILL);
    }
}
ZHost *zhost_start(const char *helper,int receive,unsigned id,const char *user,const char *text,unsigned port,uint64_t now,unsigned protocol) {
    zhost_reap(now);if(active>=ln_limit("TRANSFERS",ZMODEM_MAX) || !zhost_supported(helper))return NULL;
    ZHost *h=calloc(1,sizeof(*h));if(!h)return NULL;
    int wire[2],control[2];if(socketpair(AF_UNIX,SOCK_STREAM,0,wire)) { free(h);return NULL; }
    if(socketpair(AF_UNIX,SOCK_STREAM,0,control)) { close(wire[0]);close(wire[1]);free(h);return NULL; }
    /* Duplicate child endpoints above stdio/control before fork. */
    int wi=fcntl(wire[1],F_DUPFD,4),ct=fcntl(control[1],F_DUPFD,4);
    close(wire[1]);close(control[1]);
    if(wi<0 || ct<0) { if(wi>=0)close(wi);if(ct>=0)close(ct);close(wire[0]);close(control[0]);free(h);return NULL; }
    char number[24],service[24];snprintf(number,sizeof(number),"%u",id);snprintf(service,sizeof(service),"%u",port);
    long maxfd=sysconf(_SC_OPEN_MAX);if(maxfd<0)maxfd=65536;
    pid_t pid=fork();
    if(!pid) {
        setpgid(0,0);dup2(wi,0);dup2(wi,1);dup2(ct,3);
        int nullfd=open("/dev/null",O_WRONLY);if(nullfd>=0)dup2(nullfd,2);
        for(int fd=4;fd<maxfd;fd++)close(fd);
        const char *action=receive==12?"receive-mail-many-resume":receive==11?"receive-mail-many":receive==10?"batch-mail":receive==9?"receive-many-resume":receive==8?"receive-mail-resume":receive==7?"receive-resume":receive==6?"receive-many":receive==5?"send-mail":receive==4?"receive-mail":receive==3?"receive-in":receive==2?"batch":receive?"receive":"send";const char *codes[]={"z","x","y","g","m","t","s","k"};char selected[48];if(protocol>7)_exit(127);snprintf(selected,sizeof(selected),"%s-%s",codes[protocol],action);
        execl(helper,helper,protocol?selected:action,number,user,text,service,(char *)NULL);_exit(127);
    }
    close(wi);close(ct);
    if(pid<0) { close(wire[0]);close(control[0]);free(h);return NULL; }
    setpgid(pid,pid);h->pid=pid;h->wire=wire[0];h->control=control[0];h->started=now;active++;
    if(nonblock(h->wire) || nonblock(h->control)) { zhost_stop(h);return NULL; }
    return h;
}
int zhost_room(ZHost *h) { return h->input_size<sizeof(h->input); }
int zhost_input(ZHost *h,unsigned char byte) {
    if(h->input_size==sizeof(h->input))return 0;
    h->input[h->input_size++]=byte;return 1;
}
static int flush(int fd,void *buffer,size_t *size) {
    if(!*size)return 1;
    ssize_t n=write(fd,buffer,*size);
    if(n<0)return again();
    if(!n)return 0;
    memmove(buffer,(char *)buffer+n,*size-(size_t)n);*size-=(size_t)n;return 1;
}
const char *zhost_request(ZHost *h) { return h->waiting==1?h->command:NULL; }
void zhost_submitted(ZHost *h) { h->waiting=2; }
int zhost_answer(ZHost *h,const char *text) {
    if(h->waiting!=2 || h->answer_size || strlen(text)>=sizeof(h->answer))return 0;
    strcpy(h->answer,text);h->answer_size=strlen(text);h->waiting=0;h->command_size=0;return 1;
}
int zhost_pump(ZHost *h,int (*write_bytes)(void *,const unsigned char *,size_t),void *context,uint64_t now) {
    if(!h->exited) {
        pid_t p=waitpid(h->pid,&h->status,WNOHANG);
        if(p==h->pid)h->exited=1;
        else if(p<0 && errno!=EINTR)return -1;
    }
    size_t pending_input=h->input_size;
    if(!h->exited && !flush(h->wire,h->input,&h->input_size))return -1;
    if(h->input_size<pending_input)h->started=now;
    if(!flush(h->control,h->answer,&h->answer_size))return -1;
    unsigned char data[1024];ssize_t n=read(h->wire,data,sizeof(data));
    if(n>0) { h->started=now;if(!write_bytes(context,data,(size_t)n))return -1; }
    else if(!n)h->eof=1;else if(!again())return -1;
    h->wire_drained=n<=0;
    if(!h->waiting && !h->done) {
        for(int budget=0;budget<1100;budget++) {
            char ch;ssize_t k=read(h->control,&ch,1);if(k<0) { if(!again())return -1;break; }if(!k)break;
            if(ch=='\n') {
                h->command[h->command_size]=0;
                if(!strncmp(h->command,"PROGRESS ",9)) {
                    char extra;if(sscanf(h->command,"PROGRESS %" SCNu64 " %" SCNu64 " %c",&h->progress_bytes,&h->progress_total,&extra)!=2 || h->progress_bytes>h->progress_total || h->progress_total>17179869184ULL)return -1;
                    h->progress=1;h->started=now;h->command_size=0;
                } else if(!strncmp(h->command,"IDLE ",5)) {
                    unsigned seconds;char extra;if(sscanf(h->command,"IDLE %u %c",&seconds,&extra)!=1 || seconds<1 || seconds>3600)return -1;
                    h->idle=(uint64_t)(seconds+5)*1000;h->command_size=0;
                } else if(!strncmp(h->command,"DONE ",5)) {
                    if(sscanf(h->command,"DONE OK %u %" SCNu64,&h->id,&h->size)==2)h->done=1;
                    else { h->done=-1;h->recovery=!strncmp(h->command,"DONE RECOVER ",13);if(h->recovery)fprintf(stderr,"ZMODEM recovery: %s\n",h->command+13);
                        const char *reasons[]={"NO_PENDING","MISMATCH","RECEIVER","TIMEOUT"};
                        for(int reason=0;reason<4;reason++)if(!strncmp(h->command,"DONE FAIL ",10) && !strcmp(h->command+10,reasons[reason])) { h->recovery=reason+2;fprintf(stderr,"ZMODEM failure: %s\n",reasons[reason]);break; } }
                } else { h->waiting=1;if(strcmp(h->command,"ADMIN"))h->started=now; }
                break;
            }
            if((unsigned char)ch<32 || (unsigned char)ch==127 || h->command_size>=sizeof(h->command)-1)return -1;
            h->command[h->command_size++]=ch;
        }
    }
    if(now-h->started>(h->idle>600000?h->idle:600000)) { h->recovery=5;return -1; }
    if(h->exited && h->eof)return h->done==1 && WIFEXITED(h->status) && WEXITSTATUS(h->status)==0?1:-1;
    return 0;
}
void zhost_receipt(ZHost *h,unsigned *id,uint64_t *size,int *recovery) { *id=h->id;*size=h->size;*recovery=h->recovery; }
int zhost_progress(ZHost *h,uint64_t *bytes,uint64_t *total) {
    if(!h || !h->progress || !h->wire_drained)return 0;
    *bytes=h->progress_bytes;*total=h->progress_total;h->progress=0;return 1;
}
void zhost_stop(ZHost *h) {
    if(!h)return;
    close(h->wire);close(h->control);
    if(h->exited) { kill(-h->pid,SIGKILL);if(active)active--; }
    else {
        kill(-h->pid,SIGTERM);
        for(int i=0;i<ZMODEM_MAX;i++)if(!retired[i].pid) { retired[i].pid=h->pid;retired[i].at=clock_now;break; }
    }
    memset(h,0,sizeof(*h));free(h);
}
#endif

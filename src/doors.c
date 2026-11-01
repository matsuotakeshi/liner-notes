#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#include "doors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <termios.h>
#ifdef __FreeBSD__
#include <libutil.h>
#else
#include <pty.h>
#endif
typedef struct { unsigned id,level,seconds;char exe[1024],dir[1024],title[161]; } Game;
static Game games[32];static unsigned game_count;
struct Door { pid_t pid;int fd,exited,after_cr;uint64_t until;unsigned char input[8192];size_t used; };
static pid_t reaping[128];
int doors_pending(void) { for(unsigned i=0;i<128;i++)if(reaping[i])return 1;return 0; }
void doors_reap(void) { for(unsigned i=0;i<128;i++)if(reaping[i]) { int status;pid_t p=waitpid(reaping[i],&status,WNOHANG);if(p==reaping[i] || (p<0 && errno==ECHILD))reaping[i]=0; } }
int doors_config(const char *path) {
    game_count=0;if(!path)return 1;FILE *f=fopen(path,"r");if(!f)return 0;
    char line[4096];int ok=1;
    while(fgets(line,sizeof(line),f)) {
        size_t n=strlen(line);if(n && line[n-1]=='\n')line[--n]=0;else if(!feof(f)) { ok=0;break; }
        if(n && line[n-1]=='\r')line[--n]=0;if(!*line || *line=='#')continue;
        if(game_count==32) { ok=0;break; }Game *g=&games[game_count];char *parts[6],*p=line;unsigned i;
        for(i=0;i<6;i++) { parts[i]=p;char *bar=strchr(p,'|');if(i<5) { if(!bar)break;*bar=0;p=bar+1; }else if(bar)break; }
        if(i!=6) { ok=0;break; }unsigned *numbers[]={&g->id,&g->level,&g->seconds};
        for(i=0;i<3;i++) { char *end;unsigned long v=strtoul(parts[i],&end,10);if(!*parts[i] || *end || *parts[i]<'0' || *parts[i]>'9' || v>86400)break;*numbers[i]=(unsigned)v; }
        if(i!=3 || !g->id || g->level>5 || !g->seconds || parts[3][0]!='/' || parts[4][0]!='/' || strlen(parts[3])>=sizeof(g->exe) || strlen(parts[4])>=sizeof(g->dir) || !*parts[5] || strlen(parts[5])>=sizeof(g->title)) { ok=0;break; }
        for(const unsigned char *q=(unsigned char *)parts[5];*q;q++)if(*q<32 || *q==127)ok=0;
        for(i=0;i<game_count;i++)if(games[i].id==g->id)ok=0;
        struct stat st;if(stat(parts[3],&st) || !S_ISREG(st.st_mode) || access(parts[3],X_OK) || stat(parts[4],&st) || !S_ISDIR(st.st_mode))ok=0;
        if(!ok)break;strcpy(g->exe,parts[3]);strcpy(g->dir,parts[4]);strcpy(g->title,parts[5]);game_count++;
    }
    if(ferror(f))ok=0;fclose(f);if(!ok)game_count=0;return ok;
}
void doors_list(unsigned level,char *out,size_t size) {
    size_t n=(size_t)snprintf(out,size,"GAMES\r\n");
    for(unsigned i=0;i<game_count;i++)if(level>=games[i].level)n+=(size_t)snprintf(out+n,size-n,"GAME %u %u %s\r\n",games[i].id,games[i].seconds,games[i].title);
    snprintf(out+n,size-n,"END GAMES\r\n");
}
Door *door_start(unsigned id,unsigned level,uint32_t user,const char *name,unsigned columns,unsigned rows,uint64_t now) {
    if(!user || geteuid()==0)return NULL;doors_reap();Game *g=NULL;
    for(unsigned i=0;i<game_count;i++)if(games[i].id==id && level>=games[i].level)g=&games[i];if(!g)return NULL;
    int master,slave;struct winsize ws={0};ws.ws_col=(unsigned short)columns;ws.ws_row=(unsigned short)rows;
    if(openpty(&master,&slave,NULL,NULL,&ws))return NULL;
    Door *d=calloc(1,sizeof(*d));if(!d) { close(master);close(slave);return NULL; }
    char idenv[64],nameenv[96],cols[32],lines[32],home[1100];
    snprintf(idenv,sizeof(idenv),"LINER_USER_ID=%u",user);snprintf(nameenv,sizeof(nameenv),"LINER_USER=%s",name);
    snprintf(cols,sizeof(cols),"COLUMNS=%u",columns);snprintf(lines,sizeof(lines),"LINES=%u",rows);snprintf(home,sizeof(home),"HOME=%s",g->dir);
    char *env[]={"PATH=/usr/local/bin:/usr/bin:/bin","TERM=xterm","LANG=C.UTF-8",idenv,nameenv,cols,lines,home,NULL};char *argv[]={g->exe,NULL};
    long maxfd=sysconf(_SC_OPEN_MAX);if(maxfd<0)maxfd=65536;
    pid_t pid=fork();if(!pid) {
        setsid();ioctl(slave,TIOCSCTTY,0);dup2(slave,0);dup2(slave,1);dup2(slave,2);
        for(int fd=3;fd<maxfd;fd++)close(fd);
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);signal(SIGPIPE,SIG_DFL);
        struct rlimit cpu={g->seconds,g->seconds+1},files={64,64},core={0,0};setrlimit(RLIMIT_CPU,&cpu);setrlimit(RLIMIT_NOFILE,&files);setrlimit(RLIMIT_CORE,&core);
        if(chdir(g->dir))_exit(126);execve(g->exe,argv,env);_exit(127);
    }
    close(slave);if(pid<0) { close(master);free(d);return NULL; }
    d->pid=pid;d->fd=master;d->until=now+(uint64_t)g->seconds*1000;
    if(fcntl(master,F_SETFL,O_NONBLOCK)) { door_stop(d);return NULL; }return d;
}
int door_room(Door *d) { return d->used<sizeof(d->input); }
int door_input(Door *d,unsigned char c) { if(d->after_cr) { d->after_cr=0;if(c=='\n' || c==0)return 1; }if(!door_room(d))return 0;d->after_cr=c=='\r';d->input[d->used++]=c;return 1; }
int door_pump(Door *d,int (*write_bytes)(void *,const unsigned char *,size_t),void *context,uint64_t now) {
    if(now>=d->until)return -1;
    if(d->used) { ssize_t n=write(d->fd,d->input,d->used);if(n>0) { memmove(d->input,d->input+n,d->used-(size_t)n);d->used-=(size_t)n; }else if(n<0 && errno!=EAGAIN && errno!=EINTR && errno!=EIO)return -1; }
    unsigned char buffer[4096];ssize_t n=read(d->fd,buffer,sizeof(buffer));
    if(n>0) { if(!write_bytes(context,buffer,(size_t)n))return -1;return 0; }
    if(n<0 && errno!=EAGAIN && errno!=EINTR && errno!=EIO)return -1;
    int status;pid_t p=waitpid(d->pid,&status,WNOHANG);if(p==d->pid || (p<0 && errno==ECHILD)) { d->exited=1;return 1; }return 0;
}
void door_stop(Door *d) {
    if(!d)return;kill(-d->pid,SIGKILL);if(!d->exited)kill(d->pid,SIGKILL);close(d->fd);
    if(!d->exited) { doors_reap();for(unsigned i=0;i<128;i++)if(!reaping[i]) { reaping[i]=d->pid;break; } }
    memset(d,0,sizeof(*d));free(d);
}
#else
int doors_config(const char *p) { return !p; }
void doors_list(unsigned l,char *s,size_t n) { (void)l;snprintf(s,n,"GAMES\r\nEND GAMES\r\n"); }
Door *door_start(unsigned i,unsigned l,uint32_t u,const char *s,unsigned c,unsigned r,uint64_t t) { (void)i;(void)l;(void)u;(void)s;(void)c;(void)r;(void)t;return NULL; }
int door_input(Door *d,unsigned char b) { (void)d;(void)b;return 0; }
int door_room(Door *d) { (void)d;return 0; }
int door_pump(Door *d,int (*w)(void *,const unsigned char *,size_t),void *c,uint64_t t) { (void)d;(void)w;(void)c;(void)t;return -1; }
void door_stop(Door *d) { (void)d; }
void doors_reap(void) {}
int doors_pending(void) { return 0; }
#endif

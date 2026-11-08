#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#ifdef __FreeBSD__
#define __BSD_VISIBLE 1
#endif
#ifdef __sun
#define __EXTENSIONS__ 1
#endif
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
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#ifdef __FreeBSD__
#include <libutil.h>
#elif !defined(__sun)
#include <pty.h>
#endif
/* Solaris 11.4 declares openpty/login_tty in <termios.h>, in libc. */
typedef struct { unsigned id,level,seconds,slots,drop;char exe[1024],dir[1024],title[161]; } Game;
static Game games[32];static unsigned game_count;
struct Door { pid_t pid;int fd,exited,after_cr,lock;char run[1200];uint64_t until,stop_until;unsigned char input[8192];size_t used;struct Door *next; };
static uint64_t door_clock(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000; }
/* Keep the slot until the leader AND its process group have gone. */
static Door *reaping;
static void door_clean(Door *d) {
    if(*d->run) { char path[1240];snprintf(path,sizeof(path),"%s/DORINFO1.DEF",d->run);unlink(path);rmdir(d->run); }
    if(d->lock>=0)close(d->lock);free(d);
}
int doors_pending(void) { return reaping!=NULL; }
void doors_reap(void) {
    Door **link=&reaping;
    while(*link) {
        Door *d=*link;int status;
        if(door_clock()>=d->stop_until) { kill(-d->pid,SIGKILL);if(!d->exited)kill(d->pid,SIGKILL); }
        if(!d->exited) { pid_t p=waitpid(d->pid,&status,WNOHANG);if(p==d->pid || (p<0 && errno==ECHILD))d->exited=1; }
        if(d->exited && kill(-d->pid,0)<0 && errno==ESRCH) { *link=d->next;door_clean(d); }
        else link=&d->next;
    }
}
/* Slot locks are shared by all runtimes using this same working directory.
 * Never unlink a lock inode: another process may already have opened it. */
static int door_slot(Door *d,const Game *g,unsigned *node) {
    for(unsigned n=1;n<=g->slots;n++) {
        char path[1100];struct stat st;snprintf(path,sizeof(path),"%s/.liner-door-slot-%u",g->dir,n);
        int fd=open(path,O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(fd<0)return 0;
        if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&077) || st.st_nlink!=1) { close(fd);errno=EPERM;return 0; }
        if(!flock(fd,LOCK_EX|LOCK_NB)) { d->lock=fd;*node=n;return 1; }
        int e=errno;close(fd);if(e!=EWOULDBLOCK && e!=EAGAIN)return 0;
    }
    errno=EBUSY;return 0;
}
static int door_drop(Door *d,const Game *g,uint32_t user,unsigned level) {
    snprintf(d->run,sizeof(d->run),"%s/.liner-door-run-XXXXXX",g->dir);
    if(!mkdtemp(d->run)) { d->run[0]=0;return 0; }
    char path[1240];snprintf(path,sizeof(path),"%s/DORINFO1.DEF",d->run);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return 0;
    FILE *f=fdopen(fd,"w");if(!f) { close(fd);return 0; }
    /* ASCII permanent identity, not a truncated handle or private real name.
     * COM0 selects local stdio for native OpenDoors; no modem is emulated. */
    int ok=fprintf(f,"Liner notes\r\nSYSOP\r\n\r\nCOM0\r\n0 BAUD,N,8,1\r\n0\r\nLN%09u\r\n\r\n\r\n1\r\n%u\r\n%u\r\n",user,level,(g->seconds+59)/60)>0;
    if(fclose(f))ok=0;return ok;
}
int doors_config(const char *path) {
    game_count=0;if(!path)return 1;FILE *f=fopen(path,"r");if(!f)return 0;
    char line[4096];int ok=1;
    while(fgets(line,sizeof(line),f)) {
        size_t n=strlen(line);if(n && line[n-1]=='\n')line[--n]=0;else if(!feof(f)) { ok=0;break; }
        if(n && line[n-1]=='\r')line[--n]=0;if(!*line || *line=='#')continue;
        if(game_count==32) { ok=0;break; }Game *g=&games[game_count];memset(g,0,sizeof(*g));char *options=NULL;char *parts[6],*p=line;unsigned i;
        for(i=0;i<6;i++) { parts[i]=p;char *bar=strchr(p,'|');if(i<5) { if(!bar)break;*bar=0;p=bar+1; }else if(bar) { *bar=0;options=bar+1; } }
        if(i!=6) { ok=0;break; }unsigned *numbers[]={&g->id,&g->level,&g->seconds};
        for(i=0;i<3;i++) { char *end;unsigned long v=strtoul(parts[i],&end,10);if(!*parts[i] || *end || *parts[i]<'0' || *parts[i]>'9' || v>86400)break;*numbers[i]=(unsigned)v; }
        if(i!=3 || !g->id || g->level>5 || !g->seconds || parts[3][0]!='/' || parts[4][0]!='/' || strlen(parts[3])>=sizeof(g->exe) || strlen(parts[4])>=sizeof(g->dir) || !*parts[5] || strlen(parts[5])>=sizeof(g->title)) { ok=0;break; }
        /* Optional seventh field: none:N or dorinfo1:N, N=1..32. */
        if(options) {
            const char *value=NULL;if(!strncmp(options,"dorinfo1:",9)) { g->drop=1;value=options+9; }
            else if(!strncmp(options,"none:",5))value=options+5;
            char *end=NULL;unsigned long slots=value?strtoul(value,&end,10):0;
            if(!value || *value<'1' || *value>'9' || *end || slots<1 || slots>32) { ok=0;break; }g->slots=(unsigned)slots;
        }
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
    errno=0;if(!user || geteuid()==0)return NULL;doors_reap();errno=0;Game *g=NULL;
    for(unsigned i=0;i<game_count;i++)if(games[i].id==id && level>=games[i].level)g=&games[i];if(!g)return NULL;
    Door *d=calloc(1,sizeof(*d));if(!d)return NULL;d->lock=-1;d->fd=-1;
    unsigned node=0;
    if((g->slots && !door_slot(d,g,&node)) || (g->drop && !door_drop(d,g,user,level))) { int saved=errno;door_clean(d);errno=saved;return NULL; }
    int master,slave;struct winsize ws={0};ws.ws_col=(unsigned short)columns;ws.ws_row=(unsigned short)rows;
    if(openpty(&master,&slave,NULL,NULL,&ws)) { door_clean(d);return NULL; }
    char idenv[64],nameenv[96],cols[32],lines[32],home[1100],nodeenv[48],dropenv[1240],gameenv[48];
    snprintf(idenv,sizeof(idenv),"LINER_USER_ID=%u",user);snprintf(nameenv,sizeof(nameenv),"LINER_USER=%s",name);
    snprintf(cols,sizeof(cols),"COLUMNS=%u",columns);snprintf(lines,sizeof(lines),"LINES=%u",rows);snprintf(home,sizeof(home),"HOME=%s",g->dir);
    snprintf(nodeenv,sizeof(nodeenv),"LINER_DOOR_NODE=%u",node);snprintf(dropenv,sizeof(dropenv),"LINER_DOOR_DROP_DIR=%s",d->run);snprintf(gameenv,sizeof(gameenv),"LINER_DOOR_ID=%u",id);
    char *env[]={"PATH=/usr/local/bin:/usr/bin:/bin","TERM=xterm","LANG=C.UTF-8",idenv,nameenv,cols,lines,home,nodeenv,dropenv,gameenv,NULL};char *argv[]={g->exe,NULL};
    long maxfd=sysconf(_SC_OPEN_MAX);if(maxfd<0)maxfd=65536;
    pid_t pid=fork();if(!pid) {
#ifdef __sun
        /* login_tty establishes the controlling STREAMS terminal and stdio. */
        if(login_tty(slave))_exit(126);
#else
        setsid();ioctl(slave,TIOCSCTTY,0);dup2(slave,0);dup2(slave,1);dup2(slave,2);
#endif
        /* Inherit only the slot lock, so a surviving child still owns its slot. */
        for(int fd=3;fd<maxfd;fd++)if(fd!=d->lock)close(fd);
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);signal(SIGPIPE,SIG_DFL);
        struct rlimit cpu={g->seconds,g->seconds+1},files={64,64},core={0,0};setrlimit(RLIMIT_CPU,&cpu);setrlimit(RLIMIT_NOFILE,&files);setrlimit(RLIMIT_CORE,&core);
        if(chdir(g->dir))_exit(126);execve(g->exe,argv,env);_exit(127);
    }
    close(slave);if(pid<0) { close(master);door_clean(d);return NULL; }
    d->pid=pid;d->fd=master;d->until=now+(uint64_t)g->seconds*1000;
    if(fcntl(master,F_SETFL,O_NONBLOCK)) { door_stop(d);return NULL; }return d;
}
int door_room(Door *d) { return d->used<sizeof(d->input); }
int door_resize(Door *d,unsigned columns,unsigned rows) {
    /* Match session_dimensions(); do not introduce a game-specific limit. */
    if(!d || columns<20 || columns>240 || rows<5 || rows>100)return 0;
    struct winsize current={0};
    if(ioctl(d->fd,TIOCGWINSZ,&current))return 0;
    if(current.ws_col==columns && current.ws_row==rows)return 1;
    current.ws_col=(unsigned short)columns;current.ws_row=(unsigned short)rows;
    return ioctl(d->fd,TIOCSWINSZ,&current)==0;
}
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
    if(!d)return;
    /* Let terminal-aware games save/clear their own semaphores on hangup.
     * The main event loop never blocks; ignored hangups get a bounded grace. */
    d->stop_until=door_clock()+250;kill(-d->pid,SIGHUP);if(!d->exited)kill(d->pid,SIGHUP);close(d->fd);d->fd=-1;
    d->next=reaping;reaping=d;doors_reap();

}
#else
int doors_config(const char *p) { return !p; }
void doors_list(unsigned l,char *s,size_t n) { (void)l;snprintf(s,n,"GAMES\r\nEND GAMES\r\n"); }
Door *door_start(unsigned i,unsigned l,uint32_t u,const char *s,unsigned c,unsigned r,uint64_t t) { (void)i;(void)l;(void)u;(void)s;(void)c;(void)r;(void)t;return NULL; }
int door_input(Door *d,unsigned char b) { (void)d;(void)b;return 0; }
int door_room(Door *d) { (void)d;return 0; }
int door_resize(Door *d,unsigned c,unsigned r) { (void)d;(void)c;(void)r;return 0; }
int door_pump(Door *d,int (*w)(void *,const unsigned char *,size_t),void *c,uint64_t t) { (void)d;(void)w;(void)c;(void)t;return -1; }
void door_stop(Door *d) { (void)d; }
void doors_reap(void) {}
int doors_pending(void) { return 0; }
#endif

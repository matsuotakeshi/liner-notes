#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#ifdef __FreeBSD__
#define __BSD_VISIBLE 1
#endif
#ifdef __sun
#define __EXTENSIONS__ 1
#endif
#include "doors.h"
#include "door_lock.h"
#include "door_core.h"
#include "door_config.h"
#include "door_protocol.h"
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
static DoorCatalog catalog;
struct Door { pid_t pid;int fd,exited,after_cr,lock;char run[1200];uint64_t until,stop_until;unsigned char input[8192];size_t used;struct Door *next; };
static uint64_t door_clock(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000; }
/* Keep the slot until the leader AND its process group have gone. */
static Door *reaping;
static void door_clean(Door *d) {
    if(*d->run) { char path[1240];snprintf(path,sizeof(path),"%s/DORINFO1.DEF",d->run);unlink(path);rmdir(d->run); }
    if(d->lock>=0)door_lock_release(d->lock);
    free(d);
}
int doors_pending(void) { return reaping!=NULL; }
void doors_reap(void) {
    door_lock_reap();
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
static int door_slot(Door *d,const DoorSpec *g,unsigned *node) {
    for(unsigned n=1;n<=g->slots;n++) {
        char path[1100];struct stat st;snprintf(path,sizeof(path),"%s/.liner-door-slot-%u",g->dir,n);
        int fd=open(path,O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(fd<0)return 0;
        if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode&077) || st.st_nlink!=1) { close(fd);errno=EPERM;return 0; }
        int lease=door_lock_acquire(fd);
        if(lease>=0) { if(lease!=fd)close(fd);d->lock=lease;*node=n;return 1; }
        int e=errno;close(fd);if(e!=EWOULDBLOCK && e!=EAGAIN)return 0;
    }
    errno=EBUSY;return 0;
}
static int door_drop(Door *d,const DoorSpec *g,uint32_t user,unsigned level) {
    snprintf(d->run,sizeof(d->run),"%s/.liner-door-run-XXXXXX",g->dir);
    if(!mkdtemp(d->run)) { d->run[0]=0;return 0; }
    char path[1240];snprintf(path,sizeof(path),"%s/DORINFO1.DEF",d->run);
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return 0;
    FILE *f=fdopen(fd,"w");if(!f) { close(fd);return 0; }
    /* ASCII permanent identity, not a truncated handle or private real name.
     * COM0 selects local stdio for native OpenDoors; no modem is emulated. */
    int ok=fprintf(f,"Liner notes\r\nSYSOP\r\n\r\nCOM0\r\n0 BAUD,N,8,1\r\n0\r\nLN%09u\r\n\r\n\r\n1\r\n%u\r\n%u\r\n",user,level,(g->seconds+59)/60)>0;
    if(fclose(f))ok=0;
    return ok;
}
static int door_paths_valid(void *context,const DoorSpec *game) {
    struct stat st;(void)context;
    return !stat(game->exe,&st) && S_ISREG(st.st_mode) && !access(game->exe,X_OK)
        && !stat(game->dir,&st) && S_ISDIR(st.st_mode);
}
int doors_config(const char *path) {
    return door_config_load(&catalog,path,door_paths_valid,NULL);
}
void doors_list(unsigned level,char *out,size_t size) {
    DoorListing listing;door_core_list(&catalog,level,&listing);
    door_protocol_list(&listing,out,size);
}
Door *door_start(unsigned id,unsigned level,uint32_t user,const char *name,unsigned columns,unsigned rows,uint64_t now) {
    errno=0;if(!user || geteuid()==0)return NULL;doors_reap();errno=0;
    const DoorSpec *g=door_core_launch(&catalog,id,level,user,geteuid()==0);
    if(!g)return NULL;
    Door *d=calloc(1,sizeof(*d));if(!d)return NULL;d->lock=-1;d->fd=-1;
    unsigned node=0;
    if((g->slots && !door_slot(d,g,&node)) || (g->drop && !door_drop(d,g,user,level))) { int saved=errno;door_clean(d);errno=saved;return NULL; }
    int master,slave;struct winsize ws={0};ws.ws_col=(unsigned short)columns;ws.ws_row=(unsigned short)rows;
    if(openpty(&master,&slave,NULL,NULL,&ws)) { door_clean(d);return NULL; }
    char idenv[64],nameenv[96],cols[32],lines[32],home[1100],nodeenv[48],dropenv[1240],gameenv[48];
    snprintf(idenv,sizeof(idenv),"LINER_USER_ID=%u",user);snprintf(nameenv,sizeof(nameenv),"LINER_USER=%s",name);
    snprintf(cols,sizeof(cols),"COLUMNS=%u",columns);snprintf(lines,sizeof(lines),"LINES=%u",rows);snprintf(home,sizeof(home),"HOME=%s",g->dir);
    snprintf(nodeenv,sizeof(nodeenv),"LINER_DOOR_NODE=%u",node);snprintf(dropenv,sizeof(dropenv),"LINER_DOOR_DROP_DIR=%s",d->run);snprintf(gameenv,sizeof(gameenv),"LINER_DOOR_ID=%u",id);
    char *env[]={"PATH=/usr/local/bin:/usr/bin:/bin","TERM=xterm","LANG=C.UTF-8",idenv,nameenv,cols,lines,home,nodeenv,dropenv,gameenv,NULL};char *argv[]={(char *)g->exe,NULL};
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
        if(chdir(g->dir))_exit(126);
        execve(g->exe,argv,env);
        _exit(127);
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

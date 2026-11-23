/* Optional local isolation service. All launches use it when configured. */
#if defined(__linux__) || defined(__FreeBSD__)
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <openssl/rand.h>
#include "door_handoff.inc"
static char isolation_socket[100];
int doors_isolation_config(const char *path) {
    if(!path || path[0]!='/' || strlen(path)>=sizeof(isolation_socket))return 0;
    strcpy(isolation_socket,path);return 1;
}
static int isolation_enabled(void) { return *isolation_socket!=0; }
static Door *isolated_start(const DoorSpec *g,unsigned level,uint32_t user,const char *name,unsigned columns,unsigned rows,uint64_t now,unsigned long bbs_session,uint32_t epoch) {
    if(!bbs_session || strlen(name)>32) { door_launch_diagnostic("isolation-request",0);return NULL; }
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0) { door_launch_diagnostic("isolation-socket",errno);return NULL; }
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;strcpy(address.sun_path,isolation_socket);
    struct timeval limit={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&limit,sizeof(limit));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&limit,sizeof(limit));
    if(connect(fd,(struct sockaddr *)&address,sizeof(address))) { door_launch_diagnostic("isolation-connect",errno);close(fd);return NULL; }
#ifdef __FreeBSD__
    uid_t uid;gid_t gid;
    if(getpeereid(fd,&uid,&gid)) { door_launch_diagnostic("isolation-peer-query",errno);close(fd);return NULL; }
    if(uid!=0) { door_launch_diagnostic("isolation-peer",0);close(fd);return NULL; }
#else
    struct { pid_t pid;uid_t uid;gid_t gid; } peer; socklen_t length=sizeof(peer);
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length)) { door_launch_diagnostic("isolation-peer-query",errno);close(fd);return NULL; }
    if(length!=sizeof(peer) || peer.uid!=0) { door_launch_diagnostic("isolation-peer",0);close(fd);return NULL; }
#endif
    unsigned char random[16];if(RAND_bytes(random,sizeof(random))!=1) { door_launch_diagnostic("isolation-random",0);close(fd);return NULL; }
    char session[64],hexname[65];
    for(unsigned i=0;i<16;i++)snprintf(session+2*i,3,"%02x",random[i]);
    snprintf(session+32,sizeof(session)-32,".%lu",bbs_session);
    for(size_t i=0;i<strlen(name);i++) { snprintf(hexname+2*i,3,"%02x",(unsigned char)name[i]); }hexname[2*strlen(name)]=0;
    char request[512];int n=snprintf(request,sizeof(request),"{\"door\":%u,\"user\":%u,\"level\":%u,\"columns\":%u,\"rows\":%u,\"namehex\":\"%s\",\"session\":\"%s\",\"generation\":%u}\n",g->id,user,level,columns,rows,hexname,session,epoch);
    if(n<0 || n>=(int)sizeof(request)) { door_launch_diagnostic("isolation-encode",0);close(fd);return NULL; }
    size_t sent=0;
    while(sent<(size_t)n) { ssize_t count=send(fd,request+sent,(size_t)n-sent,MSG_NOSIGNAL);if(count<0 && errno==EINTR)continue;if(count<=0) { door_launch_diagnostic("isolation-send",errno);close(fd);return NULL; }sent+=(size_t)count; }
    DoorHandoff handoff;
    int master=door_receive_pty(fd,&handoff);
    door_reply_diagnostic(handoff.reply,(long)handoff.received,handoff.count,handoff.flags,handoff.error);
    if(master<0) {
        door_launch_diagnostic(handoff.failure,handoff.error);
        close(fd);return NULL;
    }
    Door *d=calloc(1,sizeof(*d));if(!d) { door_launch_diagnostic("isolation-allocation",errno);close(master);close(fd);return NULL; }
    d->fd=master;d->remote=fd;d->lock=-1;d->until=now+(uint64_t)g->seconds*1000;
    if(fcntl(master,F_SETFL,O_NONBLOCK) || fcntl(fd,F_SETFL,O_NONBLOCK)) { door_launch_diagnostic("isolation-nonblocking",errno);close(master);close(fd);free(d);return NULL; }
    return d;
}
#else
int doors_isolation_config(const char *path) { (void)path;return 0; }
static int isolation_enabled(void) { return 0; }
static Door *isolated_start(const DoorSpec *g,unsigned l,uint32_t u,const char *n,unsigned c,unsigned r,uint64_t t,unsigned long s,uint32_t epoch) { (void)s;(void)epoch;(void)g;(void)l;(void)u;(void)n;(void)c;(void)r;(void)t;return NULL; }
#endif
static int isolated_done(Door *d) {
#if defined(__linux__) || defined(__FreeBSD__)
    char state[32];ssize_t n=recv(d->remote,state,sizeof(state),MSG_DONTWAIT);
    return n>=0 || (errno!=EAGAIN && errno!=EINTR);
#else
    (void)d;return 1;
#endif
}

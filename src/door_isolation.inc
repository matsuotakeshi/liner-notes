/* Optional local isolation service. All launches use it when configured. */
#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <openssl/rand.h>
static char isolation_socket[100];
int doors_isolation_config(const char *path) {
    if(!path || path[0]!='/' || strlen(path)>=sizeof(isolation_socket))return 0;
    strcpy(isolation_socket,path);return 1;
}
static int isolation_enabled(void) { return *isolation_socket!=0; }
static Door *isolated_start(const DoorSpec *g,unsigned level,uint32_t user,const char *name,unsigned columns,unsigned rows,uint64_t now,unsigned long bbs_session,uint32_t epoch) {
    if(!bbs_session || strlen(name)>32)return NULL;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return NULL;
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;strcpy(address.sun_path,isolation_socket);
    struct timeval limit={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&limit,sizeof(limit));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&limit,sizeof(limit));
    if(connect(fd,(struct sockaddr *)&address,sizeof(address))) { close(fd);return NULL; }
    struct { pid_t pid;uid_t uid;gid_t gid; } peer; socklen_t length=sizeof(peer);
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length) || length!=sizeof(peer) || peer.uid!=0) { close(fd);return NULL; }
    unsigned char random[16];if(RAND_bytes(random,sizeof(random))!=1) { close(fd);return NULL; }
    char session[64],hexname[65];
    for(unsigned i=0;i<16;i++)snprintf(session+2*i,3,"%02x",random[i]);
    snprintf(session+32,sizeof(session)-32,".%lu",bbs_session);
    for(size_t i=0;i<strlen(name);i++) { snprintf(hexname+2*i,3,"%02x",(unsigned char)name[i]); }hexname[2*strlen(name)]=0;
    char request[512];int n=snprintf(request,sizeof(request),"{\"door\":%u,\"user\":%u,\"level\":%u,\"columns\":%u,\"rows\":%u,\"namehex\":\"%s\",\"session\":\"%s\",\"generation\":%u}\n",g->id,user,level,columns,rows,hexname,session,epoch);
    if(n<0 || n>=(int)sizeof(request)) { close(fd);return NULL; }
    size_t sent=0;
    while(sent<(size_t)n) { ssize_t count=send(fd,request+sent,(size_t)n-sent,MSG_NOSIGNAL);if(count<0 && errno==EINTR)continue;if(count<=0) { close(fd);return NULL; }sent+=(size_t)count; }
    char reply[3];struct iovec data={reply,sizeof(reply)};
    union { struct cmsghdr align;char bytes[CMSG_SPACE(sizeof(int)*4)]; } control;
    memset(&control,0,sizeof(control));struct msghdr message={0};message.msg_iov=&data;message.msg_iovlen=1;message.msg_control=control.bytes;message.msg_controllen=sizeof(control.bytes);
    ssize_t received=recvmsg(fd,&message,MSG_CMSG_CLOEXEC|MSG_WAITALL);int master=-1,count=0;
    for(struct cmsghdr *c=CMSG_FIRSTHDR(&message);c;c=CMSG_NXTHDR(&message,c)) {
        if(c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS && c->cmsg_len>=CMSG_LEN(0)) {
            size_t size=c->cmsg_len-CMSG_LEN(0);int *fds=(int *)CMSG_DATA(c);
            for(size_t i=0;i<size/sizeof(int);i++) { if(count++==0)master=fds[i];else close(fds[i]); }
        }
    }
    if(received!=3 || memcmp(reply,"OK\n",3) || count!=1 || !isatty(master) || (message.msg_flags&(MSG_CTRUNC|MSG_TRUNC))) { if(master>=0)close(master);close(fd);return NULL; }
    Door *d=calloc(1,sizeof(*d));if(!d) { close(master);close(fd);return NULL; }
    d->fd=master;d->remote=fd;d->lock=-1;d->until=now+(uint64_t)g->seconds*1000;
    if(fcntl(master,F_SETFL,O_NONBLOCK) || fcntl(fd,F_SETFL,O_NONBLOCK)) { close(master);close(fd);free(d);return NULL; }
    return d;
}
#else
int doors_isolation_config(const char *path) { (void)path;return 0; }
static int isolation_enabled(void) { return 0; }
static Door *isolated_start(const DoorSpec *g,unsigned l,uint32_t u,const char *n,unsigned c,unsigned r,uint64_t t,unsigned long s,uint32_t epoch) { (void)s;(void)epoch;(void)g;(void)l;(void)u;(void)n;(void)c;(void)r;(void)t;return NULL; }
#endif
static int isolated_done(Door *d) {
#ifdef __linux__
    char state[32];ssize_t n=recv(d->remote,state,sizeof(state),MSG_DONTWAIT);
    return n>=0 || (errno!=EAGAIN && errno!=EINTR);
#else
    (void)d;return 1;
#endif
}

/* Shared client transport for the isolation launcher. Including this file in
 * a native test does not enable the Solaris runtime isolation backend.
 */
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <openssl/rand.h>
#ifdef __sun
#include <ucred.h>
#endif
#include "door_diagnostics.h"
#include "door_handoff.inc"

typedef struct {
    unsigned door, level, columns, rows;
    uint32_t user, epoch;
    unsigned long session;
    const char *name;
} DoorServiceRequest;

static int door_peer_uid(int fd, uid_t *uid) {
#ifdef __FreeBSD__
    gid_t gid;
    return getpeereid(fd,uid,&gid);
#elif defined(__sun)
    ucred_t *credentials=NULL;
    if(getpeerucred(fd,&credentials))return -1;
    if(!credentials) { errno=EIO;return -1; }
    *uid=ucred_geteuid(credentials);
    ucred_free(credentials);
    if(*uid==(uid_t)-1) { errno=EIO;return -1; }
    return 0;
#else
    struct { pid_t pid;uid_t uid;gid_t gid; } peer;
    socklen_t length=sizeof(peer);
    if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length))return -1;
    if(length!=sizeof(peer)) { errno=EIO;return -1; }
    *uid=peer.uid;
    return 0;
#endif
}

static int door_service_failed(int fd, const char *phase, int problem) {
    door_launch_diagnostic(phase,problem);
    if(fd>=0)close(fd);
    errno=problem;
    return -1;
}

static int door_service_deadlines(int fd) {
    /* The supervisor validates and syncs its durable reservation before reply. */
    const struct timeval request={3,0}, reply={10,0};
    if(setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&request,sizeof(request)))return -1;
    return setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&reply,sizeof(reply));
}

/* Success returns the PTY and transfers the still-open service socket.
 * Failure closes both; identifying the service precedes writing BBS fields.
 */
static int door_service_start(const char *path,const DoorServiceRequest *request,int *service) {
    *service=-1;
    struct sockaddr_un address={0};
    if(path[0]!='/' || strlen(path)>=sizeof(address.sun_path) ||
       !request->session || strlen(request->name)>32)
        return door_service_failed(-1,"isolation-request",0);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(fd<0)return door_service_failed(-1,"isolation-socket",errno);
    address.sun_family=AF_UNIX;
    strcpy(address.sun_path,path);
    if(door_service_deadlines(fd))return door_service_failed(fd,"isolation-timeout",errno);
    if(connect(fd,(struct sockaddr *)&address,sizeof(address)))
        return door_service_failed(fd,"isolation-connect",errno);
    uid_t uid;
    if(door_peer_uid(fd,&uid))return door_service_failed(fd,"isolation-peer-query",errno);
    if(uid!=0)return door_service_failed(fd,"isolation-peer",EACCES);

    unsigned char random[16];
    if(RAND_bytes(random,sizeof(random))!=1)return door_service_failed(fd,"isolation-random",0);
    char session[64],hexname[65];
    for(unsigned i=0;i<16;i++)snprintf(session+2*i,3,"%02x",random[i]);
    snprintf(session+32,sizeof(session)-32,".%lu",request->session);
    size_t namesize=strlen(request->name);
    for(size_t i=0;i<namesize;i++)snprintf(hexname+2*i,3,"%02x",(unsigned char)request->name[i]);
    hexname[2*namesize]=0;
    char data[512];
    int size=snprintf(data,sizeof(data),"{\"door\":%u,\"user\":%u,\"level\":%u,\"columns\":%u,\"rows\":%u,\"namehex\":\"%s\",\"session\":\"%s\",\"generation\":%u}\n",
                      request->door,request->user,request->level,request->columns,request->rows,
                      hexname,session,request->epoch);
    if(size<0 || size>=(int)sizeof(data))return door_service_failed(fd,"isolation-encode",0);
    size_t sent=0;
    while(sent<(size_t)size) {
        ssize_t count=send(fd,data+sent,(size_t)size-sent,MSG_NOSIGNAL);
        if(count<0 && errno==EINTR)continue;
        if(count<=0)return door_service_failed(fd,"isolation-send",errno);
        sent+=(size_t)count;
    }
    DoorHandoff handoff;
    int master=door_receive_pty(fd,&handoff);
    door_reply_diagnostic(handoff.reply,(long)handoff.received,handoff.count,handoff.flags,handoff.error);
    if(master<0)return door_service_failed(fd,handoff.failure,handoff.error);
    *service=fd;
    return master;
}

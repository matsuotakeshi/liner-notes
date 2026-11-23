/* Optional local isolation service. All launches use it when configured. */
#if defined(__linux__) || defined(__FreeBSD__) || (defined(__sun) && defined(LINER_TEST_SOLARIS_DOOR))
#define LINER_DOOR_ISOLATION_CLIENT 1
#include "door_service_client.inc"
static char isolation_socket[100];
int doors_isolation_config(const char *path) {
    if(!path || path[0]!='/' || strlen(path)>=sizeof(isolation_socket))return 0;
    strcpy(isolation_socket,path);return 1;
}
static int isolation_enabled(void) { return *isolation_socket!=0; }
static Door *isolated_start(const DoorSpec *g,unsigned level,uint32_t user,const char *name,unsigned columns,unsigned rows,uint64_t now,unsigned long bbs_session,uint32_t epoch) {
    DoorServiceRequest request={
        .door=g->id,.level=level,.columns=columns,.rows=rows,.user=user,
        .epoch=epoch,.session=bbs_session,.name=name
    };
    int fd;
    int master=door_service_start(isolation_socket,&request,&fd);
    if(master<0)return NULL;
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
#ifdef LINER_DOOR_ISOLATION_CLIENT
    char state[32];ssize_t n=recv(d->remote,state,sizeof(state),MSG_DONTWAIT);
    return n>=0 || (errno!=EAGAIN && errno!=EINTR);
#else
    (void)d;return 1;
#endif
}
#undef LINER_DOOR_ISOLATION_CLIENT

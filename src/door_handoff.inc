/* Receive the isolation supervisor's fixed reply and its single PTY master.
 * Preserve receive diagnostics and close every installed right on rejection.
 */
typedef struct {
    char reply[3];
    ssize_t received;
    int count, flags, error;
    const char *failure;
} DoorHandoff;

static int door_receive_pty(int socket_fd, DoorHandoff *report) {
    *report=(DoorHandoff){0};
    struct iovec data={report->reply,sizeof(report->reply)};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int)*4)]; } control={0};
    struct msghdr message={0};
    message.msg_iov=&data;
    message.msg_iovlen=1;
    message.msg_control=control.bytes;
    message.msg_controllen=sizeof(control.bytes);
    report->received=recvmsg(socket_fd,&message,MSG_CMSG_CLOEXEC|MSG_WAITALL);
    report->error=report->received<0?errno:0;
    report->flags=message.msg_flags&(MSG_CTRUNC|MSG_TRUNC);

    int master=-1;
    if(report->received>=0) {
        for(struct cmsghdr *header=CMSG_FIRSTHDR(&message);header;
            header=CMSG_NXTHDR(&message,header)) {
            if(header->cmsg_level!=SOL_SOCKET || header->cmsg_type!=SCM_RIGHTS ||
               header->cmsg_len<CMSG_LEN(0))continue;
            size_t count=(header->cmsg_len-CMSG_LEN(0))/sizeof(int);
            for(size_t i=0;i<count;i++) {
                int right;
                memcpy(&right,CMSG_DATA(header)+i*sizeof(right),sizeof(right));
                if(report->count++==0)master=right;
                else close(right);
            }
        }
    }
    if(report->received!=(ssize_t)sizeof(report->reply))report->failure="isolation-receive";
    else if(memcmp(report->reply,"OK\n",3))report->failure="isolation-rejected";
    else if(report->count!=1)report->failure="isolation-fd-count";
    else if(report->flags)report->failure="isolation-truncated";
    else if(!isatty(master))report->failure="isolation-pty";
    if(report->failure && master>=0)close(master);
    return report->failure?-1:master;
}

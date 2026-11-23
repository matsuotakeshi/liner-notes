#define _DEFAULT_SOURCE 1
#define __EXTENSIONS__ 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#if defined(__FreeBSD__)
#include <libutil.h>
#elif !defined(__sun)
#include <pty.h>
#endif
#include "../src/door_handoff.inc"

static void require(int ok, const char *label) {
    if(!ok) {
        fprintf(stderr,"PTY handoff probe: %s (errno=%d)\n",label,errno);
        exit(1);
    }
}

static unsigned open_count(void) {
    unsigned count=0;
    for(int i=0;i<256;i++)if(fcntl(i,F_GETFD)>=0)count++;
    return count;
}

static void exchange(const char *reply, int right, unsigned rights, const char *failure,
                     int slave) {
    int channel[2];
    require(socketpair(AF_UNIX,SOCK_STREAM,0,channel)==0,"socketpair");
    require(rights<=6,"fixture right count");
    struct iovec bytes={(void *)reply,strlen(reply)};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(6*sizeof(int))]; } control={0};
    struct msghdr msg={0};
    msg.msg_iov=&bytes;
    msg.msg_iovlen=1;
    if(rights) {
        msg.msg_control=control.bytes;
        msg.msg_controllen=CMSG_SPACE(rights*sizeof(int));
        struct cmsghdr *header=CMSG_FIRSTHDR(&msg);
        header->cmsg_level=SOL_SOCKET;
        header->cmsg_type=SCM_RIGHTS;
        header->cmsg_len=CMSG_LEN(rights*sizeof(int));
        for(unsigned i=0;i<rights;i++)
            memcpy(CMSG_DATA(header)+i*sizeof(int),&right,sizeof(right));
    }
    require(sendmsg(channel[0],&msg,0)==(ssize_t)bytes.iov_len,"sendmsg");
    require(shutdown(channel[0],SHUT_WR)==0,"sender shutdown");
    unsigned before=open_count();
    DoorHandoff report;
    int received=door_receive_pty(channel[1],&report);
    if(failure) {
        if(received>=0 || !report.failure || strcmp(report.failure,failure))
            fprintf(stderr,"expected=%s actual=%s received=%ld fds=%d flags=%d\n",
                    failure,report.failure?report.failure:"success",(long)report.received,
                    report.count,report.flags);
        require(received<0 && report.failure && !strcmp(report.failure,failure),
                "reply/descriptor rejection");
    } else {
        require(received>=0 && !report.failure,"receiving the PTY master");
        int flags=fcntl(received,F_GETFD);
        require(flags>=0 && (flags&FD_CLOEXEC),"atomic close-on-exec");
        struct winsize size={0};
        size.ws_col=100;size.ws_row=30;
        require(ioctl(received,TIOCSWINSZ,&size)==0,"resize received PTY");
        require(ioctl(slave,TIOCGWINSZ,&size)==0 && size.ws_col==100 && size.ws_row==30,
                "slave observes resize");
        require(write(slave,"x",1)==1,"slave output");
        struct pollfd ready={received,POLLIN,0};
        require(poll(&ready,1,2000)>0,"master output deadline");
        char byte=0;
        require(read(received,&byte,1)==1 && byte=='x',"master receives output");
        close(received);
    }
    require(open_count()==before,"no received descriptor leak");
    close(channel[0]);close(channel[1]);
}

int main(void) {
    int master, slave;
    require(openpty(&master,&slave,NULL,NULL,NULL)==0,"openpty");
    int plain=open("/dev/null",O_RDONLY);
    require(plain>=0,"open non-terminal control");
    exchange("OK\n",master,1,NULL,slave);
    exchange("OK\n",master,0,"isolation-fd-count",slave);
    exchange("OK\n",master,2,"isolation-fd-count",slave);
    exchange("BAD",master,1,"isolation-rejected",slave);
    exchange("OK",master,1,"isolation-receive",slave);
    exchange("OK\n",plain,1,"isolation-pty",slave);
    exchange("OK\n",master,6,"isolation-fd-count",slave);
    close(plain);close(slave);close(master);
    puts("PASS C PTY handoff: atomic CLOEXEC, resize, output, invalid replies and descriptor cleanup");
    return 0;
}

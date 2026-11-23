#define _DEFAULT_SOURCE 1
#define __EXTENSIONS__ 1
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "../src/door_service_client.inc"

static int expect_line(int fd,const char *expected) {
    for(size_t i=0;expected[i];i++) {
        char received;
        if(read(fd,&received,1)!=1 || received!=expected[i])return 0;
    }
    return 1;
}

int main(int argc,char **argv) {
    if(argc!=3 || (strcmp(argv[2],"auth") && strcmp(argv[2],"deny")))return 1;
    alarm(8);
    DoorServiceRequest request={
        .door=3,.level=2,.columns=80,.rows=24,.user=7,.epoch=9,.session=17,.name="fixture"
    };
    int service;
    int master=door_service_start(argv[1],&request,&service);
    if(!strcmp(argv[2],"deny")) {
        if(master>=0 || service>=0 || errno!=EACCES)return 2;
        puts("DENIED non-root launcher");
        return 0;
    }
    if(master<0 || service<0)return 3;
    int pty_flags=fcntl(master,F_GETFD), service_flags=fcntl(service,F_GETFD);
    if(pty_flags<0 || !(pty_flags&FD_CLOEXEC) || service_flags<0 || !(service_flags&FD_CLOEXEC))return 4;
    struct winsize size={0};
    if(ioctl(master,TIOCGWINSZ,&size) || size.ws_col!=80 || size.ws_row!=24)return 5;
    size.ws_col=100;size.ws_row=30;
    if(ioctl(master,TIOCSWINSZ,&size) || write(master,"size\n",5)!=5)return 6;
    if(!expect_line(master,"SIZE 100 30\r\n"))return 7;
    if(write(master,"check\n",6)!=6 || !expect_line(master,"AUTH LIVE\r\n"))return 8;
    puts("AUTH LIVE");
    fflush(stdout);
    if(getchar()!='R' || write(master,"check\n",6)!=6 ||
       !expect_line(master,"AUTH REVOKED\r\n") || write(master,"quit\n",5)!=5)return 8;
    if(shutdown(service,SHUT_WR) || !expect_line(service,"DONE\n"))return 9;
    close(master);
    close(service);
    puts("PASS root launcher request, PTY resize and Broker revocation");
    return 0;
}

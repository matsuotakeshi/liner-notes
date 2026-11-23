/* Fixed Solaris fixture for joining the PTY, nosuid root and ticket channel.
 * It never executes a supplied program or logs credential bytes.
 */
#define main jail_fixture_main
#include "solaris_door_jail_probe.c"
#undef main
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>

enum { TICKET_FD = 3, CONSUMER_FD = 4, TICKET_BYTES = 65 };

static int synthetic_door(void) {
    alarm(10);
    if(getuid()!=SLOT || geteuid()!=SLOT || getgid()!=SLOT || getegid()!=SLOT ||
       getgroups(0,NULL)!=0 || !basic_limit(0) || !isatty(0) ||
       tcgetpgrp(0)!=getpgrp())return 90;

    struct stat endpoint;
    if(lstat("/system/contract/process/template",&endpoint)==0 || errno!=ENOENT)return 91;
    if(lstat("/proc/self/cred",&endpoint)==0 || errno!=ENOENT)return 91;
    if(chroot("/lib")!=-1 || errno!=EPERM)return 91;
    for(int fd=5;fd<64;fd++)if(fcntl(fd,F_GETFD)>=0 || errno!=EBADF)return 91;

    char ticket[TICKET_BYTES+1]={0};
    if(!transfer(TICKET_FD,ticket,TICKET_BYTES,0))return 92;
    char extra;
    if(read(TICKET_FD,&extra,1)!=0 || close(TICKET_FD)!=0)return 92;
    if(ticket[TICKET_BYTES-1]!='\n' || strspn(ticket,"0123456789abcdef")!=TICKET_BYTES-1)return 92;
    if(!transfer(CONSUMER_FD,ticket,TICKET_BYTES,1))return 93;
    char verdict;
    if(!transfer(CONSUMER_FD,&verdict,1,0) || verdict!='L')return 93;
    puts("AUTH LIVE");
    fflush(stdout);

    char command[32];
    while(fgets(command,sizeof(command),stdin)) {
        if(!strcmp(command,"quit\n"))return 0;
        if(!strcmp(command,"size\n")) {
            struct winsize size={0};
            if(ioctl(0,TIOCGWINSZ,&size))return 94;
            printf("SIZE %u %u\n",(unsigned)size.ws_col,(unsigned)size.ws_row);
            fflush(stdout);
            continue;
        }
        if(strcmp(command,"check\n"))return 94;
        char query='C';
        if(!transfer(CONSUMER_FD,&query,1,1) ||
           !transfer(CONSUMER_FD,&verdict,1,0) || (verdict!='L' && verdict!='R'))return 94;
        puts(verdict=='L'?"AUTH LIVE":"AUTH REVOKED");
        fflush(stdout);
    }
    return 95;
}

static int decimal_fd(const char *value) {
    char *end;
    errno=0;
    long fd=strtol(value,&end,10);
    return !errno && end!=value && !*end && fd>=3 && fd<1024?(int)fd:-1;
}

static void launch(const char *root, int ticket, int consumer) {
    alarm(10);
    struct stat info;
    int type=0;
    socklen_t length=sizeof(type);
    if(!root_path(root) || ticket<0 || consumer<0 || ticket==consumer ||
       fstat(ticket,&info) || !S_ISFIFO(info.st_mode) ||
       getsockopt(consumer,SOL_SOCKET,SO_TYPE,&type,&length) || type!=SOCK_STREAM)
        child_error("auth-transport-preflight",96);
    int ticket_copy=fcntl(ticket,F_DUPFD,5);
    int consumer_copy=fcntl(consumer,F_DUPFD,5);
    if(ticket_copy<0 || consumer_copy<0 || login_tty(0) ||
       dup2(ticket_copy,TICKET_FD)<0 || dup2(consumer_copy,CONSUMER_FD)<0)
        child_error("auth-transport-fds",96);
    closefrom(5);
    struct rlimit files={64,64}, core={0,0};
    if(setrlimit(RLIMIT_NOFILE,&files) || setrlimit(RLIMIT_CORE,&core) ||
       chdir(root) || chroot(".") || chdir("/") || setgroups(0,NULL) ||
       setgid(SLOT) || setuid(SLOT) || !basic_limit(1))
        child_error("auth-transport-boundary",96);
    char *args[]={"/auth-probe","--inside",NULL};
    char *env[]={"PATH=/","LC_ALL=C",NULL};
    execve(args[0],args,env);
    child_error("auth-transport-exec",96);
}

int main(int argc, char **argv) {
    if(argc==2 && !strcmp(argv[1],"--inside"))return synthetic_door();
    if(getuid()!=0 || geteuid()!=0)return 80;
    if(argc!=5 || strcmp(argv[1],"--launch"))return 81;
    launch(argv[2],decimal_fd(argv[3]),decimal_fd(argv[4]));
    return 96;
}

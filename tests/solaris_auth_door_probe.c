/* Synthetic Solaris game for an operator-approved isolated BBS acceptance run. */
#define __EXTENSIONS__ 1
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

int main(void) {
    char ticket[65], ack;
    size_t have = 0;
    alarm(15);
    while (have < sizeof(ticket)) {
        ssize_t count = read(3, ticket + have, sizeof(ticket) - have);
        if (count <= 0) return 70;
        have += (size_t)count;
    }
    if (read(3, &ack, 1) != 0 || close(3) != 0) return 70;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX, .sun_path = "/channel/sock"};
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) return 71;
    if (write(fd, ticket, sizeof(ticket)) != sizeof(ticket) ||
        read(fd, &ack, 1) != 1 || ack != 'L') return 72;
    alarm(0);
    puts("AUTH LIVE");
    fflush(stdout);
    char command[16];
    while (fgets(command, sizeof(command), stdin)) {
        if (strcmp(command, "quit\n") == 0) break;
        if (strcmp(command, "size\n") != 0) continue;
        struct winsize size = {0};
        if (ioctl(0, TIOCGWINSZ, &size) != 0) return 73;
        printf("SIZE %u %u\n", (unsigned)size.ws_col, (unsigned)size.ws_row);
        fflush(stdout);
    }
    close(fd);
    return 0;
}

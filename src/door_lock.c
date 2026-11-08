#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700
#ifdef __sun
#define __EXTENSIONS__ 1
#endif
#include "door_lock.h"
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(__sun) || defined(LINER_TEST_POSIX_DOOR_LOCK)
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

/* POSIX record locks belong to processes, not open descriptions. A dedicated
 * holder keeps the record lock while any inherited lease pipe remains open.
 * This preserves flock's fork/exec lifetime and same-process exclusion. */
typedef struct Holder { pid_t pid; struct Holder *next; } Holder;
static Holder *holders;

void door_lock_reap(void) {
    Holder **next = &holders;
    while (*next) {
        Holder *holder = *next;
        pid_t result = waitpid(holder->pid, NULL, WNOHANG);
        if (result == holder->pid || (result < 0 && errno == ECHILD)) {
            *next = holder->next;
            free(holder);
        } else {
            next = &holder->next;
        }
    }
}

static void hold_lock(int fd, int watch, int ack, long maxfd) {
    /* Only async-signal-safe operations after a multi-threaded process forks.
     * Move descriptors first, so closing inherited BBS/other-Door descriptors
     * never drops the active lease or accidentally retains another lease. */
    int lock = fcntl(fd, F_DUPFD, 6);
    int input = fcntl(watch, F_DUPFD, 6);
    int output = fcntl(ack, F_DUPFD, 6);
    if (lock < 0 || input < 0 || output < 0) _exit(1);
    if (dup2(lock, 3) < 0 || dup2(input, 4) < 0 || dup2(output, 5) < 0) _exit(1);
    close(0); close(1); close(2);
#ifdef __sun
    (void)maxfd;
    closefrom(6);
#else
    for (int i = 6; i < maxfd; ++i) close(i);
#endif
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
    struct flock record = {0};
    record.l_type = F_WRLCK;
    record.l_whence = SEEK_SET;
    int error = fcntl(3, F_SETLK, &record) < 0 ? errno : 0;
    if (write(5, &error, sizeof(error)) != (ssize_t)sizeof(error)) _exit(1);
    close(5);
    if (error) _exit(1);
    char value;
    for (;;) {
        ssize_t count = read(4, &value, 1);
        if (count == 0 || (count < 0 && errno != EINTR)) break;
    }
    _exit(0);
}

int door_lock_acquire(int fd) {
    door_lock_reap();
    Holder *holder = malloc(sizeof(*holder));
    if (!holder) return -1;
    int lease[2], ack[2];
    if (pipe(lease)) { free(holder); return -1; }
    if (pipe(ack)) { int error = errno; close(lease[0]); close(lease[1]); free(holder); errno = error; return -1; }
    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0) maxfd = 65536;
    pid_t pid = fork();
    if (!pid) hold_lock(fd, lease[0], ack[1], maxfd);
    close(lease[0]); close(ack[1]);
    int error = pid < 0 ? errno : 0;
    if (pid > 0) {
        struct pollfd ready = {ack[0], POLLIN, 0};
        int result = poll(&ready, 1, 2000);
        if (result != 1 || read(ack[0], &error, sizeof(error)) != (ssize_t)sizeof(error)) error = EIO;
    }
    close(ack[0]);
    if (error) {
        close(lease[1]);
        if (pid > 0) {
            kill(pid, SIGKILL);
            while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
        }
        free(holder);
        errno = error == EACCES ? EAGAIN : error;
        return -1;
    }
    holder->pid = pid;
    holder->next = holders;
    holders = holder;
    return lease[1];
}
#else
#include <sys/file.h>
int door_lock_acquire(int fd) { return flock(fd, LOCK_EX | LOCK_NB) ? -1 : fd; }
void door_lock_reap(void) {}
#endif
#else
int door_lock_acquire(int fd) { (void)fd; return -1; }
void door_lock_reap(void) {}
#endif

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
#include <time.h>

/* POSIX record locks belong to processes, not open descriptions. A dedicated
 * holder keeps the record lock while any inherited lease pipe remains open.
 * This preserves flock's fork/exec lifetime and same-process exclusion. */
typedef struct Holder { pid_t pid; int lease, done; struct Holder *next; } Holder;
static Holder *holders;

static long long lock_clock(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return -1;
    return (long long)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static int wait_ready(int fd, int milliseconds) {
    long long now = lock_clock();
    if (now < 0) return -1;
    long long deadline = now + milliseconds;
    struct pollfd ready = {fd, POLLIN, 0};
    for (;;) {
        int result = poll(&ready, 1, (int)(deadline - now));
        if (result >= 0 || errno != EINTR) return result;
        now = lock_clock();
        if (now < 0 || now >= deadline) return -1;
    }
}

void door_lock_reap(void) {
    Holder **next = &holders;
    while (*next) {
        Holder *holder = *next;
        pid_t result = waitpid(holder->pid, NULL, WNOHANG);
        if (result == holder->pid || (result < 0 && errno == ECHILD)) {
            *next = holder->next;
            close(holder->done);
            free(holder);
        } else {
            next = &holder->next;
        }
    }
}

void door_lock_release(int fd) {
    close(fd);
    for (Holder *holder = holders; holder; holder = holder->next) {
        if (holder->lease == fd) {
            holder->lease = -1;
            /* Completion is signalled only after the record lock is closed.
             * Bound the wait if an escaped descendant still owns a lease. */
            (void)wait_ready(holder->done, 100);
            break;
        }
    }
    door_lock_reap();
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
    if (error) _exit(1);
    char value;
    for (;;) {
        ssize_t count = read(4, &value, 1);
        if (count == 0 || (count < 0 && errno != EINTR)) break;
    }
    close(3);
    close(5);
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
    int error = pid < 0 ? errno : 0;
    if (!pid) hold_lock(fd, lease[0], ack[1], maxfd);
    close(lease[0]); close(ack[1]);
    if (pid > 0) {
        int result = wait_ready(ack[0], 2000);
        if (result != 1 || read(ack[0], &error, sizeof(error)) != (ssize_t)sizeof(error)) error = EIO;
    }
    if (error) {
        close(ack[0]);
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
    holder->lease = lease[1];
    holder->done = ack[0];
    holder->next = holders;
    holders = holder;
    return lease[1];
}
#else
#include <sys/file.h>
int door_lock_acquire(int fd) { return flock(fd, LOCK_EX | LOCK_NB) ? -1 : fd; }
void door_lock_release(int fd) { close(fd); }
void door_lock_reap(void) {}
#endif
#else
int door_lock_acquire(int fd) { (void)fd; return -1; }
void door_lock_release(int fd) { (void)fd; }
void door_lock_reap(void) {}
#endif

#define _DEFAULT_SOURCE
#ifndef __FreeBSD__
/* FreeBSD hides native flock when a strict POSIX/XSI namespace is requested.
 * Keep its default namespace; glibc uses _DEFAULT_SOURCE alongside XSI. */
#define _XOPEN_SOURCE 700
#endif
#ifdef __sun
#define __EXTENSIONS__ 1
#endif
#include "door_lock.h"
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
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
static char helper_path[PATH_MAX];

static int trusted_helper(const char *path) {
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) ||
        (st.st_uid != 0 && st.st_uid != geteuid()) ||
        (st.st_mode & (S_IWGRP | S_IWOTH | S_ISUID | S_ISGID)) || access(path, X_OK)) return 0;
    char directory[PATH_MAX];
    if (strlen(path) >= sizeof(directory)) return 0;
    strcpy(directory, path);
    for (;;) {
        char *slash = strrchr(directory, '/');
        if (!slash) return 0;
        if (slash == directory) slash[1] = 0; else *slash = 0;
        if (stat(directory, &st) || !S_ISDIR(st.st_mode) ||
            (st.st_uid != 0 && st.st_uid != geteuid())) return 0;
        /* A root-owned sticky /tmp does not allow another user to replace
         * this user's private entry. Other writable ancestors are unsafe. */
        if ((st.st_mode & (S_IWGRP | S_IWOTH)) &&
            !(st.st_uid == 0 && (st.st_mode & S_ISVTX))) return 0;
        if (!strcmp(directory, "/")) return 1;
    }
}

int door_lock_init(const char *program) {
    char binary[PATH_MAX], candidate[PATH_MAX];
    const char *image = program;
#if defined(__sun)
    image = "/proc/self/path/a.out";
#elif defined(__linux__)
    image = "/proc/self/exe";
#endif
    if (!realpath(image, binary) && !realpath(program, binary)) return 0;
    char *slash = strrchr(binary, '/');
    if (!slash) { errno = EINVAL; return 0; }
    *slash = 0;
    const char *suffixes[] = {"/liner-notes-door-lock", "/../libexec/liner-notes-door-lock"};
    helper_path[0] = 0;
    for (unsigned i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        int count = snprintf(candidate, sizeof(candidate), "%s%s", binary, suffixes[i]);
        if (count < 0 || count >= (int)sizeof(candidate)) continue;
        if (realpath(candidate, helper_path) && trusted_helper(helper_path)) return 1;
        helper_path[0] = 0;
    }
    errno = ENOENT;
    return 0;
}

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
    /* A Door has its own session. Killing the BBS process group must not drop
     * its lease while that Door survives. SMF contracts still contain both. */
    if (setpgid(0, 0)) _exit(1);
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
    /* Exec a tiny image before acknowledging the lease. Keeping this fork's
     * BBS address space reserves about 1 GiB of Solaris swap per live holder. */
    char *argv[] = {helper_path, "--lease-v1", NULL};
    char *env[] = {"PATH=/usr/bin:/bin", "LANG=C", NULL};
    execve(helper_path, argv, env);
    int error = errno;
    ssize_t sent;
    do { sent = write(5, &error, sizeof(error)); } while (sent < 0 && errno == EINTR);
    _exit(sent == (ssize_t)sizeof(error) ? 1 : 2);
}

int door_lock_acquire(int fd) {
    if (!*helper_path) { errno = ENOENT; return -1; }
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
int door_lock_init(const char *program) { (void)program; return 1; }
int door_lock_acquire(int fd) { return flock(fd, LOCK_EX | LOCK_NB) ? -1 : fd; }
void door_lock_release(int fd) { close(fd); }
void door_lock_reap(void) {}
#endif
#else
int door_lock_init(const char *program) { (void)program; return 1; }
int door_lock_acquire(int fd) { (void)fd; return -1; }
void door_lock_release(int fd) { (void)fd; }
void door_lock_reap(void) {}
#endif

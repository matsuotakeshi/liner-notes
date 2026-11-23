/* Candidate child entry for an owned Solaris Door process contract.
 * The supervisor must admit the root before passing its descriptor here.
 */
#define __EXTENSIONS__ 1
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <priv.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <termios.h>
#include <unistd.h>

enum { TICKET_FD = 3, STATUS_FD = 4, KEEP_END = 5 };

static int decimal(const char *value, int minimum) {
    if (!*value || strspn(value, "0123456789") != strlen(value)) return -1;
    errno = 0;
    long number = strtol(value, NULL, 10);
    return errno || number < minimum || number > INT_MAX ? -1 : (int)number;
}

static int same_file(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static int channel(int fd, int mode, struct stat *info) {
    int flags = fcntl(fd, F_GETFL);
    return flags != -1 && (flags & O_ACCMODE) == mode &&
           fstat(fd, info) == 0 && S_ISFIFO(info->st_mode);
}

static void fail(int fd, const char *stage) {
    (void)write(fd, stage, strlen(stage));
    _exit(126);
}

static int enter_root(int fd) {
    struct stat candidate, current, program;
    struct statvfs filesystem;
    return fstat(fd, &candidate) == 0 && S_ISDIR(candidate.st_mode) &&
           candidate.st_uid == 0 && !(candidate.st_mode & 022) &&
           stat("/", &current) == 0 && !same_file(&candidate, &current) &&
           fstatvfs(fd, &filesystem) == 0 && (filesystem.f_flag & ST_NOSUID) &&
           fchdir(fd) == 0 && chroot(".") == 0 && chdir("/") == 0 &&
           lstat("/door", &program) == 0 && S_ISREG(program.st_mode) &&
           program.st_uid == 0 && !(program.st_mode & (022 | S_ISUID | S_ISGID)) &&
           (program.st_mode & S_IXOTH);
}

static int restrict_privileges(void) {
    priv_set_t *allowed = priv_allocset();
    priv_set_t *actual = priv_allocset();
    int ok = 0;
    if (allowed && actual) {
        priv_basicset(allowed);
        ok = priv_delset(allowed, PRIV_NET_ACCESS) == 0 &&
             priv_delset(allowed, PRIV_PROC_INFO) == 0 &&
             priv_delset(allowed, PRIV_PROC_SESSION) == 0 &&
             setpflags(PRIV_PFEXEC, 0) == 0 &&
             setpflags(PRIV_PFEXEC_AUTH, 0) == 0;
        const priv_ptype_t sets[] = {
            PRIV_INHERITABLE, PRIV_EFFECTIVE, PRIV_PERMITTED, PRIV_LIMIT
        };
        for (size_t i = 0; ok && i < sizeof(sets) / sizeof(sets[0]); i++)
            ok = setppriv(PRIV_SET, sets[i], allowed) == 0 &&
                 getppriv(sets[i], actual) == 0 && priv_isequalset(allowed, actual);
    }
    if (actual) priv_freeset(actual);
    if (allowed) priv_freeset(allowed);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 6) return 64;
    int uid = decimal(argv[1], 1);
    int fds[4];
    if (uid < 1) return 64;
    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); i++) {
        fds[i] = decimal(argv[i + 2], TICKET_FD);
        if (fds[i] < TICKET_FD) return 64;
        for (size_t j = 0; j < i; j++)
            if (fds[i] == fds[j]) return 64;
    }
    if (getuid() != 0 || geteuid() != 0) return 77;

    int root = fds[0], tty = fds[1], ticket = fds[2], status = fds[3];
    struct stat ticket_info, status_info;
    if (!channel(status, O_WRONLY, &status_info)) return 65;
    if (!channel(ticket, O_RDONLY, &ticket_info) ||
        same_file(&ticket_info, &status_info) || !isatty(tty))
        fail(status, "ERR channels\n");
    int status_copy = fcntl(status, F_DUPFD_CLOEXEC, KEEP_END);
    int ticket_copy = fcntl(ticket, F_DUPFD_CLOEXEC, KEEP_END);
    int tty_copy = fcntl(tty, F_DUPFD_CLOEXEC, KEEP_END);
    if (status_copy < 0 || ticket_copy < 0 || tty_copy < 0)
        fail(status, "ERR duplicate\n");
    if (signal(SIGTERM, SIG_DFL) == SIG_ERR || signal(SIGINT, SIG_DFL) == SIG_ERR ||
        login_tty(tty_copy) != 0)
        fail(status_copy, "ERR tty\n");
    if (!enter_root(root)) fail(status_copy, "ERR root\n");
    if (dup2(ticket_copy, TICKET_FD) < 0 || fcntl(status_copy, F_DUP2FD_CLOEXEC, STATUS_FD) < 0)
        fail(status_copy, "ERR descriptors\n");
    closefrom(KEEP_END);

    const struct rlimit files = {64, 64}, core = {0, 0};
    if (setrlimit(RLIMIT_NOFILE, &files) || setrlimit(RLIMIT_CORE, &core) ||
        setgroups(0, NULL) || setgid((gid_t)uid) || setuid((uid_t)uid) ||
        getuid() != (uid_t)uid || geteuid() != (uid_t)uid ||
        getgid() != (gid_t)uid || getegid() != (gid_t)uid ||
        getgroups(0, NULL) != 0 || !restrict_privileges())
        fail(STATUS_FD, "ERR credentials\n");
    umask(077);

    /* PREPARED acknowledges the boundary only. The caller must still verify
     * exec and the launched program's own readiness. */
    const char prepared[] = "PREPARED\n";
    if (write(STATUS_FD, prepared, sizeof(prepared) - 1) != sizeof(prepared) - 1)
        _exit(126);
    char *args[] = {"/door", NULL};
    char *environment[] = {"PATH=/usr/bin:/bin", "TERM=xterm", "LINER_AUTH_FD=3", NULL};
    execve(args[0], args, environment);
    fail(STATUS_FD, "ERR exec\n");
}

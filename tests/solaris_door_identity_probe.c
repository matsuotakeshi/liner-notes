/* Synthetic Solaris credential probe. NOT a Door launcher and never installed
 * setuid. Only disposable copies of --report are given set-ID bits by the
 * separately approved root fixture. They cannot invoke privileged modes:
 * those require both real and effective UID zero. */
#define __EXTENSIONS__ 1
#define _STRUCTURED_PROC 1
#include <sys/types.h>
#include <sys/procfs.h>
#include <sys/statvfs.h>
#include <priv.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SLOT = 61001, LAST = 61005 };

static int credentials(const char *path, prcred_t *out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, out, sizeof(*out));
    int saved = errno;
    close(fd);
    errno = saved;
    if (n < (ssize_t)offsetof(prcred_t, pr_ngroups)) {
        if (n >= 0) errno = EIO;
        return -1;
    }
    return 0;
}

static int report(void) {
    prcred_t c;
    priv_set_t *limit = priv_allocset(), *basic = priv_allocset();
    if (!limit || !basic || credentials("/proc/self/cred", &c) < 0 ||
        getppriv(PRIV_LIMIT, limit) != 0) return 70;
    priv_basicset(basic);
    int groups = getgroups(0, NULL);
    if (groups < 0) return 71;
    printf("{\"ruid\":%lu,\"euid\":%lu,\"suid\":%lu,"
           "\"rgid\":%lu,\"egid\":%lu,\"sgid\":%lu,"
           "\"groups\":%d,\"basic_limit\":%s}\n",
           (unsigned long)c.pr_ruid, (unsigned long)c.pr_euid,
           (unsigned long)c.pr_suid, (unsigned long)c.pr_rgid,
           (unsigned long)c.pr_egid, (unsigned long)c.pr_sgid, groups,
           priv_isequalset(limit, basic) ? "true" : "false");
    priv_freeset(limit); priv_freeset(basic);
    return 0;
}

static int reserved(unsigned long id) { return id >= SLOT && id <= LAST; }

static int inventory(void) {
    DIR *directory = opendir("/proc");
    if (!directory) return 72;
    struct dirent *entry;
    int result = 0;
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (!entry->d_name[0] || strspn(entry->d_name, "0123456789") != strlen(entry->d_name)) continue;
        char path[512];
        if (snprintf(path, sizeof(path), "/proc/%s/cred", entry->d_name) >= (int)sizeof(path)) { result = 73; break; }
        prcred_t c;
        if (credentials(path, &c) < 0) {
            if (errno == ENOENT || errno == ESRCH) { errno = 0; continue; }
            result = 74; break;
        }
        if (reserved(c.pr_ruid) || reserved(c.pr_euid) || reserved(c.pr_suid) ||
            reserved(c.pr_rgid) || reserved(c.pr_egid) || reserved(c.pr_sgid)) { result = 75; break; }
        errno = 0;
    }
    if (!result && errno) result = 76;
    closedir(directory);
    return result;
}

static int limit_basic(void) {
    priv_set_t *basic = priv_allocset(), *actual = priv_allocset();
    if (!basic || !actual) return 0;
    priv_basicset(basic);
    int ok = setpflags(PRIV_PFEXEC, 0) == 0 && setpflags(PRIV_PFEXEC_AUTH, 0) == 0;
    ok = ok && setppriv(PRIV_SET, PRIV_INHERITABLE, basic) == 0;
    ok = ok && setppriv(PRIV_SET, PRIV_EFFECTIVE, basic) == 0;
    ok = ok && setppriv(PRIV_SET, PRIV_PERMITTED, basic) == 0;
    ok = ok && setppriv(PRIV_SET, PRIV_LIMIT, basic) == 0;
    ok = ok && getppriv(PRIV_LIMIT, actual) == 0 && priv_isequalset(actual, basic);
    priv_freeset(actual); priv_freeset(basic);
    return ok;
}

int main(int argc, char **argv) {
    /* No filesystem mutation or other privileged operation exists in report. */
    if (argc == 2 && strcmp(argv[1], "--report") == 0) return report();
    if (getuid() != 0 || geteuid() != 0) return 77;
    if (argc == 2 && strcmp(argv[1], "--inventory") == 0) return inventory();
    int limited = argc == 3 && strcmp(argv[1], "--basic") == 0;
    int baseline = argc == 3 && strcmp(argv[1], "--baseline") == 0;
    if (!limited && !baseline) return 78;
    alarm(5); /* survives exec; the synthetic report neither forks nor waits */
    if (setgroups(0, NULL) != 0 || setgid(SLOT) != 0 || setuid(SLOT) != 0) return 79;
    if (limited && !limit_basic()) return 80;
    prcred_t c;
    if (credentials("/proc/self/cred", &c) < 0 || c.pr_ruid != SLOT || c.pr_euid != SLOT ||
        c.pr_suid != SLOT || c.pr_rgid != SLOT || c.pr_egid != SLOT || c.pr_sgid != SLOT) return 81;
    char *args[] = {argv[2], "--report", NULL};
    char *env[] = {"PATH=/usr/bin", "LC_ALL=C", NULL};
    execve(argv[2], args, env);
    if (limited && (errno == EACCES || errno == EPERM)) {
        printf("{\"exec_denied\":true,\"errno\":%d}\n", errno);
        return 0;
    }
    return 82;
}

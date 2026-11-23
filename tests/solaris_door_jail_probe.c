/* Synthetic, root-gated credential/namespace fixture. NOT a Door backend.
 * No arbitrary commands, networking, or file writes. The opt-in lifecycle
 * payload adds one bounded synthetic descendant after checking the boundary.
 * The root observer reads credentials while its unreaped child waits on a pipe;
 * /proc and contract descriptors are NEVER inherited into the execution root.
 */
#define __EXTENSIONS__ 1
#define _STRUCTURED_PROC 1
#include <sys/types.h>
#include <sys/procfs.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <priv.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

enum { SLOT = 61001 };
struct observation { int basic, namespace_closed, descriptors_closed, chroot_denied; };

/* Fixed, short records on the existing nonblocking diagnostic pipe. Never
 * retry a full pipe or let diagnostics replace the errno under investigation. */
static void progress(const char *message, size_t length) {
    int saved = errno;
    (void)write(2,message,length);
    errno = saved;
}
#define PROGRESS(stage) progress("JAIL_PROGRESS stage=" stage "\n", sizeof("JAIL_PROGRESS stage=" stage "\n")-1)

static int basic_limit(int apply) {
    PROGRESS("priv-allocate");
    priv_set_t *basic = priv_allocset(), *actual = priv_allocset();
    if (!basic || !actual) return 0;
    PROGRESS("priv-basicset");
    priv_basicset(basic);
    int ok = 1;
    if (apply) {
        PROGRESS("priv-flags");
        ok = setpflags(PRIV_PFEXEC, 0) == 0 && setpflags(PRIV_PFEXEC_AUTH, 0) == 0;
        PROGRESS("priv-inheritable");
        ok = ok && setppriv(PRIV_SET, PRIV_INHERITABLE, basic) == 0;
        PROGRESS("priv-effective");
        ok = ok && setppriv(PRIV_SET, PRIV_EFFECTIVE, basic) == 0;
        PROGRESS("priv-permitted");
        ok = ok && setppriv(PRIV_SET, PRIV_PERMITTED, basic) == 0;
        PROGRESS("priv-limit");
        ok = ok && setppriv(PRIV_SET, PRIV_LIMIT, basic) == 0;
    }
    PROGRESS("priv-query");
    ok = ok && getppriv(PRIV_LIMIT, actual) == 0 && priv_isequalset(actual, basic);
    priv_freeset(actual); priv_freeset(basic);
    PROGRESS("priv-done");
    return ok;
}

static int transfer(int fd, void *data, size_t length, int writing) {
    char *cursor = data;
    while (length) {
        ssize_t n = writing ? write(fd, cursor, length) : read(fd, cursor, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        cursor += n; length -= (size_t)n;
    }
    return 1;
}

static void child_error(const char *stage, int code) {
    int saved = errno;
    (void)dprintf(2,"JAIL_CHILD stage=%s errno=%d exit=%d\n",stage,saved,code);
    _exit(code);
}

static void collect_errors(int fd) {
    char buffer[4096];
    ssize_t n;
    /* Nonblocking channel: bounded diagnostics cannot stall cleanup. */
    while ((n = read(fd,buffer,sizeof(buffer))) > 0)
        (void)fwrite(buffer,1,(size_t)n,stderr);
    close(fd);
}

static struct observation inspect_payload(void) {
    /* It is also safe if a baseline accidentally gives this reporter set-ID:
     * do not open any writable file, signal anything or invoke privileged mode. */
    alarm(5);
    PROGRESS("payload-entry");
    struct observation value = {basic_limit(0), 1, 1, 0};
    PROGRESS("payload-namespace");
    const char *paths[] = {"/proc/self/cred", "/system/contract/process/template",
        "/dev/fd/3", "/etc/passwd", "/../../etc/passwd", NULL};
    for (int i = 0; paths[i]; ++i) {
        int fd = open(paths[i], O_RDONLY);
        if (fd >= 0) { value.namespace_closed = 0; close(fd); }
        else if (errno != ENOENT) value.namespace_closed = 0;
    }
    PROGRESS("payload-descriptors");
    for (int fd = 3; fd < 256; ++fd) {
        if (fcntl(fd, F_GETFD) >= 0 || errno != EBADF) value.descriptors_closed = 0;
    }
    /* Solaris can allow chroot("/") when it names the system root even without
     * proc_chroot. Test a genuine root change, not that no-op. /lib exists in
     * both the host and our minimal loader root. If unexpectedly allowed this
     * only moves inward; the reporter then uses existing pipes and exits. */
    struct stat current, target;
    PROGRESS("payload-chroot-target");
    if (stat("/",&current) || stat("/lib",&target)) child_error("chroot-target",70);
    if (!S_ISDIR(target.st_mode) ||
        (current.st_dev == target.st_dev && current.st_ino == target.st_ino)) {
        errno = EINVAL; child_error("chroot-target",70);
    }
    PROGRESS("payload-chroot");
    int changed = chroot("/lib"), change_errno = errno;
    if (changed == -1 && change_errno != EPERM) {
        errno = change_errno; child_error("chroot-denial",70);
    }
    value.chroot_denied = changed == -1 && change_errno == EPERM;
    return value;
}

static int payload(void) {
    struct observation value = inspect_payload();
    PROGRESS("payload-ready");
    char ready = 'R', ack = 0;
    if (!transfer(1, &ready, 1, 1) || !transfer(1, &value, sizeof(value), 1)) return 71;
    PROGRESS("payload-ack-wait");
    int accepted = transfer(0, &ack, 1, 0) && ack == 'A';
    PROGRESS("payload-ack-done");
    return accepted ? 0 : 72;
}

static int root_path(const char *path) {
    const char *prefix = "/var/tmp/liner-door-jail-";
    if (strncmp(path, prefix, strlen(prefix))) return 0;
    const char *suffix = path + strlen(prefix);
    size_t n = strcspn(suffix, "/");
    if (n < 6 || n > 32 || strspn(suffix, "abcdefghijklmnopqrstuvwxyz0123456789_") != n ||
        strcmp(suffix+n, "/root")) return 0;
    char resolved[4096]; struct stat st; struct statvfs fs;
    return realpath(path, resolved) && !strcmp(path, resolved) &&
        lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == 0 &&
        !(st.st_mode & 0022) && statvfs(path, &fs) == 0 &&
        !strcmp(fs.f_basetype, "tmpfs") && (fs.f_flag & ST_NOSUID);
}

static int case_name(const char *name) {
    if (!strcmp(name, "plain")) return 1;
    if (strncmp(name, "uid-", 4) && strncmp(name, "gid-", 4)) return 0;
    const char *role = name+4;
    return !strcmp(role,"root") || !strcmp(role,"bbs") || !strcmp(role,"broker") ||
        !strcmp(role,"consumer") || !strcmp(role,"other_slot");
}

/* Both fixtures execute the same fixed launcher setup. Return only if exec
 * failed, with errno left intact for the caller's verdict. */
static void execute_in_root(const char *root, const char *name, char *mode) {
    struct rlimit limit = {256,256}, core = {0,0};
    PROGRESS("close-descriptors");
    closefrom(3);
    PROGRESS("limits");
    if (setrlimit(RLIMIT_NOFILE,&limit) || setrlimit(RLIMIT_CORE,&core)) child_error("limits",76);
    PROGRESS("root");
    if (chdir(root) || chroot(".") || chdir("/")) child_error("root",76);
    PROGRESS("groups");
    if (setgroups(0,NULL)) child_error("groups",76);
    PROGRESS("gid");
    if (setgid(SLOT)) child_error("gid",76);
    PROGRESS("uid");
    if (setuid(SLOT)) child_error("uid",76);
    PROGRESS("privileges");
    if (!basic_limit(1)) child_error("privileges",76);
    char path[80]; (void)snprintf(path,sizeof(path),"/%s",name);
    char *args[] = {path,mode,NULL};
    char *env[] = {"PATH=/", "LC_ALL=C", NULL};
    for (int fd = 0; fd < 3; ++fd) {
        int flags = fcntl(fd,F_GETFD);
        if (flags < 0) child_error("exec-stdio",77);
        if (flags & FD_CLOEXEC) {
            errno = EINVAL;
            child_error("exec-stdio",77);
        }
    }
    PROGRESS("exec");
    execve(path,args,env);
}

/* A synthetic member of the system-owned test contract. The direct child
 * holds stdout while a descendant in a separate session holds its other
 * copy. Each process has an independent deadline, including after fork. */
static int lifecycle_payload(void) {
    if (getuid() != SLOT || geteuid() != SLOT || getgid() != SLOT || getegid() != SLOT ||
        getgroups(0,NULL) != 0) return 82;
    struct observation value = inspect_payload();
    if (!value.basic || !value.namespace_closed || !value.descriptors_closed ||
        !value.chroot_denied) return 82;
    pid_t leader = getpid();
    pid_t descendant = fork();
    if (descendant < 0) child_error("lifecycle-fork",83);
    if (descendant == 0) {
        alarm(5);
        if (setsid() < 0) child_error("lifecycle-session",83);
        char ready[96];
        int length = snprintf(ready,sizeof(ready),"READY %ld %ld\n",
                              (long)leader,(long)getpid());
        if (length <= 0 || (size_t)length >= sizeof(ready) ||
            !transfer(1,ready,(size_t)length,1)) child_error("lifecycle-ready",83);
    }
    for (;;) pause();
}

static int observe(const char *root, const char *name) {
    int output[2], ack[2], errors[2];
    if (!root_path(root) || !case_name(name) || pipe(output) || pipe(ack) || pipe(errors)) {
        (void)fprintf(stderr,"JAIL_PARENT stage=preflight errno=%d\n",errno); return 73;
    }
    if (fcntl(errors[0],F_SETFL,O_NONBLOCK) < 0 || fcntl(errors[1],F_SETFL,O_NONBLOCK) < 0) return 73;
    pid_t child = fork();
    if (child < 0) return 74;
    if (child == 0) {
        alarm(5);
        if (dup2(ack[0], 0) < 0 || dup2(output[1], 1) < 0 || dup2(errors[1], 2) < 0) child_error("stdio",75);
        execute_in_root(root,name,"--inside");
        /* A denied set-ID exec can be secure, but missing libraries cannot. */
        if (errno == EACCES || errno == EPERM) {
            char denied = 'D'; (void)transfer(1,&denied,1,1); _exit(0);
        }
        child_error("exec",77);
    }
    close(output[1]); close(ack[0]); close(errors[1]);
    struct pollfd waiting = {output[0],POLLIN,0};
    char marker = 0; struct observation value = {0,0,0,0};
    int polled = poll(&waiting,1,6000), poll_errno = polled < 0 ? errno : 0;
    errno = 0;
    int received = polled > 0 && transfer(output[0],&marker,1,0);
    int read_errno = received || polled <= 0 ? 0 : errno;
    int ok = received;
    if (!received || (marker != 'R' && marker != 'D'))
        (void)fprintf(stderr,"JAIL_PARENT stage=ready poll=%d poll_errno=%d revents=%d received=%d read_errno=%d marker=%u\n",
                      polled,poll_errno,(int)waiting.revents,received,read_errno,(unsigned char)marker);
    int denied = ok && marker == 'D';
    prcred_t credentials; memset(&credentials,0,sizeof(credentials));
    if (!denied) {
        ok = ok && marker == 'R' && transfer(output[0],&value,sizeof(value),0);
        char path[80]; (void)snprintf(path,sizeof(path),"/proc/%ld/cred",(long)child);
        /* Child cannot exit before the acknowledgement except by its deadline;
         * an unreaped direct child PID cannot refer to an unrelated process. */
        int fd = ok ? open(path,O_RDONLY) : -1;
        ssize_t n = fd >= 0 ? read(fd,&credentials,sizeof(credentials)) : -1;
        if (fd >= 0) close(fd);
        if (ok && n < (ssize_t)(offsetof(prcred_t,pr_ngroups)+sizeof(credentials.pr_ngroups)))
            (void)fprintf(stderr,"JAIL_PARENT stage=credentials errno=%d bytes=%ld\n",errno,(long)n);
        ok = ok && n >= (ssize_t)(offsetof(prcred_t,pr_ngroups)+sizeof(credentials.pr_ngroups));
        char proceed = 'A'; if (ok) ok = transfer(ack[1],&proceed,1,1);
    }
    int status = 0;
    pid_t waited = 0;
    if (!ok) {
        /* Preserve an exit already observable on EOF before cleanup signals. */
        do { waited = waitpid(child,&status,WNOHANG); } while (waited < 0 && errno == EINTR);
        (void)fprintf(stderr,"JAIL_PARENT stage=pre-cleanup child_exited=%d wait_errno=%d\n",
                      waited==child,waited < 0 ? errno : 0);
    }
    close(ack[1]); close(output[0]);
    if (!ok && waited == 0) {
        /* Still our unreaped direct child, never a reused/unrelated PID. */
        int killed = kill(child,SIGKILL), kill_errno = killed < 0 ? errno : 0;
        (void)fprintf(stderr,"JAIL_PARENT stage=cleanup-kill result=%d errno=%d\n",killed,kill_errno);
    }
    if (waited == 0)
        do { waited = waitpid(child,&status,0); } while (waited < 0 && errno == EINTR);
    collect_errors(errors[0]);
    if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
        (void)fprintf(stderr,"JAIL_PARENT stage=wait reaped=%d exited=%d exit=%d signal=%d marker=%u\n",
                      waited==child, WIFEXITED(status)!=0,
                      WIFEXITED(status)?WEXITSTATUS(status):-1,
                      WIFSIGNALED(status)?WTERMSIG(status):0,(unsigned char)marker);
        return 78;
    }
    if (!ok) return 79;
    if (denied) { puts("{\"exec_denied\":true}"); return 0; }
    printf("{\"ruid\":%lu,\"euid\":%lu,\"suid\":%lu,\"rgid\":%lu,\"egid\":%lu,\"sgid\":%lu,"
           "\"groups\":%d,\"basic_limit\":%s,\"namespace_closed\":%s,\"descriptors_closed\":%s,\"chroot_denied\":%s}\n",
           (unsigned long)credentials.pr_ruid,(unsigned long)credentials.pr_euid,(unsigned long)credentials.pr_suid,
           (unsigned long)credentials.pr_rgid,(unsigned long)credentials.pr_egid,(unsigned long)credentials.pr_sgid,
           credentials.pr_ngroups,value.basic?"true":"false",value.namespace_closed?"true":"false",
           value.descriptors_closed?"true":"false",value.chroot_denied?"true":"false");
    return 0;
}

int main(int argc,char **argv) {
    if (argc == 2 && !strcmp(argv[1],"--inside")) return payload();
    if (argc == 2 && !strcmp(argv[1],"--inside-lifecycle")) return lifecycle_payload();
    if (getuid() != 0 || geteuid() != 0) return 80;
    if (argc == 3 && !strcmp(argv[1],"--launch-lifecycle")) {
        if (!root_path(argv[2])) return 73;
        alarm(5);
        execute_in_root(argv[2],"plain","--inside-lifecycle");
        child_error("lifecycle-exec",77);
    }
    if (argc != 4 || strcmp(argv[1],"--observe")) return 81;
    signal(SIGPIPE,SIG_IGN);
    return observe(argv[2],argv[3]);
}

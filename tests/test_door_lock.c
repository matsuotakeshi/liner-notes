#define _POSIX_C_SOURCE 200809L
#include "door_lock.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void pause_short(void) { struct timespec t = {0, 10000000}; nanosleep(&t, NULL); }
static int acquire(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    assert(fd >= 0);
    int lease = door_lock_acquire(fd);
    int error = errno;
    if (lease != fd) close(fd);
    errno = error;
    return lease;
}
static void blocked(const char *path) {
    int lease = acquire(path);
    assert(lease < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
}
static int eventual(const char *path) {
    for (int i = 0; i < 300; ++i) {
        int lease = acquire(path);
        if (lease >= 0) return lease;
        pause_short();
    }
    assert(!"lock did not release");
    return -1;
}
int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "hold")) {
        int lease = atoi(argv[2]), control = atoi(argv[3]), ready = atoi(argv[4]);
        char c = 'R';
        assert(write(ready, &c, 1) == 1);
        close(ready);
        assert(read(control, &c, 1) == 1);
        close(lease);
        return 0;
    }
    assert(argc == 2);
    const char *path = argv[1];
    int lease = acquire(path);
    assert(lease >= 0);
    /* Normal completion must permit immediate menu re-entry. */
    for (int i = 0; i < 10; ++i) {
        door_lock_release(lease);
        lease = acquire(path);
        assert(lease >= 0);
    }
    blocked(path); /* Different open in the same BBS must conflict. */
    /* A failed first-slot attempt must not block a distinct working directory
     * or another slot. Multiple record holders must retain only their lease. */
    char second[4096], third[4096];
    assert(snprintf(second, sizeof(second), "%s.second", path) < (int)sizeof(second));
    assert(snprintf(third, sizeof(third), "%s.third", path) < (int)sizeof(third));
    int second_lease = acquire(second); assert(second_lease >= 0);
    blocked(path); blocked(second);
    int third_lease = acquire(third); assert(third_lease >= 0);
    blocked(third); blocked(second); blocked(path);
    door_lock_release(second_lease);
    second_lease = acquire(second); assert(second_lease >= 0);
    blocked(third); blocked(path);
    door_lock_release(second_lease); door_lock_release(third_lease);
    assert(!unlink(second) && !unlink(third));
    int control[2], ready[2];
    assert(!pipe(control) && !pipe(ready));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        char a[32], b[32], c[32];
        snprintf(a, sizeof(a), "%d", lease);
        snprintf(b, sizeof(b), "%d", control[0]);
        snprintf(c, sizeof(c), "%d", ready[1]);
        close(control[1]); close(ready[0]);
        execl(argv[0], argv[0], "hold", a, b, c, (char *)NULL);
        _exit(2);
    }
    close(control[0]); close(ready[1]);
    char value;
    assert(read(ready[0], &value, 1) == 1);
    close(ready[0]); close(lease);
    blocked(path); /* Survives close by the BBS and exec by the Door. */
    assert(write(control[1], "X", 1) == 1); close(control[1]);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    lease = eventual(path); close(lease);
    for (int i = 0; i < 100; ++i) { door_lock_reap(); pause_short(); }
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);

    /* If the BBS crashes, an exec'd Door still retains the same slot. */
    assert(!pipe(control) && !pipe(ready));
    pid_t owner = fork(); assert(owner >= 0);
    if (!owner) {
        lease = acquire(path); assert(lease >= 0);
        pid_t game = fork(); assert(game >= 0);
        if (!game) {
            close(control[1]); close(ready[0]);
            char a[32], b[32], c[32];
            snprintf(a, sizeof(a), "%d", lease);
            snprintf(b, sizeof(b), "%d", control[0]);
            snprintf(c, sizeof(c), "%d", ready[1]);
            execl(argv[0], argv[0], "hold", a, b, c, (char *)NULL);
            _exit(2);
        }
        _exit(0); /* No cleanup: emulate sudden owner termination. */
    }
    close(control[0]); close(ready[1]);
    assert(read(ready[0], &value, 1) == 1); close(ready[0]);
    assert(waitpid(owner, &status, 0) == owner && WIFEXITED(status) && !WEXITSTATUS(status));
    blocked(path);
    assert(write(control[1], "X", 1) == 1); close(control[1]);
    lease = eventual(path); close(lease);
    for (int i = 0; i < 100; ++i) { door_lock_reap(); pause_short(); }
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    /* The Door uses a separate session, as door_start does. Killing the BBS
     * process group must not kill the record-lock holder before that Door. */
    assert(!pipe(control) && !pipe(ready));
    owner = fork(); assert(owner >= 0);
    if (!owner) {
        assert(setsid() >= 0);
        lease = acquire(path); assert(lease >= 0);
        pid_t game = fork(); assert(game >= 0);
        if (!game) {
            close(control[1]); close(ready[0]);
            assert(setsid() >= 0);
            char a[32], b[32], c[32];
            snprintf(a, sizeof(a), "%d", lease);
            snprintf(b, sizeof(b), "%d", control[0]);
            snprintf(c, sizeof(c), "%d", ready[1]);
            execl(argv[0], argv[0], "hold", a, b, c, (char *)NULL);
            _exit(2);
        }
        for (;;) pause();
    }
    close(control[0]); close(ready[1]);
    assert(read(ready[0], &value, 1) == 1); close(ready[0]);
    assert(!kill(-owner, SIGKILL));
    assert(waitpid(owner, &status, 0) == owner && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    /* Recheck after other group members have had time to observe SIGKILL. */
    for (int i = 0; i < 10; ++i) { blocked(path); pause_short(); }
    assert(write(control[1], "X", 1) == 1); close(control[1]);
    lease = eventual(path); door_lock_release(lease);
    for (int i = 0; i < 100; ++i) { door_lock_reap(); pause_short(); }
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    puts("PASS Door leases: same-process exclusion, fork/exec, owner/group death, release and holder reaping");
    return 0;
}

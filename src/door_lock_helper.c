/* Private, unprivileged record-lock holder. Never links the BBS address space. */
#define _POSIX_C_SOURCE 200809L
#ifdef __sun
#define __EXTENSIONS__ 1
#endif
#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--lease-v1")) return 2;
    struct stat lock, watch, ack;
    int error = 0;
    if (fstat(3, &lock) || fstat(4, &watch) || fstat(5, &ack)) error = errno;
    else if (!S_ISREG(lock.st_mode) || lock.st_uid != geteuid() ||
             (lock.st_mode & 077) || lock.st_nlink != 1 ||
             !S_ISFIFO(watch.st_mode) || !S_ISFIFO(ack.st_mode)) error = EPERM;
    if (!error) {
        struct flock record = {0};
        record.l_type = F_WRLCK;
        record.l_whence = SEEK_SET;
        if (fcntl(3, F_SETLK, &record)) error = errno;
    }
    if (write(5, &error, sizeof(error)) != (ssize_t)sizeof(error) || error) return 1;
    char value;
    for (;;) {
        ssize_t count = read(4, &value, 1);
        if (!count || (count < 0 && errno != EINTR)) break;
    }
    close(3);
    close(5);
    return 0;
}
#else
int main(void) { return 2; }
#endif

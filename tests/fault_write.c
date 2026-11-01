/* Test-only short-write shim: constrain sequential journal writes, not SQLite
 * pwrite() calls. Never linked into, or enabled by, the production runtime. */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <dlfcn.h>
static ssize_t (*next_write)(int,const void *,size_t);
static uint64_t ceiling;
static int enabled;
__attribute__((constructor)) static void setup(void) {
    next_write=(ssize_t (*)(int,const void *,size_t))dlsym(RTLD_NEXT,"write");
    const char *s=getenv("LINER_TEST_JOURNAL_LIMIT");if(s) { enabled=1;ceiling=strtoull(s,NULL,10); }
}
ssize_t write(int fd,const void *data,size_t size) {
    if(!next_write) { errno=ENOSYS;return -1; }
    if(enabled && size) {
        struct stat st;if(!fstat(fd,&st) && S_ISREG(st.st_mode)) {
            off_t pos=lseek(fd,0,SEEK_CUR);int flags=fcntl(fd,F_GETFL);if(flags>=0 && (flags&O_APPEND))pos=st.st_size;
            if(pos>=0) { if((uint64_t)pos>=ceiling) { errno=EFBIG;return -1; }if(size>ceiling-(uint64_t)pos)size=(size_t)(ceiling-(uint64_t)pos); }
        }
    }
    return next_write(fd,data,size);
}

/* Only the storage layer's directory sync fails; SQLite uses its normal VFS. */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail_directory_sync;
static int test_sync(int fd) {
    struct stat st;
    if(fail_directory_sync && fstat(fd,&st)==0 && S_ISDIR(st.st_mode)) {
        errno=EIO;
        return -1;
    }
    return fsync(fd);
}

#define fsync test_sync
#include "../src/storage_sqlite.c"
#undef fsync

int main(int argc,char **argv) {
    assert(argc==2);
    fail_directory_sync=1;
    assert(!storage_init(argv[1]));
    assert(storage_database()==NULL);
    fail_directory_sync=0;
    assert(storage_init(argv[1]));
    assert(storage_database()!=NULL);
    storage_shutdown();
    assert(storage_database()==NULL);
    return 0;
}

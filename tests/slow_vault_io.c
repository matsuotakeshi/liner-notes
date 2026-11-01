#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
static ssize_t (*original_read)(int,void *,size_t);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void initialize(void) { *(void **)(&original_read)=dlsym(RTLD_NEXT,"read"); }
ssize_t read(int fd,void *buf,size_t size) {
    pthread_once(&once,initialize);
    const char *inode=getenv("SLOW_VAULT_INODE"),*gate=getenv("SLOW_VAULT_GATE"),*marker=getenv("SLOW_VAULT_MARKER");
    struct stat st;
    if(inode && gate && marker && !fstat(fd,&st) && (unsigned long long)st.st_ino==strtoull(inode,NULL,10)) {
        int mark=open(marker,O_WRONLY|O_CREAT,0600);if(mark>=0)close(mark);
        struct timespec delay={0,10000000};while(!access(gate,F_OK))nanosleep(&delay,NULL);
    }
    return original_read(fd,buf,size);
}

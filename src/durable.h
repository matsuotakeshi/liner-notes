#include "storage_sqlite.h"
#ifndef DURABLE_H
#define DURABLE_H
/* Independent append journals. Sync before acknowledgement; refuse damaged tails. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define DJ_OPEN(p) _open(p,_O_RDWR|_O_CREAT|_O_BINARY,_S_IREAD|_S_IWRITE)
#define DJ_READ _read
#define DJ_WRITE _write
#define DJ_CLOSE _close
#define DJ_SYNC _commit
#define DJ_SEEK _lseeki64
#else
#include <unistd.h>
#define DJ_OPEN(p) storage_open(p,O_RDWR|O_CREAT|O_NOFOLLOW,0600)
#define DJ_READ storage_read
#define DJ_WRITE storage_write
#define DJ_CLOSE storage_close
#define DJ_SYNC storage_sync
#define DJ_SEEK storage_seek
#endif
#define DJ_TEXT 3000
#define DJ_ROW 4096
typedef struct { int fd,broken;uint32_t sequence; } Durable;
typedef struct { char kind;uint32_t sequence,actor,target;uint64_t time;char text[DJ_TEXT+1]; } DurableRow;
static inline uint32_t dj_get(const unsigned char *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline void dj_put(unsigned char *p,uint32_t v) { for(int i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i)); }
static inline uint32_t dj_sum(const unsigned char *p) { uint32_t h=2166136261u;for(int i=0;i<DJ_ROW-4;i++)h=(h^p[i])*16777619u;return h; }
static inline int dj_io(int fd,unsigned char *p,size_t n,int writing) {
    size_t done=0;while(done<n) { int k=writing?(int)DJ_WRITE(fd,p+done,(unsigned)(n-done)):(int)DJ_READ(fd,p+done,(unsigned)(n-done));if(k<0)return -1;if(!k)break;done+=(size_t)k; }return (int)done;
}
static inline void dj_close(Durable *j) { if(j->fd>=0)DJ_CLOSE(j->fd);j->fd=-1; }
static inline int dj_open(Durable *j,const char *path,const char magic[8],int (*apply)(void *,const DurableRow *),void *ctx) {
    j->fd=DJ_OPEN(path);j->sequence=0;j->broken=0;if(j->fd<0)return 0;
#ifdef _WIN32
    OVERLAPPED ov={0};if(!LockFileEx((HANDLE)_get_osfhandle(j->fd),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&ov))goto fail;
#else
    struct stat st;if(storage_fstat(j->fd,&st) || !S_ISREG(st.st_mode))goto fail;
    struct flock lock={0};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;if(storage_lock(j->fd,&lock))goto fail;
    char dir[4096];if(strlen(path)>=sizeof(dir))goto fail;strcpy(dir,path);char *slash=strrchr(dir,'/');if(slash) { if(slash==dir)slash[1]=0;else *slash=0; }else strcpy(dir,".");
    int d=open(dir,O_RDONLY);if(d<0)goto fail;int e=storage_sync(d);storage_close(d);if(e)goto fail;
#endif
    unsigned char row[DJ_ROW]={0};int n=dj_io(j->fd,row,8,0);
    if(!n) { memcpy(row,magic,8);if(dj_io(j->fd,row,8,1)!=8 || DJ_SYNC(j->fd))goto fail; }
    else if(n!=8 || memcmp(row,magic,8))goto fail;
    while((n=dj_io(j->fd,row,sizeof(row),0))!=0) {
        uint32_t len=dj_get(row+24);if(n!=DJ_ROW || row[1] || row[2] || row[3] || len>DJ_TEXT || memchr(row+28,0,len) || dj_sum(row)!=dj_get(row+DJ_ROW-4) || j->sequence==UINT32_MAX || dj_get(row+4)!=j->sequence+1)goto fail;
        for(size_t i=28+len;i<DJ_ROW-4;i++)if(row[i])goto fail;
        DurableRow item={0};item.kind=(char)row[0];item.sequence=dj_get(row+4);item.actor=dj_get(row+8);item.target=dj_get(row+12);item.time=dj_get(row+16)|((uint64_t)dj_get(row+20)<<32);memcpy(item.text,row+28,len);
        if(!apply(ctx,&item))goto fail;
        j->sequence++;
    }
    return 1;
fail:dj_close(j);return 0;
}
static inline int dj_append(Durable *j,DurableRow *item) {
    size_t n=strlen(item->text);if(j->fd<0 || j->broken || j->sequence==UINT32_MAX || n>DJ_TEXT)return 0;
    unsigned char row[DJ_ROW]={0};row[0]=(unsigned char)item->kind;item->sequence=j->sequence+1;time_t now=time(NULL);item->time=now>0?(uint64_t)now:0;
    dj_put(row+4,item->sequence);dj_put(row+8,item->actor);dj_put(row+12,item->target);dj_put(row+16,(uint32_t)item->time);dj_put(row+20,(uint32_t)(item->time>>32));dj_put(row+24,(uint32_t)n);memcpy(row+28,item->text,n);dj_put(row+DJ_ROW-4,dj_sum(row));
    if(DJ_SEEK(j->fd,0,SEEK_END)<0 || dj_io(j->fd,row,sizeof(row),1)!=DJ_ROW || DJ_SYNC(j->fd)) { j->broken=1;return 0; }
    j->sequence++;return 1;
}
#endif

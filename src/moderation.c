#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "app_limits.h"
#include "moderation.h"
#include "store_limits.h"
#include "members.h"
#include "catalog.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define OPEN(p) _open(p,_O_RDWR|_O_CREAT|_O_BINARY,_S_IREAD|_S_IWRITE)
#define READ _read
#define WRITE _write
#define CLOSE _close
#define SYNC _commit
#define SEEK _lseeki64
#else
#include <unistd.h>
#define OPEN(p) storage_open(p,O_RDWR|O_CREAT,0600)
#define READ storage_read
#define WRITE storage_write
#define CLOSE storage_close
#define SYNC storage_sync
#define SEEK storage_seek
#endif
static unsigned char admins[(MEMBER_MAX+1)],*hidden,suspended[(MEMBER_MAX+1)];
static size_t hidden_capacity;
static uint32_t epochs[(MEMBER_MAX+1)];
static int fd=-1,broken;
static uint32_t sequence;
static uint32_t (*article_board)(uint32_t);
static int (*article_history)(uint32_t,uint32_t);
static uint32_t get32(const unsigned char *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(unsigned char *p,uint32_t n) { for(int i=0;i<4;i++) p[i]=(unsigned char)(n>>(8*i)); }
static uint32_t sum(const unsigned char *p) { uint32_t h=2166136261u; for(int i=0;i<28;i++) h=(h^p[i])*16777619u; return h; }
int moderation_admins(const char *path) {
    memset(admins,0,sizeof(admins));
    if(!path) return 1;
    if(!members_enabled()) return 0;
    FILE *f=fopen(path,"rb"); if(!f) return 0;
    char line[128]; int ok=1;
    while(fgets(line,sizeof(line),f)) {
        if(!strchr(line,'\n') && !feof(f)) { ok=0; break; }
        line[strcspn(line,"\r\n")]=0;
        if(!*line || *line=='#') continue;
        uint32_t user=0;
        for(const char *p=line;*p;p++) {
            if(*p<'0' || *p>'9') { ok=0; break; }
            user=user*10+(unsigned)(*p-'0'); if(user>MEMBER_MAX) { ok=0; break; }
        }
        if(!ok || !members_name(user) || admins[user]) { ok=0; break; }
        admins[user]=1;
    }
    if(ferror(f)) ok=0;
    fclose(f); if(!ok) memset(admins,0,sizeof(admins)); return ok;
}
int moderation_admin(uint32_t user) { return user && user<=MEMBER_MAX && admins[user] && members_active(user); }
int moderation_protected(uint32_t user) { return user && user<=MEMBER_MAX && admins[user]; }
int moderation_suspended(uint32_t user) { return user && user<=MEMBER_MAX && (suspended[user] || members_pending(user) || !members_active(user)); }
uint32_t moderation_epoch(uint32_t user) { return user && user<=MEMBER_MAX?epochs[user]:0; }
int moderation_hidden(uint32_t id) { return id && id<hidden_capacity && hidden[id]; }
int moderation_broken(void) { return broken; }
void moderation_close(void) { if(fd>=0) CLOSE(fd); fd=-1;free(hidden);hidden=NULL;hidden_capacity=0; }
static int write_all(const unsigned char *p,size_t n) {
    while(n) { int k=(int)WRITE(fd,p,(unsigned)n); if(k<=0) return 0; p+=k; n-=(size_t)k; } return 1;
}
static int read_all(unsigned char *p,size_t n) {
    size_t done=0;
    while(done<n) { int k=(int)READ(fd,p+done,(unsigned)(n-done)); if(k<0) return -1; if(!k) break; done+=(size_t)k; }
    return (int)done;
}
int moderation_open(const char *board_path,uint32_t count,uint32_t (*board_of)(uint32_t),int (*was_on)(uint32_t,uint32_t)) {
    char path[4096];
    int size=snprintf(path,sizeof(path),"%s.moderation",board_path);
    if(size<0 || (size_t)size>=sizeof(path)) return 0;
    article_board=board_of;article_history=was_on;
    free(hidden);hidden=NULL;hidden_capacity=0;
    if(!store_reserve((void **)&hidden,&hidden_capacity,(size_t)count+1,1))return 0;
    memset(suspended,0,sizeof(suspended)); memset(epochs,0,sizeof(epochs)); broken=0; sequence=0;
    fd=OPEN(path); if(fd<0) return 0;
#ifdef _WIN32
    OVERLAPPED ov={0};
    if(!LockFileEx((HANDLE)_get_osfhandle(fd),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&ov)) goto fail;
#else
    struct flock lock={0}; lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET;
    if(storage_lock(fd,&lock)) goto fail;
    char dir[4096]; strcpy(dir,path); char *slash=strrchr(dir,'/');
    if(slash) { if(slash==dir) slash[1]=0; else *slash=0; } else strcpy(dir,".");
    int d=open(dir,O_RDONLY); if(d<0) goto fail;
    int e=storage_sync(d); storage_close(d); if(e) goto fail;
#endif
    unsigned char row[32]; int n=read_all(row,8);
    if(!n) { if(!write_all((const unsigned char *)"RTMOD001",8) || SYNC(fd)) goto fail; }
    else if(n!=8 || memcmp(row,"RTMOD001",8)) goto fail;
    while((n=read_all(row,sizeof(row)))!=0) {
        if(n!=32) goto fail;
        uint32_t id=get32(row+8),actor=get32(row+16);
        uint64_t stamp=get32(row+20)|((uint64_t)get32(row+24)<<32);
        if(sum(row)!=get32(row+28) || sequence==UINT32_MAX || get32(row+4)!=sequence+1 ||
           row[1]>1 || row[2] || row[3] || !members_name(actor) || stamp>253402300799ULL) goto fail;
        if(row[0]=='M') {
            if(!id || id>count || !article_history(id,get32(row+12))) goto fail;
            hidden[id]=row[1];
        } else if(row[0]=='K') {
            if(!members_name(id) || get32(row+12) || id==actor || row[1]!=1)goto fail;
        } else if(row[0]=='S') {
            if(!members_name(id) || get32(row+12)!=0 || id==actor) goto fail;
            suspended[id]=row[1]; epochs[id]++;
        } else goto fail;
        sequence++;
    }
    for(uint32_t user=1;user<=MEMBER_MAX;user++) if(admins[user] && suspended[user]) goto fail;
    return 1;
fail:
    moderation_close(); return 0;
}
int moderation_set(uint32_t id,uint32_t board,uint32_t actor,int hide) {
    if(fd<0 || broken || !id || id>CONTENT_ID_MAX || !catalog_manage(board,actor)) return 0;
    if(!store_reserve((void **)&hidden,&hidden_capacity,(size_t)id+1,1))return 0;
    if(hidden[id]==!!hide) return 1;
    if(sequence==UINT32_MAX) { broken=1; return 0; }
    unsigned char row[32]={0}; row[0]='M'; row[1]=(unsigned char)!!hide;
    put32(row+4,sequence+1); put32(row+8,id); put32(row+12,board); put32(row+16,actor);
    time_t clock_now=time(NULL); uint64_t stamp=clock_now>0?(uint64_t)clock_now:0;
    put32(row+20,(uint32_t)stamp); put32(row+24,(uint32_t)(stamp>>32)); put32(row+28,sum(row));
    if(SEEK(fd,0,SEEK_END)<0 || !write_all(row,sizeof(row)) || SYNC(fd)) { broken=1; return 0; }
    sequence++; hidden[id]=(unsigned char)!!hide; return 1;
}

int moderation_suspend(uint32_t user,uint32_t actor,int suspend) {
    if(fd<0 || broken || !members_name(user) || !moderation_admin(actor) || moderation_suspended(actor) || user==actor || moderation_protected(user)) return 0;
    if(suspended[user]==!!suspend) return 1;
    if(sequence==UINT32_MAX) { broken=1; return 0; }
    unsigned char row[32]={0}; row[0]='S'; row[1]=(unsigned char)!!suspend;
    put32(row+4,sequence+1); put32(row+8,user); put32(row+16,actor);
    time_t clock_now=time(NULL); uint64_t stamp=clock_now>0?(uint64_t)clock_now:0;
    put32(row+20,(uint32_t)stamp); put32(row+24,(uint32_t)(stamp>>32)); put32(row+28,sum(row));
    if(SEEK(fd,0,SEEK_END)<0 || !write_all(row,sizeof(row)) || SYNC(fd)) { broken=1; return 0; }
    sequence++; suspended[user]=(unsigned char)!!suspend; epochs[user]++; return 1;
}

uint32_t moderation_count(void) { return sequence; }
int moderation_entry(uint32_t number,ModerationEntry *entry) {
    if(fd<0 || broken || !number || number>sequence) return 0;
    unsigned char row[32];
    int64_t offset=8+((int64_t)number-1)*32;
    if(SEEK(fd,offset,SEEK_SET)<0 || read_all(row,sizeof(row))!=32 ||
       sum(row)!=get32(row+28) || get32(row+4)!=number || row[1]>1 || row[2] || row[3]) goto fail;
    entry->sequence=number; entry->kind=(char)row[0]; entry->enabled=row[1];
    entry->target=get32(row+8); entry->board=get32(row+12); entry->actor=get32(row+16);
    entry->time=get32(row+20)|((uint64_t)get32(row+24)<<32);
    if(!members_name(entry->actor) || entry->time>253402300799ULL) goto fail;
    if(entry->kind=='M') {
        if(!entry->target || entry->target>CONTENT_ID_MAX || !entry->board || !article_history(entry->target,entry->board)) goto fail;
        entry->board=article_board(entry->target);
    } else if(entry->kind=='K') {
        if(!members_name(entry->target) || entry->board || entry->target==entry->actor || entry->enabled!=1)goto fail;
    } else if(entry->kind=='S') {
        if(!members_name(entry->target) || entry->board || entry->target==entry->actor) goto fail;
    } else goto fail;
    return 1;
fail:
    broken=1; return 0;
}

void moderation_credentials_changed(uint32_t user) { if(user && user<=MEMBER_MAX) epochs[user]++; }

int moderation_chat_kick(uint32_t user,uint32_t actor) {
    if(fd<0 || broken || !members_name(user) || !moderation_admin(actor) || moderation_suspended(actor) || user==actor || moderation_protected(user))return 0;
    if(sequence==UINT32_MAX) { broken=1;return 0; }
    unsigned char row[32]={0};row[0]='K';row[1]=1;
    put32(row+4,sequence+1);put32(row+8,user);put32(row+16,actor);
    time_t now=time(NULL);uint64_t stamp=now>0?(uint64_t)now:0;
    put32(row+20,(uint32_t)stamp);put32(row+24,(uint32_t)(stamp>>32));put32(row+28,sum(row));
    if(SEEK(fd,0,SEEK_END)<0 || !write_all(row,sizeof(row)) || SYNC(fd)) { broken=1;return 0; }
    sequence++;return 1;
}

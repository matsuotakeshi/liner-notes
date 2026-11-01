#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "app_limits.h"
#include "extras.h"
#include "store_limits.h"
#include "members.h"
#include <stdio.h>
#include <stdlib.h>
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

static int fd=-1,broken;
static uint32_t sequence,article_limit;
typedef struct { uint32_t global,record; } AuditIndex;
#include "db_index.h"
static DbIndex audit_db,extra_db;
static AuditIndex *audit_at(uint32_t id) { if(!audit_db.sql)db_open(&audit_db,NULL,"",sizeof(AuditIndex));return db_at(&audit_db,id); }
static uint32_t audit_count;

static int audit_reserve(void) { return 1; }
uint32_t extras_audit_count(void) { return audit_count; }
typedef struct Extra { unsigned char removed;char title[81],author[33]; } Extra;
static Extra *extra_at(uint32_t id) { if(!extra_db.sql)db_open(&extra_db,NULL,"",sizeof(Extra));return db_at(&extra_db,id); }
static size_t extra_capacity;
#include "db_index.h"
typedef struct { char handle[81],bio[321],fields[5][321]; } LegacyProfile;
static DbIndex legacy_db;
static LegacyProfile *legacy(uint32_t u) { return db_at(&legacy_db,u); }
static const char field_keys[]="ROIWK";

const char *extras_field(uint32_t user,unsigned field) { return user && user<=MEMBER_MAX && field<5?legacy(user)->fields[field]:""; }
int extras_profile_match(uint32_t user,const char *query) { for(unsigned i=0;i<5;i++)if(strstr(extras_field(user,i),query))return 1;return 0; }
static uint32_t get32(const unsigned char *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(unsigned char *p,uint32_t n) { for(int i=0;i<4;i++) p[i]=(unsigned char)(n>>(8*i)); }
static uint32_t sum(const unsigned char *p) { uint32_t h=2166136261u; for(int i=0;i<508;i++) h=(h^p[i])*16777619u; return h; }
static int write_all(const unsigned char *p,size_t n) {
    while(n) { int k=(int)WRITE(fd,p,(unsigned)n); if(k<=0) return 0; p+=k; n-=(size_t)k; } return 1;
}
static int read_all(unsigned char *p,size_t n) {
    size_t done=0;
    while(done<n) { int k=(int)READ(fd,p+done,(unsigned)(n-done)); if(k<0) return -1; if(!k) break; done+=(size_t)k; }
    return (int)done;
}
void extras_close(void) { db_close(&legacy_db); if(fd>=0) CLOSE(fd); fd=-1; db_close(&audit_db);db_close(&extra_db);audit_count=0;extra_capacity=0; }
int extras_descriptor(void) { return fd; }
int extras_broken(void) { return broken; }
int extras_removed(uint32_t id) { return id && id<extra_capacity && (*extra_at(id)).removed; }
const char *extras_title(uint32_t id) { return id && id<extra_capacity?(*extra_at(id)).title:""; }
const char *extras_author(uint32_t id) { return id && id<extra_capacity?(*extra_at(id)).author:""; }
const char *extras_handle(uint32_t id) { return id && id<=MEMBER_MAX?legacy(id)->handle:""; }
const char *extras_bio(uint32_t id) { return id && id<=MEMBER_MAX?legacy(id)->bio:""; }
static int valid(char kind,uint32_t id,uint32_t actor,const char *text,uint32_t limit) {
    if(!members_name(actor)) return 0;
    size_t n=strlen(text);
    for(const unsigned char *p=(const unsigned char *)text;*p;p++) if(*p<32 || *p==127) return 0;
    if(kind=='A') return id && id<=limit && n>0 && n<=32 && members_find(text);
    if(kind=='D' || kind=='T') return id && id<=limit && (kind=='D'?n==0:(n>0 && n<=80 && !extras_removed(id)));
    if(kind && strchr(field_keys,kind) && id==actor && members_name(id))return n<=320;
    if((kind=='H' || kind=='B') && id==actor && members_name(id)) return n<=(kind=='H'?80:320);
    return 0;
}
static void apply(char kind,uint32_t id,const char *text) {
    if(kind=='D') (*extra_at(id)).removed=1;
    else if(kind=='A') strcpy((*extra_at(id)).author,text);
    else if(kind=='T') strcpy((*extra_at(id)).title,text);
    else if(kind=='H') strcpy(legacy(id)->handle,text);
    else if(kind=='B')strcpy(legacy(id)->bio,text);
    else { const char *key=strchr(field_keys,kind);if(key)strcpy(legacy(id)->fields[key-field_keys],text); }
}
int extras_open(const char *board_path,uint32_t count) {
    char path[4096]; int size=snprintf(path,sizeof(path),"%s.extras",board_path);
    if(size<0 || (size_t)size>=sizeof(path)) return 0;
    db_close(&extra_db);extra_capacity=0;
    extra_capacity=(size_t)count+1;
    db_close(&legacy_db);db_open(&legacy_db,board_path,"legacy-profile-index",sizeof(LegacyProfile)); broken=0; sequence=0; article_limit=count;
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
    unsigned char row[512]; int n=read_all(row,8);
    if(!n) { if(!write_all((const unsigned char *)"RTEXTRA1",8) || SYNC(fd)) goto fail; }
    else if(n!=8 || memcmp(row,"RTEXTRA1",8)) goto fail;
    while((n=read_all(row,sizeof(row)))!=0) {
        if(n!=512 || sum(row)!=get32(row+508) || row[1]>1 || row[2] || row[3] || (row[1] && row[0]!='D' && row[0]!='T' && row[0]!='A')) goto fail;
        uint32_t seq=get32(row+4),id=get32(row+8),actor=get32(row+12),len=get32(row+16);
        if(sequence==UINT32_MAX || seq!=sequence+1 || len>320 || memchr(row+20,0,len)) goto fail;
        for(size_t i=20+len;i<508;i++) if((!row[1] || i<340 || i>=352) && row[i]) goto fail;
        if(row[0]=='D' || row[0]=='T' || row[0]=='A') {
            uint32_t anchor=row[1]?get32(row+348):0;
            if(anchor>moderation_count() || audit_count>=UINT32_MAX-moderation_count() ||
               (audit_count && anchor<(*audit_at(audit_count-1)).global-audit_count) || !audit_reserve()) goto fail;
            (*audit_at(audit_count))=(AuditIndex){anchor+audit_count+1,seq}; audit_count++;
        }
        char text[321]; memcpy(text,row+20,len); text[len]=0;
        if(!valid((char)row[0],id,actor,text,article_limit)) goto fail;
        apply((char)row[0],id,text); sequence++;
    }
    return 1;
fail:
    extras_close(); return 0;
}
int extras_set(char kind,uint32_t id,uint32_t actor,const char *text) {
    if(fd<0 || broken || !valid(kind,id,actor,text,CONTENT_ID_MAX)) return 0;
    if((kind=='D' || kind=='T' || kind=='A') && extra_capacity<=(size_t)id)extra_capacity=(size_t)id+1;
    if(kind=='D' && (*extra_at(id)).removed) return 1;
    if(sequence==UINT32_MAX) { broken=1; return 0; }
    int event=kind=='D' || kind=='T' || kind=='A';
    if(event && (audit_count>=UINT32_MAX-moderation_count() || !audit_reserve())) { broken=1; return 0; }
    unsigned char row[512]={0}; row[0]=(unsigned char)kind;
    if(event) {
        time_t now=time(NULL); uint64_t stamp=now>0?(uint64_t)now:0;
        row[1]=1; put32(row+340,(uint32_t)stamp); put32(row+344,(uint32_t)(stamp>>32));
        put32(row+348,moderation_count());
    }
    put32(row+4,sequence+1); put32(row+8,id); put32(row+12,actor); put32(row+16,(uint32_t)strlen(text));
    memcpy(row+20,text,strlen(text)); put32(row+508,sum(row));
    if(SEEK(fd,0,SEEK_END)<0 || !write_all(row,sizeof(row)) || SYNC(fd)) { broken=1; return 0; }
    apply(kind,id,text); sequence++;
    if(event) { (*audit_at(audit_count))=(AuditIndex){moderation_count()+audit_count+1,sequence}; audit_count++; }
    return 1;
}

int extras_audit_entry(uint32_t global,ModerationEntry *entry,uint32_t *moderation_sequence) {
    uint32_t lo=0,hi=audit_count;
    while(lo<hi) { uint32_t mid=lo+(hi-lo)/2; if((*audit_at(mid)).global<global) lo=mid+1; else hi=mid; }
    *moderation_sequence=global-lo;
    if(lo==audit_count || (*audit_at(lo)).global!=global) return 1;
    unsigned char row[512];
    if(fd<0 || broken || SEEK(fd,8+(int64_t)((*audit_at(lo)).record-1)*512,SEEK_SET)<0 ||
       read_all(row,sizeof(row))!=512 || sum(row)!=get32(row+508) || get32(row+4)!=(*audit_at(lo)).record ||
       (row[0]!='D' && row[0]!='T' && row[0]!='A')) { broken=1; return 0; }
    memset(entry,0,sizeof(*entry)); entry->sequence=global; entry->kind=(char)row[0];
    entry->target=get32(row+8); entry->actor=get32(row+12);
    entry->time=row[1]?((uint64_t)get32(row+344)<<32)|get32(row+340):0;
    *moderation_sequence=0; return 1;
}

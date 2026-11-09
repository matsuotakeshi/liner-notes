#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "storage_sqlite.h"
#include <sqlite3.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <limits.h>

#define BLOCK 4096
typedef struct Store {
    int fd,dirty,readonly,broken;sqlite3 *db;sqlite3_int64 id,length,position;
    int kind;sqlite3_int64 committed,row_at;size_t row_bytes;unsigned char *row_cache;
    sqlite3_stmt *get_block,*put_block,*get_length;
    struct Store *next;
} Store;
static char database[PATH_MAX],directory[PATH_MAX];
static int owner=-1;
static Store *handles;
static pthread_mutex_t registry=PTHREAD_MUTEX_INITIALIZER;
const char *storage_database(void) { return owner>=0?database:NULL; }
static int sql(sqlite3 *db,const char *text) { return sqlite3_exec(db,text,NULL,NULL,NULL)==SQLITE_OK; }
#include "storage_records.inc"
static int rows_flush(Store *s);
static int connection(sqlite3 **db) {
    if(sqlite3_open_v2(database,db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,NULL)!=SQLITE_OK)return 0;
    sqlite3_busy_timeout(*db,5000);
    return sql(*db,"PRAGMA synchronous=FULL;PRAGMA foreign_keys=ON;PRAGMA cache_size=-512;");
}
static Store *lookup(int fd) {
    pthread_mutex_lock(&registry);Store *s=handles;while(s && s->fd!=fd)s=s->next;pthread_mutex_unlock(&registry);return s;
}
int storage_lock(int fd,struct flock *lock) { return lookup(fd)?0:fcntl(fd,F_SETLK,lock); }
static int key_for(const char *path,char out[PATH_MAX]) {
    char full[PATH_MAX];
    if(*path=='/') { if(strlen(path)>=sizeof(full))return 0;strcpy(full,path); }
    else { if(!getcwd(full,sizeof(full)))return 0;size_t n=strlen(full);if(snprintf(full+n,sizeof(full)-n,"/%s",path)>=(int)(sizeof(full)-n))return 0; }
    /* Normalize components without following symlinks or requiring a new file. */
    char normalized[PATH_MAX]="",copy[PATH_MAX];strcpy(copy,full);char *save=NULL;
    for(char *p=strtok_r(copy,"/",&save);p;p=strtok_r(NULL,"/",&save)) {
        if(!strcmp(p,"."))continue;
        if(!strcmp(p,"..")) { char *last=strrchr(normalized,'/');if(!last)return 0;*last=0;continue; }
        size_t n=strlen(normalized);if(snprintf(normalized+n,sizeof(normalized)-n,"/%s",p)>=(int)(sizeof(normalized)-n))return 0;
    }
    size_t n=strlen(directory);
    if(!strncmp(normalized,directory,n) && normalized[n]=='/')strcpy(out,normalized+n+1);
    else if(snprintf(out,PATH_MAX,"external%s",normalized)>=PATH_MAX)return 0;
    return 1;
}
int storage_init_structured(const char *path) { structured_requested=1;int ok=storage_init(path);structured_requested=0;return ok; }
int storage_init(const char *path) {
    if(owner>=0 || !path || !*path)return 0;
    struct stat st;if(!lstat(path,&st) && (!S_ISREG(st.st_mode) || st.st_nlink!=1))return 0;
    char absolute[PATH_MAX];if(*path=='/') { if(strlen(path)>=sizeof(absolute))return 0;strcpy(absolute,path); }
    else { if(!getcwd(absolute,sizeof(absolute)))return 0;size_t n=strlen(absolute);if(snprintf(absolute+n,sizeof(absolute)-n,"/%s",path)>=(int)(sizeof(absolute)-n))return 0; }
    strcpy(directory,absolute);char *slash=strrchr(directory,'/');if(!slash)return 0;
    char filename[PATH_MAX];strcpy(filename,slash+1);if(!*filename)return 0;
    if(slash==directory)slash[1]=0;else *slash=0;
    char *canonical=realpath(directory,NULL);if(!canonical)return 0;
    if(strlen(canonical)>=sizeof(directory)) { free(canonical);return 0; }strcpy(directory,canonical);free(canonical);
    if(snprintf(database,sizeof(database),"%s/%s",!strcmp(directory,"/")?"":directory,filename)>=(int)sizeof(database))return 0;
    char lock[PATH_MAX];if(snprintf(lock,sizeof(lock),"%s.lock",database)>=(int)sizeof(lock))return 0;
    owner=open(lock,O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(owner<0)return 0;
    struct flock instance_lock={0};instance_lock.l_type=F_WRLCK;instance_lock.l_whence=SEEK_SET;
    if(fstat(owner,&st) || !S_ISREG(st.st_mode) || st.st_nlink!=1 || fcntl(owner,F_SETLK,&instance_lock)) { close(owner);owner=-1;return 0; }
    int is_new=lstat(database,&st)!=0;
    int initial=open(database,O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(initial<0 || fchmod(initial,0600)) { if(initial>=0)close(initial);close(owner);owner=-1;return 0; }close(initial);
    sqlite3 *db=NULL;int ok=connection(&db) && sql(db,"PRAGMA journal_mode=WAL;BEGIN IMMEDIATE;CREATE TABLE IF NOT EXISTS storage_meta(id INTEGER PRIMARY KEY CHECK(id=1),version INTEGER NOT NULL,uuid BLOB NOT NULL);INSERT OR IGNORE INTO storage_meta VALUES(1,1,randomblob(16));CREATE TABLE IF NOT EXISTS streams(id INTEGER PRIMARY KEY,name TEXT UNIQUE NOT NULL,length INTEGER NOT NULL DEFAULT 0,revision INTEGER NOT NULL DEFAULT 0);CREATE TABLE IF NOT EXISTS blocks(stream INTEGER NOT NULL REFERENCES streams(id) ON DELETE CASCADE,number INTEGER NOT NULL,value BLOB NOT NULL CHECK(length(value)=4096),PRIMARY KEY(stream,number)) WITHOUT ROWID;COMMIT;");
    if(ok && structured_requested && is_new)ok=sql(db,"UPDATE storage_meta SET version=3 WHERE id=1");
    sqlite3_stmt *q=NULL;if(ok) { ok=sqlite3_prepare_v2(db,"SELECT version FROM storage_meta WHERE id=1",-1,&q,NULL)==SQLITE_OK && sqlite3_step(q)==SQLITE_ROW;if(ok) { storage_version=sqlite3_column_int(q,0);ok=(storage_version>=1 && storage_version<=3) && (!structured_requested || storage_version>=2); } }sqlite3_finalize(q);
    if(ok && storage_version>=2)ok=row_schema(db);
    if(db)sqlite3_close(db);
    if(!ok || chmod(database,0600)) { close(owner);owner=-1;database[0]=0;return 0; }
    int d=open(directory,O_RDONLY);ok=d>=0 && fsync(d)==0;if(d>=0)close(d);return ok;
}
static int length_refresh(Store *s) {
    if(!s->get_length && sqlite3_prepare_v2(s->db,"SELECT length FROM streams WHERE id=?",-1,&s->get_length,NULL)!=SQLITE_OK)return 0;
    sqlite3_stmt *q=s->get_length;
    int ok=1;
    if(ok) { sqlite3_bind_int64(q,1,s->id);ok=sqlite3_step(q)==SQLITE_ROW;if(ok)s->length=sqlite3_column_int64(q,0); }
    sqlite3_reset(q);sqlite3_clear_bindings(q);return ok;
}
static int block_read(Store *s,sqlite3_int64 number,unsigned char data[BLOCK]) {
    if(!s->get_block && sqlite3_prepare_v2(s->db,"SELECT value FROM blocks WHERE stream=? AND number=?",-1,&s->get_block,NULL)!=SQLITE_OK)return 0;
    sqlite3_stmt *q=s->get_block;sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,number);int rc=sqlite3_step(q);
    int ok=rc==SQLITE_ROW && sqlite3_column_bytes(q,0)==BLOCK;
    if(ok)memcpy(data,sqlite3_column_blob(q,0),BLOCK);
    sqlite3_reset(q);sqlite3_clear_bindings(q);
    if(rc==SQLITE_DONE) { if(s->kind)return row_block(s,number,data);memset(data,0,BLOCK);ok=number*BLOCK>=s->length; }
    return ok;
}
static int block_write(Store *s,sqlite3_int64 number,const unsigned char data[BLOCK]) {
    if(!s->put_block && sqlite3_prepare_v2(s->db,"INSERT INTO blocks VALUES(?,?,?) ON CONFLICT(stream,number) DO UPDATE SET value=excluded.value",-1,&s->put_block,NULL)!=SQLITE_OK)return 0;
    sqlite3_stmt *q=s->put_block;sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int64(q,2,number);sqlite3_bind_blob(q,3,data,BLOCK,SQLITE_STATIC);int ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_reset(q);sqlite3_clear_bindings(q);return ok;
}
ssize_t storage_write(int fd,const void *data,size_t bytes) {
    Store *s=lookup(fd);if(!s)return write(fd,data,bytes);
    if(s->broken || s->readonly || bytes>INT64_MAX || s->position>INT64_MAX-(sqlite3_int64)bytes || s->position>s->length) { errno=EIO;return -1; }
    if(!s->dirty) { if(!sql(s->db,"BEGIN IMMEDIATE") || !length_refresh(s)) { s->broken=1;errno=EIO;return -1; }s->dirty=1;s->committed=s->length; }
    if(s->kind && s->position<s->committed && !(s->position==0 && bytes==8)) { s->broken=1;errno=EIO;return -1; }
    size_t done=0;unsigned char block[BLOCK];
    while(done<bytes) {
        size_t at=(size_t)(s->position%BLOCK),n=BLOCK-at;if(n>bytes-done)n=bytes-done;
        if(!block_read(s,s->position/BLOCK,block))goto fail;
        memcpy(block+at,(const unsigned char *)data+done,n);if(!block_write(s,s->position/BLOCK,block))goto fail;
        done+=n;s->position+=(sqlite3_int64)n;if(s->position>s->length)s->length=s->position;
    }
    return (ssize_t)done;
fail:s->broken=1;errno=EIO;return -1;
}
int storage_sync(int fd) {
    Store *s=lookup(fd);if(!s)return fsync(fd);if(s->broken) { errno=EIO;return -1; }if(!s->dirty)return 0;
    sqlite3_stmt *q=NULL;int ok=rows_flush(s) && sqlite3_prepare_v2(s->db,"UPDATE streams SET length=?,revision=revision+1 WHERE id=?",-1,&q,NULL)==SQLITE_OK;
    if(ok) { sqlite3_bind_int64(q,1,s->length);sqlite3_bind_int64(q,2,s->id);ok=sqlite3_step(q)==SQLITE_DONE; }sqlite3_finalize(q);
    if(!ok || !sql(s->db,"COMMIT")) { sql(s->db,"ROLLBACK");s->broken=1;errno=EIO;return -1; }s->dirty=0;s->committed=s->length;s->row_bytes=0;return 0;
}
ssize_t storage_read(int fd,void *data,size_t bytes) {
    Store *s=lookup(fd);if(!s)return read(fd,data,bytes);if(s->broken || (!s->dirty && !length_refresh(s))) { errno=EIO;return -1; }
    if(s->position>=s->length)return 0;
    if(bytes>(uint64_t)(s->length-s->position))bytes=(size_t)(s->length-s->position);
    size_t done=0;unsigned char block[BLOCK];while(done<bytes) {
        size_t at=(size_t)(s->position%BLOCK),n=BLOCK-at;if(n>bytes-done)n=bytes-done;
        if(!block_read(s,s->position/BLOCK,block)) { s->broken=1;errno=EIO;return -1; }
        memcpy((unsigned char *)data+done,block+at,n);done+=n;s->position+=(sqlite3_int64)n;
    }return (ssize_t)done;
}
off_t storage_seek(int fd,off_t offset,int whence) {
    Store *s=lookup(fd);if(!s)return lseek(fd,offset,whence);
    if(whence==SEEK_END && !s->dirty && !length_refresh(s))return -1;
    sqlite3_int64 base=whence==SEEK_SET?0:whence==SEEK_CUR?s->position:whence==SEEK_END?s->length:-1;
    if(base<0 || offset < -base || (offset>0 && base>INT64_MAX-offset)) { errno=EINVAL;return -1; }
    s->position=base+offset;return (off_t)s->position;
}
int storage_fstat(int fd,struct stat *st) {
    Store *s=lookup(fd);if(!s)return fstat(fd,st);if(!s->dirty && !length_refresh(s))return -1;
    memset(st,0,sizeof(*st));st->st_mode=0100000|0600;st->st_nlink=1;st->st_uid=getuid();st->st_size=(off_t)s->length;return 0;
}
int storage_close(int fd) {
    pthread_mutex_lock(&registry);Store **p=&handles;while(*p && (*p)->fd!=fd)p=&(*p)->next;Store *s=*p;if(s)*p=s->next;pthread_mutex_unlock(&registry);
    if(!s)return close(fd);
    if(s->dirty)sql(s->db,"ROLLBACK");
    sqlite3_finalize(s->get_block);
    sqlite3_finalize(s->put_block);
    sqlite3_finalize(s->get_length);
    free(s->row_cache);
    sqlite3_close(s->db);
    free(s);
    return close(fd);
}
int storage_open(const char *path,int flags,mode_t mode) {
    if(owner<0 || strstr(path,".retaining-"))return open(path,flags,mode);
    char key[PATH_MAX];if(!key_for(path,key)) { errno=EINVAL;return -1; }
    Store *s=calloc(1,sizeof(*s));if(!s)return -1;s->fd=-1;
    if(!connection(&s->db))goto fail;
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(s->db,"SELECT id,length FROM streams WHERE name=?",-1,&q,NULL)!=SQLITE_OK)goto fail;
    sqlite3_bind_text(q,1,key,-1,SQLITE_STATIC);int rc=sqlite3_step(q);
    if(rc==SQLITE_ROW) { s->id=sqlite3_column_int64(q,0);s->length=sqlite3_column_int64(q,1); }
    sqlite3_finalize(q);q=NULL;
    if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)goto fail;
    s->kind=row_kind(s);if(s->kind<0)goto fail;s->committed=s->length;
    if(s->id && s->length) {
        unsigned char boundary[BLOCK];
        if(s->length<0 || !block_read(s,0,boundary) || !block_read(s,(s->length-1)/BLOCK,boundary))goto fail;
    }
    if(s->id && (flags&O_EXCL) && (flags&O_CREAT)) { errno=EEXIST;goto fail; }
    int original=-1;
    if(!s->id) {
        original=open(path,O_RDONLY|O_NOFOLLOW);struct stat st;
        if(original>=0 && (fstat(original,&st) || !S_ISREG(st.st_mode) || st.st_nlink!=1)) { close(original);goto fail; }
        if(original>=0) { struct flock lock={0};lock.l_type=F_RDLCK;lock.l_whence=SEEK_SET;if(fcntl(original,F_SETLK,&lock)) { close(original);goto fail; } }
        if(original>=0 && storage_version>=2) {
            /* vaultctl init supplies only an empty index header on new installs.
             * Never import an existing populated store into structured mode. */
            unsigned char header[8];size_t len=strlen(path);
            if(fstat(original,&st) || st.st_size!=8 || len<10 || strcmp(path+len-10,"/index.dat") ||
               pread(original,header,8,0)!=8 || memcmp(header,"LNVLT001",8)) { close(original);errno=EINVAL;goto fail; }
        }
        if(original<0 && (!(flags&O_CREAT) || errno!=ENOENT))goto fail;
        if(original>=0 && (flags&O_EXCL) && (flags&O_CREAT)) { close(original);errno=EEXIST;goto fail; }
        if(!sql(s->db,"BEGIN IMMEDIATE") || sqlite3_prepare_v2(s->db,"INSERT INTO streams(name) VALUES(?)",-1,&q,NULL)!=SQLITE_OK) { if(original>=0)close(original);goto fail; }
        sqlite3_bind_text(q,1,key,-1,SQLITE_STATIC);int ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);q=NULL;if(!ok) { if(original>=0)close(original);goto fail; }
        s->id=sqlite3_last_insert_rowid(s->db);s->dirty=1;
    }
    s->fd=open("/dev/null",O_RDWR|O_CLOEXEC);if(s->fd<0) { if(original>=0)close(original);goto fail; }
    pthread_mutex_lock(&registry);s->next=handles;handles=s;pthread_mutex_unlock(&registry);
    if(original>=0) {
        unsigned char buffer[65536];ssize_t n;int ok=1;
        while((n=read(original,buffer,sizeof(buffer)))>0)if(storage_write(s->fd,buffer,(size_t)n)!=n) { ok=0;break; }
        if(n<0)ok=0;
        close(original);
        if(!ok) { storage_close(s->fd);
        return -1;
        }
    }
    if(s->dirty && storage_sync(s->fd)) { storage_close(s->fd);return -1; }
    s->readonly=(flags&O_ACCMODE)==O_RDONLY;s->position=flags&O_APPEND?s->length:0;return s->fd;
fail:sqlite3_finalize(s->get_block);sqlite3_finalize(s->put_block);sqlite3_finalize(s->get_length);free(s->row_cache);if(s->db) { sql(s->db,"ROLLBACK");sqlite3_close(s->db); }free(s);return -1;
}
int storage_stat(const char *path,struct stat *st) { if(owner<0)return stat(path,st);int fd=storage_open(path,O_RDONLY,0);if(fd<0)return -1;int rc=storage_fstat(fd,st);storage_close(fd);return rc; }
int storage_probe(const char *path,void *out,size_t n) { int fd=storage_open(path,O_RDONLY,0);if(fd<0)return 0;int ok=storage_read(fd,out,n)==(ssize_t)n;storage_close(fd);return ok; }
int storage_signature(int fd,unsigned char digest[32]) {
    Store *s=lookup(fd);if(!s || s->dirty)return 0;sqlite3_stmt *q=NULL;
    if(sqlite3_prepare_v2(s->db,"SELECT uuid,name,length,revision FROM storage_meta,streams WHERE streams.id=?",-1,&q,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_int64(q,1,s->id);int ok=sqlite3_step(q)==SQLITE_ROW;EVP_MD_CTX *h=EVP_MD_CTX_new();if(!h)ok=0;
    if(ok)ok=EVP_DigestInit_ex(h,EVP_sha256(),NULL);
    for(int i=0;ok && i<4;i++) { const void *v=sqlite3_column_blob(q,i);int n=sqlite3_column_bytes(q,i);ok=EVP_DigestUpdate(h,v,(size_t)n); }
    unsigned n=0;if(ok)ok=EVP_DigestFinal_ex(h,digest,&n) && n==32;EVP_MD_CTX_free(h);sqlite3_finalize(q);return ok?1:-1;
}
int storage_rename(const char *from,const char *to) {
    if(owner<0)return rename(from,to);
    char key[PATH_MAX];if(!key_for(to,key))return -1;
    int input=open(from,O_RDONLY|O_NOFOLLOW);if(input<0)return -1;
    int fd=storage_open(to,O_RDWR|O_CREAT,0600);if(fd<0) { close(input);return -1; }Store *s=lookup(fd);
    int ok=sql(s->db,"BEGIN IMMEDIATE");if(ok)s->dirty=1;
    sqlite3_stmt *q=NULL;if(ok)ok=sqlite3_prepare_v2(s->db,"DELETE FROM blocks WHERE stream=?",-1,&q,NULL)==SQLITE_OK;
    if(ok) { sqlite3_bind_int64(q,1,s->id);ok=sqlite3_step(q)==SQLITE_DONE; }sqlite3_finalize(q);
    if(ok && s->kind) {
        char query[160];snprintf(query,sizeof(query),"DELETE FROM %s WHERE stream=%lld;DELETE FROM record_streams WHERE stream=%lld",row_tables[s->kind],(long long)s->id,(long long)s->id);ok=sql(s->db,query);
    }
    s->kind=0;s->row_bytes=0;s->committed=s->length=s->position=0;
    unsigned char data[65536];ssize_t n=0;while(ok && (n=read(input,data,sizeof(data)))>0)ok=storage_write(fd,data,(size_t)n)==n;
    if(n<0)ok=0;
    if(ok)ok=storage_sync(fd)==0;
    close(input);
    storage_close(fd);
    if(ok)unlink(from);
    return ok?0:-1;
}
static int rows_flush(Store *s) {
    if(storage_version<2 || s->length<8)return 1;
    sqlite3_int64 saved=s->position;s->position=0;unsigned char magic[8];int ok=storage_read(s->fd,magic,8)==8;s->position=saved;if(!ok)return 0;
    int kind=!memcmp(magic,"RTBOARD4",8)||!memcmp(magic,"RTBOARD5",8)?1:!memcmp(magic,"LNMAIL01",8)?2:!memcmp(magic,"RTMEMB01",8)?3:0;
    if(!kind && storage_version>=3)for(unsigned i=0;i<13;i++)if(!memcmp(magic,event_magics[i],8)) { kind=(int)i+4;break; }
    if(!kind)return !s->kind;
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(s->db,"INSERT INTO record_streams VALUES(?,?,?) ON CONFLICT(stream) DO UPDATE SET magic=excluded.magic",-1,&q,NULL)!=SQLITE_OK)return 0;
    sqlite3_bind_int64(q,1,s->id);sqlite3_bind_int(q,2,kind);sqlite3_bind_blob(q,3,magic,8,SQLITE_STATIC);ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);if(!ok)return 0;
    s->kind=kind;unsigned char *r=malloc(ROW_MAX);if(!r)return 0;
    sqlite3_int64 at=s->committed<8?8:s->committed;
    while(ok && at<s->length) {
        size_t head=kind==15?32:128;s->position=at;ok=storage_read(s->fd,r,head)==(ssize_t)head;size_t n=ok?row_size((unsigned)kind,r):0;
        if(n<head || n>ROW_MAX || n>(uint64_t)(s->length-at)) { ok=0;break; }
        if(n>head)ok=storage_read(s->fd,r+head,n-head)==(ssize_t)(n-head);
        if(ok)ok=row_insert(s,at,r,n);
        at+=(sqlite3_int64)n;
    }
    free(r);s->position=saved;s->row_bytes=0;
    if(ok) { q=NULL;ok=sqlite3_prepare_v2(s->db,"DELETE FROM blocks WHERE stream=?",-1,&q,NULL)==SQLITE_OK;if(ok) { sqlite3_bind_int64(q,1,s->id);ok=sqlite3_step(q)==SQLITE_DONE; }sqlite3_finalize(q); }
    return ok;
}
void storage_shutdown(void) {
    while(handles)storage_close(handles->fd);
    if(owner>=0) { sqlite3 *db=NULL;if(connection(&db))sqlite3_wal_checkpoint_v2(db,NULL,SQLITE_CHECKPOINT_TRUNCATE,NULL,NULL);if(db)sqlite3_close(db);close(owner);owner=-1; }
    database[0]=directory[0]=0;
}

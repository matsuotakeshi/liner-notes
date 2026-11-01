#include "storage_sqlite.h"
#ifndef LN_DB_INDEX_H
#define LN_DB_INDEX_H
/* Rebuildable SQLite working indexes. Durable journals remain authoritative.
 * Each cache has one owner. Returned cells survive the next 128 distinct gets.
 * Index errors fail closed; no fall back to incomplete authorization state. */
#include <sqlite3.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#define DB_CELLS 128
#ifndef _WIN32
#include <sys/stat.h>
#endif
typedef struct { uint64_t id,age;void *data;int used; } DbCell;
typedef struct { sqlite3 *sql;sqlite3_stmt *get,*put;size_t size;uint64_t clock;DbCell cells[DB_CELLS]; } DbIndex;
static inline void db_die(DbIndex *d) { fprintf(stderr,"SQLite working index failed: %s\n",d->sql?sqlite3_errmsg(d->sql):"allocation/open");exit(EXIT_FAILURE); }
static inline void db_exec(DbIndex *d,const char *sql) { if(sqlite3_exec(d->sql,sql,NULL,NULL,NULL)!=SQLITE_OK)db_die(d); }
static inline void db_open_mode(DbIndex *d,const char *base,const char *suffix,size_t size,int cached) {
    char path[4096]="";if(d->sql || (base && snprintf(path,sizeof(path),"%s.%s.sqlite",base,suffix)>=(int)sizeof(path)))db_die(d);
#ifndef _WIN32
    struct stat st;if(!lstat(path,&st) && !S_ISREG(st.st_mode))db_die(d);
#endif
    if(sqlite3_open_v2(base?path:"",&d->sql,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_PRIVATECACHE,NULL)!=SQLITE_OK)db_die(d);
#ifndef _WIN32
    if(base && chmod(path,0600))db_die(d);
#endif
    d->size=size;
    db_exec(d,"PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA cache_size=-2048; BEGIN; CREATE TABLE IF NOT EXISTS cells(id INTEGER PRIMARY KEY, value BLOB NOT NULL);CREATE TABLE IF NOT EXISTS checkpoint(id INTEGER PRIMARY KEY,signature BLOB,metadata BLOB,cellsize INTEGER,format INTEGER);");
    if(!cached)db_exec(d,"DELETE FROM cells;DELETE FROM checkpoint;");
    if(sqlite3_prepare_v2(d->sql,"SELECT value FROM cells WHERE id=?",-1,&d->get,NULL)!=SQLITE_OK || sqlite3_prepare_v2(d->sql,"INSERT OR REPLACE INTO cells VALUES(?,?)",-1,&d->put,NULL)!=SQLITE_OK)db_die(d);
}
static inline void db_open(DbIndex *d,const char *base,const char *suffix,size_t size) { db_open_mode(d,base,suffix,size,0); }
static inline void db_open_cached(DbIndex *d,const char *base,const char *suffix,size_t size) { db_open_mode(d,base,suffix,size,1); }
static inline void db_reset(DbIndex *d) {
    for(unsigned i=0;i<DB_CELLS;i++) { free(d->cells[i].data);memset(&d->cells[i],0,sizeof(d->cells[i])); }
    db_exec(d,"DELETE FROM cells;DELETE FROM checkpoint;");
}
/* Hash already locked descriptors. Opening/closing another descriptor here would
 * release POSIX process locks. Never trust mtime alone for cache reuse. */
static inline int db_signature(const int *fds,unsigned count,unsigned char digest[32]) {
    EVP_MD_CTX *h=EVP_MD_CTX_new();if(!h)return 0;int ok=EVP_DigestInit_ex(h,EVP_sha256(),NULL);unsigned char block[65536];
    /* Invalidate native cell layouts across compiler/ABI/build changes. */
    static const char abi[]=__DATE__ " " __TIME__ " " __VERSION__;
    uint64_t layout[]={sizeof(void *),sizeof(long),sizeof(size_t),UINT64_C(0x0102030405060708)};
    if(ok)ok=EVP_DigestUpdate(h,abi,sizeof(abi)) && EVP_DigestUpdate(h,layout,sizeof(layout));
    for(unsigned i=0;ok && i<count;i++) {
        unsigned char revision[32];int stored=storage_signature(fds[i],revision);
        if(stored<0) { ok=0;break; }
        if(stored) { ok=EVP_DigestUpdate(h,revision,sizeof(revision));continue; }
#ifdef _WIN32
        int64_t pos=_lseeki64(fds[i],0,SEEK_CUR);if(pos<0 || _lseeki64(fds[i],0,SEEK_SET)<0) { ok=0;break; }
#else
        off_t pos=storage_seek(fds[i],0,SEEK_CUR);if(pos<0 || storage_seek(fds[i],0,SEEK_SET)<0) { ok=0;break; }
#endif
        uint64_t bytes=0;for(;;) {
#ifdef _WIN32
            int n=_read(fds[i],block,sizeof(block));
#else
            ssize_t n=storage_read(fds[i],block,sizeof(block));
#endif
            if(n<0) { ok=0;break; }if(!n)break;bytes+=(uint64_t)n;if(!EVP_DigestUpdate(h,block,(size_t)n)) { ok=0;break; }
        }
#ifdef _WIN32
        if(_lseeki64(fds[i],pos,SEEK_SET)<0)ok=0;
#else
        if(storage_seek(fds[i],pos,SEEK_SET)<0)ok=0;
#endif
        if(!EVP_DigestUpdate(h,&bytes,sizeof(bytes)))ok=0;
    }
    unsigned length=0;if(ok)ok=EVP_DigestFinal_ex(h,digest,&length) && length==32;EVP_MD_CTX_free(h);return ok;
}
static inline int db_cache_load(DbIndex *d,const int *fds,unsigned count,void *meta,size_t size) {
    unsigned char digest[32];if(!db_signature(fds,count,digest))return 0;
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(d->sql,"SELECT signature,metadata,cellsize,format FROM checkpoint WHERE id=1",-1,&q,NULL)!=SQLITE_OK)db_die(d);
    int ok=sqlite3_step(q)==SQLITE_ROW && sqlite3_column_bytes(q,0)==32 && !memcmp(sqlite3_column_blob(q,0),digest,32) && sqlite3_column_bytes(q,1)==(int)size && sqlite3_column_int64(q,2)==(sqlite3_int64)d->size && sqlite3_column_int(q,3)==6501;
    if(ok)memcpy(meta,sqlite3_column_blob(q,1),size);sqlite3_finalize(q);return ok;
}
static inline void db_cache_store(DbIndex *d,const int *fds,unsigned count,const void *meta,size_t size) {
    if(!d->sql)return;unsigned char digest[32];if(!db_signature(fds,count,digest)) { db_exec(d,"DELETE FROM checkpoint");return; }
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(d->sql,"INSERT OR REPLACE INTO checkpoint VALUES(1,?,?,?,6501)",-1,&q,NULL)!=SQLITE_OK)db_die(d);
    sqlite3_bind_blob(q,1,digest,32,SQLITE_STATIC);sqlite3_bind_blob(q,2,meta,(int)size,SQLITE_STATIC);sqlite3_bind_int64(q,3,(sqlite3_int64)d->size);if(sqlite3_step(q)!=SQLITE_DONE)db_die(d);sqlite3_finalize(q);
}
static inline void db_save(DbIndex *d,DbCell *c) {
    if(!c->used)return;
    sqlite3_bind_int64(d->put,1,(sqlite3_int64)c->id);sqlite3_bind_blob(d->put,2,c->data,(int)d->size,SQLITE_STATIC);
    if(sqlite3_step(d->put)!=SQLITE_DONE)db_die(d);
    sqlite3_reset(d->put);sqlite3_clear_bindings(d->put);
}
static inline void *db_at(DbIndex *d,uint64_t id) {
    if(!d->sql)db_die(d);
    DbCell *c=&d->cells[0];
    for(unsigned i=0;i<DB_CELLS;i++) { DbCell *p=&d->cells[i];if(p->used && p->id==id) { p->age=++d->clock;return p->data; }if(!p->used || (c->used && p->age<c->age))c=p; }
    db_save(d,c);if(!c->data)c->data=malloc(d->size);if(!c->data)db_die(d);memset(c->data,0,d->size);
    sqlite3_bind_int64(d->get,1,(sqlite3_int64)id);int rc=sqlite3_step(d->get);
    if(rc==SQLITE_ROW) { if(sqlite3_column_bytes(d->get,0)!=(int)d->size)db_die(d);memcpy(c->data,sqlite3_column_blob(d->get,0),d->size); }
    else if(rc!=SQLITE_DONE)db_die(d);
    sqlite3_reset(d->get);c->id=id;c->used=1;c->age=++d->clock;return c->data;
}
static inline void db_flush(DbIndex *d) { if(!d->sql)return;for(unsigned i=0;i<DB_CELLS;i++)db_save(d,&d->cells[i]); }
static inline void db_close(DbIndex *d) { if(!d->sql)return;db_flush(d);db_exec(d,"COMMIT");sqlite3_finalize(d->get);sqlite3_finalize(d->put);sqlite3_close(d->sql);for(unsigned i=0;i<DB_CELLS;i++)free(d->cells[i].data);memset(d,0,sizeof(*d)); }

static inline void db_search_open(DbIndex *d) {
    db_exec(d,"DROP TABLE IF EXISTS search;CREATE VIRTUAL TABLE search USING fts5(title,body,tokenize='trigram case_sensitive 1');");
}
static inline void db_search_set(DbIndex *d,uint32_t id,const char *title,const char *body,size_t length) {
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(d->sql,"INSERT INTO search(rowid,title,body) VALUES(?,?,?)",-1,&q,NULL)!=SQLITE_OK)db_die(d);
    sqlite3_bind_int(q,1,(int)id);sqlite3_bind_text(q,2,title,-1,SQLITE_STATIC);sqlite3_bind_text(q,3,body,(int)length,SQLITE_STATIC);
    if(sqlite3_step(q)!=SQLITE_DONE)db_die(d);
    sqlite3_finalize(q);
}
static inline void db_search_title(DbIndex *d,uint32_t id,const char *title) {
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(d->sql,"UPDATE search SET title=? WHERE rowid=?",-1,&q,NULL)!=SQLITE_OK)db_die(d);
    sqlite3_bind_text(q,1,title,-1,SQLITE_STATIC);sqlite3_bind_int(q,2,(int)id);if(sqlite3_step(q)!=SQLITE_DONE)db_die(d);
    sqlite3_finalize(q);
}
static inline uint32_t db_search_next(DbIndex *d,uint32_t start,const char *query,int descending) {
    unsigned chars=0;for(const unsigned char *p=(const unsigned char *)query;*p;p++)if((*p&0xc0)!=0x80)chars++;
    if(chars<3 || !strcmp(query,"*"))return start;
    char phrase[164];size_t n=0;phrase[n++]='"';for(const char *p=query;*p && n+3<sizeof(phrase);p++) { phrase[n++]=*p;if(*p=='"')phrase[n++]='"'; }phrase[n++]='"';phrase[n]=0;
    sqlite3_stmt *q=NULL;const char *sql=descending?"SELECT rowid FROM search WHERE search MATCH ? AND rowid<=? ORDER BY rowid DESC LIMIT 1":"SELECT rowid FROM search WHERE search MATCH ? AND rowid>=? ORDER BY rowid LIMIT 1";
    if(sqlite3_prepare_v2(d->sql,sql,-1,&q,NULL)!=SQLITE_OK)db_die(d);
    sqlite3_bind_text(q,1,phrase,-1,SQLITE_STATIC);sqlite3_bind_int(q,2,(int)start);
    int rc=sqlite3_step(q);uint32_t id=rc==SQLITE_ROW?(uint32_t)sqlite3_column_int(q,0):0;if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)db_die(d);sqlite3_finalize(q);return id;
}
#endif

/* Exercise the actual bounded SQL record codec, without a running service. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
typedef struct Store {
    sqlite3 *db;sqlite3_int64 id,row_at,length;int kind;size_t row_bytes;unsigned char *row_cache;
} Store;
static int sql(sqlite3 *db,const char *text) { return sqlite3_exec(db,text,NULL,NULL,NULL)==SQLITE_OK; }
#define BLOCK 4096
#include "storage_records.inc"
static int scalar(sqlite3 *db,const char *text) {
    sqlite3_stmt *q=NULL;assert(sqlite3_prepare_v2(db,text,-1,&q,NULL)==SQLITE_OK);
    assert(sqlite3_step(q)==SQLITE_ROW);int n=sqlite3_column_int(q,0);sqlite3_finalize(q);return n;
}
static const char *output_prefix;
static void exercise(int version) {
    sqlite3 *db=NULL;assert(sqlite3_open(":memory:",&db)==SQLITE_OK);storage_version=version;structured_requested=1;
    assert(sql(db,"PRAGMA foreign_keys=ON;CREATE TABLE streams(id INTEGER PRIMARY KEY);INSERT INTO streams VALUES(1);"));
    assert(row_schema(db));assert(sql(db,"INSERT INTO record_streams VALUES(1,2,x'4c4e4d41494c3031');"));
    Store store={.db=db,.id=1,.kind=2};assert(row_kind(&store)==2);
    sqlite3_int64 at=8;
    for(unsigned count=0;count<=16;count++) {
        unsigned char r[256]={0};size_t n=128+5+4*count;r[0]=count>1?'u':count?'t':'s';r[2]=count>1?(unsigned char)count:0;
        rput(r+8,100+count);rput(r+12,1);rput(r+16,2);rput(r+20,1);rput(r+24,7);rput(r+28,5);rput(r+32,5);rput(r+36,1720000000);
        memcpy(r+44,"title",5);memcpy(r+124,"hello",5);
        for(unsigned i=0;i<count;i++)rput(r+129+4*i,200+i);
        rput(r+n-4,event_sum(r,n));assert(row_size(2,r)==n);
        assert(sql(db,"BEGIN IMMEDIATE"));assert(row_insert(&store,at,r,n));assert(sql(db,"COMMIT"));
        store.row_bytes=0;assert(row_load(&store,at));assert(store.row_bytes==n && !memcmp(store.row_cache,r,n));at+=(sqlite3_int64)n;
    }
    store.length=at;unsigned char block[BLOCK];assert(row_block(&store,0,block));assert(!memcmp(block,"LNMAIL01",8));
    assert(!strcmp(event_magics[0],"LNPROF01"));
    if(output_prefix) {
        char path[4096];assert(snprintf(path,sizeof(path),"%s%d.sqlite",output_prefix,version)<(int)sizeof(path));
        assert(sql(db,"CREATE TABLE storage_meta(id INTEGER PRIMARY KEY,version INTEGER);"));
        char query[100];snprintf(query,sizeof(query),"INSERT INTO storage_meta VALUES(1,%d)",version);assert(sql(db,query));
        sqlite3 *target=NULL;assert(sqlite3_open(path,&target)==SQLITE_OK);
        sqlite3_backup *copy=sqlite3_backup_init(target,"main",db,"main");assert(copy);
        assert(sqlite3_backup_step(copy,-1)==SQLITE_DONE);assert(sqlite3_backup_finish(copy)==SQLITE_OK);sqlite3_close(target);
    }
    if(version==4) {
        assert(scalar(db,"SELECT count(*) FROM mail_attachments")==136);
        assert(scalar(db,"SELECT count(*) FROM mail_records WHERE length(attributes)!=48")==0);
        assert(!sql(db,"INSERT INTO mail_attachments VALUES(1,999999,0,1)"));
        assert(!sql(db,"INSERT INTO mail_attachments VALUES(1,8,16,1)"));
        assert(!sql(db,"INSERT INTO mail_attachments VALUES(1,8,0,0)"));
        /* An interrupted transaction must not leave a parent or attachment. */
        unsigned char r[132]={0};r[0]='t';rput(r+8,999);rput(r+124,5);
        assert(sql(db,"CREATE TRIGGER disk_full BEFORE INSERT ON mail_attachments BEGIN SELECT RAISE(ABORT,'disk full fixture'); END;BEGIN IMMEDIATE"));
        assert(!row_insert(&store,at,r,sizeof(r)));assert(sql(db,"ROLLBACK;DROP TRIGGER disk_full"));
        assert(scalar(db,"SELECT count(*) FROM mail_records WHERE record_id=999")==0);
        /* Missing/extra relationships must fail closed, even with valid SQL. */
        assert(sql(db,"INSERT INTO mail_attachments VALUES(1,8,0,1)"));store.row_bytes=0;assert(!row_load(&store,8));
        assert(sql(db,"DELETE FROM mail_attachments WHERE offset=8;DELETE FROM mail_attachments WHERE offset=141"));
        store.row_bytes=0;assert(!row_load(&store,141));
        assert(sql(db,"DELETE FROM streams WHERE id=1"));assert(scalar(db,"SELECT count(*) FROM mail_attachments")==0);
    }
    free(store.row_cache);sqlite3_close(db);
}
int main(int argc,char **argv) {
    if(argc==2)output_prefix=argv[1];
    exercise(3);exercise(4);puts("PASS schema 3/4 mail codec: 0..16 attachments, byte-exact roundtrip, constraints, rollback, corruption, cascade");return 0;
}

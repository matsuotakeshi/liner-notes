/* Independent canonical journal fixtures, not generated from field descriptors. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <openssl/evp.h>
typedef struct Store { sqlite3 *db;sqlite3_int64 id,row_at,length;int kind;size_t row_bytes;unsigned char *row_cache; } Store;
static int sql(sqlite3 *db,const char *s) { return sqlite3_exec(db,s,NULL,NULL,NULL)==SQLITE_OK; }
#define BLOCK 4096
#include "storage_records.inc"
static sqlite3 *db;
static const char *prefix;
static void save(Store *s,unsigned char *r,size_t n,FILE *raw) {
    if(s->kind==17) { unsigned out=0;assert(EVP_Digest(r,608,r+608,&out,EVP_sha256(),NULL) && out==32); }
    else rput(r+n-4,event_sum(r,n));
    assert(sql(db,"BEGIN IMMEDIATE"));
    if(!row_insert(s,s->length,r,n)) { fprintf(stderr,"insert kind=%d event=%c at=%lld\n",s->kind,r[0],(long long)s->length);abort(); }
    assert(sql(db,"COMMIT"));s->row_bytes=0;
    assert(row_load(s,s->length) && s->row_bytes==n && !memcmp(s->row_cache,r,n));
    if(raw)assert(fwrite(r,1,n,raw)==n);
    s->length+=(sqlite3_int64)n;
    unsigned char block[4096];assert(row_block(s,(s->length-1)/4096,block));
}
static FILE *stream(Store *s,int kind,const char *magic) {
    memset(s,0,sizeof(*s));s->db=db;s->id=kind;s->kind=kind;s->length=8;
    char query[256];snprintf(query,sizeof(query),"INSERT INTO streams VALUES(%d);INSERT INTO record_streams VALUES(%d,%d,CAST('%s' AS BLOB));",kind,kind,kind,magic);assert(sql(db,query));
    assert(row_kind(s)==kind);
    if(!prefix)return NULL;
    char path[4096];snprintf(path,sizeof(path),"%s-%d.raw",prefix,kind);FILE *f=fopen(path,"wb");assert(f);assert(fwrite(magic,1,8,f)==8);return f;
}
static void member_fixtures(void) {
    Store s;FILE *raw=stream(&s,3,"RTMEMB01");
    const char *types="AJPQLVGgFfXNC MRpDd";
    for(const char *t=types;*t;t++) {
        if(*t==' ')continue;
        unsigned char r[1152]={0};r[0]=(unsigned char)*t;rput(r+4,7);size_t n=128;
        switch(*t) {
        case 'A':case 'J':r[1]=*t=='J';memcpy(r+8,"alice",5); /* fall through */
        case 'P':case 'Q':memset(r+40,0xa5,16);memset(r+56,0x5a,32);rput(r+88,600000);if(*t=='Q') { rput(r+8,1);rput(r+12,1720000000); }break;
        case 'L':r[8]=5;rput(r+12,0x80000001u); /* fall through */
        case 'V':rput(r+16,1);rput(r+20,1720000000);break;
        case 'G':rput(r+8,0x80000001u);rput(r+12,1);rput(r+16,0x80000000u);rput(r+20,8);rput(r+24,1);rput(r+28,1720000000);break;
        case 'g':rput(r+8,100);rput(r+12,1);rput(r+16,1720000000);break;
        case 'f':r[10]=1;r[11]=15;rput(r+12,132);rput(r+16,50); /* fall through */
        case 'F':r[8]=1;r[9]=1;break;
        case 'X':r[8]=3;rput(r+12,1);rput(r+16,1720000000);break;
        case 'N':case 'C':r[8]=1;break;
        case 'M':r[8]=1;rput(r+12,2);break;
        case 'R':rput(r+8,12001);r[12]=1;break;
        case 'D':case 'd':n=*t=='D'?1152:128;r[1]=1;rput(r+8,1024);rput(r+12,12001);rput(r+16,17);rput(r+20,5);rput(r+24,5);memcpy(r+32,"title",5);memcpy(r+112,"hello",5);break;
        }
        save(&s,r,n,raw);
    }
    assert(sql(db,"INSERT INTO member_event_groups VALUES(3,520,2)")); /* L event */
    s.row_bytes=0;assert(!row_load(&s,520)); /* checksum detects relation tampering */
    assert(sql(db,"DELETE FROM member_event_groups WHERE offset=520 AND group_id=2"));
    if(raw)fclose(raw);
    free(s.row_cache);
}
static void article_fixtures(void) {
    Store s;FILE *raw=stream(&s,1,"RTBOARD5");
    for(unsigned large=0;large<2;large++) {
        size_t body=large?262144:5,n=large?144+body:1168;unsigned char *r=calloc(1,n);assert(r);
        rput(r,large?0x80000002u:1);rput(r+4,large?1:0);rput(r+8,5);rput(r+12,(uint32_t)body);memcpy(r+16,"title",5);memset(r+96,'x',body);
        rput(r+n-48,1720000000);memcpy(r+n-40,"alice",5);rput(r+n-8,1024);save(&s,r,n,raw);free(r);
    }
    if(raw)fclose(raw);
    free(s.row_cache);
}
static void vault_fixtures(void) {
    Store s;FILE *raw=stream(&s,17,"LNVLT001");
    const char *types="ADRarBnoijkpvCHPXESFOTNQ";
    for(const char *t=types;*t;t++) {
        unsigned char r[640]={0};r[0]=(unsigned char)*t;rput(r+4,1);
        if(strchr("ADRar",*t)) {
            rput(r+8,1073741824);rput(r+12,1720000000);memset(r+20,0xab,32);
            rput(r+52,8);rput(r+56,4);memcpy(r+60,"test.txt",8);memcpy(r+188,"text",4);memcpy(r+444,"alice",5);rput(r+524,5);
            if(*t=='a'||*t=='r') { rput(r+528,3);memcpy(r+532,"a/b",3); }
        } else { rput(r+8,1720000000);rput(r+16,5);rput(r+20,4);memcpy(r+24,"alice",5);memcpy(r+104,"text",4); }
        save(&s,r,sizeof(r),raw);
    }
    assert(sql(db,"UPDATE vault_records SET name='corrupt' WHERE offset=8"));s.row_bytes=0;assert(!row_load(&s,8));
    assert(sql(db,"UPDATE vault_records SET name='test.txt' WHERE offset=8"));
    if(raw)fclose(raw);
    free(s.row_cache);
}
static void fault_tests(void) {
    Store s={.db=db,.id=3,.kind=3,.length=99999};unsigned char r[128]={0};r[0]='N';rput(r+4,1);rput(r+124,event_sum(r,128));
    assert(sql(db,"CREATE TRIGGER full BEFORE INSERT ON member_mail_notice BEGIN SELECT RAISE(ABORT,'disk full fixture'); END;BEGIN IMMEDIATE"));
    assert(!row_insert(&s,s.length,r,128));assert(sql(db,"ROLLBACK;DROP TRIGGER full"));
    assert(!row_load(&s,s.length));free(s.row_cache);
    s.row_cache=NULL;
    assert(!sql(db,"INSERT INTO member_event_groups VALUES(3,99999,1)"));
    assert(!sql(db,"INSERT INTO member_event_groups VALUES(3,520,101)"));
    assert(sql(db,"BEGIN IMMEDIATE"));r[1]=42;rput(r+124,event_sum(r,128));assert(!row_insert(&s,s.length,r,128));assert(sql(db,"ROLLBACK"));
    /* Actual SQLITE_FULL through the record codec, not a timeout or mock. */
    sqlite3_stmt *q=NULL;assert(sqlite3_prepare_v2(db,"PRAGMA page_count",-1,&q,NULL)==SQLITE_OK);assert(sqlite3_step(q)==SQLITE_ROW);int pages=sqlite3_column_int(q,0);sqlite3_finalize(q);
    char limit[100];snprintf(limit,sizeof(limit),"PRAGMA max_page_count=%d",pages);assert(sql(db,limit));
    unsigned char *large=calloc(1,262288);assert(large);rput(large,0x80000003u);rput(large+8,5);rput(large+12,262144);memcpy(large+16,"title",5);memset(large+96,'z',262144);rput(large+262284,event_sum(large,262288));
    Store article={.db=db,.id=1,.kind=1};assert(sql(db,"BEGIN IMMEDIATE"));assert(!row_insert(&article,999999,large,262288));
    assert(sqlite3_errcode(db)==SQLITE_FULL);sqlite3_exec(db,"ROLLBACK",NULL,NULL,NULL);free(large);
    assert(sql(db,"PRAGMA max_page_count=2147483646"));
    assert(sqlite3_prepare_v2(db,"SELECT count(*) FROM article_records WHERE offset=999999",-1,&q,NULL)==SQLITE_OK);assert(sqlite3_step(q)==SQLITE_ROW && sqlite3_column_int(q,0)==0);sqlite3_finalize(q);

}
int main(int argc,char **argv) {
    if(argc==2)prefix=argv[1];
    assert(sqlite3_open(":memory:",&db)==SQLITE_OK);storage_version=5;structured_requested=1;assert(structured_requested && !strcmp(event_magics[0],"LNPROF01"));
    assert(sql(db,"PRAGMA foreign_keys=ON;CREATE TABLE streams(id INTEGER PRIMARY KEY);CREATE TABLE blocks(stream INTEGER,number INTEGER,value BLOB);CREATE TABLE storage_meta(id INTEGER PRIMARY KEY,version INTEGER);INSERT INTO storage_meta VALUES(1,5)"));
    assert(row_schema(db));member_fixtures();article_fixtures();vault_fixtures();fault_tests();
    if(prefix) {
        char path[4096];snprintf(path,sizeof(path),"%s.sqlite",prefix);sqlite3 *out=NULL;assert(sqlite3_open(path,&out)==SQLITE_OK);
        sqlite3_backup *copy=sqlite3_backup_init(out,"main",db,"main");assert(copy);assert(sqlite3_backup_step(copy,-1)==SQLITE_DONE);assert(sqlite3_backup_finish(copy)==SQLITE_OK);sqlite3_close(out);
    }
    assert(sql(db,"DELETE FROM streams"));
    sqlite3_stmt *q=NULL;assert(sqlite3_prepare_v2(db,"SELECT (SELECT count(*) FROM member_event_groups)+(SELECT count(*) FROM member_password_reset)+(SELECT count(*) FROM vault_records)",-1,&q,NULL)==SQLITE_OK);assert(sqlite3_step(q)==SQLITE_ROW && sqlite3_column_int(q,0)==0);sqlite3_finalize(q);
    sqlite3_close(db);puts("PASS schema 5: every member event, groups, maximum article, vault events, corruption, rollback, cascade");return 0;
}

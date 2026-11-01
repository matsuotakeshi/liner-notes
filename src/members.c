#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "runtime_limits.h"
#include "app_limits.h"
/* Single board-worker ownership. Append-only account/read-state journal. */
#include "members.h"
int members_rtbbs_policy;
#include "store_limits.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
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
#define USERS MEMBER_MAX
#define ITERATIONS 600000
typedef struct { uint32_t board,parent,revision,length;uint64_t offset;char title[81]; } DraftIndex;
typedef struct { DraftIndex draft; int level,pending,deleted;uint32_t removed_actor;uint64_t removed_time; uint32_t groups,group_high[3]; unsigned columns,rows,aux;int editor; uint32_t mail_notify_revision,chat_revision,muted[32]; int mail_notify_deny,chat_deny; int preferences,language,pager; char name[33]; unsigned char salt[16],hash[32]; uint32_t last_seen; } User;
#include "db_index.h"
static DbIndex account_db,seen_db;
static User *user_at(uint64_t i) { return db_at(&account_db,i); }
static unsigned char *seen_page(uint32_t u,uint32_t article) { return db_at(&seen_db,((uint64_t)u<<32)|(article/4096)); }

static char first_id[33];
static uint32_t count;static int automatic_ids,detailed_form;static char id_prefix[25]="u";static unsigned id_digits=6;
int members_id_format(const char *prefix,unsigned digits) { if(strlen(prefix)>24 || digits<1 || digits>8 || strspn(prefix,"abcdefghijklmnopqrstuvwxyz0123456789_-")!=strlen(prefix))return 0;strcpy(id_prefix,prefix);id_digits=digits;return 1; }
void members_registration_form(int enabled) { detailed_form=enabled; }
int members_detailed_form(void) { return detailed_form; }
void members_id_mode(int automatic) { automatic_ids=automatic; }
int members_auto_ids(void) { return automatic_ids; }
uint32_t members_find(const char *name) {
    if(!account_db.sql)return 0;
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(account_db.sql,"SELECT id FROM names WHERE name=?",-1,&q,NULL)!=SQLITE_OK)db_die(&account_db);
    sqlite3_bind_text(q,1,name,-1,SQLITE_STATIC);int rc=sqlite3_step(q);uint32_t id=rc==SQLITE_ROW?(uint32_t)sqlite3_column_int(q,0):0;
    if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)db_die(&account_db);
    sqlite3_finalize(q);return id;
}
static void remember_name(const char *name,uint32_t id) {
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(account_db.sql,"INSERT INTO names VALUES(?,?)",-1,&q,NULL)!=SQLITE_OK)db_die(&account_db);
    sqlite3_bind_text(q,1,name,-1,SQLITE_STATIC);sqlite3_bind_int(q,2,(int)id);if(sqlite3_step(q)!=SQLITE_DONE)db_die(&account_db);sqlite3_finalize(q);
}
static int fd=-1, broken, registration,profile_required;
int members_registration_mode(void) { return registration; }
void members_registration(int mode) { registration=mode; }
static uint32_t get32(const unsigned char *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(unsigned char *p,uint32_t n) { for(int i=0;i<4;i++) p[i]=(unsigned char)(n>>(i*8)); }
static uint32_t sum(const unsigned char *p,size_t size) { uint32_t n=2166136261u; for(size_t i=0;i<size;i++) n=(n^p[i])*16777619u; return n; }
static uint32_t crc(const unsigned char *p) { return sum(p,124); }
static int valid_name(const char *s) {
    size_t n=strlen(s); if(!n || n>32) return 0;
    for(size_t i=0;i<n;i++) if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-')) return 0;
    return 1;
}
int members_first_id(const char *name) { if(*name && !valid_name(name))return 0;strcpy(first_id,name);return 1; }
static int append_size(unsigned char *row,size_t size) {
    if(broken || SEEK(fd,0,SEEK_END)<0) return 0;
    put32(row+size-4,sum(row,size-4));
    size_t offset=0;
    while(offset<size) {
        int n=(int)WRITE(fd,row+offset,(unsigned)(size-offset));
        if(n<=0) { broken=1; return 0; } offset+=(size_t)n;
    }
    if(SYNC(fd)) { broken=1; return 0; }
    return 1;
}
static int append(unsigned char *row) { return append_size(row,128); }
int members_open(const char *path) {
    fd=OPEN(path); if(fd<0) return 0;
#ifdef _WIN32
    OVERLAPPED ov={0};
    if(!LockFileEx((HANDLE)_get_osfhandle(fd),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&ov)) goto fail;
#else
    struct flock lock={0}; lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET;
    if(storage_lock(fd,&lock)) goto fail;
    /* Persist the newly created directory entry as well as journal contents. */
    char dir[4096]; if(strlen(path)>=sizeof(dir)) goto fail;
    strcpy(dir,path); char *slash=strrchr(dir,'/');
    if(slash) { if(slash==dir) slash[1]=0; else *slash=0; } else strcpy(dir,".");
    int d=open(dir,O_RDONLY); if(d<0) goto fail;
    int e=storage_sync(d); storage_close(d); if(e) goto fail;
#endif
    db_open_cached(&account_db,path,"accounts-index",sizeof(User));db_open_cached(&seen_db,path,"read-index",512);
    uint32_t meta[2],other[2];if(db_cache_load(&account_db,&fd,1,meta,sizeof(meta)) && db_cache_load(&seen_db,&fd,1,other,sizeof(other)) && !memcmp(meta,other,sizeof(meta))) { count=meta[0];profile_required=(int)meta[1];return 1; }
    db_reset(&account_db);db_reset(&seen_db);db_exec(&account_db,"DROP TABLE IF EXISTS names;CREATE TABLE names(name TEXT PRIMARY KEY,id INTEGER UNIQUE);");
    unsigned char row[CONTENT_BODY+128]; int n=(int)READ(fd,row,8);
    if(n==0) { if(WRITE(fd,"RTMEMB01",8)!=8 || SYNC(fd)) goto fail; }
    else if(n!=8 || memcmp(row,"RTMEMB01",8)) goto fail;
    while((n=(int)READ(fd,row,128))!=0) {
        if(n!=128) goto fail;
        if(row[0]=='D' || row[0]=='d') {
            uint32_t length=get32(row+24);if(length>(row[0]=='D'?1024u:CONTENT_BODY))goto fail;
            size_t row_size=row[0]=='D'?1152:(116+length<128?128:116+length);
            size_t have=128;
            while(have<row_size) { int k=(int)READ(fd,row+have,(unsigned)(row_size-have)); if(k<=0) goto fail; have+=(size_t)k; }
            uint32_t id=get32(row+4),tl=get32(row+20),bl=get32(row+24),rev=get32(row+16);
            if(sum(row,row_size-4)!=get32(row+row_size-4) || !id || id>count || row[1]>1 || row[2] || row[3] ||
               (*user_at(id-1)).draft.revision==UINT32_MAX || rev!=(*user_at(id-1)).draft.revision+1 || tl>80 || bl>CONTENT_BODY) goto fail;
            for(int i=28;i<32;i++) if(row[i]) goto fail;
            for(size_t i=32+tl;i<112;i++) if(row[i]) goto fail;
            for(size_t i=112+bl;i<row_size-4;i++) if(row[i]) goto fail;
            if(row[1]) {
                if(!get32(row+8) || get32(row+8)>10000 || get32(row+12)>CONTENT_ID_MAX || !tl || memchr(row+32,0,tl) || memchr(row+112,0,bl)) goto fail;
                for(size_t i=0;i<tl;i++) if(row[32+i]<32 || row[32+i]==127) goto fail;
                for(size_t i=0;i<bl;i++) {
                    unsigned c=row[112+i];
                    if(c=='\r') { if(i+1>=bl || row[112+i+1]!='\n') goto fail; i++; }
                    else if(c<32 || c==127) goto fail;
                }
                if(bl && (bl<2 || row[112+bl-2]!='\r' || row[112+bl-1]!='\n')) goto fail;
            } else if(get32(row+8) || get32(row+12) || tl || bl) goto fail;
            DraftIndex *d=&(*user_at(id-1)).draft; memset(d,0,sizeof(*d)); d->revision=rev;
            d->board=get32(row+8); d->parent=get32(row+12); memcpy(d->title,row+32,tl); d->length=bl;d->offset=(uint64_t)SEEK(fd,0,SEEK_CUR)-row_size+112;
            continue;
        }
        if(crc(row)!=get32(row+124)) goto fail;
        uint32_t id=get32(row+4);
        if(row[0]=='A' || row[0]=='J') {
            if(id!=count+1 || count==USERS || row[1]!=(row[0]=='J') || get32(row+88)!=ITERATIONS) goto fail;
            char name[33]={0}; memcpy(name,row+8,32);
            if(!valid_name(name)) goto fail;
            if(members_find(name))goto fail;
            remember_name(name,id);
            (*user_at(count)).level=2;(*user_at(count)).pending=row[1];
            strcpy((*user_at(count)).name,name); memcpy((*user_at(count)).salt,row+40,16); memcpy((*user_at(count)).hash,row+56,32); count++;
        } else if(row[0]=='L' || row[0]=='V') {
            if(!id || id>count || !get32(row+16) || get32(row+16)>count || row[8]>5 || row[1] || row[2] || row[3])goto fail;
            for(int i=9;i<12;i++)if(row[i])goto fail;
            for(int i=28;i<124;i++)if(row[i])goto fail;
            if((get32(row+20)|((uint64_t)get32(row+24)<<32))>253402300799ULL)goto fail;
            if(row[0]=='V') { if(row[8] || get32(row+12) || !(*user_at(id-1)).pending)goto fail;(*user_at(id-1)).pending=0; }
            else { (*user_at(id-1)).level=row[8];(*user_at(id-1)).groups=get32(row+12); }
        } else if(row[0]=='p') {
            if(!id || id>count)goto fail;
            for(unsigned i=1;i<124;i++)if((i<4 || i>=8) && row[i])goto fail;
            profile_required=1;
        } else if(row[0]=='G') {
            if(!id || id>count || !get32(row+24) || get32(row+24)>count || row[1] || row[2] || row[3] || get32(row+20)>15)goto fail;
            for(int i=36;i<124;i++)if(row[i])goto fail;
            if((get32(row+28)|((uint64_t)get32(row+32)<<32))>253402300799ULL)goto fail;
            (*user_at(id-1)).groups=get32(row+8);for(unsigned i=0;i<3;i++)(*user_at(id-1)).group_high[i]=get32(row+12+i*4);
        } else if(row[0]=='g') {
            unsigned to=get32(row+8);if(!id || id>100 || !to || to>100 || id==to || !get32(row+12) || get32(row+12)>count || row[1] || row[2] || row[3])goto fail;
            for(int i=24;i<124;i++)if(row[i])goto fail;
            if((get32(row+16)|((uint64_t)get32(row+20)<<32))>253402300799ULL)goto fail;
            for(uint32_t u=0;u<count;u++) { uint32_t *fromword=id<=32?&(*user_at(u)).groups:&(*user_at(u)).group_high[(id-1)/32-1],*toword=to<=32?&(*user_at(u)).groups:&(*user_at(u)).group_high[(to-1)/32-1];
                if(*fromword&(1u<<((id-1)%32))) { *fromword&=~(1u<<((id-1)%32));*toword|=1u<<((to-1)%32); }
            }
        } else if(row[0]=='f') {
            unsigned columns=get32(row+12),rows=get32(row+16);
            if(!id || id>count || row[1] || row[2] || row[3] || row[8]>1 || row[9]>1 || row[10]>1 || row[11]>15 || columns<20 || columns>240 || rows<5 || rows>100)goto fail;
            for(int i=20;i<124;i++)if(row[i])goto fail;
            (*user_at(id-1)).preferences=1;(*user_at(id-1)).language=row[8];(*user_at(id-1)).pager=row[9];(*user_at(id-1)).aux=row[11];(*user_at(id-1)).editor=row[10];(*user_at(id-1)).columns=columns;(*user_at(id-1)).rows=rows;
        } else if(row[0]=='X') {
            if(!id || id>count || (*user_at(id-1)).deleted || row[8]<1 || row[8]>4 || !get32(row+12) || get32(row+12)>count || row[1] || row[2] || row[3])goto fail;
            if((row[8]<=2 && !(*user_at(id-1)).pending) || (row[8]>=3 && (*user_at(id-1)).pending))goto fail;
            for(unsigned i=9;i<12;i++)if(row[i])goto fail;
            for(unsigned i=24;i<124;i++)if(row[i])goto fail;
            if((get32(row+16)|((uint64_t)get32(row+20)<<32))>253402300799ULL)goto fail;
            (*user_at(id-1)).deleted=row[8];(*user_at(id-1)).removed_actor=get32(row+12);(*user_at(id-1)).removed_time=get32(row+16)|((uint64_t)get32(row+20)<<32);OPENSSL_cleanse((*user_at(id-1)).hash,32);OPENSSL_cleanse((*user_at(id-1)).salt,16);
        } else if(row[0]=='P' || row[0]=='Q') {
            if(!id || id>count || get32(row+88)!=ITERATIONS) goto fail;
            for(int i=1;i<4;i++) if(row[i]) goto fail;
            if(row[0]=='Q') {
                uint64_t at=get32(row+12)|((uint64_t)get32(row+16)<<32);
                if(!get32(row+8) || get32(row+8)>count || !at || at>253402300799ULL)goto fail;
                for(int i=20;i<40;i++)if(row[i])goto fail;
            } else for(int i=8;i<40;i++) if(row[i]) goto fail;
            for(int i=92;i<124;i++) if(row[i]) goto fail;
            memcpy((*user_at(id-1)).salt,row+40,16); memcpy((*user_at(id-1)).hash,row+56,32);
        } else if(row[0]=='F') {
            if(!id || id>count || row[8]>1 || row[9]>1) goto fail;
            for(int i=1;i<4;i++) if(row[i]) goto fail;
            for(int i=10;i<124;i++) if(row[i]) goto fail;
            (*user_at(id-1)).preferences=1; (*user_at(id-1)).language=row[8]; (*user_at(id-1)).pager=row[9];
        } else if(row[0]=='N') {
            if(!id || id>count || row[8]>1 || (*user_at(id-1)).mail_notify_revision==UINT32_MAX)goto fail;
            for(int i=1;i<4;i++)if(row[i])goto fail;
            for(int i=9;i<124;i++)if(row[i])goto fail;
            (*user_at(id-1)).mail_notify_deny=row[8];(*user_at(id-1)).mail_notify_revision++;
        } else if(row[0]=='C' || row[0]=='M') {
            if(!id || id>count || row[8]>1 || (*user_at(id-1)).chat_revision==UINT32_MAX) goto fail;
            for(int i=1;i<4;i++)if(row[i])goto fail;
            for(int i=9;i<124;i++)if((row[0]=='C' || i<12 || i>=16) && row[i])goto fail;
            if(row[0]=='C')(*user_at(id-1)).chat_deny=row[8];
            else {
                uint32_t target=get32(row+12);if(!target || target>count || target==id)goto fail;
                int slot=-1,empty=-1;
                for(int i=0;i<32;i++) { if((*user_at(id-1)).muted[i]==target)slot=i;if(!(*user_at(id-1)).muted[i])empty=i; }
                if(row[8]) { if(slot<0)slot=empty;if(slot<0)goto fail;(*user_at(id-1)).muted[slot]=target; }
                else if(slot>=0)(*user_at(id-1)).muted[slot]=0;
            }
            (*user_at(id-1)).chat_revision++;
        } else if(row[0]=='R') {
            uint32_t article=get32(row+8);
            if(!id || id>count || !article || article>CONTENT_ID_MAX || row[12]>1) goto fail;
            unsigned char *page=seen_page(id,article);if(article>(*user_at(id-1)).last_seen)(*user_at(id-1)).last_seen=article;
            unsigned char mask=(unsigned char)(1u<<(article%8));
            if(row[12]) page[(article%4096)/8]|=mask;
            else page[(article%4096)/8]&=(unsigned char)~mask;
        } else goto fail;
    }
    return 1;
fail:
    CLOSE(fd); fd=-1; return 0;
}
uint32_t members_count(void) { return count; }
int members_enabled(void) { return fd>=0; }
void members_close(void) { if(fd>=0 && !broken) { uint32_t meta[]={count,(uint32_t)profile_required};db_cache_store(&account_db,&fd,1,meta,sizeof(meta));db_cache_store(&seen_db,&fd,1,meta,sizeof(meta)); }db_close(&account_db);db_close(&seen_db);if(fd>=0) CLOSE(fd); fd=-1;count=0;broken=0;profile_required=0; }
int members_active(uint32_t id) { return id && id<=count && !(*user_at(id-1)).deleted; }
int members_deleted(uint32_t id) { return id && id<=count?(*user_at(id-1)).deleted:0; }
int members_remove(uint32_t id,uint32_t actor,unsigned kind) {
    if(!members_active(id) || !members_active(actor) || kind<1 || kind>4 || (kind<=2?!(*user_at(id-1)).pending:(*user_at(id-1)).pending))return 0;
    unsigned char row[128]={0};row[0]='X';row[8]=(unsigned char)kind;put32(row+4,id);put32(row+12,actor);uint64_t now=(uint64_t)time(NULL);put32(row+16,(uint32_t)now);put32(row+20,(uint32_t)(now>>32));
    if(!append(row))return -1;
    (*user_at(id-1)).deleted=(int)kind;(*user_at(id-1)).removed_actor=actor;(*user_at(id-1)).removed_time=now;OPENSSL_cleanse((*user_at(id-1)).hash,32);OPENSSL_cleanse((*user_at(id-1)).salt,16);return 1;
}
const char *members_name(uint32_t id) { return id && id<=count?(*user_at(id-1)).name:NULL; }
int members_auth(const char *name,const char *password,int create) {
    char generated[33];
    if(create && automatic_ids && !count && *first_id)name=first_id;
    else if(create && automatic_ids) { uint32_t n=count+1;do { snprintf(generated,sizeof(generated),"%s%0*u",id_prefix,(int)id_digits,n++); }while(members_find(generated) && n<=MEMBER_MAX+1);name=generated; }
    if(fd<0 || broken || !valid_name(name) || strlen(password)<8 || strlen(password)>128) return 0;
    uint32_t id=0;
    id=members_find(name);
    unsigned char salt[16]={0},hash[32],row[128]={0};
    if(create) {
        if(broken || registration==2 || id || count>=ln_limit("MEMBERS",USERS) || RAND_bytes(salt,16)!=1) return 0;
    } else if(id) memcpy(salt,(*user_at(id-1)).salt,16);
    if(PKCS5_PBKDF2_HMAC(password,(int)strlen(password),salt,16,ITERATIONS,EVP_sha256(),32,hash)!=1) return 0;
    if(!create) {
        int okay=id && !(*user_at(id-1)).deleted && !(*user_at(id-1)).pending && CRYPTO_memcmp(hash,(*user_at(id-1)).hash,32)==0;
        OPENSSL_cleanse(hash,sizeof(hash)); return okay?(int)id:0;
    }
    row[0]=registration==1?'J':'A';row[1]=(unsigned char)(registration==1); put32(row+4,count+1); memcpy(row+8,name,strlen(name));
    memcpy(row+40,salt,16); memcpy(row+56,hash,32); put32(row+88,ITERATIONS);
    if(!append(row)) { OPENSSL_cleanse(hash,sizeof(hash)); return 0; }
    (*user_at(count)).level=2;(*user_at(count)).pending=registration==1;
    strcpy((*user_at(count)).name,name); memcpy((*user_at(count)).salt,salt,16); memcpy((*user_at(count)).hash,hash,32);
    remember_name(name,count+1);OPENSSL_cleanse(hash,sizeof(hash)); return (int)++count;
}
int members_seen(uint32_t user,uint32_t article) {
    return members_name(user) && article && article<=CONTENT_ID_MAX && (seen_page(user,article)[(article%4096)/8]&(1u<<(article%8)));
}
int members_mark(uint32_t user,uint32_t article,int seen) {
    if(fd<0 || !members_name(user) || !article || article>CONTENT_ID_MAX || broken) return 0;
    if(!!members_seen(user,article)==!!seen) return 1;
    unsigned char *page=seen_page(user,article);if(article>(*user_at(user-1)).last_seen)(*user_at(user-1)).last_seen=article;
    unsigned char row[128]={0}; row[0]='R'; put32(row+4,user); put32(row+8,article); row[12]=(unsigned char)seen;
    if(!append(row)) return 0;
    unsigned char mask=(unsigned char)(1u<<(article%8));
    if(seen) page[(article%4096)/8]|=mask; else page[(article%4096)/8]&=(unsigned char)~mask;
    return 1;
}

int members_broken(void) { return broken; }
int members_reset_password(uint32_t user,uint32_t actor,const char *replacement) {
    if(fd<0 || broken || !members_active(user) || !members_active(actor))return -1;
    size_t n=strlen(replacement);if(n<8 || n>128)return 0;
    unsigned char row[128]={0};row[0]='Q';put32(row+4,user);put32(row+8,actor);
    uint64_t now=(uint64_t)time(NULL);put32(row+12,(uint32_t)now);put32(row+16,(uint32_t)(now>>32));put32(row+88,ITERATIONS);
    if(RAND_bytes(row+40,16)!=1 || PKCS5_PBKDF2_HMAC(replacement,(int)n,row+40,16,ITERATIONS,EVP_sha256(),32,row+56)!=1) { OPENSSL_cleanse(row,sizeof(row));return -1; }
    int ok=append(row);if(ok) { memcpy((*user_at(user-1)).salt,row+40,16);memcpy((*user_at(user-1)).hash,row+56,32); }
    OPENSSL_cleanse(row,sizeof(row));return ok?1:-1;
}
int members_password(uint32_t user,const char *current,const char *replacement) {
    if(fd<0 || broken || !members_name(user)) return -1;
    size_t n=strlen(replacement);
    if(n<8 || n>128 || !strcmp(current,replacement)) return 0;
    if(members_auth(members_name(user),current,0)!=(int)user) return 0;
    unsigned char row[128]={0}; row[0]='P'; put32(row+4,user); put32(row+88,ITERATIONS);
    if(RAND_bytes(row+40,16)!=1 || PKCS5_PBKDF2_HMAC(replacement,(int)n,row+40,16,ITERATIONS,EVP_sha256(),32,row+56)!=1) {
        OPENSSL_cleanse(row,sizeof(row)); return -1;
    }
    int ok=append(row);
    if(ok) { memcpy((*user_at(user-1)).salt,row+40,16); memcpy((*user_at(user-1)).hash,row+56,32); }
    OPENSSL_cleanse(row,sizeof(row)); return ok?1:-1;
}

int members_preferences(uint32_t user,int *language,int *pager) {
    if(!members_name(user) || !(*user_at(user-1)).preferences) return 0;
    *language=(*user_at(user-1)).language; *pager=(*user_at(user-1)).pager; return 1;
}
int members_save_preferences(uint32_t user,int language,int pager) {
    if(fd<0 || broken || !members_name(user) || language<0 || language>1 || pager<0 || pager>1) return 0;
    if((*user_at(user-1)).preferences && (*user_at(user-1)).language==language && (*user_at(user-1)).pager==pager) return 1;
    unsigned char row[128]={0}; row[0]='F'; put32(row+4,user); row[8]=(unsigned char)language; row[9]=(unsigned char)pager;
    if(!append(row)) return 0;
    (*user_at(user-1)).preferences=1; (*user_at(user-1)).language=language; (*user_at(user-1)).pager=pager; return 1;
}

int members_draft(uint32_t user,MemberDraft *draft) {
    if(!members_name(user)) return 0;
    DraftIndex *d=&(*user_at(user-1)).draft;memset(draft,0,sizeof(*draft));draft->board=d->board;draft->parent=d->parent;draft->revision=d->revision;strcpy(draft->title,d->title);
    if(d->board) { if(SEEK(fd,(int64_t)d->offset,SEEK_SET)<0) { broken=1;return 0; }size_t got=0;while(got<d->length) { int n=(int)READ(fd,draft->body+got,(unsigned)(d->length-got));if(n<=0) { broken=1;return 0; }got+=(size_t)n; } }
    return draft->board!=0;
}
int members_store_draft(uint32_t user,const MemberDraft *draft,uint32_t expected,int remove,uint32_t *revision) {
    if(fd<0 || broken || !members_name(user)) return -1;
    DraftIndex *current=&(*user_at(user-1)).draft;
    if(expected!=current->revision && !(expected==0 && !current->board && !remove)) return 0;
    if(remove && !current->board) return 0;
    if(current->revision==UINT32_MAX) return -1;
    size_t tl=remove?0:strlen(draft->title),bl=remove?0:strlen(draft->body);
    if(!remove && (!draft->board || !tl || tl>80 || bl>CONTENT_BODY)) return -1;
    if(!remove) {
        if(draft->board>10000 || draft->parent>CONTENT_ID_MAX) return -1;
        for(size_t i=0;i<tl;i++) if((unsigned char)draft->title[i]<32 || (unsigned char)draft->title[i]==127) return -1;
        for(size_t i=0;i<bl;i++) {
            unsigned char c=(unsigned char)draft->body[i];
            if(c=='\r') { if(i+1>=bl || draft->body[i+1]!='\n') return -1; i++; }
            else if(c<32 || c==127) return -1;
        }
        if(bl && (bl<2 || draft->body[bl-2]!='\r' || draft->body[bl-1]!='\n')) return -1;
    }
    unsigned char row[CONTENT_BODY+128]={0};size_t row_size=bl>1024?116+bl:1152; row[0]=bl>1024?'d':'D'; row[1]=(unsigned char)!remove;
    put32(row+4,user); put32(row+16,current->revision+1); put32(row+20,(uint32_t)tl); put32(row+24,(uint32_t)bl);
    if(!remove) { put32(row+8,draft->board); put32(row+12,draft->parent); memcpy(row+32,draft->title,tl); memcpy(row+112,draft->body,bl); }
    if(!append_size(row,row_size)) { OPENSSL_cleanse(row,sizeof(row)); return -1; }
    *revision=current->revision+1;
    memset(current,0,sizeof(*current));if(!remove) { current->board=draft->board;current->parent=draft->parent;strcpy(current->title,draft->title);current->length=(uint32_t)bl;current->offset=(uint64_t)SEEK(fd,0,SEEK_CUR)-row_size+112; }
    current->revision=*revision; OPENSSL_cleanse(row,sizeof(row)); return 1;
}

void members_chat_preferences(uint32_t user,ChatPreferences *out) {
    memset(out,0,sizeof(*out));if(!members_name(user))return;
    out->revision=(*user_at(user-1)).chat_revision;out->deny=(*user_at(user-1)).chat_deny;
    for(int i=0;i<32;i++)if((*user_at(user-1)).muted[i])strcpy(out->muted[i],members_name((*user_at(user-1)).muted[i]));
}
int members_chat_control(uint32_t user,const char *command) {
    if(fd<0 || broken || !members_name(user))return -1;
    if(!strcmp(command,"/MUTES"))return 1;
    unsigned char row[128]={0};put32(row+4,user);int slot=-1,enabled=0;
    if(!strcmp(command,"/TALK ON") || !strcmp(command,"/TALK OFF")) {
        int deny=!strcmp(command,"/TALK OFF");if((*user_at(user-1)).chat_deny==deny)return 1;
        row[0]='C';row[8]=(unsigned char)deny;
    } else {
        const char *name;
        if(!strncmp(command,"/MUTE ",6)) { enabled=1;name=command+6; }
        else if(!strncmp(command,"/UNMUTE ",8))name=command+8;
        else return 0;
        uint32_t target=0;for(uint32_t i=0;i<count;i++)if(!strcmp(name,(*user_at(i)).name))target=i+1;
        if(!target || target==user)return 0;
        int empty=-1;
        for(int i=0;i<32;i++) { if((*user_at(user-1)).muted[i]==target)slot=i;if(!(*user_at(user-1)).muted[i])empty=i; }
        if((enabled && slot>=0) || (!enabled && slot<0))return 1;
        if(enabled) { slot=empty;if(slot<0)return 0; }
        row[0]='M';row[8]=(unsigned char)enabled;put32(row+12,target);
    }
    if((*user_at(user-1)).chat_revision==UINT32_MAX || !append(row))return -1;
    if(row[0]=='C')(*user_at(user-1)).chat_deny=row[8];
    else (*user_at(user-1)).muted[slot]=enabled?get32(row+12):0;
    (*user_at(user-1)).chat_revision++;return 1;
}

void members_mail_notify(uint32_t user,int *deny,uint32_t *revision) {
    *deny=0;*revision=0;if(!members_name(user))return;
    *deny=(*user_at(user-1)).mail_notify_deny;*revision=(*user_at(user-1)).mail_notify_revision;
}
int members_set_mail_notify(uint32_t user,int deny) {
    if(fd<0 || broken || !members_name(user) || (deny!=0 && deny!=1))return 0;
    if((*user_at(user-1)).mail_notify_deny==deny)return 1;
    if((*user_at(user-1)).mail_notify_revision==UINT32_MAX)return 0;
    unsigned char row[128]={0};row[0]='N';put32(row+4,user);row[8]=(unsigned char)deny;
    if(!append(row))return 0;
    (*user_at(user-1)).mail_notify_deny=deny;(*user_at(user-1)).mail_notify_revision++;return 1;
}

int members_level(uint32_t user) { return members_name(user)?(*user_at(user-1)).level:0; }
uint32_t members_groups(uint32_t user) { return members_name(user)?(*user_at(user-1)).groups:0; }
int members_pending(uint32_t user) { return members_active(user) && (*user_at(user-1)).pending; }
static void rights_record(unsigned char *row,uint32_t user,uint32_t actor) {
    put32(row+4,user);put32(row+16,actor);
    time_t now=time(NULL);uint64_t stamp=now>0?(uint64_t)now:0;
    put32(row+20,(uint32_t)stamp);put32(row+24,(uint32_t)(stamp>>32));
}
int members_rights(uint32_t user,uint32_t actor,int level,uint32_t groups) {
    if(!members_name(user) || !members_name(actor) || level<0 || level>5)return 0;
    if((*user_at(user-1)).level==level && (*user_at(user-1)).groups==groups)return 2;
    unsigned char row[128]={0};row[0]='L';row[8]=(unsigned char)level;put32(row+12,groups);rights_record(row,user,actor);
    if(!append(row))return -1;
    (*user_at(user-1)).level=level;(*user_at(user-1)).groups=groups;return 1;
}
int members_approve(uint32_t user,uint32_t actor) {
    if(!members_pending(user) || !members_name(actor))return 0;
    unsigned char row[128]={0};row[0]='V';rights_record(row,user,actor);
    if(!append(row))return -1;
    (*user_at(user-1)).pending=0;return 1;
}

uint32_t members_group_word(uint32_t user,unsigned word) { return !members_name(user) || word>3?0:word?(*user_at(user-1)).group_high[word-1]:(*user_at(user-1)).groups; }
int members_in_group(uint32_t user,unsigned group) { return group && group<=100 && (members_group_word(user,(group-1)/32)&(1u<<((group-1)%32)))!=0; }
int members_group_change(uint32_t user,uint32_t actor,unsigned group,int add) {
    if(!members_name(user) || !members_name(actor) || !group || group>100)return 0;
    if(members_in_group(user,group)==!!add)return 2;
    unsigned char row[128]={0};row[0]='G';put32(row+4,user);
    for(unsigned i=0;i<4;i++)put32(row+8+i*4,members_group_word(user,i));
    unsigned at=8+4*((group-1)/32);uint32_t word=get32(row+at),mask=1u<<((group-1)%32);put32(row+at,add?word|mask:word&~mask);put32(row+24,actor);
    uint64_t now=(uint64_t)time(NULL);put32(row+28,(uint32_t)now);put32(row+32,(uint32_t)(now>>32));if(!append(row))return -1;
    (*user_at(user-1)).groups=get32(row+8);for(unsigned i=0;i<3;i++)(*user_at(user-1)).group_high[i]=get32(row+12+i*4);return 1;
}
int members_group_move(uint32_t actor,unsigned from,unsigned to) {
    if(!members_name(actor) || !from || from>100 || !to || to>100 || from==to)return 0;
    unsigned char row[128]={0};row[0]='g';put32(row+4,from);put32(row+8,to);put32(row+12,actor);uint64_t now=(uint64_t)time(NULL);put32(row+16,(uint32_t)now);put32(row+20,(uint32_t)(now>>32));if(!append(row))return -1;
    for(uint32_t u=0;u<count;u++) { uint32_t *a=from<=32?&(*user_at(u)).groups:&(*user_at(u)).group_high[(from-1)/32-1],*b=to<=32?&(*user_at(u)).groups:&(*user_at(u)).group_high[(to-1)/32-1];if(*a&(1u<<((from-1)%32))) { *a&=~(1u<<((from-1)%32));*b|=1u<<((to-1)%32); } }return 1;
}
void members_ui_preferences(uint32_t user,int *editor,unsigned *columns,unsigned *rows,unsigned *aux) {
    *editor=0;*columns=0;*rows=0;*aux=0;if(!members_name(user))return;
    *aux=(*user_at(user-1)).aux;*editor=(*user_at(user-1)).editor;if((*user_at(user-1)).columns)*columns=(*user_at(user-1)).columns;if((*user_at(user-1)).rows)*rows=(*user_at(user-1)).rows;
}
int members_save_ui_preferences(uint32_t user,int language,int pager,int editor,unsigned columns,unsigned rows,unsigned aux) {
    if(!members_name(user) || language<0 || language>1 || pager<0 || pager>1 || editor<0 || editor>1 || aux>15 || columns<20 || columns>240 || rows<5 || rows>100)return 0;
    if((*user_at(user-1)).preferences && (*user_at(user-1)).language==language && (*user_at(user-1)).pager==pager && (*user_at(user-1)).editor==editor && (*user_at(user-1)).columns==columns && (*user_at(user-1)).rows==rows && (*user_at(user-1)).aux==aux)return 1;
    unsigned char row[128]={0};row[0]='f';put32(row+4,user);row[8]=(unsigned char)language;row[9]=(unsigned char)pager;row[11]=(unsigned char)aux;row[10]=(unsigned char)editor;put32(row+12,columns);put32(row+16,rows);
    if(!append(row))return 0;
    (*user_at(user-1)).preferences=1;(*user_at(user-1)).language=language;(*user_at(user-1)).pager=pager;(*user_at(user-1)).aux=aux;(*user_at(user-1)).editor=editor;(*user_at(user-1)).columns=columns;(*user_at(user-1)).rows=rows;return 1;
}

int members_profile_marker(uint32_t user) { if(!user)return profile_required;if(profile_required)return 1;if(!members_name(user))return 0;unsigned char row[128]={0};row[0]='p';put32(row+4,user);if(!append(row))return 0;profile_required=1;return 1; }

/* Offline snapshot; source stays locked and untouched. Internal setting/draft
 * revisions restart because no live sessions may retain them across this copy. */
static int snapshot_row(int out,unsigned char *row,size_t size) {
    put32(row+size-4,sum(row,size-4));size_t done=0;
    while(done<size) { int n=(int)WRITE(out,row+done,(unsigned)(size-done));if(n<=0)return 0;done+=(size_t)n; }return 1;
}
int members_compact(const char *output) {
    if(fd<0 || broken)return 0;
#ifdef _WIN32
    int out=_open(output,_O_RDWR|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
    int out=open(output,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
#endif
    if(out<0)return 0;
    int ok=WRITE(out,"RTMEMB01",8)==8;
    unsigned char row[CONTENT_BODY+128];
    for(uint32_t id=1;ok && id<=count;id++) {
        memset(row,0,128);row[0]=(*user_at(id-1)).pending?'J':'A';row[1]=(unsigned char)(*user_at(id-1)).pending;put32(row+4,id);memcpy(row+8,(*user_at(id-1)).name,strlen((*user_at(id-1)).name));memcpy(row+40,(*user_at(id-1)).salt,16);memcpy(row+56,(*user_at(id-1)).hash,32);put32(row+88,ITERATIONS);ok=snapshot_row(out,row,128);
    }
    for(uint32_t id=1;ok && id<=count;id++) {
        if((*user_at(id-1)).deleted) {
            memset(row,0,128);row[0]='X';row[8]=(unsigned char)(*user_at(id-1)).deleted;put32(row+4,id);put32(row+12,(*user_at(id-1)).removed_actor);put32(row+16,(uint32_t)(*user_at(id-1)).removed_time);put32(row+20,(uint32_t)((*user_at(id-1)).removed_time>>32));ok=snapshot_row(out,row,128);continue;
        }
        memset(row,0,128);row[0]='L';row[8]=(unsigned char)(*user_at(id-1)).level;put32(row+4,id);put32(row+12,(*user_at(id-1)).groups);put32(row+16,id);ok=snapshot_row(out,row,128);
        if(ok && ((*user_at(id-1)).group_high[0] || (*user_at(id-1)).group_high[1] || (*user_at(id-1)).group_high[2])) { memset(row,0,128);row[0]='G';put32(row+4,id);for(unsigned i=0;i<4;i++)put32(row+8+4*i,members_group_word(id,i));put32(row+24,id);ok=snapshot_row(out,row,128); }
        if(ok && (*user_at(id-1)).preferences) { memset(row,0,128);row[0]=(*user_at(id-1)).columns?'f':'F';put32(row+4,id);row[8]=(unsigned char)(*user_at(id-1)).language;row[9]=(unsigned char)(*user_at(id-1)).pager;if(row[0]=='f') { row[11]=(unsigned char)(*user_at(id-1)).aux;row[10]=(unsigned char)(*user_at(id-1)).editor;put32(row+12,(*user_at(id-1)).columns);put32(row+16,(*user_at(id-1)).rows); }ok=snapshot_row(out,row,128); }
        if(ok && (*user_at(id-1)).chat_deny) { memset(row,0,128);row[0]='C';put32(row+4,id);row[8]=1;ok=snapshot_row(out,row,128); }
        for(unsigned j=0;ok && j<32;j++)if((*user_at(id-1)).muted[j]) { memset(row,0,128);row[0]='M';put32(row+4,id);row[8]=1;put32(row+12,(*user_at(id-1)).muted[j]);ok=snapshot_row(out,row,128); }
        if(ok && (*user_at(id-1)).mail_notify_deny) { memset(row,0,128);row[0]='N';put32(row+4,id);row[8]=1;ok=snapshot_row(out,row,128); }
        for(size_t j=0;ok && j<=(size_t)(*user_at(id-1)).last_seen/8;j++)if(seen_page(id,(uint32_t)(j*8))[j%512])for(unsigned bit=0;ok && bit<8;bit++)if(seen_page(id,(uint32_t)(j*8))[j%512]&(1u<<bit)) { memset(row,0,128);row[0]='R';put32(row+4,id);put32(row+8,(uint32_t)(j*8+bit));row[12]=1;ok=snapshot_row(out,row,128); }
        if(ok && (*user_at(id-1)).draft.board) {
            MemberDraft draft;if(!members_draft(id,&draft)) { ok=0;break; }size_t tl=strlen(draft.title),bl=strlen(draft.body),size=116+bl;if(size<128)size=128;
            memset(row,0,size);row[0]='d';row[1]=1;put32(row+4,id);put32(row+8,draft.board);put32(row+12,draft.parent);put32(row+16,1);put32(row+20,(uint32_t)tl);put32(row+24,(uint32_t)bl);memcpy(row+32,draft.title,tl);memcpy(row+112,draft.body,bl);ok=snapshot_row(out,row,size);OPENSSL_cleanse(&draft,sizeof(draft));
        }
    }
    if(ok && profile_required) { memset(row,0,128);row[0]='p';put32(row+4,1);ok=snapshot_row(out,row,128); }
    if(ok)ok=SYNC(out)==0;
    CLOSE(out);OPENSSL_cleanse(row,sizeof(row));return ok;
}

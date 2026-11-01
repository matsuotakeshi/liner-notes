#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
/* Single worker owns this private journal; callers supply authenticated IDs. */
#include "mailbox.h"
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


#include "store_limits.h"
#include "app_limits.h"
#include "vault.h"
static int fd=-1,broken;
static uint32_t sequence,count;
typedef struct { unsigned used,references; } AttachmentState;
#include "db_index.h"
static DbIndex attachment_db;
static AttachmentState *attachment_at(uint32_t id) { return db_at(&attachment_db,id); }
static uint32_t attachment_last;static uint64_t record_offset;
static int remember(uint32_t id) { if(!id || id>VAULT_ID_MAX)return 0;(*attachment_at(id)).used=1;if(id>attachment_last)attachment_last=id;return 1; }
#include "db_index.h"
static DbIndex message_db,draft_db;
static MailMessage *message_at(uint64_t i) { return db_at(&message_db,i); }
static MailMessage *draft_at(uint64_t i) { return db_at(&draft_db,i); }
static uint32_t get32(const unsigned char *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(unsigned char *p,uint32_t n) { for(int i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i)); }
static uint32_t checksum(const unsigned char *p,size_t size) { uint32_t h=2166136261u;for(size_t i=0;i<size-4;i++)h=(h^p[i])*16777619u;return h; }
static int text_ok(const char *s,int body) {
    size_t n=strlen(s);if(!n || n>(body?CONTENT_BODY:80))return 0;
    for(size_t i=0;i<n;i++) {
        unsigned c=(unsigned char)s[i];
        if(body && c=='\r' && i+1<n && s[i+1]=='\n') { i++;continue; }
        if(c<32 || c==127)return 0;
    }
    return !body || (n>=2 && s[n-2]=='\r' && s[n-1]=='\n');
}
static int read_all(unsigned char *p,size_t n) {
    size_t done=0;while(done<n) { int k=(int)READ(fd,p+done,(unsigned)(n-done));if(k<0)return -1;if(!k)break;done+=(size_t)k; }return (int)done;
}
static int write_all(const unsigned char *p,size_t n) {
    while(n) { int k=(int)WRITE(fd,p,(unsigned)n);if(k<=0)return 0;p+=k;n-=(size_t)k; }return 1;
}
void mailbox_close(void) { if(fd>=0 && !broken) { uint32_t meta[]={count,sequence,attachment_last};db_cache_store(&message_db,&fd,1,meta,sizeof(meta));db_cache_store(&draft_db,&fd,1,meta,sizeof(meta));db_cache_store(&attachment_db,&fd,1,meta,sizeof(meta)); }db_close(&message_db);db_close(&draft_db);if(fd>=0)CLOSE(fd);fd=-1;count=sequence=0;db_close(&attachment_db);attachment_last=0; }
int mailbox_broken(void) { return broken; }
uint32_t mailbox_search_next(uint32_t start,const char *text) { return db_search_next(&message_db,start,text,0); }
uint32_t mailbox_count(void) { return count; }
const MailMessage *mailbox_get(uint32_t id,uint32_t user) {
    if(broken || !id || id>count || !user)return NULL;
    MailMessage *m=&(*message_at(id-1));return !m->deleted && (m->sender==user || m->recipient==user)?m:NULL;
}
unsigned mailbox_unread(uint32_t user) {
    if(!message_db.sql || !user)return 0;
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(message_db.sql,"SELECT count(*) FROM mail_flags WHERE recipient=? AND unread=1 AND deleted=0",-1,&q,NULL)!=SQLITE_OK)db_die(&message_db);
    sqlite3_bind_int64(q,1,user);if(sqlite3_step(q)!=SQLITE_ROW)db_die(&message_db);unsigned n=(unsigned)sqlite3_column_int64(q,0);sqlite3_finalize(q);return n;
}
const MailMessage *mailbox_draft(uint32_t user) { return !broken && user && user<=MEMBER_MAX && (*draft_at(user)).id?&(*draft_at(user)):NULL; }
uint32_t mailbox_attachment_last(void) { return attachment_last; }
const char *mailbox_body(const MailMessage *m) {
    static char body[CONTENT_BODY+1];
    if(broken || !m || SEEK(fd,(int64_t)m->body_offset,SEEK_SET)<0 || read_all((unsigned char *)body,m->body_size)!=(int)m->body_size) { broken=1;return ""; }
    body[m->body_size]=0;return body;
}
int mailbox_attachment_used(uint32_t id) { return id && id<=attachment_last && (*attachment_at(id)).used; }
int mailbox_attachment_referenced(uint32_t id) { return id<=attachment_last && (*attachment_at(id)).references; }
static void reference(const MailMessage *m,int add) { for(unsigned i=0;i<m->attachments.count;i++) { uint32_t id=m->attachments.ids[i];if(add)(*attachment_at(id)).references++;else if((*attachment_at(id)).references)(*attachment_at(id)).references--; } }
int mailbox_attachment_readable(uint32_t id,uint32_t user) {
    if(broken || !user || !id)return 0;
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(message_db.sql,"SELECT 1 FROM mail_links a JOIN mail_flags m ON a.message=m.id WHERE a.file=? AND m.deleted=0 AND (m.sender=? OR m.recipient=?) LIMIT 1",-1,&q,NULL)!=SQLITE_OK)db_die(&message_db);
    sqlite3_bind_int64(q,1,id);sqlite3_bind_int64(q,2,user);sqlite3_bind_int64(q,3,user);int rc=sqlite3_step(q);if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)db_die(&message_db);sqlite3_finalize(q);return rc==SQLITE_ROW;
}
static int apply(const unsigned char *r,size_t row_size) {
    int multiple=r[0]=='u' || r[0]=='x';unsigned attach_count=multiple?r[2]:(r[0]=='t' || r[0]=='v');
    if(attach_count>MAIL_ATTACH_MAX || (multiple && attach_count<2))return 0;
    char kind=(r[0]=='t' || r[0]=='u')?'S':(r[0]=='v' || r[0]=='x')?'W':(char)r[0];if(kind>='a' && kind<='z')kind-=('a'-'A');
    uint32_t id=get32(r+8),actor=get32(r+12);
    if((r[2] && !multiple) || r[3] || (!(kind=='G' && actor==0) && !members_name(actor)) || actor>MEMBER_MAX || (r[1] && (kind!='S' || r[1]!=1 || !(*draft_at(actor)).id)))return 0;
    if(kind=='S' || kind=='G' || kind=='W') {
        uint32_t from=get32(r+16),to=get32(r+20),parent=get32(r+24),tl=get32(r+28),bl=get32(r+32);
        uint64_t stamp=get32(r+36)|((uint64_t)get32(r+40)<<32);
        if(((kind=='S' || kind=='G') && (count>=CONTENT_ID_MAX || id!=count+1)) || (kind=='W' && id!=get32(r+4)) || actor!=from || (kind=='G' && (from || parent || r[1])) || !members_name(to) || parent>count || tl>80 || bl>CONTENT_BODY || bl>row_size-128 || stamp>253402300799ULL)return 0;
        if(parent && ((*message_at(parent-1)).recipient!=from || (*message_at(parent-1)).sender!=to || (*message_at(parent-1)).deleted))return 0;
        for(size_t i=44+tl;i<124;i++)if(r[i])return 0;
        MailAttachments attachments={0};attachments.count=attach_count;
        if(attach_count && row_size!=128+bl+4*attach_count)return 0;
        for(unsigned i=0;i<attach_count;i++) { uint32_t id=get32(r+124+bl+4*i);if(!id || id>VAULT_ID_MAX)return 0;for(unsigned j=0;j<i;j++)if(attachments.ids[j]==id)return 0;attachments.ids[i]=id; }
        for(size_t i=124+bl+(4*attach_count);i<row_size-4;i++)if(r[i])return 0;
        if(memchr(r+44,0,tl) || memchr(r+124,0,bl))return 0;
        MailMessage m={0};m.id=id;m.sender=from;m.recipient=to;m.parent=parent;m.time=stamp;m.attachments=attachments;m.body_size=bl;m.body_offset=record_offset+124;for(unsigned i=0;i<attach_count;i++)if(!remember(attachments.ids[i]))return 0;
        char body[CONTENT_BODY+1]={0};memcpy(m.title,r+44,tl);memcpy(body,r+124,bl);
        if(!text_ok(m.title,0) || (!text_ok(body,1) && !(kind=='W' && !bl)))return 0;
        if(kind=='W') { if((*draft_at(actor)).id)reference(&(*draft_at(actor)),0);(*draft_at(actor))=m;reference(&m,1); }
        else { db_search_set(&message_db,id,m.title,body,bl);(*message_at(count++))=m;reference(&m,1);if(r[1]) { reference(&(*draft_at(actor)),0);memset(&(*draft_at(actor)),0,sizeof((*draft_at(actor)))); } }
    } else {
        for(size_t i=16;i<row_size-4;i++)if(r[i])return 0;
        if(kind=='A')return remember(id);
        if(kind=='C') { if(!(*draft_at(actor)).id || (*draft_at(actor)).id!=id)return 0;reference(&(*draft_at(actor)),0);memset(&(*draft_at(actor)),0,sizeof((*draft_at(actor))));return 1; }
        const MailMessage *m=mailbox_get(id,actor);if(!m)return 0;
        if(kind=='R' && m->recipient==actor)(*message_at(id-1)).read=1;
        else if(kind=='D') { reference(m,0);(*message_at(id-1)).deleted=1; }
        else return 0;
    }
    if(kind=='S' || kind=='G' || kind=='R' || kind=='D') {
        const MailMessage *m=&(*message_at(id-1));sqlite3_stmt *q=NULL;
        if(sqlite3_prepare_v2(message_db.sql,"INSERT INTO mail_flags VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET unread=excluded.unread,deleted=excluded.deleted",-1,&q,NULL)!=SQLITE_OK)db_die(&message_db);
        sqlite3_bind_int64(q,1,id);sqlite3_bind_int64(q,2,m->sender);sqlite3_bind_int64(q,3,m->recipient);sqlite3_bind_int(q,4,!m->read);sqlite3_bind_int(q,5,m->deleted);if(sqlite3_step(q)!=SQLITE_DONE)db_die(&message_db);sqlite3_finalize(q);
        if(kind=='S' || kind=='G')for(unsigned i=0;i<m->attachments.count;i++) {
            if(sqlite3_prepare_v2(message_db.sql,"INSERT INTO mail_links VALUES(?,?)",-1,&q,NULL)!=SQLITE_OK)db_die(&message_db);
            sqlite3_bind_int64(q,1,m->attachments.ids[i]);sqlite3_bind_int64(q,2,id);if(sqlite3_step(q)!=SQLITE_DONE)db_die(&message_db);sqlite3_finalize(q);
        }
    }
    return 1;
}
int mailbox_open(const char *base) {
    mailbox_close();broken=0;if(!base)return 1;
    char path[4096];int n=snprintf(path,sizeof(path),"%s.mail",base);if(n<0 || (size_t)n>=sizeof(path))return 0;
    fd=OPEN(path);if(fd<0)return 0;
#ifdef _WIN32
    OVERLAPPED ov={0};if(!LockFileEx((HANDLE)_get_osfhandle(fd),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&ov))goto fail;
#else
    struct flock lock={0};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;if(storage_lock(fd,&lock))goto fail;
    char dir[4096];strcpy(dir,path);char *slash=strrchr(dir,'/');if(slash) { if(slash==dir)slash[1]=0;else *slash=0; }else strcpy(dir,".");
    int d=open(dir,O_RDONLY);if(d<0)goto fail;int e=storage_sync(d);storage_close(d);if(e)goto fail;
#endif
    db_open_cached(&message_db,path,"mail-index",sizeof(MailMessage));db_open_cached(&draft_db,path,"draft-index",sizeof(MailMessage));db_open_cached(&attachment_db,path,"attachment-index",sizeof(AttachmentState));
    uint32_t meta[3],other[3],third[3];if(db_cache_load(&message_db,&fd,1,meta,sizeof(meta)) && db_cache_load(&draft_db,&fd,1,other,sizeof(other)) && db_cache_load(&attachment_db,&fd,1,third,sizeof(third)) && !memcmp(meta,other,sizeof(meta)) && !memcmp(meta,third,sizeof(meta))) { count=meta[0];sequence=meta[1];attachment_last=meta[2];return 1; }
    db_reset(&message_db);db_reset(&draft_db);db_reset(&attachment_db);db_search_open(&message_db);
    db_exec(&message_db,"DROP TABLE IF EXISTS mail_flags;DROP TABLE IF EXISTS mail_links;CREATE TABLE mail_flags(id INTEGER PRIMARY KEY,sender INTEGER NOT NULL,recipient INTEGER NOT NULL,unread INTEGER NOT NULL,deleted INTEGER NOT NULL);CREATE INDEX mail_unread ON mail_flags(recipient,unread,deleted);CREATE TABLE mail_links(file INTEGER,message INTEGER,PRIMARY KEY(file,message)) WITHOUT ROWID;");
    unsigned char row[CONTENT_BODY+128+4*MAIL_ATTACH_MAX];n=read_all(row,8);
    if(!n) { if(!write_all((const unsigned char *)"LNMAIL01",8) || SYNC(fd))goto fail; }
    else if(n!=8 || memcmp(row,"LNMAIL01",8))goto fail;
    while((n=read_all(row,128))!=0) {
        if(n!=128)goto fail;
        int compact=row[0] && strchr("swrdctvagux",row[0])!=NULL;
        uint32_t bl=get32(row+32);
        if(compact && (bl>CONTENT_BODY || ((row[0]=='u' || row[0]=='x') && row[2]>MAIL_ATTACH_MAX)))goto fail;
        size_t row_size=compact?128+bl+((row[0]=='t' || row[0]=='v')?4:(row[0]=='u' || row[0]=='x')?4*row[2]:0):1152;
        record_offset=(uint64_t)SEEK(fd,0,SEEK_CUR)-128;
        if(read_all(row+128,row_size-128)!=(int)(row_size-128) || checksum(row,row_size)!=get32(row+row_size-4) || sequence==UINT32_MAX || get32(row+4)!=sequence+1 || !apply(row,row_size))goto fail;
        sequence++;
    }
    return 1;
fail:broken=1;mailbox_close();return 0;
}
static int append(unsigned char *row) {
    if(fd<0 || broken || sequence==UINT32_MAX)return 0;
    uint32_t bl=get32(row+32);int attached_row=row[0]=='t' || row[0]=='v' || row[0]=='u' || row[0]=='x';size_t row_size=attached_row?128+bl+4*((row[0]=='u' || row[0]=='x')?row[2]:1):bl>1024?128+bl:1152;
    if(bl>1024 && !attached_row)row[0]=(unsigned char)(row[0]+('a'-'A'));
    put32(row+4,sequence+1);put32(row+row_size-4,checksum(row,row_size));
    int64_t start=SEEK(fd,0,SEEK_END);record_offset=(uint64_t)start;
    if(start<0 || !write_all(row,row_size) || SYNC(fd)) { broken=1;return 0; }
    if(!apply(row,row_size)) { broken=1;return 0; }sequence++;return 1;
}
static int encode_attachments(unsigned char *row,const MailAttachments *a,const char *body,char kind) {
    row[0]=(unsigned char)kind;if(!a || !a->count)return 1;
    if(a->count>MAIL_ATTACH_MAX || kind=='G')return 0;
    row[0]=(unsigned char)(kind=='W'?(a->count==1?'v':'x'):(a->count==1?'t':'u'));
    if(a->count>1)row[2]=(unsigned char)a->count;
    for(unsigned i=0;i<a->count;i++) { if(!a->ids[i] || a->ids[i]>VAULT_ID_MAX)return 0;for(unsigned j=0;j<i;j++)if(a->ids[i]==a->ids[j])return 0;put32(row+124+strlen(body)+4*i,a->ids[i]); }
    return 1;
}
int mailbox_send(uint32_t from,uint32_t to,uint32_t parent,const char *title,const char *body,const MailAttachments *attachments,uint32_t revision,uint32_t *id) {
    if(revision && (!mailbox_draft(from) || (*draft_at(from)).id!=revision))return 0;
    if(count>=CONTENT_ID_MAX || (limit_mail && count>=limit_mail) || (from && !members_name(from)) || (!from && (parent || (attachments && attachments->count) || revision)) || !members_name(to) || !text_ok(title,0) || !text_ok(body,1))return 0;
    const MailMessage *old=parent?mailbox_get(parent,from):NULL;
    if(parent && (!old || old->recipient!=from || old->sender!=to))return 0;
    unsigned char row[CONTENT_BODY+128+4*MAIL_ATTACH_MAX]={0};if(!encode_attachments(row,attachments,body,from?'S':'G'))return 0;row[1]=revision?1:0;put32(row+8,count+1);put32(row+12,from);put32(row+16,from);put32(row+20,to);put32(row+24,parent);
    put32(row+28,(uint32_t)strlen(title));put32(row+32,(uint32_t)strlen(body));
    time_t now=time(NULL);uint64_t stamp=now>0?(uint64_t)now:0;put32(row+36,(uint32_t)stamp);put32(row+40,(uint32_t)(stamp>>32));
    memcpy(row+44,title,strlen(title));memcpy(row+124,body,strlen(body));
    if(!append(row))return 0;
    *id=count;return 1;
}
int mailbox_mark(uint32_t id,uint32_t user) {
    const MailMessage *m=mailbox_get(id,user);if(!m)return 0;if(m->recipient!=user || m->read)return 1;
    unsigned char row[CONTENT_BODY+128+4*MAIL_ATTACH_MAX]={0};row[0]='R';put32(row+8,id);put32(row+12,user);return append(row);
}
int mailbox_remove(uint32_t id,uint32_t user) {
    if(!mailbox_get(id,user))return 0;
    unsigned char row[CONTENT_BODY+128+4*MAIL_ATTACH_MAX]={0};row[0]='D';put32(row+8,id);put32(row+12,user);return append(row);
}

int mailbox_save_draft(uint32_t user,uint32_t to,uint32_t parent,const char *title,const char *body,const MailAttachments *attachments,uint32_t expected,uint32_t *revision) {
    if(!user || user>MEMBER_MAX || !members_name(user) || !members_name(to) || (*draft_at(user)).id!=expected || !text_ok(title,0) || (*body && !text_ok(body,1)))return 0;
    const MailMessage *old=parent?mailbox_get(parent,user):NULL;
    if(parent && (!old || old->recipient!=user || old->sender!=to))return 0;
    unsigned char row[CONTENT_BODY+128+4*MAIL_ATTACH_MAX]={0};if(!encode_attachments(row,attachments,body,'W'))return 0;put32(row+8,sequence+1);put32(row+12,user);put32(row+16,user);put32(row+20,to);put32(row+24,parent);
    put32(row+28,(uint32_t)strlen(title));put32(row+32,(uint32_t)strlen(body));
    memcpy(row+44,title,strlen(title));memcpy(row+124,body,strlen(body));
    if(!append(row))return 0;
    *revision=(*draft_at(user)).id;return 1;
}
int mailbox_delete_draft(uint32_t user,uint32_t expected) {
    const MailMessage *d=mailbox_draft(user);if(!d || d->id!=expected)return 0;
    unsigned char row[CONTENT_BODY+128+4*MAIL_ATTACH_MAX]={0};row[0]='C';put32(row+8,expected);put32(row+12,user);return append(row);
}

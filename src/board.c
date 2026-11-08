#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "runtime_limits.h"
#include "app_limits.h"
#include "activity.h"
/* Stage 2: single-owner board store, bounded asynchronous request queue. */
#include "board.h"
#include "date_filter.h"
#include "vault_bridge.h"
#include "vault_worker.h"
#include "identity_legacy.h"
#include "members.h"
#include "catalog.h"
#include "board_policy.h"
#include "durable.h"
#include "moderation.h"
#include "extras.h"
#include "profile_store.h"
#include "mailbox.h"
#include "mail_files.h"
#include "messages.h"
#include <openssl/crypto.h>
#include <openssl/rand.h>
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
#include <process.h>
static CRITICAL_SECTION mutex;
static CONDITION_VARIABLE condition;
static HANDLE worker;
#define LOCK() EnterCriticalSection(&mutex)
#define UNLOCK() LeaveCriticalSection(&mutex)
#define WAKE() WakeConditionVariable(&condition)
#define WAIT() SleepConditionVariableCS(&condition,&mutex,INFINITE)
#define SYNC(fd) _commit(fd)
#define CLOSE(fd) _close(fd)
#define READ(fd,b,n) _read(fd,b,(unsigned)(n))
#define WRITE(fd,b,n) _write(fd,b,(unsigned)(n))
#define SEEK(fd,n,w) _lseeki64(fd,n,w)
#else
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static pthread_t worker;
#define LOCK() pthread_mutex_lock(&mutex)
#define UNLOCK() pthread_mutex_unlock(&mutex)
#define WAKE() pthread_cond_signal(&condition)
#define WAIT() pthread_cond_wait(&condition,&mutex)
#define SYNC(fd) storage_sync(fd)
#define CLOSE(fd) storage_close(fd)
#define READ(fd,b,n) storage_read(fd,b,n)
#define WRITE(fd,b,n) storage_write(fd,b,n)
#define SEEK(fd,n,w) storage_seek(fd,n,w)
#endif
#include "store_limits.h"
/* Completion notifications wake the socket loop without busy polling. A full
 * pipe already represents pending work; notifications may safely coalesce. */
#ifndef _WIN32
static int completion_pipe[2]={-1,-1};
static void completion_close(void) {
    for(unsigned i=0;i<2;i++)if(completion_pipe[i]>=0) { close(completion_pipe[i]);completion_pipe[i]=-1; }
}
static int completion_open(void) {
    if(pipe(completion_pipe))return 0;
    for(unsigned i=0;i<2;i++)if(fcntl(completion_pipe[i],F_SETFL,O_NONBLOCK) || fcntl(completion_pipe[i],F_SETFD,FD_CLOEXEC)) { completion_close();return 0; }
    return 1;
}
int board_completion_fd(void) { return completion_pipe[0]; }
void board_completion_notify(void) { if(completion_pipe[1]>=0) { int saved=errno;char byte=1;ssize_t n;do { n=write(completion_pipe[1],&byte,1); }while(n<0 && errno==EINTR);errno=saved; } }
void board_completion_drain(void) { if(completion_pipe[0]>=0) { char bytes[128];while(read(completion_pipe[0],bytes,sizeof(bytes))>0) {} } }
#else
static int completion_open(void) { return 1; }
static void completion_close(void) {}
int board_completion_fd(void) { return -1; }
void board_completion_notify(void) {}
void board_completion_drain(void) {}
#endif
uint32_t limit_articles,limit_mail,limit_user_posts;
#define QUEUE 32
/* Disk format: magic(8), then fixed-size records, explicit little endian.
   id,parent,title length,body length: 4 bytes each; title 80, body 1024,
   FNV-1a checksum 4. No compiler struct layout is written to disk. */
#define RECORD_SIZE (16+BOARD_TITLE+1024+4)
#define RECORD_MAX (16+BOARD_TITLE+BOARD_BODY+48)
typedef struct {
    uint32_t parent, root, depth, child, last_child, next, prev, replies, latest;
    uint64_t created;
    uint32_t board,origin;
    char author[33];
    char title[BOARD_TITLE+1]; uint64_t body_offset; uint32_t body_size;
} Article;
#include "db_index.h"
static DbIndex article_db;
static Article *article_at(uint64_t i) { return db_at(&article_db,i); }
static uint32_t user_posts[(MEMBER_MAX+1)];
static int version=4,format5;
static uint32_t article_count;
static int store=-1, failed, ending, running, body_read_failed;
typedef struct UiSave { struct UiSave *next;uint64_t ticket;BoardRequest request; } UiSave;
static UiSave *ui_head,*ui_tail;
static struct { int state; uint64_t ticket; BoardRequest request; BoardResult result; } jobs[QUEUE];
static uint64_t next_ticket;
static int on_board(uint32_t id,uint32_t board) { return id && id<=article_count && (*article_at(id-1)).board==board; }
static int unavailable(uint32_t id);
static int link_imported(uint32_t id);
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void put32(unsigned char *p,uint32_t n) {
    for(int i=0;i<4;i++) p[i]=(unsigned char)(n>>(8*i));
}
static const char *article_body(const Article *);
static int require_v5(void);
#include "board_features.inc"
/* Derived visibility only: never alter descendants or their signed provenance.
 * The board owner thread invalidates this cache on every relevant mutation.
 * Iterative memoization keeps deep threads linear, without recursion. */
static unsigned char *visibility;
static size_t visibility_capacity,visibility_moves;
static uint32_t visibility_articles,visibility_moderation,visibility_extras;
static int unavailable(uint32_t id) {
    uint32_t moderation=moderation_count(),extras=extras_audit_count();
    if(!id || id>article_count)return 1;
    if(!store_reserve((void **)&visibility,&visibility_capacity,(size_t)article_count+1,1)) {
        body_read_failed=1;return 1;
    }
    if(visibility_articles!=article_count || visibility_moderation!=moderation ||
       visibility_extras!=extras || visibility_moves!=move_count) {
        memset(visibility,0,(size_t)article_count+1);
        visibility_articles=article_count;visibility_moderation=moderation;
        visibility_extras=extras;visibility_moves=move_count;
    }
    uint32_t at=id;unsigned char state=1;
    while(at) {
        if(at>article_count) { state=2;break; }
        if(visibility[at]) { state=visibility[at]==1?1:2;break; }
        visibility[at]=3;
        if(moderation_hidden(at) || extras_removed(at)) { state=2;break; }
        at=article_at(at-1)->parent;
    }
    at=id;
    while(at && at<=article_count && visibility[at]==3) {
        visibility[at]=state;at=article_at(at-1)->parent;
    }
    return state!=1;
}

static void index_article(uint32_t id) {
    Article *a=&(*article_at(id-1)); a->root=id; a->latest=id;
    if(a->parent) {
        Article *p=&(*article_at(a->parent-1));
        a->root=p->root; a->depth=p->depth+1; a->prev=p->last_child;
        if(p->last_child) (*article_at(p->last_child-1)).next=id;
        else p->child=id;
        p->last_child=id;
        Article *root=&(*article_at(a->root-1)); root->replies++; root->latest=id;
    }
}
static uint32_t tree_next(uint32_t id) {
    if((*article_at(id-1)).child) return (*article_at(id-1)).child;
    while(id) {
        if((*article_at(id-1)).next) return (*article_at(id-1)).next;
        id=(*article_at(id-1)).parent;
    }
    return 0;
}
static uint32_t board_of(uint32_t id) { return id && id<=article_count?(*article_at(id-1)).board:0; }
static const char *article_body(const Article *a) {
    static char body[BOARD_BODY+1];
    if(failed || SEEK(store,(int64_t)a->body_offset,SEEK_SET)<0) { failed=body_read_failed=1;return ""; }
    size_t done=0;while(done<a->body_size) { int n=(int)READ(store,body+done,a->body_size-done);if(n<=0) { failed=body_read_failed=1;return ""; }done+=(size_t)n; }
    body[done]=0;return body;
}
static const char *mail_body(const MailMessage *m) {
    const char *body=mailbox_body(m);if(mailbox_broken())body_read_failed=1;return body;
}
static void display_article(uint32_t id,BoardResult *r,int inspect) {
    Article *a=&(*article_at(id-1)); r->selected=id;
    if(extras_removed(id)) { snprintf(r->text,sizeof(r->text),"WITHDRAWN %u PARENT %u\r\n",id,a->parent); return; }
    if(unavailable(id) && !inspect) { snprintf(r->text,sizeof(r->text),"HIDDEN %u PARENT %u\r\n",id,a->parent); return; }
    snprintf(r->text,sizeof(r->text),"ARTICLE %u PARENT %u AUTHOR %.32s%s\r\nTITLE %.80s\r\nBODY %zu\r\n%sEND\r\n",
             id,a->parent,a->author[0]?a->author:"guest",unavailable(id)?" HIDDEN 1":"",a->title,(size_t)a->body_size,article_body(a));
    if(failed) { r->selected=0;strcpy(r->text,"ERR article storage\r\n"); }
}
static uint32_t checksum(const unsigned char *p,size_t n) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<n;i++) h=(h^p[i])*16777619u;
    return h;
}
static int write_all(const void *data,size_t n) {
    const char *p=data;
    while(n) {
        int k=(int)WRITE(store,p,n);
        if(k<=0) return 0;
        p+=k; n-=(size_t)k;
    }
    return 1;
}
static int require_v5(void) {
    if(format5)return 1;
    if(version!=4 || failed)return 0;
    if(SEEK(store,0,SEEK_SET)<0 || !write_all("RTBOARD5",8) || SYNC(store)) { failed=1;return 0; }
    format5=1;return 1;
}
/* Return length actually read, or -1. Short final records are never discarded. */
static int read_record(void *data,size_t n) {
    char *p=data; size_t done=0;
    while(done<n) {
        int k=(int)READ(store,p+done,n-done);
        if(k<0) return -1;
        if(k==0) break;
        done+=(size_t)k;
    }
    return (int)done;
}
static int open_store(const char *path) {
#ifdef _WIN32
    store=_open(path,_O_RDWR|_O_CREAT|_O_BINARY,_S_IREAD|_S_IWRITE);
    OVERLAPPED ov={0};
    if(store<0) return 0;
    if(!LockFileEx((HANDLE)_get_osfhandle(store),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&ov)) return 0;
#else
    store=storage_open(path,O_RDWR|O_CREAT,0600);
    struct flock lock={0}; lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET;
    if(store<0 || storage_lock(store,&lock)) return 0;
    /* Make creation's directory entry durable before accepting posts. */
    char *dir=strdup(path); if(!dir) return 0;
    char *slash=strrchr(dir,'/');
    if(slash) { if(slash==dir) slash[1]=0; else *slash=0; }
    else { free(dir); dir=strdup("."); if(!dir) return 0; }
    int d=open(dir,O_RDONLY); free(dir);
    if(d<0) return 0;
    int sync_result=storage_sync(d); storage_close(d); if(sync_result) return 0;
#endif
    uint32_t meta[3];char overlay[4096];snprintf(overlay,sizeof(overlay),"%s.extras",path);
    int extra_fd=storage_open(overlay,O_RDONLY,0);int sources[]={store,move_journal.fd,extra_fd};int cached=extra_fd>=0 && db_cache_load(&article_db,sources,3,meta,sizeof(meta));if(extra_fd>=0)CLOSE(extra_fd);
    if(cached) { article_count=meta[0];version=(int)meta[1];format5=(int)meta[2];move_replayed=move_count;if(article_count>CONTENT_ID_MAX || version<1 || version>4 || format5>1)return 0;for(uint32_t i=0;i<article_count;i++)if(!catalog_known(article_at(i)->board))return 0;return 1; }
    db_reset(&article_db);db_search_open(&article_db);db_exec(&article_db,"DROP TABLE IF EXISTS locations;CREATE TABLE locations(id INTEGER,board INTEGER,PRIMARY KEY(id,board));");
    version=4;format5=0;
    unsigned char raw[RECORD_MAX];
    int n=read_record(raw,8);
    if(n==0) { if(!write_all("RTBOARD4",8)||SYNC(store)) return 0; }
    else if(n==8 && !memcmp(raw,"RTBOARD1",8)) version=1;
    else if(n==8 && !memcmp(raw,"RTBOARD2",8)) version=2;
    else if(n==8 && !memcmp(raw,"RTBOARD3",8)) version=3;
    else if(n==8 && !memcmp(raw,"RTBOARD5",8))format5=1;
    else if(n!=8 || memcmp(raw,"RTBOARD4",8)) return 0;
    while((n=read_record(raw,16))!=0) {
        if(n!=16 || article_count>=CONTENT_ID_MAX)return 0;
        uint32_t encoded=get32(raw),parent=get32(raw+4),tl=get32(raw+8),bl=get32(raw+12);
        int compact=(encoded&0x80000000u)!=0;
        uint32_t id=encoded&0x7fffffffu;
        if((compact && version!=4) || bl>(compact?BOARD_BODY:1024u))return 0;
        size_t base=compact?100+bl:RECORD_SIZE;
        size_t record_size=base+(version>=2?8:0)+(version>=3?32:0)+(version==4?4:0);
        if(read_record(raw+16,record_size-16)!=(int)record_size-16)return 0;
        if(id!=article_count+1 || parent>=id || tl==0 || tl>BOARD_TITLE || bl==0 || bl>BOARD_BODY ||
           checksum(raw,record_size-4)!=get32(raw+record_size-4) ||
           memchr(raw+16,0,tl) || memchr(raw+16+BOARD_TITLE,0,bl)) return 0;
        Article *a=&(*article_at(article_count++)); a->parent=parent;
        memcpy(a->title,raw+16,tl); a->body_size=bl;a->body_offset=(uint64_t)SEEK(store,0,SEEK_CUR)-record_size+96;
        if(version>=2) {
            a->created=get32(raw+base-4)|((uint64_t)get32(raw+base)<<32);
            if(a->created>253402300799ULL) return 0;
        }
        if(version>=3) memcpy(a->author,raw+base+4,32);
        a->board=version==4?get32(raw+base+36):1;
        a->origin=a->board;
        if(!catalog_known(a->board) || (a->parent && (*article_at(a->parent-1)).board!=a->board)) return 0;
        db_search_set(&article_db,article_count,a->title,(const char *)raw+96,bl);index_article(article_count);
        if(!moves_at(article_count))return 0;
    }
    if(move_replayed!=move_count)return 0;
    for(uint32_t i=0;i<article_count;i++)if(!catalog_known((*article_at(i)).board))return 0;
    return 1;
}
static uint32_t user_named(const char *name) {
    return members_find(name);
}
static int extra_text(const char *text,size_t max,int empty) {
    size_t n=strlen(text); if(n>max || (!empty && !n)) return 0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++) if(*p<32 || *p==127) return 0;
    return 1;
}
static int matches(const BoardRequest *q,uint32_t id) {
    Article *a=&(*article_at(id-1));
    return catalog_read(a->board,q->user) && (q->scope==1 || (q->scope==2?catalog_under(a->board,q->board):a->board==q->board)) && !unavailable(id) &&
           (!strcmp(q->username,"*") || !strcmp(q->username,a->author)) &&
           (!strcmp(q->title,"*") || strstr(a->title,q->title) || strstr(article_body(a),q->title));
}
static void extra_execute(const BoardRequest *q,BoardResult *r) {
    if(q->action==1 || q->action==2) {
        if(!q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
        if(!on_board(q->number,q->board) || !catalog_read(q->board,q->user)) { strcpy(r->text,"ERR no article\r\n"); return; }
        Article *a=&(*article_at(q->number-1));
        int owner=a->author[0] && strcmp(a->author,"guest") && members_name(q->user) && !strcmp(a->author,members_name(q->user));
        if(!owner && !catalog_manage((*article_at(q->number-1)).board,q->user)) { strcpy(r->text,"ERR article owner\r\n"); return; }
        if(q->action==1) {
            int imported=link_imported(q->number);
            if(imported<0) { strcpy(r->text,"ERR link storage; restart inspection required\r\n");return; }
            if(imported) { strcpy(r->text,"ERR imported article; use HIDE for local moderation\r\n");return; }
            if(q->cursor!=1) { snprintf(r->text,sizeof(r->text),"WITHDRAW CONFIRM %u\r\n",q->number); return; }
            if(!extras_set('D',q->number,q->user,"")) { strcpy(r->text,"ERR extras storage\r\n"); return; }
            snprintf(r->text,sizeof(r->text),"WITHDRAW SAVED %u\r\n",q->number); return;
        }
        if(unavailable(q->number)) { strcpy(r->text,"ERR article hidden\r\n"); return; }
        if(!catalog_write(q->board,q->user)) { strcpy(r->text,"ERR posting denied\r\n"); return; }
        if(!extra_text(q->title,80,0)) { strcpy(r->text,"ERR title 1..80 bytes, no controls\r\n"); return; }
        if(!extras_set('T',q->number,q->user,q->title)) { strcpy(r->text,"ERR extras storage\r\n"); return; }
        strcpy(a->title,q->title);db_search_title(&article_db,q->number,q->title); snprintf(r->text,sizeof(r->text),"RETITLED %u\r\n",q->number); return;
    }
    if(q->action==11) {
        uint64_t since;
        if((q->until?(since=q->since,0):!date_filter(q->title,&since)) || q->cursor>CONTENT_ID_MAX+1 || (q->scope<0 || q->scope>2)) { strcpy(r->text,"ERR extended syntax\r\n");return; }
        if(!q->scope && !catalog_read(q->board,q->user)) { strcpy(r->text,"ERR board unavailable\r\n");return; }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"NEWS %s %s\r\n",q->scope==2?"SUBTREE":q->scope?"ALL":"HERE",q->title);
        uint32_t id=q->cursor?q->cursor:1;unsigned shown=0,scanned=0;
        for(;id<=article_count && shown<8 && scanned<256;id++,scanned++) {
            Article *a=&(*article_at(id-1));
            if(!a->created || a->created<since || (q->until && a->created>q->until) || unavailable(id) || (!q->scope && a->board!=q->board) || (q->scope==2 && !catalog_under(a->board,q->board)) || !catalog_read(a->board,q->user))continue;
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %u %u %.80s\r\n",id,a->board,a->parent,a->title);shown++;
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id<=article_count?id:0);return;
    }
    if(q->action==3 || q->action==4) {
        if(!q->scope && !catalog_read(q->board,q->user)) { strcpy(r->text,"ERR board unavailable\r\n"); return; }
        if((q->scope<0 || q->scope>2) || !extra_text(q->title,80,0) ||
           (strcmp(q->username,"*") && !user_named(q->username))) { strcpy(r->text,"ERR find syntax\r\n"); return; }
        uint32_t id=q->cursor?q->cursor:1;
        if(q->action==4 && q->descending && id>article_count)id=article_count;
        if(id>CONTENT_ID_MAX+1) { strcpy(r->text,"ERR find syntax\r\n"); return; }
        if(q->action==4) {
            r->sequence=1;
            for(;id && id<=article_count;id=q->descending?id-1:id+1) { id=db_search_next(&article_db,id,q->title,q->descending);if(!id)break;if(matches(q,id)) {
                display_article(id,r,0); r->board=(*article_at(id-1)).board; r->direct=1; r->sequence_next=q->descending?id-1:id<article_count?id+1:0; return;
            }
            }
            strcpy(r->text,"SCAN END\r\n"); return;
        }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"FIND %s %s %s\r\n",q->scope==2?"SUBTREE":q->scope?"ALL":"HERE",q->username,q->title);
        unsigned shown=0,scanned=0;
        for(;id<=article_count && shown<8 && scanned<256;id++,scanned++) { id=db_search_next(&article_db,id,q->title,0);if(!id) { id=article_count+1;break; }if(matches(q,id)) {
            Article *a=&(*article_at(id-1));
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %u %u %.80s\r\n",id,a->board,a->parent,a->title); shown++;
        }
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id<=article_count?id:0); return;
    }
    if(q->action==5) {
        if(!q->number || q->number>article_count || !catalog_read((*article_at(q->number-1)).board,q->user)) { strcpy(r->text,"ERR no article\r\n"); return; }
        display_article(q->number,r,0); r->board=(*article_at(q->number-1)).board; r->direct=1; return;
    }
    if(q->action==6) {
        uint32_t b=q->number?q->number:q->board;
        if(!catalog_read(b,q->user)) { strcpy(r->text,"ERR board unavailable\r\n"); return; }
        snprintf(r->text,sizeof(r->text),"INFO %u %s\r\nDESCRIPTION %s\r\nEND INFO\r\n",b,catalog_title(b),catalog_description(b));
        if(*policy_info(b))snprintf(r->text,sizeof(r->text),"INFO %u %s\r\nDESCRIPTION %s\r\n%sEND INFO\r\n",b,catalog_title(b),catalog_description(b),policy_info(b));
        return;
    }
    if(!q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
    if(q->action==7 || q->action==17) {
        uint32_t id=q->number?q->number:q->username[0]?user_named(q->username):q->user;
        if(q->action==17) {
            if(!q->number || q->number>article_count || unavailable(q->number) || !catalog_read(board_of(q->number),q->user)) { strcpy(r->text,"ERR no article\r\n");return; }
            id=user_named(article_at(q->number-1)->author);
        }
        if(!members_name(id) || moderation_suspended(id)) { strcpy(r->text,"ERR profile unavailable\r\n"); return; }
        char scratch[16];size_t used=(size_t)snprintf(r->text,sizeof(r->text),"PROFILE %u %s\r\n",id,members_name(id));
        for(unsigned f=0;f<PROFILE_FIELDS;f++) { const char *v=profile_visible(id,q->user,f,scratch);if(f>=7 && !*v)continue;
            if(f==1 && strchr(v,'\n'))used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"BIO \r\n");
            else used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"%s %s\r\n",profile_key(f),v);
        }
        const char *bio=profile_visible(id,q->user,1,scratch);if(strchr(bio,'\n'))used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"PROFILEBODY %zu\r\n%s",strlen(bio),bio);
        snprintf(r->text+used,sizeof(r->text)-used,"END PROFILE\r\n");return;
    }
    if(q->action>=12 && q->action<=16) {
        if(!extra_text(q->body,320,1)) { strcpy(r->text,"ERR profile text\r\n");return; }
        if(profile_set(q->user,(unsigned)q->action-10,q->body)!=1) { strcpy(r->text,"ERR extras storage\r\n");return; }
        strcpy(r->text,"PROFILE SAVED\r\n");return;
    }
    if(q->action==8 || q->action==9) {
        const char *text=q->action==8?q->title:q->body;
        if(!extra_text(text,q->action==8?80:320,1)) { strcpy(r->text,"ERR profile text\r\n"); return; }
        if(profile_set(q->user,q->action==8?0:1,text)!=1) { strcpy(r->text,"ERR extras storage\r\n"); return; }
        strcpy(r->text,"PROFILE SAVED\r\n"); return;
    }
    if(q->action==10) {
        if(members_rtbbs_policy && !moderation_admin(q->user)) {
            int sigop=0;for(uint32_t b=1;b<=CATALOG_MAX;b++)if(catalog_sigop(b,q->user)) { sigop=1;break; }
            if((q->scope==100 && !sigop) || (q->scope>=10 && q->scope<=13)) { strcpy(r->text,"ERR profile search permission\r\n");return; }
        }
        if(!extra_text(q->title,80,0)) { strcpy(r->text,"ERR profile text\r\n"); return; }
        uint32_t id=q->cursor?q->cursor:1; unsigned shown=0;
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"PROFILES %s\r\n",q->title);
        for(;id<=members_count() && shown<(q->notes_unread?1u:8u);id++) {
            const char *name=members_name(id); if(!name || moderation_suspended(id)) continue;
            if(q->scope==100) { if(!members_in_group(id,q->number))continue; }
            else if(q->scope? !profile_match(id,q->user,q->scope-1,q->title):(strcmp(q->title,"*") && !strstr(name,q->title) && !profile_match(id,q->user,-1,q->title)))continue;
            if(q->notes_unread) {
                pos=(size_t)snprintf(r->text,sizeof(r->text),"PROFILE %u %s\r\n",id,name);
                for(unsigned field=0;field<PROFILE_FIELDS;field++) { char scratch[16];const char *value=profile_visible(id,q->user,field,scratch);if(*value)pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%s %s\r\n",profile_key(field),value); }
            }else pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %s %s\r\n",id,name,profile_visible(id,q->user,0,(char[16]){0})); shown++;
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id<=members_count()?id:0); return;
    }
    strcpy(r->text,"ERR command\r\n");
}
static const char *mail_text(const BoardRequest *q,MessageId id,const char *raw) { return q->scope?message_text(q->scope==2?LANGUAGE_EN:LANGUAGE_JA,id):raw; }
static void mail_execute(const BoardRequest *q,BoardResult *r) {
    if((q->action=='S' || q->action=='W') && strlen(q->body)>ln_limit("BODY",BOARD_BODY)) { strcpy(r->text,"ERR body limit\r\n");return; }
    r->mail_action=q->action;
    if(mailbox_broken()) { snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_STORAGE,"MAIL ERROR storage\r\n"));return; }
    uint32_t id=q->number;
    if(q->mail_attachments.count>MAIL_ATTACH_MAX)goto unavailable;
    if(q->action=='A') {
        if(id && !q->cursor && !mail_files_owned(q->user,id))goto unavailable;
        r->mail_attachments=q->mail_attachments;
        if(q->cursor) { unsigned n=0;for(unsigned i=0;i<r->mail_attachments.count;i++)if(r->mail_attachments.ids[i]!=id)r->mail_attachments.ids[n++]=r->mail_attachments.ids[i];r->mail_attachments.count=n; }
        else if(!id)memset(&r->mail_attachments,0,sizeof(r->mail_attachments));
        else if(!mail_has(&r->mail_attachments,id)) { if(r->mail_attachments.count==MAIL_ATTACH_MAX)goto unavailable;r->mail_attachments.ids[r->mail_attachments.count++]=id; }
        r->mail_attachment=id;r->mail_to=q->user;
        snprintf(r->text,sizeof(r->text),q->cursor?mail_text(q,MSG_MAIL_ATTACHMENT_DETACHED,"MAIL ATTACHMENT DETACHED %u\r\n"):mail_text(q,MSG_MAIL_ATTACHMENT_SET,"MAIL ATTACHMENT %u\r\n"),id);return;
    }
    if(q->action=='I') { mail_files_list(q->user,q->cursor,q->scope,r->text,sizeof(r->text),&r->mail_next);return; }
    if(q->action=='X') {
        if(!mail_files_drop(q->user,id))goto unavailable;
        snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_ATTACHMENT_DROPPED,"MAIL ATTACHMENT REMOVED %u\r\n"),id);return;
    }
    if(q->action=='U') {
        if(q->number>2)goto unavailable;
        if(q->number && !members_set_mail_notify(q->user,q->number==2)) { strcpy(r->text,"ERR member storage unavailable\r\n");r->mail_action=0;return; }
        r->mail_notify_user=q->user;members_mail_notify(q->user,&r->mail_notify_deny,&r->mail_notify_revision);
        snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_NOTIFY,"MAIL NOTIFY %s\r\n"),r->mail_notify_deny?"OFF":"ON");return;
    }
    if(q->action=='F' || q->action=='O') {
        const MailMessage *d=mailbox_draft(q->user);if(!d)goto unavailable;
        r->mail_revision=d->id;r->mail_attachments=d->attachments;
        if(q->action=='O') {
            if(!members_name(d->recipient) || moderation_suspended(d->recipient))goto unavailable;
            const MailMessage *old=d->parent?mailbox_get(d->parent,q->user):NULL;
            if(d->parent && (!old || old->recipient!=q->user || old->sender!=d->recipient))goto unavailable;
            r->mail_to=d->recipient;r->mail_parent=d->parent;
            strcpy(r->draft_title,d->title);strcpy(r->draft_body,mail_body(d));
        }
        size_t offset=0;for(unsigned i=0;i<d->attachments.count;i++)offset+=mail_files_describe(q->user,d->attachments.ids[i],q->scope,r->text+offset,sizeof(r->text)-offset);
        snprintf(r->text+offset,sizeof(r->text)-offset,mail_text(q,MSG_MAIL_DRAFT_INFO,"MAIL DRAFT %u TO %s REPLY %u TITLE %s\r\n"),d->id,members_name(d->recipient),d->parent,d->title);return;
    }
    if(q->action=='W' || q->action=='C') {
        if(q->action=='W')for(unsigned i=0;i<q->mail_attachments.count;i++)if(!mail_files_owned(q->user,q->mail_attachments.ids[i]))goto unavailable;
        int ok=q->action=='W'?mailbox_save_draft(q->user,q->number,q->cursor,q->title,q->body,&q->mail_attachments,q->mail_revision,&r->mail_revision):mailbox_delete_draft(q->user,q->mail_revision);
        if(!ok) { if(mailbox_broken())goto storage;goto unavailable; }
        mail_files_cleanup();
        snprintf(r->text,sizeof(r->text),"%s",q->action=='W'?mail_text(q,MSG_MAIL_DRAFT_SAVED,"MAIL DRAFT SAVED\r\n"):mail_text(q,MSG_MAIL_DRAFT_DELETED,"MAIL DRAFT DELETED\r\n"));return;
    }
    if(q->action=='B'  || q->action=='Q') {
        uint32_t target=0;const MailMessage *m=NULL;
        if(q->action=='Q') { m=mailbox_get(id,q->user);if(m && m->recipient==q->user)target=m->sender; }
        else for(uint32_t i=1;i<=members_count();i++)if(members_name(i) && !strcmp(members_name(i),q->username)) { target=i;break; }
        if(!target || moderation_suspended(target))goto unavailable;
        r->mail_to=target;r->mail_parent=m?m->id:0;
        snprintf(r->draft_title,sizeof(r->draft_title),"%s",*q->title?q->title:m?m->title:"");
        snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_EDIT,"MAIL EDIT end with . or /CANCEL\r\n"));return;
    }
    if(q->action=='L') {
        if(q->number>5 || q->cursor>CONTENT_ID_MAX || (q->number==4 && !extra_text(q->title,80,0)))goto unavailable;
        uint64_t since=0;if(q->number==5 && (q->until?(since=q->since,0):!date_filter(q->title,&since)))goto unavailable;
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_LIST,"MAIL LIST UNREAD %u\r\n"),mailbox_unread(q->user));unsigned shown=0;
        if(q->number==4)pos=(size_t)snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_SEARCH,"MAIL SEARCH %s UNREAD %u\r\n"),q->title,mailbox_unread(q->user));
        for(uint32_t i=q->cursor+1;i<=mailbox_count();i++) {
            if(q->number==4) { i=mailbox_search_next(i,q->title);if(!i)break; }
            const MailMessage *m=mailbox_get(i,q->user);if(!m)continue;
            if((q->number==1 && m->sender!=q->user) || (q->number==2 && (m->recipient!=q->user || m->read)) || (q->number==3 && m->recipient!=q->user))continue;
            if(q->number==5 && (m->recipient!=q->user || m->time<since || (q->until && m->time>q->until)))continue;
            if(q->number==4 && !strstr(mail_body(m),q->title) && !strstr(m->title,q->title))continue;
            if(shown==8) { r->mail_next=i-1;break; }
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,mail_text(q,MSG_MAIL_ROW,"MAIL ROW %u %s %s %u %s\r\n"),m->id,(m->sender?members_name(m->sender):"guest"),members_name(m->recipient),(unsigned)m->read,m->title);shown++;
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,mail_text(q,MSG_MAIL_END,"MAIL END NEXT %u\r\n"),r->mail_next);return;
    }
    if(q->action=='f') {
        uint32_t total=mailbox_count(),i=q->cursor;if(q->descending && i>total)i=total;
        for(;i && i<=total;i=q->descending?i-1:i+1)if(mailbox_get(i,q->user)) { BoardRequest next=*q;next.action='R';next.number=i;mail_execute(&next,r);r->sequence=1;r->sequence_next=q->descending?i-1:i<total?i+1:0;return; }
        r->sequence=1;strcpy(r->text,"MAIL SCAN END\r\n");return;
    }
    if(q->action=='N' || q->action=='T') {
        id=0;for(uint32_t i=1;i<=mailbox_count();i++) {
            const MailMessage *m=mailbox_get(i,q->user);if(!m)continue;
            if(q->action=='T')id=i;
            else if(!m->read && m->recipient==q->user) { id=i;break; }
        }
    }
    if(q->action=='R' || q->action=='N' || q->action=='T') {
        const MailMessage *m=mailbox_get(id,q->user);if(!m)goto unavailable;
        if(!mailbox_mark(id,q->user))goto storage;
        r->mail_id=id;
        size_t offset=0;for(unsigned i=0;i<m->attachments.count;i++)offset+=mail_files_describe(q->user,m->attachments.ids[i],q->scope,r->text+offset,sizeof(r->text)-offset);
        snprintf(r->text+offset,sizeof(r->text)-offset,mail_text(q,MSG_MAIL_ITEM,"MAIL ITEM %u FROM %s TO %s READ %u REPLY %u\r\nTITLE %s\r\nBODY\r\n%sMAIL END\r\n"),m->id,(m->sender?members_name(m->sender):"guest"),members_name(m->recipient),(unsigned)m->read,m->parent,m->title,mail_body(m));return;
    }
    if(q->action=='D') {
        if(!mailbox_get(id,q->user))goto unavailable;
        if(!mailbox_remove(id,q->user))goto storage;
        mail_files_cleanup();
        snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_DELETED,"MAIL DELETED %u\r\n"),id);return;
    }
    if(q->action=='S') {
        for(unsigned i=0;i<q->mail_attachments.count;i++)if(!mail_files_owned(q->user,q->mail_attachments.ids[i]))goto unavailable;
        if(!members_name(q->number) || moderation_suspended(q->number))goto unavailable;
        if(!mailbox_send(q->user,q->number,q->cursor,q->title,q->body,&q->mail_attachments,q->mail_revision,&id)) { if(mailbox_broken())goto storage;goto unavailable; }
        mail_files_cleanup();r->mail_delivery=q->number;r->mail_id=id;snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_SENT,"MAIL SENT %u\r\n"),id);return;
    }
unavailable:snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_UNAVAILABLE,"MAIL ERROR unavailable\r\n"));return;
storage:snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_STORAGE,"MAIL ERROR storage\r\n"));
}
#include "link_schema.h"
#include "features64.inc"
static void post_article(const BoardRequest *q,BoardResult *r,const char *remote_author,uint64_t remote_stamp,int validate_only) {
        if(members_enabled() && !members_name(q->user) && !feature_setting("guest_boards",q->board)) { strcpy(r->text,"ERR login required\r\n"); return; }
        if(members_enabled() && version<3) { strcpy(r->text,"ERR member posting needs RTBOARD3\r\n"); return; }
        if(failed) { strcpy(r->text,"ERR storage unavailable\r\n"); return; }
        if(q->number>article_count) { strcpy(r->text,"ERR no parent\r\n"); return; }
        if(strlen(q->body)>ln_limit("BODY",BOARD_BODY)) { strcpy(r->text,"ERR body limit\r\n");return; }
        if(article_count>=CONTENT_ID_MAX || (limit_articles && article_count>=limit_articles) || (!remote_author && limit_user_posts && q->user && user_posts[q->user]>=limit_user_posts)) { strcpy(r->text,"ERR board full\r\n"); return; }
        size_t tl=strlen(q->title),bl=strlen(q->body);
        if(!board_usage(q->board,1,bl)) { strcpy(r->text,"ERR board full\r\n");return; }
        if(!tl || tl>BOARD_TITLE || !bl || bl>BOARD_BODY) { strcpy(r->text,"ERR draft\r\n"); return; }
        if(validate_only) { r->selected=article_count+1;return; }
        unsigned char raw[RECORD_MAX]={0};
        if(bl>1024 && version!=4) { strcpy(r->text,"ERR legacy board capacity; compact to RTBOARD4 first\r\n");return; }
        int compact=bl>1024;
        size_t base=compact?100+bl:RECORD_SIZE;
        size_t record_size=base+(version>=2?8:0)+(version>=3?32:0)+(version==4?4:0);
        time_t clock_now=time(NULL);
        uint64_t stamp=remote_author?remote_stamp:version>=2 && clock_now>0?(uint64_t)clock_now:0;
        put32(raw,(article_count+1)|(compact?0x80000000u:0)); put32(raw+4,q->number);
        put32(raw+8,(uint32_t)tl); put32(raw+12,(uint32_t)bl);
        memcpy(raw+16,q->title,tl); memcpy(raw+16+BOARD_TITLE,q->body,bl);
        if(version>=2) { put32(raw+base-4,(uint32_t)stamp); put32(raw+base,(uint32_t)(stamp>>32)); }
        const char *author=remote_author?remote_author:members_name(q->user); if(!author) author=*q->username?q->username:"guest";
        if(version>=3) memcpy(raw+base+4,author,strlen(author));
        if(version==4) put32(raw+base+36,q->board);
        put32(raw+record_size-4,checksum(raw,record_size-4));
        int64_t record_offset=SEEK(store,0,SEEK_END);
        if(record_offset<0 || !write_all(raw,record_size) || SYNC(store)) {
            failed=1; strcpy(r->text,"ERR storage uncertain; restart inspection required\r\n"); return;
        }
        Article *a=&(*article_at(article_count++)); a->parent=q->number;
        strcpy(a->title,q->title); a->body_size=(uint32_t)bl;a->body_offset=(uint64_t)record_offset+96;
        if(version>=3) strcpy(a->author,author);
        a->board=q->board;a->origin=q->board;
        a->created=stamp;db_search_set(&article_db,article_count,a->title,q->body,bl); if(q->user && !remote_author)user_posts[q->user]++; index_article(article_count); r->selected=article_count;
        snprintf(r->text,sizeof(r->text),"SAVED %u\r\n",article_count);
}
#include "board_link.inc"
static void execute(const BoardRequest *q,BoardResult *r) {
    memset(r,0,sizeof(*r));
    policy_context(q->board_grants);
    if(features_broken || policy_broken() || move_journal.broken || access_journal.broken) { r->session=q->session;r->slot=q->slot;strcpy(r->text,"ERR board policy storage\r\n");return; }
    r->deferred=0;r->binary_enter=0;r->binary_size=0;
    r->session=q->session; r->slot=q->slot;r->zcontrol=q->zcontrol; r->selected=0; r->auth=0; r->board=0; r->editing=0; r->user_epoch=0; r->account_user=0; r->account_epoch=0; r->revoked=0; r->password_changed=0; r->preferences=0; r->language=0; r->pager=0; r->draft_loaded=0; r->draft_revision=0; r->draft_parent=0;
    if(extras_broken() || profile_store_broken()) { strcpy(r->text,"ERR extras storage\r\n"); return; }
    if(members_broken()) { strcpy(r->text,"ERR member storage unavailable\r\n"); return; }
    if(((q->op=='m' || q->op==BOARD_MAIL_TRANSFER || (q->op=='@' && q->action==1)) && !board_feature_enabled("mail")) || (q->op=='&' && !board_feature_enabled("chat")) || (strchr("@Y$^",q->op) && !(q->op=='@' && q->action==1) && !board_feature_enabled("vault"))) { strcpy(r->text,"ERR feature disabled\r\n");return; }
    if(q->op=='a' || q->op=='g') {
        if(moderation_broken()) { r->auth=-1; strcpy(r->text,"ERR moderation storage unavailable\r\n"); return; }
        if(q->op=='g' && members_registration_mode()==2) { r->auth=-1;strcpy(r->text,"ERR registration closed\r\n");return; }
        if(q->op=='g' && !registration_details(0,q->body)) { r->auth=-1;strcpy(r->text,"ERR application values\r\n");return; }
        int id=members_auth(q->username,q->password,q->op=='g');
        if(id && q->op=='g' && !registration_details((uint32_t)id,q->body)) { r->auth=-1;snprintf(r->text,sizeof(r->text),"ERR application storage; account %s reserved\r\n",members_name((uint32_t)id));return; }
        if(id && q->op=='g' && members_pending((uint32_t)id)) {
            r->auth=-1;if(members_auto_ids())snprintf(r->text,sizeof(r->text),"APPLICATION SAVED %s\r\n",members_name((uint32_t)id));else strcpy(r->text,"APPLICATION SAVED\r\n");return;
        }
        if(id && moderation_suspended((uint32_t)id)) id=0;
        if(id) {
            r->user_epoch=moderation_epoch((uint32_t)id);
            r->mail_notice=board_feature_enabled("mail")?mailbox_unread((uint32_t)id):0;
            r->mail_notify_user=(uint32_t)id;members_mail_notify((uint32_t)id,&r->mail_notify_deny,&r->mail_notify_revision);
            const MailMessage *mail_draft=mailbox_draft((uint32_t)id);
            if(mail_draft && board_feature_enabled("mail"))r->mail_draft_notice=mail_draft->id;
            r->chat_admin=moderation_admin((uint32_t)id);r->chat_user=(uint32_t)id;members_chat_preferences((uint32_t)id,&r->chat_preferences);
            snprintf(r->chat_login,sizeof(r->chat_login),"%s",members_name((uint32_t)id));
            snprintf(r->chat_name,sizeof(r->chat_name),"%s",*profile_value((uint32_t)id,0)?profile_value((uint32_t)id,0):members_name((uint32_t)id));
            r->menu_mode=(int)feature_setting("menu_preferences",(uint32_t)id);r->preferences=members_preferences((uint32_t)id,&r->language,&r->pager);
            MemberDraft draft={0};
            if(members_draft((uint32_t)id,&draft)) r->saved_draft_notice=draft.revision;
            OPENSSL_cleanse(&draft,sizeof(draft));
            int readable[CATALOG_MAX+1]={0}, has_unread[CATALOG_MAX+1]={0};
            for(uint32_t b=1;b<=CATALOG_MAX;b++) readable[b]=catalog_read(b,(uint32_t)id);
            for(uint32_t i=1;i<=article_count;i++) {
                uint32_t b=(*article_at(i-1)).board;
                if(readable[b] && !unavailable(i) && !members_seen((uint32_t)id,i)) {
                    r->unread_articles_notice++;
                    if(!has_unread[b]) { has_unread[b]=1; r->unread_boards_notice++; }
                }
            }
        }
        if(id) { const char *birth=profile_value((uint32_t)id,8);time_t now=time(NULL);struct tm today;
#ifdef _WIN32
            struct tm *local=localtime(&now);if(local)today=*local;
#else
            struct tm *local=localtime_r(&now,&today);
#endif
            r->birthday=local && strlen(birth)==10 && atoi(birth+5)==today.tm_mon+1 && atoi(birth+8)==today.tm_mday;
        }
        r->auth=id?id:-1;
        if(id) snprintf(r->text,sizeof(r->text),"LOGGED IN %s\r\n",members_name((uint32_t)id));
        else strcpy(r->text,"ERR authentication\r\n");
        return;
    }
    if(q->user && (moderation_suspended(q->user) || q->user_epoch!=moderation_epoch(q->user))) {
        r->revoked=1; strcpy(r->text,"ERR session revoked\r\n"); return;
    }
    if(q->op=='}') { advanced_execute(q,r);return; }
    if(q->op==BOARD_LINK) { link_execute(q,r);return; }
    if(q->op==BOARD_MEMBER_LIFECYCLE) {
        uint32_t target=q->action==3?q->user:q->number;
        if(q->action!=3 && !moderation_admin(q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
        if(!q->user || !members_active(target)) { strcpy(r->text,"ERR no member\r\n");return; }
        if(moderation_protected(target) || (target==q->user && q->action!=3)) { strcpy(r->text,"ERR protected member\r\n");return; }
        int ok=members_remove(target,q->user,(unsigned)q->action);
        if(ok<=0) { strcpy(r->text,ok<0?"ERR member storage unavailable\r\n":"ERR member state\r\n");return; }
        moderation_credentials_changed(target);r->account_user=target;r->account_epoch=moderation_epoch(target);
        snprintf(r->text,sizeof(r->text),"MEMBER REMOVED %u KIND %d\r\n",target,q->action);return;
    }
    if(q->op=='(') { board_features(q,r);return; }
    if(q->op=='?' && q->action=='M') {
        if(strlen(q->body)>ln_limit("BODY",BOARD_BODY)) { strcpy(r->text,"ERR body limit\r\n");return; }
        if(!board_feature_enabled("mail")) { strcpy(r->text,"ERR feature disabled\r\n");return; }
        uint32_t to=0,id=0;for(uint32_t i=1;i<=members_count();i++)if(moderation_admin(i) && !moderation_suspended(i)) { to=i;break; }
        if(!to) { strcpy(r->text,"ERR SYSOP unavailable\r\n");return; }
        if(!mailbox_send(q->user,to,0,q->title,q->body,0,0,&id)) { strcpy(r->text,"ERR mail storage or limit\r\n");return; }
        r->service_action='M';r->mail_delivery=to;snprintf(r->text,sizeof(r->text),"SYSOP MAIL SENT %u\r\n",id);return;
    }
    if(q->op=='!') { activity_execute(q,r);return; }
    if(q->op=='%') {
        uint32_t target=q->number?q->number:q->user;
        if(!members_name(q->user)) { strcpy(r->text,"ERR login required\r\n");return; }
        if((q->action || target!=q->user) && !moderation_admin(q->user) && !((q->action=='G' || q->action=='D') && group_delegated(q->board,q->user,q->cursor) && !moderation_protected(target))) { strcpy(r->text,"ERR admin required\r\n");return; }
        if(q->action=='P') {
            size_t used=0;unsigned shown=0;uint32_t i=q->cursor?q->cursor:1;
            used=(size_t)snprintf(r->text,sizeof(r->text),"APPLICATIONS\r\n");
            for(;i<=members_count();i++)if(members_pending(i)) {
                if(shown++==8)break;
                used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"APPLICATION %u %s\r\n",i,members_name(i));
            }
            snprintf(r->text+used,sizeof(r->text)-used,"END NEXT %u\r\n",i<=members_count()?i:0);return;
        }
        if(!members_active(target)) { strcpy(r->text,"ERR no member\r\n");return; }
        int result=2;
        if(q->action=='L')result=members_rights(target,q->user,(int)q->cursor,members_groups(target));
        if(q->action=='G' || q->action=='D') {
            result=members_group_change(target,q->user,q->cursor,q->action=='G');
        }
        if(q->action=='A')result=members_approve(target,q->user);
        if(result<=0) { strcpy(r->text,result<0?"ERR member storage unavailable\r\n":"ERR rights syntax\r\n");return; }
        if(result==1) { moderation_credentials_changed(target);r->account_user=target;r->account_epoch=moderation_epoch(target); }
        char groups[401]="";size_t used=0;
        for(unsigned i=0;i<100;i++)if(members_in_group(target,i+1))used+=(size_t)snprintf(groups+used,sizeof(groups)-used,"%s%u",used?",":"",i+1);
        snprintf(r->text,sizeof(r->text),"RIGHTS %u LEVEL %d GROUPS %s STATUS %s\r\n",target,members_level(target),*groups?groups:"-",members_pending(target)?"pending":moderation_suspended(target)?"suspended":"active");return;
    }
    if(q->op=='z') {
        snprintf(r->text,sizeof(r->text),"ROLE %s\r\n",moderation_admin(q->user)?"admin":members_name(q->user)?"member":"guest"); return;
    }
    if(((strchr("hriA",q->op) && !catalog_manage(q->board,q->user)) || (strchr("xylt",q->op) && !moderation_admin(q->user)))) { strcpy(r->text,"ERR admin required\r\n"); return; }
    if(moderation_broken()) { strcpy(r->text,"ERR moderation storage unavailable\r\n"); return; }
    if(q->op==BOARD_MAIL_TRANSFER) { if(!members_name(q->user)) { strcpy(r->text,"ERR login required\r\n");return; }mail_files_transfer(q->user,q->body,r->text,sizeof(r->text));return; }
    if(q->op=='m') { if(!members_name(q->user)) { strcpy(r->text,"ERR login required\r\n");return; }mail_execute(q,r);return; }
    if(q->op=='&') {
        Principal p={0};IdentityProvider identity=identity_legacy_provider();
        if(!identity.resolve(identity.context,q->user,q->user_epoch,&p)) { strcpy(r->text,"ERR login required\r\n");return; }
        if(q->action=='K') {
            if(!moderation_admin(q->user)) { r->chat_action='!';strcpy(r->text,"ERR admin required\r\n");return; }
            uint32_t target=0;for(uint32_t i=1;i<=members_count();i++)if(members_name(i) && !strcmp(q->body,members_name(i))) { target=i;break; }
            if(!target) { r->chat_action='!';strcpy(r->text,"ERR member not found\r\n");return; }
            if(target==q->user || moderation_protected(target)) { r->chat_action='!';strcpy(r->text,"ERR protected member\r\n");return; }
            if((uint64_t)moderation_count()+extras_audit_count()>=UINT32_MAX || !moderation_chat_kick(target,q->user)) {
                r->chat_action='!';strcpy(r->text,"ERR moderation storage uncertain\r\n");return;
            }
            r->chat_action='K';r->chat_kick_user=target;strcpy(r->text,members_name(target));return;
        }
        if(q->action=='P') {
            int ok=members_chat_control(q->user,q->body);
            if(ok<0) { strcpy(r->text,"ERR member storage unavailable\r\n");return; }
            if(!ok) { r->chat_action='E';return; }
        }
        r->chat_user=q->user;members_chat_preferences(q->user,&r->chat_preferences);
        r->chat_action=q->action;r->chat_target=q->chat_target;
        snprintf(r->chat_login,sizeof(r->chat_login),"%s",members_name(q->user));
        const char *name=profile_value(q->user,0);
        snprintf(r->chat_name,sizeof(r->chat_name),"%s",*name?name:r->chat_login);
        snprintf(r->text,sizeof(r->text),"%s",q->body);return;
    }
    if(q->op=='@' || q->op=='Y' || q->op=='$' || q->op=='^') {
        Principal p={0};IdentityProvider identity=identity_legacy_provider();
        if(!identity.resolve(identity.context,q->user,q->user_epoch,&p)) { strcpy(r->text,"ERR login required\r\n");return; }
        memcpy(p.vault_grants,q->vault_grants,sizeof(p.vault_grants));
        if(q->op=='@') { r->binary_enter=1;return; }
        BoardRequest copy=*q;
        if(q->op=='Y' && (q->action==VAULT_CMD_OWNER || q->action==VAULT_CMD_BOARDSIGOP)) {
            Principal target={0};if(q->cursor && !identity.resolve(identity.context,q->cursor,moderation_epoch(q->cursor),&target)) { strcpy(r->text,"ERR no member\r\n");return; }
            if(q->action==VAULT_CMD_OWNER) { if(!q->cursor) { strcpy(r->text,"ERR no member\r\n");return; }strcpy(copy.body,target.subject); }
            else snprintf(copy.body,sizeof(copy.body),"%s|%s",q->title,target.subject);
        }
        if(vault_worker_submit(&copy,&p))r->deferred=1;
        else strcpy(r->text,"ERR vault busy\r\n");
        return;
    }
    if(q->op==')') {
        if(!q->user) { strcpy(r->text,"ERR login required\r\n");return; }
        if(q->action=='G') {
            if(!moderation_admin(q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
            int ok=members_group_move(q->user,q->number,q->cursor);
            if(ok==1) { for(uint32_t u=1;u<=MEMBER_MAX;u++)if(members_name(u)) { moderation_credentials_changed(u);vault_worker_account(u,moderation_epoch(u)); }
                r->all_accounts_changed=1;r->password_changed=1;r->account_epoch=moderation_epoch(q->user);strcpy(r->text,"GROUP MOVED\r\n"); }
            else strcpy(r->text,ok<0?"ERR member storage unavailable\r\n":"ERR rights syntax\r\n");
            return;
        }
        if(q->action=='A') { int ok=profile_admin_set(q->user,q->cursor,q->number,q->body);strcpy(r->text,ok==1?"PROFILE SAVED\r\n":"ERR profile permission or value\r\n");return; }
        if(q->action=='I') { r->service_action='8';r->service_number=q->number;strcpy(r->draft_body,profile_value(q->user,q->number==1?1:PROFILE_BUFFER));strcpy(r->text,"DOCUMENT EDIT\r\n");return; }
        if(q->action=='E') { r->service_action='9';strcpy(r->draft_body,profile_value(q->user,PROFILE_BUFFER));return; }
        if(members_rtbbs_policy && !moderation_admin(q->user) && q->action!='V' && q->action!='B' && q->action!='C' && (q->number==8 || q->number==11)) { strcpy(r->text,"ERR profile administrator required\r\n");return; }
        int ok=q->action=='V'?profile_visibility(q->user,q->number,q->cursor):profile_set(q->user,q->action=='B'?1:q->action=='C'?PROFILE_BUFFER:q->number,q->body);
        if(ok==1 && q->action=='V')r->board_refresh=1;
        if(ok==1 && (q->action=='B' || q->action=='C')) { r->service_action='0';r->service_number=q->scope?0:1; }
        strcpy(r->text,ok==1?"PROFILE SAVED\r\n":ok<0?"ERR profile text\r\n":"ERR extras storage\r\n");return;
    }
    if(q->op=='Z') { extra_execute(q,r); return; }
    if(q->op=='d' || q->op=='w' || q->op=='o' || q->op=='X') {
        if(!q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
        MemberDraft draft={0}; int exists=members_draft(q->user,&draft);
        if(q->op=='d') {
            if(!exists) strcpy(r->text,"DRAFT NONE\r\n");
            else snprintf(r->text,sizeof(r->text),"DRAFT STORED %u\r\n",draft.revision);
            return;
        }
        if(q->op=='X') {
            uint32_t revision;
            int ok=members_store_draft(q->user,&draft,q->cursor,1,&revision);
            strcpy(r->text,ok==1?"DRAFT REMOVED\r\n":ok==0?"ERR draft conflict\r\n":"ERR member storage unavailable\r\n"); return;
        }
        if(q->op=='w') {
            draft.board=q->board; draft.parent=q->number;
            strcpy(draft.title,q->title); strcpy(draft.body,q->body);
        } else if(!exists) { strcpy(r->text,"DRAFT NONE\r\n"); return; }
        if(!catalog_read(draft.board,q->user) || !catalog_write(draft.board,q->user)) { strcpy(r->text,"ERR board unavailable\r\n"); return; }
        if(draft.parent && !on_board(draft.parent,draft.board)) { strcpy(r->text,"ERR no parent\r\n"); return; }
        if(draft.parent && unavailable(draft.parent)) { strcpy(r->text,"ERR article hidden\r\n"); return; }
        if(q->op=='w') {
            size_t tl=strlen(draft.title),bl=strlen(draft.body);
            if(!tl || tl>80 || bl>BOARD_BODY) { strcpy(r->text,"ERR draft\r\n"); return; }
            uint32_t revision; int ok=members_store_draft(q->user,&draft,q->cursor,0,&revision);
            if(ok==1) { r->draft_revision=revision; snprintf(r->text,sizeof(r->text),"DRAFT STORED %u\r\n",revision); }
            else strcpy(r->text,ok==0?"ERR draft conflict\r\n":"ERR member storage unavailable\r\n");
        } else {
            r->draft_loaded=1; r->draft_revision=draft.revision; r->draft_parent=draft.parent; r->board=draft.board; r->editing=1;
            strcpy(r->draft_title,draft.title); strcpy(r->draft_body,draft.body); strcpy(r->text,"DRAFT RESUMED\r\n");
        }
        return;
    }
    if(q->op=='V') {
        if(!q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
        if(q->number>2 || (q->number==1 && (q->cursor>1 || q->title[0]<'0' || q->title[0]>'1' || q->title[1]))) {
            strcpy(r->text,"ERR preferences\r\n"); return;
        }
        if(q->number==1 && !members_save_ui_preferences(q->user,(int)q->cursor,q->title[0]-'0',q->scope,q->audit_article,q->audit_actor,q->ui_aux)) {
            strcpy(r->text,"ERR member storage unavailable\r\n"); return;
        }
        int language=0,pager=0;
        if(!members_preferences(q->user,&language,&pager)) { strcpy(r->text,"PREFS NONE\r\n"); return; }
        if(q->number==2) { r->preferences=1;r->menu_mode=(int)feature_setting("menu_preferences",q->user); r->language=language; r->pager=pager; }
        snprintf(r->text,sizeof(r->text),"PREFS %s %s %s\r\n",q->number==1?"SAVED":q->number==2?"LOADED":"STORED",language?"en":"ja",pager?"ON":"OFF"); return;
    }
    if(q->op=='~') {
        if(!moderation_admin(q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
        if(!members_name(q->number)) { strcpy(r->text,"ERR no member\r\n");return; }
        if(q->number==q->user || moderation_protected(q->number)) { strcpy(r->text,"ERR protected member\r\n");return; }
        if(!q->action) { r->service_action='p';r->service_number=q->number;strcpy(r->text,"NEW PASSWORD\r\n");return; }
        int changed=members_reset_password(q->number,q->user,q->new_password);
        if(changed==1) { moderation_credentials_changed(q->number);r->account_user=q->number;r->account_epoch=moderation_epoch(q->number);snprintf(r->text,sizeof(r->text),"PASSWORD RESET %u\r\n",q->number); }
        else strcpy(r->text,changed<0?"ERR member storage unavailable\r\n":"ERR password change\r\n");
        return;
    }
    if(q->op=='k') {
        if(!q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
        int changed=members_password(q->user,q->password,q->new_password);
        if(changed==1) {
            moderation_credentials_changed(q->user);
            r->password_changed=1; r->account_user=q->user; r->account_epoch=moderation_epoch(q->user);
            strcpy(r->text,"PASSWORD CHANGED\r\n");
        } else strcpy(r->text,changed<0?"ERR member storage unavailable\r\n":"ERR password change\r\n");
        return;
    }
    if(q->op=='A') {
        uint32_t actor=q->audit_actor;
        if(q->title[0]) {
            for(uint32_t user=1;user<=MEMBER_MAX;user++) {
                const char *name=members_name(user);
                if(name && !strcmp(name,q->title)) { actor=user; break; }
            }
            if(!actor) { strcpy(r->text,"ERR audit filter\r\n"); return; }
        }
        if(actor && !members_name(actor)) { strcpy(r->text,"ERR audit filter\r\n"); return; }
        uint32_t latest=moderation_count()+extras_audit_count(),id=q->number?q->number:latest;
        if(q->number>latest) { strcpy(r->text,"ERR audit cursor\r\n"); return; }
        size_t pos=(size_t)(q->audit_article || actor?
            snprintf(r->text,sizeof(r->text),"AUDIT %u ARTICLE %u ACTORID %u\r\n",id,q->audit_article,actor):
            snprintf(r->text,sizeof(r->text),"AUDIT %u\r\n",id));
        unsigned shown=0,scanned=0;
        for(;id && shown<8 && scanned<256;id--,scanned++) {
            ModerationEntry e;
            uint32_t mod_sequence;
            if(!extras_audit_entry(id,&e,&mod_sequence)) { strcpy(r->text,"ERR extras storage\r\n"); return; }
            if(mod_sequence && !moderation_entry(mod_sequence,&e)) { strcpy(r->text,"ERR moderation storage unavailable\r\n"); return; }
            if(!mod_sequence) e.board=board_of(e.target);
            if(!moderation_admin(q->user) && (!(e.kind=='M' || e.kind=='D' || e.kind=='T' || e.kind=='A') || !catalog_sigop(e.board,q->user)))continue;
            if((e.kind=='M' || e.kind=='D' || e.kind=='T' || e.kind=='A') && !catalog_read(e.board,q->user)) continue;
            if(q->audit_article && (!(e.kind=='M' || e.kind=='D' || e.kind=='T' || e.kind=='A') || e.target!=q->audit_article)) continue;
            if(actor && e.actor!=actor) continue;
            char date[32]="unknown"; time_t stamp=(time_t)e.time;
            struct tm *tm=e.time?localtime(&stamp):NULL;
            if(tm) strftime(date,sizeof(date),date_pattern(),tm);
            const char *action=e.kind=='K'?"CHAT_KICK":e.kind=='D'?"WITHDRAW":e.kind=='T'?"RETITLE":e.kind=='A'?"AUTHOR":e.kind=='M'?(e.enabled?"HIDE":"RESTORE"):(e.enabled?"SUSPEND":"RESUME");
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"EVENT %u %s %u %s %s %u %u\r\n",
                id,date,e.actor,members_name(e.actor),action,e.target,e.board);
            shown++;
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id); return;
    }
    if(q->op=='l') {
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"MEMBERS\r\n");
        uint32_t id=q->number?q->number:1; int shown=0;
        for(;id<=members_count();id++) if(members_name(id)) {
            if(shown++==8) break;
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"MEMBER %u %s %s\r\n",id,members_name(id),members_pending(id)?"pending":moderation_suspended(id)?"suspended":"active");
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id<=members_count()?id:0); return;
    }
    if(q->op=='x' || q->op=='y' || q->op=='t') {
        if(!members_name(q->number)) { strcpy(r->text,"ERR no member\r\n"); return; }
        if(q->op=='t') { snprintf(r->text,sizeof(r->text),"MEMBER %u %s %s\r\n",q->number,members_name(q->number),members_pending(q->number)?"pending":moderation_suspended(q->number)?"suspended":"active"); return; }
        if(q->number==q->user || moderation_protected(q->number)) { strcpy(r->text,"ERR protected member\r\n"); return; }
        if((uint64_t)moderation_count()+extras_audit_count()>=UINT32_MAX) { strcpy(r->text,"ERR moderation storage uncertain\r\n"); return; }
        if(!moderation_suspend(q->number,q->user,q->op=='x')) { strcpy(r->text,"ERR moderation storage uncertain\r\n"); return; }
        r->account_user=q->number; r->account_epoch=moderation_epoch(q->number);
        snprintf(r->text,sizeof(r->text),"%s %u\r\n",q->op=='x'?"SUSPENDED":"RESUMED",q->number); return;
    }
    if(q->op=='b' || q->op=='J') {
        if(q->op=='J' && !q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"BOARDS\r\n");
        uint32_t id=q->number?q->number:1; int shown=0;
        if(!catalog_exists(id)) { strcpy(r->text,"ERR board unavailable\r\n"); return; }
        unsigned unread[CATALOG_MAX+1]={0};
        int readable[CATALOG_MAX+1]={0};
        for(uint32_t b=1;b<=CATALOG_MAX;b++) readable[b]=catalog_read(b,q->user);
        if(q->user) for(uint32_t i=1;i<=article_count;i++) {
            uint32_t b=(*article_at(i-1)).board;
            if(readable[b] && !unavailable(i) && !members_seen(q->user,i)) unread[b]++;
        }
        if(q->op=='J') {
            uint32_t next=q->board;
            for(unsigned visited=0;visited<CATALOG_MAX;visited++) {
                next=catalog_next(next); if(!next) next=1;
                if(next==q->board) break;
                if(readable[next] && unread[next]) {
                    r->board=next;
                    snprintf(r->text,sizeof(r->text),"BOARD %u %.80s\r\n",next,catalog_title(next));
                    return;
                }
            }
            strcpy(r->text,"NEXTBOARD NONE\r\n"); return;
        }
        for(;id;id=catalog_next(id)) if(catalog_visible(id,q->user) && (q->action!='C' || catalog_parent(id)==q->number)) {
            if(shown++==8) break;
            unsigned depth=catalog_depth(id), indent=depth>12?24:depth*2;
            char count[16];
            if(q->user && readable[id]) snprintf(count,sizeof(count),"%u",unread[id]); else strcpy(count,"-");
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%*s#%u parent=%u depth=%u write=%s unread=%s %.80s\r\n",
                (int)indent,"",id,catalog_parent(id),depth,catalog_write(id,q->user)?"yes":"no",count,catalog_title(id));
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id); return;
    }
    if(q->op=='j') {
        if(!catalog_visible(q->number,q->user)) strcpy(r->text,"ERR board unavailable\r\n");
        else if(policy_locked(q->number,q->user) && q->action=='t')strcpy(r->text,"ERR try later\r\n");
        else if(policy_locked(q->number,q->user)) { r->service_action='1';r->service_number=q->number;r->board_grant=(uint32_t)policy_locked(q->number,q->user);strcpy(r->text,"BOARD PASSWORD\r\n"); }
        else { r->board=q->number;if(policy_menu(q->number))r->service_action='7'; snprintf(r->text,sizeof(r->text),"BOARD %u %.80s\r\n",q->number,catalog_title(q->number)); }
        return;
    }
    if(!catalog_read(q->board,q->user)) { strcpy(r->text,"ERR board unavailable\r\n"); return; }
    if(q->op=='f') {
        size_t length=strlen(q->title);
        if(!length || length>80) { strcpy(r->text,"ERR search query\r\n"); return; }
        for(size_t k=0;k<length;k++) if((unsigned char)q->title[k]<32 || (unsigned char)q->title[k]==127) {
            strcpy(r->text,"ERR search query\r\n"); return;
        }
        uint32_t id=q->cursor?q->cursor:1;
        if(id>article_count+1) { strcpy(r->text,"ERR search cursor\r\n"); return; }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"SEARCH %s\r\n",q->title);
        unsigned shown=0,scanned=0;
        for(;id<=article_count && shown<8 && scanned<256;id++,scanned++) {
            id=db_search_next(&article_db,id,q->title,0);if(!id) { id=article_count+1;break; }
            Article *a=&(*article_at(id-1));
            if(a->board!=q->board || unavailable(id)) continue;
            if(!strstr(a->title,q->title) && !strstr(article_body(a),q->title)) continue;
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %u %.80s\r\n",id,a->parent,a->title);
            shown++;
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id<=article_count?id:0); return;
    }
    if((q->op=='P'||q->op=='e') && members_enabled() && !q->user && !feature_setting("guest_boards",q->board)) { strcpy(r->text,"ERR login required\r\n");return; }
    if(q->number && strchr("PeRUCNBTsunvhri",q->op) && !on_board(q->number,q->board)) {
        strcpy(r->text,q->op=='P'||q->op=='e'?"ERR no parent\r\n":"ERR no article\r\n"); return;
    }
    if(strchr("hri",q->op)) {
        if(!on_board(q->number,q->board)) { strcpy(r->text,"ERR no article\r\n"); return; }
        if(q->op=='i') { display_article(q->number,r,1); return; }
        if(extras_removed(q->number)) { strcpy(r->text,"ERR article withdrawn\r\n"); return; }
        if((uint64_t)moderation_count()+extras_audit_count()>=UINT32_MAX) { strcpy(r->text,"ERR moderation storage uncertain\r\n"); return; }
        if(!moderation_set(q->number,q->board,q->user,q->op=='h')) { strcpy(r->text,"ERR moderation storage uncertain\r\n"); return; }
        snprintf(r->text,sizeof(r->text),"%s %u\r\n",q->op=='h'?"HIDDEN SAVED":"RESTORED",q->number); return;
    }
    if(q->number && strchr("Pesu",q->op) && unavailable(q->number)) { strcpy(r->text,"ERR article hidden\r\n"); return; }
    if((q->op=='P'||q->op=='e') && !catalog_write(q->board,q->user)) { strcpy(r->text,"ERR posting denied\r\n"); return; }
    if((q->op=='P'||q->op=='e') && version<4 && q->board!=1) { strcpy(r->text,"ERR multiple boards need RTBOARD4\r\n"); return; }
    if(q->op=='e') { if(q->action=='?') { r->service_action='E';r->service_number=q->number?2:1;return; }r->editing=1; strcpy(r->text,"EDIT end with . or /CANCEL\r\n"); return; }
    if(q->op=='s' || q->op=='u' || q->op=='v' || q->op=='n') {
        if(!members_name(q->user)) { strcpy(r->text,"ERR login required\r\n"); return; }
        if(q->op=='s' || q->op=='u') {
            if(!q->number || q->number>article_count) { strcpy(r->text,"ERR no article\r\n"); return; }
            if(!members_mark(q->user,q->number,q->op=='s')) strcpy(r->text,"ERR read-state storage\r\n");
            else snprintf(r->text,sizeof(r->text),"%s %u\r\n",q->op=='s'?"SEEN":"UNSEEN",q->number);
        } else {
            if(q->number>article_count) { strcpy(r->text,"ERR no article\r\n"); return; }
            uint32_t root=q->number?(*article_at(q->number-1)).root:0, total=0, first=0;
            for(uint32_t i=1;i<=article_count;i++) if(on_board(i,q->board) && !unavailable(i) && (!root || (*article_at(i-1)).root==root) && !members_seen(q->user,i)) { total++; if(!first) first=i; }
            if(q->op=='n') {
                if(first) display_article(first,r,0); else strcpy(r->text,"NO UNREAD\r\n");
            } else {
                size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"UNREAD %u\r\n",total);
                uint32_t i=q->cursor?q->cursor:1; int shown=0;
                for(;i<=article_count;i++) if(on_board(i,q->board) && !unavailable(i) && (!root || (*article_at(i-1)).root==root) && !members_seen(q->user,i)) {
                    if(shown++==8) break;
                    pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"#%u root=%u %.80s\r\n",i,(*article_at(i-1)).root,(*article_at(i-1)).title);
                }
                snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",i<=article_count?i:0);
            }
        }
        return;
    }
    if(q->op=='P') {
        post_article(q,r,NULL,0,0);
    } else if(q->op=='R') {
        if(q->number==0 || q->number>article_count) { strcpy(r->text,"ERR no article\r\n"); return; }
        display_article(q->number,r,0);
    } else if(q->op=='U' || q->op=='C' || q->op=='N' || q->op=='B') {
        if(!q->number || q->number>article_count) { strcpy(r->text,"ERR no current article\r\n"); return; }
        Article *a=&(*article_at(q->number-1));
        uint32_t target=q->op=='U'?a->parent:q->op=='C'?a->child:q->op=='N'?a->next:a->prev;
        while(target && unavailable(target)) {
            Article *next=article_at(target-1);
            target=q->op=='U'?next->parent:q->op=='B'?next->prev:next->next;
        }
        if(!target) { strcpy(r->text,"ERR no neighbour\r\n"); return; }
        display_article(target,r,0);
    } else if(q->op=='T') {
        if(!q->number || q->number>article_count) { strcpy(r->text,"ERR no article\r\n"); return; }
        uint32_t root=(*article_at(q->number-1)).root, id=q->cursor?q->cursor:root;
        if(id>article_count || (*article_at(id-1)).root!=root) { strcpy(r->text,"ERR tree cursor\r\n"); return; }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"TREE %u\r\n",root);
        for(int shown=0;id;) {
            if(unavailable(id)) { id=tree_next(id);continue; }
            if(shown++==8)break;
            Article *a=&(*article_at(id-1)); unsigned indent=a->depth>12?24:a->depth*2;
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%*s#%u parent=%u depth=%u %.80s\r\n",
                                 (int)indent,"",id,a->parent,a->depth,a->title);
            id=tree_next(id);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id);
    } else if(q->op=='O') {
        uint32_t id=q->number?q->number:1, count=0;
        if(q->notes_unread && !q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
        unsigned *unread_counts=calloc((size_t)article_count+1,sizeof(*unread_counts));
        if(!unread_counts) { strcpy(r->text,"ERR storage unavailable\r\n");return; }
        if(q->user) for(uint32_t i=1;i<=article_count;i++)
            if(on_board(i,q->board) && !unavailable(i) && !members_seen(q->user,i))
                unread_counts[(*article_at(i-1)).root]++;
        for(uint32_t i=0;i<article_count;i++)
            if((*article_at(i)).board==q->board && !(*article_at(i)).parent && !unavailable(i+1) &&
               (!q->notes_unread || unread_counts[i+1])) count++;
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),q->notes_unread?"NOTES UNREAD %u\r\n":"NOTES %u\r\n",count);
        int shown=0;
        for(;id<=article_count;id++) {
            Article *a=&(*article_at(id-1)); if(a->board!=q->board || a->parent || unavailable(id)) continue;
            if(q->notes_unread && !unread_counts[id]) continue;
            if(shown++==5) break;
            uint32_t replies=0,latest=id;
            for(uint32_t child=tree_next(id);child;child=tree_next(child))
                if(!unavailable(child)) { replies++;if(child>latest)latest=child; }
            char date[32]="unknown";
            uint64_t stamp=(*article_at(latest-1)).created;
            time_t t=(time_t)stamp; struct tm *tm=stamp?localtime(&t):NULL;
            if(tm) strftime(date,sizeof(date),date_pattern(),tm);
            char unread_text[16];
            if(q->user) snprintf(unread_text,sizeof(unread_text),"%u",unread_counts[id]);
            else strcpy(unread_text,"-");
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"#%u replies=%u latest=%u updated=%s unread=%s %.80s\r\n",
                                 id,replies,latest,date,unread_text,a->title);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id<=article_count?id:0);
        free(unread_counts);
    } else {
        uint32_t id=q->number?q->number:1, count=0; int shown=0;
        for(uint32_t i=1;i<=article_count;i++) if(on_board(i,q->board) && !unavailable(i)) count++;
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"LIST %u\r\n",count);
        for(;id<=article_count;id++) {
            if(!on_board(id,q->board) || unavailable(id)) continue;
            if(shown++==5) break;
            Article *a=&(*article_at(id-1));
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %u %s\r\n",id,a->parent,a->title);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",id<=article_count?id:0);
    }
}
#ifdef _WIN32
static unsigned __stdcall work(void *unused)
#else
static void *work(void *unused)
#endif
{
    (void)unused;
    LOCK();
    for(;;) {
        int job=-1;
        for(int i=0;i<QUEUE;i++) if(jobs[i].state==1 && (job<0 || jobs[i].ticket<jobs[job].ticket)) job=i;
        if(ui_head && (job<0 || ui_head->ticket<jobs[job].ticket)) {
            UiSave *save=ui_head;ui_head=save->next;if(!ui_head)ui_tail=NULL;UNLOCK();
            BoardResult *result=calloc(1,sizeof(*result));
            if(result) { execute(&save->request,result);if(strncmp(result->text,"PREFS SAVED",11) && !result->revoked)fprintf(stderr,"disconnect preferences: %s",result->text);free(result); }
            else fprintf(stderr,"disconnect preferences: allocation failed\n");
            free(save);LOCK();continue;
        }
        if(job<0) { if(ending) break; WAIT(); continue; }
        jobs[job].state=2; UNLOCK();
        body_read_failed=0;execute(&jobs[job].request,&jobs[job].result);
        if(body_read_failed) { BoardResult *r=&jobs[job].result;unsigned long session=r->session;int slot=r->slot;memset(r,0,sizeof(*r));r->session=session;r->slot=slot;r->board=jobs[job].request.board;strcpy(r->text,"ERR storage unavailable\r\n"); }
        BoardRequest *rq=&jobs[job].request;BoardResult *rs=&jobs[job].result;
        if(rs->selected && !strncmp(rs->text,"SAVED ",6) && !access_record('P',rs->selected,rq->user))snprintf(rs->text,sizeof(rs->text),"POST SAVED LOG FAILED %u\r\n",rs->selected);
        else if((!strncmp(rs->text,"ARTICLE ",8) || rs->service_action=='6') && !access_record('R',rs->service_action=='6'?rq->number:rs->selected,rq->user)) { rs->service_action=0;rs->selected=0;rs->draft_body[0]=0;strcpy(rs->text,"ERR board access storage\r\n"); }
        if(access_changed) { rs->access_refresh=1;access_changed=0; }
        rs->member_level=members_level(rs->auth>0?(uint32_t)rs->auth:rq->user);
        if(rs->preferences)members_ui_preferences(rs->auth>0?(uint32_t)rs->auth:rq->user,&rs->editor_preference,&rs->columns,&rs->rows,&rs->ui_aux);
        uint32_t who=rs->auth>0?(uint32_t)rs->auth:rq->user;
        rs->sigop=catalog_sigop(rs->board?rs->board:rq->board,who);
        if(rs->account_user2)vault_worker_account(rs->account_user2,rs->account_epoch2);
        if(jobs[job].result.account_user)vault_worker_account(jobs[job].result.account_user,jobs[job].result.account_epoch);
        vault_worker_health(!members_broken() && !moderation_broken());
        OPENSSL_cleanse(jobs[job].request.password,sizeof(jobs[job].request.password));
        OPENSSL_cleanse(jobs[job].request.new_password,sizeof(jobs[job].request.new_password));
        LOCK(); jobs[job].state=jobs[job].result.deferred?0:3;
        if(jobs[job].state==3)board_completion_notify();
    }
    UNLOCK();
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
int board_start(const char *path,const char *members,const char *catalog) { return board_start_config(path,members,catalog,NULL); }
int board_start_config(const char *path,const char *members,const char *catalog,const char *admins) {
    /* A stage60 store must not silently lose protection or moves when a sidecar
     * was omitted from a backup/restore. Check before dj_open can create it. */
    char marker[8];int needs_sidecars=storage_probe(path,marker,8) && !memcmp(marker,"RTBOARD5",8);
    if(needs_sidecars) {
        const char *suffixes[]={".boardpolicy",".boardmoves",".boardaccess"};
        for(unsigned i=0;i<3;i++) { char required[4096];struct stat st;int n=snprintf(required,sizeof(required),"%s%s",path,suffixes[i]);
            if(n<=0 || (size_t)n>=sizeof(required) || storage_stat(required,&st) || !S_ISREG(st.st_mode) || st.st_size<8) { fprintf(stderr,"board policy: missing required stage60 sidecar\n");return 0; }
        }
    }
    if(members && !members_open(members)) { fprintf(stderr,"members: cannot open/lock/validate journal\n"); return 0; }
    if(!moderation_admins(admins)) { fprintf(stderr,"admins: invalid member IDs or configuration\n"); members_close(); return 0; }
    if(!catalog_load(catalog)) { fprintf(stderr,"catalog: invalid hierarchy or ACL\n"); members_close(); return 0; }
    if(!catalog_open_changes(path)) { fprintf(stderr,"catalog: cannot open/lock/validate online settings\n");members_close();return 0; }
    char side[4096];int side_size=snprintf(side,sizeof(side),"%s.boardmoves",path);
    if(!policy_open(path) || side_size<=0 || (size_t)side_size>=sizeof(side) || !dj_open(&move_journal,side,"LNBMOV01",move_load,NULL)) { fprintf(stderr,"board policy: cannot open/lock/validate\n");members_close();return 0; }
    side_size=snprintf(side,sizeof(side),"%s.boardaccess",path);
    if(side_size<=0 || (size_t)side_size>=sizeof(side) || !dj_open(&access_journal,side,"LNBACC01",access_load,NULL)) { fprintf(stderr,"board access: cannot open/lock/validate\n");members_close();return 0; }
    db_open_cached(&article_db,path,"article-index",sizeof(Article));
    if(!open_store(path)) {
        fprintf(stderr,"board: cannot open/lock/validate store; original bytes retained\n");
        if(store>=0) CLOSE(store);
        store=-1; members_close(); return 0;
    }
    for(uint32_t b=1;b<=CATALOG_MAX;b++)if(catalog_exists(b) && catalog_parent(b) && !catalog_exists(catalog_parent(b))) { fprintf(stderr,"catalog: deleted parent\n");return 0; }
    for(size_t i=0;i<access_count;i++)if(!article_was_on(access_at((uint32_t)i)->article,access_at((uint32_t)i)->board)) { fprintf(stderr,"board access: unknown article/location\n");return 0; }
    strcpy(access_path,side);
    if(!moderation_open(path,article_count,board_of,article_was_on)) {
        fprintf(stderr,"moderation: cannot open/lock/validate journal; original bytes retained\n");
        CLOSE(store); store=-1; members_close(); return 0;
    }
    if(!extras_open(path,article_count)) {
        fprintf(stderr,"extras: cannot open/lock/validate journal; original bytes retained\n");
        moderation_close(); CLOSE(store); store=-1; members_close(); return 0;
    }
    for(uint32_t i=1;i<=article_count;i++)if(*extras_title(i)) { strcpy(article_at(i-1)->title,extras_title(i));db_search_title(&article_db,i,extras_title(i)); }
    for(uint32_t i=1;i<=article_count;i++)if(*extras_author(i))strcpy(article_at(i-1)->author,extras_author(i));
    for(uint32_t i=0;i<article_count;i++) { uint32_t user=user_named((*article_at(i)).author);if(user)user_posts[user]++; }
    mail_files_config(members);
    if(!mailbox_open(members)) { fprintf(stderr,"mailbox: cannot open/lock/validate journal\n");extras_close();moderation_close();members_close();CLOSE(store);store=-1;return 0; }
    if(!profile_store_open(members)) { fprintf(stderr,"profile: cannot open/validate journal\n");return 0; }
    if(!features_open(path)) { fprintf(stderr,"settings database: cannot open/recover\n");return 0; }
    if(!link_open()) { fprintf(stderr,"link: configuration or recovery failed\n");return 0; }
    for(uint32_t b=1;b<=CATALOG_MAX;b++)catalog_guest(b,(int)feature_setting("guest_boards",b));
    if(!activity_open(path)) { fprintf(stderr,"activity: cannot open/lock/validate journal\n");mailbox_close();extras_close();moderation_close();members_close();CLOSE(store);store=-1;return 0; }
    if(!access_prune(1)) { fprintf(stderr,"board access: retention failed\n");return 0; }
    for(uint32_t id=1;id<=article_count;id++) if(*extras_title(id)) strcpy((*article_at(id-1)).title,extras_title(id));
    if(!completion_open())return 0;
#ifdef _WIN32
    InitializeCriticalSection(&mutex); InitializeConditionVariable(&condition);
    worker=(HANDLE)_beginthreadex(NULL,32*1024*1024,work,NULL,0,NULL);
    if(!worker) { mail_files_close();mailbox_close(); extras_close(); moderation_close(); members_close(); CLOSE(store); store=-1; DeleteCriticalSection(&mutex); return 0; }
#else
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,32*1024*1024);int created=pthread_create(&worker,&attr,work,NULL);pthread_attr_destroy(&attr);
    if(created) { completion_close();mail_files_close();mailbox_close(); extras_close(); moderation_close(); members_close(); CLOSE(store); store=-1; return 0; }
#endif
    if(!vault_worker_start()) { running=1;board_stop();return 0; }
    running=1; return 1;
}
int board_save_disconnect(const BoardRequest *q) {
    if(!running || !q->user)return 1;
    UiSave *save=calloc(1,sizeof(*save));if(!save)return 0;save->request=*q;
    LOCK();save->ticket=next_ticket++;if(ui_tail)ui_tail->next=save;else ui_head=save;ui_tail=save;WAKE();UNLOCK();return 1;
}
int board_submit(const BoardRequest *q) {
    if(!running) return 0;
    LOCK();
    for(int i=0;i<QUEUE;i++) if(!jobs[i].state) {
        jobs[i].request=*q; jobs[i].state=1; jobs[i].ticket=next_ticket++;
        WAKE(); UNLOCK(); return 1;
    }
    UNLOCK(); return 0;
}
int board_poll(BoardResult *r) {
    if(!running) return 0;
    LOCK();
    for(int i=0;i<QUEUE;i++) if(jobs[i].state==3) {
        *r=jobs[i].result; jobs[i].state=0; UNLOCK(); return 1;
    }
    UNLOCK(); return vault_worker_poll(r);
}
void board_stop(void) {
    if(!running) return;
    LOCK(); ending=1; WAKE(); UNLOCK();
#ifdef _WIN32
    WaitForSingleObject(worker,INFINITE); CloseHandle(worker); DeleteCriticalSection(&mutex);
#else
    pthread_join(worker,NULL);
#endif
    vault_worker_stop();
    free(visibility);visibility=NULL;visibility_capacity=0;
    visibility_articles=visibility_moderation=visibility_extras=0;visibility_moves=0;
    completion_close();
    if(!failed && !move_journal.broken) { uint32_t meta[]={article_count,(uint32_t)version,(uint32_t)format5};int sources[]={store,move_journal.fd,extras_descriptor()};db_cache_store(&article_db,sources,3,meta,sizeof(meta)); }
    policy_close();dj_close(&move_journal);dj_close(&access_journal);db_close(&moves_db);db_close(&accesses_db);move_count=move_replayed=0;access_count=0;
    sqlite3_close(features_db);features_db=NULL;profile_store_close();catalog_close_changes();activity_close();mail_files_close();mailbox_close(); extras_close(); moderation_close(); members_close(); db_close(&article_db);CLOSE(store); running=0;article_count=0;memset(user_posts,0,sizeof(user_posts));
}

static unsigned disabled_features;
static unsigned feature_bit(const char *name) { return !strcmp(name,"chat")?1u:!strcmp(name,"mail")?2u:!strcmp(name,"vault")?4u:0u; }
int board_disable_feature(const char *name) { unsigned bit=feature_bit(name);if(!bit)return 0;disabled_features|=bit;return 1; }
int board_feature_enabled(const char *name) { unsigned bit=feature_bit(name);return bit && !(disabled_features&bit); }

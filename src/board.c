#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "runtime_limits.h"
#include "app_limits.h"
#include "activity.h"
/* Stage 2: single-owner board store, bounded asynchronous request queue. */
#include "board.h"
#include "article_core.h"
#include "catalog_policy_core.h"
#include "catalog_group_core.h"
#include "board_topology_core.h"
#include "board_topology_repository.h"
#include "member_core.h"
#include "mail_core.h"
#include "mail_repository.h"
#include "transfer_line.h"
#include "article_repository.h"
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

int article_repository_find(uint32_t id,ArticleMetadata *out) {
    if(!id || id>article_count)return 0;
    const Article *a=article_at(id-1);
    out->board=a->board;out->parent=a->parent;out->body_size=a->body_size;
    out->root=a->root;out->depth=a->depth;out->created=a->created;
    out->child=a->child;out->next=a->next;out->prev=a->prev;
    memcpy(out->author,a->author,sizeof(out->author));memcpy(out->title,a->title,sizeof(out->title));
    return 1;
}
int article_repository_unavailable(uint32_t id) {
    int hidden=unavailable(id);return body_read_failed?-1:hidden;
}
int article_repository_imported(uint32_t id) { return link_imported(id); }
void article_repository_title_changed(uint32_t id,const char *title) {
    Article *a=article_at(id-1);strcpy(a->title,title);db_search_title(&article_db,id,title);
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
uint32_t article_repository_tree_next(uint32_t id) { return tree_next(id); }
static uint32_t board_of(uint32_t id) { return id && id<=article_count?(*article_at(id-1)).board:0; }
static const char *article_body(const Article *a) {
    static char body[BOARD_BODY+1];
    if(failed || SEEK(store,(int64_t)a->body_offset,SEEK_SET)<0) { failed=body_read_failed=1;return ""; }
    size_t done=0;while(done<a->body_size) { int n=(int)READ(store,body+done,a->body_size-done);if(n<=0) { failed=body_read_failed=1;return ""; }done+=(size_t)n; }
    body[done]=0;return body;
}
int article_repository_body(uint32_t id,char *body,size_t capacity) {
    if(!id || id>article_count || !body)return 0;
    const Article *a=article_at(id-1);size_t length=a->body_size;
    if(capacity<=length)return 0;
    const char *text=article_body(a);
    if(failed || body_read_failed)return 0;
    memcpy(body,text,length+1);return 1;
}
uint32_t article_repository_count(void) { return article_count; }
uint32_t article_repository_search_next(uint32_t start,const char *query) {
    return db_search_next(&article_db,start,query,0);
}
uint32_t article_repository_search_ordered(uint32_t start,const char *query,int descending) {
    return db_search_next(&article_db,start,query,descending);
}
int article_repository_body_contains(uint32_t id,const char *query) {
    if(!id || id>article_count)return -1;
    const char *body=article_body(article_at(id-1));
    if(failed || body_read_failed)return -1;
    return strstr(body,query)!=NULL;
}
static void display_article_page(const BoardRequest *q,BoardResult *r,int search) {
    ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticlePage page;
    ArticleOutcome result=search?article_search(&actor,q->board,q->cursor,q->title,&page):
                                 article_list(&actor,q->board,q->number,&page);
    if(result!=ARTICLE_OK) {
        switch(result) {
        case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
        case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
        case ARTICLE_INVALID_QUERY: strcpy(r->text,"ERR search query\r\n");break;
        case ARTICLE_INVALID_CURSOR: strcpy(r->text,"ERR search cursor\r\n");break;
        case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
        case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
        case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
        case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
        default: strcpy(r->text,"ERR article storage\r\n");break;
        }
        return;
    }
    size_t pos=search?(size_t)snprintf(r->text,sizeof(r->text),"SEARCH %s\r\n",q->title):
                      (size_t)snprintf(r->text,sizeof(r->text),"LIST %u\r\n",page.total);
    for(unsigned i=0;i<page.count;i++) {
        const ArticleSummary *row=&page.rows[i];
        pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %u %s\r\n",row->id,row->parent,row->title);
    }
    snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);
}
static void display_article_tree(const BoardRequest *q,BoardResult *r) {
    ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticleTreePage page;
    ArticleOutcome result=article_tree(&actor,q->board,q->number,q->cursor,&page);
    if(result!=ARTICLE_OK) {
        switch(result) {
        case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
        case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
        case ARTICLE_NOT_FOUND: strcpy(r->text,"ERR no article\r\n");break;
        case ARTICLE_INVALID_CURSOR: strcpy(r->text,"ERR tree cursor\r\n");break;
        case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
        case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
        case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
        case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
        default: strcpy(r->text,"ERR article storage\r\n");break;
        }
        return;
    }
    size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"TREE %u\r\n",page.root);
    for(unsigned i=0;i<page.count;i++) {
        const ArticleTreeRow *row=&page.rows[i];unsigned indent=row->depth>12?24:row->depth*2;
        pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%*s#%u parent=%u depth=%u %.80s\r\n",
                             (int)indent,"",row->summary.id,row->summary.parent,row->depth,row->summary.title);
    }
    snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);
}
static void display_article_notes(const BoardRequest *q,BoardResult *r) {
    ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticleNotesPage page;
    ArticleOutcome result=article_notes(&actor,q->board,q->number,q->notes_unread,&page);
    if(result!=ARTICLE_OK) {
        switch(result) {
        case ARTICLE_LOGIN_REQUIRED: strcpy(r->text,"ERR login required\r\n");break;
        case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
        case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
        case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
        case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
        case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
        case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
        case ARTICLE_READ_STATE_STORAGE: strcpy(r->text,"ERR read-state storage\r\n");break;
        default: strcpy(r->text,"ERR storage unavailable\r\n");break;
        }
        return;
    }
    size_t pos=(size_t)snprintf(r->text,sizeof(r->text),q->notes_unread?"NOTES UNREAD %u\r\n":"NOTES %u\r\n",page.total);
    for(unsigned i=0;i<page.count;i++) {
        const ArticleNoteRow *row=&page.rows[i];char date[32]="unknown",unread_text[16];
        time_t t=(time_t)row->updated;struct tm *tm=row->updated?localtime(&t):NULL;
        if(tm)strftime(date,sizeof(date),date_pattern(),tm);
        if(page.unread_known)snprintf(unread_text,sizeof(unread_text),"%u",row->unread);
        else strcpy(unread_text,"-");
        pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"#%u replies=%u latest=%u updated=%s unread=%s %.80s\r\n",
                             row->summary.id,row->replies,row->latest,date,unread_text,row->summary.title);
    }
    snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);
}
static void display_article(const BoardRequest *q,uint32_t id,BoardResult *r,int inspect) {
    ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticleView view;
    /* Reuse an existing worker-owned buffer; do not add a full-body allocation.
     * Consume the structured result now and clear scratch before queue delivery. */
    ArticleOutcome result=article_read(&actor,id,inspect,&view,r->draft_body,sizeof(r->draft_body));
    if(result!=ARTICLE_OK) {
        r->selected=0;
        switch(result) {
        case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
        case ARTICLE_NOT_FOUND: strcpy(r->text,"ERR no article\r\n");break;
        case ARTICLE_ADMIN_REQUIRED: strcpy(r->text,"ERR admin required\r\n");break;
        case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
        case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
        case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
        case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
        default: strcpy(r->text,"ERR article storage\r\n");break;
        }
    } else {
        r->selected=view.id;
        if(view.state==ARTICLE_WITHDRAWN)
            snprintf(r->text,sizeof(r->text),"WITHDRAWN %u PARENT %u\r\n",view.id,view.parent);
        else if(view.state==ARTICLE_CONCEALED)
            snprintf(r->text,sizeof(r->text),"HIDDEN %u PARENT %u\r\n",view.id,view.parent);
        else snprintf(r->text,sizeof(r->text),"ARTICLE %u PARENT %u AUTHOR %.32s%s\r\nTITLE %.80s\r\nBODY %zu\r\n%sEND\r\n",
                      view.id,view.parent,*view.author?view.author:"guest",view.inspected_hidden?" HIDDEN 1":"",
                      view.title,view.body_size,r->draft_body);
    }
    OPENSSL_cleanse(r->draft_body,sizeof(r->draft_body));
}
static void article_read_state(const BoardRequest *q,BoardResult *r) {
    ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticleUnreadPage page;
    int mark=q->op=='s' || q->op=='u';
    ArticleOutcome result=mark?article_mark_seen(&actor,q->board,q->number,q->op=='s'):
                               article_unread(&actor,q->board,q->number,q->cursor,&page);
    if(result!=ARTICLE_OK) {
        switch(result) {
        case ARTICLE_LOGIN_REQUIRED: strcpy(r->text,"ERR login required\r\n");break;
        case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
        case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
        case ARTICLE_NOT_FOUND: strcpy(r->text,"ERR no article\r\n");break;
        case ARTICLE_HIDDEN: strcpy(r->text,"ERR article hidden\r\n");break;
        case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
        case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
        case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
        case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
        case ARTICLE_READ_STATE_STORAGE: strcpy(r->text,"ERR read-state storage\r\n");break;
        default: strcpy(r->text,"ERR article storage\r\n");break;
        }
        return;
    }
    if(mark)snprintf(r->text,sizeof(r->text),"%s %u\r\n",q->op=='s'?"SEEN":"UNSEEN",q->number);
    else if(q->op=='n') {
        if(page.first)display_article(q,page.first,r,0);else strcpy(r->text,"NO UNREAD\r\n");
    } else {
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"UNREAD %u\r\n",page.total);
        for(unsigned i=0;i<page.count;i++) {
            const ArticleUnreadRow *row=&page.rows[i];
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"#%u root=%u %.80s\r\n",
                                 row->summary.id,row->root,row->summary.title);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);
    }
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
static void post_result(ArticleOutcome result,BoardResult *r);

static void member_result(MemberOutcome result,BoardResult *r);
static void extra_execute(const BoardRequest *q,BoardResult *r) {
    if(q->action==1 || q->action==2) {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleOutcome result=q->action==1?
            article_withdraw(&actor,q->board,q->number,q->cursor==1):
            article_retitle(&actor,q->board,q->number,q->title);
        /* Legacy protocol adapter: Core returns status, never terminal text. */
        switch(result) {
        case ARTICLE_OK:
            snprintf(r->text,sizeof(r->text),q->action==1?"WITHDRAW SAVED %u\r\n":"RETITLED %u\r\n",q->number);break;
        case ARTICLE_CONFIRM: snprintf(r->text,sizeof(r->text),"WITHDRAW CONFIRM %u\r\n",q->number);break;
        case ARTICLE_LOGIN_REQUIRED: strcpy(r->text,"ERR login required\r\n");break;
        case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
        case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
        case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
        case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
        case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
        case ARTICLE_NOT_FOUND: strcpy(r->text,"ERR no article\r\n");break;
        case ARTICLE_OWNER_REQUIRED: strcpy(r->text,"ERR article owner\r\n");break;
        case ARTICLE_IMPORTED: strcpy(r->text,"ERR imported article; use HIDE for local moderation\r\n");break;
        case ARTICLE_LINK_STORAGE: strcpy(r->text,"ERR link storage; restart inspection required\r\n");break;
        case ARTICLE_HIDDEN: strcpy(r->text,"ERR article hidden\r\n");break;
        case ARTICLE_POSTING_DENIED: strcpy(r->text,"ERR posting denied\r\n");break;
        case ARTICLE_INVALID_TITLE: strcpy(r->text,"ERR title 1..80 bytes, no controls\r\n");break;
        case ARTICLE_STORAGE: case ARTICLE_BUFFER_TOO_SMALL: case ARTICLE_INVALID_READ:
        case ARTICLE_LEGACY_BOARDS: case ARTICLE_LEGACY_MEMBERS: case ARTICLE_BODY_LIMIT:
        case ARTICLE_FULL: case ARTICLE_INVALID_DRAFT: case ARTICLE_LEGACY_BODY:
        case ARTICLE_POST_UNAVAILABLE: case ARTICLE_POST_UNCERTAIN: case ARTICLE_DRAFT_CONFLICT:
            strcpy(r->text,"ERR article storage\r\n");break;
        case ARTICLE_ADMIN_REQUIRED: strcpy(r->text,"ERR admin required\r\n");break;
        case ARTICLE_WITHDRAWN_TARGET: strcpy(r->text,"ERR article withdrawn\r\n");break;
        case ARTICLE_MODERATION_WRITE: strcpy(r->text,"ERR moderation storage uncertain\r\n");break;
        case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
        case ARTICLE_INVALID_QUERY: strcpy(r->text,"ERR search query\r\n");break;
        case ARTICLE_INVALID_CURSOR: strcpy(r->text,"ERR search cursor\r\n");break;
        case ARTICLE_READ_STATE_STORAGE: strcpy(r->text,"ERR read-state storage\r\n");break;
        case ARTICLE_MANAGEMENT_INVALID: strcpy(r->text,"ERR command\r\n");break;
        case ARTICLE_CATALOG_STORAGE: strcpy(r->text,"ERR catalog storage\r\n");break;
        case ARTICLE_SETTINGS_STORAGE: strcpy(r->text,"ERR settings storage\r\n");break;
        }
        return;
    }
    if(q->action==11) {
        uint64_t since;
        if((q->until?(since=q->since,0):!date_filter(q->title,&since)) || q->cursor>CONTENT_ID_MAX+1 || (q->scope<0 || q->scope>2)) { strcpy(r->text,"ERR extended syntax\r\n");return; }
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticlePage page;
        ArticleOutcome result=article_news(&actor,q->board,(ArticleScope)q->scope,q->cursor,since,q->until,&page);
        if(result!=ARTICLE_OK) {
            switch(result) {
            case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
            case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
            case ARTICLE_INVALID_QUERY: case ARTICLE_INVALID_CURSOR:
                strcpy(r->text,"ERR extended syntax\r\n");break;
            case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
            case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
            case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
            case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
            default: strcpy(r->text,"ERR article storage\r\n");break;
            }
            return;
        }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"NEWS %s %s\r\n",q->scope==2?"SUBTREE":q->scope?"ALL":"HERE",q->title);
        for(unsigned i=0;i<page.count;i++) {
            const ArticleSummary *row=&page.rows[i];
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %u %u %s\r\n",row->id,row->board,row->parent,row->title);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);return;
    }
    if(q->action==3 || q->action==4) {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleFilter filter={q->board,(ArticleScope)q->scope,q->username,q->title};
        ArticlePage page;ArticleScan scan;
        ArticleOutcome result=q->action==4?article_scan(&actor,&filter,q->cursor,q->descending,&scan):
                                             article_find(&actor,&filter,q->cursor,&page);
        if(result!=ARTICLE_OK) {
            switch(result) {
            case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
            case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
            case ARTICLE_INVALID_QUERY: case ARTICLE_INVALID_CURSOR: strcpy(r->text,"ERR find syntax\r\n");break;
            case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
            case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
            case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
            case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
            default: strcpy(r->text,"ERR article storage\r\n");break;
            }
            return;
        }
        if(q->action==4) {
            r->sequence=1;
            if(scan.article.id) {
                display_article(q,scan.article.id,r,0);r->board=scan.article.board;
                r->direct=1;r->sequence_next=scan.next;return;
            }
            strcpy(r->text,"SCAN END\r\n"); return;
        }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"FIND %s %s %s\r\n",q->scope==2?"SUBTREE":q->scope?"ALL":"HERE",q->username,q->title);
        for(unsigned i=0;i<page.count;i++) {
            const ArticleSummary *row=&page.rows[i];
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %u %u %s\r\n",row->id,row->board,row->parent,row->title);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);return;
    }
    if(q->action==5) {
        if(!q->number || q->number>article_count || !catalog_read((*article_at(q->number-1)).board,q->user)) { strcpy(r->text,"ERR no article\r\n"); return; }
        display_article(q,q->number,r,0); r->board=(*article_at(q->number-1)).board; r->direct=1; return;
    }
    if(q->action==6) {
        uint32_t b=q->number?q->number:q->board;
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleBoardInfo info;char information[BOARD_INFO_MAX+1];
        ArticleOutcome result=article_board_info(&actor,b,&info,information,sizeof(information));
        if(result!=ARTICLE_OK) { post_result(result,r);return; }
        snprintf(r->text,sizeof(r->text),"INFO %u %s\r\nDESCRIPTION %s\r\n%sEND INFO\r\n",info.id,info.title,info.description,information);
        return;
    }
    if(!q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
    if(q->action==7 || q->action==17) {
        uint32_t target=q->number;const char *login=*q->username?q->username:NULL;
        if(q->action==17) {
            if(!q->number || q->number>article_count || unavailable(q->number) || !catalog_read(board_of(q->number),q->user)) { strcpy(r->text,"ERR no article\r\n");return; }
            target=0;login=article_at(q->number-1)->author;
        }
        MemberActor actor={q->user,q->user_epoch};MemberProfile profile;
        MemberOutcome result=member_core_profile_get(&actor,target,login,&profile);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        size_t used=(size_t)snprintf(r->text,sizeof(r->text),"PROFILE %u %s\r\n",profile.user,profile.name);
        for(unsigned field=0;field<PROFILE_FIELDS;field++) {
            const char *value=member_profile_value(&profile,field);if(field>=7 && !*value)continue;
            if(field==1 && strchr(value,'\n'))used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"BIO \r\n");
            else used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"%s %s\r\n",profile_key(field),value);
        }
        if(strchr(profile.bio,'\n'))used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"PROFILEBODY %zu\r\n%s",strlen(profile.bio),profile.bio);
        snprintf(r->text+used,sizeof(r->text)-used,"END PROFILE\r\n");return;
    }
    if(q->action>=12 && q->action<=16) {
        if(!extra_text(q->body,320,1)) { strcpy(r->text,"ERR profile text\r\n");return; }
        MemberActor actor={q->user,q->user_epoch};
        MemberOutcome result=member_core_profile_set(&actor,(unsigned)q->action-10,q->body);
        if(result!=MEMBER_CORE_OK) { member_result(result==MEMBER_CORE_PROFILE_TEXT?MEMBER_CORE_EXTRAS_STORAGE:result,r);return; }
        strcpy(r->text,"PROFILE SAVED\r\n");return;
    }
    if(q->action==8 || q->action==9) {
        const char *text=q->action==8?q->title:q->body;
        if(!extra_text(text,q->action==8?80:320,1)) { strcpy(r->text,"ERR profile text\r\n"); return; }
        MemberActor actor={q->user,q->user_epoch};
        MemberOutcome result=member_core_profile_set(&actor,q->action==8?0:1,text);
        if(result!=MEMBER_CORE_OK) { member_result(result==MEMBER_CORE_PROFILE_TEXT?MEMBER_CORE_EXTRAS_STORAGE:result,r);return; }
        strcpy(r->text,"PROFILE SAVED\r\n"); return;
    }
    if(q->action==10) {
        MemberActor actor={q->user,q->user_epoch};MemberProfilePage page;
        MemberProfileQuery query={q->scope==100?MEMBER_PROFILE_GROUP:q->scope?MEMBER_PROFILE_FIELD:MEMBER_PROFILE_ALL,
                                  q->scope?q->scope-1:0,q->number,q->cursor,q->notes_unread?1u:8u,q->title};
        MemberOutcome result=member_core_profile_search(&actor,&query,&page);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"PROFILES %s\r\n",q->title);
        if(q->notes_unread && page.count) {
            MemberProfile profile;result=member_core_profile_get(&actor,page.rows[0].user,NULL,&profile);
            if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
            pos=(size_t)snprintf(r->text,sizeof(r->text),"PROFILE %u %s\r\n",profile.user,profile.name);
            for(unsigned field=0;field<PROFILE_FIELDS;field++) {
                const char *value=member_profile_value(&profile,field);
                if(*value)pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%s %s\r\n",profile_key(field),value);
            }
        } else for(unsigned i=0;i<page.count;i++) {
            const MemberProfileRow *row=&page.rows[i];
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%u %s %s\r\n",row->user,row->name,row->handle);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);return;
    }
    strcpy(r->text,"ERR command\r\n");
}
static const char *mail_text(const BoardRequest *q,MessageId id,const char *raw) { return q->scope?message_text(q->scope==2?LANGUAGE_EN:LANGUAGE_JA,id):raw; }
int mail_repository_enabled(void) { return board_feature_enabled("mail"); }
int mail_repository_buffer_save(uint32_t user,const char *body) { return profile_set(user,PROFILE_BUFFER,body)==1; }
static size_t mail_attachment_text(const BoardRequest *q,uint32_t id,char *out,size_t size) {
    MailActor actor={q->user,q->user_epoch};MailFileInfo info;
    if(mail_core_attachment_info(&actor,id,&info)!=MAIL_CORE_OK)
        return (size_t)snprintf(out,size,"%s",mail_text(q,MSG_MAIL_ATTACHMENT_MISSING,"MAIL ATTACHMENT unavailable\r\n"));
    return (size_t)snprintf(out,size,mail_text(q,MSG_MAIL_ATTACHMENT_INFO,"MAIL ATTACHMENT %u SIZE %u NAME %s\r\n"),info.id,info.size,info.name);
}
static void mail_result(const BoardRequest *q,MailOutcome result,BoardResult *r) {
    switch(result) {
    case MAIL_CORE_LOGIN_REQUIRED:strcpy(r->text,"ERR login required\r\n");break;
    case MAIL_CORE_REVOKED:r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
    case MAIL_CORE_MEMBER_STORAGE:strcpy(r->text,"ERR member storage unavailable\r\n");break;
    case MAIL_CORE_MODERATION_STORAGE:strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
    case MAIL_CORE_DISABLED:strcpy(r->text,"ERR feature disabled\r\n");break;
    case MAIL_CORE_BODY_LIMIT:strcpy(r->text,"ERR body limit\r\n");break;
    case MAIL_CORE_SYSOP_UNAVAILABLE:strcpy(r->text,"ERR SYSOP unavailable\r\n");break;
    case MAIL_CORE_BUFFER_STORAGE:strcpy(r->text,"ERR storage unavailable\r\n");break;
    case MAIL_CORE_BODY_STORAGE:body_read_failed=1;strcpy(r->text,"ERR storage unavailable\r\n");break;
    case MAIL_CORE_STORAGE:snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_STORAGE,"MAIL ERROR storage\r\n"));break;
    default:snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_UNAVAILABLE,"MAIL ERROR unavailable\r\n"));break;
    }
}
typedef struct { MailActor actor; MailOutcome outcome; } MailTransferContext;
static VaultResult mail_transfer_request(void *context,const Principal *unused,const TransferRequest *q,TransferReply *reply) {
    (void)unused;MailTransferContext *c=context;VaultResult status=VAULT_DENIED;
    c->outcome=mail_core_transfer(&c->actor,q,reply,&status);return status;
}
static void mail_transfer_execute(const BoardRequest *q,BoardResult *r) {
    MailTransferContext context={{q->user,q->user_epoch},MAIL_CORE_OK};
    if(!strncmp(q->body,"FILE ",5)) {
        char *end;unsigned long id=strtoul(q->body+5,&end,10);
        if(end==q->body+5 || *end || !id || id>VAULT_ID_MAX) { strcpy(r->text,"ERR no file\r\n");return; }
        TransferRequest request={.operation=TRANSFER_INFO,.id=(uint32_t)id};TransferReply reply;VaultResult status;
        MailOutcome result=mail_core_transfer(&context.actor,&request,&reply,&status);
        if(result!=MAIL_CORE_OK) { if(result==MAIL_CORE_STORAGE)strcpy(r->text,"ERR no file\r\n");else mail_result(q,result,r);return; }
        if(status!=VAULT_OK) { strcpy(r->text,"ERR no file\r\n");return; }
        const VaultEntry *e=&reply.entry;char hash[65];vault_hex(e->sha256,hash);
        snprintf(r->text,sizeof(r->text),"FILE %u %u %s\r\nNAME %s\r\nDESCRIPTION %s\r\nEND FILE\r\n",e->id,e->size,hash,e->name,e->description);return;
    }
    TransferBackend backend={&context,mail_transfer_request};
    transfer_line_execute(backend,NULL,q->body,r->text,sizeof(r->text));
    if(context.outcome==MAIL_CORE_STORAGE)strcpy(r->text,"ERR vault storage\r\n");
    else if(context.outcome!=MAIL_CORE_OK)mail_result(q,context.outcome,r);
}
static void mail_execute(const BoardRequest *q,BoardResult *r) {
    /* Preserve the completion kind even on quota refusal so a failed send
     * returns to the editor with its unsent text and draft revision intact. */
    r->mail_action=q->action;
    if((q->action=='S' || q->action=='W') && strlen(q->body)>ln_limit("BODY",BOARD_BODY)) { strcpy(r->text,"ERR body limit\r\n");return; }
    MailActor actor={q->user,q->user_epoch};
    if(mailbox_broken()) { snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_STORAGE,"MAIL ERROR storage\r\n"));return; }
    uint32_t id=q->number;
    if(q->mail_attachments.count>MAIL_ATTACH_MAX)goto unavailable;
    if(q->action=='A') {
        MailOutcome result=mail_core_attachment_select(&actor,&q->mail_attachments,id,!!q->cursor,&r->mail_attachments);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        r->mail_attachment=id;r->mail_to=q->user;
        snprintf(r->text,sizeof(r->text),q->cursor?mail_text(q,MSG_MAIL_ATTACHMENT_DETACHED,"MAIL ATTACHMENT DETACHED %u\r\n"):mail_text(q,MSG_MAIL_ATTACHMENT_SET,"MAIL ATTACHMENT %u\r\n"),id);return;
    }
    if(q->action=='I') {
        MailFilePage page;MailOutcome result=mail_core_files(&actor,q->cursor,&page);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        size_t n=(size_t)snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_FILES,"MAIL FILES\r\n"));
        for(unsigned i=0;i<page.count;i++) {
            const MailFileInfo *file=&page.rows[i];
            const char *state=file->active?mail_text(q,MSG_MAIL_FILE_ACTIVE,"ACTIVE"):mail_text(q,MSG_MAIL_FILE_READY,"READY");
            n+=(size_t)snprintf(r->text+n,sizeof(r->text)-n,mail_text(q,MSG_MAIL_FILE_ROW,"MAILFILE %u SIZE %u STATE %s NAME %s\r\n"),file->id,file->size,state,file->name);
        }
        r->mail_next=page.next;snprintf(r->text+n,sizeof(r->text)-n,mail_text(q,MSG_MAIL_FILES_END,"END MAILFILES NEXT %u\r\n"),page.next);return;
    }
    if(q->action=='X') {
        MailOutcome result=mail_core_attachment_drop(&actor,id);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_ATTACHMENT_DROPPED,"MAIL ATTACHMENT REMOVED %u\r\n"),id);return;
    }
    if(q->action=='U') {
        if(q->number>2)goto unavailable;
        MemberActor actor={q->user,q->user_epoch};MemberMailNotify notification;
        MemberNotifyAction action=q->number==0?MEMBER_NOTIFY_QUERY:q->number==1?MEMBER_NOTIFY_ON:MEMBER_NOTIFY_OFF;
        MemberOutcome result=member_core_mail_notify(&actor,action,&notification);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);r->mail_action=0;return; }
        r->mail_notify_user=notification.user;r->mail_notify_deny=notification.deny;r->mail_notify_revision=notification.revision;
        snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_NOTIFY,"MAIL NOTIFY %s\r\n"),r->mail_notify_deny?"OFF":"ON");return;
    }
    if(q->action=='F' || q->action=='O') {
        MailDraft draft;int resume=q->action=='O';
        MailOutcome result=mail_core_draft_get(&actor,resume,&draft,r->draft_body,sizeof(r->draft_body));
        if(result!=MAIL_CORE_OK) {
            if(resume && mailbox_broken())body_read_failed=1;
            mail_result(q,result,r);return;
        }
        r->mail_revision=draft.revision;r->mail_attachments=draft.attachments;
        if(resume) { r->mail_to=draft.to;r->mail_parent=draft.parent;strcpy(r->draft_title,draft.title); }
        size_t offset=0;for(unsigned i=0;i<draft.attachments.count;i++)offset+=mail_attachment_text(q,draft.attachments.ids[i],r->text+offset,sizeof(r->text)-offset);
        snprintf(r->text+offset,sizeof(r->text)-offset,mail_text(q,MSG_MAIL_DRAFT_INFO,"MAIL DRAFT %u TO %s REPLY %u TITLE %s\r\n"),draft.revision,draft.recipient,draft.parent,draft.title);return;
    }
    if(q->action=='W' || q->action=='C') {
        MailWrite input={q->number,q->cursor,q->mail_revision,q->title,q->body,q->mail_attachments};
        MailOutcome result=q->action=='W'?mail_core_draft_save(&actor,&input,&r->mail_revision):mail_core_draft_delete(&actor,q->mail_revision);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        snprintf(r->text,sizeof(r->text),"%s",q->action=='W'?mail_text(q,MSG_MAIL_DRAFT_SAVED,"MAIL DRAFT SAVED\r\n"):mail_text(q,MSG_MAIL_DRAFT_DELETED,"MAIL DRAFT DELETED\r\n"));return;
    }
    if(q->action=='B' || q->action=='Q') {
        MailCompose compose;
        MailOutcome result=mail_core_prepare(&actor,q->username,q->action=='Q'?id:0,q->title,&compose);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        r->mail_to=compose.to;r->mail_parent=compose.parent;strcpy(r->draft_title,compose.title);
        snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_EDIT,"MAIL EDIT end with . or /CANCEL\r\n"));return;
    }
    if(q->action=='L') {
        uint64_t since=0;if(q->number==5 && (q->until?(since=q->since,0):!date_filter(q->title,&since)))goto unavailable;
        MailQuery query={(MailListMode)q->number,q->cursor,q->title,since,q->until};MailPage page;
        MailOutcome result=mail_core_list(&actor,&query,&page);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_LIST,"MAIL LIST UNREAD %u\r\n"),page.unread);
        if(q->number==4)pos=(size_t)snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_SEARCH,"MAIL SEARCH %s UNREAD %u\r\n"),q->title,page.unread);
        for(unsigned i=0;i<page.count;i++) {
            const MailSummary *m=&page.rows[i];
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,mail_text(q,MSG_MAIL_ROW,"MAIL ROW %u %s %s %u %s\r\n"),m->id,m->from,m->to,(unsigned)m->read,m->title);
        }
        r->mail_next=page.next;
        snprintf(r->text+pos,sizeof(r->text)-pos,mail_text(q,MSG_MAIL_END,"MAIL END NEXT %u\r\n"),r->mail_next);return;
    }
    if(q->action=='R' || q->action=='N' || q->action=='T' || q->action=='f') {
        MailReadMode mode=q->action=='R'?MAIL_READ_ID:q->action=='N'?MAIL_READ_UNREAD:q->action=='T'?MAIL_READ_LATEST:MAIL_READ_SEQUENCE;
        MailRead item;
        MailOutcome result=mail_core_read(&actor,mode,q->action=='f'?q->cursor:id,!!q->descending,&item,r->draft_body,sizeof(r->draft_body));
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        r->sequence=item.sequence;r->sequence_next=item.next;
        if(item.sequence && !item.message.id) { strcpy(r->text,"MAIL SCAN END\r\n");return; }
        if(q->action=='f')r->mail_action='R';
        r->mail_id=item.message.id;
        const MailSummary *m=&item.message;
        size_t offset=0;for(unsigned i=0;i<item.attachments.count;i++)offset+=mail_attachment_text(q,item.attachments.ids[i],r->text+offset,sizeof(r->text)-offset);
        snprintf(r->text+offset,sizeof(r->text)-offset,mail_text(q,MSG_MAIL_ITEM,"MAIL ITEM %u FROM %s TO %s READ %u REPLY %u\r\nTITLE %s\r\nBODY\r\n%sMAIL END\r\n"),m->id,m->from,m->to,(unsigned)m->read,m->parent,m->title,r->draft_body);return;
    }
    if(q->action=='D') {
        MailOutcome result=mail_core_delete(&actor,id);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_DELETED,"MAIL DELETED %u\r\n"),id);return;
    }
    if(q->action=='S') {
        MailWrite input={q->number,q->cursor,q->mail_revision,q->title,q->body,q->mail_attachments};
        MailOutcome result=mail_core_send(&actor,&input,&id);
        if(result!=MAIL_CORE_OK) { mail_result(q,result,r);return; }
        r->mail_delivery=q->number;r->mail_id=id;
        snprintf(r->text,sizeof(r->text),mail_text(q,MSG_MAIL_SENT,"MAIL SENT %u\r\n"),id);return;
    }
unavailable:snprintf(r->text,sizeof(r->text),"%s",mail_text(q,MSG_MAIL_UNAVAILABLE,"MAIL ERROR unavailable\r\n"));return;
}
#include "link_schema.h"
#include "features64.inc"
static int append_article_record(uint32_t board,uint32_t parent,uint32_t user,
                                 const char *author,const char *title,const char *body,
                                 int remote,uint64_t remote_stamp,uint32_t *id) {
        *id=0;size_t tl=strlen(title),bl=strlen(body);
        unsigned char raw[RECORD_MAX]={0};
        int compact=bl>1024;
        size_t base=compact?100+bl:RECORD_SIZE;
        size_t record_size=base+(version>=2?8:0)+(version>=3?32:0)+(version==4?4:0);
        time_t clock_now=time(NULL);
        uint64_t stamp=remote?remote_stamp:version>=2 && clock_now>0?(uint64_t)clock_now:0;
        put32(raw,(article_count+1)|(compact?0x80000000u:0)); put32(raw+4,parent);
        put32(raw+8,(uint32_t)tl); put32(raw+12,(uint32_t)bl);
        memcpy(raw+16,title,tl); memcpy(raw+16+BOARD_TITLE,body,bl);
        if(version>=2) { put32(raw+base-4,(uint32_t)stamp); put32(raw+base,(uint32_t)(stamp>>32)); }
        if(version>=3) memcpy(raw+base+4,author,strlen(author));
        if(version==4) put32(raw+base+36,board);
        put32(raw+record_size-4,checksum(raw,record_size-4));
        int64_t record_offset=SEEK(store,0,SEEK_END);
        if(record_offset<0 || !write_all(raw,record_size) || SYNC(store)) {
            failed=1;return 0;
        }
        Article *a=&(*article_at(article_count++)); a->parent=parent;
        strcpy(a->title,title); a->body_size=(uint32_t)bl;a->body_offset=(uint64_t)record_offset+96;
        if(version>=3) strcpy(a->author,author);
        a->board=board;a->origin=board;
        a->created=stamp;db_search_set(&article_db,article_count,a->title,body,bl); if(user && !remote)user_posts[user]++; index_article(article_count); *id=article_count;
        return 1;
}

int article_repository_posting_policy(uint32_t board,uint32_t user,ArticlePostingPolicy *out) {
    out->version=(unsigned)version;out->members_required=members_enabled();
    out->guest_allowed=!user && out->members_required?(int)feature_setting("guest_boards",board):0;
    out->storage_failed=failed;out->total_limit=limit_articles;out->user_limit=limit_user_posts;
    out->user_posts=user?user_posts[user]:0;out->body_limit=ln_limit("BODY",BOARD_BODY);
    return !features_broken;
}
int article_repository_post_capacity(uint32_t board,size_t size) {
    int fits=board_usage(board,1,size);
    return policy_broken() || extras_broken()?-1:fits;
}
int article_repository_append(uint32_t board,uint32_t parent,uint32_t user,
                              const char *author,const char *title,const char *body,uint32_t *id) {
    return append_article_record(board,parent,user,author,title,body,0,0,id);
}

static void post_result(ArticleOutcome result,BoardResult *r) {
    switch(result) {
    case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
    case ARTICLE_LOGIN_REQUIRED: strcpy(r->text,"ERR login required\r\n");break;
    case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
    case ARTICLE_POSTING_DENIED: strcpy(r->text,"ERR posting denied\r\n");break;
    case ARTICLE_NOT_FOUND: strcpy(r->text,"ERR no parent\r\n");break;
    case ARTICLE_HIDDEN: strcpy(r->text,"ERR article hidden\r\n");break;
    case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
    case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
    case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
    case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
    case ARTICLE_LEGACY_BOARDS: strcpy(r->text,"ERR multiple boards need RTBOARD4\r\n");break;
    case ARTICLE_LEGACY_MEMBERS: strcpy(r->text,"ERR member posting needs RTBOARD3\r\n");break;
    case ARTICLE_BODY_LIMIT: strcpy(r->text,"ERR body limit\r\n");break;
    case ARTICLE_FULL: strcpy(r->text,"ERR board full\r\n");break;
    case ARTICLE_INVALID_DRAFT: strcpy(r->text,"ERR draft\r\n");break;
    case ARTICLE_DRAFT_CONFLICT: strcpy(r->text,"ERR draft conflict\r\n");break;
    case ARTICLE_LEGACY_BODY: strcpy(r->text,"ERR legacy board capacity; compact to RTBOARD4 first\r\n");break;
    case ARTICLE_POST_UNAVAILABLE: strcpy(r->text,"ERR storage unavailable\r\n");break;
    case ARTICLE_POST_UNCERTAIN: strcpy(r->text,"ERR storage uncertain; restart inspection required\r\n");break;
    default: strcpy(r->text,"ERR article storage\r\n");break;
    }
}

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
        if(bl>1024 && version!=4) { strcpy(r->text,"ERR legacy board capacity; compact to RTBOARD4 first\r\n");return; }
        if(!append_article_record(q->board,q->number,q->user,remote_author,q->title,q->body,1,remote_stamp,&r->selected)) {
            strcpy(r->text,"ERR storage uncertain; restart inspection required\r\n");return;
        }
        snprintf(r->text,sizeof(r->text),"SAVED %u\r\n",r->selected);
}
#include "board_link.inc"
static const char *member_state_text(MemberState state) {
    return state==MEMBER_STATE_PENDING?"pending":state==MEMBER_STATE_SUSPENDED?"suspended":"active";
}
static void member_result(MemberOutcome result,BoardResult *r) {
    switch(result) {
    case MEMBER_CORE_REVOKED:r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
    case MEMBER_CORE_ADMIN_REQUIRED:strcpy(r->text,"ERR admin required\r\n");break;
    case MEMBER_CORE_NOT_FOUND:strcpy(r->text,"ERR no member\r\n");break;
    case MEMBER_CORE_LOGIN_REQUIRED:strcpy(r->text,"ERR login required\r\n");break;
    case MEMBER_CORE_RIGHTS_INVALID:strcpy(r->text,"ERR rights syntax\r\n");break;
    case MEMBER_CORE_PREFERENCES:strcpy(r->text,"ERR preferences\r\n");break;
    case MEMBER_CORE_PROFILE_TEXT:strcpy(r->text,"ERR profile text\r\n");break;
    case MEMBER_CORE_PROFILE_UNAVAILABLE:strcpy(r->text,"ERR profile unavailable\r\n");break;
    case MEMBER_CORE_PROFILE_SEARCH_DENIED:strcpy(r->text,"ERR profile search permission\r\n");break;
    case MEMBER_CORE_APPLICATION_INVALID:strcpy(r->text,"ERR command\r\n");break;
    case MEMBER_CORE_SETTINGS_STORAGE:strcpy(r->text,"ERR settings storage\r\n");break;
    case MEMBER_CORE_PROFILE_EDIT:strcpy(r->text,"ERR profile permission or value\r\n");break;
    case MEMBER_CORE_PROFILE_ADMINISTRATOR:strcpy(r->text,"ERR profile administrator required\r\n");break;
    case MEMBER_CORE_PASSWORD:strcpy(r->text,"ERR password change\r\n");break;
    case MEMBER_CORE_STATE:strcpy(r->text,"ERR member state\r\n");break;
    case MEMBER_CORE_PROTECTED:strcpy(r->text,"ERR protected member\r\n");break;
    case MEMBER_CORE_EXTRAS_STORAGE:strcpy(r->text,"ERR extras storage\r\n");break;
    case MEMBER_CORE_MODERATION_UNCERTAIN:strcpy(r->text,"ERR moderation storage uncertain\r\n");break;
    case MEMBER_CORE_MODERATION_STORAGE:strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
    default:strcpy(r->text,"ERR member storage unavailable\r\n");break;
    }
}

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
            MailActor mail_actor={(uint32_t)id,r->user_epoch};MailNotice mail_notice;
            if(mail_core_notice(&mail_actor,&mail_notice)==MAIL_CORE_OK) {
                r->mail_notice=mail_notice.unread;r->mail_draft_notice=mail_notice.draft_revision;
            }
            r->mail_notify_user=(uint32_t)id;members_mail_notify((uint32_t)id,&r->mail_notify_deny,&r->mail_notify_revision);
            r->chat_admin=moderation_admin((uint32_t)id);r->chat_user=(uint32_t)id;members_chat_preferences((uint32_t)id,&r->chat_preferences);
            snprintf(r->chat_login,sizeof(r->chat_login),"%s",members_name((uint32_t)id));
            snprintf(r->chat_name,sizeof(r->chat_name),"%s",*profile_value((uint32_t)id,0)?profile_value((uint32_t)id,0):members_name((uint32_t)id));
            MemberActor menu_actor={(uint32_t)id,r->user_epoch};MemberMenuMode menu_mode;
            r->menu_mode=member_core_menu_get(&menu_actor,&menu_mode)==MEMBER_CORE_OK?(int)menu_mode:MEMBER_MENU_STANDARD;
            r->preferences=members_preferences((uint32_t)id,&r->language,&r->pager);
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
        MemberActor actor={q->user,q->user_epoch};MemberChange change;
        MemberOutcome result=member_core_remove(&actor,q->number,(unsigned)q->action,&change);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        r->account_user=change.user;r->account_epoch=change.epoch;
        snprintf(r->text,sizeof(r->text),"MEMBER REMOVED %u KIND %d\r\n",change.user,q->action);return;
    }
    if(q->op=='(') { board_features(q,r);return; }
    if(q->op=='?' && q->action=='M') {
        MailActor actor={q->user,q->user_epoch};MailDelivery delivery;
        MailOutcome result=mail_core_sysop_send(&actor,q->title,q->body,&delivery);
        if(result!=MAIL_CORE_OK) {
            if(result==MAIL_CORE_STORAGE || result==MAIL_CORE_UNAVAILABLE)strcpy(r->text,"ERR mail storage or limit\r\n");
            else mail_result(q,result,r);
            return;
        }
        r->service_action='M';r->mail_delivery=delivery.recipient;
        snprintf(r->text,sizeof(r->text),"SYSOP MAIL SENT %u\r\n",delivery.id);return;
    }
    if(q->op=='!') { activity_execute(q,r);return; }
    if(q->op=='%') {
        uint32_t target=q->number?q->number:q->user;
        if(!q->action) {
            MemberActor actor={q->user,q->user_epoch};MemberRights rights;
            MemberOutcome outcome=member_core_rights(&actor,target,&rights);
            if(outcome!=MEMBER_CORE_OK) { member_result(outcome,r);return; }
            char groups[401]="";size_t used=0;
            for(unsigned i=0;i<100;i++)if(rights.groups[i])used+=(size_t)snprintf(groups+used,sizeof(groups)-used,"%s%u",used?",":"",i+1);
            snprintf(r->text,sizeof(r->text),"RIGHTS %u LEVEL %d GROUPS %s STATUS %s\r\n",rights.user,rights.level,*groups?groups:"-",rights.state==MEMBER_STATE_PENDING?"pending":rights.state==MEMBER_STATE_SUSPENDED?"suspended":"active");return;
        }
        if(q->action=='P') {
            MemberActor actor={q->user,q->user_epoch};MemberPage page;
            MemberOutcome outcome=member_core_applications(&actor,q->cursor,&page);
            if(outcome!=MEMBER_CORE_OK) { member_result(outcome,r);return; }
            size_t used=(size_t)snprintf(r->text,sizeof(r->text),"APPLICATIONS\r\n");
            for(unsigned i=0;i<page.count;i++)
                used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"APPLICATION %u %s\r\n",page.rows[i].id,page.rows[i].name);
            snprintf(r->text+used,sizeof(r->text)-used,"END NEXT %u\r\n",page.next);return;
        }
        if(q->action=='A' || q->action=='L' || q->action=='G' || q->action=='D') {
            MemberActor actor={q->user,q->user_epoch};MemberChange change;
            MemberOutcome outcome;
            if(q->action=='A')outcome=member_core_approve(&actor,target,&change);
            else if(q->action=='L')outcome=member_core_level(&actor,target,q->cursor,&change);
            else outcome=member_core_group(&actor,q->board,target,q->cursor,q->action=='G',&change);
            if(outcome!=MEMBER_CORE_OK) { member_result(outcome,r);return; }
            r->account_user=change.user;r->account_epoch=change.epoch;
        } else {
            if(!members_name(q->user)) { strcpy(r->text,"ERR login required\r\n");return; }
            if(!moderation_admin(q->user)) { strcpy(r->text,"ERR admin required\r\n");return; }
            if(!members_active(target)) { strcpy(r->text,"ERR no member\r\n");return; }
        }
        char groups[401]="";size_t used=0;
        for(unsigned i=0;i<100;i++)if(members_in_group(target,i+1))used+=(size_t)snprintf(groups+used,sizeof(groups)-used,"%s%u",used?",":"",i+1);
        snprintf(r->text,sizeof(r->text),"RIGHTS %u LEVEL %d GROUPS %s STATUS %s\r\n",target,members_level(target),*groups?groups:"-",members_pending(target)?"pending":moderation_suspended(target)?"suspended":"active");return;
    }
    if(q->op=='z') {
        MemberActor actor={q->user,q->user_epoch};MemberRole role;
        MemberOutcome result=member_core_role(&actor,&role);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        snprintf(r->text,sizeof(r->text),"ROLE %s\r\n",role==MEMBER_ROLE_ADMIN?"admin":role==MEMBER_ROLE_MEMBER?"member":"guest");return;
    }
    if(strchr("hri",q->op) && !catalog_manage(q->board,q->user)) { strcpy(r->text,"ERR admin required\r\n"); return; }
    if(moderation_broken()) { strcpy(r->text,"ERR moderation storage unavailable\r\n"); return; }
    if(q->op==BOARD_MAIL_TRANSFER) { if(!members_name(q->user)) { strcpy(r->text,"ERR login required\r\n");return; }mail_transfer_execute(q,r);return; }
    if(q->op=='m') { if(!members_name(q->user)) { strcpy(r->text,"ERR login required\r\n");return; }mail_execute(q,r);return; }
    if(q->op=='&') {
        Principal p={0};IdentityProvider identity=identity_legacy_provider();
        if(!identity.resolve(identity.context,q->user,q->user_epoch,&p)) { strcpy(r->text,"ERR login required\r\n");return; }
        if(q->action=='K') {
            MemberActor actor={q->user,q->user_epoch};MemberChatKick removal;
            MemberOutcome result=member_core_chat_kick(&actor,q->body,&removal);
            if(result!=MEMBER_CORE_OK) {
                r->chat_action='!';
                if(result==MEMBER_CORE_NOT_FOUND)strcpy(r->text,"ERR member not found\r\n");
                else member_result(result,r);
                return;
            }
            r->chat_action='K';r->chat_kick_user=removal.user;strcpy(r->text,removal.name);return;
        }
        ChatPreferenceAction action=CHAT_PREF_QUERY;const char *login=NULL;
        if(q->action=='P') {
            if(!strcmp(q->body,"/MUTES"))action=CHAT_PREF_QUERY;
            else if(!strcmp(q->body,"/TALK ON"))action=CHAT_PREF_RECEIVE_ON;
            else if(!strcmp(q->body,"/TALK OFF"))action=CHAT_PREF_RECEIVE_OFF;
            else if(!strncmp(q->body,"/MUTE ",6)) { action=CHAT_PREF_MUTE;login=q->body+6; }
            else if(!strncmp(q->body,"/UNMUTE ",8)) { action=CHAT_PREF_UNMUTE;login=q->body+8; }
            else { r->chat_action='E';return; }
        }
        MemberActor actor={q->user,q->user_epoch};
        MemberOutcome result=member_core_chat_preferences(&actor,action,login,&r->chat_preferences);
        if(result!=MEMBER_CORE_OK) {
            if(result==MEMBER_CORE_INVALID)r->chat_action='E';else member_result(result,r);
            return;
        }
        r->chat_user=q->user;
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
            MemberActor actor={q->user,q->user_epoch};MemberGroupMove change;
            MemberOutcome outcome=member_core_group_move(&actor,q->number,q->cursor,&change);
            if(outcome!=MEMBER_CORE_OK) { member_result(outcome,r);return; }
            for(uint32_t u=1;u<=change.through;u++)if(members_name(u))vault_worker_account(u,moderation_epoch(u));
            r->all_accounts_changed=1;r->password_changed=1;r->account_epoch=change.actor_epoch;strcpy(r->text,"GROUP MOVED\r\n");
            return;
        }
        if(q->action=='A') {
            MemberActor actor={q->user,q->user_epoch};
            MemberOutcome result=member_core_profile_admin_set(&actor,q->cursor,q->number,q->body);
            if(result==MEMBER_CORE_OK)strcpy(r->text,"PROFILE SAVED\r\n");
            else member_result(result==MEMBER_CORE_EXTRAS_STORAGE?MEMBER_CORE_PROFILE_EDIT:result,r);
            return;
        }
        MemberActor actor={q->user,q->user_epoch};
        if(q->action=='I' || q->action=='E') {
            MemberDocument kind=q->action=='I' && q->number==1?MEMBER_DOCUMENT_BIO:MEMBER_DOCUMENT_BUFFER;
            MemberOutcome result=member_core_document_get(&actor,kind,r->draft_body,sizeof(r->draft_body));
            if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
            r->service_action=q->action=='I'?'8':'9';r->service_number=q->number;
            if(q->action=='I')strcpy(r->text,"DOCUMENT EDIT\r\n");
            return;
        }
        MemberOutcome result=q->action=='V'?member_core_profile_visibility(&actor,q->number,q->cursor):
            (q->action=='B' || q->action=='C')?member_core_document_save(&actor,q->action=='B'?MEMBER_DOCUMENT_BIO:MEMBER_DOCUMENT_BUFFER,q->body):
            member_core_profile_set(&actor,q->number,q->body);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        if(q->action=='V')r->board_refresh=1;
        if(q->action=='B' || q->action=='C') { r->service_action='0';r->service_number=q->scope?0:1; }
        strcpy(r->text,"PROFILE SAVED\r\n");return;
    }
    if(q->op=='Z') { extra_execute(q,r); return; }
    if(q->op=='d' || q->op=='w' || q->op=='o' || q->op=='X') {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleDraftView draft={0};uint32_t revision=0;ArticleOutcome result;
        if(q->op=='d')result=article_draft_status(&actor,&revision);
        else if(q->op=='X')result=article_draft_remove(&actor,q->cursor,&revision);
        else if(q->op=='w')result=article_draft_save(&actor,q->board,q->number,q->title,q->body,q->cursor,&revision);
        else result=article_draft_resume(&actor,&draft,r->draft_body,sizeof(r->draft_body));
        if(result!=ARTICLE_OK) { post_result(result,r);return; }
        if(q->op=='X')strcpy(r->text,"DRAFT REMOVED\r\n");
        else if(q->op=='w') {
            r->draft_revision=revision;snprintf(r->text,sizeof(r->text),"DRAFT STORED %u\r\n",revision);
        } else if(q->op=='d') {
            if(revision)snprintf(r->text,sizeof(r->text),"DRAFT STORED %u\r\n",revision);
            else strcpy(r->text,"DRAFT NONE\r\n");
        } else if(!draft.revision)strcpy(r->text,"DRAFT NONE\r\n");
        else {
            r->draft_loaded=1;r->draft_revision=draft.revision;r->draft_parent=draft.parent;r->board=draft.board;r->editing=1;
            strcpy(r->draft_title,draft.title);strcpy(r->text,"DRAFT RESUMED\r\n");
        }
        return;
    }
    if(q->op=='V') {
        if(!q->user) { strcpy(r->text,"ERR login required\r\n"); return; }
        if(q->number>2 || (q->number==1 && (q->cursor>1 || q->title[0]<'0' || q->title[0]>'1' || q->title[1]))) {
            strcpy(r->text,"ERR preferences\r\n"); return;
        }
        MemberActor actor={q->user,q->user_epoch};MemberPreferences preferences;
        MemberOutcome result;
        if(q->number==1) {
            MemberPreferences input={0,(int)q->cursor,q->title[0]-'0',q->scope,q->audit_article,q->audit_actor,q->ui_aux};
            result=member_core_save_preferences(&actor,&input,&preferences);
        } else result=member_core_preferences(&actor,&preferences);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        if(!preferences.present) { strcpy(r->text,"PREFS NONE\r\n");return; }
        if(q->number==2) {
            MemberMenuMode menu_mode;result=member_core_menu_get(&actor,&menu_mode);
            if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
            r->preferences=1;r->menu_mode=(int)menu_mode;
            r->language=preferences.language;r->pager=preferences.pager;r->editor_preference=preferences.editor;
            r->columns=preferences.columns;r->rows=preferences.rows;r->ui_aux=preferences.aux;
        }
        snprintf(r->text,sizeof(r->text),"PREFS %s %s %s\r\n",q->number==1?"SAVED":q->number==2?"LOADED":"STORED",preferences.language?"en":"ja",preferences.pager?"ON":"OFF");return;
    }
    if(q->op=='~') {
        MemberActor actor={q->user,q->user_epoch};MemberChange change;
        if(!q->action) {
            MemberOutcome result=member_core_reset_check(&actor,q->number);
            if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
            r->service_action='p';r->service_number=q->number;strcpy(r->text,"NEW PASSWORD\r\n");return;
        }
        MemberOutcome result=member_core_reset_password(&actor,q->number,q->new_password,&change);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        r->account_user=change.user;r->account_epoch=change.epoch;
        snprintf(r->text,sizeof(r->text),"PASSWORD RESET %u\r\n",change.user);return;
    }
    if(q->op=='k') {
        MemberActor actor={q->user,q->user_epoch};MemberChange change;
        MemberOutcome result=member_core_password(&actor,q->password,q->new_password,&change);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        r->password_changed=1;r->account_user=change.user;r->account_epoch=change.epoch;
        strcpy(r->text,"PASSWORD CHANGED\r\n");return;
    }
    if(q->op=='A') {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticleAuditPage page;
        ArticleOutcome result=article_audit(&actor,q->board,q->number,q->audit_article,q->audit_actor,q->title,&page);
        if(result!=ARTICLE_OK) {
            if(result==ARTICLE_INVALID_QUERY)strcpy(r->text,"ERR audit filter\r\n");
            else if(result==ARTICLE_INVALID_CURSOR)strcpy(r->text,"ERR audit cursor\r\n");
            else if(result==ARTICLE_ADMIN_REQUIRED)strcpy(r->text,"ERR admin required\r\n");
            else post_result(result,r);
            return;
        }
        size_t pos=(size_t)(page.article || page.actor?
            snprintf(r->text,sizeof(r->text),"AUDIT %u ARTICLE %u ACTORID %u\r\n",page.start,page.article,page.actor):
            snprintf(r->text,sizeof(r->text),"AUDIT %u\r\n",page.start));
        for(unsigned i=0;i<page.count;i++) {
            const ArticleAuditRow *row=&page.rows[i];
            char date[32]="unknown";time_t stamp=(time_t)row->time;
            struct tm *tm=row->time?localtime(&stamp):NULL;
            if(tm)strftime(date,sizeof(date),date_pattern(),tm);
            const char *actions[]={"HIDE","RESTORE","WITHDRAW","RETITLE","AUTHOR","CHAT_KICK","SUSPEND","RESUME"};
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"EVENT %u %s %u %s %s %u %u\r\n",
                row->sequence,date,row->actor,row->actor_name,actions[row->action],row->target,row->board);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);return;
    }
    if(q->op=='l' || q->op=='t') {
        MemberActor actor={q->user,q->user_epoch};MemberOutcome result;
        if(q->op=='t') {
            MemberSummary member;result=member_core_get(&actor,q->number,&member);
            if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
            snprintf(r->text,sizeof(r->text),"MEMBER %u %s %s\r\n",member.id,member.name,member_state_text(member.state));return;
        }
        MemberPage page;result=member_core_list(&actor,q->number,&page);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"MEMBERS\r\n");
        for(unsigned i=0;i<page.count;i++) {
            const MemberSummary *member=&page.rows[i];
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"MEMBER %u %s %s\r\n",member->id,member->name,member_state_text(member->state));
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);return;
    }
    if(q->op=='x' || q->op=='y') {
        MemberActor actor={q->user,q->user_epoch};MemberChange change;
        MemberOutcome result=member_core_suspend(&actor,q->number,q->op=='x',&change);
        if(result!=MEMBER_CORE_OK) { member_result(result,r);return; }
        r->account_user=change.user;r->account_epoch=change.epoch;
        snprintf(r->text,sizeof(r->text),"%s %u\r\n",q->op=='x'?"SUSPENDED":"RESUMED",change.user);return;
    }
    if(q->op=='b' || q->op=='J') {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleBoardsPage page;ArticleBoardRow next;
        ArticleOutcome result=q->op=='J'?article_next_board(&actor,q->board,&next):
                                        article_boards(&actor,q->number,q->action=='C',&page);
        if(result!=ARTICLE_OK) {
            switch(result) {
            case ARTICLE_LOGIN_REQUIRED: strcpy(r->text,"ERR login required\r\n");break;
            case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
            case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
            case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
            case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
            case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
            case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
            case ARTICLE_READ_STATE_STORAGE: strcpy(r->text,"ERR read-state storage\r\n");break;
            default: strcpy(r->text,"ERR article storage\r\n");break;
            }
            return;
        }
        if(q->op=='J') {
            if(!next.id) { strcpy(r->text,"NEXTBOARD NONE\r\n");return; }
            r->board=next.id;
            snprintf(r->text,sizeof(r->text),"BOARD %u %.80s\r\n",next.id,next.title);return;
        }
        size_t pos=(size_t)snprintf(r->text,sizeof(r->text),"BOARDS\r\n");
        for(unsigned i=0;i<page.count;i++) {
            const ArticleBoardRow *row=&page.rows[i];
            unsigned indent=row->depth>12?24:row->depth*2;char count[16];
            if(row->unread_known)snprintf(count,sizeof(count),"%u",row->unread);else strcpy(count,"-");
            pos+=(size_t)snprintf(r->text+pos,sizeof(r->text)-pos,"%*s#%u parent=%u depth=%u write=%s unread=%s %.80s\r\n",
                (int)indent,"",row->id,row->parent,row->depth,row->writable?"yes":"no",count,row->title);
        }
        snprintf(r->text+pos,sizeof(r->text)-pos,"END NEXT %u\r\n",page.next);return;
    }
    if(q->op=='j') {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticleBoardEntry entry;
        ArticleOutcome result=article_enter_board(&actor,q->number,&entry);
        if(result!=ARTICLE_OK) { post_result(result,r);return; }
        if(entry.password_board && q->action=='t')strcpy(r->text,"ERR try later\r\n");
        else if(entry.password_board) { r->service_action='1';r->service_number=entry.id;r->board_grant=entry.password_board;strcpy(r->text,"BOARD PASSWORD\r\n"); }
        else { r->board=entry.id;if(entry.menu)r->service_action='7';snprintf(r->text,sizeof(r->text),"BOARD %u %.80s\r\n",entry.id,entry.title); }
        return;
    }
    if(!catalog_read(q->board,q->user)) { strcpy(r->text,"ERR board unavailable\r\n"); return; }
    if(q->op=='f') {
        display_article_page(q,r,1);return;
    }
    if(q->op=='P' || q->op=='e') {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleOutcome result=q->op=='e'?article_prepare_post(&actor,q->board,q->number):
            article_post(&actor,q->board,q->number,q->username,q->title,q->body,&r->selected);
        if(result!=ARTICLE_OK) { post_result(result,r);return; }
        if(q->op=='P')snprintf(r->text,sizeof(r->text),"SAVED %u\r\n",r->selected);
        else if(q->action=='?') { r->service_action='E';r->service_number=q->number?2:1; }
        else { r->editing=1;strcpy(r->text,"EDIT end with . or /CANCEL\r\n"); }
        return;
    }
    if(q->number && strchr("RUCNBTsunvhri",q->op) && !on_board(q->number,q->board)) {
        strcpy(r->text,"ERR no article\r\n"); return;
    }
    if(strchr("hri",q->op)) {
        if(!on_board(q->number,q->board)) { strcpy(r->text,"ERR no article\r\n"); return; }
        if(q->op=='i') { display_article(q,q->number,r,1); return; }
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};
        ArticleOutcome result=q->op=='h'?article_hide(&actor,q->board,q->number):
                                            article_restore(&actor,q->board,q->number);
        switch(result) {
        case ARTICLE_OK:
            snprintf(r->text,sizeof(r->text),"%s %u\r\n",q->op=='h'?"HIDDEN SAVED":"RESTORED",q->number);break;
        case ARTICLE_LOGIN_REQUIRED: strcpy(r->text,"ERR login required\r\n");break;
        case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
        case ARTICLE_ADMIN_REQUIRED: strcpy(r->text,"ERR admin required\r\n");break;
        case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
        case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
        case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
        case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
        case ARTICLE_NOT_FOUND: strcpy(r->text,"ERR no article\r\n");break;
        case ARTICLE_WITHDRAWN_TARGET: strcpy(r->text,"ERR article withdrawn\r\n");break;
        default: strcpy(r->text,"ERR moderation storage uncertain\r\n");break;
        }
        return;
    }
    if(q->number && strchr("su",q->op) && unavailable(q->number)) { strcpy(r->text,"ERR article hidden\r\n"); return; }
    if(q->op=='s' || q->op=='u' || q->op=='v' || q->op=='n') {
        article_read_state(q,r);return;
    }
    if(q->op=='R') {
        if(q->number==0 || q->number>article_count) { strcpy(r->text,"ERR no article\r\n"); return; }
        display_article(q,q->number,r,0);
    } else if(q->op=='U' || q->op=='C' || q->op=='N' || q->op=='B') {
        if(!q->number || q->number>article_count) { strcpy(r->text,"ERR no current article\r\n"); return; }
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticleSummary target;
        ArticleDirection direction=q->op=='U'?ARTICLE_PARENT:q->op=='C'?ARTICLE_CHILD:
                                   q->op=='N'?ARTICLE_NEXT:ARTICLE_PREVIOUS;
        ArticleOutcome result=article_navigate(&actor,q->board,q->number,direction,&target);
        if(result!=ARTICLE_OK) {
            switch(result) {
            case ARTICLE_REVOKED: r->revoked=1;strcpy(r->text,"ERR session revoked\r\n");break;
            case ARTICLE_BOARD_DENIED: strcpy(r->text,"ERR board unavailable\r\n");break;
            case ARTICLE_NOT_FOUND: strcpy(r->text,"ERR no article\r\n");break;
            case ARTICLE_MEMBER_STORAGE: strcpy(r->text,"ERR member storage unavailable\r\n");break;
            case ARTICLE_MODERATION_STORAGE: strcpy(r->text,"ERR moderation storage unavailable\r\n");break;
            case ARTICLE_POLICY_STORAGE: strcpy(r->text,"ERR board policy storage\r\n");break;
            case ARTICLE_EXTRAS_STORAGE: strcpy(r->text,"ERR extras storage\r\n");break;
            default: strcpy(r->text,"ERR article storage\r\n");break;
            }
            return;
        }
        if(!target.id) { strcpy(r->text,"ERR no neighbour\r\n"); return; }
        display_article(q,target.id,r,0);
    } else if(q->op=='T') {
        display_article_tree(q,r);
    } else if(q->op=='O') {
        display_article_notes(q,r);
    } else {
        display_article_page(q,r,0);
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
        if(rs->preferences && rq->op!='V')members_ui_preferences(rs->auth>0?(uint32_t)rs->auth:rq->user,&rs->editor_preference,&rs->columns,&rs->rows,&rs->ui_aux);
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

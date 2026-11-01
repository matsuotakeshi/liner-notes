#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "userlog.h"
#include "app_limits.h"
#include "durable.h"
#include <stdlib.h>
#include <inttypes.h>
#include <openssl/rand.h>
unsigned userlog_keep=1000;int userlog_public;
static uint32_t checked;static size_t checked_count;
static char journal_path[4096];static Durable ledger_lock={-1,0,0};
typedef struct { uint32_t id,user;uint64_t elapsed,start,checkpoint; } Live;
typedef struct { uint32_t id,user,connection;char kind;uint64_t at,elapsed;char transport[8]; } Event;
typedef struct { uint64_t logins,elapsed,last; } Stats;
static Durable journal={-1,0,0};
static Live live[CONNECTION_MAX];static Stats totals[MEMBER_MAX+1];
static Event *events;static size_t count,capacity;static int enabled;
static int reserve(void) {
    if(count<capacity)return 1;
    size_t next=capacity?capacity*2:256;
    if(next<capacity || next>SIZE_MAX/sizeof(*events))return 0;
    Event *p=realloc(events,next*sizeof(*p));if(!p)return 0;events=p;capacity=next;return 1;
}
static Live *connection(uint32_t id) { for(unsigned i=0;i<CONNECTION_MAX;i++)if(live[i].id==id)return &live[i];return NULL; }
static int apply(void *unused,const DurableRow *r) {
    (void)unused;if(r->actor>MEMBER_MAX)return 0;
    if(r->kind=='S') {
        uint64_t logins,elapsed,last;int n=0;char canonical[80];
        if(count || !r->actor || r->target || totals[r->actor].logins ||
           sscanf(r->text,"%" SCNu64 " %" SCNu64 " %" SCNu64 "%n",&logins,&elapsed,&last,&n)!=3 || r->text[n] || !logins || last>253402300799ULL)return 0;
        snprintf(canonical,sizeof(canonical),"%" PRIu64 " %" PRIu64 " %" PRIu64,logins,elapsed,last);
        if(strcmp(canonical,r->text))return 0;
        totals[r->actor]=(Stats){logins,elapsed,last};return 1;
    }
    if(!reserve())return 0;
    Live *l=NULL;uint64_t elapsed=0;int n=0;
    if(r->kind=='C') {
        if(r->actor || r->target || (strcmp(r->text,"TELNET") && strcmp(r->text,"RAW")))return 0;
        for(unsigned i=0;i<CONNECTION_MAX;i++)if(!live[i].id) { l=&live[i];break; }
        if(!l)return 0;
        l->id=r->sequence;
    } else {
        l=connection(r->target);if(!l)return 0;
        if(r->kind=='L') {
            if(!r->actor || l->user || *r->text)return 0;
            if(totals[r->actor].logins==UINT64_MAX)return 0;
            l->user=r->actor;l->elapsed=0;totals[l->user].logins++;if(r->time>totals[l->user].last)totals[l->user].last=r->time;
        } else {
            if(!strchr("TOEX",r->kind) || !r->kind || r->actor!=l->user ||
               sscanf(r->text,"%" SCNu64 "%n",&elapsed,&n)!=1 || r->text[n] || elapsed<l->elapsed || (!l->user && elapsed))return 0;
            if(l->user) { if(UINT64_MAX-totals[l->user].elapsed<elapsed-l->elapsed)return 0;totals[l->user].elapsed+=elapsed-l->elapsed; }
            l->elapsed=elapsed;
        }
    }
    Event e={r->sequence,r->actor,r->kind=='C'?r->sequence:r->target,r->kind,r->time,elapsed,{0}};if(r->kind=='C')snprintf(e.transport,sizeof(e.transport),"%.7s",r->text);events[count++]=e;
    if(r->kind=='E' || r->kind=='X')memset(l,0,sizeof(*l));
    else if(r->kind=='O') { l->user=0;l->elapsed=l->start=l->checkpoint=0; }
    return 1;
}
static int append(char kind,Live *l,uint64_t elapsed,const char *text,uint32_t user) {
    if(!reserve())return 0;
    DurableRow row={0};row.kind=kind;row.target=l?l->id:0;row.actor=user;
    if(text)snprintf(row.text,sizeof(row.text),"%s",text);else snprintf(row.text,sizeof(row.text),"%" PRIu64,elapsed);
    if(!dj_append(&journal,&row) || !apply(NULL,&row)) { journal.broken=1;return 0; }return 1;
}
int userlog_open(const char *base) {
    char path[4096];int n=snprintf(path,sizeof(path),"%s.userlog",base);
    return n>0 && (size_t)n<sizeof(path) && userlog_open_file(path);
}
static int empty_lock(void *ctx,const DurableRow *row) { (void)ctx;(void)row;return 0; }
int userlog_open_file(const char *path) {
    userlog_close();
    if(strlen(path)>=sizeof(journal_path))return 0;
    strcpy(journal_path,path);char lockpath[4120];snprintf(lockpath,sizeof(lockpath),"%s.lock",path);if(!dj_open(&ledger_lock,lockpath,"LNULOCK1",empty_lock,NULL))return 0;
    if(!dj_open(&journal,path,"LNUSER01",apply,NULL))return 0;
    /* A crash has no reliable end time. Keep only checkpointed duration. */
    for(unsigned i=0;i<CONNECTION_MAX;i++)if(live[i].id && !append('X',&live[i],live[i].elapsed,NULL,live[i].user))return 0;
    enabled=1;return userlog_maintain();
}
void userlog_close(void) { dj_close(&journal);dj_close(&ledger_lock);free(events);events=NULL;count=capacity=0;memset(live,0,sizeof(live));memset(totals,0,sizeof(totals));enabled=0;checked=0;checked_count=0; }
int userlog_connect(int slot,const char *transport,uint64_t now) {
    if(!enabled)return 1;
    if(slot<0 || slot>=CONNECTION_MAX || live[slot].id)return 0;
    if(!append('C',NULL,0,transport,0))return 0;
    /* Replay uses the first free slot; runtime slots can have different holes. */
    Live *l=connection(journal.sequence);if(l!=&live[slot]) { live[slot]=*l;memset(l,0,sizeof(*l)); }
    live[slot].start=live[slot].checkpoint=now;return 1;
}
int userlog_update(int slot,uint32_t user,uint64_t now) {
    if(!enabled)return 1;
    if(slot<0 || slot>=CONNECTION_MAX || user>MEMBER_MAX)return 0;
    Live *l=&live[slot];if(!l->id)return 1;
    if(l->user!=user) {
        if(l->user && !append('O',l,now>=l->start?now-l->start:l->elapsed,NULL,l->user))return 0;
        if(user) { if(!append('L',l,0,"",user))return 0;l->start=l->checkpoint=now; }
    } else if(user && now>=l->checkpoint && now-l->checkpoint>=60000) {
        if(!append('T',l,now-l->start,NULL,user))return 0;
        l->checkpoint=now;
    }
    return !journal.broken;
}
int userlog_disconnect(int slot,uint64_t now) {
    if(!enabled)return 1;
    if(slot<0 || slot>=CONNECTION_MAX)return 0;
    Live *l=&live[slot];if(!l->id)return 1;
    int ok=append('E',l,l->user && now>=l->start?now-l->start:0,NULL,l->user);return ok && userlog_maintain();
}
void userlog_report(uint32_t user,uint32_t cursor,int all,int stats,uint64_t now,char *out,size_t size) {
    if(!enabled || journal.broken) { snprintf(out,size,!enabled?"ERR userlog disabled\r\n":"ERR userlog storage\r\n");return; }
    if(user>MEMBER_MAX || cursor>count+1) { snprintf(out,size,"ERR command\r\n");return; }
    if(stats) {
        uint64_t logins=0,elapsed=0,last=0;
        for(uint32_t i=1;i<=MEMBER_MAX;i++)if(all || i==user) { logins+=totals[i].logins;elapsed+=totals[i].elapsed;if(totals[i].last>last)last=totals[i].last; }
        for(unsigned i=0;i<CONNECTION_MAX;i++)if(live[i].user && (all || live[i].user==user) && now>=live[i].start && now-live[i].start>=live[i].elapsed)elapsed+=now-live[i].start-live[i].elapsed;
        snprintf(out,size,"USERSTATS %u LOGINS %" PRIu64 " SECONDS %" PRIu64 " LAST %" PRIu64 "\r\nEND USERSTATS\r\n",all?0:user,logins,elapsed/1000,last);return;
    }
    size_t index=cursor?cursor-1:count,used=(size_t)snprintf(out,size,"USERLOG\r\n");unsigned shown=0,scanned=0;
    while(index && shown<8 && scanned++<256) { Event *e=&events[--index];
        if(e->kind=='T' || (!all && e->user!=user))continue;
        used+=(size_t)snprintf(out+used,size-used,"USERROW %u %u %u %c %" PRIu64 " %" PRIu64 "\r\n",e->id,e->connection,e->user,e->kind,e->at,e->elapsed/1000);shown++;
    }
    snprintf(out+used,size-used,"END NEXT %zu\r\n",index?index+1:0);
}
/* Retain complete connection graphs plus lifetime totals. No live session is
 * discarded. The separate ledger lock survives atomic snapshot replacement. */
static int snapshot_row(Durable *next,char kind,uint32_t actor,uint32_t target,uint64_t at,const char *text) {
    size_t n=strlen(text);unsigned char row[DJ_ROW]={0};if(n>DJ_TEXT || next->sequence==UINT32_MAX)return 0;
    row[0]=(unsigned char)kind;dj_put(row+4,++next->sequence);dj_put(row+8,actor);dj_put(row+12,target);dj_put(row+16,(uint32_t)at);dj_put(row+20,(uint32_t)(at>>32));dj_put(row+24,(uint32_t)n);memcpy(row+28,text,n);dj_put(row+DJ_ROW-4,dj_sum(row));return dj_io(next->fd,row,DJ_ROW,1)==DJ_ROW;
}
int userlog_maintain(void) {
    if(!enabled || !userlog_keep)return 1;
    if(journal.broken)return 0;
    if(checked==journal.sequence && checked_count==count)return 1;
    checked=journal.sequence;checked_count=count;size_t closed=0;
    for(size_t i=0;i<count;i++)if(events[i].kind=='E' || events[i].kind=='X')closed++;
    if(closed<=userlog_keep)return 1;
    size_t slots=(size_t)journal.sequence+1;
    unsigned char *keep=calloc(slots,1);uint32_t *mapping=calloc(slots,sizeof(*mapping));uint64_t *previous=calloc(slots,sizeof(*previous));Stats *base=malloc(sizeof(totals));
    if(!keep || !mapping || !previous || !base) { free(keep);free(mapping);free(previous);free(base);return 0; }
    memcpy(base,totals,sizeof(totals));unsigned retained=0;
    for(size_t i=count;i>0;i--) { Event *e=&events[i-1];if((e->kind=='E' || e->kind=='X') && retained++<userlog_keep)keep[e->connection]=1; }
    for(unsigned i=0;i<CONNECTION_MAX;i++)if(live[i].id)keep[live[i].id]=1;
    for(size_t i=0;i<count;i++) { Event *e=&events[i];if(!keep[e->connection] || !e->user)continue;
        if(e->kind=='L') { base[e->user].logins--;previous[e->connection]=0; }
        else if(strchr("TOEX",e->kind)) { base[e->user].elapsed-=e->elapsed-previous[e->connection];previous[e->connection]=e->kind=='O'?0:e->elapsed; }
    }
    unsigned char random[8]={0};char temporary[4160],suffix[17];int ok=RAND_bytes(random,sizeof(random))==1;
    for(unsigned i=0;i<8;i++)snprintf(suffix+i*2,3,"%02x",random[i]);
    snprintf(temporary,sizeof(temporary),"%s.retaining-%s",journal_path,suffix);
    int fd=-1;
    if(ok) {
#ifdef _WIN32
        fd=_open(temporary,_O_RDWR|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
        fd=open(temporary,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
#endif
    }
    Durable next={fd,0,0};if(fd<0)ok=0;
    unsigned char magic[]="LNUSER01";if(ok && dj_io(fd,magic,8,1)!=8)ok=0;
    for(unsigned i=1;i<=MEMBER_MAX && ok;i++)if(base[i].logins) { char text[100];snprintf(text,sizeof(text),"%" PRIu64 " %" PRIu64 " %" PRIu64,base[i].logins,base[i].elapsed,base[i].last);ok=snapshot_row(&next,'S',i,0,0,text); }
    for(size_t i=0;i<count && ok;i++) { Event *e=&events[i];if(!keep[e->connection])continue;char text[32];
        if(e->kind=='C') { mapping[e->connection]=next.sequence+1;ok=snapshot_row(&next,'C',0,0,e->at,e->transport); }
        else { if(e->kind=='L')text[0]=0;else snprintf(text,sizeof(text),"%" PRIu64,e->elapsed);ok=snapshot_row(&next,e->kind,e->user,mapping[e->connection],e->at,text); }
    }
    if(ok && DJ_SYNC(fd))ok=0;
    Live saved[CONNECTION_MAX];memcpy(saved,live,sizeof(saved));for(unsigned i=0;i<CONNECTION_MAX;i++)if(saved[i].id)saved[i].id=mapping[saved[i].id];
    free(keep);free(mapping);free(previous);free(base);
    if(!ok) { dj_close(&next);return 0; }
#ifdef _WIN32
    dj_close(&journal);dj_close(&next);
    if(!MoveFileExA(temporary,journal_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { journal.broken=1;return 0; }
#else
    if(storage_rename(temporary,journal_path)) { dj_close(&next);return 0; }
    dj_close(&journal);dj_close(&next);
    char directory[4096];strcpy(directory,journal_path);char *slash=strrchr(directory,'/');if(slash) { if(slash==directory)slash[1]=0;else *slash=0; }else strcpy(directory,".");
    int dir=open(directory,O_RDONLY);if(dir<0 || storage_sync(dir)) { if(dir>=0)storage_close(dir);journal.broken=1;return 0; }storage_close(dir);
#endif
    count=0;memset(live,0,sizeof(live));memset(totals,0,sizeof(totals));
    if(!dj_open(&journal,journal_path,"LNUSER01",apply,NULL)) { journal.broken=1;return 0; }
    memcpy(live,saved,sizeof(live));checked=journal.sequence;checked_count=count;return 1;
}
void userlog_table(uint32_t user,uint32_t cursor,int all,uint64_t now,char *out,size_t size) {
    if(!enabled || journal.broken) { snprintf(out,size,!enabled?"ERR userlog disabled\r\n":"ERR userlog storage\r\n");return; }
    if(user>MEMBER_MAX || cursor>count+1) { snprintf(out,size,"ERR command\r\n");return; }
    uint64_t *ordinal=calloc(MEMBER_MAX+1,sizeof(*ordinal));if(!ordinal) { snprintf(out,size,"ERR userlog allocation\r\n");return; }for(unsigned i=0;i<=MEMBER_MAX;i++)ordinal[i]=totals[i].logins;
    size_t index=cursor?cursor-1:count,used=(size_t)snprintf(out,size,"USERTABLE LOGIN LOGOUT SECONDS COUNT TRANSPORT\r\n");unsigned shown=0,scanned=0;
    for(size_t i=count;i>index;i--)if(events[i-1].kind=='L')ordinal[events[i-1].user]--;
    while(index && shown<8 && scanned++<256) { Event *e=&events[--index];if(e->kind!='L')continue;uint64_t number=ordinal[e->user]--;if(!all && e->user!=user)continue;
        uint64_t end=0,elapsed=0;const char *transport="UNKNOWN";
        for(size_t i=index;i>0;i--)if(events[i-1].kind=='C' && events[i-1].connection==e->connection) { transport=events[i-1].transport;break; }
        for(size_t i=index+1;i<count;i++)if(events[i].connection==e->connection && strchr("TOEX",events[i].kind)) { elapsed=events[i].elapsed;if(events[i].kind!='T') { end=events[i].at;break; } }
        if(!end) { Live *l=connection(e->connection);if(l && l->user==e->user && now>=l->start)elapsed=now-l->start; }
        used+=(size_t)snprintf(out+used,size-used,"SESSIONROW %u %u %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %s\r\n",e->connection,e->user,e->at,end,elapsed/1000,number,transport);shown++;
    }
    snprintf(out+used,size-used,"END NEXT %zu\r\n",index?index+1:0);free(ordinal);
}

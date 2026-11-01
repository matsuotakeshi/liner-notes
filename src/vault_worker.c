#include "app_limits.h"
#include "vault_worker.h"
#include "vault_bridge.h"
#include <string.h>
#include <stdio.h>
#include <openssl/crypto.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
static CRITICAL_SECTION mutex;
static CONDITION_VARIABLE condition;
static HANDLE worker;
#define LOCK() EnterCriticalSection(&mutex)
#define UNLOCK() LeaveCriticalSection(&mutex)
#define WAKE() WakeConditionVariable(&condition)
#define WAIT() SleepConditionVariableCS(&condition,&mutex,INFINITE)
#else
#include <pthread.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static pthread_t worker;
#define LOCK() pthread_mutex_lock(&mutex)
#define UNLOCK() pthread_mutex_unlock(&mutex)
#define WAKE() pthread_cond_signal(&condition)
#define WAIT() pthread_cond_wait(&condition,&mutex)
#endif
#define JOBS 16
typedef struct { int state; uint64_t ticket; BoardRequest request; BoardResult result; Principal principal; } Job;
static Job jobs[JOBS];
static struct { unsigned long id; int active; } sessions[CONNECTION_MAX];
static uint32_t epochs[(MEMBER_MAX+1)];
static uint64_t next_ticket;
static int running,ending,healthy;
static int valid_locked(const Job *j) {
    const BoardRequest *q=&j->request;
    return !ending && healthy && q->slot>=0 && q->slot<CONNECTION_MAX && sessions[q->slot].active && sessions[q->slot].id==q->session &&
        q->user>0 && q->user<=MEMBER_MAX && epochs[q->user]==q->user_epoch;
}
static int guard(void *context) { LOCK();int ok=valid_locked(context);UNLOCK();return ok; }
static int resolve(void *context,uint32_t user,uint32_t epoch,Principal *p) {
    Job *j=context;
    if(user!=j->request.user || epoch!=j->request.user_epoch || !guard(j))return 0;
    *p=j->principal;return 1;
}
#ifdef _WIN32
static unsigned __stdcall work(void *unused)
#else
static void *work(void *unused)
#endif
{
    (void)unused;LOCK();
    for(;;) {
        int index=-1;
        for(int i=0;i<JOBS;i++)if(jobs[i].state==1 && (index<0 || jobs[i].ticket<jobs[index].ticket))index=i;
        if(index<0) { if(ending)break;WAIT();continue; }
        Job *j=&jobs[index];j->state=2;UNLOCK();
        BoardRequest *q=&j->request;BoardResult *r=&j->result;memset(r,0,sizeof(*r));r->sigop=-1;r->session=q->session;r->slot=q->slot;r->zcontrol=q->zcontrol;
        IdentityProvider identity={j,resolve};vault_bridge_guard(guard,j);
        if(!guard(j)) { r->revoked=1;strcpy(r->text,"ERR session revoked\r\n"); }
        else if(q->op=='Y')vault_bridge_execute(identity,q->user,q->user_epoch,q->action,q->number,(*q->body || q->action==VAULT_CMD_DESCRIPTION || q->action==VAULT_CMD_BOARDSET || q->action>=VAULT_CMD_OWNER)?q->body:q->title,r->text,sizeof(r->text));
        else if(q->op=='^')vault_bridge_binary(identity,q,r);
        else vault_bridge_transfer(identity,q->user,q->user_epoch,q->body,q->zcontrol?(unsigned)q->scope:0,r->text,sizeof(r->text));
        vault_bridge_guard(NULL,NULL);
        if(q->op=='Y' && (q->action==VAULT_CMD_PASSWORD || q->action==VAULT_CMD_UNLOCK))OPENSSL_cleanse(q->body,sizeof(q->body));
        if(!guard(j)) { r->binary_size=0;r->revoked=1;strcpy(r->text,"ERR session revoked\r\n"); }
        LOCK();j->state=3;
    }
    UNLOCK();
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
int vault_worker_start(void) {
    memset(jobs,0,sizeof(jobs));memset(sessions,0,sizeof(sessions));memset(epochs,0,sizeof(epochs));ending=0;healthy=1;next_ticket=1;
#ifdef _WIN32
    InitializeCriticalSection(&mutex);InitializeConditionVariable(&condition);
    worker=(HANDLE)_beginthreadex(NULL,32*1024*1024,work,NULL,0,NULL);
    if(!worker) { DeleteCriticalSection(&mutex);return 0; }
#else
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,32*1024*1024);int created=pthread_create(&worker,&attr,work,NULL);pthread_attr_destroy(&attr);
    if(created)return 0;
#endif
    running=1;return 1;
}
void vault_worker_session(int slot,unsigned long id,int active) {
    if(!running || slot<0 || slot>=CONNECTION_MAX)return;
    LOCK();if(active || sessions[slot].id==id) { sessions[slot].id=id;sessions[slot].active=active; }UNLOCK();
}
void vault_worker_account(uint32_t user,uint32_t epoch) {
    if(!running || !user || user>MEMBER_MAX)return;
    LOCK();if(epoch>epochs[user])epochs[user]=epoch;UNLOCK();
}
void vault_worker_health(int ok) { if(running) { LOCK();healthy=ok;UNLOCK(); } }
int vault_worker_submit(const BoardRequest *q,const Principal *p) {
    if(!running || !q->user || q->user>MEMBER_MAX)return 0;
    LOCK();
    if(epochs[q->user]<q->user_epoch)epochs[q->user]=q->user_epoch;
    for(int i=0;i<JOBS;i++)if(!jobs[i].state) {
        Job *j=&jobs[i];j->request=*q;j->principal=*p;
        if(!valid_locked(j)) { UNLOCK();return 0; }
        j->ticket=next_ticket++;j->state=1;WAKE();UNLOCK();return 1;
    }
    UNLOCK();return 0;
}
int vault_worker_poll(BoardResult *r) {
    if(!running)return 0;
    LOCK();
    for(int i=0;i<JOBS;i++)if(jobs[i].state==3) { *r=jobs[i].result;OPENSSL_cleanse(&jobs[i],sizeof(jobs[i]));UNLOCK();return 1; }
    UNLOCK();return 0;
}
void vault_worker_stop(void) {
    if(!running)return;
    LOCK();ending=1;WAKE();UNLOCK();
#ifdef _WIN32
    WaitForSingleObject(worker,INFINITE);CloseHandle(worker);DeleteCriticalSection(&mutex);
#else
    pthread_join(worker,NULL);
#endif
    running=0;OPENSSL_cleanse(jobs,sizeof(jobs));
}

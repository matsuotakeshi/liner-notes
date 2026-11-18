#include "activity_core.h"
#include "activity_repository.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int member_bad,moderation_bad,profile_bad,suspended,admin=1,storage_ok=1;
static unsigned limit=2,count=1,appends;static uint32_t epoch=4;
int userlog_public,members_rtbbs_policy;
static ActivityRecord saved={.sequence=10,.actor=2,.time=123,.body="hello\r\n"};
static char name[33]="member",handle[81]="handle";
int members_broken(void) { return member_bad; }
int moderation_broken(void) { return moderation_bad; }
int profile_store_broken(void) { return profile_bad; }
const char *members_name(uint32_t id) { return id==1 || id==2?name:NULL; }
const char *profile_value(uint32_t id,unsigned field) { assert(id==2 && field==0);return handle; }
int moderation_suspended(uint32_t id) { (void)id;return suspended; }
uint32_t moderation_epoch(uint32_t id) { (void)id;return epoch; }
int moderation_admin(uint32_t id) { return id==1 && admin; }
/* Dependency seam, not a replacement test of chat's UTF-8 validator. */
int chat_text_valid(const char *s) {
    if(!*s || strlen(s)>512)return 0;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++)if(*p<32 || *p==127)return 0;
    return 1;
}
int activity_repository_ready(void) { return storage_ok; }
unsigned activity_repository_limit(void) { return limit; }
unsigned activity_repository_count(void) { return count; }
int activity_repository_read(unsigned cursor,ActivityRecord *out) {
    assert(cursor<count);if(!storage_ok)return 0;*out=saved;return 1;
}
static char last_kind;static uint32_t last_actor,last_target;static char last_text[3001];
int activity_repository_append(char kind,uint32_t actor,uint32_t target,const char *text) {
    appends++;if(!storage_ok)return 0;
    last_kind=kind;last_actor=actor;last_target=target;strcpy(last_text,text);return 1;
}
static int zero(const void *p,size_t n) {
    const unsigned char *s=p;for(size_t i=0;i<n;i++)if(s[i])return 0;return 1;
}
int main(void) {
    ActivityActor owner={1,4},member={2,4},guest={0,0},stale={1,3},missing={3,4};
    ActivityGather row;ActivityLogRequest log;ActivityControl effect;
    assert(activity_gather_post(&guest,"x\r\n")==ACTIVITY_LOGIN);
    assert(activity_gather_post(&stale,"x\r\n")==ACTIVITY_REVOKED);
    assert(activity_gather_post(&missing,"x\r\n")==ACTIVITY_REVOKED);
    suspended=1;assert(activity_control(&owner,ACTIVITY_ANNOUNCE,0,"x",&effect)==ACTIVITY_REVOKED);suspended=0;
    member_bad=1;assert(activity_gather_post(&owner,"x\r\n")==ACTIVITY_MEMBER_STORAGE);member_bad=0;
    moderation_bad=1;assert(activity_userlog(&owner,ACTIVITY_HISTORY,0,0,0,&log)==ACTIVITY_MODERATION_STORAGE);moderation_bad=0;
    assert(appends==0);puts("PASS: direct Core identity, epoch, suspension and unhealthy authority rejection");

    assert(!gather_line_valid(NULL) && gather_line_valid(""));
    assert(!activity_gather_valid(NULL) && !activity_gather_valid(""));
    assert(!activity_gather_valid("x\n") && !activity_gather_valid("x\r\ny"));
    assert(!activity_gather_valid("bad\033\r\n"));
    char body[3001]={0};memset(body,'x',70);body[70]=0;assert(gather_line_valid(body));
    body[70]='x';body[71]=0;assert(!gather_line_valid(body));
    body[0]=0;for(int i=0;i<10;i++)strcat(body,"\r\n");assert(activity_gather_valid(body));
    strcat(body,"\r\n");assert(!activity_gather_valid(body));
    limit=0;assert(activity_gather_post(&member,"ok\r\n")==ACTIVITY_DISABLED);limit=2;
    assert(activity_gather_post(&member,"ok\r\n")==ACTIVITY_OK);
    assert(last_kind=='G' && last_actor==2 && !last_target && !strcmp(last_text,"ok\r\n"));
    puts("PASS: Gather admission, CRLF/line/character bounds and disabled feature");

    assert(activity_gather_read(&member,0,&row)==ACTIVITY_OK && row.next==1 && !row.end);
    assert(row.sequence==10 && row.actor==2 && row.time==123 && !strcmp(row.body,"hello\r\n"));
    strcpy(name,"changed");strcpy(handle,"new");saved.body[0]='!';
    assert(!strcmp(row.name,"member") && !strcmp(row.handle,"handle") && row.body[0]=='h');
    assert(activity_gather_read(&member,1,&row)==ACTIVITY_OK && row.end);
    memset(&row,0xff,sizeof(row));assert(activity_gather_read(&member,2,&row)==ACTIVITY_INVALID && zero(&row,sizeof(row)));
    storage_ok=0;assert(activity_gather_read(&member,0,&row)==ACTIVITY_STORAGE && zero(&row,sizeof(row)));storage_ok=1;
    count=0;storage_ok=0;assert(activity_gather_read(&member,0,&row)==ACTIVITY_STORAGE && zero(&row,sizeof(row)));count=1;storage_ok=1;
    profile_bad=1;assert(activity_gather_read(&member,0,&row)==ACTIVITY_PROFILE_STORAGE);profile_bad=0;
    epoch++;assert(activity_gather_read(&member,0,&row)==ACTIVITY_REVOKED && zero(&row,sizeof(row)));epoch--;
    puts("PASS: copied bounded Gather snapshot, end/cursor, storage failure and page reauthorization");

    for(int k=ACTIVITY_HISTORY;k<=ACTIVITY_TABLE;k++) {
        assert(activity_userlog(&member,(ActivityLogKind)k,0,7,0,&log)==ACTIVITY_OK && log.target==2 && log.cursor==7 && !log.all);
        assert(activity_userlog(&member,(ActivityLogKind)k,1,0,0,&log)==ACTIVITY_DENIED && zero(&log,sizeof(log)));
        assert(activity_userlog(&member,(ActivityLogKind)k,0,0,1,&log)==ACTIVITY_DENIED);
        assert(activity_userlog(&owner,(ActivityLogKind)k,99,0,0,&log)==ACTIVITY_NO_MEMBER);
        assert(activity_userlog(&owner,(ActivityLogKind)k,99,0,1,&log)==ACTIVITY_OK && log.all);
        userlog_public=1;assert(activity_userlog(&member,(ActivityLogKind)k,1,0,0,&log)==ACTIVITY_OK);userlog_public=0;
        members_rtbbs_policy=1;assert(activity_userlog(&member,(ActivityLogKind)k,0,0,1,&log)==ACTIVITY_OK);members_rtbbs_policy=0;
    }
    assert(activity_userlog(&guest,ACTIVITY_HISTORY,1,0,1,&log)==ACTIVITY_LOGIN);
    assert(activity_userlog(&owner,(ActivityLogKind)99,0,0,0,&log)==ACTIVITY_INVALID);
    puts("PASS: history/stats/table privacy, admin/public/compatibility policies, missing member");

    unsigned before=appends;
    for(int k=ACTIVITY_CONNECTIONS;k<=ACTIVITY_CANCEL_SHUTDOWN;k++) {
        memset(&effect,0xff,sizeof(effect));
        assert(activity_control(&member,(ActivityControlKind)k,12,"x",&effect)==ACTIVITY_DENIED && zero(&effect,sizeof(effect)));
    }
    assert(appends==before);
    assert(activity_control(&owner,ACTIVITY_CONNECTIONS,99,NULL,&effect)==ACTIVITY_OK && appends==before);
    const char kinds[]="AKSC";
    for(int k=ACTIVITY_ANNOUNCE;k<=ACTIVITY_CANCEL_SHUTDOWN;k++) {
        assert(activity_control(&owner,(ActivityControlKind)k,12,"reason",&effect)==ACTIVITY_OK);
        assert(last_kind==kinds[k-1] && last_actor==1 && last_target==12 && !strcmp(last_text,"reason"));
        assert(effect.kind==(ActivityControlKind)k && effect.number==12 && !strcmp(effect.text,"reason"));
        storage_ok=0;memset(&effect,0xff,sizeof(effect));
        assert(activity_control(&owner,(ActivityControlKind)k,12,"reason",&effect)==ACTIVITY_STORAGE && zero(&effect,sizeof(effect)));storage_ok=1;
    }
    admin=0;assert(activity_control(&owner,ACTIVITY_ANNOUNCE,0,"x",&effect)==ACTIVITY_DENIED);admin=1;
    before=appends;assert(activity_control(&owner,(ActivityControlKind)99,0,"x",&effect)==ACTIVITY_INVALID);
    memset(body,'x',513);body[513]=0;
    assert(activity_control(&owner,ACTIVITY_ANNOUNCE,0,body,&effect)==ACTIVITY_INVALID);
    assert(activity_control(&owner,ACTIVITY_ANNOUNCE,0,"bad\n",&effect)==ACTIVITY_INVALID && appends==before);
    body[512]=0;assert(activity_control(&owner,ACTIVITY_ANNOUNCE,0,body,&effect)==ACTIVITY_OK);
    assert(activity_control(&owner,ACTIVITY_CANCEL_SHUTDOWN,0,"",&effect)==ACTIVITY_OK);
    assert(activity_gather_post(&owner,"bad")==ACTIVITY_INVALID);
    storage_ok=0;assert(activity_gather_post(&owner,"ok\r\n")==ACTIVITY_STORAGE);
    puts("PASS: admin-only control, audit-before-effect, no effect on failure, text boundaries");
    return 0;
}

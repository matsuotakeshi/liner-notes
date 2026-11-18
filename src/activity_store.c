#define _POSIX_C_SOURCE 200809L
#include "activity_repository.h"
#include "durable.h"
#include "members.h"
#include <stdlib.h>
static Durable journal={-1,0,0};
static DurableRow *gathers;
static unsigned count;
unsigned gather_limit=16;
static int apply(void *ctx,const DurableRow *r) {
    (void)ctx;if(!members_name(r->actor))return 0;
    if(r->kind=='G') {
        if(!activity_gather_valid(r->text))return 0;
        if(gather_limit) { if(count==gather_limit) { memmove(gathers,gathers+1,(count-1)*sizeof(*gathers));count--; }gathers[count++]=*r; }
    } else if(!strchr("AKSC",r->kind))return 0;
    return 1;
}
int activity_open(const char *base) {
    count=0;char path[4096];int n=snprintf(path,sizeof(path),"%s.activity",base);if(n<0 || (size_t)n>=sizeof(path))return 0;
    if(gather_limit && !(gathers=calloc(gather_limit,sizeof(*gathers))))return 0;
    if(!dj_open(&journal,path,"LNACTV01",apply,NULL)) { free(gathers);gathers=NULL;return 0; }return 1;
}
void activity_close(void) { dj_close(&journal);free(gathers);gathers=NULL;count=0; }

int activity_repository_ready(void) { return journal.fd>=0 && !journal.broken; }
unsigned activity_repository_limit(void) { return gather_limit; }
unsigned activity_repository_count(void) { return count; }
int activity_repository_read(unsigned cursor,ActivityRecord *out) {
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(journal.fd<0 || journal.broken || cursor>=count)return 0;
    const DurableRow *row=&gathers[cursor];out->actor=row->actor;
    out->sequence=row->sequence;out->time=row->time;strcpy(out->body,row->text);return 1;
}
int activity_repository_append(char kind,uint32_t actor,uint32_t target,const char *text) {
    if(!text || strlen(text)>DJ_TEXT || !kind || !strchr("GAKSC",kind))return 0;
    DurableRow row={0};row.kind=kind;row.actor=actor;row.target=target;strcpy(row.text,text);
    if(!dj_append(&journal,&row))return 0;
    return kind!='G' || apply(NULL,&row);
}

#define _POSIX_C_SOURCE 200809L
#include "date_filter.h"
#include "activity.h"
#include "durable.h"
#include "members.h"
#include "moderation.h"
#include "extras.h"
#include "profile_store.h"
#include "chat.h"
#include "messages.h"
#include "catalog.h"
#include "userlog.h"
#include <stdlib.h>
static Durable journal={-1,0,0};
static DurableRow *gathers;
static unsigned count;
unsigned gather_limit=16;
int gather_line_valid(const char *text) {
    if(!*text)return 1;
    if(!chat_text_valid(text))return 0;
    unsigned n=0;for(const unsigned char *p=(const unsigned char *)text;*p;p++)if((*p&0xc0)!=0x80)n++;
    return n<=70;
}
static int gather_valid(const char *body) {
    unsigned lines=0;const char *p=body;
    while(*p) { const char *e=strstr(p,"\r\n");if(!e || e-p>280 || ++lines>10)return 0;char line[281];memcpy(line,p,(size_t)(e-p));line[e-p]=0;if(!gather_line_valid(line))return 0;p=e+2; }
    return lines!=0;
}
static int apply(void *ctx,const DurableRow *r) {
    (void)ctx;if(!members_name(r->actor))return 0;
    if(r->kind=='G') {
        if(!gather_valid(r->text))return 0;
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
void activity_execute(const BoardRequest *q,BoardResult *r) {
    int lang=q->scope;const char *error="ERR command\r\n";
    if(q->action=='G' || q->action=='g') {
        if(!members_name(q->user)) { error="ERR login required\r\n";goto fail; }
        if(!gather_limit) { error="ERR feature disabled\r\n";goto fail; }
        if(q->action=='g') {
            if(!gather_valid(q->body))goto fail;
            DurableRow row={0};row.kind='G';row.actor=q->user;strcpy(row.text,q->body);
            if(!dj_append(&journal,&row) || !apply(NULL,&row)) { error="ERR activity storage\r\n";goto fail; }
            r->service_action='g';snprintf(r->text,sizeof(r->text),"%s",lang?message_text(lang==2?LANGUAGE_EN:LANGUAGE_JA,MSG_GATHER_SAVED):"GATHER SAVED\r\n");return;
        }
        unsigned start=q->cursor;size_t used=0;
        if(start>count)goto fail;
        used=(size_t)snprintf(r->text,sizeof(r->text),"Gather world\r\n");
        for(unsigned i=start;i<count;i++) {
            DurableRow *row=&gathers[i];size_t need=strlen(row->text)+180;
            if(used+need+100>=sizeof(r->text)) { r->service_next=i;break; }
            time_t at=(time_t)row->time;struct tm *utc=localtime(&at);char date[32]="unknown";if(utc)strftime(date,sizeof(date),date_pattern(),utc);
            used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"%s %s %s\r\n%s",date,members_name(row->actor),profile_value(row->actor,0),row->text);
        }
        snprintf(r->text+used,sizeof(r->text)-used,"GATHER END NEXT %u\r\n",r->service_next);r->service_action='G';return;
    }
    if(q->action=='U' || q->action=='V' || q->action=='u') {
        if(!members_name(q->user)) { error="ERR login required\r\n";goto fail; }
        uint32_t target=q->number?q->number:q->user;
        if((q->notes_unread || target!=q->user) && !moderation_admin(q->user) && !userlog_public && !members_rtbbs_policy) { error="ERR admin required\r\n";goto fail; }
        if(!q->notes_unread && !members_name(target)) { error="ERR no member\r\n";goto fail; }
        r->service_action=q->action;r->service_number=target;r->service_next=q->cursor;
        if(q->notes_unread)strcpy(r->service_text,"ALL");
        return;
    }
    if(q->action=='i') {
        if(!catalog_manage(q->number,q->user)) { error="ERR admin required\r\n";goto fail; }
        catalog_settings(q->number,r->text,sizeof(r->text));return;
    }
    if(q->action=='j') {
        if(!moderation_admin(q->user) && !catalog_sigop(q->board,q->user)) { error="ERR admin required\r\n";goto fail; }
        catalog_history(q->user,q->cursor,r->text,sizeof(r->text));return;
    }
    if(q->action=='v') { if(!members_name(q->user)) { error="ERR login required\r\n";goto fail; }catalog_roles(q->user,r->text,sizeof(r->text));return; }
    if(q->action!='b' && q->action!='s' && !moderation_admin(q->user)) { error="ERR admin required\r\n";goto fail; }
    if(q->action=='s') {
        uint32_t previous=0;int ok=catalog_role_set(q->number,q->cursor,q->user,&previous);
        if(ok<=0) { error=ok<0?"ERR catalog storage\r\n":"ERR catalog syntax\r\n";goto fail; }
        if(ok==1) {
            if(previous) { moderation_credentials_changed(previous);r->account_user2=previous;r->account_epoch2=moderation_epoch(previous); }
            if(q->cursor) { moderation_credentials_changed(q->cursor);r->account_user=q->cursor;r->account_epoch=moderation_epoch(q->cursor); }
        }
        snprintf(r->text,sizeof(r->text),"SIGOP SET %u %u\r\n",q->number,q->cursor);return;
    }
    if(q->action=='b') {
        /* catalog_set validates both the existing and proposed parent scopes. */
        uint32_t id;int ok=catalog_set(q->user,q->body,&id);
        if(ok<=0) { error=ok<0?"ERR catalog storage\r\n":"ERR catalog syntax\r\n";goto fail; }
        r->service_action='b';snprintf(r->text,sizeof(r->text),"BOARD UPDATED %u\r\n",id);return;
    }
    if(q->action=='L') { r->service_action='L';return; }
    if(!strchr("AKSC",q->action) || !q->action || strlen(q->body)>512 || (*q->body && !chat_text_valid(q->body)))goto fail;
    DurableRow row={0};row.kind=(char)q->action;row.actor=q->user;row.target=q->number;strcpy(row.text,q->body);
    if(!dj_append(&journal,&row)) { error="ERR activity storage\r\n";goto fail; }
    r->service_action=q->action;r->service_number=q->number;strcpy(r->service_text,q->body);return;
fail:snprintf(r->text,sizeof(r->text),"%s",error);
}

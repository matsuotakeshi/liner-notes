#define _POSIX_C_SOURCE 200809L
#include "date_filter.h"
#include "activity.h"
#include "article_core.h"
#include "catalog_core.h"
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
static void catalog_result(const BoardRequest *q,CatalogOutcome result,BoardResult *r) {
    const char *error="ERR catalog syntax\r\n";
    switch(result) {
    case CATALOG_CORE_OK:return;
    case CATALOG_CORE_LOGIN_REQUIRED:
        error=q->action=='v'?"ERR login required\r\n":
              (q->action=='b'?"ERR catalog syntax\r\n":"ERR admin required\r\n");break;
    case CATALOG_CORE_REVOKED:r->revoked=1;error="ERR session revoked\r\n";break;
    case CATALOG_CORE_MEMBER_STORAGE:error="ERR member storage unavailable\r\n";break;
    case CATALOG_CORE_MODERATION_STORAGE:error="ERR moderation storage unavailable\r\n";break;
    case CATALOG_CORE_POLICY_STORAGE:error="ERR board policy storage\r\n";break;
    case CATALOG_CORE_STORAGE:error="ERR catalog storage\r\n";break;
    case CATALOG_CORE_DENIED:error="ERR admin required\r\n";break;
    case CATALOG_CORE_INVALID:break;
    case CATALOG_CORE_CURSOR:error="ERR command\r\n";break;
    }
    snprintf(r->text,sizeof(r->text),"%s",error);
}
static void catalog_execute(const BoardRequest *q,BoardResult *r) {
    CatalogActor actor={q->user,q->user_epoch};CatalogOutcome result;
    if(q->action=='i') {
        CatalogConfig info;result=catalog_core_info(&actor,q->number,&info);
        if(result!=CATALOG_CORE_OK) { catalog_result(q,result,r);return; }
        snprintf(r->text,sizeof(r->text),"BOARDSETTING %u|%u|%s|%s|%s|%s\r\n",
                 info.id,info.parent,info.read_acl,info.write_acl,info.title,info.description);
    } else if(q->action=='b') {
        uint32_t id=0;result=catalog_core_update(&actor,q->body,&id);
        if(result!=CATALOG_CORE_OK) { catalog_result(q,result,r);return; }
        r->service_action='b';snprintf(r->text,sizeof(r->text),"BOARD UPDATED %u\r\n",id);
    } else if(q->action=='v') {
        CatalogRoles roles;result=catalog_core_roles(&actor,&roles);
        if(result!=CATALOG_CORE_OK) { catalog_result(q,result,r);return; }
        size_t used=(size_t)snprintf(r->text,sizeof(r->text),"SIGOPS\r\n");
        for(unsigned i=0;i<roles.count;i++)
            used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"SIGOP %u %u\r\n",roles.rows[i].board,roles.rows[i].user);
        snprintf(r->text+used,sizeof(r->text)-used,"END SIGOPS\r\n");
    } else {
        CatalogHistory history;result=catalog_core_history(&actor,q->board,q->cursor,&history);
        if(result!=CATALOG_CORE_OK) { catalog_result(q,result,r);return; }
        size_t used=(size_t)snprintf(r->text,sizeof(r->text),"BOARDLOG\r\n");
        for(unsigned i=0;i<history.count;i++) {
            const CatalogChange *row=&history.rows[i];
            used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"BOARDCHANGE %u %u %llu %s\r\n",
                                  row->sequence,row->actor,(unsigned long long)row->time,row->specification);
        }
        snprintf(r->text+used,sizeof(r->text)-used,"END NEXT %u\r\n",history.next);
    }
}
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
    if(q->action=='i' || q->action=='j' || q->action=='v' || q->action=='b') {
        catalog_execute(q,r);return;
    }
    if(q->action!='s' && !moderation_admin(q->user)) { error="ERR admin required\r\n";goto fail; }
    if(q->action=='s') {
        ArticleActor actor={q->user,q->user_epoch,q->board_grants};ArticleRoleChange change;
        ArticleOutcome result=article_sigop_set(&actor,q->number,q->cursor,&change);
        if(result!=ARTICLE_OK) {
            switch(result) {
            case ARTICLE_REVOKED:r->revoked=1;error="ERR session revoked\r\n";break;
            case ARTICLE_LOGIN_REQUIRED:error="ERR login required\r\n";break;
            case ARTICLE_MEMBER_STORAGE:error="ERR member storage unavailable\r\n";break;
            case ARTICLE_MODERATION_STORAGE:error="ERR moderation storage unavailable\r\n";break;
            case ARTICLE_POLICY_STORAGE:error="ERR board policy storage\r\n";break;
            case ARTICLE_EXTRAS_STORAGE:error="ERR extras storage\r\n";break;
            case ARTICLE_CATALOG_STORAGE:error="ERR catalog storage\r\n";break;
            default:error="ERR catalog syntax\r\n";break;
            }
            goto fail;
        }
        r->account_user2=change.previous;r->account_epoch2=change.previous_epoch;
        r->account_user=change.current;r->account_epoch=change.current_epoch;
        snprintf(r->text,sizeof(r->text),"SIGOP SET %u %u\r\n",q->number,q->cursor);return;
    }
    if(q->action=='L') { r->service_action='L';return; }
    if(!strchr("AKSC",q->action) || !q->action || strlen(q->body)>512 || (*q->body && !chat_text_valid(q->body)))goto fail;
    DurableRow row={0};row.kind=(char)q->action;row.actor=q->user;row.target=q->number;strcpy(row.text,q->body);
    if(!dj_append(&journal,&row)) { error="ERR activity storage\r\n";goto fail; }
    r->service_action=q->action;r->service_number=q->number;strcpy(r->service_text,q->body);return;
fail:snprintf(r->text,sizeof(r->text),"%s",error);
}

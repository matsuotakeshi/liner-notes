#define _POSIX_C_SOURCE 200809L
#include "date_filter.h"
#include "activity.h"
#include "article_core.h"
#include "catalog_core.h"
#include "activity_core.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "moderation.h"
#include "messages.h"
#include "catalog.h"
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
    case CATALOG_CORE_INVALID:case CATALOG_CORE_NOT_EMPTY:
    case CATALOG_CORE_INFO_LIMIT:case CATALOG_CORE_BUFFER:break;
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
static void activity_result(ActivityOutcome result,int admin,BoardResult *r) {
    const char *error="ERR command\r\n";
    switch(result) {
    case ACTIVITY_OK:return;
    case ACTIVITY_LOGIN:error=admin?"ERR admin required\r\n":"ERR login required\r\n";break;
    case ACTIVITY_REVOKED:r->revoked=1;error="ERR session revoked\r\n";break;
    case ACTIVITY_DENIED:error="ERR admin required\r\n";break;
    case ACTIVITY_MEMBER_STORAGE:error="ERR member storage unavailable\r\n";break;
    case ACTIVITY_MODERATION_STORAGE:error="ERR moderation storage unavailable\r\n";break;
    case ACTIVITY_PROFILE_STORAGE:error="ERR extras storage\r\n";break;
    case ACTIVITY_STORAGE:error="ERR activity storage\r\n";break;
    case ACTIVITY_DISABLED:error="ERR feature disabled\r\n";break;
    case ACTIVITY_NO_MEMBER:error="ERR no member\r\n";break;
    case ACTIVITY_INVALID:break;
    }
    r->feedback.failed=1;r->service_action=0;r->service_number=r->service_next=0;r->service_text[0]=0;
    snprintf(r->text,sizeof(r->text),"%s",error);
}
void activity_execute(const BoardRequest *q,BoardResult *r) {
    int lang=q->scope;const char *error="ERR command\r\n";
    ActivityActor actor={q->user,q->user_epoch};ActivityOutcome outcome;
    if(q->action=='G' || q->action=='g') {
        if(q->action=='g') {
            outcome=activity_gather_post(&actor,q->body);
            if(outcome!=ACTIVITY_OK) { activity_result(outcome,0,r);return; }
            r->service_action='g';snprintf(r->text,sizeof(r->text),"%s",lang?message_text(lang==2?LANGUAGE_EN:LANGUAGE_JA,MSG_GATHER_SAVED):"GATHER SAVED\r\n");return;
        }
        unsigned cursor=q->cursor;size_t used=0;ActivityGather row;
        outcome=activity_gather_read(&actor,cursor,&row);
        if(outcome!=ACTIVITY_OK) { activity_result(outcome,0,r);return; }
        used=(size_t)snprintf(r->text,sizeof(r->text),"Gather world\r\n");
        while(!row.end) {
            size_t need=strlen(row.body)+180;
            if(used+need+100>=sizeof(r->text)) { r->service_next=cursor;break; }
            time_t at=(time_t)row.time;struct tm *utc=localtime(&at);char date[32]="unknown";if(utc)strftime(date,sizeof(date),date_pattern(),utc);
            used+=(size_t)snprintf(r->text+used,sizeof(r->text)-used,"%s %s %s\r\n%s",date,row.name,row.handle,row.body);
            cursor=row.next;outcome=activity_gather_read(&actor,cursor,&row);
            if(outcome!=ACTIVITY_OK) { activity_result(outcome,0,r);return; }
        }
        snprintf(r->text+used,sizeof(r->text)-used,"GATHER END NEXT %u\r\n",r->service_next);r->service_action='G';return;
    }
    if(q->action=='U' || q->action=='V' || q->action=='u') {
        ActivityLogRequest request;
        outcome=activity_userlog(&actor,q->action=='U'?ACTIVITY_HISTORY:q->action=='V'?ACTIVITY_STATS:ACTIVITY_TABLE,
                                q->number,q->cursor,q->notes_unread,&request);
        if(outcome!=ACTIVITY_OK) { activity_result(outcome,0,r);return; }
        r->service_action=q->action;r->service_number=request.target;r->service_next=request.cursor;
        if(request.all)strcpy(r->service_text,"ALL");
        return;
    }
    if(q->action=='i' || q->action=='j' || q->action=='v' || q->action=='b') {
        catalog_execute(q,r);return;
    }
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
    ActivityControlKind kind;
    switch(q->action) {
    case 'L':kind=ACTIVITY_CONNECTIONS;break;
    case 'A':kind=ACTIVITY_ANNOUNCE;break;
    case 'K':kind=ACTIVITY_DISCONNECT;break;
    case 'S':kind=ACTIVITY_SHUTDOWN;break;
    case 'C':kind=ACTIVITY_CANCEL_SHUTDOWN;break;
    default:kind=(ActivityControlKind)-1;break;
    }
    ActivityControl control;outcome=activity_control(&actor,kind,q->number,q->body,&control);
    if(outcome!=ACTIVITY_OK) { activity_result(outcome,1,r);return; }
    r->service_action=q->action;r->service_number=control.number;strcpy(r->service_text,control.text);return;
fail:snprintf(r->text,sizeof(r->text),"%s",error);
}

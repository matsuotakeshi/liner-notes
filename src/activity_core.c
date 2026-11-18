#include "activity_core.h"
#include "activity_repository.h"
#include "members.h"
#include "moderation.h"
#include "profile_store.h"
#include "userlog.h"
#include "chat.h"
#include <string.h>
static ActivityOutcome authorize(const ActivityActor *actor) {
    if(members_broken())return ACTIVITY_MEMBER_STORAGE;
    if(moderation_broken())return ACTIVITY_MODERATION_STORAGE;
    if(!actor || !actor->user)return ACTIVITY_LOGIN;
    if(!members_name(actor->user) || moderation_suspended(actor->user) ||
       moderation_epoch(actor->user)!=actor->epoch)return ACTIVITY_REVOKED;
    return ACTIVITY_OK;
}
int gather_line_valid(const char *text) {
    if(!text)return 0;
    if(!*text)return 1;
    if(!chat_text_valid(text))return 0;
    unsigned n=0;for(const unsigned char *p=(const unsigned char *)text;*p;p++)if((*p&0xc0)!=0x80)n++;
    return n<=70;
}
int activity_gather_valid(const char *body) {
    if(!body)return 0;
    unsigned lines=0;const char *p=body;
    while(*p) {
        const char *e=strstr(p,"\r\n");if(!e || e-p>280 || ++lines>10)return 0;
        char line[281];memcpy(line,p,(size_t)(e-p));line[e-p]=0;
        if(!gather_line_valid(line))return 0;
        p=e+2;
    }
    return lines!=0;
}
static ActivityOutcome gather_authorize(const ActivityActor *actor) {
    ActivityOutcome result=authorize(actor);if(result!=ACTIVITY_OK)return result;
    if(!activity_repository_limit())return ACTIVITY_DISABLED;
    return activity_repository_ready()?ACTIVITY_OK:ACTIVITY_STORAGE;
}
ActivityOutcome activity_gather_post(const ActivityActor *actor,const char *body) {
    ActivityOutcome result=gather_authorize(actor);if(result!=ACTIVITY_OK)return result;
    if(!activity_gather_valid(body))return ACTIVITY_INVALID;
    return activity_repository_append('G',actor->user,0,body)?ACTIVITY_OK:ACTIVITY_STORAGE;
}
ActivityOutcome activity_gather_read(const ActivityActor *actor,unsigned cursor,ActivityGather *out) {
    if(!out)return ACTIVITY_INVALID;
    memset(out,0,sizeof(*out));
    ActivityOutcome result=gather_authorize(actor);if(result!=ACTIVITY_OK)return result;
    if(profile_store_broken())return ACTIVITY_PROFILE_STORAGE;
    unsigned count=activity_repository_count();if(cursor>count)return ACTIVITY_INVALID;
    if(cursor==count) { out->end=1;return ACTIVITY_OK; }
    ActivityRecord row={0};if(!activity_repository_read(cursor,&row))return ACTIVITY_STORAGE;
    const char *name=members_name(row.actor),*handle=profile_value(row.actor,0);
    if(!name || !handle || strlen(name)>=sizeof(out->name) || strlen(handle)>=sizeof(out->handle))return ACTIVITY_STORAGE;
    out->actor=row.actor;out->sequence=row.sequence;out->time=row.time;out->next=cursor+1;
    strcpy(out->name,name);strcpy(out->handle,handle);strcpy(out->body,row.body);return ACTIVITY_OK;
}
ActivityOutcome activity_userlog(const ActivityActor *actor,ActivityLogKind kind,uint32_t target,
                                unsigned cursor,int all,ActivityLogRequest *out) {
    if(!out)return ACTIVITY_INVALID;
    memset(out,0,sizeof(*out));
    ActivityOutcome result=authorize(actor);if(result!=ACTIVITY_OK)return result;
    if(kind!=ACTIVITY_HISTORY && kind!=ACTIVITY_STATS && kind!=ACTIVITY_TABLE)return ACTIVITY_INVALID;
    if(!target)target=actor->user;
    if((all || target!=actor->user) && !moderation_admin(actor->user) &&
       !userlog_public && !members_rtbbs_policy)return ACTIVITY_DENIED;
    if(!all && !members_name(target))return ACTIVITY_NO_MEMBER;
    out->kind=kind;out->target=target;out->cursor=cursor;out->all=!!all;return ACTIVITY_OK;
}
ActivityOutcome activity_control(const ActivityActor *actor,ActivityControlKind kind,uint32_t number,
                                 const char *text,ActivityControl *out) {
    if(!out)return ACTIVITY_INVALID;
    memset(out,0,sizeof(*out));
    ActivityOutcome result=authorize(actor);if(result!=ACTIVITY_OK)return result;
    if(!moderation_admin(actor->user))return ACTIVITY_DENIED;
    char wire=0;
    switch(kind) {
    case ACTIVITY_CONNECTIONS:out->kind=kind;return ACTIVITY_OK;
    case ACTIVITY_ANNOUNCE:wire='A';break;
    case ACTIVITY_DISCONNECT:wire='K';break;
    case ACTIVITY_SHUTDOWN:wire='S';break;
    case ACTIVITY_CANCEL_SHUTDOWN:wire='C';break;
    default:return ACTIVITY_INVALID;
    }
    if(!text || strlen(text)>512 || (*text && !chat_text_valid(text)))return ACTIVITY_INVALID;
    if(!activity_repository_append(wire,actor->user,number,text))return ACTIVITY_STORAGE;
    out->kind=kind;out->number=number;strcpy(out->text,text);return ACTIVITY_OK;
}

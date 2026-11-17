#include "member_core.h"
#include "members.h"
#include "moderation.h"
#include "app_limits.h"
#include <string.h>

static MemberOutcome authorize(const MemberActor *actor,int admin) {
    if(members_broken())return MEMBER_CORE_STORAGE;
    if(moderation_broken())return MEMBER_CORE_MODERATION_STORAGE;
    uint32_t user=actor?actor->user:0;
    if(user && (!members_name(user) || moderation_suspended(user) || moderation_epoch(user)!=actor->epoch))
        return MEMBER_CORE_REVOKED;
    if(admin && (!user || !moderation_admin(user)))return MEMBER_CORE_ADMIN_REQUIRED;
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_role(const MemberActor *actor,MemberRole *out) {
    if(!out)return MEMBER_CORE_INVALID;
    *out=MEMBER_ROLE_NONE;
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    uint32_t user=actor?actor->user:0;
    *out=!user?MEMBER_ROLE_GUEST:moderation_admin(user)?MEMBER_ROLE_ADMIN:MEMBER_ROLE_MEMBER;
    return MEMBER_CORE_OK;
}

static MemberOutcome summary(uint32_t id,MemberSummary *out) {
    const char *name=members_name(id);
    if(!name)return MEMBER_CORE_NOT_FOUND;
    size_t length=0;while(length<=32 && name[length])length++;
    if(length>32)return MEMBER_CORE_STORAGE;
    out->id=id;memcpy(out->name,name,length+1);
    out->state=members_pending(id)?MEMBER_STATE_PENDING:
               moderation_suspended(id)?MEMBER_STATE_SUSPENDED:MEMBER_STATE_ACTIVE;
    if(members_broken())return MEMBER_CORE_STORAGE;
    if(moderation_broken())return MEMBER_CORE_MODERATION_STORAGE;
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_get(const MemberActor *actor,uint32_t target,MemberSummary *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,1);
    if(result==MEMBER_CORE_OK)result=summary(target,out);
    if(result!=MEMBER_CORE_OK)memset(out,0,sizeof(*out));
    return result;
}

MemberOutcome member_core_list(const MemberActor *actor,uint32_t start,MemberPage *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,1);
    if(result!=MEMBER_CORE_OK)return result;
    uint32_t total=members_count();
    if(total>MEMBER_MAX)return MEMBER_CORE_STORAGE;
    uint32_t id=start?start:1;
    for(;id<=total;id++) {
        if(!members_name(id))continue;
        if(out->count==8)break;
        result=summary(id,&out->rows[out->count]);
        if(result!=MEMBER_CORE_OK)break;
        out->count++;
    }
    if(members_broken())result=MEMBER_CORE_STORAGE;
    else if(moderation_broken())result=MEMBER_CORE_MODERATION_STORAGE;
    if(result!=MEMBER_CORE_OK)memset(out,0,sizeof(*out));
    else out->next=id<=total?id:0;
    return result;
}

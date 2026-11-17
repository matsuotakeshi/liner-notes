#include "member_core.h"
#include "members.h"
#include "moderation.h"
#include "extras.h"
#include "catalog.h"
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

MemberOutcome member_core_suspend(const MemberActor *actor,uint32_t target,int suspend,MemberChange *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,1);
    if(result!=MEMBER_CORE_OK)return result;
    if(extras_broken())return MEMBER_CORE_EXTRAS_STORAGE;
    if(suspend!=0 && suspend!=1)return MEMBER_CORE_INVALID;
    if(!members_name(target))return MEMBER_CORE_NOT_FOUND;
    if(target==actor->user || moderation_protected(target))return MEMBER_CORE_PROTECTED;
    if((uint64_t)moderation_count()+extras_audit_count()>=UINT32_MAX)
        return MEMBER_CORE_MODERATION_UNCERTAIN;
    if(!moderation_suspend(target,actor->user,suspend))return MEMBER_CORE_MODERATION_UNCERTAIN;
    out->user=target;out->epoch=moderation_epoch(target);
    return MEMBER_CORE_OK;
}

static MemberOutcome authorize_member_admin(const MemberActor *actor) {
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    return moderation_admin(actor->user)?MEMBER_CORE_OK:MEMBER_CORE_ADMIN_REQUIRED;
}

MemberOutcome member_core_applications(const MemberActor *actor,uint32_t start,MemberPage *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize_member_admin(actor);
    if(result!=MEMBER_CORE_OK)return result;
    uint32_t total=members_count();
    if(total>MEMBER_MAX)return MEMBER_CORE_STORAGE;
    uint32_t id=start?start:1;
    for(;id<=total;id++) {
        if(!members_pending(id))continue;
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

MemberOutcome member_core_approve(const MemberActor *actor,uint32_t target,MemberChange *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize_member_admin(actor);
    if(result!=MEMBER_CORE_OK)return result;
    if(!members_active(target))return MEMBER_CORE_NOT_FOUND;
    int saved=members_approve(target,actor->user);
    if(saved<=0)return saved<0?MEMBER_CORE_STORAGE:MEMBER_CORE_RIGHTS_INVALID;
    if(saved==1) {
        moderation_credentials_changed(target);
        out->user=target;out->epoch=moderation_epoch(target);
    }
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_rights(const MemberActor *actor,uint32_t target,MemberRights *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    if(target!=actor->user && !moderation_admin(actor->user))return MEMBER_CORE_ADMIN_REQUIRED;
    if(!members_active(target))return MEMBER_CORE_NOT_FOUND;
    out->user=target;out->level=members_level(target);
    for(unsigned i=0;i<100;i++)out->groups[i]=(unsigned char)!!members_in_group(target,i+1);
    out->state=members_pending(target)?MEMBER_STATE_PENDING:
               moderation_suspended(target)?MEMBER_STATE_SUSPENDED:MEMBER_STATE_ACTIVE;
    if(members_broken() || out->level<0 || out->level>5)result=MEMBER_CORE_STORAGE;
    else if(moderation_broken())result=MEMBER_CORE_MODERATION_STORAGE;
    if(result!=MEMBER_CORE_OK)memset(out,0,sizeof(*out));
    return result;
}

MemberOutcome member_core_level(const MemberActor *actor,uint32_t target,unsigned level,MemberChange *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize_member_admin(actor);
    if(result!=MEMBER_CORE_OK)return result;
    if(!members_active(target))return MEMBER_CORE_NOT_FOUND;
    if(level>5)return MEMBER_CORE_RIGHTS_INVALID;
    int saved=members_rights(target,actor->user,(int)level,members_groups(target));
    if(saved<=0)return saved<0?MEMBER_CORE_STORAGE:MEMBER_CORE_RIGHTS_INVALID;
    if(saved==1) {
        moderation_credentials_changed(target);
        out->user=target;out->epoch=moderation_epoch(target);
    }
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_group(const MemberActor *actor,uint32_t board,uint32_t target,unsigned group,int add,MemberChange *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    if(!moderation_admin(actor->user) &&
       !(catalog_sigop(board,actor->user) && catalog_group(board,group) && !moderation_protected(target)))
        return MEMBER_CORE_ADMIN_REQUIRED;
    if(!members_active(target))return MEMBER_CORE_NOT_FOUND;
    if(!group || group>100 || (add!=0 && add!=1))return MEMBER_CORE_RIGHTS_INVALID;
    int saved=members_group_change(target,actor->user,group,add);
    if(saved<=0)return saved<0?MEMBER_CORE_STORAGE:MEMBER_CORE_RIGHTS_INVALID;
    if(saved==1) {
        moderation_credentials_changed(target);
        out->user=target;out->epoch=moderation_epoch(target);
    }
    return MEMBER_CORE_OK;
}

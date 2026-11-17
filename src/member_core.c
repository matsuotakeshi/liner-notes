#include "member_core.h"
#include "members.h"
#include "moderation.h"
#include "extras.h"
#include "profile_store.h"
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

MemberOutcome member_core_group_move(const MemberActor *actor,unsigned from,unsigned to,MemberGroupMove *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize_member_admin(actor);
    if(result!=MEMBER_CORE_OK)return result;
    if(!from || from>100 || !to || to>100 || from==to)return MEMBER_CORE_RIGHTS_INVALID;
    uint32_t total=members_count();
    if(total>MEMBER_MAX)return MEMBER_CORE_STORAGE;
    int saved=members_group_move(actor->user,from,to);
    if(saved!=1)return saved<0?MEMBER_CORE_STORAGE:MEMBER_CORE_RIGHTS_INVALID;
    for(uint32_t user=1;user<=total;user++)if(members_name(user))moderation_credentials_changed(user);
    out->through=total;out->actor_epoch=moderation_epoch(actor->user);
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_remove(const MemberActor *actor,uint32_t target,unsigned kind,MemberChange *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    uint32_t user=actor?actor->user:0;
    if(kind!=MEMBER_RETIRE && (!user || !moderation_admin(user)))return MEMBER_CORE_ADMIN_REQUIRED;
    if(kind==MEMBER_RETIRE)target=user;
    if(!user || !members_active(target))return MEMBER_CORE_NOT_FOUND;
    if(moderation_protected(target) || (target==user && kind!=MEMBER_RETIRE))return MEMBER_CORE_PROTECTED;
    if(kind<MEMBER_REJECT || kind>MEMBER_DELETE)return MEMBER_CORE_STATE;
    int saved=members_remove(target,user,kind);
    if(saved<=0)return saved<0?MEMBER_CORE_STORAGE:MEMBER_CORE_STATE;
    moderation_credentials_changed(target);
    out->user=target;out->epoch=moderation_epoch(target);
    return MEMBER_CORE_OK;
}

static int password_length(const char *value) {
    if(!value)return 0;
    size_t n=0;while(n<=128 && value[n])n++;
    return n>=8 && n<=128;
}

MemberOutcome member_core_reset_check(const MemberActor *actor,uint32_t target) {
    MemberOutcome result=authorize(actor,1);
    if(result!=MEMBER_CORE_OK)return result;
    if(!members_name(target))return MEMBER_CORE_NOT_FOUND;
    if(target==actor->user || moderation_protected(target))return MEMBER_CORE_PROTECTED;
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_reset_password(const MemberActor *actor,uint32_t target,const char *replacement,MemberChange *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=member_core_reset_check(actor,target);
    if(result!=MEMBER_CORE_OK)return result;
    if(!password_length(replacement))return MEMBER_CORE_PASSWORD;
    int changed=members_reset_password(target,actor->user,replacement);
    if(changed!=1)return changed<0?MEMBER_CORE_STORAGE:MEMBER_CORE_PASSWORD;
    moderation_credentials_changed(target);
    out->user=target;out->epoch=moderation_epoch(target);
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_password(const MemberActor *actor,const char *current,const char *replacement,MemberChange *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    if(!password_length(current) || !password_length(replacement))return MEMBER_CORE_PASSWORD;
    int changed=members_password(actor->user,current,replacement);
    if(changed!=1)return changed<0?MEMBER_CORE_STORAGE:MEMBER_CORE_PASSWORD;
    moderation_credentials_changed(actor->user);
    out->user=actor->user;out->epoch=moderation_epoch(actor->user);
    return MEMBER_CORE_OK;
}

static MemberOutcome preferences_read(uint32_t user,MemberPreferences *out) {
    out->present=members_preferences(user,&out->language,&out->pager);
    if(out->present)members_ui_preferences(user,&out->editor,&out->columns,&out->rows,&out->aux);
    MemberOutcome result=members_broken()?MEMBER_CORE_STORAGE:
                         moderation_broken()?MEMBER_CORE_MODERATION_STORAGE:MEMBER_CORE_OK;
    if(result!=MEMBER_CORE_OK || !out->present)memset(out,0,sizeof(*out));
    return result;
}

MemberOutcome member_core_preferences(const MemberActor *actor,MemberPreferences *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    return preferences_read(actor->user,out);
}

MemberOutcome member_core_save_preferences(const MemberActor *actor,const MemberPreferences *input,MemberPreferences *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    if(!input || input->language<0 || input->language>1 || input->pager<0 || input->pager>1)
        return MEMBER_CORE_PREFERENCES;
    if(!members_save_ui_preferences(actor->user,input->language,input->pager,input->editor,input->columns,input->rows,input->aux))
        return MEMBER_CORE_STORAGE;
    return preferences_read(actor->user,out);
}

MemberOutcome member_core_mail_notify(const MemberActor *actor,MemberNotifyAction action,MemberMailNotify *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    if(action!=MEMBER_NOTIFY_QUERY && action!=MEMBER_NOTIFY_ON && action!=MEMBER_NOTIFY_OFF)
        return MEMBER_CORE_INVALID;
    if(action!=MEMBER_NOTIFY_QUERY && !members_set_mail_notify(actor->user,action==MEMBER_NOTIFY_OFF))
        return MEMBER_CORE_STORAGE;
    members_mail_notify(actor->user,&out->deny,&out->revision);
    result=members_broken()?MEMBER_CORE_STORAGE:
           moderation_broken()?MEMBER_CORE_MODERATION_STORAGE:MEMBER_CORE_OK;
    if(result!=MEMBER_CORE_OK)memset(out,0,sizeof(*out));
    else out->user=actor->user;
    return result;
}

MemberOutcome member_core_chat_kick(const MemberActor *actor,const char *login,MemberChatKick *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,1);
    if(result!=MEMBER_CORE_OK)return result;
    if(extras_broken())return MEMBER_CORE_EXTRAS_STORAGE;
    if(!login)return MEMBER_CORE_INVALID;
    size_t length=0;while(length<=32 && login[length])length++;
    if(!length || length>32)return MEMBER_CORE_NOT_FOUND;
    uint32_t total=members_count(),target=0;
    if(total>MEMBER_MAX)return MEMBER_CORE_STORAGE;
    for(uint32_t id=1;id<=total;id++) {
        const char *name=members_name(id);
        if(name && !strcmp(name,login)) { target=id;break; }
    }
    if(members_broken())return MEMBER_CORE_STORAGE;
    if(moderation_broken())return MEMBER_CORE_MODERATION_STORAGE;
    if(!target)return MEMBER_CORE_NOT_FOUND;
    if(target==actor->user || moderation_protected(target))return MEMBER_CORE_PROTECTED;
    if((uint64_t)moderation_count()+extras_audit_count()>=UINT32_MAX)
        return MEMBER_CORE_MODERATION_UNCERTAIN;
    if(!moderation_chat_kick(target,actor->user))return MEMBER_CORE_MODERATION_UNCERTAIN;
    out->user=target;memcpy(out->name,login,length+1);
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_chat_preferences(const MemberActor *actor,ChatPreferenceAction action,const char *login,ChatPreferences *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    if(action<CHAT_PREF_QUERY || action>CHAT_PREF_UNMUTE)return MEMBER_CORE_INVALID;
    if(action==CHAT_PREF_MUTE || action==CHAT_PREF_UNMUTE) {
        if(!login)return MEMBER_CORE_INVALID;
        size_t length=0;while(length<=32 && login[length])length++;
        if(!length || length>32)return MEMBER_CORE_INVALID;
    } else if(login)return MEMBER_CORE_INVALID;
    if(action!=CHAT_PREF_QUERY) {
        int ok=members_chat_update(actor->user,action,login);
        if(ok<0)return MEMBER_CORE_STORAGE;
        if(!ok)return MEMBER_CORE_INVALID;
    }
    members_chat_preferences(actor->user,out);
    result=members_broken()?MEMBER_CORE_STORAGE:
           moderation_broken()?MEMBER_CORE_MODERATION_STORAGE:MEMBER_CORE_OK;
    if(result!=MEMBER_CORE_OK)memset(out,0,sizeof(*out));
    return result;
}

static MemberOutcome authorize_profile(const MemberActor *actor) {
    MemberOutcome result=authorize(actor,0);
    if(result!=MEMBER_CORE_OK)return result;
    if(!actor || !actor->user)return MEMBER_CORE_LOGIN_REQUIRED;
    if(extras_broken() || profile_store_broken())return MEMBER_CORE_EXTRAS_STORAGE;
    return MEMBER_CORE_OK;
}

MemberOutcome member_core_profile_set(const MemberActor *actor,unsigned field,const char *text) {
    MemberOutcome result=authorize_profile(actor);
    if(result!=MEMBER_CORE_OK)return result;
    if(!text || field>PROFILE_BUFFER)return MEMBER_CORE_PROFILE_TEXT;
    if(members_rtbbs_policy && !moderation_admin(actor->user) && (field==8 || field==11))
        return MEMBER_CORE_PROFILE_ADMINISTRATOR;
    int saved=profile_set(actor->user,field,text);
    return saved==1?MEMBER_CORE_OK:saved<0?MEMBER_CORE_PROFILE_TEXT:MEMBER_CORE_EXTRAS_STORAGE;
}

MemberOutcome member_core_profile_visibility(const MemberActor *actor,unsigned field,unsigned mode) {
    MemberOutcome result=authorize_profile(actor);
    if(result!=MEMBER_CORE_OK)return result;
    if(field>=PROFILE_FIELDS || mode>2 || (field==0 && mode!=1) || (mode==2 && field!=8))
        return MEMBER_CORE_PROFILE_TEXT;
    int saved=profile_visibility(actor->user,field,mode);
    return saved==1?MEMBER_CORE_OK:saved<0?MEMBER_CORE_PROFILE_TEXT:MEMBER_CORE_EXTRAS_STORAGE;
}

static MemberOutcome profile_health(void) {
    if(members_broken())return MEMBER_CORE_STORAGE;
    if(moderation_broken())return MEMBER_CORE_MODERATION_STORAGE;
    if(extras_broken() || profile_store_broken())return MEMBER_CORE_EXTRAS_STORAGE;
    return MEMBER_CORE_OK;
}

static int profile_copy(char *out,size_t size,const char *value) {
    if(!value)return 0;
    size_t n=0;while(n<size && value[n])n++;
    if(n==size)return 0;
    memcpy(out,value,n+1);return 1;
}

const char *member_profile_value(const MemberProfile *profile,unsigned field) {
    if(!profile || field>=PROFILE_FIELDS)return "";
    return field==0?profile->handle:field==1?profile->bio:profile->fields[field-2];
}

static MemberOutcome profile_snapshot(uint32_t target,uint32_t viewer,int review,MemberProfile *out) {
    MemberOutcome result=MEMBER_CORE_OK;
    const char *name=members_name(target);
    if(!profile_copy(out->name,sizeof(out->name),name))result=MEMBER_CORE_STORAGE;
    for(unsigned field=0;result==MEMBER_CORE_OK && field<PROFILE_FIELDS;field++) {
        char scratch[16];const char *value=review?profile_value(target,field):profile_visible(target,viewer,field,scratch);
        char *dest=field==0?out->handle:field==1?out->bio:out->fields[field-2];
        size_t size=field==0?sizeof(out->handle):field==1?sizeof(out->bio):sizeof(out->fields[0]);
        if(!profile_copy(dest,size,value))result=MEMBER_CORE_EXTRAS_STORAGE;
    }
    if(result==MEMBER_CORE_OK)result=profile_health();
    if(result!=MEMBER_CORE_OK)memset(out,0,sizeof(*out));
    else out->user=target;
    return result;
}

MemberOutcome member_core_profile_get(const MemberActor *actor,uint32_t target,const char *login,MemberProfile *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize_profile(actor);
    if(result!=MEMBER_CORE_OK)return result;
    if(login) {
        size_t n=0;while(n<=32 && login[n])n++;
        if(target || !n || n>32)return MEMBER_CORE_PROFILE_UNAVAILABLE;
        target=members_find(login);
    } else if(!target)target=actor->user;
    if(!members_name(target) || moderation_suspended(target))return MEMBER_CORE_PROFILE_UNAVAILABLE;
    return profile_snapshot(target,actor->user,0,out);
}

MemberOutcome member_core_profile_search(const MemberActor *actor,const MemberProfileQuery *query,MemberProfilePage *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize_profile(actor);
    if(result!=MEMBER_CORE_OK)return result;
    if(!query || query->scope<MEMBER_PROFILE_ALL || query->scope>MEMBER_PROFILE_GROUP ||
       !query->limit || query->limit>8 || (query->scope==MEMBER_PROFILE_FIELD && query->field>=PROFILE_FIELDS) ||
       (query->scope==MEMBER_PROFILE_GROUP && (!query->group || query->group>100)))return MEMBER_CORE_PROFILE_TEXT;
    if(members_rtbbs_policy && !moderation_admin(actor->user)) {
        if(query->scope==MEMBER_PROFILE_FIELD && query->field>=9)return MEMBER_CORE_PROFILE_SEARCH_DENIED;
        if(query->scope==MEMBER_PROFILE_GROUP) {
            int sigop=0;for(uint32_t b=1;b<=CATALOG_MAX;b++)if(catalog_sigop(b,actor->user)) { sigop=1;break; }
            if(!sigop)return MEMBER_CORE_PROFILE_SEARCH_DENIED;
        }
    }
    if(!query->text)return MEMBER_CORE_PROFILE_TEXT;
    size_t n=0;while(n<=80 && query->text[n]) {
        unsigned char c=(unsigned char)query->text[n++];
        if(c<32 || c==127)return MEMBER_CORE_PROFILE_TEXT;
    }
    if(!n || n>80)return MEMBER_CORE_PROFILE_TEXT;
    uint32_t total=members_count();if(total>MEMBER_MAX)return MEMBER_CORE_STORAGE;
    uint32_t id=query->start?query->start:1;
    for(;id<=total && out->count<query->limit;id++) {
        const char *name=members_name(id);if(!name || moderation_suspended(id))continue;
        if(query->scope==MEMBER_PROFILE_GROUP) { if(!members_in_group(id,query->group))continue; }
        else if(query->scope==MEMBER_PROFILE_FIELD) { if(!profile_match(id,actor->user,(int)query->field,query->text))continue; }
        else if(strcmp(query->text,"*") && !strstr(name,query->text) && !profile_match(id,actor->user,-1,query->text))continue;
        MemberProfileRow *row=&out->rows[out->count];char scratch[16];
        if(!profile_copy(row->name,sizeof(row->name),name) ||
           !profile_copy(row->handle,sizeof(row->handle),profile_visible(id,actor->user,0,scratch))) {
            result=MEMBER_CORE_EXTRAS_STORAGE;break;
        }
        row->user=id;out->count++;
    }
    if(result==MEMBER_CORE_OK)result=profile_health();
    if(result!=MEMBER_CORE_OK)memset(out,0,sizeof(*out));
    else out->next=id<=total?id:0;
    return result;
}

MemberOutcome member_core_profile_admin_set(const MemberActor *actor,uint32_t target,unsigned field,const char *text) {
    MemberOutcome result=authorize_profile(actor);
    if(result!=MEMBER_CORE_OK)return result;
    if(!moderation_admin(actor->user) || !members_active(target) || field>=PROFILE_FIELDS || !text)
        return MEMBER_CORE_PROFILE_EDIT;
    int saved=profile_admin_set(actor->user,target,field,text);
    return saved==1?MEMBER_CORE_OK:saved<0?MEMBER_CORE_PROFILE_EDIT:MEMBER_CORE_EXTRAS_STORAGE;
}

MemberOutcome member_core_application_get(const MemberActor *actor,uint32_t target,MemberApplication *out) {
    if(!out)return MEMBER_CORE_INVALID;
    memset(out,0,sizeof(*out));
    MemberOutcome result=authorize_member_admin(actor);
    if(result!=MEMBER_CORE_OK)return result;
    result=profile_health();if(result!=MEMBER_CORE_OK)return result;
    if(!target || target>MEMBER_MAX || !members_pending(target))return MEMBER_CORE_APPLICATION_INVALID;
    result=profile_snapshot(target,actor->user,1,&out->profile);
    if(result==MEMBER_CORE_OK && !application_repository_load(target,&out->notes))result=MEMBER_CORE_SETTINGS_STORAGE;
    if(result==MEMBER_CORE_OK)result=profile_health();
    if(result!=MEMBER_CORE_OK)memset(out,0,sizeof(*out));
    return result;
}

MemberOutcome member_core_application_set(const MemberActor *actor,uint32_t target,ApplicationNoteField field,const char *value) {
    MemberOutcome result=authorize_member_admin(actor);
    if(result!=MEMBER_CORE_OK)return result;
    result=profile_health();if(result!=MEMBER_CORE_OK)return result;
    if(!target || target>MEMBER_MAX || !members_pending(target) || field<APPLICATION_FROM_BBS || field>APPLICATION_SYSOP_NOTE || !value)
        return MEMBER_CORE_APPLICATION_INVALID;
    size_t n=0;while(n<=320 && value[n])n++;
    if(n>320)return MEMBER_CORE_APPLICATION_INVALID;
    return application_repository_save(target,field,value)?MEMBER_CORE_OK:MEMBER_CORE_SETTINGS_STORAGE;
}

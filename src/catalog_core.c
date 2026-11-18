#include "catalog_core.h"
#include "members.h"
#include "moderation.h"
#include "board_policy.h"
#include <string.h>

static CatalogOutcome authorize(const CatalogActor *actor) {
    if(members_broken())return CATALOG_CORE_MEMBER_STORAGE;
    if(moderation_broken())return CATALOG_CORE_MODERATION_STORAGE;
    if(policy_broken())return CATALOG_CORE_POLICY_STORAGE;
    if(catalog_storage_broken())return CATALOG_CORE_STORAGE;
    if(!actor || !actor->user)return CATALOG_CORE_LOGIN_REQUIRED;
    if(!members_name(actor->user) || moderation_suspended(actor->user) ||
       moderation_epoch(actor->user)!=actor->epoch)return CATALOG_CORE_REVOKED;
    return CATALOG_CORE_OK;
}
CatalogOutcome catalog_core_manage(const CatalogActor *actor,uint32_t board) {
    CatalogOutcome result=authorize(actor);
    if(result!=CATALOG_CORE_OK)return result;
    return catalog_manage(board,actor->user)?CATALOG_CORE_OK:CATALOG_CORE_DENIED;
}
CatalogOutcome catalog_core_info(const CatalogActor *actor,uint32_t board,CatalogConfig *out) {
    if(!out)return CATALOG_CORE_INVALID;
    memset(out,0,sizeof(*out));
    CatalogOutcome result=catalog_core_manage(actor,board);if(result!=CATALOG_CORE_OK)return result;
    if(!catalog_config(board,out)) { memset(out,0,sizeof(*out));return CATALOG_CORE_STORAGE; }
    return CATALOG_CORE_OK;
}
CatalogOutcome catalog_core_update(const CatalogActor *actor,const char *specification,uint32_t *out_id) {
    if(!out_id)return CATALOG_CORE_INVALID;
    *out_id=0;
    CatalogOutcome result=authorize(actor);if(result!=CATALOG_CORE_OK)return result;
    if(!specification)return CATALOG_CORE_INVALID;
    size_t n=0;while(n<1024 && specification[n])n++;
    if(!n || n>=1024)return CATALOG_CORE_INVALID;
    CatalogConfig config;
    if(!catalog_decode(specification,&config))return CATALOG_CORE_INVALID;
    if(!moderation_admin(actor->user)) {
        int exists=catalog_exists(config.id);
        if((!exists || catalog_parent(config.id)!=config.parent) &&
           !catalog_sigop(config.parent,actor->user))return CATALOG_CORE_INVALID;
        if(exists && !catalog_sigop(config.id,actor->user))return CATALOG_CORE_INVALID;
    }
    uint32_t id=0;int ok=catalog_set(actor->user,specification,&id);
    if(ok<0)return CATALOG_CORE_STORAGE;
    if(!ok)return CATALOG_CORE_INVALID;
    *out_id=id;return CATALOG_CORE_OK;
}
CatalogOutcome catalog_core_roles(const CatalogActor *actor,CatalogRoles *out) {
    if(!out)return CATALOG_CORE_INVALID;
    memset(out,0,sizeof(*out));
    CatalogOutcome result=authorize(actor);if(result!=CATALOG_CORE_OK)return result;
    for(uint32_t id=1;id<=CATALOG_MAX;id++)if(catalog_manage(id,actor->user)) {
        uint32_t user=0;
        if(!catalog_role_get(id,&user)) { memset(out,0,sizeof(*out));return CATALOG_CORE_STORAGE; }
        out->rows[out->count++]=(CatalogRole){id,user};
    }
    return CATALOG_CORE_OK;
}
CatalogOutcome catalog_core_history(const CatalogActor *actor,uint32_t current_board,uint32_t cursor,CatalogHistory *out) {
    if(!out)return CATALOG_CORE_INVALID;
    memset(out,0,sizeof(*out));
    CatalogOutcome result=authorize(actor);if(result!=CATALOG_CORE_OK)return result;
    if(!moderation_admin(actor->user) && !catalog_sigop(current_board,actor->user))return CATALOG_CORE_DENIED;
    uint32_t count=catalog_change_count(),index=cursor?cursor-1:count;
    if(index>count)return CATALOG_CORE_CURSOR;
    unsigned scanned=0;
    while(index && out->count<8 && scanned++<256) {
        CatalogChange change;
        if(!catalog_change_get(index,&change)) {
            memset(out,0,sizeof(*out));return CATALOG_CORE_STORAGE;
        }
        if(catalog_manage(change.board,actor->user))out->rows[out->count++]=change;
        index--;
    }
    out->next=index?index+1:0;return CATALOG_CORE_OK;
}

#include "catalog_group_core.h"
#include "catalog_group_repository.h"
#include "members.h"
#include "app_limits.h"
#include "moderation.h"
#include <string.h>
static CatalogGroupOutcome authorize(const CatalogActor *actor) {
    switch(catalog_core_admin(actor)) {
    case CATALOG_CORE_OK:break;
    case CATALOG_CORE_LOGIN_REQUIRED:case CATALOG_CORE_DENIED:return GROUP_CORE_ADMIN_REQUIRED;
    case CATALOG_CORE_REVOKED:return GROUP_CORE_REVOKED;
    case CATALOG_CORE_MEMBER_STORAGE:return GROUP_CORE_MEMBER_STORAGE;
    case CATALOG_CORE_MODERATION_STORAGE:return GROUP_CORE_MODERATION_STORAGE;
    case CATALOG_CORE_POLICY_STORAGE:return GROUP_CORE_POLICY_STORAGE;
    case CATALOG_CORE_STORAGE:return GROUP_CORE_CATALOG_STORAGE;
    case CATALOG_CORE_INVALID:case CATALOG_CORE_CURSOR:case CATALOG_CORE_NOT_EMPTY:
    case CATALOG_CORE_INFO_LIMIT:case CATALOG_CORE_BUFFER:return GROUP_CORE_INVALID;
    }
    return catalog_group_repository_ready()?GROUP_CORE_OK:GROUP_CORE_SETTINGS_STORAGE;
}
CatalogGroupOutcome catalog_group_delegate(const CatalogActor *actor,uint32_t board,unsigned group) {
    CatalogGroupOutcome result=authorize(actor);if(result!=GROUP_CORE_OK)return result;
    if(!catalog_exists(board) || group>100 || (group && !catalog_group(board,group)))return GROUP_CORE_INVALID;
    return catalog_group_repository_delegate(actor->user,board,group)?GROUP_CORE_OK:GROUP_CORE_SETTINGS_STORAGE;
}
CatalogGroupOutcome catalog_group_move(const CatalogActor *actor,uint32_t board,unsigned from,unsigned to,CatalogGroupChange *out) {
    if(!out)return GROUP_CORE_INVALID;
    memset(out,0,sizeof(*out));
    CatalogGroupOutcome result=authorize(actor);if(result!=GROUP_CORE_OK)return result;
    if(!catalog_exists(board) || !from || from>100 || !to || to>100 || from==to || !catalog_group(board,from))return GROUP_CORE_INVALID;
    uint32_t total=members_count();if(total>MEMBER_MAX)return GROUP_CORE_MEMBER_STORAGE;
    if(!catalog_group_repository_move(actor->user,board,from,to))return GROUP_CORE_SETTINGS_STORAGE;
    /* Preserve BOARDGROUP's inclusive slot invalidation, including vacant IDs.
     * Unlike GROUPMOVE's named-account loop this intentionally visits all slots. */
    for(uint32_t user=1;user<=total;user++)moderation_credentials_changed(user);
    out->through=total;out->actor_epoch=moderation_epoch(actor->user);return GROUP_CORE_OK;
}

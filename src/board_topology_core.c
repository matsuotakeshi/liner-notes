#include "board_topology_core.h"
#include "board_topology_repository.h"
#include "board_policy_repository.h"
#include "board_policy.h"
#include "extras.h"

static BoardTopologyOutcome authority(CatalogOutcome result) {
    switch(result) {
    case CATALOG_CORE_OK:return TOPOLOGY_OK;
    case CATALOG_CORE_LOGIN_REQUIRED:case CATALOG_CORE_DENIED:return TOPOLOGY_DENIED;
    case CATALOG_CORE_REVOKED:return TOPOLOGY_REVOKED;
    case CATALOG_CORE_MEMBER_STORAGE:return TOPOLOGY_MEMBER_STORAGE;
    case CATALOG_CORE_MODERATION_STORAGE:return TOPOLOGY_MODERATION_STORAGE;
    case CATALOG_CORE_POLICY_STORAGE:return TOPOLOGY_POLICY_STORAGE;
    case CATALOG_CORE_STORAGE:return TOPOLOGY_CATALOG_STORAGE;
    case CATALOG_CORE_INVALID:case CATALOG_CORE_CURSOR:case CATALOG_CORE_NOT_EMPTY:
    case CATALOG_CORE_INFO_LIMIT:case CATALOG_CORE_BUFFER:return TOPOLOGY_INVALID;
    }
    return TOPOLOGY_INVALID;
}
static BoardTopologyOutcome healthy(void) {
    if(board_policy_repository_broken() || policy_broken())return TOPOLOGY_POLICY_STORAGE;
    if(extras_broken())return TOPOLOGY_EXTRAS_STORAGE;
    if(!board_topology_repository_ready())return TOPOLOGY_SETTINGS_STORAGE;
    return TOPOLOGY_OK;
}
/* Legacy empty-delete/move order: managed source, format preparation, admin.
 * SIGOP never receives deletion/move authority from catalog_manage alone. */
static BoardTopologyOutcome prepare(const CatalogActor *actor,uint32_t board) {
    BoardTopologyOutcome result=authority(catalog_core_manage(actor,board));
    if(result!=TOPOLOGY_OK)return result;
    result=healthy();if(result!=TOPOLOGY_OK)return result;
    if(!board_policy_repository_prepare())return TOPOLOGY_POLICY_STORAGE;
    return authority(catalog_core_admin(actor));
}
int board_topology_capacity(uint32_t board,uint32_t extra_count,uint64_t extra_bytes) {
    if(healthy()!=TOPOLOGY_OK)return -1;
    uint64_t bytes=extra_bytes,count=extra_count;
    uint32_t through=board_topology_repository_count();
    for(uint32_t i=1;i<=through;i++) {
        BoardTopologyArticle a;
        if(!board_topology_repository_article(i,&a))return -1;
        if(a.board==board && !a.removed) {
            if(UINT64_MAX-bytes<a.bytes)return -1;
            count++;bytes+=a.bytes;
        }
    }
    return (!policy_max_articles(board) || count<=policy_max_articles(board)) &&
           (!policy_max_bytes(board) || bytes<=policy_max_bytes(board));
}
BoardTopologyOutcome board_topology_delete(const CatalogActor *actor,uint32_t board,int cascade) {
    BoardTopologyOutcome result;
    if(cascade) {
        result=authority(catalog_core_admin(actor));if(result!=TOPOLOGY_OK)return result;
        result=healthy();if(result!=TOPOLOGY_OK)return result;
        /* Preserve the cascade command's invalid-target/format result. */
        if(board==1 || !catalog_exists(board) || !board_policy_repository_prepare())return TOPOLOGY_INVALID;
        BoardDeletion plan[CATALOG_MAX];size_t count=0;
        for(uint32_t b=2;b<=CATALOG_MAX;b++)if(catalog_under(b,board))
            plan[count++]=(BoardDeletion){b,catalog_depth(b)};
        return board_topology_repository_cascade(actor->user,plan,count)?TOPOLOGY_OK:TOPOLOGY_SETTINGS_STORAGE;
    }
    result=prepare(actor,board);if(result!=TOPOLOGY_OK)return result;
    if(board==1 || board_policy_repository_has_articles(board))return TOPOLOGY_NOT_EMPTY;
    for(uint32_t b=1;b<=CATALOG_MAX;b++)if(catalog_exists(b) && catalog_parent(b)==board)return TOPOLOGY_NOT_EMPTY;
    return board_topology_repository_delete(board,actor->user)?TOPOLOGY_OK:TOPOLOGY_POLICY_STORAGE;
}
/* Parent IDs strictly precede child IDs in the existing append-only store. */
static int in_branch(uint32_t id,uint32_t root) {
    while(id>=root) {
        if(id==root)return 1;
        BoardTopologyArticle a;
        if(!board_topology_repository_article(id,&a) || a.parent>=id)return -1;
        id=a.parent;
    }
    return 0;
}
BoardTopologyOutcome board_topology_move(const CatalogActor *actor,uint32_t from,uint32_t root,
                                         uint32_t to,int branch) {
    BoardTopologyOutcome result=prepare(actor,from);if(result!=TOPOLOGY_OK)return result;
    uint32_t through=board_topology_repository_count();BoardTopologyArticle first;
    if(!root || root>through)return TOPOLOGY_MOVE_TARGET;
    if(!board_topology_repository_article(root,&first))return TOPOLOGY_POLICY_STORAGE;
    if(first.board!=from || (!branch && first.parent) || !catalog_exists(to) || policy_menu(to) || from==to)
        return TOPOLOGY_MOVE_TARGET;
    uint32_t count=0;uint64_t bytes=0;
    for(uint32_t i=root;i<=through;i++) {
        BoardTopologyArticle a;
        if(!board_topology_repository_article(i,&a))return TOPOLOGY_POLICY_STORAGE;
        int selected=branch?in_branch(i,root):a.root==root;
        if(selected<0)return TOPOLOGY_POLICY_STORAGE;
        if(selected && !a.removed) { count++;bytes+=a.bytes; }
    }
    int fits=board_topology_capacity(to,count,bytes);
    if(fits<0)return TOPOLOGY_POLICY_STORAGE;
    if(!fits)return TOPOLOGY_FULL;
    return board_topology_repository_move(actor->user,from,to,root,through,branch!=0)?
        TOPOLOGY_OK:TOPOLOGY_POLICY_STORAGE;
}

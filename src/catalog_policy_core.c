#include "catalog_policy_core.h"
#include <string.h>
static CatalogOutcome manage(const CatalogActor *actor,uint32_t board) {
    CatalogOutcome result=catalog_core_manage(actor,board);
    if(result!=CATALOG_CORE_OK)return result;
    return board_policy_repository_broken()?CATALOG_CORE_POLICY_STORAGE:CATALOG_CORE_OK;
}
static CatalogOutcome prepare(const CatalogActor *actor,uint32_t board) {
    CatalogOutcome result=manage(actor,board);
    if(result!=CATALOG_CORE_OK)return result;
    return board_policy_repository_prepare()?CATALOG_CORE_OK:CATALOG_CORE_POLICY_STORAGE;
}
CatalogOutcome catalog_policy_get(const CatalogActor *actor,uint32_t board,BoardPolicySettings *out) {
    if(!out)return CATALOG_CORE_INVALID;
    memset(out,0,sizeof(*out));
    CatalogOutcome result=manage(actor,board);if(result!=CATALOG_CORE_OK)return result;
    if(!policy_settings(board,out)) { memset(out,0,sizeof(*out));return CATALOG_CORE_POLICY_STORAGE; }
    return CATALOG_CORE_OK;
}
CatalogOutcome catalog_policy_set(const CatalogActor *actor,uint32_t board,const char *specification) {
    CatalogOutcome result=prepare(actor,board);if(result!=CATALOG_CORE_OK)return result;
    if(!specification)return CATALOG_CORE_INVALID;
    if(!strncmp(specification,"MENU ",5) && board_policy_repository_has_articles(board))return CATALOG_CORE_NOT_EMPTY;
    int ok=policy_options(board,actor->user,specification);
    if(ok<0)return CATALOG_CORE_INVALID;
    if(!ok || !board_policy_repository_prune())return CATALOG_CORE_POLICY_STORAGE;
    return CATALOG_CORE_OK;
}
CatalogOutcome catalog_password_prepare(const CatalogActor *actor,uint32_t board) {
    return manage(actor,board);
}
CatalogOutcome catalog_password_set(const CatalogActor *actor,uint32_t board,const char *password) {
    CatalogOutcome result=prepare(actor,board);if(result!=CATALOG_CORE_OK)return result;
    return password && policy_password(board,actor->user,password)?CATALOG_CORE_OK:CATALOG_CORE_POLICY_STORAGE;
}
CatalogOutcome catalog_information_get(const CatalogActor *actor,uint32_t board,char *body,size_t capacity) {
    if(body && capacity)memset(body,0,capacity);
    if(!body || !capacity)return CATALOG_CORE_BUFFER;
    CatalogOutcome result=manage(actor,board);if(result!=CATALOG_CORE_OK)return result;
    const char *text=policy_info(board);
    if(!text)return CATALOG_CORE_POLICY_STORAGE;
    size_t n=0;while(n<=BOARD_INFO_MAX && text[n])n++;
    if(n>BOARD_INFO_MAX)return CATALOG_CORE_POLICY_STORAGE;
    if(n>=capacity)return CATALOG_CORE_BUFFER;
    memcpy(body,text,n+1);return CATALOG_CORE_OK;
}
CatalogOutcome catalog_information_set(const CatalogActor *actor,uint32_t board,const char *body,int *attempted) {
    if(!attempted)return CATALOG_CORE_INVALID;
    *attempted=0;
    CatalogOutcome result=prepare(actor,board);if(result!=CATALOG_CORE_OK)return result;
    *attempted=1;
    if(!body)return CATALOG_CORE_INFO_LIMIT;
    int ok=policy_information(board,actor->user,body);
    return ok>0?CATALOG_CORE_OK:ok<0?CATALOG_CORE_INFO_LIMIT:CATALOG_CORE_POLICY_STORAGE;
}
CatalogOutcome catalog_access_page(const CatalogActor *actor,uint32_t board,uint32_t cursor,BoardAccessPage *out) {
    if(!out)return CATALOG_CORE_INVALID;
    memset(out,0,sizeof(*out));
    CatalogOutcome result=manage(actor,board);if(result!=CATALOG_CORE_OK)return result;
    uint32_t count=board_policy_repository_access_count(),oldest=count;
    uint32_t keep=policy_log_keep(board),retained=0,index=cursor?cursor-1:count;
    if(index>count)return CATALOG_CORE_CURSOR;
    BoardAccessRecord row;
    if(keep)while(oldest && retained<keep) {
        if(!board_policy_repository_access_get(--oldest,&row))goto storage;
        if(row.board==board)retained++;
    }
    unsigned scanned=0;
    while(index>oldest && out->count<8 && scanned++<256) {
        if(!board_policy_repository_access_get(--index,&row))goto storage;
        if(row.board==board)out->rows[out->count++]=row;
    }
    out->next=index>oldest?index+1:0;return CATALOG_CORE_OK;
storage:memset(out,0,sizeof(*out));return CATALOG_CORE_POLICY_STORAGE;
}

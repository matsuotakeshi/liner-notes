#define _POSIX_C_SOURCE 200809L
#include "vault_query_core.h"
#include "runtime_limits.h"
#include <string.h>

static VaultResult finish(VaultResult r,void *out,size_t size) {
    if(r!=VAULT_OK)memset(out,0,size);
    return r;
}
/* Keep operation-specific admission in vault.c, including audit's manage
 * capability. A trusted resolved Principal is not a cached board grant. */
static VaultResult access_check(Vault *v,const Principal *p) {
    return !v?VAULT_STORAGE:!p?VAULT_DENIED:VAULT_OK;
}
VaultResult vault_query_files(Vault *v,const Principal *p,const VaultQuery *q,VaultQueryFiles *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=access_check(v,p);if(r!=VAULT_OK)return r;
    if(!q)return VAULT_INVALID;
    if(q->check_scope) {
        if(!q->scope)return VAULT_INVALID;
        VaultBoard board;
        if(vault_board_get(v,p,q->scope,&board)!=VAULT_OK)return VAULT_DENIED;
    }
    if(q->news) {
        if(!q->date_valid)return VAULT_INVALID;
        r=vault_news_in(v,p,q->cursor,q->since,q->scope,out->rows,&out->count,&out->next);
        if(r==VAULT_OK && q->has_until) {
            size_t kept=0;
            for(size_t i=0;i<out->count;i++)if(out->rows[i].created<=q->until)out->rows[kept++]=out->rows[i];
            memset(out->rows+kept,0,(8-kept)*sizeof(out->rows[0]));out->count=kept;
        }
    } else {
        if(!q->pattern)return VAULT_INVALID;
        r=vault_search(v,p,q->cursor,q->pattern,q->scope,out->rows,&out->count,&out->next);
    }
    return finish(r,out,sizeof(*out));
}
VaultResult vault_query_match(Vault *v,const Principal *p,const char *pattern,const char *scope,VaultQueryMatch *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=access_check(v,p);if(r!=VAULT_OK)return r;
    if(!pattern)return VAULT_INVALID;
    uint32_t cursor=0;
    do {
        VaultEntry rows[8];size_t n;
        r=vault_search(v,p,cursor,pattern,scope,rows,&n,&cursor);if(r!=VAULT_OK)break;
        for(size_t i=0;i<n;i++) {
            if(scope && strcmp(rows[i].category,scope))continue;
            if(out->count>=ln_limit("BATCH_FILES",16) || rows[i].size>ln_limit("FILE",1073741824u) ||
               out->bytes+rows[i].size>ln_limit("BATCH_BYTES",VAULT_BATCH_BYTES)) { r=VAULT_LIMIT;break; }
            out->ids[out->count++]=rows[i].id;out->bytes+=rows[i].size;
        }
        if(r!=VAULT_OK)break;
    }while(cursor);
    return finish(r,out,sizeof(*out));
}
VaultResult vault_query_children(Vault *v,const Principal *p,const char *parent,uint32_t cursor,VaultQueryChildren *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=access_check(v,p);if(r!=VAULT_OK)return r;
    if(!parent || strlen(parent)>48)return VAULT_INVALID;
    size_t plen=strlen(parent);
    if(plen) {
        VaultBoard board;if(vault_board_get(v,p,parent,&board)!=VAULT_OK)return VAULT_DENIED;
    }
    do {
        VaultBoard boards[8];size_t n;
        r=vault_boards(v,p,cursor,boards,&n,&cursor);if(r!=VAULT_OK)break;
        for(size_t i=0;i<n;i++) {
            const char *name=boards[i].name;
            if(!*name || (plen && (strncmp(name,parent,plen) || name[plen]!='/')))continue;
            const char *child=name+(plen?plen+1:0);if(strchr(child,'/'))continue;
            VaultQueryChild *row=&out->rows[out->count++];
            strcpy(row->name,name);strcpy(row->display,*boards[i].display?boards[i].display:child);
        }
    }while(cursor && out->count<8);
    out->next=cursor;return finish(r,out,sizeof(*out));
}
VaultResult vault_query_detail(Vault *v,const Principal *p,uint32_t id,VaultQueryDetail *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=access_check(v,p);if(r!=VAULT_OK)return r;
    r=vault_info(v,p,id,&out->entry);
    if(r==VAULT_OK && vault_protocol(v,p,id,out->protocol)!=VAULT_OK)r=VAULT_STORAGE;
    return finish(r,out,sizeof(*out));
}
VaultResult vault_query_boards(Vault *v,const Principal *p,uint32_t cursor,VaultQueryBoards *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=access_check(v,p);if(r!=VAULT_OK)return r;
    return finish(vault_boards(v,p,cursor,out->rows,&out->count,&out->next),out,sizeof(*out));
}
VaultResult vault_query_categories(Vault *v,const Principal *p,uint32_t cursor,VaultQueryCategories *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=access_check(v,p);if(r!=VAULT_OK)return r;
    return finish(vault_categories(v,p,cursor,out->rows,&out->count,&out->next),out,sizeof(*out));
}
VaultResult vault_query_audit(Vault *v,const Principal *p,uint32_t cursor,VaultQueryAudit *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=access_check(v,p);if(r!=VAULT_OK)return r;
    return finish(vault_audit(v,p,cursor,out->rows,&out->count,&out->next),out,sizeof(*out));
}

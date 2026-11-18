#include "vault_management_core.h"
#include "download.h"
#include <string.h>

static VaultResult admission(Vault *v,const Principal *p) {
    return !v?VAULT_STORAGE:!p?VAULT_DENIED:VAULT_OK;
}
static VaultResult finish(VaultResult r,void *out,size_t size) {
    if(r!=VAULT_OK)memset(out,0,size);
    return r;
}
VaultResult vault_manage_file(Vault *v,const Principal *p,uint32_t id,const VaultFileChange *change) {
    VaultResult r=admission(v,p);if(r!=VAULT_OK)return r;
    if(!change)return VAULT_INVALID;
    switch(change->kind) {
    case VAULT_FILE_DESCRIPTION:return change->text?vault_description(v,p,id,change->text):VAULT_INVALID;
    case VAULT_FILE_RENAME:return change->text?vault_rename(v,p,id,change->text):VAULT_INVALID;
    case VAULT_FILE_OWNER:return change->text?vault_owner(v,p,id,change->text):VAULT_INVALID;
    case VAULT_FILE_ASCII:return vault_ascii(v,p,id,change->ascii);
    case VAULT_FILE_CATEGORY:return change->text?vault_category(v,p,id,change->text):VAULT_INVALID;
    case VAULT_FILE_HIDE:return vault_hide(v,p,id);
    case VAULT_FILE_RESTORE:return vault_restore(v,p,id);
    case VAULT_FILE_DELETE:return vault_reclaim(v,p,id);
    }
    return VAULT_INVALID;
}
VaultResult vault_manage_copy(Vault *v,const Principal *p,uint32_t id,const char *destination,VaultEntry *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=admission(v,p);if(r!=VAULT_OK)return r;
    if(!destination)return VAULT_INVALID;
    return finish(vault_copy(v,p,id,destination,out),out,sizeof(*out));
}
VaultResult vault_manage_policy(Vault *v,const Principal *p,const char *name,const char *read,const char *upload) {
    VaultResult r=admission(v,p);if(r!=VAULT_OK)return r;
    if(!name || !read || !upload)return VAULT_INVALID;
    return vault_board_set(v,p,name,read,upload);
}
VaultBoardUpdate vault_manage_board(Vault *v,const Principal *p,const char *name,VaultBoardField field,const char *value) {
    VaultBoardUpdate out={admission(v,p),0};if(out.result!=VAULT_OK)return out;
    if(!name || !value) { out.result=VAULT_INVALID;return out; }
    char kind;
    switch(field) {
    case VAULT_BOARD_NAME:kind='n';break;
    case VAULT_BOARD_GUIDE:kind='i';break;
    case VAULT_BOARD_SIGOP:kind='o';break;
    case VAULT_BOARD_OPTIONS:kind='v';break;
    default:out.result=VAULT_INVALID;return out;
    }
    out.result=vault_board_metadata(v,p,name,kind,value);
    if(out.result==VAULT_OK && field==VAULT_BOARD_OPTIONS && !downloads_prune(v)) {
        out.result=VAULT_STORAGE;out.retention_failed=1;
    }
    return out;
}
VaultResult vault_manage_inspect(Vault *v,const Principal *p,const char *name,int for_edit,VaultBoard *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=admission(v,p);if(r!=VAULT_OK)return r;
    if(!name)return VAULT_INVALID;
    r=vault_board_get(v,p,name,out);
    if(r==VAULT_OK && for_edit && !out->can_manage)r=VAULT_DENIED;
    return finish(r,out,sizeof(*out));
}
VaultResult vault_manage_password(Vault *v,const Principal *p,const char *name,const char *password) {
    VaultResult r=admission(v,p);if(r!=VAULT_OK)return r;
    if(!name || !password)return VAULT_INVALID;
    return vault_password(v,p,name,password);
}
VaultResult vault_manage_unlock(Vault *v,const Principal *p,const char *name,const char *password,VaultBoardGrant *out) {
    if(!out)return VAULT_INVALID;
    memset(out,0,sizeof(*out));
    VaultResult r=admission(v,p);if(r!=VAULT_OK)return r;
    if(!name || !password)return VAULT_INVALID;
    return finish(vault_unlock(v,p,name,password,&out->board,&out->revision),out,sizeof(*out));
}

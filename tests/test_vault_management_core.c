#include "vault_management_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Vault *v=(Vault *)1;
/* Intentionally not global admin: Core must use per-operation repository ACLs. */
static Principal actor={.subject="member:2",.capabilities=CAP_VAULT_READ|CAP_VAULT_UPLOAD};
static VaultResult result=VAULT_OK;
static unsigned calls,prunes;static int prune_ok=1,can_manage;
static char operation,metadata;static uint32_t file;
static const char *value,*path,*read_acl,*upload_acl;
static int ascii_value;
static void zero(const void *data,size_t n) { const unsigned char *p=data;for(size_t i=0;i<n;i++)assert(!p[i]); }
static void reset(void) { result=VAULT_OK;calls=prunes=0;prune_ok=1;can_manage=0;operation=metadata=0;file=0;value=path=read_acl=upload_acl=NULL; }
static VaultResult record(Vault *vault,const Principal *p,char op,uint32_t id,const char *text) {
    assert(vault==v && p==&actor);calls++;operation=op;file=id;value=text;return result;
}
VaultResult vault_description(Vault *v,const Principal *p,uint32_t id,const char *s) { return record(v,p,'E',id,s); }
VaultResult vault_rename(Vault *v,const Principal *p,uint32_t id,const char *s) { return record(v,p,'N',id,s); }
VaultResult vault_owner(Vault *v,const Principal *p,uint32_t id,const char *s) { return record(v,p,'O',id,s); }
VaultResult vault_ascii(Vault *v,const Principal *p,uint32_t id,int on) { ascii_value=on;return record(v,p,'T',id,NULL); }
VaultResult vault_category(Vault *v,const Principal *p,uint32_t id,const char *s) { return record(v,p,'C',id,s); }
VaultResult vault_hide(Vault *v,const Principal *p,uint32_t id) { return record(v,p,'H',id,NULL); }
VaultResult vault_restore(Vault *v,const Principal *p,uint32_t id) { return record(v,p,'S',id,NULL); }
VaultResult vault_reclaim(Vault *v,const Principal *p,uint32_t id) { return record(v,p,'F',id,NULL); }
VaultResult vault_copy(Vault *v,const Principal *p,uint32_t id,const char *dest,VaultEntry *out) {
    out->id=50;out->size=7;strcpy(out->publisher,"member:3");strcpy(out->description,"preserved");
    path=dest;return record(v,p,'A',id,NULL);
}
VaultResult vault_board_set(Vault *v,const Principal *p,const char *name,const char *read,const char *upload) {
    path=name;read_acl=read;upload_acl=upload;return record(v,p,'B',0,NULL);
}
VaultResult vault_board_metadata(Vault *v,const Principal *p,const char *name,char kind,const char *s) {
    path=name;metadata=kind;return record(v,p,'M',0,s);
}
int downloads_prune(Vault *vault) { assert(vault==v && calls==1 && operation=='M' && metadata=='v' && result==VAULT_OK);prunes++;return prune_ok; }
VaultResult vault_board_get(Vault *v,const Principal *p,const char *name,VaultBoard *out) {
    path=name;strcpy(out->name,name);strcpy(out->info,"private edit buffer");out->can_manage=can_manage;
    return record(v,p,'G',0,NULL);
}
VaultResult vault_password(Vault *v,const Principal *p,const char *name,const char *secret) {
    path=name;return record(v,p,'P',0,secret);
}
VaultResult vault_unlock(Vault *v,const Principal *p,const char *name,const char *secret,uint32_t *id,uint32_t *revision) {
    *id=4;*revision=7;path=name;return record(v,p,'U',0,secret);
}
static void file_changes(void) {
    const char expected[]="ENOTCHSF";
    for(int i=0;i<8;i++) {
        reset();VaultFileChange c={.kind=(VaultFileChangeKind)i,.text="field",.ascii=-1};
        assert(vault_manage_file(v,&actor,17,&c)==VAULT_OK && calls==1 && file==17 && operation==expected[i]);
        if(i==3)assert(ascii_value==-1); /* preserve repository syntax/ACL order */
        else if(i<5)assert(!strcmp(value,"field"));
        for(VaultResult r=VAULT_DENIED;r<=VAULT_LIMIT;r++) {
            result=r;assert(vault_manage_file(v,&actor,17,&c)==r);
        }
    }
    reset();VaultFileChange bad={.kind=(VaultFileChangeKind)99};
    assert(vault_manage_file(v,&actor,1,&bad)==VAULT_INVALID && !calls);
    bad.kind=VAULT_FILE_DESCRIPTION;assert(vault_manage_file(v,&actor,1,&bad)==VAULT_INVALID && !calls);
    bad.text="";assert(vault_manage_file(v,&actor,1,&bad)==VAULT_OK && !*value);
}
static void copy_result(void) {
    reset();VaultEntry out;
    assert(vault_manage_copy(v,&actor,12,"",&out)==VAULT_OK && file==12 && !*path && out.id==50 && out.size==7);
    assert(!strcmp(out.publisher,"member:3") && !strcmp(out.description,"preserved"));
    for(VaultResult r=VAULT_DENIED;r<=VAULT_LIMIT;r++) { result=r;assert(vault_manage_copy(v,&actor,12,"to",&out)==r);zero(&out,sizeof(out)); }
}
static void policies(void) {
    reset();assert(vault_manage_policy(v,&actor,"parent/child","l2+g3","member")==VAULT_OK);
    assert(!strcmp(path,"parent/child") && !strcmp(read_acl,"l2+g3") && !strcmp(upload_acl,"member"));
    result=VAULT_DENIED;assert(vault_manage_policy(v,&actor,"","member","none")==VAULT_DENIED);
    const char kinds[]="niov";
    for(int i=0;i<4;i++) {
        reset();VaultBoardUpdate r=vault_manage_board(v,&actor,"parent",(VaultBoardField)i,"value");
        assert(r.result==VAULT_OK && !r.retention_failed && metadata==kinds[i] && !strcmp(value,"value"));
        assert(prunes==(i==3));
    }
    reset();assert(vault_manage_board(v,&actor,"",(VaultBoardField)99,"value").result==VAULT_INVALID && !calls);
}
static void retention(void) {
    reset();result=VAULT_DENIED;VaultBoardUpdate r=vault_manage_board(v,&actor,"pub",VAULT_BOARD_OPTIONS,"BOARD 0 2");
    assert(r.result==VAULT_DENIED && !r.retention_failed && !prunes);
    reset();prune_ok=0;r=vault_manage_board(v,&actor,"pub",VAULT_BOARD_OPTIONS,"BOARD 0 2");
    assert(r.result==VAULT_STORAGE && r.retention_failed && calls==1 && prunes==1);
    /* Saved metadata is not rolled back or retried after retention failure. */
    assert(metadata=='v' && !strcmp(value,"BOARD 0 2"));
    reset();result=VAULT_STORAGE;r=vault_manage_board(v,&actor,"pub",VAULT_BOARD_OPTIONS,"BOARD 0 2");
    assert(r.result==VAULT_STORAGE && !r.retention_failed && !prunes);
}
static void inspection_and_grants(void) {
    reset();VaultBoard board;VaultBoardGrant grant;
    assert(vault_manage_inspect(v,&actor,"pub",0,&board)==VAULT_OK && *board.info);
    assert(vault_manage_inspect(v,&actor,"pub",1,&board)==VAULT_DENIED);zero(&board,sizeof(board));
    can_manage=1;assert(vault_manage_inspect(v,&actor,"pub",1,&board)==VAULT_OK && board.can_manage);
    result=VAULT_STORAGE;assert(vault_manage_inspect(v,&actor,"pub",1,&board)==VAULT_STORAGE);zero(&board,sizeof(board));
    reset();assert(vault_manage_password(v,&actor,"pub","test-secret")==VAULT_OK && !strcmp(value,"test-secret"));
    assert(vault_manage_password(v,&actor,"pub","")==VAULT_OK && !*value);
    assert(vault_manage_unlock(v,&actor,"pub","test-secret",&grant)==VAULT_OK && grant.board==4 && grant.revision==7);
    for(VaultResult r=VAULT_DENIED;r<=VAULT_LIMIT;r++) { result=r;assert(vault_manage_unlock(v,&actor,"pub","bad",&grant)==r);zero(&grant,sizeof(grant)); }
}
static void invalid_context(void) {
    reset();VaultEntry e;VaultBoard b;VaultBoardGrant g;VaultFileChange c={.kind=VAULT_FILE_HIDE};
    assert(vault_manage_file(v,NULL,1,&c)==VAULT_DENIED);
    assert(vault_manage_file(NULL,&actor,1,&c)==VAULT_STORAGE);
    assert(vault_manage_copy(v,NULL,1,"",&e)==VAULT_DENIED);zero(&e,sizeof(e));
    assert(vault_manage_copy(v,&actor,1,NULL,&e)==VAULT_INVALID);zero(&e,sizeof(e));
    assert(vault_manage_policy(v,NULL,"","member","member")==VAULT_DENIED);
    assert(vault_manage_board(v,NULL,"",VAULT_BOARD_GUIDE,"").result==VAULT_DENIED);
    assert(vault_manage_inspect(v,NULL,"",0,&b)==VAULT_DENIED);zero(&b,sizeof(b));
    assert(vault_manage_password(v,NULL,"","secret")==VAULT_DENIED);
    assert(vault_manage_unlock(v,NULL,"","secret",&g)==VAULT_DENIED);zero(&g,sizeof(g));
    assert(vault_manage_file(v,&actor,1,NULL)==VAULT_INVALID);
    assert(vault_manage_inspect(v,&actor,"",0,NULL)==VAULT_INVALID);
    assert(vault_manage_unlock(v,&actor,"",NULL,&g)==VAULT_INVALID);
    assert(!calls && !prunes);
}
int main(void) {
    file_changes();copy_result();policies();retention();inspection_and_grants();invalid_context();
    puts("PASS: 6 vault-management groups (mutations, copy, policies, retention, grants/editor, admission)");return 0;
}

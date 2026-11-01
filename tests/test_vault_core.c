#include "vault.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    assert(argc==3);
    Principal foreign={"future-identity:uuid-123",CAP_VAULT_READ|CAP_VAULT_MANAGE,0,0,{0},{0}};
    Principal reader={"another-provider:9",CAP_VAULT_READ,0,0,{0},{0}},guest={0};
    assert(vault_init(argv[1])==VAULT_OK);Vault *v=NULL;assert(vault_open(argv[1],&v)==VAULT_OK);
    VaultEntry e,rows[8];size_t count;uint32_t next;
    assert(vault_import(v,&reader,argv[2],"x","",&e)==VAULT_DENIED);
    assert(vault_import(v,&foreign,argv[2],"x","standalone",&e)==VAULT_OK);
    assert(!strcmp(e.publisher,foreign.subject));
    assert(vault_list(v,&guest,0,rows,&count,&next)==VAULT_DENIED);
    assert(vault_list(v,&reader,0,rows,&count,&next)==VAULT_OK && count==1);
    VaultEvent events[4];
    assert(vault_category(v,&reader,e.id,"Docs")==VAULT_DENIED);
    assert(vault_category(v,&foreign,e.id,"Docs")==VAULT_OK);
    assert(vault_search(v,&reader,0,"STAND", "Docs",rows,&count,&next)==VAULT_OK && count==1);
    assert(vault_audit(v,&reader,0,events,&count,&next)==VAULT_DENIED);
    assert(vault_audit(v,&foreign,0,events,&count,&next)==VAULT_OK && count==2);
    assert(events[1].action=='C' && !strcmp(events[1].actor,foreign.subject) && !strcmp(events[1].after,"Docs"));
    assert(vault_hide(v,&reader,e.id)==VAULT_DENIED);
    assert(vault_hide(v,&foreign,e.id)==VAULT_OK);
    assert(vault_info(v,&reader,e.id,&e)==VAULT_NOT_FOUND);
    vault_close(v);puts("PASS: standalone vault uses namespaced principals and capabilities without BBS/account/UI linkage");return 0;
}

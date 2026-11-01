#include "vault.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
static void sha(const char *s,unsigned char out[32]) { unsigned n=0;assert(EVP_Digest(s,strlen(s),out,&n,EVP_sha256(),NULL)==1 && n==32); }
int main(int argc,char **argv) {
    assert(argc==2);Vault *v=NULL;assert(vault_init(argv[1])==VAULT_OK);assert(vault_open(argv[1],&v)==VAULT_OK);
    Principal owner={"future-account:alice",CAP_VAULT_READ|CAP_VAULT_UPLOAD,0,0,{0},{0}};
    Principal other={"future-account:bob",CAP_VAULT_READ|CAP_VAULT_UPLOAD,0,0,{0},{0}};
    Principal admin={"local-admin:1",CAP_VAULT_MANAGE,0,0,{0},{0}},revoked=owner;revoked.capabilities=CAP_VAULT_READ;
    unsigned char abc[32],empty[32];sha("abc",abc);sha("",empty);
    uint32_t first,second,id,next;VaultEntry e,rows[8];size_t count;VaultUpload state;
    assert(vault_upload_start(v,&admin,3,abc,"x","",&id)==VAULT_DENIED);
    assert(vault_upload_start(v,&owner,3,abc,"x","description",&first)==VAULT_OK);
    assert(vault_upload_start(v,&owner,0,empty,"zero","",&second)==VAULT_OK);
    assert(vault_upload_start(v,&owner,0,empty,"third","",&id)==VAULT_LIMIT);
    assert(vault_list(v,&owner,0,rows,&count,&next)==VAULT_OK && count==0);
    assert(vault_upload_write(v,&other,first,0,(const unsigned char *)"abc",3,&next)==VAULT_DENIED);
    assert(vault_upload_status(v,&other,first,&state)==VAULT_DENIED);
    assert(vault_upload_cancel(v,&other,first)==VAULT_DENIED);
    assert(vault_upload_finish(v,&other,first,&e)==VAULT_DENIED);
    assert(vault_upload_write(v,&owner,first,2,(const unsigned char *)"c",1,&next)==VAULT_INVALID);
    assert(vault_upload_write(v,&owner,first,0,(const unsigned char *)"ab",2,&next)==VAULT_OK && next==2);
    vault_close(v);assert(vault_open(argv[1],&v)==VAULT_OK);
    assert(vault_upload_status(v,&owner,first,&state)==VAULT_OK && state.received==2 && state.state==VAULT_UPLOAD_ACTIVE);
    assert(vault_upload_write(v,&revoked,first,0,(const unsigned char *)"abc",3,&next)==VAULT_DENIED);
    assert(vault_upload_finish(v,&revoked,first,&e)==VAULT_DENIED);
    assert(vault_upload_write(v,&owner,first,0,(const unsigned char *)"abc",3,&next)==VAULT_OK && next==3);
    assert(vault_upload_write(v,&owner,first,0,(const unsigned char *)"abd",3,&next)==VAULT_INVALID);
    assert(vault_upload_write(v,&owner,first,0,(const unsigned char *)"abc",3,&next)==VAULT_OK && next==3);
    assert(vault_upload_finish(v,&owner,second,&e)==VAULT_OK && e.id==second && e.size==0);
    assert(vault_upload_finish(v,&owner,first,&e)==VAULT_OK && e.id==first && !strcmp(e.publisher,owner.subject));
    assert(vault_upload_finish(v,&owner,first,&e)==VAULT_OK && e.id==first);
    assert(vault_upload_cancel(v,&owner,first)==VAULT_INVALID);
    assert(vault_upload_write(v,&owner,first,0,(const unsigned char *)"abc",3,&next)==VAULT_INVALID);
    assert(vault_list(v,&owner,0,rows,&count,&next)==VAULT_OK && count==2);
    Principal principals[VAULT_UPLOADS/2];uint32_t ids[VAULT_UPLOADS];
    for(int i=0;i<VAULT_UPLOADS/2;i++) {
        memset(&principals[i],0,sizeof(principals[i]));snprintf(principals[i].subject,sizeof(principals[i].subject),"provider:%d",i);principals[i].capabilities=CAP_VAULT_UPLOAD;
        for(int j=0;j<2;j++)assert(vault_upload_start(v,&principals[i],0,empty,"pending","",&ids[i*2+j])==VAULT_OK);
    }
    assert(vault_upload_start(v,&owner,0,empty,"over-limit","",&id)==VAULT_LIMIT);
    for(int i=0;i<VAULT_UPLOADS;i++)assert(vault_upload_cancel(v,&admin,ids[i])==VAULT_OK);
    assert(vault_upload_cancel(v,&admin,ids[0])==VAULT_OK);
    assert(vault_upload_status(v,&admin,ids[0],&state)==VAULT_OK && state.state==VAULT_UPLOAD_CANCELLED);
    assert(vault_upload_start(v,&owner,0,empty,"again","",&id)==VAULT_OK);
    vault_close(v);
    puts("PASS: independent upload API, owner/capability checks, resume/retry, out-of-order commits, empty file, global/per-subject limits and administrative cancellation");return 0;
}

#include "transfer_line.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Alternate in-memory backend: connector tests link without vault.c, BBS,
 * membership or filesystem implementation. */
static VaultResult fake(void *context,const Principal *p,const TransferRequest *q,TransferReply *r) {
    (*(int *)context)++;assert(!strcmp(p->subject,"other-provider:42"));
    if(q->operation==TRANSFER_START) { assert(q->size==2 && !strcmp(q->name,"x") && !*q->description);r->id=7; }
    else if(q->operation==TRANSFER_WRITE) { assert(q->id==7 && q->offset==0 && q->count==2 && q->bytes[0]==255 && q->bytes[1]==0);r->received=2; }
    else if(q->operation==TRANSFER_READ) { r->count=2;r->bytes[0]=255;r->bytes[1]=0; }
    else if(q->operation==TRANSFER_STATUS) { r->upload.state=VAULT_UPLOAD_ACTIVE;r->upload.size=2;r->upload.received=2; }
    else if(q->operation==TRANSFER_INFO || q->operation==TRANSFER_FINISH) { r->entry.id=7;r->entry.size=2; }
    return VAULT_OK;
}
int main(void) {
    int calls=0;TransferBackend backend={&calls,fake};Principal p={"other-provider:42",CAP_VAULT_UPLOAD|CAP_VAULT_READ,0,0,{0},{0}};char out[2048];
    assert(transfer_line_command("FILEGET 7 0") && !transfer_line_command("FILEGETTING"));
    transfer_line_execute(backend,&p,"FILESTART 2 0000000000000000000000000000000000000000000000000000000000000000 eA== -",out,sizeof(out));assert(!strcmp(out,"UPLOAD 7\r\n"));
    transfer_line_execute(backend,&p,"FILEPUT 7 0 /wA=",out,sizeof(out));assert(!strcmp(out,"RECEIVED 7 2\r\n"));
    transfer_line_execute(backend,&p,"FILEGET 7 0",out,sizeof(out));assert(!strcmp(out,"FILEDATA 7 0 2 /wA=\r\n"));
    transfer_line_execute(backend,&p,"FILESTATUS 7",out,sizeof(out));assert(strstr(out,"UPLOADSTATUS 7 ACTIVE 2 2 "));
    transfer_line_execute(backend,&p,"FILEMETA 7",out,sizeof(out));assert(strstr(out,"FILE 7 2 "));
    transfer_line_execute(backend,&p,"FILEFINISH 7",out,sizeof(out));assert(strstr(out,"UPLOADED 7 2 "));
    transfer_line_execute(backend,&p,"FILECANCEL 7",out,sizeof(out));assert(!strcmp(out,"UPLOAD CANCELLED 7\r\n"));
    int before=calls;
    const char *bad[]={"FILEPUT 7 0 /x==","FILEPUT 7 0 -","FILEPUT 7 0 AAA= extra","FILEGET 0 0","FILEGET 1 999999999999999999999","FILEMETA 7 ","FILESTART 2 bad eA== -","FILESTART 2 0000000000000000000000000000000000000000000000000000000000000000 AA== -"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) { transfer_line_execute(backend,&p,bad[i],out,sizeof(out));assert(!strcmp(out,"ERR vault syntax\r\n")); }
    assert(calls==before);
    puts("PASS: line connector uses replaceable backend, preserves byte payload/principal and rejects malformed frames before dispatch");return 0;
}

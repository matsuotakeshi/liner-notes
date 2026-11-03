#include "transfer.h"
#include <string.h>
static VaultResult execute(void *context,const Principal *p,const TransferRequest *q,TransferReply *r) {
    Vault *v=context;memset(r,0,sizeof(*r));
    switch(q->operation) {
    case TRANSFER_CHECK:return vault_board_check(v,p,q->category);
    case TRANSFER_INFO:return vault_info(v,p,q->id,&r->entry);
    case TRANSFER_READ:return vault_read_block(v,p,q->id,q->offset,r->bytes,q->count?q->count:VAULT_CHUNK,&r->count);
    case TRANSFER_START:return vault_upload_start_in(v,p,q->size,q->sha256,q->name,q->description,q->category,&r->id);
    case TRANSFER_WRITE:return vault_upload_write_block(v,p,q->id,q->offset,q->bytes,q->count,&r->received);
    case TRANSFER_STATUS:return vault_upload_status(v,p,q->id,&r->upload);
    case TRANSFER_FINISH:return vault_upload_finish(v,p,q->id,&r->entry);
    case TRANSFER_CANCEL:return vault_upload_cancel(v,p,q->id);
    }
    return VAULT_INVALID;
}
TransferBackend vault_transfer_backend(Vault *v) { TransferBackend b={v,execute};return b; }

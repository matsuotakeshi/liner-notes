#ifndef TRANSFER_H
#define TRANSFER_H
#include "vault.h"
/* Protocol-neutral source/sink contract. No sockets, line editor or account DB.
 * The trusted entry point resolves a fresh Principal for EACH request. Future
 * streaming connectors must do the same on chunks and final publication. */
typedef enum { TRANSFER_INFO,TRANSFER_READ,TRANSFER_START,TRANSFER_WRITE,
    TRANSFER_STATUS,TRANSFER_FINISH,TRANSFER_CANCEL,TRANSFER_CHECK } TransferOperation;
typedef struct {
    TransferOperation operation;
    uint32_t id,offset,size;
    size_t count;
    unsigned char bytes[VAULT_CHUNK],sha256[32];
    char name[128],description[256],category[49];
} TransferRequest;
typedef struct {
    VaultEntry entry;
    VaultUpload upload;
    uint32_t id,received;
    size_t count;
    unsigned char bytes[VAULT_CHUNK];
} TransferReply;
typedef struct {
    void *context;
    VaultResult (*execute)(void *,const Principal *,const TransferRequest *,TransferReply *);
} TransferBackend;
/* Adapter from the common contract to the current vault implementation. */
TransferBackend vault_transfer_backend(Vault *);
#endif

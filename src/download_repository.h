#ifndef DOWNLOAD_REPOSITORY_H
#define DOWNLOAD_REPOSITORY_H
#include "vault.h"
#include <string.h>
/* Internal persistence seam, not an authorization API. Serialized worker only. */
typedef struct { char token[33],subject[80],protocol[12];uint32_t file,size;uint64_t at; } DownloadReceipt;
static inline int download_token_valid(const char *p) {
    if(!p || strlen(p)!=32)return 0;
    for(;*p;p++)if(!(*p>='0' && *p<='9') && !(*p>='a' && *p<='f'))return 0;
    return 1;
}
int download_repository_open(const char *);
void download_repository_close(void);
int download_repository_ready(void);
size_t download_repository_count(void);
int download_repository_get(size_t,DownloadReceipt *);
/* Existing append/apply/prune sequence; failure may follow a committed append.
 * Caller must not claim rollback or clear the ticket on such a failure. */
int download_repository_commit(Vault *,const DownloadReceipt *);
#endif

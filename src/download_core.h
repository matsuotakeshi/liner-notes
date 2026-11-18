#ifndef DOWNLOAD_CORE_H
#define DOWNLOAD_CORE_H
#include "vault.h"
/* Vault-worker services. Principal must be resolved by the trusted identity
 * provider for EACH request; never accept client-supplied capability bits.
 * One worker owns live tickets and repository. Inputs must not alias outputs. */
typedef enum { DOWNLOAD_OK,DOWNLOAD_DENIED,DOWNLOAD_NOT_FOUND,DOWNLOAD_INVALID,
               DOWNLOAD_BUSY,DOWNLOAD_STORAGE,DOWNLOAD_TICKET } DownloadOutcome;
typedef enum { DOWNLOAD_LINE,DOWNLOAD_ZMODEM,DOWNLOAD_XMODEM,DOWNLOAD_YMODEM,
               DOWNLOAD_YMODEM_G,DOWNLOAD_MLINK,DOWNLOAD_TEXT,
               DOWNLOAD_XMODEM_SUM,DOWNLOAD_XMODEM_1K } DownloadProtocol;
typedef struct { char token[33];uint32_t size;unsigned char sha256[32]; } DownloadOpened;
typedef struct { uint32_t offset;size_t size;unsigned char bytes[VAULT_BLOCK]; } DownloadBlock;
/* valid=0 represents an invalid supplied digest, preserving command error order. */
typedef struct { int valid;unsigned char bytes[32]; } DownloadDigest;
typedef struct { uint32_t file; } DownloadCompleted;
typedef struct { size_t sequence;uint32_t file,size;uint64_t at;char subject[80],protocol[12]; } DownloadHistoryRow;
typedef struct { size_t count,next;DownloadHistoryRow rows[8]; } DownloadHistory;
typedef struct { uint32_t file;uint64_t count; } DownloadCount;
typedef struct { size_t count;uint32_t next;DownloadCount rows[8]; } DownloadStats;
/* Access preflight also lets the legacy parser preserve health/ACL-before-syntax
 * errors. Each operation independently checks admission; preflight is not a grant. */
DownloadOutcome download_core_access(const Principal *);
DownloadOutcome download_core_open(Vault *,const Principal *,uint32_t,DownloadProtocol,DownloadOpened *);
DownloadOutcome download_core_fetch(Vault *,const Principal *,const char *,uint32_t,size_t,DownloadBlock *);
DownloadOutcome download_core_finish(Vault *,const Principal *,const char *,const DownloadDigest *,DownloadCompleted *);
DownloadOutcome download_core_abort(const Principal *,const char *);
DownloadOutcome download_core_history(const Principal *,uint32_t,uint32_t,DownloadHistory *);
DownloadOutcome download_core_stats(Vault *,const Principal *,uint32_t,uint32_t,DownloadStats *);
#endif

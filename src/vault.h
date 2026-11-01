#ifndef VAULT_H
#define VAULT_H
#include <stddef.h>
#include <stdint.h>
#include "principal.h"
#define VAULT_ID_MAX 0x7ffffffeu
#define VAULT_MAX_FILES 2000
#define VAULT_MAX_BYTES (1024u*1024u*1024u)
#define VAULT_TOTAL_BYTES (16ULL*1024*1024*1024)
#define VAULT_BATCH_BYTES (16ULL*1024*1024*1024)
#define VAULT_CHUNK 768
#define VAULT_MAX_EVENTS 10000
#define VAULT_CATEGORY_BYTES 48
#define VAULT_UPLOADS 24
#define VAULT_UPLOADS_PER_SUBJECT 2
typedef enum { VAULT_OK,VAULT_DENIED,VAULT_NOT_FOUND,VAULT_INVALID,VAULT_BUSY,VAULT_STORAGE,VAULT_LIMIT } VaultResult;
typedef struct {
    uint32_t id,size; uint64_t created;int ascii;
    unsigned char sha256[32];
    char name[128],description[256],publisher[80],category[VAULT_CATEGORY_BYTES+1];
} VaultEntry;
typedef struct {
    uint32_t sequence,file; uint64_t at;
    char action,actor[80],before[241],after[241];
} VaultEvent;
typedef struct { char name[VAULT_CATEGORY_BYTES+1]; uint32_t count; } VaultCategory;
typedef struct { char name[49],read[65],upload[65],display[81],info[8193],sigop[80]; int can_upload,can_manage,menu,locked;unsigned order,keep; } VaultBoard;
enum { VAULT_UPLOAD_ACTIVE=1, VAULT_UPLOAD_COMMITTED=2, VAULT_UPLOAD_CANCELLED=3 };
typedef struct { uint32_t id,size,received; int state; unsigned char sha256[32]; } VaultUpload;
typedef struct Vault Vault;
/* No BBS, account-store, sockets, UI or authentication dependencies. One owner
 * thread per handle. Separate processes coordinate using the journal lock. */
VaultResult vault_init(const char *directory);
VaultResult vault_open(const char *directory,Vault **out);
void vault_close(Vault *vault);
uint32_t vault_last_id(Vault *);
VaultResult vault_compact_index(Vault *,const char *new_output);
/* Optional live authorization checkpoint; callback never performs storage I/O. */
void vault_set_guard(Vault *,int (*guard)(void *),void *context);
VaultResult vault_list(Vault *,const Principal *,uint32_t cursor,VaultEntry out[8],size_t *count,uint32_t *next);
VaultResult vault_info(Vault *,const Principal *,uint32_t id,VaultEntry *out);
VaultResult vault_read(Vault *,const Principal *,uint32_t id,uint32_t offset,unsigned char out[VAULT_CHUNK],size_t *count);
/* Local administration only; no filesystem path is accepted from BBS clients. */
VaultResult vault_import(Vault *,const Principal *,const char *source,const char *name,const char *description,VaultEntry *out);
VaultResult vault_upload_protocol(Vault *,const Principal *,uint32_t,const char *);
VaultResult vault_protocol(Vault *,const Principal *,uint32_t,char [12]);
VaultResult vault_copy(Vault *,const Principal *,uint32_t,const char *,VaultEntry *);
VaultResult vault_rename(Vault *,const Principal *,uint32_t,const char *);
VaultResult vault_password(Vault *,const Principal *,const char *,const char *);
VaultResult vault_unlock(Vault *,const Principal *,const char *,const char *,uint32_t *,uint32_t *);
VaultResult vault_owner(Vault *,const Principal *,uint32_t,const char *);
VaultResult vault_ascii(Vault *,const Principal *,uint32_t,int);
VaultResult vault_board_metadata(Vault *,const Principal *,const char *,char,const char *);
VaultResult vault_description(Vault *,const Principal *,uint32_t,const char *);
VaultResult vault_restore(Vault *,const Principal *,uint32_t);
VaultResult vault_news_in(Vault *,const Principal *,uint32_t,uint64_t,const char *,VaultEntry [8],size_t *,uint32_t *);
VaultResult vault_news(Vault *,const Principal *,uint32_t,uint64_t,VaultEntry out[8],size_t *,uint32_t *);
VaultResult vault_board_check(Vault*,const Principal*,const char*);
int vault_log_policy(Vault *,uint32_t,unsigned *,unsigned *);
VaultResult vault_board_get(Vault *,const Principal *,const char *,VaultBoard *);
VaultResult vault_board_set(Vault *,const Principal *,const char *,const char *,const char *);
VaultResult vault_boards(Vault *,const Principal *,uint32_t,VaultBoard out[8],size_t *,uint32_t *);
VaultResult vault_import_in(Vault *,const Principal *,const char *,const char *,const char *,const char *,VaultEntry *);
VaultResult vault_upload_start_in(Vault *,const Principal *,uint32_t,const unsigned char[32],const char *,const char *,const char *,uint32_t *);
VaultResult vault_reclaim(Vault *,const Principal *,uint32_t);
VaultResult vault_hide(Vault *,const Principal *,uint32_t id);
VaultResult vault_search(Vault *,const Principal *,uint32_t cursor,const char *query,const char *category,VaultEntry out[8],size_t *count,uint32_t *next);
VaultResult vault_category(Vault *,const Principal *,uint32_t id,const char *category);
VaultResult vault_categories(Vault *,const Principal *,uint32_t cursor,VaultCategory out[8],size_t *count,uint32_t *next);
VaultResult vault_audit(Vault *,const Principal *,uint32_t cursor,VaultEvent out[4],size_t *count,uint32_t *next);
/* Caller authenticates on every call. Upload IDs are not bearer credentials.
 * Reservation and finalization are durable. Data is sequential; identical
 * chunk retries are accepted. A successful commit can be retried safely. */
VaultResult vault_upload_start(Vault *,const Principal *,uint32_t size,const unsigned char sha256[32],const char *name,const char *description,uint32_t *id);
VaultResult vault_upload_status(Vault *,const Principal *,uint32_t id,VaultUpload *out);
VaultResult vault_upload_write(Vault *,const Principal *,uint32_t id,uint32_t offset,const unsigned char *bytes,size_t count,uint32_t *received);
VaultResult vault_upload_finish(Vault *,const Principal *,uint32_t id,VaultEntry *out);
VaultResult vault_upload_cancel(Vault *,const Principal *,uint32_t id);
const char *vault_result_name(VaultResult result);
void vault_hex(const unsigned char hash[32],char text[65]);
#endif

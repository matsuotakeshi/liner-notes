#ifndef LINER_BATCH_CORE_H
#define LINER_BATCH_CORE_H
#include <stddef.h>
#include <stdint.h>
enum { BATCH_CAPACITY=16, BATCH_PATH_MAX=48 };
/* Session-owned selection; completion flags travel with their IDs on removal.
 * No file/DB I/O, authentication or transfer engine access. Zero-init/reset. */
typedef struct {
    uint32_t ids[BATCH_CAPACITY];unsigned char done[BATCH_CAPACITY],copied[BATCH_CAPACITY];
    unsigned count,copy_at;char copy_path[BATCH_PATH_MAX+1];
} BatchSelection;
typedef struct { uint32_t ids[BATCH_CAPACITY];unsigned count; } BatchIds;
void batch_reset(BatchSelection *);
/* Existing duplicate add/missing remove are successful no-ops. limit may only
 * reduce the fixed capacity. A full-list rejection leaves selection unchanged. */
int batch_add(BatchSelection *,uint32_t,unsigned limit);
void batch_remove(BatchSelection *,uint32_t);
void batch_replace(BatchSelection *,const BatchIds *);
void batch_downloaded(BatchSelection *,uint32_t);
BatchIds batch_ids(const BatchSelection *,int pending_only);
/* -1 invalid, 0 already copied, 1 next ID. Destination changes reset only copy
 * receipts, never download receipts. Caller authorizes each actual copy. */
int batch_copy_begin(BatchSelection *,const char *,uint32_t *);
/* Call only after a successful current copy receipt; returns next ID or zero.
 * Failed worker results must not call this: the same item remains retryable. */
uint32_t batch_copy_complete(BatchSelection *);
#endif

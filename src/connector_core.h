#ifndef LINER_CONNECTOR_CORE_H
#define LINER_CONNECTOR_CORE_H
#include <stddef.h>
#include <stdint.h>
/* Per-session transport state. This API grants no authentication or store access. */
enum { CONNECTOR_FILE_BLOCK_MAX=90000, CONNECTOR_LINK_BLOCK_MAX=360000,
       CONNECTOR_FRAME_MAX=1024, CONNECTOR_WRITE_MAX=768 };
typedef enum { CONNECTOR_FILE_BLOCK,CONNECTOR_LINK_BLOCK } ConnectorBlockKind;
typedef struct {
    char *data;size_t size,used;uint64_t deadline;ConnectorBlockKind kind;
} ConnectorBlock;
void connector_block_clear(ConnectorBlock *);
/* Replaces the old block even on failure. 1: allocated, 0: invalid, -1: no memory. */
int connector_block_begin(ConnectorBlock *,ConnectorBlockKind,size_t,uint64_t);
/* -1: invalid and cleared; 0: incomplete; 1: complete. */
int connector_block_append(ConnectorBlock *,const char *,size_t);
/* Complete block ownership transfers to caller (free). Incomplete remains owned. */
char *connector_block_take(ConnectorBlock *,ConnectorBlockKind *);
int connector_block_expired(const ConnectorBlock *,uint64_t);

typedef struct {
    size_t used,length;uint64_t deadline;
    unsigned char bytes[CONNECTOR_FRAME_MAX+5];
} ConnectorReader;
typedef struct {
    unsigned char kind;uint32_t id,offset;
    const unsigned char *data;size_t size;
} ConnectorFrame;
/* Zero initialize before use; enter sets the existing 60s idle deadline. */
void connector_reader_enter(ConnectorReader *,uint64_t);
void connector_reader_touch(ConnectorReader *,uint64_t);
int connector_reader_expired(const ConnectorReader *,uint64_t);
/* -1: malformed; 0: incomplete; 1: decoded. On 1, data borrows reader memory
 * until the next feed/enter. On -1 the caller MUST terminate this connection.
 * L frames carry an untrusted line; the adapter still checks the command allowlist. */
int connector_reader_feed(ConnectorReader *,unsigned char,uint64_t,ConnectorFrame *);
/* Encodes existing binary-v1 output. No partial successful frame on failure.
 * Payload must not overlap output. maximum is the caller's logical body limit. */
int connector_frame_encode(unsigned char,const void *,size_t,size_t,unsigned char *,size_t,size_t *);
#endif

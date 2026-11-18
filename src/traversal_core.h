#ifndef TRAVERSAL_CORE_H
#define TRAVERSAL_CORE_H
#include <stdint.h>
/* Session-owned values. No UI, worker, persistence or authorization dependency. */
typedef struct {
    int active,automatic,cancel_pending;
} ReaderState;
typedef struct { uint32_t cursor;int descending; } ReaderPosition;
typedef enum { READER_REPEAT,READER_PREVIOUS,READER_FORWARD,READER_BACKWARD } ReaderMove;
ReaderPosition reader_move(ReaderMove,uint32_t current,int descending,uint32_t distance);
void reader_stop(ReaderState *,int pending);
/* Call only after the caller checks the completion's session and slot. */
int reader_cancel_completion(ReaderState *,int sequence);
void reader_complete(ReaderState *,int sequence,int selected,int failed);

typedef struct {
    char key[80];
    int scope,automatic,committing,active,pending,finished;
    uint32_t board,cursor;
    uint64_t since,until;
} CheckTraversal;
typedef struct { unsigned kind;uint64_t until;char key[80]; } CheckCommit;
void check_traversal_reset(CheckTraversal *);
void check_traversal_begin(CheckTraversal *,unsigned kind,uint64_t since,uint64_t until);
void check_traversal_page(CheckTraversal *,int has_end,uint32_t next);
int check_traversal_commit(const CheckTraversal *,CheckCommit *);
void check_traversal_committed(CheckTraversal *,int saved);
int check_traversal_auto_ready(const CheckTraversal *,int page,int pending,int closing,int drained);
#endif

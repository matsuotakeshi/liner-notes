#ifndef LINK_CORE_H
#define LINK_CORE_H
#include <stdint.h>
#include <stddef.h>
/* Trusted, serialized board-worker API, not a signature verification endpoint.
 * Identity and epoch must come from the authenticated session. Borrowed input
 * strings/grants live through the call; outputs own their metadata. */
typedef struct { uint32_t user,epoch;const uint32_t *grants; } LinkActor;
typedef enum { LINK_OK,LINK_DENIED,LINK_REVOKED,LINK_BOARD_STORAGE,
    LINK_EXTRAS_STORAGE,LINK_MEMBER_STORAGE,LINK_STORAGE,LINK_IDENTITY,
    LINK_POSTING_DENIED,LINK_REVISION,LINK_PREDECESSOR,LINK_ACTION,
    LINK_UNAVAILABLE,LINK_TITLE,LINK_CONFLICT,LINK_PARENT,
    LINK_POST_LOGIN,LINK_POST_VERSION,LINK_POST_STORAGE,LINK_POST_PARENT,
    LINK_POST_BODY,LINK_POST_FULL,LINK_POST_DRAFT,LINK_POST_CAPACITY,
    LINK_POST_UNCERTAIN } LinkOutcome;
typedef enum { LINK_LIVE,LINK_HIDDEN,LINK_WITHDRAWN } LinkState;
typedef struct {
    uint32_t id,parent,revision;uint64_t created;LinkState state;
    char author[33],title[81],origin[65],key[65];
} LinkItem;
typedef enum { LINK_METADATA,LINK_SNAPSHOT,LINK_BATCH } LinkQuery;
typedef struct { unsigned count;int end;size_t body_size;LinkItem items[32]; } LinkPage;
typedef struct { uint32_t id,revision; } LinkReference;
typedef struct {
    uint32_t board,revision,parent;uint64_t created;
    char kind; /* P post, T title, D withdraw, C checkpoint, W tombstone */
    const char *origin,*key,*author,*title,*body;
} LinkEvent;
typedef struct { LinkReference reference;int created; } LinkApplied;
/* On failure, clear all output (including caller-owned body). Scan returns
 * metadata for hidden articles for synchronization, never their title/body.
 * body is needed only for LINK_SNAPSHOT; it must not alias any input/output. */
LinkOutcome link_core_scan(const LinkActor *,uint32_t board,LinkQuery,uint32_t start,
                           LinkPage *,char *body,size_t capacity);
LinkOutcome link_core_lookup(const LinkActor *,uint32_t board,const char *origin,
                             const char *key,LinkReference *);
LinkOutcome link_core_apply(const LinkActor *,const LinkEvent *,LinkApplied *);
#endif

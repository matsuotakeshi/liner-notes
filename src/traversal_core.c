#include "traversal_core.h"
#include "store_limits.h"

ReaderPosition reader_move(ReaderMove move,uint32_t id,int descending,uint32_t distance) {
    ReaderPosition p={id,descending};
    if(move==READER_REPEAT)return p;
    p.descending=move==READER_PREVIOUS?!descending:move==READER_BACKWARD;
    if(move==READER_PREVIOUS)distance=1;
    if(p.descending)p.cursor=id>distance?id-distance:0;
    else p.cursor=id<=CONTENT_ID_MAX && distance<=CONTENT_ID_MAX-id?id+distance:0;
    return p;
}
void reader_stop(ReaderState *r,int pending) {
    r->automatic=0;r->active=0;r->cancel_pending=pending;
}
int reader_cancel_completion(ReaderState *r,int sequence) {
    int cancelled=r->cancel_pending && sequence;r->cancel_pending=0;
    if(cancelled) { r->active=0;r->automatic=0; }
    return cancelled;
}
void reader_complete(ReaderState *r,int sequence,int selected,int failed) {
    if(sequence)r->active=!!selected;
    else if(failed)r->active=0;
}
void check_traversal_reset(CheckTraversal *c) {
    c->active=c->pending=c->finished=0;c->since=c->until=0;c->cursor=0;
}
void check_traversal_begin(CheckTraversal *c,unsigned kind,uint64_t since,uint64_t until) {
    c->active=(int)kind+1;c->since=since;c->until=until;
    c->cursor=c->active==1?1:0;c->finished=0;
}
void check_traversal_page(CheckTraversal *c,int has_end,uint32_t next) {
    c->pending=0;c->cursor=next;c->finished=has_end && !next;
}
int check_traversal_commit(const CheckTraversal *c,CheckCommit *out) {
    memset(out,0,sizeof(*out));
    if(!c->active || !c->finished)return 0;
    out->kind=(unsigned)c->active-1;out->until=c->until;
    memcpy(out->key,c->key,sizeof(out->key));return 1;
}
void check_traversal_committed(CheckTraversal *c,int saved) {
    c->committing=0;if(saved)c->active=0;else c->automatic=0;
}
int check_traversal_auto_ready(const CheckTraversal *c,int page,int pending,int closing,int drained) {
    return c->active && c->automatic && c->finished && !page && !pending && !closing && drained;
}

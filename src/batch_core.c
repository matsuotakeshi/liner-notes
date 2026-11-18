#include "batch_core.h"
#include <string.h>
void batch_reset(BatchSelection *b) { memset(b,0,sizeof(*b)); }
int batch_add(BatchSelection *b,uint32_t id,unsigned limit) {
    if(!id)return 0;
    for(unsigned i=0;i<b->count;i++)if(b->ids[i]==id)return 1;
    if(b->count>=BATCH_CAPACITY || b->count>=limit)return 0;
    b->ids[b->count]=id;b->done[b->count]=b->copied[b->count]=0;b->count++;return 1;
}
void batch_remove(BatchSelection *b,uint32_t id) {
    unsigned i=0;while(i<b->count && b->ids[i]!=id)i++;
    if(i==b->count)return;
    memmove(b->ids+i,b->ids+i+1,(b->count-i-1)*sizeof(b->ids[0]));
    memmove(b->done+i,b->done+i+1,b->count-i-1);
    memmove(b->copied+i,b->copied+i+1,b->count-i-1);
    b->count--;b->ids[b->count]=0;b->done[b->count]=b->copied[b->count]=0;
    if(i<b->copy_at)b->copy_at--;
}
void batch_replace(BatchSelection *b,const BatchIds *ids) {
    /* MATCH replaces selection/receipts; remembered destination remains local. */
    memset(b->ids,0,sizeof(b->ids));memset(b->done,0,sizeof(b->done));memset(b->copied,0,sizeof(b->copied));
    b->count=ids->count>BATCH_CAPACITY?BATCH_CAPACITY:ids->count;b->copy_at=0;
    memcpy(b->ids,ids->ids,b->count*sizeof(b->ids[0]));
}
void batch_downloaded(BatchSelection *b,uint32_t id) {
    for(unsigned i=0;i<b->count;i++)if(b->ids[i]==id)b->done[i]=1;
}
BatchIds batch_ids(const BatchSelection *b,int pending_only) {
    BatchIds out={0};
    for(unsigned i=0;i<b->count;i++)if(!pending_only || !b->done[i])out.ids[out.count++]=b->ids[i];
    return out;
}
static uint32_t copy_next(BatchSelection *b) {
    while(b->copy_at<b->count && b->copied[b->copy_at])b->copy_at++;
    return b->copy_at<b->count?b->ids[b->copy_at]:0;
}
int batch_copy_begin(BatchSelection *b,const char *path,uint32_t *id) {
    *id=0;if(!path || !*path || strlen(path)>BATCH_PATH_MAX || !b->count)return -1;
    for(const unsigned char *p=(const unsigned char *)path;*p;p++)if(*p<32 || *p==127)return -1;
    if(strcmp(path,b->copy_path))memset(b->copied,0,sizeof(b->copied));
    strcpy(b->copy_path,path);b->copy_at=0;*id=copy_next(b);return *id?1:0;
}
uint32_t batch_copy_complete(BatchSelection *b) {
    if(b->copy_at>=b->count)return 0;
    b->copied[b->copy_at++]=1;return copy_next(b);
}

#include "connector_core.h"
#include <stdlib.h>
#include <string.h>

void connector_block_clear(ConnectorBlock *block) { free(block->data);memset(block,0,sizeof(*block)); }
int connector_block_begin(ConnectorBlock *block,ConnectorBlockKind kind,size_t size,uint64_t now) {
    connector_block_clear(block);
    size_t maximum=kind==CONNECTOR_FILE_BLOCK?CONNECTOR_FILE_BLOCK_MAX:kind==CONNECTOR_LINK_BLOCK?CONNECTOR_LINK_BLOCK_MAX:0;
    if(!size || size>maximum)return 0;
    block->data=malloc(size+1);if(!block->data)return -1;
    block->size=size;block->kind=kind;block->deadline=now+60000;return 1;
}
int connector_block_append(ConnectorBlock *block,const char *data,size_t size) {
    if(!block->data || block->used>block->size || size>block->size-block->used) {
        connector_block_clear(block);return -1;
    }
    if(size)memcpy(block->data+block->used,data,size);
    block->used+=size;return block->used==block->size;
}
char *connector_block_take(ConnectorBlock *block,ConnectorBlockKind *kind) {
    if(!block->data || block->used!=block->size)return NULL;
    char *data=block->data;data[block->used]=0;*kind=block->kind;
    memset(block,0,sizeof(*block));return data;
}
int connector_block_expired(const ConnectorBlock *block,uint64_t now) { return block->data && now>=block->deadline; }
static uint32_t big(const unsigned char *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
void connector_reader_touch(ConnectorReader *reader,uint64_t now) { reader->deadline=now+60000; }
void connector_reader_enter(ConnectorReader *reader,uint64_t now) { memset(reader,0,sizeof(*reader));connector_reader_touch(reader,now); }
int connector_reader_expired(const ConnectorReader *reader,uint64_t now) { return now>=reader->deadline; }
int connector_reader_feed(ConnectorReader *reader,unsigned char byte,uint64_t now,ConnectorFrame *frame) {
    memset(frame,0,sizeof(*frame));
    if(!reader->used) { if(byte!='~')return -1;reader->deadline=now+15000; }
    if(reader->used>=sizeof(reader->bytes))return -1;
    reader->bytes[reader->used++]=byte;
    if(reader->used==5) {
        reader->length=big(reader->bytes+1);
        if(!reader->length || reader->length>CONNECTOR_FRAME_MAX)return -1;
    }
    if(reader->used<5 || reader->used<5+reader->length)return 0;
    const unsigned char *p=reader->bytes+5;size_t n=reader->length;
    reader->used=0;connector_reader_touch(reader,now);
    if(*p=='Q' && n==1)frame->kind='Q';
    else if(*p=='L' && n>1 && !memchr(p+1,0,n-1) && !memchr(p+1,'\r',n-1) && !memchr(p+1,'\n',n-1)) {
        frame->kind='L';frame->data=p+1;frame->size=n-1;
    } else if((*p=='R' && n==9) || (*p=='W' && n>9 && n<=9+CONNECTOR_WRITE_MAX)) {
        frame->kind=*p;frame->id=big(p+1);frame->offset=big(p+5);frame->data=p+9;frame->size=n-9;
    } else return -1;
    return 1;
}
int connector_frame_encode(unsigned char kind,const void *data,size_t size,size_t maximum,unsigned char *out,size_t capacity,size_t *written) {
    *written=0;
    if(size>UINT32_MAX-(kind?1u:0u))return 0;
    size_t length=size+(kind?1u:0u);
    if(!length || length>maximum || capacity<5 || length>capacity-5)return 0;
    out[0]='~';for(unsigned i=0;i<4;i++)out[1+i]=(unsigned char)(length>>(24-i*8));
    if(kind)out[5]=kind;
    if(size)memcpy(out+5+(kind?1:0),data,size);
    *written=length+5;return 1;
}

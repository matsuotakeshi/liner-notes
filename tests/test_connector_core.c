#include "connector_core.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int feed(ConnectorReader *r,const unsigned char *bytes,size_t size,uint64_t now,ConnectorFrame *f) {
    for(size_t i=0;i<size;i++) {
        int result=connector_reader_feed(r,bytes[i],now,f);
        if(i+1==size)return result;
        assert(result==0 && !f->kind && !f->data && !f->size);
    }
    return 0;
}
static void blocks(void) {
    ConnectorBlock a={0},b={0};ConnectorBlockKind kind;
    assert(!connector_block_expired(&a,UINT64_MAX));
    assert(connector_block_begin(&a,CONNECTOR_FILE_BLOCK,6,100)==1);
    assert(!connector_block_expired(&a,60099) && connector_block_expired(&a,60100));
    assert(connector_block_append(&a,"abc",3)==0);
    assert(!connector_block_take(&a,&kind) && a.used==3);
    assert(connector_block_begin(&b,CONNECTOR_LINK_BLOCK,3,900)==1);
    assert(connector_block_append(&b,"xyz",3)==1);
    assert(connector_block_append(&a,"def",3)==1 && a.deadline==60100);
    char *text=connector_block_take(&a,&kind);assert(kind==CONNECTOR_FILE_BLOCK && !strcmp(text,"abcdef"));free(text);
    assert(!a.data && !a.size && !a.used && !a.deadline);
    text=connector_block_take(&b,&kind);assert(kind==CONNECTOR_LINK_BLOCK && !strcmp(text,"xyz"));free(text);
    assert(connector_block_append(&a,"orphan",6)==-1 && !a.data);
    for(unsigned k=0;k<2;k++) {
        size_t maximum=k?CONNECTOR_LINK_BLOCK_MAX:CONNECTOR_FILE_BLOCK_MAX;
        assert(connector_block_begin(&a,(ConnectorBlockKind)k,maximum,10)==1);
        char *chunk=malloc(maximum);assert(chunk);memset(chunk,'A',maximum);
        assert(connector_block_append(&a,chunk,maximum-1)==0);
        assert(connector_block_append(&a,"BC",2)==-1 && !a.data); /* no partial oversized write */
        assert(connector_block_begin(&a,(ConnectorBlockKind)k,maximum,10)==1);
        assert(connector_block_append(&a,chunk,maximum)==1);
        text=connector_block_take(&a,&kind);assert(kind==(ConnectorBlockKind)k && strlen(text)==maximum);free(text);free(chunk);
        assert(!connector_block_begin(&a,(ConnectorBlockKind)k,maximum+1,0));
    }
    assert(connector_block_begin(&a,CONNECTOR_LINK_BLOCK,10,1)==1);
    assert(connector_block_begin(&a,CONNECTOR_FILE_BLOCK,2,2)==1 && a.used==0 && a.size==2 && a.deadline==60002);
    assert(!connector_block_begin(&a,CONNECTOR_FILE_BLOCK,0,0) && !a.data);
    assert(!connector_block_begin(&a,(ConnectorBlockKind)99,1,0));
    assert(!connector_block_begin(&a,CONNECTOR_LINK_BLOCK,SIZE_MAX,0));
    assert(connector_block_begin(&a,CONNECTOR_FILE_BLOCK,2,2)==1);connector_block_clear(&a);connector_block_clear(&a);
    assert(!a.data && !a.deadline);
}
static void frames(void) {
    ConnectorReader r={0},other={0};ConnectorFrame f;unsigned char wire[1030];size_t n;
    connector_reader_enter(&r,100);assert(!connector_reader_expired(&r,60099) && connector_reader_expired(&r,60100));
    assert(connector_reader_feed(&r,'~',200,&f)==0 && r.deadline==15200);
    assert(connector_reader_feed(&r,0,10000,&f)==0 && r.deadline==15200);
    assert(!connector_reader_expired(&r,15199) && connector_reader_expired(&r,15200));
    connector_reader_enter(&r,100);assert(!r.used && !r.length);
    assert(connector_frame_encode('Q',NULL,0,1024,wire,sizeof(wire),&n));
    assert(feed(&r,wire,n,200,&f)==1 && f.kind=='Q' && !f.size && !f.data && r.deadline==60200);
    connector_reader_touch(&r,300);assert(r.deadline==60300);
    const char *line="FILESTATUS 12";
    assert(connector_frame_encode('L',line,strlen(line),1024,wire,sizeof(wire),&n));
    assert(feed(&r,wire,n,400,&f)==1 && f.kind=='L' && f.size==strlen(line) && !memcmp(f.data,line,f.size));
    unsigned char payload[8+CONNECTOR_WRITE_MAX];
    payload[0]=0x12;payload[1]=0x34;payload[2]=0x56;payload[3]=0x78;
    payload[4]=0x9a;payload[5]=0xbc;payload[6]=0xde;payload[7]=0xf0;
    for(unsigned i=8;i<sizeof(payload);i++)payload[i]=(unsigned char)i;
    assert(connector_frame_encode('W',payload,sizeof(payload),1024,wire,sizeof(wire),&n));
    connector_reader_enter(&other,0);
    for(size_t i=0;i<n;i++) {
        assert(connector_reader_feed(&r,wire[i],500,&f)==(i+1==n));
        ConnectorFrame g;assert(connector_reader_feed(&other,wire[i],600,&g)==(i+1==n));
        if(i+1==n)assert(g.kind=='W' && g.size==768 && g.data!=f.data && !memcmp(g.data,f.data,768));
    }
    assert(f.kind=='W' && f.id==0x12345678 && f.offset==0x9abcdef0 && f.size==768 && !memcmp(f.data,payload+8,768));
    assert(connector_frame_encode('R',payload,8,1024,wire,sizeof(wire),&n));
    assert(feed(&r,wire,n,700,&f)==1 && f.kind=='R' && f.size==0 && f.id==0x12345678);
    char maximum[1023];memset(maximum,'x',sizeof(maximum));
    assert(connector_frame_encode('L',maximum,sizeof(maximum),1024,wire,sizeof(wire),&n));
    assert(n==1029 && feed(&r,wire,n,800,&f)==1 && f.size==1023);
}
static void invalid_frames(void) {
    ConnectorReader r={0};ConnectorFrame f;
    const unsigned char *bad[]={ (const unsigned char *)"x", (const unsigned char *)"~\0\0\0\0", (const unsigned char *)"~\0\0\4\1",
        (const unsigned char *)"~\0\0\0\1X", (const unsigned char *)"~\0\0\0\2Qx", (const unsigned char *)"~\0\0\0\1L",
        (const unsigned char *)"~\0\0\0\3La\n", (const unsigned char *)"~\0\0\0\3La\r", (const unsigned char *)"~\0\0\0\3La\0" };
    const size_t lengths[]={1,5,5,6,7,6,8,8,8};
    for(size_t i=0;i<sizeof(lengths)/sizeof(*lengths);i++) {
        connector_reader_enter(&r,1);assert(feed(&r,bad[i],lengths[i],2,&f)==-1 && !f.kind && !f.data && !f.size);
    }
    unsigned char wire[1030],payload[778]={0};size_t size;
    for(unsigned mode=0;mode<4;mode++) {
        unsigned char kind=mode<2?'R':'W';size_t n=mode==0?7:mode==1?9:mode==2?8:777;
        assert(connector_frame_encode(kind,payload,n,1024,wire,sizeof(wire),&size));
        connector_reader_enter(&r,1);assert(feed(&r,wire,size,2,&f)==-1);
    }
}
static void output(void) {
    unsigned char guarded[16];size_t written;
    for(size_t capacity=0;capacity<7;capacity++) {
        memset(guarded,0xcc,sizeof(guarded));written=99;
        assert(!connector_frame_encode('T',"x",1,1024,guarded+1,capacity,&written) && !written);
        for(size_t i=0;i<sizeof(guarded);i++)assert(guarded[i]==0xcc);
    }
    assert(connector_frame_encode('T',"x",1,2,guarded,7,&written) && written==7);
    assert(!memcmp(guarded,"~\0\0\0\2Tx",7));
    assert(connector_frame_encode(0,"raw",3,3,guarded,8,&written) && written==8 && !memcmp(guarded,"~\0\0\0\3raw",8));
    assert(!connector_frame_encode(0,NULL,0,100,guarded,sizeof(guarded),&written) && !written);
    assert(!connector_frame_encode('T',"x",1,1,guarded,sizeof(guarded),&written));
    assert(!connector_frame_encode('T',NULL,SIZE_MAX,SIZE_MAX,guarded,sizeof(guarded),&written));
    assert(!connector_frame_encode('T',NULL,UINT32_MAX,SIZE_MAX,guarded,sizeof(guarded),&written));
}
int main(void) {
    blocks();frames();invalid_frames();output();
    puts("PASS connector Core: block ownership/replacement/bounds/deadlines, frame fragmentation/isolation/validation and output encoding");return 0;
}

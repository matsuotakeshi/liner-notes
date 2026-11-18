#include "batch_core.h"
#include "batch_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static void selection(void) {
    BatchSelection b={0};
    assert(!batch_add(&b,0,16));assert(!batch_add(&b,1,0));
    assert(batch_add(&b,10,2) && batch_add(&b,20,2));
    batch_downloaded(&b,20);b.copied[1]=1;BatchSelection before=b;
    assert(batch_add(&b,20,1) && !memcmp(&b,&before,sizeof(b)));
    assert(!batch_add(&b,30,2) && !memcmp(&b,&before,sizeof(b)));
    batch_remove(&b,999);batch_downloaded(&b,999);assert(!memcmp(&b,&before,sizeof(b)));
    batch_remove(&b,10);assert(b.count==1 && b.ids[0]==20 && b.done[0] && b.copied[0]);
    assert(batch_add(&b,10,16));assert(!b.done[1] && !b.copied[1] && b.ids[1]==10);
    batch_remove(&b,20);assert(b.count==1 && b.ids[0]==10 && !b.done[0] && !b.copied[0]);
    batch_reset(&b);BatchSelection zero={0};assert(!memcmp(&b,&zero,sizeof(b)));
    for(unsigned i=1;i<=16;i++)assert(batch_add(&b,i,100));
    before=b;assert(!batch_add(&b,17,100) && !memcmp(&b,&before,sizeof(b)));
    BatchIds all=batch_ids(&b,0);assert(all.count==16 && all.ids[15]==16);
}
static void transfer_and_copy(void) {
    BatchSelection b={0},other={0};uint32_t next;
    assert(batch_add(&b,1,16) && batch_add(&b,2,16) && batch_add(&b,3,16));
    batch_downloaded(&b,2);batch_downloaded(&b,2);batch_downloaded(&other,2);
    BatchIds pending=batch_ids(&b,1);assert(pending.count==2 && pending.ids[0]==1 && pending.ids[1]==3 && !other.count);
    assert(batch_copy_begin(&b,"tools",&next)==1 && next==1);
    assert(batch_copy_complete(&b)==2);assert(b.copied[0] && !b.copied[1]);
    /* Failed second copy does not acknowledge it; same destination retries it. */
    assert(batch_copy_begin(&b,"tools",&next)==1 && next==2);
    assert(batch_copy_complete(&b)==3 && batch_copy_complete(&b)==0);
    assert(batch_copy_complete(&b)==0); /* no out-of-bounds write after completion */
    assert(batch_copy_begin(&b,"tools",&next)==0 && !next);
    pending=batch_ids(&b,1);assert(pending.count==2); /* copies never count as downloads */
    assert(batch_copy_begin(&b,"other",&next)==1 && next==1 && !b.copied[0] && b.done[1]);
    BatchSelection before=b;char long_path[50];memset(long_path,'p',49);long_path[49]=0;
    const char *bad[]={"",long_path,"bad\npath","bad\177path"};
    for(unsigned i=0;i<4;i++)assert(batch_copy_begin(&b,bad[i],&next)==-1 && !next && !memcmp(&before,&b,sizeof(b)));
    assert(batch_copy_begin(&other,"tools",&next)==-1);
    batch_downloaded(&b,1);batch_downloaded(&b,3);assert(!batch_ids(&b,1).count);
    batch_remove(&b,2);assert(batch_add(&b,2,16));pending=batch_ids(&b,1);assert(pending.count==1 && pending.ids[0]==2);
    BatchIds replacement={{7,8},2};batch_replace(&b,&replacement);
    assert(b.count==2 && b.ids[0]==7 && !b.done[0] && !b.copied[0] && !strcmp(b.copy_path,"other"));
    assert(batch_copy_begin(&b,"other",&next)==1 && next==7);
    batch_reset(&b);assert(!b.count && !*b.copy_path && !b.copy_at);
}
static void protocol(void) {
    struct { const char *input;unsigned count; } cases[]={
        {"",0},{" 1 2\r\n",2},{" 1  2",1},{" 1x 2",1},{" 0 1",0},
        {" 01 2",2},{" 2147483647",1},{" 2147483648",0},{" 1 1",2},{"1 2",0},
        {" 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17",16}
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++)assert(batch_response_ids(cases[i].input).count==cases[i].count);
    BatchIds ids=batch_response_ids(" 7 8 9");char out[192];
    assert(batch_format_ids(&ids,out,sizeof(out)) && !strcmp(out,"7,8,9"));
    for(size_t n=1;n<=5;n++)assert(!batch_format_ids(&ids,out,n) && !*out);
    assert(batch_format_ids(&ids,out,6));assert(!batch_format_ids(&ids,out,0));
    ids.count=0;assert(batch_format_ids(&ids,out,1) && !*out);
    ids.count=16;for(unsigned i=0;i<16;i++)ids.ids[i]=UINT32_MAX;
    assert(batch_format_ids(&ids,out,sizeof(out)) && strlen(out)==175);
    ids.count=17;assert(!batch_format_ids(&ids,out,sizeof(out)) && !*out);
    uint32_t id;assert(batch_download_receipt("DOWNLOADED 42\r\n",&id) && id==42);
    assert(batch_download_receipt("DOWNLOADED 42 extra",&id) && id==42); /* preserve old helper grammar */
    assert(!batch_download_receipt("ERR transfer failed",&id) && !id);
    assert(!batch_download_receipt("UPLOADED 42",&id) && !id);
}
static void completion_orders(void) {
    /* All 16-file subsets: pending list must contain only the unacknowledged
     * IDs in original order, regardless of reverse/repeated completion. */
    for(unsigned mask=0;mask<65536;mask++) {
        BatchSelection b={0};for(unsigned n=1;n<=16;n++)assert(batch_add(&b,n,16));
        for(unsigned n=16;n;n--)if(mask&(1u<<(n-1))) { batch_downloaded(&b,n);batch_downloaded(&b,n); }
        BatchIds pending=batch_ids(&b,1);unsigned at=0;
        for(unsigned n=1;n<=16;n++)if(!(mask&(1u<<(n-1))))assert(pending.ids[at++]==n);
        assert(at==pending.count);
    }
}
int main(void) {
    selection();transfer_and_copy();protocol();completion_orders();
    puts("PASS: batch selection/capacity, per-ID receipts, copy resume/destination changes, protocol bounds and 65536 completion subsets");return 0;
}

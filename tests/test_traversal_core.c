#include "traversal_core.h"
#include "traversal_protocol.h"
#include "store_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void navigation(void) {
    const uint32_t ids[]={0,1,2,50,10000,CONTENT_ID_MAX-1,CONTENT_ID_MAX};
    const uint32_t steps[]={1,2,49,10000,CONTENT_ID_MAX,CONTENT_ID_MAX+1};
    for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);i++)for(int dir=0;dir<2;dir++) {
        uint32_t id=ids[i];ReaderPosition p=reader_move(READER_REPEAT,id,dir,1);
        assert(p.cursor==id && p.descending==dir);
        p=reader_move(READER_PREVIOUS,id,dir,1);
        assert(p.descending==!dir);
        assert(p.cursor==(dir?(id<CONTENT_ID_MAX?id+1:0):(id>1?id-1:0)));
        for(unsigned j=0;j<sizeof(steps)/sizeof(*steps);j++) {
            uint32_t step=steps[j];
            p=reader_move(READER_FORWARD,id,dir,step);
            uint64_t sum=(uint64_t)id+step;
            assert(!p.descending && p.cursor==(sum<=CONTENT_ID_MAX?(uint32_t)sum:0));
            p=reader_move(READER_BACKWARD,id,dir,step);
            assert(p.descending && p.cursor==(id>step?id-step:0));
        }
    }
    ReaderState a={1,1,0},b=a;
    reader_stop(&a,1);assert(!a.active && !a.automatic && a.cancel_pending);
    assert(reader_cancel_completion(&a,1));assert(!a.cancel_pending);
    assert(!reader_cancel_completion(&a,1));assert(b.active && b.automatic);
    reader_stop(&a,1);assert(!reader_cancel_completion(&a,0) && !a.cancel_pending);
    reader_complete(&a,1,123,0);assert(a.active);
    reader_complete(&a,0,0,0);assert(a.active);
    reader_complete(&a,0,0,1);assert(!a.active);
    reader_complete(&a,1,0,0);assert(!a.active);
}
static void checkpoints(void) {
    const char *kinds[]={"ARTICLES","ARTICLES ALL","ARTICLES HERE","ARTICLES SUBTREE","MAIL","FILES","FILES tools/日本語"};
    const unsigned numbers[]={0,0,0,0,1,2,2};
    const char *keys[]={"","","42:0","42:2","","","tools/日本語"};
    for(unsigned i=0;i<sizeof(kinds)/sizeof(*kinds);i++) {
        CheckTraversal c={0};CheckCommit commit;
        unsigned kind=check_traversal_select(&c,kinds[i],42);
        assert(kind==numbers[i] && c.board==42 && !strcmp(c.key,keys[i]));
        assert(c.scope==(i==2?0:i==3?2:1));
        check_traversal_begin(&c,kind,100,200);
        assert(c.cursor==(kind==0?1u:0u) && c.since==100 && c.until==200);
        memset(&commit,0xff,sizeof(commit));assert(!check_traversal_commit(&c,&commit));
        assert(!commit.kind && !commit.until && !commit.key[0]);
        c.pending=1;check_traversal_response(&c,"item\r\nEND NEXT 17\r\n");
        assert(!c.pending && c.cursor==17 && !c.finished);
        check_traversal_response(&c,"ERR storage\r\n");assert(!c.finished);
        check_traversal_response(&c,"END NEXT 0\r\n");assert(c.finished);
        assert(check_traversal_commit(&c,&commit));
        assert(commit.kind==kind && commit.until==200 && !strcmp(commit.key,keys[i]));
        for(unsigned mask=0;mask<64;mask++) {
            c.automatic=!!(mask&1);c.finished=!!(mask&2);
            assert(check_traversal_auto_ready(&c,mask&4,mask&8,mask&16,mask&32)==(mask==35));
        }
        c.finished=1;c.automatic=1;c.committing=1;
        check_traversal_committed(&c,0);
        assert(c.active && c.finished && !c.automatic && !c.committing);
        assert(check_traversal_commit(&c,&commit)); /* explicit retry retains window */
        c.committing=1;check_traversal_committed(&c,1);
        assert(!c.active && !c.committing && !check_traversal_commit(&c,&commit));
        c.automatic=1;check_traversal_begin(&c,kind,300,400);c.pending=1;
        check_traversal_reset(&c);
        assert(!c.active && !c.pending && !c.finished && !c.since && !c.until && !c.cursor && c.automatic);
    }
    CheckTraversal a={0},b={0};
    assert(check_traversal_select(&a,"ARTICLES HERE",1)==0);
    assert(check_traversal_select(&b,"ARTICLES SUBTREE",2)==0);
    check_traversal_begin(&a,0,10,20);check_traversal_begin(&b,0,30,40);
    check_traversal_page(&a,1,0);assert(!b.finished && b.until==40 && !strcmp(b.key,"2:2"));
    const char *bad[]={"articles","ARTICLES OTHER","FILES a|b","FILES a\nb","FILES a\177b"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(check_traversal_select(&a,bad[i],1)==3);
    char path[56]="FILES ";memset(path+6,'x',48);path[54]=0;
    assert(check_traversal_select(&a,path,1)==2 && strlen(a.key)==48);
    path[54]='x';path[55]=0;assert(check_traversal_select(&a,path,1)==3);
    /* Trusted response decoder preserves the existing marker grammar. */
    check_traversal_response(&a,"prefix END NEXT 7 extra");assert(a.cursor==7 && !a.finished);
    check_traversal_response(&a,"END NEXT invalid");assert(a.finished && !a.cursor);
    check_traversal_response(&a,"END NEXT 2147483647");assert(a.cursor==2147483647u && !a.finished);
}
int main(void) {
    navigation();checkpoints();
    puts("PASS: reader ID bounds/direction/cancel, checkpoint scopes/windows/retry, auto barriers, independent sessions and legacy markers");
    return 0;
}

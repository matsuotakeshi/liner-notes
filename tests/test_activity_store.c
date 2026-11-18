#include "activity_repository.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern unsigned gather_limit;
int activity_open(const char *);
void activity_close(void);
/* Replay identity and text validation seams; the Core unit and socket tests
 * exercise actual admission. This test exercises the real durable journal. */
const char *members_name(uint32_t id) { return id==1 || id==2?"member":NULL; }
int activity_gather_valid(const char *s) { size_t n=strlen(s);return n>=2 && !strcmp(s+n-2,"\r\n"); }
int main(int argc,char **argv) {
    assert(argc==2);const char *base=argv[1];ActivityRecord row;
    assert(!activity_repository_ready());
    gather_limit=2;assert(activity_open(base) && activity_repository_ready());
    assert(activity_repository_append('G',1,0,"old\r\n"));
    assert(activity_repository_append('G',2,0,"retained\r\n"));
    assert(activity_repository_append('A',1,0,"notice"));
    assert(activity_repository_append('G',1,0,"new\r\n"));
    assert(activity_repository_count()==2 && activity_repository_read(0,&row));
    assert(row.sequence==2 && row.actor==2 && !strcmp(row.body,"retained\r\n"));
    assert(activity_repository_read(1,&row) && row.sequence==4 && !strcmp(row.body,"new\r\n"));
    assert(!activity_repository_read(2,&row));activity_close();
    assert(activity_open(base) && activity_repository_count()==2);
    assert(activity_repository_read(0,&row) && row.sequence==2);
    assert(activity_repository_append('K',1,42,"reason"));
    assert(activity_repository_append('S',1,30,"shutdown"));
    assert(activity_repository_append('C',1,0,""));activity_close();
    assert(!activity_repository_ready());
    assert(!activity_repository_append('G',1,0,"closed\r\n"));
    puts("PASS: actual append journal, audit sequence, retention and restart");
    gather_limit=1;assert(activity_open(base));assert(activity_repository_count()==1);
    assert(activity_repository_read(0,&row) && row.sequence==4);activity_close();
    gather_limit=0;assert(activity_open(base));assert(!activity_repository_count());activity_close();
    assert(!activity_repository_ready());
    gather_limit=2;assert(activity_open(base) && activity_repository_ready());assert(activity_repository_count()==2);activity_close();
    char path[4096];assert(snprintf(path,sizeof(path),"%s.activity",base)>0);
    FILE *f=fopen(path,"ab");assert(f);assert(fwrite("bad",1,3,f)==3);assert(!fclose(f));
    assert(!activity_open(base));activity_close();
    f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));assert(ftell(f)==8+7*4096+3);assert(!fclose(f));
    puts("PASS: retention reload/disable, damaged-tail refusal without repair or truncation");return 0;
}

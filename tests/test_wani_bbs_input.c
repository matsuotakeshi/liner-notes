#include "wani_bbs_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char input[]=" \tW   Keep mixed case and spaces";
    WaniCommand cmd=wani_command(input);
    assert(!strcmp(cmd.word,"W") && !strcmp(cmd.argument,"Keep mixed case and spaces"));
    assert(wani_equal("Read","rEaD") && !wani_equal("read","reader"));
    assert(wani_match("comp.*","COMP.FreeBSD") && !wani_match("comp.*","Compatible"));
    assert(wani_match("F?ee*","FreeBSD") && !wani_match("F?ee?","FreeBSD"));
    uint32_t first,last;
    assert(wani_range("=,$",8,120,&first,&last) && first==8 && last==120);
    assert(wani_range("100,",8,120,&first,&last) && first==100 && last==120);
    assert(wani_range(",30",8,120,&first,&last) && first==1 && last==30);
    assert(wani_range("5",8,120,&first,&last) && first==5 && last==5);
    assert(!wani_range("0",8,120,&first,&last));
    assert(!wani_range("4294967296,",8,120,&first,&last));
    assert(!wani_range("1,foo",8,120,&first,&last));
    puts("PASS WANI-BBS command case, wildcard matching and range boundaries");
    return 0;
}

#include "ktbbs_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char original[]=" t7";
    KtbbsInput token=ktbbs_input(original);
    assert(token.key=='T' && !strcmp(token.argument,"7") && !strcmp(original," t7"));
    token=ktbbs_input("n@ 96/04/02");
    assert(token.key=='N' && !strcmp(token.argument,"@ 96/04/02"));
    token=ktbbs_input("r \t#21");
    assert(token.key=='R' && !strcmp(token.argument,"#21"));
    assert(ktbbs_input("read 21").key==0);
    assert(ktbbs_input("R21").key==0);
    assert(ktbbs_input("   ").key=='\r');
    assert(ktbbs_input("//").key=='/');
    assert(ktbbs_input("/say hello").key==0);
    assert(ktbbs_input("0 trailing").key=='0');
    assert(ktbbs_input("\x01").key==0);
    uint32_t id=99;
    assert(ktbbs_id("#321",&id) && id==321);
    assert(ktbbs_id("4294967295",&id) && id==UINT32_MAX);
    assert(!ktbbs_id("4294967296",&id) && id==UINT32_MAX);
    assert(!ktbbs_id("000",&id) && !ktbbs_id("-2",&id) && !ktbbs_id("12é",&id));
    char date[11];
    assert(ktbbs_date("96/04/02",date) && !strcmp(date,"1996-04-02"));
    assert(ktbbs_date("00/12/31",date) && !strcmp(date,"2000-12-31"));
    assert(ktbbs_date("2026-11-26",date) && !strcmp(date,"2026-11-26"));
    assert(!ktbbs_date("1996/4/2",date));
    puts("PASS KTBBS input: prompt grammar, compact NEWS/telegram, ID and dates");
    return 0;
}

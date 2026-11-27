#include "vs_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char line[]="  \xef\xbd\x8e\xef\xbc\xa1 MiXeD";
    VsCommand command=vs_command(line);
    assert(!strcmp(command.key,"NA") && !strcmp(command.argument,"MiXeD"));
    char invalid[]="ABCDEFGHIJKLMNOP";
    assert(!*vs_command(invalid).key);
    VsDirect post=vs_direct("d;w;#1;Mixed Title;y");
    assert(post.kind==VS_DIRECT_BASE && !strcmp(post.board,"#1"));
    assert(!strcmp(post.title,"Mixed Title"));
    post=vs_direct("DW#15;Title;Y");
    assert(post.kind==VS_DIRECT_BASE && !strcmp(post.board,"#15"));
    post=vs_direct("\xef\xbd\x84;\xef\xbc\xb2;#12;2147483646;Y");
    assert(post.kind==VS_DIRECT_REPLY && post.parent==2147483646u && !*post.title);
    assert(vs_direct("DJ;#2;1;Y;D;W;#3;wrong;Y").kind==VS_DIRECT_JUMP);
    assert(vs_direct("D;J;#2;1;Y").kind==VS_DIRECT_JUMP);
    assert(vs_direct("D;W;#1;Valid;Y;D;W;#2;more;Y").kind==VS_DIRECT_INVALID);
    assert(vs_direct("D;W;#1;incomplete").kind==VS_DIRECT_INVALID);
    assert(vs_direct("D;R;#1;2147483647;Y").kind==VS_DIRECT_INVALID);
    assert(vs_direct("D;R;#1;0;Y").kind==VS_DIRECT_INVALID);
    assert(vs_direct("D;W;#1;control\x01;Y").kind==VS_DIRECT_INVALID);
    puts("PASS VS command folding and bounded direct-write grammar");
    return 0;
}

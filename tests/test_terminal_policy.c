#include "classic_input.h"
#include "terminal_pager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void normalized(ClassicInput state,const char *before,const char *after) {
    char text[512];strcpy(text,before);classic_input_normalize(&state,text,strlen(text));assert(!strcmp(text,after));
}
static void inputs(void) {
    ClassicInput normal={0};
    normalized(normal,"lang en","LANG EN");normalized(normal,"post MiXeD日本語","POST MiXeD日本語");
    normalized(normal,"login Alice","LOGIN Alice");normalized(normal,"help mail","HELP MAIL");
    normalized(normal,"find desc all MiXeD","FIND DESC ALL MiXeD");
    normalized(normal,"scan asc all MiXeD","SCAN ASC ALL MiXeD");
    normalized(normal,"mail scan desc","MAIL SCAN DESC");
    normalized(normal,"mail notify off","MAIL NOTIFY OFF");
    normalized(normal,"mail drop 3 confirm","MAIL DROP 3 CONFIRM");
    normalized(normal,"memberprofile Alice location Tokyo","MEMBERPROFILE Alice LOCATION Tokyo");
    normalized(normal,"applicationset Alice handle MiXeD","APPLICATIONSET Alice HANDLE MiXeD");
    normalized(normal,"fileboardoptions path|menu 3","FILEBOARDOPTIONS path|MENU 3");
    normalized(normal,"boardpass 2 none confirm","BOARDPASS 2 NONE CONFIRM");
    normalized(normal,"profilevis address private","PROFILEVIS address PRIVATE");
    normalized(normal,"withdraw 1 confirm","WITHDRAW 1 CONFIRM");
    normalized(normal,"post confirm","POST confirm");
    normalized(normal,"//cancel","//cancel");
    ClassicInput secret={.secret=1};normalized(secret,"PaSs日本語","PaSs日本語");
    normalized(secret,"/cancel","/CANCEL");normalized(secret,"/cancel extra","/cancel extra");
    ClassicInput memo={.memo=1};normalized(memo,"this is MiXeD","this is MiXeD");
    normalized(memo,"/cancel","/CANCEL");
    ClassicInput selection={.selection=1,.classic=1};normalized(selection,"match *.lzh","MATCH *.LZH");
    ClassicInput application={.application=1};normalized(application,"My Address","My Address");
    ClassicInput dialog={.classic=1};normalized(dialog,"MiXeD Path","MiXeD Path");
    ClassicInput editor={.editing=1};normalized(editor,"MiXeD body","MiXeD body");
    normalized(editor,"/title MiXeD","/TITLE MiXeD");normalized(editor,"/editor line","/EDITOR LINE");
    normalized(editor,"//help","//help");editor.line_editor=1;
    normalized(editor,"l3-5","L3-5");normalized(editor,"s MiXeD","S MiXeD");
    editor.editor_input=1;normalized(editor,"lower body","lower body");
    ClassicInput chat={.chatting=1};normalized(chat,"hello World","hello World");
    normalized(chat,"/talk Bob","/TALK Bob");normalized(chat,"/notify off","/NOTIFY OFF");
    ClassicInput mail={.mail_mode=1};normalized(mail,"drop 3 confirm","DROP 3 CONFIRM");
    /* Echo decisions use bounded, non-NUL-terminated prefixes. */
    const char prefix[]={'l','a','n','g',' ','e','n','!'};
    assert(classic_input_uppercase(&normal,prefix,0));
    assert(classic_input_uppercase(&normal,prefix,5));
    const char post[]={'p','o','s','t',' ','x'};
    assert(!classic_input_uppercase(&normal,post,5));
    assert(!classic_input_uppercase(&secret,prefix,3));
    assert(!classic_input_uppercase(&editor,"//",2));
    ClassicInput blocked[]={ {.input=1},{.mail_input=1},{.paging=1},{.binary=1},{.transfer=1},{.memo=1},{.application=1},{.classic=1} };
    for(size_t i=0;i<sizeof(blocked)/sizeof(*blocked);i++)assert(!classic_input_uppercase(&blocked[i],prefix,3));
}
static void dimensions(void) {
    TerminalDimensions size=terminal_dimensions(20,5);assert(size.columns==20 && size.rows==5);
    size=terminal_dimensions(240,100);assert(size.columns==240 && size.rows==100);
    size=terminal_dimensions(19,101);assert(size.columns==80 && size.rows==24);
    size=terminal_dimensions(241,4);assert(size.columns==80 && size.rows==24);
    size=terminal_dimensions(0,0);assert(size.columns==80 && size.rows==24);
    size=terminal_dimensions(100,0);assert(size.columns==100 && size.rows==24);
}
static void pages(void) {
    size_t offset=0;char out[128];
    assert(terminal_pager_render("a\nb\nc\n",&offset,20,5,out,sizeof(out))==1);
    assert(!strcmp(out,"a\r\nb\r\n") && offset==4);
    assert(!terminal_pager_render("a\nb\nc\n",&offset,20,5,out,sizeof(out)));
    assert(!strcmp(out,"c\r\n") && offset==6);
    offset=0;assert(!terminal_pager_render("a\r\nb\rc",&offset,20,24,out,sizeof(out)) && !strcmp(out,"a\r\nb\r\nc"));
    offset=0;assert(!terminal_pager_render("12345678901234567890",&offset,20,5,out,sizeof(out)));
    assert(!strcmp(out,"1234567890123456789\r\n0"));
    offset=0;assert(!terminal_pager_render("日本語日本語日本語日",&offset,20,5,out,sizeof(out)));
    assert(!strcmp(out,"日本語日本語日本語\r\n日"));
    offset=0;assert(!terminal_pager_render("1234567890123456789\xcc\x81x",&offset,20,5,out,sizeof(out)));
    assert(!strcmp(out,"1234567890123456789\xcc\x81\r\nx"));
    offset=0;assert(!terminal_pager_render("\xf0\x9f\x98\x80",&offset,20,5,out,5) && offset==4);
    assert(!strcmp(out,"\xf0\x9f\x98\x80"));
    offset=0;assert(!terminal_pager_render("\xe3\x81",&offset,20,5,out,sizeof(out)) && offset==2);
    offset=0;assert(!terminal_pager_render("",&offset,0,0,out,sizeof(out)) && !*out);
    /* Insufficient capacity never consumes input or overwrites guards. */
    for(size_t cap=0;cap<5;cap++) {
        char guard[10];memset(guard,'!',sizeof(guard));offset=0;
        assert(terminal_pager_render("日本語",&offset,80,24,guard+1,cap)==-1 && !offset);
        assert(guard[0]=='!' && guard[cap+1]=='!');
    }
    offset=0;char joined[32]="";
    do { int more=terminal_pager_render("日本語ABC",&offset,80,24,out,5);assert(more>=0);strcat(joined,out);if(!more)break; } while(1);
    assert(!strcmp(joined,"日本語ABC"));
    size_t other=0;offset=0;assert(terminal_pager_render("a\nb\nc",&offset,20,5,out,sizeof(out))==1);
    assert(!terminal_pager_render("other",&other,80,24,out,sizeof(out)) && other==5 && offset==4);
}
static void selection(void) {
    const char *yes[]={"ARTICLE 1","PREVIEW body","LIST 1","NOTES 1","INFO 1","FILES","FILEHISTORY","BOARDS\r\n1","MEMBERS\r\n","PROFILE alice","BOARDACCESS"};
    const char *no[]={"ERR denied","PONG","CONNECTOR STATE","BINARY READY","MAIL ARRIVED","BOARDS","ARTICLE",""};
    for(size_t i=0;i<sizeof(yes)/sizeof(*yes);i++)assert(terminal_pager_response(yes[i]));
    for(size_t i=0;i<sizeof(no)/sizeof(*no);i++)assert(!terminal_pager_response(no[i]));
}
int main(void) {
    inputs();dimensions();pages();selection();
    puts("PASS terminal policy: syntax/data case boundaries, secret/editor/dialog isolation, dimensions, UTF-8 paging, capacity and response selection");return 0;
}

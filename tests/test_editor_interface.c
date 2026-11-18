#include "editor_commands.h"
#include "editor_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { char title[81],body[CONTENT_BODY+1],undo_title[81],undo_body[CONTENT_BODY+1];int undo; } Fixture;
static Fixture f,before;
static EditorDocument d={f.title,f.body,f.undo_title,f.undo_body,&f.undo};
static void draft_commands(void) {
    struct { const char *text;DraftCommandKind kind;EditorOperation op;unsigned line;const char *payload; } cases[]={
        {"/UNDO",DRAFT_MUTATION,EDITOR_UNDO,0,NULL},{"/LINES",DRAFT_LINES,0,0,NULL},
        {"/TITLE Mixed Case",DRAFT_MUTATION,EDITOR_SET_TITLE,0,"Mixed Case"},
        {"/TITLE",DRAFT_MUTATION,EDITOR_SET_TITLE,0,""},
        {"/INSERT 1",DRAFT_MUTATION,EDITOR_INSERT,1,""},
        {"/REPLACE 02  text",DRAFT_MUTATION,EDITOR_REPLACE,2," text"},
        {"/DELETE 2147483647",DRAFT_MUTATION,EDITOR_DELETE,2147483647u,""},
        {"/DELETE 2147483648",DRAFT_INVALID,0,0,NULL},
        {"/DELETE 1 ",DRAFT_INVALID,0,0,NULL},{"/INSERT 0 bad",DRAFT_INVALID,0,0,NULL},
        {"/REPLACE 1x",DRAFT_INVALID,0,0,NULL},{"/INSERT",DRAFT_INVALID,0,0,NULL},
        {"/DELETE -1",DRAFT_INVALID,0,0,NULL},{"/REPLACE  1 x",DRAFT_INVALID,0,0,NULL},
        {"/UNDO extra",DRAFT_UNHANDLED,0,0,NULL},{"/LINESx",DRAFT_UNHANDLED,0,0,NULL},
        {"/TITLE\tvalue",DRAFT_UNHANDLED,0,0,NULL},{"/insert 1 text",DRAFT_UNHANDLED,0,0,NULL},
        {"ordinary text",DRAFT_UNHANDLED,0,0,NULL}
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        DraftCommand q=editor_draft_command(cases[i].text);assert(q.kind==cases[i].kind);
        if(q.kind==DRAFT_MUTATION) { assert(q.operation==cases[i].op && q.line==cases[i].line);if(cases[i].payload)assert(!strcmp(q.text,cases[i].payload)); }
    }
    assert(editor_command_word("/SAVE draft","/SAVE"));assert(!editor_command_word("/SAVED","/SAVE"));
    memset(&f,0,sizeof(f));strcpy(f.title,"Original");
    DraftCommand q=editor_draft_command("/INSERT 1 Kept Case");
    assert(editor_apply(&d,q.operation,q.line,q.line,q.text)==EDITOR_OK);
    assert(!strcmp(f.body,"Kept Case\r\n") && f.undo);
    before=f;q=editor_draft_command("/DELETE 2");
    assert(editor_apply(&d,q.operation,q.line,q.line,q.text)==EDITOR_LINE && !memcmp(&f,&before,sizeof(f)));
    q=editor_draft_command("/UNDO");assert(editor_apply(&d,q.operation,0,0,q.text)==EDITOR_OK && !*f.body);
}
static void line_commands(void) {
    struct { const char *text;LineCommandKind kind;unsigned first,last;const char *query; } cases[]={
        {"L",LINE_LIST,1,4,NULL},{"l2-",LINE_LIST,2,4,NULL},{"L-3",LINE_LIST,1,3,NULL},
        {"E",LINE_EDIT,3,3,NULL},{"I5",LINE_INSERT,5,5,NULL},{"D2-4",LINE_DELETE,2,4,NULL},
        {"Q",LINE_QUOTE,3,3,NULL},{"2",LINE_SELECT,2,0,NULL},{"N",LINE_NEW,0,0,NULL},
        {"u",LINE_UPLOAD,0,0,NULL},{"A",LINE_APPEND,0,0,NULL},{"unknown",LINE_HELP,0,0,NULL},
        {"S",LINE_SEARCH,1,4,""},{"s2-3 Mixed Case",LINE_SEARCH,2,3,"Mixed Case"},
        {"S2-   Two Words",LINE_SEARCH,2,4,"Two Words"},{"S2",LINE_SEARCH,2,2,""},
        {"S  all",LINE_SEARCH,1,4,"all"},{"S-4 a",LINE_INVALID,0,0,NULL},
        {"L 1",LINE_INVALID,0,0,NULL},{"D4-2",LINE_INVALID,0,0,NULL},
        {"E5",LINE_INVALID,0,0,NULL},{"I6",LINE_INVALID,0,0,NULL},
        {"L2x",LINE_INVALID,0,0,NULL},{"0",LINE_INVALID,0,0,NULL},
        {"2147483648",LINE_INVALID,0,0,NULL},{"S1-5 a",LINE_INVALID,0,0,NULL},
        {"S1x a",LINE_INVALID,0,0,NULL},{"S1-2x a",LINE_INVALID,0,0,NULL}
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        LineCommand q=editor_line_command(cases[i].text,4,3);assert(q.kind==cases[i].kind);
        if(q.kind!=LINE_INVALID && q.kind!=LINE_HELP) { assert(q.first==cases[i].first && q.last==cases[i].last);if(cases[i].query)assert(!strcmp(q.query,cases[i].query)); }
    }
    assert(editor_line_command("E",4,0).first==1);
    assert(editor_line_command("E",4,99).first==1);
    assert(editor_line_command("S",0,1).kind==LINE_INVALID);
    assert(editor_line_command("L999",0,1).kind==LINE_LIST); /* existing empty-list special case */
    assert(editor_line_command("I",0,1).kind==LINE_INSERT);
}
typedef struct { char bytes[2048];size_t used;unsigned calls;int fail; } Sink;
static int write_line(void *context,const char *text) {
    Sink *s=context;s->calls++;if(s->fail)return 0;
    size_t n=strlen(text);assert(n<sizeof(s->bytes)-s->used);memcpy(s->bytes+s->used,text,n+1);s->used+=n;return 1;
}
static void views(void) {
    memset(&f,0,sizeof(f));strcpy(f.title,"Mixed");strcpy(f.body,"alpha alpha\r\nBeta\r\nalpha\r\n");before=f;
    char out[1024];const char *expected="DRAFT LINES 3\r\nTITLE Mixed\r\nLINE 1 alpha alpha\r\nLINE 2 Beta\r\nLINE 3 alpha\r\nEND DRAFT\r\n";
    assert(editor_view_draft(&d,out,sizeof(out))==EDITOR_VIEW_OK && !strcmp(out,expected));
    for(size_t cap=1;cap<=strlen(expected);cap++)assert(editor_view_draft(&d,out,cap)==EDITOR_VIEW_CAPACITY && !*out);
    assert(editor_view_draft(&d,out,strlen(expected)+1)==EDITOR_VIEW_OK);
    assert(editor_view_draft(&d,out,0)==EDITOR_VIEW_CAPACITY);
    assert(editor_view_lines(&d,2,3,1,out,sizeof(out))==EDITOR_VIEW_OK && !strcmp(out,"2 Beta\r\n3 alpha\r\n"));
    assert(editor_view_lines(&d,1,1,0,out,sizeof(out))==EDITOR_VIEW_OK && !strcmp(out,"alpha alpha\r\n"));
    assert(editor_view_lines(&d,2,1,1,out,sizeof(out))==EDITOR_VIEW_INVALID && !*out);
    unsigned matches;Sink sink={0};
    assert(editor_view_search(&d,1,2,"alpha",1,1,write_line,&sink,&matches)==EDITOR_VIEW_OK && matches==2 && sink.calls==1);
    assert(!strcmp(sink.bytes,"1 \033[33malpha\033[0m \033[33malpha\033[0m\r\n"));
    memset(&sink,0,sizeof(sink));assert(editor_view_search(&d,2,3,"alpha",0,0,write_line,&sink,&matches)==EDITOR_VIEW_OK && matches==1 && !strcmp(sink.bytes,"alpha\r\n"));
    memset(&sink,0,sizeof(sink));assert(editor_view_search(&d,1,3,"Alpha",1,1,write_line,&sink,&matches)==EDITOR_VIEW_OK && !matches && !sink.calls);
    assert(editor_view_search(&d,1,3,"",1,1,write_line,&sink,&matches)==EDITOR_VIEW_INVALID && !sink.calls);
    assert(editor_view_search(&d,1,4,"alpha",1,1,write_line,&sink,&matches)==EDITOR_VIEW_INVALID && !sink.calls);
    sink.fail=1;assert(editor_view_search(&d,1,3,"alpha",1,1,write_line,&sink,&matches)==EDITOR_VIEW_OUTPUT && sink.calls==1);
    assert(!memcmp(&f,&before,sizeof(f))); /* Rendering never changes document or Undo. */
    strcpy(f.body,"aaaa\r\n\xe6\x97\xa5\xe6\x9c\xac \xe6\x97\xa5\xe6\x9c\xac\r\n");
    memset(&sink,0,sizeof(sink));assert(editor_view_search(&d,1,1,"aa",0,0,write_line,&sink,&matches)==EDITOR_VIEW_OK && matches==2);
    memset(&sink,0,sizeof(sink));assert(editor_view_search(&d,2,2,"\xe6\x97\xa5\xe6\x9c\xac",0,1,write_line,&sink,&matches)==EDITOR_VIEW_OK && matches==2 && sink.calls==1);
    strcpy(f.body,"alpha\r\nbroken\n");memset(&sink,0,sizeof(sink));
    assert(editor_view_search(&d,1,1,"alpha",1,1,write_line,&sink,&matches)==EDITOR_VIEW_INVALID && !sink.calls && !matches);
    assert(editor_view_draft(&d,out,sizeof(out))==EDITOR_VIEW_INVALID && !*out);
    *f.body=0;assert(editor_view_draft(&d,out,sizeof(out))==EDITOR_VIEW_OK && !strcmp(out,"DRAFT LINES 0\r\nTITLE Mixed\r\nEND DRAFT\r\n"));
}
static int count_line(void *context,const char *text) { size_t *bytes=context;*bytes+=strlen(text);return 1; }
static void maximum(void) {
    memset(&f,0,sizeof(f));char *out=malloc(CONTENT_DISPLAY);assert(out);
    for(size_t n=0;n<CONTENT_BODY;n+=2) { f.body[n]='\r';f.body[n+1]='\n'; }
    assert(editor_view_draft(&d,out,CONTENT_DISPLAY)==EDITOR_VIEW_OK && strstr(out,"LINE 131072 \r\nEND DRAFT\r\n"));
    memset(f.body,'x',CONTENT_BODY-2);f.body[CONTENT_BODY-2]='\r';f.body[CONTENT_BODY-1]='\n';
    unsigned matches=0;size_t bytes=0;
    assert(editor_view_search(&d,1,1,"x",0,1,count_line,&bytes,&matches)==EDITOR_VIEW_OK);
    assert(matches==CONTENT_BODY-2 && bytes==(CONTENT_BODY-2)*10u+2u);
    free(out);
}
int main(void) {
    draft_commands();line_commands();views();maximum();
    puts("PASS: draft/line command grammar, Core mutation/Undo, exact listing and search, UTF-8, ranges, output failure, readonly and maximum-size documents");return 0;
}

#include "editor_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef struct { char title[81],body[CONTENT_BODY+1],undo_title[81],undo_body[CONTENT_BODY+1];int undo; } Fixture;
static Fixture fixture,before;
static EditorDocument d={fixture.title,fixture.body,fixture.undo_title,fixture.undo_body,&fixture.undo};
static void reset(void) { memset(&fixture,0,sizeof(fixture));strcpy(fixture.title,"original"); }
static void clean(const char *s,size_t n) { for(size_t i=0;i<n;i++)assert(s[i]==0); }
static void reject(EditorOperation op,uint32_t first,uint32_t last,const char *text,EditorOutcome error) {
    before=fixture;assert(editor_apply(&d,op,first,last,text)==error);assert(!memcmp(&before,&fixture,sizeof(fixture)));
}
static void lines_and_undo(void) {
    reset();reject(EDITOR_UNDO,0,0,NULL,EDITOR_NO_UNDO);
    assert(editor_apply(&d,EDITOR_INSERT,1,0,"middle")==EDITOR_OK);
    assert(editor_apply(&d,EDITOR_INSERT,1,0,"first")==EDITOR_OK);
    assert(editor_apply(&d,EDITOR_INSERT,3,0,"last")==EDITOR_OK);
    assert(!strcmp(d.body,"first\r\nmiddle\r\nlast\r\n"));
    assert(editor_apply(&d,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && !fixture.undo);
    assert(!strcmp(d.body,"first\r\nmiddle\r\n"));clean(d.undo_body,CONTENT_BODY+1);clean(d.undo_title,81);
    reject(EDITOR_UNDO,0,0,NULL,EDITOR_NO_UNDO);
    assert(editor_apply(&d,EDITOR_REPLACE,1,0,"changed")==EDITOR_OK);
    before=fixture;assert(editor_apply(&d,EDITOR_REPLACE,1,0,"changed")==EDITOR_OK);assert(!memcmp(&fixture,&before,sizeof(fixture)));
    assert(editor_apply(&d,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && !strcmp(d.body,"first\r\nmiddle\r\n"));
    reject(EDITOR_DELETE,3,3,NULL,EDITOR_LINE);reject(EDITOR_INSERT,4,0,"no",EDITOR_LINE);
    reject(EDITOR_INSERT,0,0,"no",EDITOR_LINE);reject(EDITOR_DELETE,2,1,NULL,EDITOR_LINE);
    assert(editor_apply(&d,EDITOR_DELETE,1,2,NULL)==EDITOR_OK && !*d.body);clean(d.body,CONTENT_BODY+1);
    assert(editor_apply(&d,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && !strcmp(d.body,"first\r\nmiddle\r\n"));
}
static void title_and_clear(void) {
    reset();assert(editor_apply(&d,EDITOR_APPEND_LINE,0,0,"one")==EDITOR_OK);
    assert(editor_apply(&d,EDITOR_SET_TITLE,0,0,"new")==EDITOR_OK);
    before=fixture;assert(editor_apply(&d,EDITOR_SET_TITLE,0,0,"new")==EDITOR_OK);assert(!memcmp(&fixture,&before,sizeof(fixture)));
    reject(EDITOR_SET_TITLE,0,0,"",EDITOR_TITLE_INVALID);reject(EDITOR_SET_TITLE,0,0,"bad\t",EDITOR_TITLE_INVALID);
    char too_long[82];memset(too_long,'a',81);too_long[81]=0;reject(EDITOR_SET_TITLE,0,0,too_long,EDITOR_TITLE_INVALID);
    assert(editor_apply(&d,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && !strcmp(d.title,"original") && !strcmp(d.body,"one\r\n"));
    assert(editor_apply(&d,EDITOR_CLEAR,0,0,NULL)==EDITOR_OK);clean(d.body,CONTENT_BODY+1);
    assert(editor_apply(&d,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && !strcmp(d.body,"one\r\n"));
    assert(editor_apply(&d,EDITOR_CLEAR,0,0,NULL)==EDITOR_OK);editor_discard_undo(&d);
    reject(EDITOR_UNDO,0,0,NULL,EDITOR_NO_UNDO);clean(d.undo_body,CONTENT_BODY+1);
}
static void quote_and_import(void) {
    reset();assert(editor_apply(&d,EDITOR_APPEND_BLOCK,0,0,"one\r\n\r\nthree\r\nfour\r\n")==EDITOR_OK);
    assert(editor_apply(&d,EDITOR_QUOTE,2,3,NULL)==EDITOR_OK && !strcmp(d.body,"one\r\n> \r\n> three\r\nfour\r\n"));
    assert(editor_apply(&d,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && !strcmp(d.body,"one\r\n\r\nthree\r\nfour\r\n"));
    assert(editor_apply(&d,EDITOR_APPEND_BLOCK,0,0,"tail\r\n")==EDITOR_OK);
    assert(editor_apply(&d,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && !strcmp(d.body,"one\r\n\r\nthree\r\nfour\r\n"));
    reject(EDITOR_APPEND_BLOCK,0,0,"unterminated",EDITOR_INVALID);
    reject(EDITOR_APPEND_LINE,0,0,"two\nlines",EDITOR_CONTROL);
    reject(EDITOR_REPLACE,1,0,"tab\t",EDITOR_CONTROL);
}
static void bounds_and_capacity(void) {
    reset();memset(d.body,'a',CONTENT_BODY-2);memcpy(d.body+CONTENT_BODY-2,"\r\n",3);
    EditorInfo info;assert(editor_inspect(&d,&info)==EDITOR_OK && info.bytes==CONTENT_BODY && info.lines==1);
    reject(EDITOR_APPEND_LINE,0,0,"",EDITOR_LIMIT);reject(EDITOR_QUOTE,1,1,NULL,EDITOR_LIMIT);
    reject(EDITOR_APPEND_BLOCK,0,0,"\r\n",EDITOR_LIMIT);
    assert(editor_apply(&d,EDITOR_REPLACE,1,0,"small")==EDITOR_OK);clean(d.body+7,CONTENT_BODY+1-7);
    assert(editor_apply(&d,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && strlen(d.body)==CONTENT_BODY);
    reset();for(size_t i=0;i<CONTENT_BODY;i+=2)memcpy(d.body+i,"\r\n",2);
    assert(editor_inspect(&d,&info)==EDITOR_OK && info.lines==CONTENT_BODY/2);
    assert(editor_apply(&d,EDITOR_DELETE,1,CONTENT_BODY/2,NULL)==EDITOR_OK && !*d.body);
    assert(editor_apply(&d,EDITOR_APPEND_LINE,0,0,"日本語")==EDITOR_OK);
    assert(editor_inspect(&d,&info)==EDITOR_OK && info.bytes==strlen("日本語")+2);
}
static void spans_and_search(void) {
    reset();strcpy(d.body,"banana banana\r\nnext\r\n日本語 日本語\r\n");
    EditorInfo info;assert(editor_inspect(&d,&info)==EDITOR_OK && info.lines==3);
    EditorSpan span;assert(editor_range(&d,2,3,&span)==EDITOR_OK && span.offset==15 && span.size==strlen("next\r\n日本語 日本語\r\n"));
    assert(editor_range(&d,0,3,&span)==EDITOR_LINE);assert(!span.text && !span.size);
    size_t at=0;assert(editor_next_line(d.body,info.bytes,&at,&span)==1 && span.size==13 && at==15);
    EditorMatch hit;size_t search=0;
    assert(editor_find_next(&span,"ana",&search,&hit)==1 && hit.offset==1 && hit.size==3);
    assert(editor_find_next(&span,"ana",&search,&hit)==1 && hit.offset==8);
    assert(editor_find_next(&span,"ana",&search,&hit)==0 && !hit.size);
    search=0;assert(editor_find_next(&span,"BANANA",&search,&hit)==0);
    search=0;assert(editor_find_next(&span,"banana\r\nnext",&search,&hit)==0);
    search=0;assert(editor_find_next(&span,"",&search,&hit)==-1 && !hit.size);
    assert(editor_next_line(d.body,info.bytes,&at,&span)==1);
    assert(editor_next_line(d.body,info.bytes,&at,&span)==1);search=0;
    assert(editor_find_next(&span,"日本語",&search,&hit)==1 && hit.offset==0);
    assert(editor_find_next(&span,"日本語",&search,&hit)==1 && hit.offset==strlen("日本語 "));
    assert(editor_next_line(d.body,info.bytes,&at,&span)==0 && !span.text);
}
static void malformed_and_isolation(void) {
    reset();strcpy(d.body,"missing terminator");EditorInfo info={99,99};
    assert(editor_inspect(&d,&info)==EDITOR_INVALID && !info.bytes && !info.lines);
    reject(EDITOR_DELETE,1,1,NULL,EDITOR_INVALID);
    strcpy(d.body,"bad\n");assert(editor_inspect(&d,&info)==EDITOR_INVALID);
    memset(d.body,'x',CONTENT_BODY+1);assert(editor_inspect(&d,&info)==EDITOR_INVALID);
    reset();assert(editor_apply(&d,EDITOR_APPEND_LINE,0,0,"private")==EDITOR_OK);
    static Fixture other;memset(&other,0,sizeof(other));strcpy(other.title,"other");
    EditorDocument second={other.title,other.body,other.undo_title,other.undo_body,&other.undo};
    assert(editor_apply(&second,EDITOR_APPEND_LINE,0,0,"separate")==EDITOR_OK);
    before=fixture;assert(editor_apply(&second,EDITOR_UNDO,0,0,NULL)==EDITOR_OK && !*other.body);assert(!memcmp(&fixture,&before,sizeof(fixture)));
    strcpy(d.undo_body,"bad");reject(EDITOR_UNDO,0,0,NULL,EDITOR_INVALID);
    assert(editor_apply(NULL,EDITOR_CLEAR,0,0,NULL)==EDITOR_INVALID);
}
int main(void) {
    lines_and_undo();title_and_clear();quote_and_import();bounds_and_capacity();spans_and_search();malformed_and_isolation();
    puts("PASS: 6 editor Core groups (mutation/undo, title/clear, quote/import, capacity, search, isolation)");return 0;
}

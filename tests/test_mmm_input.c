#include "mmm_input.h"
#include "editor_core.h"
#include "terminal.h"
#include <assert.h>
#include <string.h>

static MmmInput parse(const char *text,int member) {
    static char copy[128];
    strcpy(copy,text);
    return mmm_parse(copy,member);
}

static int sink(void *ctx,const unsigned char *data,size_t size) {
    (void)ctx;(void)data;(void)size;
    return 1;
}

int main(void) {
    assert(parse("notes",0).command==MMM_UNKNOWN);
    assert(parse("mail",0).command==MMM_UNKNOWN);
    assert(parse("filer",0).command==MMM_UNKNOWN);
    assert(parse("new",1).command==MMM_NEW);
    assert(parse("se",1).command==MMM_SEQUENCER);
    assert(parse("fi Manual/Downloads",1).command==MMM_FILER);
    assert(parse("  12  ",1).number==12);
    assert(parse("lo",1).command==MMM_AMBIGUOUS);
    assert(parse("login",1).command==MMM_LOGIN);
    MmmInput open=parse("OP -i Music.Synth",1);
    assert(open.command==MMM_OPEN && !strcmp(open.argument,"-i Music.Synth"));
    assert(parse("open -?",1).usage);
    assert(parse("4294967296",1).command==MMM_UNKNOWN);
    uint32_t first,last;
    int except;
    const char *rest;
    assert(mmm_range("! 2 , $ word new",3,&first,&last,&except,&rest));
    assert(first==2 && last==3 && except && !strcmp(rest,"word new"));
    assert(!mmm_range("3,2",3,&first,&last,&except,&rest));

    static char title[81],prior_title[81],body[CONTENT_BODY+1],prior[CONTENT_BODY+1];
    int undo=0;
    EditorDocument doc={title,body,prior_title,prior,&undo};
    strcpy(body,"alpha abc\r\nalpha def\r\nalpha ghi\r\n");
    assert(editor_transform(&doc,2,3,1,"alpha","short")==EDITOR_OK);
    assert(!strcmp(body,"short abc\r\nalpha def\r\nalpha ghi\r\n"));
    assert(editor_apply(&doc,EDITOR_UNDO,0,0,NULL)==EDITOR_OK);
    assert(editor_transform(&doc,2,2,1,NULL,NULL)==EDITOR_OK);
    assert(!strcmp(body,"alpha def\r\n"));

    Terminal term;
    assert(terminal_init(&term,TERMINAL_PLAIN,sink,NULL));
    term.keys=1;
    assert(terminal_receive(&term,'f',0)==TERMINAL_KEY && term.key=='f');
    assert(terminal_receive(&term,'F',0)==TERMINAL_KEY && term.key=='F');
    assert(!terminal_receive(&term,27,0));
    assert(!terminal_receive(&term,'[',0));
    assert(!terminal_receive(&term,'A',0));
    assert(terminal_receive(&term,'\r',0)==TERMINAL_KEY);
    term.keys=0;
    assert(!terminal_receive(&term,'\n',0));
    assert(!terminal_receive(&term,'a',1));
    assert(terminal_receive(&term,'\r',1)==TERMINAL_LINE && term.size==1);
    return 0;
}

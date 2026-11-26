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
    MashInput wire=mash_parse("  !\t12 Keep My Case");
    assert(wire.command==MASH_WIRE && !strcmp(wire.argument,"12 Keep My Case"));
    assert(mash_parse("!uname -a").command==MASH_UNKNOWN);
    assert(mash_parse("SETUP -?").command==MASH_SETUP);
    assert(!strcmp(mash_parse("  readnews -?").usage,"readnews [-n]"));
    assert(mash_parse("readnew -n").command==MASH_UNKNOWN);
    assert(!strcmp(mash_menu_command(0,3),"setup"));
    assert(!strcmp(mash_menu_command(0,12),"readnews -n"));
    assert(!strcmp(mash_menu_command(0,14),"talk -n"));
    assert(!mash_menu_command(0,11) && !mash_menu_command(0,UINT32_MAX));
    assert(!strcmp(mash_menu_command(1,4),"games") && !mash_menu_command(1,5));
    assert(parse("bbs",1).command==MMM_UNKNOWN);
    assert(parse("notes",0).command==MMM_UNKNOWN);
    assert(parse("mail",0).command==MMM_UNKNOWN);
    assert(parse("filer",0).command==MMM_UNKNOWN);
    assert(parse("scanlist",0).command==MMM_UNKNOWN);
    assert(parse("withdraw 4 confirm",0).command==MMM_UNKNOWN);
    assert(parse("draft resume",1).command==MMM_DRAFT);
    assert(parse("retitle 1 corrected",1).command==MMM_RETITLE);
    assert(parse("manage",1).command==MMM_MANAGE);
    assert(parse("new",1).command==MMM_NEW);
    assert(parse("scanlist -d",1).command==MMM_SCANLIST);
    assert(parse("boardlabel",1).command==MMM_BOARDLABEL);
    assert(parse("se",1).command==MMM_SEQUENCER);
    assert(parse("fi Manual/Downloads",1).command==MMM_FILER);
    assert(parse("  12  ",1).number==12);
    assert(parse("lo",1).command==MMM_AMBIGUOUS);
    assert(parse("login",1).command==MMM_LOGIN);
    MmmInput open=parse("OP -i Music.Synth",1);
    assert(open.command==MMM_OPEN && !strcmp(open.argument,"-i Music.Synth"));
    assert(parse("open -?",1).usage);
    assert(parse("4294967296",1).command==MMM_UNKNOWN);
    MmmOpen spec;
    char open_new[]="-S @Synth",open_or[]="-x\tRetro music",open_bad[]="-s -x #3",single[]="-i -b42 #3";
    assert(mmm_open_parse(open_new,&spec) && spec.mode==MMM_OPEN_NEW && !strcmp(spec.board,"@Synth"));
    assert(mmm_open_parse(open_or,&spec) && spec.mode==MMM_OPEN_NEW_OR_INDEX && !strcmp(spec.board,"Retro music"));
    assert(!mmm_open_parse(open_bad,&spec));
    assert(mmm_open_parse(single,&spec) && spec.mode==MMM_OPEN_ARTICLE && spec.article==42);
    char continuous[]="-n #4", continuous_all[]="-a -n @synth", all_only[]="-a #3", conflicting[]="-n -b4 #3";
    assert(mmm_open_parse(continuous,&spec) && spec.continuous && !spec.all && !strcmp(spec.board,"#4"));
    assert(mmm_open_parse(continuous_all,&spec) && spec.continuous && spec.all);
    assert(!mmm_open_parse(all_only,&spec));
    assert(!mmm_open_parse(conflicting,&spec));
    MmmScanTargets scan;
    char ordered[]="  #3\t@Synth 7 ",invalid[]="#3 @ #2";
    assert(mmm_scanlist_parse(ordered,&scan) && scan.count==3 && scan.targets[0].board==3 &&
           !strcmp(scan.targets[1].label,"Synth") && scan.targets[2].board==7);
    assert(!mmm_scanlist_parse(invalid,&scan));
    char pattern[]="-x @syn* -i #7 !#2",bad_mode[]="-x !#2";
    assert(mmm_scanlist_parse(pattern,&scan) && scan.count==3);
    assert(scan.targets[0].mode==MMM_SCAN_FALLBACK && !strcmp(scan.targets[0].label,"syn*"));
    assert(scan.targets[1].mode==MMM_SCAN_INDEX && scan.targets[1].board==7);
    assert(scan.targets[2].exclude && scan.targets[2].board==2);
    assert(!mmm_scanlist_parse(bad_mode,&scan));
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

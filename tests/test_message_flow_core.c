#include "message_flow_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void gather_tests(void) {
    GatherFlow g={0},other={0};char body[4096]="untouched";
    assert(gather_flow_complete(&g,GATHER_RESPONSE_PAGE,9)==GATHER_NO_EFFECT && !g.cursor);
    gather_flow_read_submitted(&g,1,0);assert(!g.pending);
    gather_flow_read_submitted(&g,1,1);assert(g.pending==2);
    assert(gather_flow_complete(&g,GATHER_RESPONSE_PAGE,9)==GATHER_NO_EFFECT);
    assert(g.mode==2 && g.cursor==9 && !g.pending);
    assert(gather_flow_input(&g,GATHER_INPUT_TEXT,"ignored",1,body,sizeof(body))==GATHER_NO_EFFECT);
    assert(!strcmp(body,"untouched"));
    assert(gather_flow_input(&g,GATHER_INPUT_NEXT,"",1,body,sizeof(body))==GATHER_NEXT_PAGE);
    gather_flow_read_submitted(&g,1,1);
    assert(gather_flow_complete(&g,GATHER_RESPONSE_OTHER,0)==GATHER_FAILED && g.cursor==9 && g.mode==2);
    assert(gather_flow_input(&g,GATHER_INPUT_QUIT,"Q",1,body,sizeof(body))==GATHER_CLOSED);
    assert(!g.mode && !g.cursor && !strcmp(body,"untouched"));
    gather_flow_read_submitted(&g,1,1);
    assert(gather_flow_complete(&g,GATHER_RESPONSE_PAGE,0)==GATHER_CLOSED && !g.mode);
    gather_flow_read_submitted(&g,0,1);
    assert(gather_flow_complete(&g,GATHER_RESPONSE_PAGE,9)==GATHER_NO_EFFECT);
    assert(gather_flow_input(&g,GATHER_INPUT_FINISH,"//",1,body,sizeof(body))==GATHER_EDIT);
    assert(g.mode==1 && !g.cursor && !g.lines);
    body[0]=0; /* Session clears its full draft on EDIT. */
    assert(gather_flow_input(&g,GATHER_INPUT_FINISH,"//",1,body,sizeof(body))==GATHER_EMPTY && !g.mode);
    gather_flow_read_submitted(&g,0,1);
    assert(gather_flow_complete(&g,GATHER_RESPONSE_PAGE,0)==GATHER_EDIT);
    assert(gather_flow_input(&g,GATHER_INPUT_TEXT,"invalid",0,body,sizeof(body))==GATHER_INVALID && !g.lines && !body[0]);
    assert(gather_flow_input(&g,GATHER_INPUT_NEXT,"",1,body,sizeof(body))==GATHER_NO_EFFECT && g.lines==1);
    assert(gather_flow_input(&g,GATHER_INPUT_QUIT,"Q",1,body,sizeof(body))==GATHER_NO_EFFECT);
    assert(gather_flow_input(&g,GATHER_INPUT_NEXT,"+",1,body,sizeof(body))==GATHER_NO_EFFECT);
    assert(!strcmp(body,"\r\nQ\r\n+\r\n"));
    for(unsigned n=4;n<=10;n++)assert(gather_flow_input(&g,GATHER_INPUT_TEXT,"text",1,body,sizeof(body))==(n==10?GATHER_POST:GATHER_NO_EFFECT));
    gather_flow_post_submitted(&g,0);assert(!g.pending && g.lines==10 && *body);
    gather_flow_post_submitted(&g,1);
    assert(gather_flow_complete(&g,GATHER_RESPONSE_OTHER,0)==GATHER_FAILED && g.mode==1 && g.lines==10 && *body);
    assert(gather_flow_input(&g,GATHER_INPUT_FINISH,"//",1,body,sizeof(body))==GATHER_POST);
    gather_flow_post_submitted(&g,1);
    assert(gather_flow_complete(&g,GATHER_RESPONSE_SAVED,0)==GATHER_SAVED && !g.mode && !g.lines);
    assert(gather_flow_complete(&g,GATHER_RESPONSE_SAVED,0)==GATHER_NO_EFFECT);
    g.mode=1;g.cursor=9;g.lines=2;
    assert(gather_flow_input(&g,GATHER_INPUT_CANCEL,"/CANCEL",1,body,sizeof(body))==GATHER_ABORTED);
    assert(!g.mode && !g.cursor && !g.lines && !other.pending && !other.mode);
    char small[4]="";g.mode=1;
    assert(gather_flow_input(&g,GATHER_INPUT_TEXT,"ab",1,small,sizeof(small))==GATHER_INVALID && !g.lines && !*small);
    assert(gather_flow_input(&g,GATHER_INPUT_TEXT,"a",1,small,sizeof(small))==GATHER_NO_EFFECT && !strcmp(small,"a\r\n"));
    assert(gather_flow_input(&g,GATHER_INPUT_NEXT,"",1,small,sizeof(small))==GATHER_INVALID && g.lines==1);
    assert(gather_flow_input(&g,GATHER_INPUT_FINISH,"//",1,small,sizeof(small))==GATHER_POST);
}
static void mail_tests(void) {
    SysopMailFlow m={0},other={0};char title[82];
    assert(!sysop_mail_title_valid(NULL) && !sysop_mail_title_valid(""));
    assert(sysop_mail_title_valid("Mixed title"));
    memset(title,'x',80);title[80]=0;assert(sysop_mail_title_valid(title));
    title[80]='x';title[81]=0;assert(!sysop_mail_title_valid(title));
    for(int c=1;c<32;c++){title[0]=(char)c;title[1]=0;assert(!sysop_mail_title_valid(title));}
    assert(!sysop_mail_title_valid("a\177b"));
    assert(sysop_mail_ready(&m,0));
    sysop_mail_submitted(&m,0);assert(sysop_mail_complete(&m,1,10)==SYSOP_MAIL_NONE && !m.after);
    sysop_mail_submitted(&m,1);assert(sysop_mail_complete(&m,0,20)==SYSOP_MAIL_RETRY && !m.pending && !m.after);
    sysop_mail_submitted(&m,1);assert(sysop_mail_complete(&m,1,30)==SYSOP_MAIL_SENT && m.after==30030 && !m.pending);
    assert(!sysop_mail_ready(&m,30029) && sysop_mail_ready(&m,30030) && sysop_mail_ready(&m,30031));
    assert(sysop_mail_complete(&m,1,40)==SYSOP_MAIL_NONE && m.after==30030);
    assert(!other.pending && !other.after && sysop_mail_ready(&other,30));
}
int main(void) { gather_tests();mail_tests();puts("PASS: Gather navigation/input/retry/capacity and SYSOPMAIL title/completion/cooldown isolation");return 0; }

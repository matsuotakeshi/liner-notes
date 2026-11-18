#include "list_navigation_core.h"
#include "list_navigation_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void reports(void) {
    const char *names[]={"","BOARDLOG","USERLOG","USERTABLE","FILEHISTORY","FILESTATS","BOARDACCESS","FILETYPE"};
    unsigned cases=0;
    for(int kind=REPORT_BOARDLOG;kind<=REPORT_FILETEXT;kind++)for(int all=0;all<2;all++) {
        ReportNavigation r={.next=8};char actual[96],expected[96];
        report_navigation_begin(&r,(ReportKind)kind,42,all);
        assert(r.next==8 && r.pending);
        report_navigation_complete(&r,1,19);assert(!r.pending && r.next==19);
        if(kind==REPORT_BOARDLOG)strcpy(expected,"BOARDLOG 19");
        else snprintf(expected,sizeof(expected),all?"%s ALL 19":"%s 42 19",names[kind]);
        assert(report_navigation_command(&r,actual,sizeof(actual)) && !strcmp(actual,expected));
        for(size_t n=0;n<=strlen(expected);n++) {
            strcpy(actual,"sentinel");assert(!report_navigation_command(&r,actual,n));
            if(n)assert(!actual[0]);else assert(!strcmp(actual,"sentinel"));
        }
        r.pending=1;
        int affected=report_navigation_access_refresh(&r);
        assert(affected==(kind==REPORT_BOARDACCESS) && r.pending);
        if(affected)assert(r.kind==REPORT_NONE && !r.next);
        else assert(r.kind==(ReportKind)kind && r.next==19);
        report_navigation_clear(&r);assert(r.kind==REPORT_NONE && !r.next && r.pending);
        report_navigation_complete(&r,1,23);assert(r.kind==REPORT_NONE && r.next==23 && !r.pending);
        assert(!report_navigation_command(&r,actual,sizeof(actual)) && !actual[0]);cases++;
    }
    assert(cases==14);
    struct {const char *text;int allow,found;uint32_t next;} rows[]={
        {"HEADER\r\nEND NEXT 42\r\n",0,1,42},
        {"END NEXT 0",1,1,0},{"ERR denied\r\n",1,0,0},
        {"END NEXT wrong",1,1,0},{"TEXT\r\nEND TEXT NEXT 17",0,0,0},
        {"TEXT\r\nEND TEXT NEXT 17",1,1,17},
        {"TEXT\r\nEND TEXT NEXT 17\r\nEND NEXT 22",1,1,22},
        {"END NEXT 4294967295",1,1,UINT32_MAX},{"END NEXT 9 suffix",0,1,9}
    };
    for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++) {
        ListContinuation p=list_continuation_parse(rows[i].text,rows[i].allow);
        assert(p.found==rows[i].found && p.next==rows[i].next);
        ReportNavigation r={.kind=REPORT_FILETEXT,.pending=1,.next=99};
        report_navigation_complete(&r,p.found,p.next);
        assert(!r.pending && r.next==p.next && r.kind==(p.found?REPORT_FILETEXT:REPORT_NONE));
    }
}
static void profiles(void) {
    ProfileWalk w={.active=1},other={0};char query[81];memset(query,'x',80);query[80]=0;
    assert(sizeof(w)<256);
    profile_walk_begin(&w,4,0,query);query[0]='z';
    assert(w.active && w.pending && w.query.query[0]=='x' && strlen(w.query.query)==80);
    profile_walk_complete(&w,999999);assert(w.active && !w.pending && w.query.cursor==999999);
    ProfileWalkQuery plan=profile_walk_next(&w);assert(w.pending && plan.cursor==999999 && plan.field==4);
    plan.query[0]='y';assert(w.query.query[0]=='x');
    profile_walk_complete(&w,0);assert(!w.active && !w.pending && w.query.cursor==999999);
    assert(!other.active && !other.pending && !other.query.query[0]);
    w.active=1;profile_walk_begin(&w,100,100,"*");
    plan=profile_walk_next(&w);assert(plan.field==100 && plan.group==100 && !plan.cursor && !strcmp(plan.query,"*"));
    profile_walk_reset(&w);const unsigned char *p=(const unsigned char *)&w;
    for(size_t i=0;i<sizeof(w);i++)assert(!p[i]);
}
static void vaults(void) {
    VaultNavigation v={0},other={0};char parent[49];
    assert(!strcmp(vault_navigation_path(&v),"-") && !vault_navigation_parent(&v,parent));
    strcpy(v.path,"public/sub");v.child_view=1;v.count=3;v.next=17;
    vault_navigation_begin(&v,"private");assert(v.selecting && !strcmp(v.path,"public/sub"));
    assert(!vault_navigation_complete(&v,0) && !v.selecting && !strcmp(v.path,"public/sub") && v.count==3);
    assert(!vault_navigation_complete(&v,1) && !strcmp(v.path,"public/sub"));
    vault_navigation_begin(&v,"public/child");assert(vault_navigation_complete(&v,1));
    assert(!v.child_view && !v.count && v.next==17 && !strcmp(v.path,"public/child"));
    assert(vault_navigation_parent(&v,parent) && !strcmp(parent,"public"));
    strcpy(v.path,"public");assert(vault_navigation_parent(&v,parent) && !strcmp(parent,"-"));
    strcpy(v.path,"-");assert(!vault_navigation_parent(&v,parent));
    vault_children_response(&v,"FILECHILDREN -\r\nCHILD 1 public|Public\r\nCHILD 2 games|Games\r\nEND CHILDREN NEXT 17\r\n");
    assert(v.child_view && v.count==2 && v.next==17);
    assert(!strcmp(vault_children_select(&v,1),"public") && !strcmp(vault_children_select(&v,2),"games"));
    assert(!vault_children_select(&v,0) && !vault_children_select(&v,3) && !vault_children_select(&v,UINT32_MAX));
    assert(!other.child_view && !other.count && !other.path[0]);
    vault_children_response(&v,"FILECHILDREN -\r\nEND CHILDREN NEXT 0\r\n");
    assert(v.child_view && !v.count && !v.next && !vault_children_select(&v,1));
    char path[49],text[150];memset(path,'a',48);path[48]=0;
    snprintf(text,sizeof(text),"FILECHILDREN -\r\nCHILD 16 %s|Long\r\nEND CHILDREN NEXT 0\r\n",path);
    vault_children_response(&v,text);assert(v.count==16 && !strcmp(vault_children_select(&v,16),path));
    vault_children_row(&v,0,"ignored");vault_children_row(&v,17,"ignored");assert(v.count==16);
    vault_navigation_reset(&v);const unsigned char *p=(const unsigned char *)&v;
    for(size_t i=0;i<sizeof(v);i++)assert(!p[i]);
}
int main(void) {
    reports();profiles();vaults();
    puts("PASS: report scopes/capacity/markers, profile snapshot lifetime, vault selection/denial/parent/16-row boundaries");
    return 0;
}

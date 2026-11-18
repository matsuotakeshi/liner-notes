#include "list_navigation_protocol.h"
#include <stdio.h>
#include <string.h>
ListContinuation list_continuation_parse(const char *text,int allow_text) {
    ListContinuation page={0};unsigned next=0;const char *end=strstr(text,"END NEXT ");
    if(end) { page.found=1;sscanf(end,"END NEXT %u",&next); }
    else if(allow_text && (end=strstr(text,"\r\nEND TEXT NEXT "))!=NULL) {
        page.found=1;sscanf(end+2,"END TEXT NEXT %u",&next);
    }
    page.next=next;return page;
}
int report_navigation_command(const ReportNavigation *r,char *out,size_t capacity) {
    const char *name=NULL;char target[32]="";
    if(!capacity)return 0;
    out[0]=0;
    switch(r->kind) {
    case REPORT_BOARDLOG:name="BOARDLOG";break;
    case REPORT_USERLOG:name="USERLOG";break;
    case REPORT_USERTABLE:name="USERTABLE";break;
    case REPORT_FILEHISTORY:name="FILEHISTORY";break;
    case REPORT_FILESTATS:name="FILESTATS";break;
    case REPORT_BOARDACCESS:name="BOARDACCESS";break;
    case REPORT_FILETEXT:name="FILETYPE";break;
    default:return 0;
    }
    if(r->kind!=REPORT_BOARDLOG) {
        if(r->all)strcpy(target," ALL");else snprintf(target,sizeof(target)," %u",r->target);
    }
    int n=snprintf(out,capacity,"%s%s %u",name,target,r->next);
    if(n<0 || (size_t)n>=capacity) { out[0]=0;return 0; }
    return 1;
}
void vault_children_response(VaultNavigation *v,const char *text) {
    vault_children_begin(v);
    const char *row=strstr(text,"\r\n");
    while(row && *row) {
        row+=2;unsigned index=0,next=0;char path[49];
        if(sscanf(row,"CHILD %u %48[^|]",&index,path)==2)vault_children_row(v,index,path);
        if(sscanf(row,"END CHILDREN NEXT %u",&next)==1) { v->next=next;break; }
        row=strstr(row,"\r\n");
    }
}

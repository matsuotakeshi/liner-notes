#include "list_navigation_core.h"
#include <stddef.h>
#include <string.h>
void report_navigation_begin(ReportNavigation *r,ReportKind kind,uint32_t target,int all) {
    r->kind=kind;r->target=target;r->all=all;r->pending=1;
}
void report_navigation_clear(ReportNavigation *r) { r->kind=REPORT_NONE;r->next=0; }
void report_navigation_complete(ReportNavigation *r,int has_marker,uint32_t next) {
    r->pending=0;r->next=next;if(!has_marker)r->kind=REPORT_NONE;
}
int report_navigation_access_refresh(ReportNavigation *r) {
    if(r->kind!=REPORT_BOARDACCESS)return 0;
    report_navigation_clear(r);return 1;
}
void profile_walk_begin(ProfileWalk *w,int field,uint32_t group,const char *query) {
    w->query.field=field;w->query.group=group;w->query.cursor=0;
    memmove(w->query.query,query,strlen(query)+1);w->pending=1;
}
ProfileWalkQuery profile_walk_next(ProfileWalk *w) { w->pending=1;return w->query; }
void profile_walk_complete(ProfileWalk *w,uint32_t next) {
    w->pending=0;if(next)w->query.cursor=next;else w->active=0;
}
void profile_walk_reset(ProfileWalk *w) {
    volatile unsigned char *p=(volatile unsigned char *)w;
    for(size_t i=0;i<sizeof(*w);i++)p[i]=0;
}
void vault_navigation_reset(VaultNavigation *v) { memset(v,0,sizeof(*v)); }
void vault_navigation_begin(VaultNavigation *v,const char *path) {
    memmove(v->selection,path,strlen(path)+1);v->selecting=1;
}
int vault_navigation_complete(VaultNavigation *v,int accepted) {
    if(!v->selecting)return 0;
    v->selecting=0;if(!accepted)return 0;
    strcpy(v->path,v->selection);v->child_view=0;v->count=0;return 1;
}
void vault_children_begin(VaultNavigation *v) { v->count=0;v->next=0;v->child_view=1; }
void vault_children_row(VaultNavigation *v,unsigned index,const char *path) {
    if(index && index<=16) { strcpy(v->children[index-1],path);v->count=index; }
}
const char *vault_children_select(const VaultNavigation *v,unsigned index) {
    return index && index<=v->count && index<=16?v->children[index-1]:NULL;
}
const char *vault_navigation_path(const VaultNavigation *v) { return *v->path?v->path:"-"; }
int vault_navigation_parent(const VaultNavigation *v,char out[49]) {
    if(!*v->path || !strcmp(v->path,"-")) { strcpy(out,"-");return 0; }
    strcpy(out,v->path);char *slash=strrchr(out,'/');if(slash)*slash=0;else *out=0;
    if(!*out)strcpy(out,"-");
    return 1;
}

#define _POSIX_C_SOURCE 200809L
#include "runtime_limits.h"
#include "app_limits.h"
/* Board-worker-owned catalog. Online changes are durable overlays. */
#include "catalog.h"
#include "board_policy.h"
#include "members.h"
#include "moderation.h"
#include "durable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int kind,level; uint32_t groups[4],users[32]; unsigned count; } Acl;
typedef struct { int present; uint32_t parent; Acl read,write; char title[81],description[321],read_text[256],write_text[256]; } Board;
static Board boards[CATALOG_MAX+1];
static unsigned char guest_posts[CATALOG_MAX+1];
void catalog_guest(uint32_t id,int enabled) { if(id<=CATALOG_MAX)guest_posts[id]=(unsigned char)enabled; }
static Durable changes={-1,0,0},roles={-1,0,0};
static uint32_t sigops[CATALOG_MAX+1];
static int integer(const char *p,uint32_t max,uint32_t *out) {
    if(!*p) return 0;
    uint32_t n=0;
    for(;*p;p++) { if(*p<'0'||*p>'9') return 0; if(n>max/10 || (n==max/10 && (unsigned)(*p-'0')>max%10))return 0; n=n*10+(unsigned)(*p-'0'); if(n>max) return 0; }
    *out=n; return 1;
}
static int parse_acl(char *p,Acl *a) {
    if(*p=='l' || *p=='g') {
        if(!members_enabled())return 0;
        a->kind=3;a->level=0;int level_set=0;
        while(*p) {
            char *next=strchr(p,'+');if(next)*next=0;
            uint32_t value;
            if(*p=='l') { if(level_set++ || !integer(p+1,5,&value))return 0;a->level=(int)value; }
            else if(*p=='g') { if(!integer(p+1,100,&value) || !value || (a->groups[(value-1)/32]&(1u<<((value-1)%32))))return 0;a->groups[(value-1)/32]|=1u<<((value-1)%32); }
            else return 0;
            if(!next)break;
            p=next+1;if(!*p)return 0;
        }
        return 1;
    }
    if(!strcmp(p,"all")) { a->kind=1; return 1; }
    if(!strcmp(p,"member")) { a->kind=2; return members_enabled(); }
    if(!strcmp(p,"none")) return 1;
    while(*p) {
        char *comma=strchr(p,','); if(comma) *comma=0;
        uint32_t id;
        if(*p!='u' || !integer(p+1,MEMBER_MAX,&id) || !members_name(id) || a->count==32) return 0;
        a->users[a->count++]=id;
        if(!comma) break;
        p=comma+1; if(!*p) return 0;
    }
    return a->count>0;
}
int catalog_load(const char *path) {
    memset(boards,0,sizeof(boards));
    if(!path) {
        boards[1].present=1; boards[1].read.kind=1;
        boards[1].write.kind=members_enabled()?2:1; strcpy(boards[1].title,"General");strcpy(boards[1].read_text,"all");strcpy(boards[1].write_text,members_enabled()?"member":"all"); return 1;
    }
    FILE *f=fopen(path,"rb"); if(!f) return 0;
    char line[1024]; int ok=1;
    while(fgets(line,sizeof(line),f)) {
        if(!strchr(line,'\n') && !feof(f)) { ok=0; break; }
        line[strcspn(line,"\r\n")]=0;
        if(!*line || line[0]=='#') continue;
        char *parts[5]; parts[0]=line;
        for(int i=1;i<5;i++) { char *sep=strchr(parts[i-1],'|'); if(!sep) { ok=0; break; } *sep=0; parts[i]=sep+1; }
        if(!ok) break;
        char *description=strchr(parts[4],'|');
        if(description) *description++=0; else description="";
        if(strlen(description)>320 || strchr(description,'|')) { ok=0; break; }
        for(const unsigned char *p=(unsigned char *)description;*p;p++) if(*p<32 || *p==127) ok=0;
        uint32_t id,parent;
        if(!integer(parts[0],CATALOG_MAX,&id) || !id || !integer(parts[1],CATALOG_MAX,&parent) ||
           id==parent || boards[id].present || !*parts[4] || strlen(parts[4])>80 || strchr(parts[4],'|')) { ok=0; break; }
        for(const unsigned char *p=(unsigned char *)parts[4];*p;p++) if(*p<32 || *p==127) ok=0;
        if(strlen(parts[2])>=256 || strlen(parts[3])>=256) { ok=0;break; }
        strcpy(boards[id].read_text,parts[2]);strcpy(boards[id].write_text,parts[3]);
        if(!ok || !parse_acl(parts[2],&boards[id].read) || !parse_acl(parts[3],&boards[id].write)) { ok=0; break; }
        boards[id].present=1; boards[id].parent=parent; strcpy(boards[id].title,parts[4]); strcpy(boards[id].description,description);
    }
    if(ferror(f)) ok=0;
    fclose(f);
    if(!boards[1].present || boards[1].parent) ok=0;
    for(uint32_t i=1;i<=CATALOG_MAX && ok;i++) if(boards[i].present) {
        unsigned depth=0;
        for(uint32_t n=i;n;n=boards[n].parent) if(!boards[n].present || ++depth>CATALOG_MAX) { ok=0; break; }
    }
    return ok;
}
static int allows(const Acl *a,uint32_t user) {
    if(members_pending(user))return 0;
    if(a->kind==3) { if(!members_name(user) || members_level(user)<a->level)return 0;for(unsigned i=0;i<4;i++)if((members_group_word(user,i)&a->groups[i])!=a->groups[i])return 0;return 1; }
    if(a->kind==1) return 1;
    if(a->kind==2) return members_name(user)!=NULL;
    for(unsigned i=0;i<a->count;i++) if(a->users[i]==user) return 1;
    return 0;
}
int catalog_known(uint32_t id) { return id>0 && id<=CATALOG_MAX && boards[id].present; }
int catalog_exists(uint32_t id) { return catalog_known(id) && !policy_deleted(id); }
int catalog_under(uint32_t id,uint32_t parent) {
    if(!catalog_exists(id) || !catalog_exists(parent))return 0;
    for(uint32_t n=id;n;n=boards[n].parent)if(n==parent)return 1;
    return 0;
}
int catalog_sigop(uint32_t id,uint32_t user) {
    if(!catalog_exists(id) || !user || !members_active(user) || members_pending(user) || moderation_suspended(user))return 0;
    for(uint32_t n=id;n;n=boards[n].parent)if(catalog_exists(n) && sigops[n]==user)return 1;
    return 0;
}
int catalog_group(uint32_t id,unsigned group) {
    if(!catalog_exists(id) || !group || group>100)return 0;
    unsigned word=(group-1)/32;uint32_t bit=1u<<((group-1)%32);
    return !!((boards[id].read.groups[word]|boards[id].write.groups[word])&bit);
}
int catalog_manage(uint32_t id,uint32_t user) { return catalog_exists(id) && (moderation_admin(user) || catalog_sigop(id,user)); }
int catalog_visible(uint32_t id,uint32_t user) {
    if(!catalog_exists(id)) return 0;
    if(catalog_sigop(id,user))return 1;
    for(uint32_t n=id;n;n=boards[n].parent) if(!allows(&boards[n].read,user)) return 0;
    return 1;
}
int catalog_read(uint32_t id,uint32_t user) { return catalog_visible(id,user) && !policy_locked(id,user); }
int catalog_write(uint32_t id,uint32_t user) {
    if(!catalog_exists(id) || policy_menu(id))return 0;
    return catalog_sigop(id,user) || ((!members_enabled() || members_name(user) || guest_posts[id]) && catalog_read(id,user) && allows(&boards[id].write,user));
}
uint32_t catalog_parent(uint32_t id) { return boards[id].parent; }
unsigned catalog_depth(uint32_t id) { unsigned n=0; while(boards[id].parent) { id=boards[id].parent; n++; } return n; }
const char *catalog_title(uint32_t id) { return boards[id].title; }
static uint32_t next_sibling(uint32_t parent,uint32_t after) {
    uint32_t best=0;
    for(uint32_t i=1;i<=CATALOG_MAX;i++)if(catalog_exists(i) && boards[i].parent==parent && !(parent==0 && i==1) && (!after || (parent==0 && after==1) || policy_order(i)>policy_order(after) || (policy_order(i)==policy_order(after) && i>after)))
        if(!best || policy_order(i)<policy_order(best) || (policy_order(i)==policy_order(best) && i<best))best=i;
    return best;
}
uint32_t catalog_next(uint32_t id) {
    uint32_t child=next_sibling(id,0);if(child)return child;
    while(id) { uint32_t next=next_sibling(boards[id].parent,id);if(next)return next;id=boards[id].parent; }return 0;
}

const char *catalog_description(uint32_t id) { return catalog_exists(id)?boards[id].description:""; }

static int update(const char *text,int commit,uint32_t *number) {
    if(strlen(text)>=1024)return 0;
    char copy[1024];strcpy(copy,text);char *part[6]={copy};
    for(unsigned i=1;i<6;i++) { char *sep=strchr(part[i-1],'|');if(!sep)return 0;*sep=0;part[i]=sep+1; }
    uint32_t id,parent;Board b={0};
    if(!integer(part[0],CATALOG_MAX,&id) || !id || !integer(part[1],CATALOG_MAX,&parent) || id==parent || (id==1 && parent) || (parent && !catalog_exists(parent)) ||
       strlen(part[2])>=256 || strlen(part[3])>=256 || !*part[4] || strlen(part[4])>80 || strlen(part[5])>320 || strchr(part[5],'|'))return 0;
    for(unsigned i=4;i<6;i++)for(const unsigned char *p=(unsigned char *)part[i];*p;p++)if(*p<32 || *p==127)return 0;
    for(uint32_t p=parent;p;p=boards[p].parent)if(p==id)return 0;
    strcpy(b.read_text,part[2]);strcpy(b.write_text,part[3]);
    if(!parse_acl(part[2],&b.read) || !parse_acl(part[3],&b.write))return 0;
    b.present=1;b.parent=parent;strcpy(b.title,part[4]);strcpy(b.description,part[5]);
    if(commit)boards[id]=b;
    *number=id;return 1;
}
static int change_apply(void *unused,const DurableRow *row) {
    (void)unused;uint32_t id;
    return row->kind=='B' && members_name(row->actor) && update(row->text,1,&id) && row->target==id;
}
static int role_apply(void *unused,const DurableRow *r) {
    (void)unused;uint32_t user;
    if(r->kind!='S' || !catalog_exists(r->target) || !members_name(r->actor) || !integer(r->text,MEMBER_MAX,&user) || (user && !members_name(user)))return 0;
    sigops[r->target]=user;return 1;
}
int catalog_role_set(uint32_t id,uint32_t user,uint32_t actor,uint32_t *previous) {
    if((!moderation_admin(actor) && !(catalog_exists(id) && catalog_sigop(boards[id].parent,actor))) || !catalog_exists(id) || (user && (!members_name(user) || members_pending(user) || moderation_suspended(user))))return 0;
    *previous=sigops[id];if(user==*previous)return 2;
    DurableRow row={0};row.kind='S';row.actor=actor;row.target=id;snprintf(row.text,sizeof(row.text),"%u",user);
    if(!dj_append(&roles,&row))return -1;
    return role_apply(NULL,&row)?1:-1;
}
void catalog_roles(uint32_t user,char *out,size_t size) {
    size_t used=(size_t)snprintf(out,size,"SIGOPS\r\n");
    for(uint32_t id=1;id<=CATALOG_MAX;id++)if(catalog_exists(id) && (moderation_admin(user) || catalog_sigop(id,user)))
        used+=(size_t)snprintf(out+used,size-used,"SIGOP %u %u\r\n",id,sigops[id]);
    snprintf(out+used,size-used,"END SIGOPS\r\n");
}
int catalog_open_changes(const char *base) {
    char path[4096];int n=snprintf(path,sizeof(path),"%s.catalog",base);
    if(n<=0 || (size_t)n>=sizeof(path) || !dj_open(&changes,path,"LNCAT001",change_apply,NULL))return 0;
    memset(sigops,0,sizeof(sigops));n=snprintf(path,sizeof(path),"%s.sigops",base);
    return n>0 && (size_t)n<sizeof(path) && dj_open(&roles,path,"LNSIG001",role_apply,NULL);
}
void catalog_close_changes(void) { dj_close(&changes);dj_close(&roles); }
int catalog_set(uint32_t actor,const char *text,uint32_t *id) {
    unsigned long proposed=strtoul(text,NULL,10);if(proposed>ln_limit("BOARDS",CATALOG_MAX) && !catalog_known((uint32_t)proposed))return 0;
    if(!members_name(actor) || !update(text,0,id) || policy_deleted(*id))return 0;
    if(!moderation_admin(actor)) {
        const char *separator=strchr(text,'|');char *end=NULL;
        unsigned long parent=separator?strtoul(separator+1,&end,10):0;
        if(!separator || !end || *end!='|' || parent>CATALOG_MAX)return 0;
        if((!catalog_exists(*id) || boards[*id].parent!=parent) && !catalog_sigop((uint32_t)parent,actor))return 0;
        if(catalog_exists(*id) && !catalog_sigop(*id,actor))return 0;
    }
    DurableRow row={0};row.kind='B';row.actor=actor;row.target=*id;strcpy(row.text,text);
    if(!dj_append(&changes,&row))return -1;
    return change_apply(NULL,&row)?1:-1;
}
void catalog_settings(uint32_t id,char *out,size_t size) {
    if(!catalog_exists(id)) { snprintf(out,size,"ERR no board\r\n");return; }
    Board *b=&boards[id];snprintf(out,size,"BOARDSETTING %u|%u|%s|%s|%s|%s\r\n",id,b->parent,b->read_text,b->write_text,b->title,b->description);
}
void catalog_history(uint32_t user,uint32_t cursor,char *out,size_t size) {
    if(changes.broken) { snprintf(out,size,"ERR catalog storage\r\n");return; }
    uint32_t index=cursor?cursor-1:changes.sequence;
    if(index>changes.sequence) { snprintf(out,size,"ERR command\r\n");return; }
    size_t used=(size_t)snprintf(out,size,"BOARDLOG\r\n");unsigned shown=0,scanned=0;
    while(index && shown<8 && scanned++<256) {
        unsigned char bytes[DJ_ROW];
        if(DJ_SEEK(changes.fd,8+(int64_t)(index-1)*DJ_ROW,SEEK_SET)<0 || dj_io(changes.fd,bytes,DJ_ROW,0)!=DJ_ROW || dj_sum(bytes)!=dj_get(bytes+DJ_ROW-4) || dj_get(bytes+4)!=index || dj_get(bytes+24)>1023) {
            changes.broken=1;snprintf(out,size,"ERR catalog storage\r\n");return;
        }
        if(!catalog_manage(dj_get(bytes+12),user)) { index--;continue; }
        shown++;char text[1024];uint32_t len=dj_get(bytes+24);memcpy(text,bytes+28,len);text[len]=0;
        used+=(size_t)snprintf(out+used,size-used,"BOARDCHANGE %u %u %llu %s\r\n",index,dj_get(bytes+8),(unsigned long long)(dj_get(bytes+16)|((uint64_t)dj_get(bytes+20)<<32)),text);index--;
    }
    snprintf(out+used,size-used,"END NEXT %u\r\n",index?index+1:0);
}

int catalog_replace_group(uint32_t b,uint32_t actor,unsigned from,unsigned to) {
    if(!catalog_exists(b) || !from || from>100 || !to || to>100)return 0;
    if(!catalog_group(b,from))return catalog_group(b,to)?2:0;
    char read[256]="",write[256]="",line[1024];char *outputs[]={read,write};const char *inputs[]={boards[b].read_text,boards[b].write_text};
    for(unsigned i=0;i<2;i++) { char copy[256];strcpy(copy,inputs[i]);char *p=copy;size_t n=0;int exists=0;
        for(const char *v=inputs[i];*v;v++)if((v==inputs[i] || v[-1]=='+') && *v=='g' && strtoul(v+1,NULL,10)==to)exists=1;
        while(p) { char *next=strchr(p,'+');if(next)*next++=0;if(*p=='g' && strtoul(p+1,NULL,10)==from) { if(!exists)n+=(size_t)snprintf(outputs[i]+n,256-n,"%sg%u",n?"+":"",to); }else n+=(size_t)snprintf(outputs[i]+n,256-n,"%s%s",n?"+":"",p);p=next; }
    }
    snprintf(line,sizeof(line),"%u|%u|%s|%s|%s|%s",b,boards[b].parent,read,write,boards[b].title,boards[b].description);
    uint32_t id;return catalog_set(actor,line,&id);
}

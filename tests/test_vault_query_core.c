#define _POSIX_C_SOURCE 200809L
#include "vault_query_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Vault *v=(Vault *)1;
static Principal member={.subject="member:1",.capabilities=CAP_VAULT_READ};
static Principal manager={.subject="member:2",.capabilities=CAP_VAULT_MANAGE};
static VaultEntry files[32];static size_t file_count;
static VaultBoard boards[32];static size_t board_count;
static unsigned calls,selected,protocol_calls;
static uint32_t fail_cursor=UINT32_MAX;
static VaultResult selection=VAULT_OK,protocol_result=VAULT_OK,backend=VAULT_OK;
static const char *last_scope,*last_pattern;static uint64_t last_since;
static uint32_t last_cursor;
static void zero(const void *out,size_t n) { const unsigned char *s=out;for(size_t i=0;i<n;i++)assert(!s[i]); }
static void limit(const char *key,const char *value) {
#ifdef _WIN32
    assert(!_putenv_s(key,value));
#else
    if(*value)assert(!setenv(key,value,1));else assert(!unsetenv(key));
#endif
}
static void reset(void) {
    memset(files,0,sizeof(files));memset(boards,0,sizeof(boards));file_count=board_count=0;
    calls=selected=protocol_calls=0;fail_cursor=UINT32_MAX;selection=protocol_result=backend=VAULT_OK;
    last_scope=last_pattern=NULL;last_cursor=0;last_since=0;
    limit("LINER_LIMIT_BATCH_FILES","");limit("LINER_LIMIT_FILE","");limit("LINER_LIMIT_BATCH_BYTES","");
}
static void fill(size_t n) {
    file_count=n;
    for(size_t i=0;i<n;i++) { files[i].id=(uint32_t)i+1;files[i].size=10;files[i].created=100+i;strcpy(files[i].category,"pub");strcpy(files[i].name,"name.LZH"); }
}
/* These backend stubs deliberately return partial data even on storage failure.
 * Core must clear it, not assume every repository has already sanitized errors. */
VaultResult vault_search(Vault *vault,const Principal *p,uint32_t cursor,const char *pattern,const char *scope,VaultEntry out[8],size_t *count,uint32_t *next) {
    assert(vault==v);calls++;last_cursor=cursor;last_scope=scope;last_pattern=pattern;
    size_t start=cursor?cursor-1:0;*count=0;*next=0;
    while(start<file_count && *count<8)out[(*count)++]=files[start++];
    if(start<file_count)*next=(uint32_t)start+1;
    return !(p->capabilities&CAP_VAULT_READ)?VAULT_DENIED:cursor==fail_cursor?VAULT_STORAGE:backend;
}
VaultResult vault_news_in(Vault *vault,const Principal *p,uint32_t cursor,uint64_t since,const char *scope,VaultEntry out[8],size_t *count,uint32_t *next) {
    last_since=since;return vault_search(vault,p,cursor,"",scope,out,count,next);
}
VaultResult vault_board_get(Vault *vault,const Principal *p,const char *name,VaultBoard *out) {
    assert(vault==v && name);selected++;memset(out,0,sizeof(*out));
    return !(p->capabilities&CAP_VAULT_READ)?VAULT_DENIED:selection;
}
VaultResult vault_boards(Vault *vault,const Principal *p,uint32_t cursor,VaultBoard out[8],size_t *count,uint32_t *next) {
    assert(vault==v);calls++;last_cursor=cursor;size_t start=cursor?cursor-1:0;*count=0;*next=0;
    while(start<board_count && *count<8)out[(*count)++]=boards[start++];
    if(start<board_count)*next=(uint32_t)start+1;
    return !(p->capabilities&CAP_VAULT_READ)?VAULT_DENIED:cursor==fail_cursor?VAULT_STORAGE:backend;
}
VaultResult vault_info(Vault *vault,const Principal *p,uint32_t id,VaultEntry *out) {
    assert(vault==v);calls++;*out=files[0];
    return !(p->capabilities&CAP_VAULT_READ)?VAULT_DENIED:id!=1?VAULT_NOT_FOUND:backend;
}
VaultResult vault_protocol(Vault *vault,const Principal *p,uint32_t id,char out[12]) {
    assert(vault==v && p==&member && id==1);protocol_calls++;strcpy(out,"ZMODEM");return protocol_result;
}
VaultResult vault_categories(Vault *vault,const Principal *p,uint32_t cursor,VaultCategory out[8],size_t *count,uint32_t *next) {
    assert(vault==v);calls++;last_cursor=cursor;*count=1;*next=17;strcpy(out[0].name,"pub");out[0].count=2;
    return !(p->capabilities&CAP_VAULT_READ)?VAULT_DENIED:backend;
}
VaultResult vault_audit(Vault *vault,const Principal *p,uint32_t cursor,VaultEvent out[4],size_t *count,uint32_t *next) {
    assert(vault==v);calls++;last_cursor=cursor;*count=1;*next=5;out[0].sequence=1;out[0].action='E';strcpy(out[0].actor,"member:1");
    return !(p->capabilities&CAP_VAULT_MANAGE)?VAULT_DENIED:backend;
}
static void query_scope(void) {
    reset();fill(10);VaultQueryFiles out;VaultQuery q={.pattern="*.LZH"};
    assert(vault_query_files(v,&member,&q,&out)==VAULT_OK && out.count==8 && out.next==9);
    assert(last_scope==NULL && !strcmp(last_pattern,"*.LZH") && !selected);
    q.scope="";q.check_scope=1;q.cursor=9;
    assert(vault_query_files(v,&member,&q,&out)==VAULT_OK && out.count==2 && !out.next);
    assert(last_scope && !*last_scope && selected==1 && last_cursor==9);
    q.news=1;q.date_valid=0;selection=VAULT_NOT_FOUND;calls=0;
    assert(vault_query_files(v,&member,&q,&out)==VAULT_DENIED && !calls);zero(&out,sizeof(out));
    selection=VAULT_OK;assert(vault_query_files(v,&member,&q,&out)==VAULT_INVALID && !calls);
    q.scope=NULL;assert(vault_query_files(v,&member,&q,&out)==VAULT_INVALID);
    q.check_scope=0;q.news=0;backend=VAULT_BUSY;
    assert(vault_query_files(v,&member,&q,&out)==VAULT_BUSY);zero(&out,sizeof(out));
    assert(vault_query_files(NULL,&member,&q,&out)==VAULT_STORAGE);zero(&out,sizeof(out));
    assert(vault_query_files(v,NULL,&q,&out)==VAULT_DENIED);zero(&out,sizeof(out));
    assert(vault_query_files(v,&member,NULL,&out)==VAULT_INVALID);
    assert(vault_query_files(v,&member,&q,NULL)==VAULT_INVALID);
}
static void date_window(void) {
    reset();fill(10);VaultQueryFiles out;
    VaultQuery q={.news=1,.date_valid=1,.since=99,.has_until=1,.until=102,.scope="pub"};
    assert(vault_query_files(v,&member,&q,&out)==VAULT_OK && out.count==3 && out.next==9);
    assert(last_since==99 && !strcmp(last_scope,"pub"));zero(out.rows+3,5*sizeof(out.rows[0]));
    q.until=1;assert(vault_query_files(v,&member,&q,&out)==VAULT_OK && !out.count && out.next==9);
    zero(out.rows,sizeof(out.rows));q.cursor=9;q.has_until=0;
    assert(vault_query_files(v,&member,&q,&out)==VAULT_OK && out.count==2 && !out.next);
}
static void matching(void) {
    reset();fill(16);VaultQueryMatch out;
    strcpy(files[0].category,"pub/child");strcpy(files[1].category,"");
    assert(vault_query_match(v,&member,"?.LZH","pub",&out)==VAULT_OK && out.count==14 && out.ids[0]==3 && out.bytes==140);
    assert(!selected && !strcmp(last_pattern,"?.LZH"));
    assert(vault_query_match(v,&member,"*","",&out)==VAULT_OK && out.count==1 && out.ids[0]==2);
    assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_OK && out.count==16 && out.bytes==160);
    fill(17);assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_LIMIT);zero(&out,sizeof(out));
    fill(10);fail_cursor=9;assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_STORAGE);zero(&out,sizeof(out));fail_cursor=UINT32_MAX;
    limit("LINER_LIMIT_BATCH_FILES","9");assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_LIMIT);zero(&out,sizeof(out));
    limit("LINER_LIMIT_BATCH_FILES","");limit("LINER_LIMIT_BATCH_BYTES","99");
    assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_LIMIT);zero(&out,sizeof(out));
    limit("LINER_LIMIT_BATCH_BYTES","100");assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_OK);
    limit("LINER_LIMIT_FILE","9");assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_LIMIT);zero(&out,sizeof(out));
    reset();fill(16);for(size_t i=0;i<16;i++)files[i].size=1073741824u;
    assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_OK && out.bytes==VAULT_BATCH_BYTES);
    files[15].size++;assert(vault_query_match(v,&member,"*",NULL,&out)==VAULT_LIMIT);zero(&out,sizeof(out));
    assert(vault_query_match(v,NULL,"*",NULL,&out)==VAULT_DENIED);
}
static void children(void) {
    reset();board_count=18;VaultQueryChildren out;
    for(size_t i=1;i<board_count;i++)snprintf(boards[i].name,sizeof(boards[i].name),"child%u",(unsigned)i);
    strcpy(boards[1].display,"Title");
    assert(vault_query_children(v,&member,"",0,&out)==VAULT_OK && out.count==15 && out.next==17);
    assert(!strcmp(out.rows[0].display,"Title") && !strcmp(out.rows[14].name,"child15") && !selected);
    assert(vault_query_children(v,&member,"",out.next,&out)==VAULT_OK && out.count==2 && !out.next);
    strcpy(boards[1].name,"pub/leaf");strcpy(boards[2].name,"pub/leaf/deep");strcpy(boards[3].name,"public/other");strcpy(boards[4].name,"pub/last");
    assert(vault_query_children(v,&member,"pub",0,&out)==VAULT_OK && out.count==2 && !strcmp(out.rows[1].display,"last"));
    selection=VAULT_STORAGE;assert(vault_query_children(v,&member,"pub",0,&out)==VAULT_DENIED);zero(&out,sizeof(out));
    selection=VAULT_OK;fail_cursor=9;
    assert(vault_query_children(v,&member,"",0,&out)==VAULT_STORAGE);zero(&out,sizeof(out));
    assert(vault_query_children(v,&member,"123456789012345678901234567890123456789012345678901",0,&out)==VAULT_INVALID);
}
static void detail(void) {
    reset();fill(1);VaultQueryDetail out;
    assert(vault_query_detail(v,&member,1,&out)==VAULT_OK && out.entry.id==1 && !strcmp(out.protocol,"ZMODEM"));
    protocol_calls=0;assert(vault_query_detail(v,&member,2,&out)==VAULT_NOT_FOUND && !protocol_calls);zero(&out,sizeof(out));
    protocol_result=VAULT_DENIED;assert(vault_query_detail(v,&member,1,&out)==VAULT_STORAGE);zero(&out,sizeof(out));
    assert(vault_query_detail(v,&manager,1,&out)==VAULT_DENIED);zero(&out,sizeof(out));
}
static void reports(void) {
    reset();VaultQueryBoards b;VaultQueryCategories c;VaultQueryAudit a;
    board_count=1;strcpy(boards[0].name,"pub");boards[0].can_upload=1;
    assert(vault_query_boards(v,&member,0,&b)==VAULT_OK && b.count==1 && b.rows[0].can_upload);
    assert(vault_query_categories(v,&member,9,&c)==VAULT_OK && last_cursor==9 && c.next==17 && c.rows[0].count==2);
    assert(vault_query_audit(v,&member,0,&a)==VAULT_DENIED);zero(&a,sizeof(a));
    /* Manage-only admission is deliberately distinct from ordinary read. */
    assert(vault_query_audit(v,&manager,9,&a)==VAULT_OK && last_cursor==9 && a.next==5 && a.rows[0].action=='E');
    backend=VAULT_STORAGE;
    assert(vault_query_boards(v,&member,0,&b)==VAULT_STORAGE);zero(&b,sizeof(b));
    assert(vault_query_categories(v,&member,0,&c)==VAULT_STORAGE);zero(&c,sizeof(c));
    assert(vault_query_audit(v,&manager,0,&a)==VAULT_STORAGE);zero(&a,sizeof(a));
}
int main(void) {
    query_scope();date_window();matching();children();detail();reports();
    puts("PASS: 6 vault-query groups (scope/ACL, dates/cursors, batch limits, hierarchy, detail, reports)");return 0;
}

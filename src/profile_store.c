#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "profile_store.h"
#include "app_limits.h"
#include "durable.h"
#include "members.h"
#include "extras.h"
#include "moderation.h"
#include "date_filter.h"
#include <stdlib.h>
#define FIELDS 14
#include "db_index.h"
static DbIndex profiles_db;
static char *profile_texts[32];static unsigned text_slot;
static sqlite3_stmt *profile_query(const char *sql,uint32_t u,unsigned f) {
    sqlite3_stmt *q=NULL;if(sqlite3_prepare_v2(profiles_db.sql,sql,-1,&q,NULL)!=SQLITE_OK)db_die(&profiles_db);
    sqlite3_bind_int(q,1,(int)u);sqlite3_bind_int(q,2,(int)f);return q;
}
static void profile_db_set(uint32_t u,unsigned f,const char *s,int mode) {
    sqlite3_stmt *q=profile_query(s?"INSERT INTO fields(u,f,value) VALUES(?,?,?) ON CONFLICT(u,f) DO UPDATE SET value=excluded.value":"INSERT INTO fields(u,f,mode) VALUES(?,?,?) ON CONFLICT(u,f) DO UPDATE SET mode=excluded.mode",u,f);
    if(s)sqlite3_bind_text(q,3,s,-1,SQLITE_STATIC);else sqlite3_bind_int(q,3,mode);
    if(sqlite3_step(q)!=SQLITE_DONE)db_die(&profiles_db);
    sqlite3_finalize(q);
}
static const char *profile_db_get(uint32_t u,unsigned f) {
    sqlite3_stmt *q=profile_query("SELECT value FROM fields WHERE u=? AND f=?",u,f);const char *out=NULL;
    int rc=sqlite3_step(q);if(rc==SQLITE_ROW && sqlite3_column_type(q,0)!=SQLITE_NULL) { unsigned i=text_slot++%32;sqlite3_free(profile_texts[i]);profile_texts[i]=sqlite3_mprintf("%s",sqlite3_column_text(q,0));if(!profile_texts[i])db_die(&profiles_db);out=profile_texts[i]; }
    else if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)db_die(&profiles_db);
    sqlite3_finalize(q);return out;
}
static unsigned profile_mode(uint32_t u,unsigned f) {
    sqlite3_stmt *q=profile_query("SELECT mode FROM fields WHERE u=? AND f=?",u,f);unsigned mode=f<7;int rc=sqlite3_step(q);
    if(rc==SQLITE_ROW && sqlite3_column_type(q,0)!=SQLITE_NULL)mode=(unsigned)sqlite3_column_int(q,0);else if(rc!=SQLITE_ROW && rc!=SQLITE_DONE)db_die(&profiles_db);
    sqlite3_finalize(q);return mode;
}
static Durable journal={-1,0,0};
static uint32_t pending_user,pending_field,pending_size,pending_actor;
static char pending[CONTENT_BODY+1];
static uint32_t edit_actor;
static const char *keys[]={"HANDLE","BIO","LOCATION","OCCUPATION","INTERESTS","WEBSITE","CONTACT","MACHINE","BIRTHDAY","REALNAME","KANA","ADDRESS","PHONE"};
const char *profile_key(unsigned field) { return field<PROFILE_FIELDS?keys[field]:"BUFFER"; }
int profile_field(const char *key) { for(unsigned i=0;i<PROFILE_FIELDS;i++)if(!strcmp(key,keys[i]))return (int)i;return -1; }
static int valid(unsigned f,const char *text) {
    size_t n=strlen(text);if(f>=FIELDS || n>(f==PROFILE_BUFFER?CONTENT_BODY:f==1?8192:f==0?80:320))return 0;
    for(size_t i=0;i<n;) {
        const unsigned char *p=(const unsigned char *)text+i;unsigned c=*p,len=c<128?1:c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0,cp=len==1?c:len?c&((1u<<(7-len))-1):0;
        if(!len || i+len>n)return 0;
        for(unsigned j=1;j<len;j++) { if((p[j]&0xc0)!=0x80)return 0;cp=(cp<<6)|(p[j]&63); }
        if((cp<32 && !((f==1 || f==PROFILE_BUFFER) && (cp=='\r' || cp=='\n' || cp=='\t'))) || (cp>=127 && cp<=159) || (len==3 && cp<2048) || (len==4 && cp<65536) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return 0;
        i+=len;
    }
    if(f==8 && n) { char shifted[11];if(n!=10 || text[4]!='-')return 0;memcpy(shifted,text,11);unsigned year=0;for(unsigned i=0;i<4;i++) { if(text[i]<'0' || text[i]>'9')return 0;year=year*10+(unsigned)(text[i]-'0'); }if(year<1900 || year>9999)return 0;unsigned leapyear=2000+year%400;snprintf(shifted,5,"%04u",leapyear);shifted[4]='-';uint64_t stamp;if(!date_filter_utc(shifted,&stamp))return 0; }
    return 1;
}
int profile_text_valid(unsigned f,const char *s) { return valid(f,s); }
static int apply(void *unused,const DurableRow *r) {
    (void)unused;if(!members_name(r->target) || !members_name(r->actor))return 0;
    if(pending_user) {
        if(r->target!=pending_user || r->actor!=pending_actor)return 0;
        size_t n=strlen(pending),add=strlen(r->text);
        if(r->kind=='J' && add && n+add<=pending_size) { memcpy(pending+n,r->text,add+1);return 1; }
        if(r->kind!='E' || add || n!=pending_size || !valid(pending_field,pending))return 0;
        profile_db_set(pending_user,pending_field,pending,0);pending_user=0;return 1;
    }
    unsigned f,x;int n=0;char canonical[40];
    if(sscanf(r->text,"%u %u%n",&f,&x,&n)!=2 || r->text[n])return 0;
    snprintf(canonical,sizeof(canonical),"%u %u",f,x);if(strcmp(canonical,r->text))return 0;
    if(r->kind=='V') { if(f>=PROFILE_FIELDS || x>2 || (f==0 && x!=1) || (x==2 && f!=8))return 0;profile_db_set(r->target,f,NULL,(int)x);return 1; }
    if(r->kind!='I' || f>=FIELDS || x>CONTENT_BODY)return 0;
    pending_user=r->target;pending_actor=r->actor;pending_field=f;pending_size=x;pending[0]=0;return 1;
}
void profile_store_close(void) { dj_close(&journal);db_close(&profiles_db);for(unsigned i=0;i<32;i++) { sqlite3_free(profile_texts[i]);profile_texts[i]=NULL; }pending_user=0; }

int profile_store_open(const char *base) {
    profile_store_close();
    if(!base)return 1;
    char path[4096];int n=snprintf(path,sizeof(path),"%s.profile",base);
    if(n<=0 || (size_t)n>=sizeof(path))return 0;
    if(members_profile_marker(0)) { struct stat st;if(storage_stat(path,&st))return 0; }
    db_open(&profiles_db,base,"profile-index",1);db_exec(&profiles_db,"DROP TABLE IF EXISTS fields;CREATE TABLE fields(u INTEGER,f INTEGER,value TEXT,mode INTEGER,PRIMARY KEY(u,f)) WITHOUT ROWID;");
    return dj_open(&journal,path,"LNPROF01",apply,NULL) && !pending_user && (!members_profile_marker(0) || journal.sequence);
}
int profile_store_broken(void) { return journal.broken; }
const char *profile_value(uint32_t u,unsigned f) {
    if(!members_active(u) || f>=FIELDS)return "";
    const char *saved=profile_db_get(u,f);if(saved)return saved;
    return f==0?extras_handle(u):f==1?extras_bio(u):f<7?extras_field(u,f-2):"";
}
const char *profile_visible(uint32_t u,uint32_t viewer,unsigned f,char scratch[16]) {
    if(!members_active(u) || !members_active(viewer) || f>=PROFILE_FIELDS)return "";
    const char *text=profile_value(u,f);
    if(u==viewer || moderation_admin(viewer))return text;
    if(!profile_mode(u,f))return "";
    if(f==8 && profile_mode(u,f)==2 && strlen(text)==10) { memcpy(scratch,text+5,6);return scratch; }return text;
}
static int append(uint32_t u,char kind,const char *text) { if(!members_profile_marker(u))return 0;DurableRow row={0};row.actor=edit_actor?edit_actor:u;row.target=u;row.kind=kind;snprintf(row.text,sizeof(row.text),"%s",text);if(!dj_append(&journal,&row) || !apply(NULL,&row)) { journal.broken=1;return 0; }return 1; }
int profile_set(uint32_t u,unsigned f,const char *text) {
    if(!members_name(u) || !valid(f,text))return -1;
    char head[40];size_t n=strlen(text);snprintf(head,sizeof(head),"%u %zu",f,n);if(!append(u,'I',head))return 0;
    for(size_t pos=0;pos<n;) { char chunk[DJ_TEXT+1];size_t len=n-pos;if(len>DJ_TEXT) { len=DJ_TEXT;while(len && ((unsigned char)text[pos+len]&0xc0)==0x80)len--;if(!len)len=DJ_TEXT; }memcpy(chunk,text+pos,len);chunk[len]=0;if(!append(u,'J',chunk))return 0;pos+=len; }
    return append(u,'E',"");
}
int profile_visibility(uint32_t u,unsigned f,unsigned mode) { if(!members_name(u) || f>=PROFILE_FIELDS || mode>2 || (f==0 && mode!=1) || (mode==2 && f!=8))return -1;char value[40];snprintf(value,sizeof(value),"%u %u",f,mode);return append(u,'V',value); }
int profile_match(uint32_t u,uint32_t viewer,int field,const char *query) {
    if(!strcmp(query,"*"))return 1;
    for(unsigned f=0;f<PROFILE_FIELDS;f++)if(field<0 || (unsigned)field==f) { char scratch[16];const char *v=profile_visible(u,viewer,f,scratch);if(*v && strstr(v,query))return 1; }
    return 0;
}

int profile_admin_set(uint32_t actor,uint32_t target,unsigned field,const char *text) {
    if(!moderation_admin(actor) || !members_active(target) || field>=PROFILE_FIELDS)return -1;
    edit_actor=actor;int ok=profile_set(target,field,text);edit_actor=0;return ok;
}

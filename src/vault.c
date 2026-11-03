#define _POSIX_C_SOURCE 200809L
#include "storage_sqlite.h"
#include "runtime_limits.h"
#include "vault.h"
#include "store_limits.h"
#include "db_index.h"
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#define READ _read
#define WRITE _write
#define CLOSE _close
#define SYNC _commit
#define SEEK _lseeki64
#define STAT _stat64
#define FSTAT _fstat64
#define MKDIR(p) _mkdir(p)
#define REGULAR(m) (((m)&_S_IFMT)==_S_IFREG)
#define DIRECTORY(m) (((m)&_S_IFMT)==_S_IFDIR)
#else
#include <unistd.h>
#define READ storage_read
#define WRITE storage_write
#define CLOSE storage_close
#define SYNC storage_sync
#define SEEK storage_seek
#define STAT stat
#define FSTAT storage_fstat
#define MKDIR(p) mkdir(p,0700)
#define REGULAR(m) S_ISREG(m)
#define DIRECTORY(m) S_ISDIR(m)
#endif
#define ROW 640
#define FILE_BOARDS 1024
typedef struct { int deny; unsigned level; uint32_t groups[4]; } FileAcl;
typedef struct { char name[49],read[65],upload[65],display[81],info[8193],sigop[80]; FileAcl r,w;char password[98];uint32_t revision;unsigned menu,order,keep; } FileBoard;
typedef struct { VaultEntry entry,upload; unsigned char state,hidden,deleted;char protocol[12]; } FileState;
struct Vault {
    char root[3500]; uint32_t last;
    FileBoard boards[FILE_BOARDS];unsigned board_count;unsigned info_pending,info_length;char info_body[8193],info_actor[80];
    int (*guard)(void *);void *guard_context;
    int64_t loaded; int broken;
    uint32_t event_count;DbIndex events_db;
    DbIndex files_db;unsigned active_uploads;
    uint32_t file_limit,event_limit;uint64_t byte_limit;
};
static FileState *vault_file(Vault *v,uint32_t id) { if(!v->files_db.sql)db_open(&v->files_db,NULL,"",sizeof(FileState));return db_at(&v->files_db,id); }
static VaultEvent *vault_event(Vault *v,uint32_t id) { if(!v->events_db.sql)db_open(&v->events_db,NULL,"",sizeof(VaultEvent));return db_at(&v->events_db,id); }
static uint32_t get32(const unsigned char *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(unsigned char *p,uint32_t n) { for(int i=0;i<4;i++) p[i]=(unsigned char)(n>>(8*i)); }
static int digest(const void *p,size_t n,unsigned char out[32]) { unsigned len=0; return EVP_Digest(p,n,out,&len,EVP_sha256(),NULL)==1 && len==32; }
void vault_hex(const unsigned char hash[32],char text[65]) { static const char hex[]="0123456789abcdef"; for(int i=0;i<32;i++) { text[i*2]=hex[hash[i]>>4];text[i*2+1]=hex[hash[i]&15]; } text[64]=0; }
const char *vault_result_name(VaultResult r) { static const char *names[]={"OK","DENIED","NOT_FOUND","INVALID","BUSY","STORAGE","LIMIT"}; return r<=VAULT_LIMIT?names[r]:"STORAGE"; }
static int path(Vault *v,char out[4096],uint32_t id) {
    int n=id?snprintf(out,4096,"%s/%08u.bin",v->root,id):snprintf(out,4096,"%s/index.dat",v->root);
    return n>0 && n<4096;
}
static int regular_open(const char *p,int flags) {
#ifdef _WIN32
    DWORD attr=GetFileAttributesA(p);
    if(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT)) { errno=EINVAL; return -1; }
    int fd=_open(p,flags|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
    size_t plen=strlen(p);int fd=plen>=9 && !strcmp(p+plen-9,"index.dat")?storage_open(p,flags|O_NOFOLLOW|O_NONBLOCK,0600):open(p,flags|O_NOFOLLOW|O_NONBLOCK,0600);
#endif
    struct STAT st;
    if(fd>=0 && (FSTAT(fd,&st) || !REGULAR(st.st_mode))) { CLOSE(fd); errno=EINVAL; return -1; }
    return fd;
}
static int io_all(int fd,unsigned char *buf,size_t n,int write_mode) {
    while(n) { int k=write_mode?(int)WRITE(fd,buf,(unsigned)n):(int)READ(fd,buf,(unsigned)n); if(k<=0) return 0; buf+=k;n-=(size_t)k; } return 1;
}
static int dir_sync(Vault *v) {
#ifdef _WIN32
    (void)v; return 1;
#else
    int fd=open(v->root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW); if(fd<0) return 0;
    int ok=storage_sync(fd)==0; storage_close(fd); return ok;
#endif
}
static int root_ok(const char *p) {
#ifdef _WIN32
    DWORD a=GetFileAttributesA(p);return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_DIRECTORY) && !(a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;return !lstat(p,&st) && S_ISDIR(st.st_mode);
#endif
}
static int text_ok(const char *s,size_t maximum,int empty) {
    size_t n=strlen(s); if(n>maximum || (!empty && !n)) return 0;
    const unsigned char *p=(const unsigned char *)s;
    for(size_t i=0;i<n;) {
        unsigned c=p[i],len=c<128?1:c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:0;
        if(!len || i+len>n) return 0;
        unsigned cp=len==1?c:c&((1u<<(7-len))-1);
        for(unsigned j=1;j<len;j++) { if((p[i+j]&0xc0)!=0x80)return 0;cp=(cp<<6)|(p[i+j]&63); }
        if(cp<32 || (cp>=127 && cp<=159) || (len==3 && cp<2048) || (len==4 && cp<65536) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return 0;
        i+=len;
    }
    return 1;
}
static int guide_ok(const char *s) {
    size_t n=strlen(s);if(n>8192)return 0;char copy[8193];memcpy(copy,s,n+1);
    for(size_t i=0;i<n;i++)if(copy[i]=='\r' || copy[i]=='\n' || copy[i]=='\t')copy[i]=' ';
    return text_ok(copy,8192,1);
}
static int allowed(const Principal *p,unsigned cap) { return p && text_ok(p->subject,79,0) && (p->capabilities&cap)==cap; }
/* Policies receive provider claims only; this module never opens members.dat. */
static int board_path(const char *s) {
    if(!text_ok(s,48,1) || !strcmp(s,"-") || strpbrk(s,"|\\:\t"))return 0;
    if(!*s)return 1;
    const char *p=s;
    while(*p) { const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
        if(!n || (n==1 && *p=='.') || (n==2 && !memcmp(p,"..",2)))return 0;
        if(!end)break;
        p=end+1;if(!*p)return 0;
    }
    return 1;
}
static int acl_parse(const char *text,FileAcl *out) {
    memset(out,0,sizeof(*out));
    if(!strcmp(text,"all") || !strcmp(text,"member"))return 1;
    if(!strcmp(text,"none")) { out->deny=1;return 1; }
    if(!*text || strlen(text)>64)return 0;
    int level=0;const char *p=text;
    while(*p) { char kind=*p++;unsigned n=0,digits=0;
        if(kind!='l' && kind!='g')return 0;
        while(*p>='0' && *p<='9') { n=n*10+(unsigned)(*p++-'0');if(++digits>3 || n>100)return 0; }
        if(!digits)return 0;
        if(kind=='l') { if(level++ || n>5)return 0;out->level=n; }
        else { if(!n || (out->groups[(n-1)/32]&(1u<<((n-1)%32))))return 0;out->groups[(n-1)/32]|=1u<<((n-1)%32); }
        if(!*p)break;
        if(*p++!='+' || !*p)return 0;
    }
    return 1;
}
static int board_index(Vault *v,const char *name) { for(unsigned i=0;i<v->board_count;i++)if(!strcmp(v->boards[i].name,name))return (int)i;return -1; }
static int policy_parse(Vault *v,const char *text,FileBoard *out,int *index) {
    char copy[241];if(strlen(text)>240)return 0;strcpy(copy,text);
    char *read=strchr(copy,'|');if(!read)return 0;*read++=0;
    char *upload=strchr(read,'|');if(!upload)return 0;*upload++=0;
    const char *name=!strcmp(copy,"-")?"":copy;
    if(!board_path(name) || strlen(read)>64 || strlen(upload)>64 || !acl_parse(read,&out->r) || !acl_parse(upload,&out->w))return 0;
    if(*name) { char parent[49];strcpy(parent,name);char *slash=strrchr(parent,'/');if(slash)*slash=0;else *parent=0;if(board_index(v,parent)<0)return 0; }
    *index=board_index(v,name);if(*index<0 && v->board_count==FILE_BOARDS)return 0;
    strcpy(out->name,name);strcpy(out->read,read);strcpy(out->upload,upload);return 1;
}
static int acl_allows(const FileAcl *a,const Principal *p) { if(a->deny || p->level>5 || p->level<a->level)return 0;for(unsigned i=0;i<4;i++) { uint32_t word=i?p->group_high[i-1]:p->groups;if((word&a->groups[i])!=a->groups[i])return 0; }return 1; }
static int board_manage(Vault *,const Principal *,const char *);
static int board_allows(Vault *v,const Principal *p,const char *name,int upload) {
    if(!v->board_count)return 1;
    int i=board_index(v,name);if(upload && i>=0 && v->boards[i].menu)return 0;
    if(board_manage(v,p,name))return 1;
    if(i<0 || (upload && (v->boards[i].menu || !acl_allows(&v->boards[i].w,p))))return 0;
    char parent[49];strcpy(parent,name);
    for(;;) { i=board_index(v,parent);if(i<0 || !acl_allows(&v->boards[i].r,p) || (v->boards[i].revision && p->vault_grants[i+1]!=v->boards[i].revision))return 0;
        if(!*parent)break;
        char *slash=strrchr(parent,'/');if(slash)*slash=0;else *parent=0;
    }
    return 1;
}
static int under(const char *name,const char *parent) { size_t n=strlen(parent);return !n || !strcmp(name,parent) || (!strncmp(name,parent,n) && name[n]=='/'); }
static int board_manage(Vault *v,const Principal *p,const char *name) {
    if(allowed(p,CAP_VAULT_MANAGE))return 1;
    if(!allowed(p,CAP_VAULT_READ))return 0;
    for(unsigned i=0;i<v->board_count;i++)if(under(name,v->boards[i].name) && *v->boards[i].sigop && !strcmp(v->boards[i].sigop,p->subject))return 1;
    return 0;
}
static int entry_ok(VaultEntry *e) {
    return e->id && e->id<=VAULT_ID_MAX && e->size<=VAULT_MAX_BYTES &&
        text_ok(e->name,100,0) && strcmp(e->name,".") && strcmp(e->name,"..") &&
        !strpbrk(e->name,"/\\:") && text_ok(e->description,240,1) && text_ok(e->publisher,79,0);
}
static void encode(unsigned char row[ROW],VaultEntry *e,char kind) {
    memset(row,0,ROW);row[0]=(unsigned char)kind;put32(row+4,e->id);put32(row+8,e->size);
    put32(row+12,(uint32_t)e->created);put32(row+16,(uint32_t)(e->created>>32));memcpy(row+20,e->sha256,32);
    put32(row+52,(uint32_t)strlen(e->name));put32(row+56,(uint32_t)strlen(e->description));
    memcpy(row+60,e->name,strlen(e->name));memcpy(row+188,e->description,strlen(e->description));
    memcpy(row+444,e->publisher,strlen(e->publisher));put32(row+524,(uint32_t)strlen(e->publisher));
    if(kind=='a' || kind=='r') { put32(row+528,(uint32_t)strlen(e->category));memcpy(row+532,e->category,strlen(e->category)); }
}
static int decode(unsigned char row[ROW],VaultEntry *e) {
    unsigned char hash[32],canonical[ROW];
    uint32_t n=get32(row+52),d=get32(row+56),p=get32(row+524);
    if(n>100 || d>240 || p>79 || !digest(row,608,hash) || memcmp(hash,row+608,32)) return 0;
    memset(e,0,sizeof(*e));e->id=get32(row+4);e->size=get32(row+8);e->created=((uint64_t)get32(row+16)<<32)|get32(row+12);
    memcpy(e->sha256,row+20,32);memcpy(e->name,row+60,n);memcpy(e->description,row+188,d);memcpy(e->publisher,row+444,p);
    if(row[0]=='a' || row[0]=='r') { uint32_t c=get32(row+528);if(c>48)return 0;memcpy(e->category,row+532,c);if(!board_path(e->category))return 0; }
    if(!entry_ok(e)) return 0;
    encode(canonical,e,(char)row[0]);return !memcmp(row,canonical,608);
}
static uint64_t timestamp(void) { time_t now=time(NULL);return now>0?(uint64_t)now:0; }
static int category_ok(const char *s) { return text_ok(s,VAULT_CATEGORY_BYTES,1) && strcmp(s,"-"); }
static void encode_event(unsigned char row[ROW],const VaultEvent *e) {
    memset(row,0,ROW);row[0]=(unsigned char)e->action;put32(row+4,e->file);
    put32(row+8,(uint32_t)e->at);put32(row+12,(uint32_t)(e->at>>32));
    put32(row+16,(uint32_t)strlen(e->actor));put32(row+20,(uint32_t)strlen(e->after));
    memcpy(row+24,e->actor,strlen(e->actor));memcpy(row+104,e->after,strlen(e->after));
}
/* Both state and audit derive from the same durable record, avoiding a second
 * journal and a crash window between changing a file and recording its actor. */
static int apply_record(Vault *v,unsigned char row[ROW]) {
    if(0 || v->event_count>=VAULT_ID_MAX || (v->info_pending && row[0]!='j' && row[0]!='k')) return 0;
    VaultEvent event={0};event.action=(char)row[0];event.sequence=v->event_count+1;
    if(row[0]=='A' || row[0]=='D' || row[0]=='R' || row[0]=='a' || row[0]=='r') {
        VaultEntry e;if(!decode(row,&e) || 0)return 0;
        event.file=e.id;if(row[0]=='a')event.action='A';if(row[0]=='r')event.action='R';
        if(row[0]=='A' || row[0]=='R' || row[0]=='a' || row[0]=='r') {
            if(e.id<=v->last)return 0;
            if(row[0]=='R' || row[0]=='r') {
                if(v->active_uploads>=VAULT_UPLOADS || v->event_count+v->active_uploads+2>VAULT_ID_MAX)return 0;
                unsigned own=0;for(uint32_t i=1;i<=v->last;i++)if((*vault_file(v,i)).state==VAULT_UPLOAD_ACTIVE && !strcmp((*vault_file(v,i)).upload.publisher,e.publisher))own++;
                if(own>=VAULT_UPLOADS_PER_SUBJECT)return 0;
                (*vault_file(v,e.id)).upload=e;(*vault_file(v,e.id)).state=VAULT_UPLOAD_ACTIVE;v->active_uploads++;
            } else (*vault_file(v,e.id)).entry=e;
            v->last=e.id;event.at=e.created;strcpy(event.actor,e.publisher);
        } else {
            if(!(*vault_file(v,e.id)).entry.id || (*vault_file(v,e.id)).hidden)return 0;
            strcpy(e.category,(*vault_file(v,e.id)).entry.category);
            if(memcmp(&e,&(*vault_file(v,e.id)).entry,sizeof(e)))return 0;
            (*vault_file(v,e.id)).hidden=1;strcpy(event.actor,"unknown");
        }
    } else if(row[0]=='B') {
        unsigned char sum[32],canonical[ROW];uint32_t a=get32(row+16),n=get32(row+20);
        if(get32(row+4) || a>79 || n>240 || !digest(row,608,sum) || memcmp(sum,row+608,32))return 0;
        event.at=((uint64_t)get32(row+12)<<32)|get32(row+8);memcpy(event.actor,row+24,a);memcpy(event.after,row+104,n);
        if(!text_ok(event.actor,79,0))return 0;
        encode_event(canonical,&event);if(memcmp(canonical,row,608))return 0;
        FileBoard b={0};int index;if(!policy_parse(v,event.after,&b,&index))return 0;
        if(index<0)index=(int)v->board_count++;
        strcpy(b.display,v->boards[index].display);strcpy(b.info,v->boards[index].info);strcpy(b.sigop,v->boards[index].sigop);strcpy(b.password,v->boards[index].password);b.revision=v->boards[index].revision;b.menu=v->boards[index].menu;b.order=v->boards[index].order;b.keep=v->boards[index].keep;v->boards[index]=b;
    } else if(strchr("noijkpv",row[0]) && row[0]) {
        unsigned char sum[32],canonical[ROW];uint32_t a=get32(row+16),n=get32(row+20),id=get32(row+4);
        if(!id || id>v->board_count || a>79 || n>240 || !digest(row,608,sum) || memcmp(sum,row+608,32))return 0;
        event.file=id;event.at=((uint64_t)get32(row+12)<<32)|get32(row+8);memcpy(event.actor,row+24,a);memcpy(event.after,row+104,n);
        if(!text_ok(event.actor,79,0))return 0;
        encode_event(canonical,&event);if(memcmp(canonical,row,608))return 0;
        FileBoard *b=&v->boards[id-1];
        if(row[0]=='n') { if(!text_ok(event.after,80,1))return 0;strcpy(b->display,event.after); }
        else if(row[0]=='o') { if(!text_ok(event.after,79,1))return 0;strcpy(b->sigop,event.after); }
        else if(row[0]=='p') { if(*event.after && (strlen(event.after)!=97 || event.after[32]!=' '))return 0;strcpy(b->password,event.after);b->revision=*event.after?v->event_count+1:0; }
        else if(row[0]=='v') { char type[8],canonical[100];unsigned order,keep;int n=0;
            if(sscanf(event.after,"%7s %u %u%n",type,&order,&keep,&n)!=3 || event.after[n] || (strcmp(type,"MENU") && strcmp(type,"BOARD")) || keep>1000000)return 0;
            snprintf(canonical,sizeof(canonical),"%s %u %u",type,order,keep);if(strcmp(canonical,event.after))return 0;b->menu=!strcmp(type,"MENU");b->order=order;b->keep=keep;
        }
        else if(row[0]=='i') { char *end;unsigned long size=strtoul(event.after,&end,10);if(!*event.after || *end || size>8192)return 0;v->info_pending=id;v->info_length=(unsigned)size;v->info_body[0]=0;strcpy(v->info_actor,event.actor); }
        else { if(id!=v->info_pending || strcmp(v->info_actor,event.actor))return 0;size_t length=strlen(v->info_body);
            if(row[0]=='j') { if(!n || length+n>v->info_length)return 0;for(unsigned i=0;i<n;i++)if(((unsigned char)event.after[i]<32 && !strchr("\r\n\t",event.after[i])) || (unsigned char)event.after[i]==127)return 0;memcpy(v->info_body+length,event.after,n+1); }
            else { if(n || length!=v->info_length || !guide_ok(v->info_body))return 0;strcpy(b->info,v->info_body);v->info_pending=0; }
        }
    } else if(row[0]=='C' || row[0]=='H'   || row[0]=='P' || row[0]=='X' || row[0]=='E' || row[0]=='S' || row[0]=='F' || row[0]=='O' || row[0]=='T' || row[0]=='N' || row[0]=='Q') {
        unsigned char hash[32],canonical[ROW];uint32_t a=get32(row+16),c=get32(row+20);
        if(a>79 || c>(row[0]=='E'?240u:row[0]=='O'?79u:row[0]=='N'?100u:VAULT_CATEGORY_BYTES) || !digest(row,608,hash) || memcmp(hash,row+608,32))return 0;
        event.file=get32(row+4);event.at=((uint64_t)get32(row+12)<<32)|get32(row+8);
        memcpy(event.actor,row+24,a);memcpy(event.after,row+104,c);
        if(!event.file || event.file>v->last || !text_ok(event.actor,79,0) || !(row[0]=='E'?text_ok(event.after,240,1):row[0]=='O'?text_ok(event.after,79,0):row[0]=='N'?(text_ok(event.after,100,0) && !strpbrk(event.after,"/\\:") && strcmp(event.after,".") && strcmp(event.after,"..")):category_ok(event.after)))return 0;
        encode_event(canonical,&event);if(memcmp(canonical,row,608))return 0;
        VaultEntry *e=&(*vault_file(v,event.file)).entry;
        if(row[0]=='Q') {
            FileState *f=vault_file(v,event.file);if((!f->entry.id && !f->upload.id) || !*event.after || strlen(event.after)>=sizeof(f->protocol))return 0;
            strcpy(event.before,f->protocol);strcpy(f->protocol,event.after);
        } else if(row[0]=='P' || row[0]=='X') {
            if((*vault_file(v,event.file)).state!=VAULT_UPLOAD_ACTIVE || *event.after)return 0;
            if(row[0]=='P') {
                if(strcmp(event.actor,(*vault_file(v,event.file)).upload.publisher))return 0;
                *e=(*vault_file(v,event.file)).upload;e->created=event.at;(*vault_file(v,event.file)).state=VAULT_UPLOAD_COMMITTED;
            } else (*vault_file(v,event.file)).state=VAULT_UPLOAD_CANCELLED;
            v->active_uploads--;
        } else {
        if(!e->id)return 0;
        if(row[0]=='F') { if(*event.after || (*vault_file(v,event.file)).deleted)return 0;(*vault_file(v,event.file)).deleted=(*vault_file(v,event.file)).hidden=1; }
        else if((*vault_file(v,event.file)).deleted)return 0;
        else if(row[0]=='S') { if(!(*vault_file(v,event.file)).hidden || *event.after)return 0;(*vault_file(v,event.file)).hidden=0; }
        else if(row[0]=='O') { strcpy(event.before,e->publisher);strcpy(e->publisher,event.after); }
        else if(row[0]=='N') { strcpy(event.before,e->name);strcpy(e->name,event.after); }
        else if(row[0]=='T') { if(strcmp(event.after,"0") && strcmp(event.after,"1"))return 0;e->ascii=*event.after=='1'; }
        else if(row[0]=='E') { if(!strcmp(e->description,event.after))return 0;strcpy(event.before,e->description);strcpy(e->description,event.after); }
        else if((*vault_file(v,event.file)).hidden)return 0;
        else if(row[0]=='H') { if(*event.after)return 0;(*vault_file(v,event.file)).hidden=1; }
        else {
            if(!strcmp(e->category,event.after))return 0;
            strcpy(event.before,e->category);strcpy(e->category,event.after);
        }
        }
    } else return 0;
    if(v->event_count+1+v->active_uploads>VAULT_ID_MAX)return 0;
    (*vault_event(v,v->event_count++))=event;return 1;
}
/* Reload only committed additions under the same cross-process lock used by
 * local administration. No account-store lock is held inside this library. */
static VaultResult begin(Vault *v,int *out) {
    if(!v || v->broken) return VAULT_STORAGE;
    char p[4096];if(!path(v,p,0)) return VAULT_STORAGE;
    int fd=regular_open(p,O_RDWR);if(fd<0) return VAULT_STORAGE;
#ifdef _WIN32
    OVERLAPPED ov={0};
    if(!LockFileEx((HANDLE)_get_osfhandle(fd),LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&ov)) { CLOSE(fd);return VAULT_BUSY; }
#else
    struct flock lock={0};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(storage_lock(fd,&lock)) { CLOSE(fd);return VAULT_BUSY; }
#endif
    struct STAT st;unsigned char header[8],row[ROW];
    if(FSTAT(fd,&st) || st.st_size<8 || st.st_size>8+(int64_t)VAULT_ID_MAX*ROW ||
       (st.st_size-8)%ROW || st.st_size<v->loaded || !io_all(fd,header,8,0) || memcmp(header,"LNVLT001",8)) goto fail;
    if(!v->loaded) v->loaded=8;
    if(SEEK(fd,v->loaded,SEEK_SET)<0) goto fail;
    while(v->loaded<st.st_size) {
        if(!io_all(fd,row,ROW,0) || !apply_record(v,row)) goto fail;
        v->loaded+=ROW;
    }
    if(v->info_pending)goto fail;
    *out=fd;return VAULT_OK;
fail:
    v->broken=1;CLOSE(fd);return VAULT_STORAGE;
}
VaultResult vault_init(const char *directory) {
    if(!directory || strlen(directory)>=3500) return VAULT_INVALID;
    if(MKDIR(directory) && errno!=EEXIST) return VAULT_STORAGE;
    if(!root_ok(directory)) return VAULT_INVALID;
    Vault *v=NULL;char p[4096];snprintf(p,sizeof(p),"%s/index.dat",directory);
    int fd=regular_open(p,O_RDWR|O_CREAT|O_EXCL);
    if(fd<0) return vault_open(directory,&v)==VAULT_OK?(vault_close(v),VAULT_OK):VAULT_STORAGE;
    unsigned char magic[]="LNVLT001";
    int ok=io_all(fd,magic,8,1) && !SYNC(fd);CLOSE(fd);
    if(!ok) return VAULT_STORAGE;
    VaultResult r=vault_open(directory,&v);if(r==VAULT_OK && !dir_sync(v)) r=VAULT_STORAGE;
    vault_close(v);return r;
}
static int limits(Vault *v) {
    v->file_limit=VAULT_MAX_FILES;v->event_limit=VAULT_MAX_EVENTS;v->byte_limit=VAULT_TOTAL_BYTES;
    char path[4096];snprintf(path,sizeof(path),"%s/limits.conf",v->root);
    int fd=regular_open(path,O_RDONLY);if(fd<0)return errno==ENOENT;
    FILE *f=fdopen(fd,"r");if(!f) { CLOSE(fd);return 0; }
    char line[160];unsigned seen=0;int ok=1;
    while(fgets(line,sizeof(line),f)) {
        if(!strchr(line,'\n') && !feof(f)) { ok=0;break; }
        line[strcspn(line,"\r\n")]=0;if(!*line || *line=='#')continue;
        char *value=strchr(line,'=');if(!value || !value[1]) { ok=0;break; }*value++=0;
        uint64_t n=0;for(char *c=value;*c;c++) { if(*c<'0' || *c>'9' || n>(UINT64_MAX-(unsigned)(*c-'0'))/10) { ok=0;break; }n=n*10+(unsigned)(*c-'0'); }
        unsigned bit=!strcmp(line,"max_files")?1:!strcmp(line,"max_bytes")?2:!strcmp(line,"max_events")?4:0;
        if(!ok || !bit || (seen&bit) || (bit!=2 && n>VAULT_ID_MAX)) { ok=0;break; }seen|=bit;
        if(bit==1)v->file_limit=n?(uint32_t)n:VAULT_ID_MAX;
        else if(bit==4)v->event_limit=n?(uint32_t)n:VAULT_ID_MAX;
        else v->byte_limit=n?n:UINT64_MAX;
    }
    if(ferror(f))ok=0;
    fclose(f);return ok;
}
VaultResult vault_open(const char *directory,Vault **out) {
    *out=NULL;struct STAT st;
    if(!directory || strlen(directory)>=3500 || STAT(directory,&st) || !DIRECTORY(st.st_mode) || !root_ok(directory)) return VAULT_INVALID;
    Vault *v=calloc(1,sizeof(*v));if(!v) return VAULT_STORAGE;strcpy(v->root,directory);
    if(!limits(v)) { vault_close(v);return VAULT_INVALID; }
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK) { vault_close(v);return r; }
    CLOSE(fd);*out=v;return VAULT_OK;
}
void vault_set_guard(Vault *v,int (*guard)(void *),void *context) { if(v) { v->guard=guard;v->guard_context=context; } }
static int authorized(Vault *v) { return !v->guard || v->guard(v->guard_context); }
void vault_close(Vault *v) { if(v) { db_close(&v->files_db);db_close(&v->events_db);free(v); } }
uint32_t vault_last_id(Vault *v) { return v?v->last:0; }
static VaultResult write_record(Vault *v,int fd,unsigned char row[ROW]) {
    if(!authorized(v))return VAULT_DENIED;
    unsigned reserved=v->active_uploads;
    if(row[0]=='P' || row[0]=='X') { if(reserved)reserved--; }
    else if(row[0]=='R' || row[0]=='r')reserved++;
    if((uint64_t)v->event_count+1+reserved>v->event_limit)return VAULT_LIMIT;
    if(0)return VAULT_STORAGE;
    if(strchr("AaRr",row[0]) && 0)return VAULT_STORAGE;
    if(!digest(row,608,row+608) || SEEK(fd,0,SEEK_END)<0 || !io_all(fd,row,ROW,1) || SYNC(fd) || !apply_record(v,row)) { v->broken=1;return VAULT_STORAGE; }
    v->loaded+=ROW;return VAULT_OK;
}
VaultResult vault_board_check(Vault *v,const Principal *p,const char *category) {
    if(!allowed(p,CAP_VAULT_UPLOAD) || !allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(!board_path(category))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if((v->board_count && board_index(v,category)<0) || !board_allows(v,p,category,1))r=VAULT_DENIED;
    CLOSE(fd);return r;
}
VaultResult vault_board_get(Vault *v,const Principal *p,const char *name,VaultBoard *out) {
    if(!allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(!board_path(name))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    int i=board_index(v,name);memset(out,0,sizeof(*out));
    if(!board_allows(v,p,name,0)) { CLOSE(fd);return VAULT_DENIED; }
    if(i<0) { if(!v->board_count && !*name) { strcpy(out->read,"member");strcpy(out->upload,"member"); }else r=VAULT_NOT_FOUND; }
    else { strcpy(out->name,v->boards[i].name);strcpy(out->read,v->boards[i].read);strcpy(out->upload,v->boards[i].upload);strcpy(out->display,v->boards[i].display);strcpy(out->info,v->boards[i].info);strcpy(out->sigop,v->boards[i].sigop);out->can_manage=board_manage(v,p,name);out->menu=v->boards[i].menu;out->order=v->boards[i].order;out->keep=v->boards[i].keep;out->locked=v->boards[i].revision!=0; }
    CLOSE(fd);return r;
}
VaultResult vault_board_set(Vault *v,const Principal *p,const char *name,const char *read,const char *upload) {
    if(!allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(!board_path(name) || strlen(read)>64 || strlen(upload)>64)return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    VaultEvent e={0};e.action='B';e.at=timestamp();strcpy(e.actor,p->subject);
    snprintf(e.after,sizeof(e.after),"%s|%s|%s",*name?name:"-",read,upload);
    FileBoard b={0};int index;
    if(!board_manage(v,p,name))r=VAULT_DENIED;
    else if(!policy_parse(v,e.after,&b,&index))r=VAULT_INVALID;
    else if(index<0 || strcmp(v->boards[index].read,read) || strcmp(v->boards[index].upload,upload)) {
        unsigned char row[ROW];encode_event(row,&e);r=write_record(v,fd,row);
    }
    CLOSE(fd);return r;
}
VaultResult vault_boards(Vault *v,const Principal *p,uint32_t cursor,VaultBoard out[8],size_t *count,uint32_t *next) {
    *count=0;*next=0;if(!allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(cursor>FILE_BOARDS)return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if(!v->board_count) {
        if(!cursor) { memset(out,0,sizeof(*out));strcpy(out->read,"member");strcpy(out->upload,"member");out->can_upload=allowed(p,CAP_VAULT_UPLOAD);*count=1; }
    } else {
        unsigned order[FILE_BOARDS];for(unsigned i=0;i<v->board_count;i++)order[i]=i;
        for(unsigned i=1;i<v->board_count;i++) { unsigned key=order[i],j=i;while(j && (v->boards[order[j-1]].order>v->boards[key].order || (v->boards[order[j-1]].order==v->boards[key].order && order[j-1]>key))) { order[j]=order[j-1];j--; }order[j]=key; }
        unsigned i=cursor?cursor-1:0;
        for(;i<v->board_count && *count<8;i++)if(board_allows(v,p,v->boards[order[i]].name,0)) {
            VaultBoard *b=&out[(*count)++];memset(b,0,sizeof(*b));strcpy(b->name,v->boards[order[i]].name);strcpy(b->read,v->boards[order[i]].read);strcpy(b->upload,v->boards[order[i]].upload);strcpy(b->display,v->boards[order[i]].display);
            b->can_upload=allowed(p,CAP_VAULT_UPLOAD) && board_allows(v,p,b->name,1);
        }
        *next=i<v->board_count?i+1:0;
    }
    CLOSE(fd);return VAULT_OK;
}
static VaultResult append(Vault *v,int fd,VaultEntry *e,char kind) {
    unsigned char row[ROW];encode(row,e,kind);return write_record(v,fd,row);
}
/* ASCII letters compare case-insensitively; UTF-8 text otherwise matches bytes.
 * No locale-dependent byte folding can damage Japanese text. */
static unsigned fold(unsigned c) { return c>='A' && c<='Z'?c+32:c; }
/* Linear wildcard matching with bounded input; wildcards apply to names only. */
static int wildcard_part(const char *name,size_t n,const char *pattern,size_t m) {
    size_t i=0;
    for(;i<m;i++) {
        if(pattern[i]=='*')return 1;
        unsigned value=i<n?(unsigned char)name[i]:' ';
        if(pattern[i]!='?' && fold((unsigned char)pattern[i])!=fold(value))return 0;
    }
    return i>=n;
}
static int wildcard(const char *name,const char *pattern) {
    const char *nd=strrchr(name,'.'),*pd=strrchr(pattern,'.');
    if(!pd)return wildcard_part(name,strlen(name),pattern,strlen(pattern));
    return wildcard_part(name,nd?(size_t)(nd-name):strlen(name),pattern,(size_t)(pd-pattern)) &&
        wildcard_part(nd?nd+1:"",nd?strlen(nd+1):0,pd+1,strlen(pd+1));
}
static int contains(const char *text,const char *query) {
    for(;*text;text++) {
        size_t i=0;
        while(query[i] && text[i]) {
            unsigned a=(unsigned char)text[i],b=(unsigned char)query[i];
            if(a>='A' && a<='Z')a+=32;
            if(b>='A' && b<='Z')b+=32;
            if(a!=b)break;
            i++;
        }
        if(!query[i])return 1;
    }
    return !*query;
}
VaultResult vault_search(Vault *v,const Principal *p,uint32_t cursor,const char *query,const char *category,VaultEntry out[8],size_t *count,uint32_t *next) {
    *count=0;*next=0;if(!allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(cursor>VAULT_ID_MAX || !text_ok(query,80,1) || (category && !category_ok(category)))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    uint32_t id=cursor?cursor:1;unsigned scanned=0;
    for(;id<=v->last && *count<8 && scanned<256;id++,scanned++) {
        VaultEntry *e=&(*vault_file(v,id)).entry;
        if(e->id && !(*vault_file(v,id)).hidden && board_allows(v,p,e->category,0) && (!category || (v->board_count?under(e->category,category):!strcmp(category,e->category))) &&
           (strpbrk(query,"*?")?wildcard(e->name,query):(contains(e->name,query) || contains(e->description,query) || contains(e->category,query))))out[(*count)++]=*e;
    }
    *next=id<=v->last?id:0;CLOSE(fd);return VAULT_OK;
}

VaultResult vault_news(Vault *v,const Principal *p,uint32_t cursor,uint64_t since,VaultEntry out[8],size_t *count,uint32_t *next) {
    return vault_news_in(v,p,cursor,since,NULL,out,count,next);
}
VaultResult vault_news_in(Vault *v,const Principal *p,uint32_t cursor,uint64_t since,const char *category,VaultEntry out[8],size_t *count,uint32_t *next) {
    *count=0;*next=0;if(!allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(cursor>VAULT_ID_MAX)return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    uint32_t id=cursor?cursor:1;unsigned scanned=0;
    for(;id<=v->last && *count<8 && scanned<256;id++,scanned++) {
        VaultEntry *e=&(*vault_file(v,id)).entry;
        if(e->id && !(*vault_file(v,id)).hidden && board_allows(v,p,e->category,0) && (!category || under(e->category,category)) && e->created>=since)out[(*count)++]=*e;
    }
    *next=id<=v->last?id:0;CLOSE(fd);return VAULT_OK;
}
VaultResult vault_description(Vault *v,const Principal *p,uint32_t id,const char *text) {
    if(!allowed(p,CAP_VAULT_MANAGE) && !allowed(p,CAP_VAULT_UPLOAD|CAP_VAULT_READ))return VAULT_DENIED;
    if(!id || id>VAULT_ID_MAX || !text || !text_ok(text,240,1))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if(id>v->last || !(*vault_file(v,id)).entry.id || (*vault_file(v,id)).deleted)r=VAULT_NOT_FOUND;
    else if(!board_manage(v,p,(*vault_file(v,id)).entry.category) && (strcmp((*vault_file(v,id)).entry.publisher,p->subject) || !board_allows(v,p,(*vault_file(v,id)).entry.category,0) || !board_allows(v,p,(*vault_file(v,id)).entry.category,1)))r=VAULT_DENIED;
    else if(strcmp((*vault_file(v,id)).entry.description,text)) {
        VaultEvent event={0};unsigned char row[ROW];event.file=id;event.at=timestamp();event.action='E';
        strcpy(event.actor,p->subject);strcpy(event.after,text);encode_event(row,&event);r=write_record(v,fd,row);
    }
    CLOSE(fd);return r;
}
VaultResult vault_restore(Vault *v,const Principal *p,uint32_t id) {
    if(!allowed(p,CAP_VAULT_MANAGE) && !allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(!id || id>VAULT_ID_MAX)return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if(id>v->last || !(*vault_file(v,id)).entry.id || (*vault_file(v,id)).deleted)r=VAULT_NOT_FOUND;
    else if(!board_manage(v,p,(*vault_file(v,id)).entry.category))r=VAULT_DENIED;
    else if((*vault_file(v,id)).hidden) {
        char file[4096];path(v,file,id);int blob=regular_open(file,O_RDONLY);struct STAT st;
        EVP_MD_CTX *hash=EVP_MD_CTX_new();unsigned char bytes[8192],value[32];unsigned len=0;
        int okay=blob>=0 && !FSTAT(blob,&st) && st.st_size==(*vault_file(v,id)).entry.size && hash && EVP_DigestInit_ex(hash,EVP_sha256(),NULL)==1;
        uint32_t left=(*vault_file(v,id)).entry.size;
        while(okay && left) { size_t n=left>sizeof(bytes)?sizeof(bytes):left;okay=(!v->guard || v->guard(v->guard_context)) && io_all(blob,bytes,n,0) && EVP_DigestUpdate(hash,bytes,n)==1;left-=(uint32_t)n; }
        if(okay)okay=READ(blob,bytes,1)==0 && EVP_DigestFinal_ex(hash,value,&len)==1 && len==32 && !memcmp(value,(*vault_file(v,id)).entry.sha256,32);
        EVP_MD_CTX_free(hash);if(blob>=0)CLOSE(blob);
        if(!okay)r=VAULT_STORAGE;
        else { VaultEvent event={0};unsigned char row[ROW];event.file=id;event.at=timestamp();event.action='S';strcpy(event.actor,p->subject);encode_event(row,&event);r=write_record(v,fd,row); }
    }
    CLOSE(fd);return r;
}
VaultResult vault_list(Vault *v,const Principal *p,uint32_t cursor,VaultEntry out[8],size_t *count,uint32_t *next) {
    return vault_search(v,p,cursor,"",NULL,out,count,next);
}
VaultResult vault_categories(Vault *v,const Principal *p,uint32_t cursor,VaultCategory out[8],size_t *count,uint32_t *next) {
    *count=0;*next=0;if(!allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(cursor>VAULT_ID_MAX)return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    uint32_t id=cursor?cursor:1;unsigned scanned=0;
    for(;id<=v->last && *count<8 && scanned<256;id++,scanned++) {
        if(id>v->last || !(*vault_file(v,id)).entry.id || (*vault_file(v,id)).hidden || !board_allows(v,p,(*vault_file(v,id)).entry.category,0))continue;
        unsigned n=0;uint32_t first=0;
        for(uint32_t j=1;j<=v->last;j++)if((*vault_file(v,j)).entry.id && !(*vault_file(v,j)).hidden && board_allows(v,p,(*vault_file(v,j)).entry.category,0) && !strcmp((*vault_file(v,id)).entry.category,(*vault_file(v,j)).entry.category)) { n++;if(!first)first=j; }
        if(first==id) { strcpy(out[*count].name,(*vault_file(v,id)).entry.category);out[(*count)++].count=n; }
    }
    *next=id<=v->last?id:0;CLOSE(fd);return VAULT_OK;
}
VaultResult vault_audit(Vault *v,const Principal *p,uint32_t cursor,VaultEvent out[4],size_t *count,uint32_t *next) {
    *count=0;*next=0;if(!allowed(p,CAP_VAULT_MANAGE))return VAULT_DENIED;
    if(cursor>VAULT_ID_MAX)return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    uint32_t index=cursor?cursor-1:0;
    for(;index<v->event_count && *count<4;index++)out[(*count)++]=(*vault_event(v,index));
    *next=index<v->event_count?index+1:0;CLOSE(fd);return VAULT_OK;
}
static VaultResult change(Vault *v,const Principal *p,uint32_t id,const char *category) {
    if(!allowed(p,CAP_VAULT_MANAGE) && !allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(!id || id>VAULT_ID_MAX || (category && !category_ok(category)))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if(category && v->board_count && board_index(v,category)<0)r=VAULT_INVALID;
    else if(id>v->last || !(*vault_file(v,id)).entry.id || (category && (*vault_file(v,id)).hidden))r=VAULT_NOT_FOUND;
    else if(!board_manage(v,p,(*vault_file(v,id)).entry.category) || (category && !board_manage(v,p,category)))r=VAULT_DENIED;
    else if(category?strcmp(category,(*vault_file(v,id)).entry.category)!=0:!(*vault_file(v,id)).hidden) {
        VaultEvent event={0};unsigned char row[ROW];event.file=id;event.at=timestamp();
        event.action=category?'C':'H';strcpy(event.actor,p->subject);if(category)strcpy(event.after,category);
        encode_event(row,&event);r=write_record(v,fd,row);
    }
    CLOSE(fd);return r;
}
VaultResult vault_category(Vault *v,const Principal *p,uint32_t id,const char *category) {
    return category?change(v,p,id,category):VAULT_INVALID;
}
VaultResult vault_info(Vault *v,const Principal *p,uint32_t id,VaultEntry *out) {
    if(!allowed(p,CAP_VAULT_READ)) return VAULT_DENIED;
    if(!id || id>VAULT_ID_MAX) return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK) return r;
    r=id>v->last || !(*vault_file(v,id)).entry.id || (*vault_file(v,id)).hidden || !board_allows(v,p,(*vault_file(v,id)).entry.category,0)?VAULT_NOT_FOUND:VAULT_OK;
    if(r==VAULT_OK) {
        char file[4096];path(v,file,id);int blob=regular_open(file,O_RDONLY);struct STAT st;
        if(blob<0 || FSTAT(blob,&st) || st.st_size!=(*vault_file(v,id)).entry.size) r=VAULT_STORAGE;
        if(blob>=0) CLOSE(blob);
        if(r==VAULT_OK) *out=(*vault_file(v,id)).entry;
    }
    CLOSE(fd);return r;
}
VaultResult vault_read(Vault *v,const Principal *p,uint32_t id,uint32_t offset,unsigned char out[VAULT_CHUNK],size_t *count) {
    return vault_read_block(v,p,id,offset,out,VAULT_CHUNK,count);
}
VaultResult vault_read_block(Vault *v,const Principal *p,uint32_t id,uint32_t offset,unsigned char *out,size_t limit,size_t *count) {
    *count=0;if(!out || !limit || limit>VAULT_BLOCK)return VAULT_INVALID;
    if(!allowed(p,CAP_VAULT_READ)) return VAULT_DENIED;
    if(!id || id>VAULT_ID_MAX) return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK) return r;
    if(id>v->last || !(*vault_file(v,id)).entry.id || (*vault_file(v,id)).hidden || !board_allows(v,p,(*vault_file(v,id)).entry.category,0)) { CLOSE(fd);return VAULT_NOT_FOUND; }
    uint32_t size=(*vault_file(v,id)).entry.size;if(offset>size) { CLOSE(fd);return VAULT_INVALID; }
    char file[4096];path(v,file,id);int blob=regular_open(file,O_RDONLY);struct STAT st;
    if(blob<0 || FSTAT(blob,&st) || st.st_size!=size || SEEK(blob,offset,SEEK_SET)<0) r=VAULT_STORAGE;
    else { size_t n=size-offset;if(n>limit) n=limit; if(!io_all(blob,out,n,0)) r=VAULT_STORAGE;else *count=n; }
    if(blob>=0) CLOSE(blob);
    CLOSE(fd);return r;
}
VaultResult vault_hide(Vault *v,const Principal *p,uint32_t id) { return change(v,p,id,NULL); }
static void part_path(Vault *v,uint32_t id,char out[4096]) { snprintf(out,4096,"%s/%08u.part",v->root,id); }
static int file_size(const char *name,uint64_t *size) {
    int fd=regular_open(name,O_RDONLY);if(fd<0)return errno==ENOENT?0:-1;
    struct STAT st;int ok=!FSTAT(fd,&st) && st.st_size>=0;CLOSE(fd);
    if(!ok)return -1;
    *size=(uint64_t)st.st_size;return 1;
}
static VaultResult capacity(Vault *v,uint32_t size,uint32_t *id) {
    uint64_t total=0;uint32_t highest=v->last;DIR *dir=opendir(v->root);if(!dir)return VAULT_STORAGE;
    struct dirent *de;VaultResult result=VAULT_OK;
    while((de=readdir(dir))!=NULL) {
        unsigned number=0;char ext[8],canonical[40];int used=0;
        if(sscanf(de->d_name,"%u.%7s%n",&number,ext,&used)!=2 || de->d_name[used] || (strcmp(ext,"bin") && strcmp(ext,"part")))continue;
        snprintf(canonical,sizeof(canonical),"%08u.%s",number,ext);
        if(strcmp(canonical,de->d_name))continue;
        char file[4096];uint64_t bytes=0;snprintf(file,sizeof(file),"%s/%s",v->root,canonical);
        if(!number || number>VAULT_ID_MAX || file_size(file,&bytes)!=1 || total>UINT64_MAX-bytes) { result=VAULT_STORAGE;break; }
        total+=bytes;if(number>highest)highest=number;
    }
    closedir(dir);if(result!=VAULT_OK)return result;
    for(uint32_t i=1;i<=v->last;i++)if((*vault_file(v,i)).state==VAULT_UPLOAD_ACTIVE) {
        char file[4096];uint64_t bin=0,part=0;path(v,file,i);int b=file_size(file,&bin);part_path(v,i,file);int t=file_size(file,&part);
        if(b<0 || t<0)return VAULT_STORAGE;
        uint64_t used=bin+part;if(used<(*vault_file(v,i)).upload.size)total+=(*vault_file(v,i)).upload.size-used;
    }
    if(highest>=v->file_limit || total>v->byte_limit || size>v->byte_limit-total)return VAULT_LIMIT;
    *id=highest+1;return VAULT_OK;
}
VaultResult vault_import(Vault *v,const Principal *p,const char *source,const char *name,const char *description,VaultEntry *out) {
    return vault_import_in(v,p,source,name,description,"",out);
}
VaultResult vault_import_in(Vault *v,const Principal *p,const char *source,const char *name,const char *description,const char *category,VaultEntry *out) {
    if(!allowed(p,CAP_VAULT_MANAGE)) return VAULT_DENIED;
    VaultEntry e={0};e.id=1;if(!board_path(category))return VAULT_INVALID;strcpy(e.category,category);
    if(!text_ok(name,100,0) || !text_ok(description,240,1)) return VAULT_INVALID;
    strcpy(e.name,name);strcpy(e.description,description);strcpy(e.publisher,p->subject);
    if(!entry_ok(&e)) return VAULT_INVALID;
    int source_fd=regular_open(source,O_RDONLY);struct STAT source_stat;
    if(source_fd<0) return VAULT_STORAGE;
    if(FSTAT(source_fd,&source_stat) || source_stat.st_size<0 || (uint64_t)source_stat.st_size>ln_limit("FILE",VAULT_MAX_BYTES)) { CLOSE(source_fd);return VAULT_LIMIT; }
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK) { CLOSE(source_fd);return r; }
    if(v->board_count && board_index(v,category)<0) { CLOSE(fd);CLOSE(source_fd);return VAULT_INVALID; }
    if((uint64_t)v->event_count+v->active_uploads>=v->event_limit) { CLOSE(fd);CLOSE(source_fd);return VAULT_LIMIT; }
    char file[4096];r=capacity(v,(uint32_t)source_stat.st_size,&e.id);if(r!=VAULT_OK)goto done;
    e.size=(uint32_t)source_stat.st_size;e.created=timestamp();
    path(v,file,e.id);int blob=regular_open(file,O_WRONLY|O_CREAT|O_EXCL);
    if(blob<0) { r=VAULT_STORAGE;goto done; }
    EVP_MD_CTX *hash=EVP_MD_CTX_new();int ok=hash && EVP_DigestInit_ex(hash,EVP_sha256(),NULL)==1;
    uint32_t remaining=e.size;unsigned char buf[8192];
    while(ok && remaining) {
        size_t n=remaining>sizeof(buf)?sizeof(buf):remaining;
        ok=io_all(source_fd,buf,n,0) && io_all(blob,buf,n,1) && EVP_DigestUpdate(hash,buf,n)==1;
        remaining-=(uint32_t)n;
    }
    unsigned char extra;unsigned len=0;
    if(ok) ok=READ(source_fd,&extra,1)==0 && EVP_DigestFinal_ex(hash,e.sha256,&len)==1 && len==32 && !SYNC(blob);
    EVP_MD_CTX_free(hash);CLOSE(blob);
    if(!ok || !dir_sync(v)) { r=VAULT_STORAGE;goto done; }
    r=append(v,fd,&e,*category?'a':'A');if(r==VAULT_OK) *out=e;
done:
    CLOSE(fd);CLOSE(source_fd);return r;
}

VaultResult vault_upload_start(Vault *v,const Principal *p,uint32_t size,const unsigned char hash[32],const char *name,const char *description,uint32_t *id) {
    return vault_upload_start_in(v,p,size,hash,name,description,"",id);
}
VaultResult vault_upload_start_in(Vault *v,const Principal *p,uint32_t size,const unsigned char hash[32],const char *name,const char *description,const char *category,uint32_t *id) {
    if(size>ln_limit("FILE",1073741824u))return VAULT_LIMIT;
    *id=0;if(!allowed(p,CAP_VAULT_UPLOAD))return VAULT_DENIED;
    if(size>VAULT_MAX_BYTES)return VAULT_LIMIT;
    VaultEntry e={0};e.id=1;e.size=size;if(!board_path(category))return VAULT_INVALID;strcpy(e.category,category);
    if(!hash || !text_ok(name,100,0) || !text_ok(description,240,1))return VAULT_INVALID;
    strcpy(e.name,name);strcpy(e.description,description);strcpy(e.publisher,p->subject);memcpy(e.sha256,hash,32);
    if(!entry_ok(&e))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if((v->board_count && board_index(v,category)<0) || !board_allows(v,p,category,1)) { CLOSE(fd);return VAULT_DENIED; }
    unsigned own=0;for(uint32_t i=1;i<=v->last;i++)if((*vault_file(v,i)).state==VAULT_UPLOAD_ACTIVE && !strcmp((*vault_file(v,i)).upload.publisher,p->subject))own++;
    if(v->active_uploads>=VAULT_UPLOADS || own>=VAULT_UPLOADS_PER_SUBJECT || (uint64_t)v->event_count+v->active_uploads+2>v->event_limit)r=VAULT_LIMIT;
    else if((r=capacity(v,size,&e.id))==VAULT_OK) { e.created=timestamp();r=append(v,fd,&e,*category?'r':'R');if(r==VAULT_OK)*id=e.id; }
    CLOSE(fd);return r;
}
static VaultResult upload_access(Vault *v,const Principal *p,uint32_t id,int admin) {
    if(!id || id>VAULT_ID_MAX)return VAULT_INVALID;
    if(id>v->last || !(*vault_file(v,id)).state)return VAULT_NOT_FOUND;
    if(admin && allowed(p,CAP_VAULT_MANAGE))return VAULT_OK;
    const char *category=(*vault_file(v,id)).state==VAULT_UPLOAD_COMMITTED?(*vault_file(v,id)).entry.category:(*vault_file(v,id)).upload.category;
    if((*vault_file(v,id)).state==VAULT_UPLOAD_COMMITTED && (*vault_file(v,id)).hidden)return VAULT_NOT_FOUND;
    return allowed(p,CAP_VAULT_UPLOAD) && !strcmp((*vault_file(v,id)).upload.publisher,p->subject) && board_allows(v,p,category,1)?VAULT_OK:VAULT_DENIED;
}
static VaultResult received(Vault *v,uint32_t id,uint32_t *size) {
    char file[4096];uint64_t n=0;path(v,file,id);int exists=file_size(file,&n);
    if(!exists) { part_path(v,id,file);exists=file_size(file,&n); }
    if(exists<0 || n>(*vault_file(v,id)).upload.size)return VAULT_STORAGE;
    *size=(uint32_t)n;return VAULT_OK;
}
VaultResult vault_upload_status(Vault *v,const Principal *p,uint32_t id,VaultUpload *out) {
    if(!allowed(p,CAP_VAULT_UPLOAD) && !allowed(p,CAP_VAULT_MANAGE))return VAULT_DENIED;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    r=upload_access(v,p,id,1);
    if(r==VAULT_OK) {
        memset(out,0,sizeof(*out));out->id=id;out->size=(*vault_file(v,id)).upload.size;out->state=(*vault_file(v,id)).state;memcpy(out->sha256,(*vault_file(v,id)).upload.sha256,32);
        if(out->state==VAULT_UPLOAD_ACTIVE)r=received(v,id,&out->received);
        else if(out->state==VAULT_UPLOAD_COMMITTED)out->received=out->size;
    }
    CLOSE(fd);return r;
}
VaultResult vault_upload_write(Vault *v,const Principal *p,uint32_t id,uint32_t offset,const unsigned char *bytes,size_t count,uint32_t *out) {
    if(count>VAULT_CHUNK) { *out=0;return VAULT_INVALID; }
    return vault_upload_write_block(v,p,id,offset,bytes,count,out);
}
VaultResult vault_upload_write_block(Vault *v,const Principal *p,uint32_t id,uint32_t offset,const unsigned char *bytes,size_t count,uint32_t *out) {
    *out=0;if(!allowed(p,CAP_VAULT_UPLOAD))return VAULT_DENIED;
    if(!bytes || !count || count>VAULT_BLOCK)return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    r=upload_access(v,p,id,0);int blob=-1;
    if(r!=VAULT_OK)goto done_upload;
    if((*vault_file(v,id)).state!=VAULT_UPLOAD_ACTIVE || offset>(*vault_file(v,id)).upload.size || count>(*vault_file(v,id)).upload.size-offset) { r=VAULT_INVALID;goto done_upload; }
    char file[4096];uint64_t n=0;path(v,file,id);int staged=file_size(file,&n);
    if(staged) { r=staged<0?VAULT_STORAGE:VAULT_INVALID;goto done_upload; }
    part_path(v,id,file);blob=regular_open(file,O_RDWR);
    if(blob<0 && errno==ENOENT)blob=regular_open(file,O_RDWR|O_CREAT|O_EXCL);
    struct STAT st;
    if(blob<0 || FSTAT(blob,&st) || st.st_size<0 || st.st_size>(*vault_file(v,id)).upload.size) { r=VAULT_STORAGE;goto done_upload; }
    if(offset>(uint32_t)st.st_size) { r=VAULT_INVALID;goto done_upload; }
    size_t prefix=(size_t)st.st_size-offset;if(prefix>count)prefix=count;
    unsigned char previous[VAULT_BLOCK];
    if(SEEK(blob,offset,SEEK_SET)<0 || !io_all(blob,previous,prefix,0)) { r=VAULT_STORAGE;goto done_upload; }
    if(memcmp(previous,bytes,prefix)) { r=VAULT_INVALID;goto done_upload; }
    if(!authorized(v)) { r=VAULT_DENIED;goto done_upload; }
    if(!io_all(blob,(unsigned char *)bytes+prefix,count-prefix,1) || SYNC(blob) || !dir_sync(v)) { r=VAULT_STORAGE;goto done_upload; }
    *out=(uint32_t)st.st_size;if(offset+count>*out)*out=offset+(uint32_t)count;
done_upload:
    if(blob>=0)CLOSE(blob);
    CLOSE(fd);return r;
}
static VaultResult upload_event(Vault *v,int fd,const Principal *p,uint32_t id,char action) {
    VaultEvent e={0};unsigned char row[ROW];e.file=id;e.action=action;e.at=timestamp();strcpy(e.actor,p->subject);
    encode_event(row,&e);return write_record(v,fd,row);
}
/* Unlink only internally numbered, regular unpublished files under the lock. */
static int remove_staged(const char *file) {
    uint64_t size;int exists=file_size(file,&size);
    return exists==0 || (exists==1 && !remove(file));
}
VaultResult vault_reclaim(Vault *v,const Principal *p,uint32_t id) {
    if(!allowed(p,CAP_VAULT_UPLOAD) && !allowed(p,CAP_VAULT_MANAGE))return VAULT_DENIED;
    if(!id || id>VAULT_ID_MAX)return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if(id>v->last) { CLOSE(fd);return VAULT_NOT_FOUND; }
    VaultEntry *e=&(*vault_file(v,id)).entry;
    if(!e->id)r=VAULT_NOT_FOUND;
    else if(!board_manage(v,p,e->category) && (!allowed(p,CAP_VAULT_READ) || strcmp(e->publisher,p->subject) || !board_allows(v,p,e->category,0) || !board_allows(v,p,e->category,1)))r=VAULT_DENIED;
    else {
        if(!(*vault_file(v,id)).deleted) { VaultEvent event={0};unsigned char row[ROW];event.file=id;event.action='F';event.at=timestamp();strcpy(event.actor,p->subject);encode_event(row,&event);r=write_record(v,fd,row); }
        if(r==VAULT_OK) { char file[4096];path(v,file,id);if(!remove_staged(file) || !dir_sync(v))r=VAULT_STORAGE; }
    }
    CLOSE(fd);return r;
}
VaultResult vault_upload_cancel(Vault *v,const Principal *p,uint32_t id) {
    if(!allowed(p,CAP_VAULT_UPLOAD) && !allowed(p,CAP_VAULT_MANAGE))return VAULT_DENIED;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    r=upload_access(v,p,id,1);
    if(r==VAULT_OK && (*vault_file(v,id)).state==VAULT_UPLOAD_COMMITTED)r=VAULT_INVALID;
    else if(r==VAULT_OK && (*vault_file(v,id)).state==VAULT_UPLOAD_ACTIVE) {
        char file[4096];part_path(v,id,file);int ok=remove_staged(file);path(v,file,id);
        if(!ok || !remove_staged(file) || !dir_sync(v))r=VAULT_STORAGE;
        else r=upload_event(v,fd,p,id,'X');
    }
    CLOSE(fd);return r;
}
VaultResult vault_upload_finish(Vault *v,const Principal *p,uint32_t id,VaultEntry *out) {
    if(!allowed(p,CAP_VAULT_UPLOAD))return VAULT_DENIED;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    r=upload_access(v,p,id,0);int blob=-1;
    if(r!=VAULT_OK)goto finish_done;
    if((*vault_file(v,id)).state==VAULT_UPLOAD_COMMITTED) { *out=(*vault_file(v,id)).entry;goto finish_done; }
    if((*vault_file(v,id)).state!=VAULT_UPLOAD_ACTIVE) { r=VAULT_INVALID;goto finish_done; }
    char file[4096],part[4096];path(v,file,id);part_path(v,id,part);uint64_t size=0;
    int staged=file_size(file,&size);if(staged<0) { r=VAULT_STORAGE;goto finish_done; }
    blob=regular_open(staged?file:part,O_RDWR);
    if(blob<0 && errno==ENOENT && !staged && !(*vault_file(v,id)).upload.size)blob=regular_open(part,O_RDWR|O_CREAT|O_EXCL);
    struct STAT st;
    if(blob<0 || FSTAT(blob,&st)) { r=VAULT_STORAGE;goto finish_done; }
    if(st.st_size!=(*vault_file(v,id)).upload.size) { r=VAULT_INVALID;goto finish_done; }
    EVP_MD_CTX *hash=EVP_MD_CTX_new();unsigned char bytes[8192],sum[32];unsigned len=0;
    int ok=hash && EVP_DigestInit_ex(hash,EVP_sha256(),NULL)==1;uint32_t left=(*vault_file(v,id)).upload.size;
    while(ok && left) { size_t n=left>sizeof(bytes)?sizeof(bytes):left;ok=io_all(blob,bytes,n,0) && EVP_DigestUpdate(hash,bytes,n)==1;left-=(uint32_t)n; }
    if(ok)ok=EVP_DigestFinal_ex(hash,sum,&len)==1 && len==32;
    EVP_MD_CTX_free(hash);
    if(!ok) { r=VAULT_STORAGE;goto finish_done; }
    if(memcmp(sum,(*vault_file(v,id)).upload.sha256,32)) { r=VAULT_INVALID;goto finish_done; }
    if(SYNC(blob)) { r=VAULT_STORAGE;goto finish_done; }
    CLOSE(blob);blob=-1;
    if(!staged) {
#ifdef _WIN32
        if(!CreateHardLinkA(file,part,NULL)) { r=VAULT_STORAGE;goto finish_done; }
#else
        if(link(part,file)) { r=VAULT_STORAGE;goto finish_done; }
#endif
    }
    if(!dir_sync(v) || !remove_staged(part) || !dir_sync(v)) { r=VAULT_STORAGE;goto finish_done; }
    r=upload_event(v,fd,p,id,'P');if(r==VAULT_OK)*out=(*vault_file(v,id)).entry;
finish_done:
    if(blob>=0)CLOSE(blob);
    CLOSE(fd);return r;
}

static VaultResult file_property(Vault *v,const Principal *p,uint32_t id,char kind,const char *value) {
    if(!allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(!id || id>VAULT_ID_MAX || !text_ok(value,kind=='N'?100:79,0) || (kind=='N' && (strpbrk(value,"/\\:") || !strcmp(value,".") || !strcmp(value,".."))))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if(id>v->last) { CLOSE(fd);return VAULT_NOT_FOUND; }
    VaultEntry *e=&(*vault_file(v,id)).entry;
    if(!e->id || (*vault_file(v,id)).deleted)r=VAULT_NOT_FOUND;
    else if(!board_manage(v,p,e->category) && ((kind=='O' || kind=='N') || strcmp(e->publisher,p->subject) || !board_allows(v,p,e->category,1)))r=VAULT_DENIED;
    else { VaultEvent event={0};unsigned char row[ROW];event.file=id;event.at=timestamp();event.action=kind;strcpy(event.actor,p->subject);strcpy(event.after,value);encode_event(row,&event);r=write_record(v,fd,row); }
    CLOSE(fd);return r;
}
VaultResult vault_rename(Vault *v,const Principal *p,uint32_t id,const char *name) { return file_property(v,p,id,'N',name); }
VaultResult vault_owner(Vault *v,const Principal *p,uint32_t id,const char *subject) { return file_property(v,p,id,'O',subject); }
VaultResult vault_ascii(Vault *v,const Principal *p,uint32_t id,int ascii) { return ascii==0 || ascii==1?file_property(v,p,id,'T',ascii?"1":"0"):VAULT_INVALID; }
static int vault_board_has_files(Vault *v,const char *name) { for(uint32_t i=1;i<=v->last;i++)if(!(*vault_file(v,i)).deleted && (((*vault_file(v,i)).entry.id && !strcmp((*vault_file(v,i)).entry.category,name)) || ((*vault_file(v,i)).state==VAULT_UPLOAD_ACTIVE && !strcmp((*vault_file(v,i)).upload.category,name))))return 1;return 0; }
VaultResult vault_board_metadata(Vault *v,const Principal *p,const char *name,char kind,const char *value) {
    if(!allowed(p,CAP_VAULT_READ))return VAULT_DENIED;
    if(!board_path(name) || !strchr("noipv",kind) || !kind)return VAULT_INVALID;
    if(kind=='v') { unsigned order,keep;int used=0;char type[8],canonical[100];if(sscanf(value,"%7s %u %u%n",type,&order,&keep,&used)!=3 || value[used] || (strcmp(type,"MENU") && strcmp(type,"BOARD")) || keep>1000000)return VAULT_INVALID;snprintf(canonical,sizeof(canonical),"%s %u %u",type,order,keep);if(strcmp(canonical,value))return VAULT_INVALID; }
    size_t n=strlen(value);if(n>(kind=='i'?8192u:kind=='n'?80u:kind=='p'?97u:kind=='v'?100u:79u))return VAULT_INVALID;
    if(kind!='i' && !text_ok(value,kind=='n'?80:kind=='p'?97:kind=='v'?100:79,1))return VAULT_INVALID;
    if(kind=='i' && !guide_ok(value))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;int index=board_index(v,name);
    if(index<0)r=VAULT_NOT_FOUND;
    else if(!board_manage(v,p,name))r=VAULT_DENIED;
    else if(kind=='o' && !allowed(p,CAP_VAULT_MANAGE)) {
        int inherited=0;for(unsigned i=0;i<v->board_count;i++)if(strcmp(name,v->boards[i].name) && under(name,v->boards[i].name) && !strcmp(v->boards[i].sigop,p->subject))inherited=1;
        if(!inherited)r=VAULT_DENIED;
        else { VaultEvent event={0};unsigned char row[ROW];event.file=(unsigned)index+1;event.at=timestamp();event.action=kind;strcpy(event.actor,p->subject);strcpy(event.after,value);encode_event(row,&event);r=write_record(v,fd,row); }
    }
    else if(kind=='v' && !strncmp(value,"MENU ",5) && vault_board_has_files(v,name))r=VAULT_INVALID;
    else if((uint64_t)v->event_count+v->active_uploads+(kind=='i'?2+(n+235)/236:1)>v->event_limit)r=VAULT_LIMIT;
    else { VaultEvent event={0};unsigned char row[ROW];event.file=(unsigned)index+1;event.at=timestamp();event.action=kind;strcpy(event.actor,p->subject);
        if(kind=='i')snprintf(event.after,sizeof(event.after),"%zu",n);else strcpy(event.after,value);
        encode_event(row,&event);r=write_record(v,fd,row);
        if(kind=='i') { for(size_t pos=0;r==VAULT_OK && pos<n;) { size_t len=n-pos;if(len>240) { len=240;while(len && ((unsigned char)value[pos+len]&0xc0)==0x80)len--; }event.action='j';memcpy(event.after,value+pos,len);event.after[len]=0;encode_event(row,&event);r=write_record(v,fd,row);pos+=len; }
            if(r==VAULT_OK) { event.action='k';event.after[0]=0;encode_event(row,&event);r=write_record(v,fd,row); }
            if(r!=VAULT_OK)v->broken=1;
        }
    }
    CLOSE(fd);return r;
}

/* Copy-only offline snapshot. The caller keeps the complete backup as audit
 * history; file IDs, active reservations, ACLs and the latest state survive. */
static VaultResult snapshot_event(Vault *v,int fd,char kind,uint32_t id,const char *actor,const char *text,uint64_t at) {
    VaultEvent e={0};e.action=kind;e.file=id;e.at=at;snprintf(e.actor,sizeof(e.actor),"%s",actor);snprintf(e.after,sizeof(e.after),"%s",text);unsigned char row[ROW];encode_event(row,&e);return write_record(v,fd,row);
}
VaultResult vault_compact_index(Vault *v,const char *output) {
    int input;VaultResult r=begin(v,&input);if(r!=VAULT_OK)return r;
    int out=regular_open(output,O_RDWR|O_CREAT|O_EXCL);if(out<0) { CLOSE(input);return VAULT_STORAGE; }
    Vault *copy=calloc(1,sizeof(*copy));if(!copy) { CLOSE(out);CLOSE(input);return VAULT_STORAGE; }
    copy->event_limit=copy->file_limit=VAULT_ID_MAX;copy->byte_limit=UINT64_MAX;
    if(!io_all(out,(unsigned char *)"LNVLT001",8,1))r=VAULT_STORAGE;
    for(unsigned i=0;r==VAULT_OK && i<v->board_count;i++) {
        FileBoard *b=&v->boards[i];char text[241];snprintf(text,sizeof(text),"%s|%s|%s",*b->name?b->name:"-",b->read,b->upload);
        r=snapshot_event(copy,out,'B',0,"snapshot",text,0);
        if(r==VAULT_OK && *b->display)r=snapshot_event(copy,out,'n',i+1,"snapshot",b->display,0);
        if(r==VAULT_OK && *b->sigop)r=snapshot_event(copy,out,'o',i+1,"snapshot",b->sigop,0);
        if(r==VAULT_OK && *b->password)r=snapshot_event(copy,out,'p',i+1,"snapshot",b->password,0);
        if(r==VAULT_OK) { snprintf(text,sizeof(text),"%s %u %u",b->menu?"MENU":"BOARD",b->order,b->keep);r=snapshot_event(copy,out,'v',i+1,"snapshot",text,0); }
        if(r==VAULT_OK && *b->info) {
            size_t length=strlen(b->info);snprintf(text,sizeof(text),"%zu",length);r=snapshot_event(copy,out,'i',i+1,"snapshot",text,0);
            for(size_t at=0;r==VAULT_OK && at<length;) { size_t n=length-at;if(n>236)n=236;memcpy(text,b->info+at,n);text[n]=0;r=snapshot_event(copy,out,'j',i+1,"snapshot",text,0);at+=n; }
            if(r==VAULT_OK)r=snapshot_event(copy,out,'k',i+1,"snapshot","",0);
        }
    }
    for(uint32_t id=1;r==VAULT_OK && id<=v->last;id++) {
        FileState *f=&(*vault_file(v,id));if(!f->entry.id && !f->upload.id)continue;
        VaultEntry e=f->state==VAULT_UPLOAD_COMMITTED?f->entry:f->upload.id?f->upload:f->entry;
        if(f->upload.id) { strcpy(e.publisher,f->upload.publisher);e.created=f->upload.created; }
        unsigned char row[ROW];encode(row,&e,f->upload.id?(*e.category?'r':'R'):(*e.category?'a':'A'));r=write_record(copy,out,row);
        if(r==VAULT_OK && *f->protocol)r=snapshot_event(copy,out,'Q',id,"snapshot",f->protocol,0);
        if(r==VAULT_OK && f->state==VAULT_UPLOAD_COMMITTED)r=snapshot_event(copy,out,'P',id,f->upload.publisher,"",f->entry.created);
        if(r==VAULT_OK && f->state==VAULT_UPLOAD_CANCELLED)r=snapshot_event(copy,out,'X',id,f->upload.publisher,"",0);
        if(r==VAULT_OK && f->entry.id) {
            if(strcmp(e.publisher,f->entry.publisher))r=snapshot_event(copy,out,'O',id,"snapshot",f->entry.publisher,0);
            if(r==VAULT_OK && f->entry.ascii)r=snapshot_event(copy,out,'T',id,"snapshot","1",0);
            if(r==VAULT_OK && (f->deleted || f->hidden))r=snapshot_event(copy,out,f->deleted?'F':'H',id,"snapshot","",0);
        }
    }
    if(r==VAULT_OK && (copy->last!=v->last || copy->active_uploads!=v->active_uploads || SYNC(out)))r=VAULT_STORAGE;
    vault_close(copy);CLOSE(out);CLOSE(input);return r;
}

VaultResult vault_password(Vault *v,const Principal *p,const char *name,const char *password) {
    if(!*password)return vault_board_metadata(v,p,name,'p',"");
    size_t n=strlen(password);unsigned char salt[16],hash[32];char encoded[98];if(n<8 || n>128)return VAULT_INVALID;
    if(RAND_bytes(salt,16)!=1 || PKCS5_PBKDF2_HMAC(password,(int)n,salt,16,600000,EVP_sha256(),32,hash)!=1)return VAULT_STORAGE;
    for(unsigned i=0;i<16;i++)snprintf(encoded+i*2,3,"%02x",salt[i]);
    encoded[32]=' ';
    for(unsigned i=0;i<32;i++)snprintf(encoded+33+i*2,3,"%02x",hash[i]);
    VaultResult result=vault_board_metadata(v,p,name,'p',encoded);OPENSSL_cleanse(hash,sizeof(hash));return result;
}
VaultResult vault_unlock(Vault *v,const Principal *p,const char *name,const char *password,uint32_t *id,uint32_t *revision) {
    if(!allowed(p,CAP_VAULT_READ) || strlen(password)>128)return VAULT_DENIED;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;int i=board_index(v,name);
    if(i<0 || !v->boards[i].revision)r=VAULT_NOT_FOUND;
    else { unsigned char salt[16],expected[32],hash[32];FileBoard *b=&v->boards[i];
        for(unsigned j=0;j<48;j++) { unsigned x;const char *s=b->password+(j<16?j*2:33+(j-16)*2);if(sscanf(s,"%2x",&x)!=1) { CLOSE(fd);return VAULT_STORAGE; }if(j<16)salt[j]=(unsigned char)x;else expected[j-16]=(unsigned char)x; }
        if(PKCS5_PBKDF2_HMAC(password,(int)strlen(password),salt,16,600000,EVP_sha256(),32,hash)!=1)r=VAULT_STORAGE;
        else if(CRYPTO_memcmp(hash,expected,32))r=VAULT_DENIED;
        else { *id=(uint32_t)i+1;*revision=b->revision; }
        OPENSSL_cleanse(hash,sizeof(hash));
    }CLOSE(fd);return r;
}

int vault_log_policy(Vault *v,uint32_t file,unsigned *board,unsigned *keep) {
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return 0;int i=file<=v->last && (*vault_file(v,file)).entry.id?board_index(v,(*vault_file(v,file)).entry.category):-1;
    *board=i>=0?(unsigned)i+1:0;*keep=i>=0?v->boards[i].keep:0;CLOSE(fd);return 1;
}

VaultResult vault_copy(Vault *v,const Principal *p,uint32_t id,const char *category,VaultEntry *out) {
    if(!board_path(category))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    int source=-1,target=-1;char src[4096],dst[4096];VaultEntry e={0};
    if(!id || id>v->last || !(*vault_file(v,id)).entry.id || (*vault_file(v,id)).hidden) { r=VAULT_NOT_FOUND;goto done_copy; }
    e=(*vault_file(v,id)).entry;
    if((v->board_count && board_index(v,category)<0) || !board_manage(v,p,e.category) || !board_manage(v,p,category) || !board_allows(v,p,category,1)) { r=VAULT_DENIED;goto done_copy; }
    if(e.size>ln_limit("FILE",VAULT_MAX_BYTES) || (uint64_t)v->event_count+v->active_uploads+3>v->event_limit) { r=VAULT_LIMIT;goto done_copy; }
    r=capacity(v,e.size,&e.id);if(r!=VAULT_OK)goto done_copy;
    path(v,src,id);path(v,dst,e.id);source=regular_open(src,O_RDONLY);if(source<0) { r=VAULT_STORAGE;goto done_copy; }
    target=regular_open(dst,O_WRONLY|O_CREAT|O_EXCL);if(target<0) { r=VAULT_STORAGE;goto done_copy; }
    EVP_MD_CTX *hash=EVP_MD_CTX_new();int ok=hash && EVP_DigestInit_ex(hash,EVP_sha256(),NULL)==1;unsigned char bytes[8192],digest_[32];unsigned length=0;uint32_t remaining=e.size;
    while(ok && remaining) { size_t n=remaining>sizeof(bytes)?sizeof(bytes):remaining;ok=authorized(v) && io_all(source,bytes,n,0) && io_all(target,bytes,n,1) && EVP_DigestUpdate(hash,bytes,n)==1;remaining-=(uint32_t)n; }
    if(ok)ok=READ(source,bytes,1)==0 && EVP_DigestFinal_ex(hash,digest_,&length)==1 && length==32 && !memcmp(digest_,e.sha256,32) && !SYNC(target);
    EVP_MD_CTX_free(hash);CLOSE(target);target=-1;
    if(!ok || !dir_sync(v)) { remove(dst);r=VAULT_STORAGE;goto done_copy; }
    strcpy(e.category,category);r=append(v,fd,&e,*category?'a':'A');
    if(r==VAULT_OK)r=snapshot_event(v,fd,'Q',e.id,p->subject,"COPY",timestamp());
    if(r==VAULT_OK && e.ascii)r=snapshot_event(v,fd,'T',e.id,p->subject,"1",timestamp());
    if(r==VAULT_OK)*out=e;
done_copy:if(source>=0)CLOSE(source);if(target>=0)CLOSE(target);CLOSE(fd);return r;
}

VaultResult vault_upload_protocol(Vault *v,const Principal *p,uint32_t id,const char *protocol) {
    if(!*protocol || strlen(protocol)>11 || !category_ok(protocol))return VAULT_INVALID;
    int fd;VaultResult r=begin(v,&fd);if(r!=VAULT_OK)return r;
    if(!id || id>v->last)r=VAULT_NOT_FOUND;
    else {
        FileState *f=vault_file(v,id);
        if(!f->upload.id || strcmp(f->upload.publisher,p->subject))r=VAULT_DENIED;
        else if(strcmp(f->protocol,protocol)) {
            if((uint64_t)v->event_count+v->active_uploads+1>v->event_limit)r=VAULT_LIMIT;
            else r=snapshot_event(v,fd,'Q',id,p->subject,protocol,timestamp());
        }
    }
    CLOSE(fd);return r;
}
VaultResult vault_protocol(Vault *v,const Principal *p,uint32_t id,char out[12]) {
    VaultEntry entry;VaultResult r=vault_info(v,p,id,&entry);if(r!=VAULT_OK)return r;
    int fd;r=begin(v,&fd);if(r!=VAULT_OK)return r;
    snprintf(out,12,"%s",*vault_file(v,id)->protocol?vault_file(v,id)->protocol:"UNKNOWN");CLOSE(fd);return r;
}

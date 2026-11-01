#include "vault.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
static int number(const char *s,uint32_t *id) {
    unsigned n=0;if(!*s)return 0;
    for(;*s;s++) { if(*s<'0'||*s>'9')return 0;if(n>(VAULT_ID_MAX-(unsigned)(*s-'0'))/10)return 0;n=n*10+(unsigned)(*s-'0');if(n>VAULT_ID_MAX)return 0; }
    *id=n;return n!=0;
}
static int decimal(const char *s,uint32_t maximum,uint32_t *out) {
    uint32_t n=0;if(!*s)return 0;
    for(;*s;s++) { if(*s<'0'||*s>'9' || n>(maximum-(unsigned)(*s-'0'))/10)return 0;n=n*10+(unsigned)(*s-'0');if(n>maximum)return 0; }
    *out=n;return 1;
}
static int hash_arg(const char *s,unsigned char out[32]) {
    if(strlen(s)!=64)return 0;
    for(int i=0;i<32;i++) { unsigned n=0;for(int j=0;j<2;j++) { unsigned c=(unsigned char)s[2*i+j];if(c>='0' && c<='9')c-='0';else if(c>='a' && c<='f')c=c-'a'+10;else return 0;n=n*16+c; }out[i]=(unsigned char)n; }
    return 1;
}
static void show(VaultEntry *e) { char hash[65];vault_hex(e->sha256,hash);printf("%u %u %s %s\n",e->id,e->size,hash,e->name); }
static int vaultctl_main(int argc,char **argv) {
    if(argc<3) goto usage;
    if(!strcmp(argv[1],"init") && argc==3) { VaultResult r=vault_init(argv[2]);printf("%s\n",vault_result_name(r));return r!=VAULT_OK; }
    Vault *v=NULL;VaultResult r=vault_open(argv[2],&v);if(r!=VAULT_OK) { fprintf(stderr,"%s\n",vault_result_name(r));return 1; }
    Principal p={"local-operator",CAP_VAULT_READ|CAP_VAULT_MANAGE|CAP_VAULT_UPLOAD,0,0,{0},{0}};uint32_t id=0;VaultEntry e;
    if(!strcmp(argv[1],"compact-index") && argc==4)r=vault_compact_index(v,argv[3]);
    else if(!strcmp(argv[1],"board-set") && argc==6)r=vault_board_set(v,&p,!strcmp(argv[3],"-")?"":argv[3],argv[4],argv[5]);
    else if(!strcmp(argv[1],"import-in") && argc==7) { r=vault_import_in(v,&p,argv[4],argv[5],argv[6],!strcmp(argv[3],"-")?"":argv[3],&e);if(r==VAULT_OK)show(&e); }
    else if(!strcmp(argv[1],"boards") && argc==3) {
        uint32_t cursor=0;do { VaultBoard rows[8];size_t count;r=vault_boards(v,&p,cursor,rows,&count,&cursor);if(r!=VAULT_OK)break;
            for(size_t i=0;i<count;i++)printf("%s|%s|%s\n",*rows[i].name?rows[i].name:"-",rows[i].read,rows[i].upload);
        } while(cursor);
    }
    else if(!strcmp(argv[1],"upload-start") && argc==7) {
        uint32_t size;unsigned char hash[32];r=VAULT_INVALID;
        if(decimal(argv[3],VAULT_MAX_BYTES,&size) && hash_arg(argv[4],hash))r=vault_upload_start(v,&p,size,hash,argv[5],argv[6],&id);
        if(r==VAULT_OK)printf("UPLOAD %u\n",id);
    }
    else if(!strcmp(argv[1],"upload-write") && argc==6 && number(argv[3],&id)) {
        uint32_t offset,next;r=VAULT_INVALID;
        if(decimal(argv[4],VAULT_MAX_BYTES,&offset)) {
            FILE *in=fopen(argv[5],"rb");
            if(!in)r=VAULT_STORAGE;
            else {
                unsigned char bytes[VAULT_CHUNK+1];size_t n=fread(bytes,1,sizeof(bytes),in);int error=ferror(in);fclose(in);
                r=error?VAULT_STORAGE:vault_upload_write(v,&p,id,offset,bytes,n,&next);
                if(r==VAULT_OK)printf("RECEIVED %u\n",next);
            }
        }
    }
    else if(!strcmp(argv[1],"upload-status") && argc==4 && number(argv[3],&id)) {
        VaultUpload state;r=vault_upload_status(v,&p,id,&state);
        if(r==VAULT_OK)printf("UPLOAD %u %s %u %u\n",id,state.state==VAULT_UPLOAD_ACTIVE?"ACTIVE":state.state==VAULT_UPLOAD_COMMITTED?"COMMITTED":"CANCELLED",state.received,state.size);
    }
    else if(!strcmp(argv[1],"upload-finish") && argc==4 && number(argv[3],&id)) { r=vault_upload_finish(v,&p,id,&e);if(r==VAULT_OK)show(&e); }
    else if(!strcmp(argv[1],"upload-cancel") && argc==4 && number(argv[3],&id))r=vault_upload_cancel(v,&p,id);
    else if(!strcmp(argv[1],"import") && argc==6) { r=vault_import(v,&p,argv[3],argv[4],argv[5],&e);if(r==VAULT_OK)show(&e); }
    else if(!strcmp(argv[1],"category") && argc==5 && number(argv[3],&id)) r=vault_category(v,&p,id,!strcmp(argv[4],"-")?"":argv[4]);
    else if(!strcmp(argv[1],"describe") && argc==5 && number(argv[3],&id))r=vault_description(v,&p,id,argv[4]);
    else if(!strcmp(argv[1],"delete") && argc==5 && !strcmp(argv[4],"CONFIRM") && number(argv[3],&id))r=vault_reclaim(v,&p,id);
    else if(!strcmp(argv[1],"restore") && argc==4 && number(argv[3],&id))r=vault_restore(v,&p,id);
    else if(!strcmp(argv[1],"audit") && argc==3) {
        uint32_t cursor=0;do { VaultEvent events[4];size_t count;r=vault_audit(v,&p,cursor,events,&count,&cursor);if(r!=VAULT_OK)break;
            for(size_t i=0;i<count;i++)printf("%u file=%u time=%llu action=%c actor=%s category=%s -> %s\n",events[i].sequence,events[i].file,(unsigned long long)events[i].at,events[i].action,events[i].actor,*events[i].before?events[i].before:"-",*events[i].after?events[i].after:"-");
        } while(cursor);
    }
    else if(!strcmp(argv[1],"categories") && argc==3) {
        uint32_t cursor=0;do { VaultCategory rows[8];size_t count;r=vault_categories(v,&p,cursor,rows,&count,&cursor);if(r!=VAULT_OK)break;
            for(size_t i=0;i<count;i++)printf("%u %s\n",rows[i].count,*rows[i].name?rows[i].name:"-");
        } while(cursor);
    }
    else if((!strcmp(argv[1],"search") || !strcmp(argv[1],"in")) && argc==4) {
        uint32_t cursor=0;do { VaultEntry rows[8];size_t count;int search=!strcmp(argv[1],"search");
            r=vault_search(v,&p,cursor,search?argv[3]:"",search?NULL:!strcmp(argv[3],"-")?"":argv[3],rows,&count,&cursor);if(r!=VAULT_OK)break;
            for(size_t i=0;i<count;i++)show(&rows[i]);
        } while(cursor);
    }
    else if(!strcmp(argv[1],"list") && argc==3) {
        uint32_t cursor=0;do { VaultEntry entries[8];size_t count;r=vault_list(v,&p,cursor,entries,&count,&cursor);if(r!=VAULT_OK)break;for(size_t i=0;i<count;i++)show(&entries[i]); } while(cursor);
    } else if(argc>=4 && number(argv[3],&id) && !strcmp(argv[1],"info") && argc==4) { r=vault_info(v,&p,id,&e);if(r==VAULT_OK) { show(&e);printf("%s\nCATEGORY %s\n",e.description,*e.category?e.category:"-"); } }
    else if(argc>=4 && number(argv[3],&id) && !strcmp(argv[1],"hide") && argc==4) r=vault_hide(v,&p,id);
    else if(argc==5 && number(argv[3],&id) && !strcmp(argv[1],"export")) {
        r=vault_info(v,&p,id,&e);
        if(r==VAULT_OK) {
            FILE *out=fopen(argv[4],"wbx");
            if(!out) r=VAULT_STORAGE;
            else {
                EVP_MD_CTX *hash=EVP_MD_CTX_new();unsigned char bytes[VAULT_CHUNK],digest[32];unsigned len=0;
                if(!hash || EVP_DigestInit_ex(hash,EVP_sha256(),NULL)!=1) r=VAULT_STORAGE;
                for(uint32_t offset=0;r==VAULT_OK && offset<e.size;) {
                    size_t count;r=vault_read(v,&p,id,offset,bytes,&count);
                    if(r==VAULT_OK && (!count || fwrite(bytes,1,count,out)!=count || EVP_DigestUpdate(hash,bytes,count)!=1)) r=VAULT_STORAGE;
                    offset+=(uint32_t)count;
                }
                if(r==VAULT_OK && (EVP_DigestFinal_ex(hash,digest,&len)!=1 || len!=32 || memcmp(digest,e.sha256,32))) r=VAULT_STORAGE;
                EVP_MD_CTX_free(hash);if(fclose(out))r=VAULT_STORAGE;
                if(r!=VAULT_OK) remove(argv[4]);
            }
        }
    } else { vault_close(v);goto usage; }
    vault_close(v);if(r!=VAULT_OK)fprintf(stderr,"%s\n",vault_result_name(r));return r!=VAULT_OK;
usage:
    fprintf(stderr,"usage: vaultctl board-set ROOT PATH_OR_MINUS READ_ACL UPLOAD_ACL | boards ROOT | import-in ROOT CATEGORY_OR_MINUS SOURCE NAME DESCRIPTION | init ROOT | import ROOT SOURCE NAME DESCRIPTION | list ROOT | info ROOT ID | hide ROOT ID | export ROOT ID NEW_DESTINATION | category ROOT ID NAME_OR_MINUS | categories ROOT | search ROOT QUERY | in ROOT CATEGORY_OR_MINUS | describe ROOT ID TEXT | restore ROOT ID | audit ROOT | upload-start ROOT SIZE SHA256 NAME DESCRIPTION | upload-write ROOT ID OFFSET CHUNK_FILE | upload-status ROOT ID | upload-finish ROOT ID | upload-cancel ROOT ID\n");return 2;
}

#ifdef _WIN32
#include <windows.h>
int wmain(int argc,wchar_t **wide) {
    char **args=calloc((size_t)argc+1,sizeof(*args));if(!args)return 1;
    int ok=1;
    for(int i=0;i<argc;i++) {
        int n=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,NULL,0,NULL,NULL);
        if(n<=0 || !(args[i]=malloc((size_t)n))) { ok=0;break; }
        if(!WideCharToMultiByte(CP_UTF8,0,wide[i],-1,args[i],n,NULL,NULL)) { ok=0;break; }
    }
    int result=ok?vaultctl_main(argc,args):1;
    for(int i=0;i<argc;i++)free(args[i]);
    free(args);return result;
}
#else
int main(int argc,char **argv) { return vaultctl_main(argc,argv); }
#endif

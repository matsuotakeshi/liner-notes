#include "download.h"
#include "download_core.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <openssl/evp.h>
static int parse_hash(const char *text,unsigned char out[32]) {
    if(strlen(text)!=64)return 0;
    for(unsigned i=0;i<32;i++) { unsigned x=0;for(unsigned j=0;j<2;j++) { unsigned c=(unsigned char)text[i*2+j];if(c>='0' && c<='9')c-='0';else if(c>='a' && c<='f')c=c-'a'+10;else return 0;x=x*16+c; }out[i]=(unsigned char)x; }return 1;
}

static const char *download_error(DownloadOutcome r) {
    switch(r) {
    case DOWNLOAD_DENIED:return "ERR vault denied";
    case DOWNLOAD_NOT_FOUND:return "ERR no file";
    case DOWNLOAD_BUSY:return "ERR vault busy";
    case DOWNLOAD_STORAGE:return "ERR vault storage";
    case DOWNLOAD_TICKET:return "ERR download ticket";
    default:return "ERR vault syntax";
    }
}
int downloads_command_result(Vault *v,const Principal *p,const char *line,const char *protocol,char *out,size_t size,int *failed) {
    *failed=0;
    int block=!strncmp(line,"FILEFETCHBLOCK ",15);
    if(!block && strncmp(line,"FILEOPEN ",9) && strncmp(line,"FILEFETCH ",10) && strncmp(line,"FILEDONE ",9) && strncmp(line,"FILEABORT ",10))return 0;
    DownloadOutcome status=download_core_access(p);if(status!=DOWNLOAD_OK)goto fail;
    status=DOWNLOAD_INVALID;char token[33]={0},hash[65]={0};uint32_t id=0,offset=0;int n=0;
    if(!strncmp(line,"FILEOPEN ",9)) {
        if(sscanf(line+9,"%u%n",&id,&n)!=1 || line[9+n])goto fail;
        const char *names[]={"LINE","ZMODEM","XMODEM","YMODEM","YMODEM-G","MLINK","TEXT","XMODEM-SUM","XMODEM-1K"};
        unsigned kind=0;while(kind<9 && strcmp(protocol,names[kind]))kind++;
        DownloadOpened opened;status=download_core_open(v,p,id,(DownloadProtocol)kind,&opened);if(status!=DOWNLOAD_OK)goto fail;
        vault_hex(opened.sha256,hash);snprintf(out,size,"DOWNLOAD %s %u %s\r\n",opened.token,opened.size,hash);return 1;
    }
    if(block || !strncmp(line,"FILEFETCH ",10)) {
        int start=block?15:10;
        if(sscanf(line+start,"%32s %u%n",token,&offset,&n)!=2 || line[start+n])goto fail;
        DownloadBlock data;status=download_core_fetch(v,p,token,offset,block?VAULT_BLOCK:VAULT_CHUNK,&data);if(status!=DOWNLOAD_OK)goto fail;
        unsigned char encoded[(VAULT_BLOCK+2)/3*4+1];EVP_EncodeBlock(encoded,data.bytes,(int)data.size);
        snprintf(out,size,"DOWNLOADDATA %u %zu %s\r\n",data.offset,data.size,data.size?(char *)encoded:"-");return 1;
    }
    if(!strncmp(line,"FILEDONE ",9)) {
        if(sscanf(line+9,"%32s %64s%n",token,hash,&n)!=2 || line[9+n])goto fail;
        DownloadDigest digest={0};digest.valid=parse_hash(hash,digest.bytes);
        DownloadCompleted completed;status=download_core_finish(v,p,token,&digest,&completed);if(status!=DOWNLOAD_OK)goto fail;
        snprintf(out,size,"DOWNLOADED %u\r\n",completed.file);return 1;
    }
    if(sscanf(line+10,"%32s%n",token,&n)!=1 || line[10+n])goto fail;
    status=download_core_abort(p,token);if(status!=DOWNLOAD_OK)goto fail;
    snprintf(out,size,"DOWNLOAD ABORTED\r\n");return 1;
fail:*failed=1;snprintf(out,size,"%s\r\n",download_error(status));return 1;
}
int downloads_report_page(Vault *v,const Principal *p,int history,uint32_t file,uint32_t cursor,char *out,size_t size,uint32_t *next) {
    *next=0;
    DownloadOutcome status;
    if(history) {
        DownloadHistory page;status=download_core_history(p,file,cursor,&page);if(status!=DOWNLOAD_OK)goto fail;
        size_t used=(size_t)snprintf(out,size,"FILEHISTORY\r\n");
        for(size_t i=0;i<page.count;i++) {
            const DownloadHistoryRow *r=&page.rows[i];
            used+=(size_t)snprintf(out+used,size-used,"DOWNLOADROW %zu %u %s %" PRIu64 " %u %s\r\n",r->sequence,r->file,r->subject,r->at,r->size,r->protocol);
        }
        snprintf(out+used,size-used,"END NEXT %zu\r\n",page.next);*next=(uint32_t)page.next;return 1;
    }
    DownloadStats page;status=download_core_stats(v,p,file,cursor,&page);if(status!=DOWNLOAD_OK)goto fail;
    size_t used=(size_t)snprintf(out,size,"FILESTATS\r\n");
    for(size_t i=0;i<page.count;i++)used+=(size_t)snprintf(out+used,size-used,"DOWNLOADCOUNT %u %" PRIu64 "\r\n",page.rows[i].file,page.rows[i].count);
    snprintf(out+used,size-used,"END NEXT %u\r\n",page.next);*next=(uint32_t)page.next;return 1;
fail:snprintf(out,size,"%s\r\n",download_error(status));return 0;
}

void downloads_report(Vault *v,const Principal *p,int history,uint32_t file,uint32_t cursor,char *out,size_t size) { uint32_t next;(void)downloads_report_page(v,p,history,file,cursor,out,size,&next); }

int downloads_command(Vault *v,const Principal *p,const char *line,const char *protocol,char *out,size_t size) { int failed;return downloads_command_result(v,p,line,protocol,out,size,&failed); }

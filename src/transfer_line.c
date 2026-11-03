#include "transfer_line.h"
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
static void hex(const unsigned char hash[32],char out[65]) { static const char digits[]="0123456789abcdef";for(int i=0;i<32;i++) { out[i*2]=digits[hash[i]>>4];out[i*2+1]=digits[hash[i]&15]; }out[64]=0; }
int transfer_line_command(const char *s) {
    static const char *names[]={"FILEOPEN","FILEFETCH","FILEFETCHBLOCK","FILEPUTBLOCK","FILEGETBLOCK","FILEDONE","FILEABORT","FILECHECK","FILEMETA","FILEGET","FILESTART","FILESTARTIN","FILEPUT","FILESTATUS","FILEFINISH","FILECANCEL"};
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        size_t n=strlen(names[i]);if(!strncmp(s,names[i],n) && (!s[n] || s[n]==' '))return 1;
    }
    return 0;
}
static int number(const char *s,uint32_t max,uint32_t *out) {
    uint64_t n=0;if(!*s)return 0;
    for(;*s;s++) { if(*s<'0'||*s>'9')return 0;n=n*10+(unsigned)(*s-'0');if(n>max)return 0; }
    *out=(uint32_t)n;return 1;
}
static int hash(const char *s,unsigned char out[32]) {
    if(strlen(s)!=64)return 0;
    for(int i=0;i<32;i++) { unsigned n=0;for(int j=0;j<2;j++) { unsigned c=(unsigned char)s[2*i+j];if(c>='0' && c<='9')c-='0';else if(c>='a' && c<='f')c=c-'a'+10;else return 0;n=n*16+c; }out[i]=(unsigned char)n; }
    return 1;
}
static int decode(const char *s,unsigned char *out,size_t max,size_t *count) {
    if(!strcmp(s,"-")) { *count=0;return 1; }
    size_t len=strlen(s);unsigned char decoded[VAULT_BLOCK+3],canonical[(VAULT_BLOCK+2)/3*4+1];
    if(!len || len>(VAULT_BLOCK+2)/3*4 || len%4)return 0;
    int n=EVP_DecodeBlock(decoded,(const unsigned char *)s,(int)len);if(n<0)return 0;
    if(s[len-1]=='=')n--;
    if(s[len-2]=='=')n--;
    if(n<0 || (size_t)n>max)return 0;
    EVP_EncodeBlock(canonical,decoded,n);if(strcmp((char *)canonical,s))return 0;
    memcpy(out,decoded,(size_t)n);*count=(size_t)n;return 1;
}
void transfer_line_execute(TransferBackend backend,const Principal *p,const char *line,char *out,size_t capacity) {
    TransferRequest q={0};TransferReply reply={0};char copy[90001],*tokens[6];size_t n=0;
    VaultResult r=VAULT_INVALID;
    if(strlen(line)>90000)goto error;
    strcpy(copy,line);char *cur=copy;
    while(*cur) {
        if(n==6 || *cur==' ')goto error;
        tokens[n++]=cur;char *space=strchr(cur,' ');if(!space)break;
        *space=0;cur=space+1;if(!*cur)goto error;
    }
    if(n<2)goto error;
    if(!strcmp(tokens[0],"FILECHECK")) {
        size_t len;if(n!=2 || !decode(tokens[1],(unsigned char *)q.category,48,&len) || memchr(q.category,0,len))goto error;
        q.operation=TRANSFER_CHECK;
    } else if(!strcmp(tokens[0],"FILESTART") || !strcmp(tokens[0],"FILESTARTIN")) {
        size_t len;
        int in=!strcmp(tokens[0],"FILESTARTIN");
        if(n!=(in?6u:5u) || !number(tokens[1],VAULT_MAX_BYTES,&q.size) || !hash(tokens[2],q.sha256) ||
           !decode(tokens[3],(unsigned char *)q.name,100,&len) || !len || memchr(q.name,0,len))goto error;
        if(!decode(tokens[4],(unsigned char *)q.description,240,&len) || memchr(q.description,0,len))goto error;
        if(in && (!decode(tokens[5],(unsigned char *)q.category,48,&len) || memchr(q.category,0,len)))goto error;
        q.operation=TRANSFER_START;
    } else {
        if(!number(tokens[1],VAULT_ID_MAX,&q.id) || !q.id)goto error;
        if(!strcmp(tokens[0],"FILEGET") || !strcmp(tokens[0],"FILEGETBLOCK")) {
            if(n!=3 || !number(tokens[2],VAULT_MAX_BYTES,&q.offset))goto error;
            q.operation=TRANSFER_READ;q.count=!strcmp(tokens[0],"FILEGETBLOCK")?VAULT_BLOCK:VAULT_CHUNK;
        } else if(!strcmp(tokens[0],"FILEPUT") || !strcmp(tokens[0],"FILEPUTBLOCK")) {
            if(n!=4 || !number(tokens[2],VAULT_MAX_BYTES,&q.offset) || !decode(tokens[3],q.bytes,!strcmp(tokens[0],"FILEPUTBLOCK")?VAULT_BLOCK:TRANSFER_LINE_CHUNK,&q.count) || !q.count)goto error;
            q.operation=TRANSFER_WRITE;
        } else {
            if(n!=2)goto error;
            if(!strcmp(tokens[0],"FILEMETA"))q.operation=TRANSFER_INFO;
            else if(!strcmp(tokens[0],"FILESTATUS"))q.operation=TRANSFER_STATUS;
            else if(!strcmp(tokens[0],"FILEFINISH"))q.operation=TRANSFER_FINISH;
            else if(!strcmp(tokens[0],"FILECANCEL"))q.operation=TRANSFER_CANCEL;
            else goto error;
        }
    }
    if(!backend.execute)goto error;
    r=backend.execute(backend.context,p,&q,&reply);if(r!=VAULT_OK)goto error;
    char digest[65];
    if(q.operation==TRANSFER_INFO || q.operation==TRANSFER_FINISH) {
        hex(reply.entry.sha256,digest);
        snprintf(out,capacity,q.operation==TRANSFER_INFO?"FILE %u %u %s\r\nEND FILE\r\n":"UPLOADED %u %u %s\r\n",reply.entry.id,reply.entry.size,digest);
    } else if(q.operation==TRANSFER_READ) {
        unsigned char encoded[(VAULT_BLOCK+2)/3*4+1];
        if(reply.count>VAULT_BLOCK) { r=VAULT_STORAGE;goto error; }
        EVP_EncodeBlock(encoded,reply.bytes,(int)reply.count);
        snprintf(out,capacity,"FILEDATA %u %u %zu %s\r\n",q.id,q.offset,reply.count,reply.count?(char *)encoded:"-");
    } else if(q.operation==TRANSFER_START)snprintf(out,capacity,"UPLOAD %u\r\n",reply.id);
    else if(q.operation==TRANSFER_WRITE)snprintf(out,capacity,"RECEIVED %u %u\r\n",q.id,reply.received);
    else if(q.operation==TRANSFER_STATUS) {
        hex(reply.upload.sha256,digest);
        snprintf(out,capacity,"UPLOADSTATUS %u %s %u %u %s\r\n",q.id,reply.upload.state==VAULT_UPLOAD_ACTIVE?"ACTIVE":reply.upload.state==VAULT_UPLOAD_COMMITTED?"COMMITTED":"CANCELLED",reply.upload.received,reply.upload.size,digest);
    } else if(q.operation==TRANSFER_CHECK)snprintf(out,capacity,"FILEBOARD READY\r\n");
    else snprintf(out,capacity,"UPLOAD CANCELLED %u\r\n",q.id);
    return;
error:
    snprintf(out,capacity,"%s\r\n",r==VAULT_DENIED?"ERR vault denied":r==VAULT_NOT_FOUND?"ERR no file":r==VAULT_BUSY?"ERR vault busy":r==VAULT_LIMIT?"ERR vault limit":r==VAULT_STORAGE?"ERR vault storage":"ERR vault syntax");
}

#include "connector_protocol.h"
#include "store_limits.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <openssl/evp.h>
static int wire_hex(const char *text,size_t size) {
    for(size_t i=0;i<size;i++)if(!((text[i]>='0' && text[i]<='9') || (text[i]>='a' && text[i]<='f')))return 0;
    return 1;
}
static int wire_number(const char *p,uint32_t *number,const char **rest) {
    if(*p<'0' || *p>'9') return 0;
    uint32_t n=0;
    while(*p>='0' && *p<='9') { unsigned digit=(unsigned)(*p++-'0');if(n>(CONTENT_ID_MAX+1-digit)/10)return 0;n=n*10+digit; }
    if(!n) return 0;
    *number=n; *rest=p; return 1;
}

static int wire_safe_text(const char *p) {
    for(;*p;p++)if((unsigned char)*p<32 || (unsigned char)*p==127)return 0;
    return 1;
}
ConnectorEnvelope connector_envelope(const char *line,size_t size,const char *token) {
    ConnectorEnvelope result={CONNECTOR_NONE,NULL,0};
    static const struct { const char *prefix;size_t length;ConnectorEnvelopeKind kind; } forms[]={
        {"CONNECTOR LINKBLOCK ",20,CONNECTOR_LINKBLOCK},
        {"CONNECTOR TRANSFER ",19,CONNECTOR_TRANSFER},
        {"CONNECTOR BLOCK ",16,CONNECTOR_BLOCK},
        {"CONNECTOR DATA ",15,CONNECTOR_DATA},
        {"CONNECTOR VIEW ",15,CONNECTOR_VIEW},
        {"CONNECTOR UPDATE ",17,CONNECTOR_UPDATE}
    };
    if(token && *token) {
        for(size_t i=0;i<sizeof(forms)/sizeof(forms[0]);i++) {
            size_t n=forms[i].length;
            if(size>n+65 && !memcmp(line,forms[i].prefix,n) &&
               !memcmp(line+n,token,64) && line[n+64]==' ') {
                result.kind=forms[i].kind;result.payload=line+n+65;
                result.size=size-n-65;return result;
            }
        }
        if(size==82 && !memcmp(line,"CONNECTOR LITERAL ",18) && !memcmp(line+18,token,64)) {
            result.kind=CONNECTOR_LITERAL;return result;
        }
        if(size==112 && !memcmp(line,"CONNECTOR SYNC ",15) &&
           !memcmp(line+15,token,64) && line[79]==' ' && wire_hex(line+80,32)) {
            result.kind=CONNECTOR_SYNC;result.payload=line+80;result.size=32;return result;
        }
    }
    if(size==79 && !memcmp(line,"CONNECTOR OPEN ",15) && wire_hex(line+15,64)) {
        result.kind=CONNECTOR_OPEN;result.payload=line+15;result.size=64;
    }
    return result;
}
int link_wire_command(const char *line) {
    return !strncmp(line,"LINKSNAP ",9) || !strncmp(line,"LINKSCAN ",9) ||
        !strncmp(line,"LINKPAGE ",9) || !strncmp(line,"LINKLOOKUP ",11) ||
        !strncmp(line,"LINKAPPLY ",10);
}
static int link_decode(const char *in,char *out,size_t capacity) {
    if(!strcmp(in,"-")) { out[0]=0;return 1; }
    size_t n=strlen(in);if(!n || n%4 || n>4*((capacity-1+2)/3))return 0;
    unsigned char *raw=malloc(n/4*3+1);if(!raw)return 0;
    int length=EVP_DecodeBlock(raw,(const unsigned char *)in,(int)n);
    if(length>=0) { if(in[n-1]=='=')length--;if(n>1 && in[n-2]=='=')length--; }
    int ok=length>=0 && (size_t)length<capacity && !memchr(raw,0,(size_t)(length<0?0:length));
    if(ok) { memcpy(out,raw,(size_t)length);out[length]=0; }
    free(raw);return ok;
}
int link_wire_parse(char *line,LinkWireRequest *q,char *output,size_t capacity) {
    memset(q,0,sizeof(*q));
    if(!capacity)return 0;
    output[0]=0;
    if(!strncmp(line,"LINKSNAP ",9) || !strncmp(line,"LINKSCAN ",9) || !strncmp(line,"LINKPAGE ",9)) {
        const char *end;if(!wire_number(line+9,&q->cursor,&end) || *end)goto bad;q->action=line[4]=='P'?'B':line[5]=='N'?'S':'M';
    } else if(!strncmp(line,"LINKLOOKUP ",11)) {
        char extra;if(sscanf(line+11,"%64s %64s %c",q->origin,q->article,&extra)!=2)goto bad;q->action='L';
    } else if(!strncmp(line,"LINKAPPLY ",10)) {
        char *fields[10],*at=line+10;unsigned count=0;
        while(at && count<10) { fields[count++]=at;char *space=strchr(at,' ');if(space)*space++=0;at=space; }
        if(count!=9 || at || strlen(fields[0])!=64 || strlen(fields[1])!=64 || !wire_hex(fields[0],64) || !wire_hex(fields[1],64))goto bad;
        strcpy(q->origin,fields[0]);strcpy(q->article,fields[1]);const char *end;
        if(!wire_number(fields[2],&q->cursor,&end) || *end)goto bad;
        if(strcmp(fields[3],"0") && (!wire_number(fields[3],&q->parent,&end) || *end))goto bad;
        if(!*fields[4] || strlen(fields[4])>12)goto bad;
        for(const char *p=fields[4];*p;p++)if(*p<'0' || *p>'9')goto bad;
        q->timestamp=strtoull(fields[4],NULL,10);if(!q->timestamp || q->timestamp>253402300799ULL)goto bad;
        if(strlen(fields[5])!=1 || !strchr("PTDCW",fields[5][0]))goto bad;
        q->action=fields[5][0];
        if(!link_decode(fields[6],q->author,sizeof(q->author)) || !link_decode(fields[7],q->title,sizeof(q->title)) || !link_decode(fields[8],output,capacity))goto bad;
        if(!wire_safe_text(q->author) || !wire_safe_text(q->title))goto bad;
        for(const unsigned char *p=(const unsigned char *)output;*p;p++)if((*p<32 && *p!='\r' && *p!='\n' && *p!='\t') || *p==127)goto bad;
    } else goto bad;
    return 1;
bad:memset(q,0,sizeof(*q));output[0]=0;return 0;
}
int connector_state_format(const ConnectorState *q,const char *nonce,char *out,size_t capacity) {
    if(!capacity)return 0;
    int n=snprintf(out,capacity,"CONNECTOR STATE %.32s {\"version\":1,\"transfer_block\":65536,\"core_views\":1,\"core_updates\":1,\"core_conference\":1,\"core_search\":1,\"core_discovery\":1,\"core_operations\":1,\"input_limit\":%u,\"connection\":%lu,\"user\":%u,\"username\":\"%s\",\"board\":%u,\"article\":%u,\"admin\":%d,\"sigop\":%d,\"level\":%u,\"editing\":%d,\"editor_kind\":%u,\"secret\":%d,\"password_mode\":%d,\"mail\":%d,\"mail_current\":%u,\"chat\":%d,\"talk\":%d,\"pager\":%d,\"gather\":%d,\"input\":%d,\"confirmation\":%u,\"binary\":%d,\"door\":%d,\"members\":%d,\"features\":{\"mail\":%d,\"chat\":%d,\"vault\":%d}}\r\n",
        nonce,q->input_limit,q->connection,q->user,q->username,q->board,q->article,q->admin,q->sigop,q->level,
        q->editing,q->editor_kind,q->secret,q->password_mode,q->mail,q->mail_current,q->chat,q->talk,
        q->pager,q->gather,q->input,q->confirmation,q->binary,q->door,q->members,
        q->feature_mail,q->feature_chat,q->feature_vault);
    if(n<0 || (size_t)n>=capacity) { out[0]=0;return 0; }
    return 1;
}

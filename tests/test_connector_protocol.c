#include "connector_protocol.h"
#include "content_limits.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
static const char token[]="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char nonce[]="0123456789abcdef0123456789abcdef";
static void envelopes(void) {
    const char *names[]={"LINKBLOCK","TRANSFER","BLOCK","DATA","VIEW","UPDATE","LITERAL","OPEN","SYNC"};
    ConnectorEnvelopeKind kinds[]={CONNECTOR_LINKBLOCK,CONNECTOR_TRANSFER,CONNECTOR_BLOCK,CONNECTOR_DATA,CONNECTOR_VIEW,CONNECTOR_UPDATE,CONNECTOR_LITERAL,CONNECTOR_OPEN,CONNECTOR_SYNC};
    char line[256];
    for(size_t i=0;i<sizeof(kinds)/sizeof(kinds[0]);i++) {
        int payload=i<6;
        const char *suffix=payload?" payload":i==8?" 0123456789abcdef0123456789abcdef":"";
        snprintf(line,sizeof(line),"CONNECTOR %s %s%s",names[i],token,suffix);
        size_t size=strlen(line);ConnectorEnvelope e=connector_envelope(line,size,token);
        assert(e.kind==kinds[i]);
        if(payload)assert(e.size==7 && !memcmp(e.payload,"payload",7));
        for(size_t n=0;n<size;n++) {
            e=connector_envelope(line,n,token);
            if(!payload || n<=size-7)assert(e.kind==CONNECTOR_NONE);
        }
        if(kinds[i]!=CONNECTOR_OPEN) {
            assert(connector_envelope(line,size,"").kind==CONNECTOR_NONE);
            char other[65];memset(other,'f',64);other[64]=0;
            assert(connector_envelope(line,size,other).kind==CONNECTOR_NONE);
        }
        if(!payload)assert(connector_envelope(line,size+1,token).kind==CONNECTOR_NONE);
    }
    snprintf(line,sizeof(line),"CONNECTOR OPEN %s",token);line[15]='A';
    assert(connector_envelope(line,79,token).kind==CONNECTOR_NONE);
    snprintf(line,sizeof(line),"CONNECTOR SYNC %s %s",token,nonce);line[80]='g';
    assert(connector_envelope(line,112,token).kind==CONNECTOR_NONE);
    assert(connector_envelope("",0,NULL).kind==CONNECTOR_NONE);
}
static int parse(const char *input,LinkWireRequest *q,char *body,size_t cap) {
    char *line=malloc(strlen(input)+1);assert(line);strcpy(line,input);
    int ok=link_wire_parse(line,q,body,cap);free(line);return ok;
}
static void links(void) {
    LinkWireRequest q;char body[128],line[1024];
    const char *commands[]={"LINKSNAP 1","LINKSCAN 2147483647","LINKPAGE 001","LINKLOOKUP origin article"};
    const char actions[]="SMBL";
    for(size_t i=0;i<4;i++) { assert(link_wire_command(commands[i]));assert(parse(commands[i],&q,body,sizeof(body)));assert(q.action==actions[i] && !*body); }
    const char *bad[]={"LINKSNAP 0","LINKSCAN 2147483648","LINKPAGE -1","LINKPAGE 1 ","LINKSNAP +1","LINKLOOKUP a b c","LINKAPPLY x","LINK"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) { assert(!parse(bad[i],&q,body,sizeof(body)));assert(!q.action && !*body); }
    for(const char *a="PTDCW";*a;a++) {
        snprintf(line,sizeof(line),"LINKAPPLY %s %s 7 2 253402300799 %c YWxpY2U= 5pel5pys6Kqe YQ0KYgk=",token,token,*a);
        assert(parse(line,&q,body,sizeof(body)) && q.action==*a && q.cursor==7 && q.parent==2 && q.timestamp==253402300799ULL);
        assert(!strcmp(q.author,"alice") && !strcmp(q.title,"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") && !strcmp(body,"a\r\nb\t"));
    }
    const char *tails[]={
        "7 0 1 P - - -", /* empty strings remain accepted */
        "7 0 0 P - - -","7 0 253402300800 P - - -","7 0 1 X - - -",
        "7 0 1 P YQA= - -","7 0 1 P YQo= - -","7 0 1 P - fw== -",
        "7 0 1 P - - Gw==","7 0 1 P - - ????","7 0 1 P - - YQ=",
        "7 00 1 P - - -","7 0 1 P - - - extra","7 0 1 P - - - "};
    for(size_t i=0;i<sizeof(tails)/sizeof(tails[0]);i++) {
        snprintf(line,sizeof(line),"LINKAPPLY %s %s %s",token,token,tails[i]);
        int ok=parse(line,&q,body,sizeof(body));assert(ok==(i==0));
        if(!ok)assert(!q.action && !q.origin[0] && !body[0]);
    }
    snprintf(line,sizeof(line),"LINKAPPLY %s %s 1 0 1 P - - YWI=",token,token);
    assert(!parse(line,&q,body,2) && !body[0]);assert(parse(line,&q,body,3) && !strcmp(body,"ab"));
    line[10]='A';assert(!parse(line,&q,body,sizeof(body)));
    assert(!parse("LINKSNAP 1",&q,body,0));
}
static void large_body(void) {
    size_t size=CONTENT_BODY;unsigned char *raw=malloc(size+2);
    char *encoded=malloc(4*((size+3)/3)+1),*line=malloc(4*((size+3)/3)+256),*out=malloc(size+1);
    assert(raw && encoded && line && out);memset(raw,'x',size+1);LinkWireRequest q;
    for(size_t n=size;n<=size+1;n++) {
        EVP_EncodeBlock((unsigned char *)encoded,raw,(int)n);
        sprintf(line,"LINKAPPLY %s %s 1 0 1 P - - %s",token,token,encoded);
        int ok=link_wire_parse(line,&q,out,size+1);assert(ok==(n==size));
        if(ok)assert(strlen(out)==size && !memcmp(out,raw,size));else assert(!q.action && !out[0]);
    }
    free(raw);free(encoded);free(line);free(out);
}
static void snapshot(void) {
    ConnectorState q={.connection=77,.username="alice",.user=2,.board=3,.article=4,.admin=1,.sigop=1,.level=5,.input_limit=4096,
        .editing=1,.editor_kind=109,.secret=1,.password_mode=2,.mail=1,.mail_current=8,.chat=1,.talk=1,
        .pager=1,.gather=1,.input=6,.confirmation=7,.binary=1,.door=1,.members=1,
        .feature_mail=1,.feature_chat=0,.feature_vault=1};
    const char *expected="CONNECTOR STATE 0123456789abcdef0123456789abcdef {\"version\":1,\"transfer_block\":65536,\"core_views\":1,\"core_updates\":1,\"core_conference\":1,\"core_search\":1,\"core_discovery\":1,\"core_operations\":1,\"input_limit\":4096,\"connection\":77,\"user\":2,\"username\":\"alice\",\"board\":3,\"article\":4,\"admin\":1,\"sigop\":1,\"level\":5,\"editing\":1,\"editor_kind\":109,\"secret\":1,\"password_mode\":2,\"mail\":1,\"mail_current\":8,\"chat\":1,\"talk\":1,\"pager\":1,\"gather\":1,\"input\":6,\"confirmation\":7,\"binary\":1,\"door\":1,\"members\":1,\"features\":{\"mail\":1,\"chat\":0,\"vault\":1}}\r\n";
    char out[2048];assert(connector_state_format(&q,nonce,out,sizeof(out)) && !strcmp(out,expected));
    size_t n=strlen(expected);
    assert(connector_state_format(&q,nonce,out,n+1));
    for(size_t cap=1;cap<=n;cap++)assert(!connector_state_format(&q,nonce,out,cap) && !out[0]);
    assert(!connector_state_format(&q,nonce,out,0));
}
int main(void) {
    envelopes();links();large_body();snapshot();
    puts("PASS: connector envelopes, token/length rejection, link operations and malformed payloads, 256KiB boundary, exact state snapshot and capacity");
    return 0;
}

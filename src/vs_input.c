#include "vs_input.h"
#include <stddef.h>
#include <string.h>

static unsigned char ascii(unsigned char c) {
    return c>='a' && c<='z'?(unsigned char)(c-'a'+'A'):c;
}
static unsigned char letter(const unsigned char **text) {
    const unsigned char *p=*text;
    unsigned char c=*p++;
    if(c==0xef && *p && p[1]) {
        if(p[0]==0xbc && p[1]>=0xa1 && p[1]<=0xba) { c=(unsigned char)('A'+p[1]-0xa1);p+=2; }
        else if(p[0]==0xbd && p[1]>=0x81 && p[1]<=0x9a) { c=(unsigned char)('A'+p[1]-0x81);p+=2; }
    }
    *text=p;
    return ascii(c);
}

VsCommand vs_command(char *line) {
    VsCommand out={0};
    const unsigned char *p=(const unsigned char *)line+strspn(line," \t");
    size_t n=0;
    while(*p && *p!=' ' && *p!='\t') {
        unsigned char c=letter(&p);
        if(c>=128 || n==sizeof(out.key)-1) { out.key[0]=0;out.argument=line;return out; }
        out.key[n++]=(char)ascii(c);
    }
    out.argument=(char *)p+strspn((char *)p," \t");
    return out;
}

static int token(const char **p,char *out,size_t capacity) {
    size_t n=strcspn(*p,";");
    if(!n || n>=capacity)return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)(*p)[i]<32 || (*p)[i]==127)return 0;
    memcpy(out,*p,n);out[n]=0;*p+=n;
    if(**p)(*p)++;
    return 1;
}

VsDirect vs_direct(const char *line) {
    VsDirect out={0};
    const unsigned char *command=(const unsigned char *)line+strspn(line," \t");
    if(letter(&command)!='D')return out;
    int separated=*command==';';
    if(separated)command++;
    unsigned char key=letter(&command);
    if(key=='J') { out.kind=VS_DIRECT_JUMP;return out; }
    if(key!='W' && key!='R')return out;
    const char *p=(const char *)command;
    if(separated && *p++!=';')return out;
    if(!token(&p,out.board,sizeof(out.board)))return out;
    if(key=='R') {
        char id[12];
        if(!token(&p,id,sizeof(id)))return out;
        for(const char *d=id;*d;d++) {
            if(*d<'0' || *d>'9' || out.parent>(2147483646u-(unsigned)(*d-'0'))/10)return (VsDirect){0};
            out.parent=out.parent*10+(unsigned)(*d-'0');
        }
        if(!out.parent)return (VsDirect){0};
        if(!(ascii((unsigned char)p[0])=='Y' && p[1]==0) && !token(&p,out.title,sizeof(out.title)))return (VsDirect){0};
    } else if(!token(&p,out.title,sizeof(out.title)))return (VsDirect){0};
    if(ascii((unsigned char)*p)!='Y' || p[1])return (VsDirect){0};
    out.kind=key=='W'?VS_DIRECT_BASE:VS_DIRECT_REPLY;
    return out;
}
